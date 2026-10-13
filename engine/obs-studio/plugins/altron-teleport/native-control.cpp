#include "native-control.hpp"
#include <obs-module.h>
#include <obs-frontend-api.h>
#include <graphics/matrix4.h>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QNetworkReply>
#include <QPushButton>
#include <QTcpSocket>
#include <QSaveFile>
#include <QUrlQuery>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <memory>
#include <vector>

namespace {
constexpr const char *nativeKind = "pw_altron_teleport_filter";
constexpr const char *soundKind = "pw_altron_teleport_audio";
constexpr const char *soundName = "Altron Teleport Audio";
QPointer<AltronControl> runtime;
bool frontendRegistered = false;
obs_module_t *nativeModule = nullptr;
void require(bool condition, const QString &message) { if (!condition) throw std::runtime_error(message.toStdString()); }
QString name(obs_source_t *source) { return source ? QString::fromUtf8(obs_source_get_name(source)) : QString(); }
struct Source {
  obs_source_t *p;
  explicit Source(const QString &n) : p(obs_get_source_by_name(n.toUtf8().constData())) {}
  explicit Source(obs_source_t *s) : p(s) {}
  ~Source() { obs_source_release(p); }
  Source(const Source &) = delete;
};
struct Data {
  obs_data_t *p;
  explicit Data(obs_data_t *d = nullptr) : p(d ? d : obs_data_create()) {}
  ~Data() { obs_data_release(p); }
};
QString error(const std::exception &e) { return QString::fromUtf8(e.what()); }
void appendStage(const QString &path, const QString &stage, const QString &scene)
{
  if (path.isEmpty()) return;
  QJsonArray stages;
  QFile file(path);
  if (file.exists()) {
    require(file.open(QIODevice::ReadOnly), "Cannot read the Stage catalogue; no Stage was changed.");
    auto doc = QJsonDocument::fromJson(file.readAll());
    require(doc.isArray(), "The Stage catalogue is invalid; no Stage was changed.");
    stages = doc.array();
    file.close();
  }
  for (auto value : stages) {
    auto object = value.toObject();
    if (object.value("name").toString().compare(stage, Qt::CaseInsensitive) == 0) {
      require(object.value("horizontal").toString() == scene,
        "This Stage name is already used by another scene. Existing Stage assignments were not changed.");
      return;
    }
  }
  QJsonObject assignments;
  for (QString provider : {"twitch", "youtube", "kick", "recording"})
    assignments.insert(provider + "_horizontal", QJsonObject{{"canvas","horizontal"},{"scene",scene},{"excluded",QJsonArray{}}});
  // Keep every existing Stage object intact; only append our new dedicated Stage.
  stages.append(QJsonObject{{"name",stage},{"horizontal",scene},{"vertical",""},
    {"horizontalTransition","cut"},{"verticalTransition","cut"},{"horizontalDurationMs",0},
    {"verticalDurationMs",0},{"assignments",assignments},{"createdBy","altron-native"}});
  require(QDir().mkpath(QFileInfo(path).absolutePath()), "Cannot create the Stage settings folder.");
  QSaveFile out(path);
  auto bytes = QJsonDocument(stages).toJson(QJsonDocument::Indented);
  require(out.open(QIODevice::WriteOnly) && out.write(bytes) == bytes.size() && out.commit(), "Could not save the new Stage.");
}
}

QJsonObject AltronControl::normalize(const QJsonObject &input)
{
  QJsonObject result{{"backend","pulse-weaver"},{"stageName","PW White Teleport"},
    {"programScene","PW White Teleport"},{"actorScene","PW Teleport Actor"},
    {"cameraSource","PW Teleport AI Camera"},{"filterName","White Ranger Teleport"}};
  if (input.contains("backend")) {
    QString backend = input.value("backend").toString();
    require(backend == "obs" || backend == "pulse-weaver", "Choose OBS or Pulse Weaver.");
    result.insert("backend",backend);
  }
  for (auto key : {"stageName","programScene","actorScene","cameraSource","filterName"}) if (input.contains(key)) {
    QString value = input.value(key).toString().trimmed();
    require(!value.isEmpty() && value.size() <= 200, QString("Invalid ") + key);
    result.insert(key,value);
  }
  require(result.value("actorScene") != result.value("programScene") && result.value("cameraSource") != result.value("actorScene"), "Use separate dedicated camera, actor and show helpers.");
  return result;
}

AltronControl::AltronControl(AltronHost value, QObject *parent, quint16 port) : QObject(parent), host(std::move(value)), config(normalize({}))
{
  requestedPort=port;
  cueId = QDateTime::currentMSecsSinceEpoch() * 1000;
  QFile settings(host.settingsPath);
  if (!host.settingsPath.isEmpty() && settings.open(QIODevice::ReadOnly)) {
    try {
      auto saved = QJsonDocument::fromJson(settings.readAll()).object();
      config = normalize(saved.value("bridge").toObject());
      int p = saved.value("port").toInt(8766); if (p >= 1024 && p <= 65535) overlayPort = p;
    } catch (...) {}
  }
  server.listen(QHostAddress::LocalHost,port);
  connect(&server,&QTcpServer::newConnection,this,[this] { accept(); });
  connect(&observer,&QTimer::timeout,this,[this] { observe(); });
  observer.start(750);
  if (!server.isListening()) last = {{"ok",false},{"status","The old Altron helper is using port 18767. Close it once; Pulse Weaver handles subsequent starts."}};
}

AltronControl::~AltronControl()
{
  ++generation;
  observer.stop(); server.close();
  if (busy) { try { cue("reset"); } catch (...) {} }
  for (auto *socket : findChildren<QTcpSocket *>()) socket->abort();
}

QJsonObject AltronControl::status() const
{
  auto value = last;
  value.insert("busy",busy); value.insert("configuration",config);
  value.insert("implementation","pulse-weaver-native"); value.insert("nodeRequired",false);
  value.insert("listening",listening()); return value;
}

void AltronControl::saveSettings()
{
  if (host.settingsPath.isEmpty()) return;
  require(QDir().mkpath(QFileInfo(host.settingsPath).absolutePath()), "Cannot save command-centre settings.");
  QSaveFile out(host.settingsPath);
  auto bytes = QJsonDocument(QJsonObject{{"port",overlayPort},{"listenPort",int(requestedPort)},{"bridge",config}}).toJson();
  require(out.open(QIODevice::WriteOnly) && out.write(bytes) == bytes.size() && out.commit(), "Cannot save command-centre settings.");
}

void AltronControl::reply(QTcpSocket *socket, int code, const QJsonObject &body)
{
  if (!socket || socket->state() != QAbstractSocket::ConnectedState) return;
  QByteArray data = QJsonDocument(body).toJson(QJsonDocument::Compact);
  socket->write("HTTP/1.1 " + QByteArray::number(code) + (code == 200 ? " OK" : " Error") +
    "\r\nContent-Type: application/json\r\nCache-Control: no-store\r\nConnection: close\r\nContent-Length: " + QByteArray::number(data.size()) + "\r\n\r\n" + data);
  socket->disconnectFromHost();
}

void AltronControl::accept()
{
  while (auto *socket = server.nextPendingConnection()) {
    if (openSockets >= 16) { socket->abort(); socket->deleteLater(); continue; }
    ++openSockets;
    auto buffer = std::make_shared<QByteArray>();
    auto timer = new QTimer(socket); timer->setSingleShot(true); timer->start(3000);
    connect(timer,&QTimer::timeout,socket,[socket] { socket->abort(); });
    connect(socket,&QTcpSocket::disconnected,this,[this,socket] { --openSockets; socket->deleteLater(); });
    connect(socket,&QTcpSocket::readyRead,this,[this,socket,buffer,timer] {
      if (socket->property("altronHandled").toBool()) { socket->readAll(); return; }
      buffer->append(socket->readAll());
      if (buffer->size() > 16384) { reply(socket,413,{{"ok",false},{"status","Request too large"}}); return; }
      int end = buffer->indexOf("\r\n\r\n"); if (end < 0) return;
      auto lines = buffer->left(end).split('\n'); auto first = lines.takeFirst().trimmed().split(' ');
      QHash<QByteArray,QByteArray> headers;
      for (auto line : lines) { int colon = line.indexOf(':'); if (colon > 0) headers.insert(line.left(colon).trimmed().toLower(),line.mid(colon+1).trimmed()); }
      bool valid = true; int length = headers.contains("content-length") ? headers.value("content-length").toInt(&valid) : 0;
      if (first.size() != 3 || !valid || length < 0 || length > 8192 || headers.contains("transfer-encoding")) { reply(socket,400,{{"ok",false},{"status","Invalid request"}}); return; }
      QByteArray expected = "127.0.0.1:" + QByteArray::number(port()), local = "localhost:" + QByteArray::number(port());
      if (headers.contains("origin") || (headers.value("host") != expected && headers.value("host") != local)) { reply(socket,403,{{"ok",false},{"status","Local application requests only"}}); return; }
      if (buffer->size() < end + 4 + length) return;
      socket->setProperty("altronHandled",true); timer->stop();
      QUrl url = QUrl::fromEncoded(first[1]); QString route = url.path();
      QJsonObject body;
      if (length) {
        QJsonParseError parse; auto doc = QJsonDocument::fromJson(buffer->mid(end+4,length),&parse);
        if (parse.error != QJsonParseError::NoError || !doc.isObject()) { reply(socket,400,{{"ok",false},{"status","Send a JSON object"}}); return; }
        body = doc.object();
      }
      try {
        if (first[0] == "GET" && route == "/status") { reply(socket,200,status()); return; }
        if (first[0] == "POST" && route == "/register") {
          auto next = body.contains("bridge") ? normalize(body.value("bridge").toObject()) : config;
          require(!busy || next == config, "Wait for teleport to finish before changing settings.");
          int p = body.contains("port") ? body.value("port").toInt(-1) : overlayPort;
          require(p >= 1024 && p <= 65535, "Choose a valid Lumia overlay port.");
          require(!busy || p == overlayPort, "Wait for teleport to finish before changing the overlay port.");
          auto old = config; int oldPort = overlayPort;
          config = next; overlayPort = p;
          if (old != config || oldPort != p) { try { saveSettings(); } catch (...) { config=old;overlayPort=oldPort;throw; } }
          reply(socket,200,{{"ok",true},{"implementation","pulse-weaver-native"}}); return;
        }
        QString action = route.mid(1); if (action == "teleport") action = "depart";
        const QStringList actions{"enter","arrive","out","depart","return","random","next","reset"};
        if (first[0] == "POST" && actions.contains(action)) {
          QString destination = body.value("destination").toString(QUrlQuery(url).queryItemValue("destination"));
          QPointer<QTcpSocket> client = socket;
          startCue(action,destination,[this,client](const QJsonObject &result,const QString &problem) { reply(client,problem.isEmpty()?200:409,result); });
          return;
        }
        reply(socket,404,{{"ok",false},{"status","Unknown route"}});
      } catch (const std::exception &e) { reply(socket,409,{{"ok",false},{"status",error(e)}}); }
    });
  }
}

void AltronControl::request(const QString &method, const QString &route, const QJsonObject &data, Result callback)
{
  QNetworkRequest req(QUrl(QString("http://127.0.0.1:%1/api/mission/%2").arg(overlayPort).arg(route)));
  req.setTransferTimeout(10000);
  req.setHeader(QNetworkRequest::ContentTypeHeader,"application/json");
  auto *response = method == "GET" ? network.get(req) : network.post(req,QJsonDocument(data).toJson(QJsonDocument::Compact));
  connect(response,&QNetworkReply::finished,this,[response,callback] {
    auto value = QJsonDocument::fromJson(response->readAll()).object();
    int code = response->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    QString problem;
    if (code < 200 || code >= 300 || response->error() != QNetworkReply::NoError) problem = value.value("error").toString(value.value("status").toString(response->errorString()));
    response->deleteLater(); callback(value,problem);
  });
}

void AltronControl::phase(const QString &type, const QJsonObject &data, quint64 token, std::function<void()> next)
{
  auto payload=data; payload.insert("type",type);
  request("POST","phase",payload,[this,token,next](const QJsonObject &,const QString &problem) {
    if (token != generation) return;
    if (!problem.isEmpty()) { fail(token,problem,completion); return; }
    try { next(); } catch (const std::exception &e) { fail(token,error(e),completion); }
  });
}

void AltronControl::cue(const QString &kind)
{
  Source actor(config.value("actorScene").toString());
  require(actor.p, "Set up the dedicated Altron show in Tools > Altron Command Centre first.");
  Source filter(obs_source_get_filter_by_name(actor.p,config.value("filterName").toString().toUtf8().constData()));
  require(filter.p && QString::fromUtf8(obs_source_get_unversioned_id(filter.p)) == nativeKind, "The dedicated Altron native filter is missing.");
  Data settings(obs_source_get_settings(filter.p));
  obs_data_set_string(settings.p,"cue",kind.toUtf8().constData());
  obs_data_set_int(settings.p,"cue_id",++cueId);
  obs_data_set_double(settings.p,"preview_time",-1);
  obs_source_update(filter.p,settings.p);
}

void AltronControl::ensureNative()
{
  Source actor(config.value("actorScene").toString()), show(config.value("programScene").toString());
  auto *actorScene = actor.p ? obs_scene_from_source(actor.p) : nullptr;
  auto *showScene = show.p ? obs_scene_from_source(show.p) : nullptr;
  require(actorScene && showScene && obs_scene_find_source(showScene,obs_source_get_name(actor.p)) &&
    obs_scene_find_source(actorScene,config.value("cameraSource").toString().toUtf8().constData()), "The dedicated Altron helpers are missing. Use Tools > Altron Command Centre.");
  const QString filterName = config.value("filterName").toString();
  Source old(obs_source_get_filter_by_name(actor.p,filterName.toUtf8().constData()));
  QString kind = old.p ? QString::fromUtf8(obs_source_get_unversioned_id(old.p)) : QString();
  Data owner(obs_source_get_private_settings(actor.p));
  require(kind == nativeKind || kind == "pw_white_teleport_filter" || obs_data_get_bool(owner.p,"altron_owned"), "This is not an Altron-owned helper. The source was not changed.");
  Source sound(QString::fromUtf8(soundName));
  require(!sound.p || QString::fromUtf8(obs_source_get_unversioned_id(sound.p)) == soundKind, "An unrelated source uses the teleport audio name.");
  if (!sound.p) {
    Data settings; obs_data_set_double(settings.p,"volume",70); obs_data_set_bool(settings.p,"monitor",false);
    Source audio(obs_source_create(soundKind,soundName,settings.p,nullptr));
    require(audio.p, "Cannot create native teleport audio.");
    obs_scene_add(showScene,audio.p);
  } else if (!obs_scene_find_source(showScene,soundName)) obs_scene_add(showScene,sound.p);
  if (kind == nativeKind) return;
  const QString backup = filterName + " (Lua backup)";
  Source existingBackup(obs_source_get_filter_by_name(actor.p,backup.toUtf8().constData()));
  require(!existingBackup.p, "A prior Lua backup exists. Inspect the dedicated helper before upgrading again.");
  Data settings(old.p ? obs_source_get_settings(old.p) : nullptr);
  obs_data_set_string(settings.p,"audio_source",soundName);
  obs_data_set_double(settings.p,"preview_time",-1);
  obs_data_set_int(settings.p,"cue_id",0);
  Source replacement(obs_source_create_private(nativeKind,(filterName+" (native setup)").toUtf8().constData(),settings.p));
  require(replacement.p, "Cannot create the native filter; the previous filter was preserved.");
  if (old.p) { obs_source_set_enabled(old.p,false); obs_source_set_name(old.p,backup.toUtf8().constData()); }
  obs_source_set_name(replacement.p,filterName.toUtf8().constData());
  obs_source_filter_add(actor.p,replacement.p);
}

QJsonObject AltronControl::setCamera(const QJsonObject &c)
{
  auto coordinate = [&](const char *key, double low, double high) {
    require(c.value(key).isDouble(),QString("Missing camera ")+key);
    double v=c.value(key).toDouble(); require(std::isfinite(v)&&v>=low&&v<=high,QString("Invalid camera ")+key); return v;
  };
  double x=coordinate("x",-.5,1),y=coordinate("y",-.5,1),w=coordinate("width",.05,1.5),h=coordinate("height",.05,1.5);
  obs_video_info video{}; require(obs_get_video_info(&video),"The video canvas is unavailable.");
  Source actor(config.value("actorScene").toString());
  auto *scene=actor.p?obs_scene_from_source(actor.p):nullptr;
  auto *item=scene?obs_scene_find_source(scene,config.value("cameraSource").toString().toUtf8().constData()):nullptr;
  require(item,"The Altron camera helper is missing.");
  bool bottom=c.value("anchor").toString().startsWith("bottom");
  obs_transform_info t{};obs_sceneitem_get_info2(item,&t);
  t.pos={float(x*video.base_width),float(y*video.base_height)};t.scale={1,1};
  t.alignment=OBS_ALIGN_LEFT|OBS_ALIGN_TOP;t.bounds_type=OBS_BOUNDS_SCALE_INNER;
  t.bounds={float(w*video.base_width),float(h*video.base_height)};t.bounds_alignment=bottom?OBS_ALIGN_BOTTOM:OBS_ALIGN_CENTER;
  obs_sceneitem_set_info2(item,&t);
  double fit=std::min(w,h);QJsonObject actual=visibleCamera();
  if(actual.isEmpty())actual={{"x",x+(w-fit)/2},{"y",y+(h-fit)*(bottom?1:.5)},{"width",fit},{"height",fit}};
  Source filter(obs_source_get_filter_by_name(actor.p,config.value("filterName").toString().toUtf8().constData()));
  require(filter.p,"The native teleport filter is missing.");
  Data settings(obs_source_get_settings(filter.p));
  obs_data_set_double(settings.p,"centre_x",actual.value("x").toDouble()+actual.value("width").toDouble()*.664);
  obs_data_set_double(settings.p,"centre_y",actual.value("y").toDouble()+actual.value("height").toDouble()*.64);
  obs_data_set_double(settings.p,"preview_time",-1);obs_source_update(filter.p,settings.p);
  return actual;
}

QJsonObject AltronControl::visibleCamera() const
{
  Source actor(config.value("actorScene").toString()); auto *scene=actor.p?obs_scene_from_source(actor.p):nullptr;
  auto *item=scene?obs_scene_find_source(scene,config.value("cameraSource").toString().toUtf8().constData()):nullptr;
  if(!item)return {};
  obs_video_info video{};if(!obs_get_video_info(&video)||!video.base_width||!video.base_height)return {};
  obs_sceneitem_crop crop{};obs_sceneitem_get_crop(item,&crop);
  auto *source=obs_sceneitem_get_source(item);
  double sw=std::max(0,int(obs_source_get_width(source))-crop.left-crop.right),sh=std::max(0,int(obs_source_get_height(source))-crop.top-crop.bottom);
  if(!sw||!sh)return {};
  obs_sceneitem_force_update_transform(item);
  matrix4 transform{};obs_sceneitem_get_draw_transform(item,&transform);
  double minx=1e12,miny=1e12,maxx=-1e12,maxy=-1e12;
  for(int i=0;i<4;++i){vec3 p{float(i&1?sw:0),float(i&2?sh:0),0},out{};vec3_transform(&out,&p,&transform);minx=std::min(minx,double(out.x));miny=std::min(miny,double(out.y));maxx=std::max(maxx,double(out.x));maxy=std::max(maxy,double(out.y));}
  return {{"x",minx/video.base_width},{"y",miny/video.base_height},{"width",(maxx-minx)/video.base_width},{"height",(maxy-miny)/video.base_height}};
}

void AltronControl::observe()
{
  if(!server.isListening()&&server.listen(QHostAddress::LocalHost,requestedPort))last={{"ok",true},{"status","ready"}};
  if(busy||observing)return;
  auto rect=visibleCamera();if(rect.isEmpty())return;
  observing=true;quint64 token=generation;
  request("GET","state",{},[this,token,rect](const QJsonObject &state,const QString &problem){
    if(!problem.isEmpty()||busy||token!=generation){observing=false;return;}
    QString key=QString::fromUtf8(QJsonDocument(rect).toJson(QJsonDocument::Compact));
    if(key==lastCamera&&state.value("cameraBox").toObject()==rect){observing=false;return;}
    request("POST","phase",{{"type","camera"},{"camera",rect}},[this,token,key](const QJsonObject &,const QString &failure){
      if(token==generation&&failure.isEmpty())lastCamera=key;observing=false;
    });
  });
}

void AltronControl::fail(quint64 token,const QString &problem,Result callback)
{
  if(token!=generation)return;
  try{if(host.currentScene()==config.value("programScene").toString())cue("reset");}catch(...){}
  last={{"ok",false},{"status",problem}};
  request("POST","phase",{{"type","fail"},{"error",problem}},[this,token,callback,problem](const QJsonObject &,const QString &){
    if(token!=generation)return;busy=false;auto result=last;completion={};callback(result,problem);
  });
}

void AltronControl::finish(quint64 token,const QString &message,const QString &target,Result callback)
{
  phase("finish",{},token,[this,token,message,target,callback]{
    if(token!=generation)return;busy=false;
    last={{"ok",true},{"status",message},{"destination",target},{"at",QDateTime::currentDateTimeUtc().toString(Qt::ISODate)}};
    completion={};callback(last,{});
  });
}

void AltronControl::startCue(const QString &action,const QString &destination,Result callback)
{
  require(!busy,"Teleport already running.");
  require(QStringList{"enter","arrive","out","depart","return","random","next","reset"}.contains(action),"Unknown cue.");
  if(action!="enter"&&action!="arrive"&&action!="reset")require(host.currentScene()==config.value("programScene").toString(),"Enter the command centre first.");
  ensureNative();busy=true;completion=callback;quint64 token=++generation;
  try{
    if((action=="enter"||action=="arrive")&&host.currentScene()!=config.value("programScene").toString()){
      cue("hide");host.select(config.value("backend").toString(),config.value("stageName").toString(),config.value("programScene").toString());
    }
  }catch(const std::exception &e){fail(token,error(e),callback);return;}
  request("GET","state",{},[this,token,action,destination,callback](const QJsonObject &state,const QString &problem){
    if(token!=generation)return;if(!problem.isEmpty()){fail(token,problem,callback);return;}
    QString current=state.value("current").toString("room");
    if(action=="reset"){
      request("GET","locations",{},[this,token,callback](const QJsonObject &poses,const QString &failure){
        if(token!=generation)return;if(!failure.isEmpty()){fail(token,failure,callback);return;}
        phase("reset",{{"id","room"}},token,[this,token,poses,callback]{
          auto camera=setCamera(poses.value("room").toObject());cue("reset");
          phase("camera",{{"camera",camera}},token,[this,callback]{busy=false;last={{"ok",true},{"status","reset"},{"destination","room"}};completion={};callback(last,{});});
        });
      });return;
    }
    if(action=="out"){
      phase("start",{{"kind","out"},{"to",current},{"duration",2700}},token,[this,token,current,callback]{cue("out");QTimer::singleShot(2750,this,[this,token,current,callback]{if(token==generation)finish(token,"departed",current,callback);});});return;
    }
    QString id=(action=="enter"||action=="return")?"room":destination;
    QJsonObject prepare{{"kind",action=="random"?"random":action=="next"?"next":"selected"}};
    if(!id.isEmpty()||action=="arrive")prepare.insert("id",id.isEmpty()?"room":id);
    request("POST","prepare",prepare,[this,token,action,current,callback](const QJsonObject &data,const QString &failure){
      if(token!=generation)return;if(!failure.isEmpty()){fail(token,failure,callback);return;}
      try{prepared(token,action,data,current,callback);}catch(const std::exception &e){fail(token,error(e),callback);}
    });
  });
}

void AltronControl::prepared(quint64 token,const QString &action,const QJsonObject &data,const QString &current,Result callback)
{
  QString target=data.value("id").toString();auto camera=data.value("camera").toObject();
  require(!target.isEmpty()&&!camera.isEmpty(),"Lumia did not provide a destination camera placement.");
  if(action=="enter"){
    phase("switch",{{"id","room"}},token,[this,token,camera,callback]{
      auto actual=setCamera(camera);cue("reset");host.select(config.value("backend").toString(),config.value("stageName").toString(),config.value("programScene").toString());
      phase("camera",{{"camera",actual}},token,[this,token,callback]{finish(token,"entered","room",callback);});
    });return;
  }
  if(action=="arrive"){
    cue("hide");phase("switch",{{"id",target}},token,[this,token,target,camera,callback]{arrived(token,target,camera,callback);});return;
  }
  require(current!=target,"Already at this destination: choose another stop.");
  phase("start",{{"kind","out"},{"to",target},{"duration",2700}},token,[this,token,target,camera,callback]{
    cue("out");QTimer::singleShot(2050,this,[this,token,target,camera,callback]{
      if(token!=generation)return;
      phase("switch",{{"id",target}},token,[this,token,target,camera,callback]{QTimer::singleShot(700,this,[this,token,target,camera,callback]{
        if(token!=generation)return;try{arrived(token,target,camera,callback);}catch(const std::exception &e){fail(token,error(e),callback);}
      });});
    });
  });
}

void AltronControl::arrived(quint64 token,const QString &target,const QJsonObject &camera,Result callback)
{
  auto actual=setCamera(camera);
  phase("camera",{{"camera",actual}},token,[this,token,target,callback]{
    phase("start",{{"kind","in"},{"to",target},{"duration",2600}},token,[this,token,target,callback]{
      cue("in");QTimer::singleShot(2750,this,[this,token,target,callback]{if(token==generation)finish(token,"arrived",target,callback);});
    });
  });
}

QJsonObject AltronControl::setup(const QString &cutout,const QString &alerts)
{
  require(!busy,"Wait for the teleport to finish before changing setup.");
  require(!host.outputsActive||!host.outputsActive(),"Stop active stream/recording outputs before creating a show.");
  const QString program=config.value("programScene").toString(),actor=config.value("actorScene").toString(),camera=config.value("cameraSource").toString();
  Source existingActor(actor),existingShow(program);
  if(existingActor.p&&existingShow.p){
    ensureNative();
    if(host.pulseWeaver)appendStage(host.stagePath,config.value("stageName").toString(),program);
    if(host.save)host.save();
    return {{"ok",true},{"status","Existing Altron helpers upgraded. Existing Stage definitions and original camera sources were preserved."}};
  }
  Source input(cutout),alert(alerts);
  require(input.p,"Choose a working background-removed camera scene or source.");
  require(alerts.isEmpty()||alert.p,"The chosen alert layer no longer exists.");
  const QString cameraName=config.value("cameraSource").toString();
  require(cutout!=actor&&cutout!=program&&cutout!=cameraName,"Choose an existing camera cutout, not an Altron helper.");
  QStringList names{program,actor,cameraName,QString::fromUtf8(soundName),"Altron Background"};
  for(const QString &n:names){Source conflict(n);require(!conflict.p,"An Altron helper name is already in use. No existing source was overwritten: "+n);}
  obs_video_info video{};require(obs_get_video_info(&video)&&std::abs(double(video.base_width)/video.base_height-16.0/9)<.01,"Use a 16:9 base canvas for the landscape command centre.");
  bool browserAvailable=false;const char *id=nullptr;for(size_t i=0;obs_enum_input_types(i,&id);++i)if(!std::strcmp(id,"browser_source"))browserAvailable=true;
  require(browserAvailable,"The browser-source module is unavailable.");
  std::vector<obs_source_t*> made;
  try{
    auto scene=[&](const QString &n){auto *s=obs_scene_create(n.toUtf8().constData());require(s,"Could not create the show helper.");made.push_back(obs_scene_get_source(s));return s;};
    auto *cam=scene(cameraName);
    auto *item=obs_scene_add(cam,input.p);require(item,"Could not reuse the camera cutout.");
    obs_transform_info transform{};obs_sceneitem_get_info2(item,&transform);
    transform.pos={0,0};transform.scale={1,1};transform.alignment=OBS_ALIGN_LEFT|OBS_ALIGN_TOP;
    transform.bounds_type=OBS_BOUNDS_SCALE_INNER;transform.bounds={float(video.base_width),float(video.base_height)};
    obs_sceneitem_set_info2(item,&transform);
    auto *actorScene=scene(actor);obs_scene_add(actorScene,obs_scene_get_source(cam));
    Data owned(obs_source_get_private_settings(obs_scene_get_source(actorScene)));obs_data_set_bool(owned.p,"altron_owned",true);
    auto *show=scene(program);
    Data browser;obs_data_set_string(browser.p,"url",QString("http://127.0.0.1:%1/mission").arg(overlayPort).toUtf8().constData());
    obs_data_set_int(browser.p,"width",1920);obs_data_set_int(browser.p,"height",1080);obs_data_set_int(browser.p,"fps",30);obs_data_set_bool(browser.p,"shutdown",false);
    auto *room=obs_source_create("browser_source","Altron Background",browser.p,nullptr);require(room,"Cannot create the command-centre browser source.");made.push_back(room);
    auto *background=obs_scene_add(show,room);obs_sceneitem_get_info2(background,&transform);
    transform.pos={0,0};transform.scale={float(video.base_width/1920.0),float(video.base_height/1080.0)};transform.alignment=OBS_ALIGN_LEFT|OBS_ALIGN_TOP;
    obs_sceneitem_set_info2(background,&transform);
    obs_scene_add(show,obs_scene_get_source(actorScene));if(alert.p)obs_scene_add(show,alert.p);
    ensureNative();
    require(!host.outputsActive||!host.outputsActive(),"An output started during setup. The new helpers were rolled back.");
    if(host.pulseWeaver)appendStage(host.stagePath,config.value("stageName").toString(),program);
    if(host.save)host.save();
    for(auto *source:made)obs_source_release(source);
    return {{"ok",true},{"status","Created the dedicated Altron show and Stage. Original cameras and existing Stages were preserved."}};
  }catch(...){
    // Sources are solely new helpers; never remove the user's reused camera or alerts.
    for(auto it=made.rbegin();it!=made.rend();++it){obs_source_remove(*it);obs_source_release(*it);}
    throw;
  }
}

void AltronControl::showDialog()
{
  auto *window=static_cast<QWidget*>(obs_frontend_get_main_window());
  QDialog dialog(window);dialog.setWindowTitle("Altron Command Centre");dialog.resize(560,360);
  auto *layout=new QVBoxLayout(&dialog);
  auto *intro=new QLabel("Lumia and Lumi-Con connect automatically while Pulse Weaver is open. Create the dedicated show once, then use your existing Altron commands.");intro->setWordWrap(true);layout->addWidget(intro);
  auto *form=new QFormLayout;auto *camera=new QComboBox;auto *alerts=new QComboBox;alerts->addItem("No alert layer","");
  struct Lists{QComboBox *camera;QComboBox *alerts;};Lists lists{camera,alerts};
  auto add=[](void *opaque,obs_source_t *source){auto *l=static_cast<Lists*>(opaque);QString n=name(source);if((obs_source_get_type(source)==OBS_SOURCE_TYPE_INPUT||obs_source_get_type(source)==OBS_SOURCE_TYPE_SCENE)&&(obs_source_get_output_flags(source)&OBS_SOURCE_VIDEO)){l->camera->addItem(n,n);l->alerts->addItem(n,n);}return true;};
  obs_enum_sources(add,&lists);obs_enum_scenes(add,&lists);
  form->addRow("Existing camera cutout",camera);form->addRow("Alert layer",alerts);layout->addLayout(form);
  auto *create=new QPushButton("Create / use Altron show");layout->addWidget(create);
  auto *message=new QLabel;message->setWordWrap(true);layout->addWidget(message);
  connect(create,&QPushButton::clicked,&dialog,[this,camera,alerts,message]{try{auto result=setup(camera->currentData().toString(),alerts->currentData().toString());message->setText(result.value("status").toString());}catch(const std::exception &e){message->setText(error(e));}});
  auto *row=new QHBoxLayout;
  for(auto pair:std::initializer_list<std::pair<const char*,const char*>>{{"Enter","enter"},{"Teleport in","arrive"},{"Teleport out","out"},{"Reset","reset"}}){
    auto *button=new QPushButton(pair.first);row->addWidget(button);QString action=pair.second;
    connect(button,&QPushButton::clicked,&dialog,[this,action,message]{try{QPointer<QLabel> label=message;startCue(action,"room",[label](const QJsonObject &result,const QString &problem){if(label)label->setText(problem.isEmpty()?result.value("status").toString():problem);});}catch(const std::exception &e){message->setText(error(e));}});
  }
  layout->addLayout(row);
  auto *sounds=new QPushButton("Choose arrival / departure sounds…");layout->addWidget(sounds);
  connect(sounds,&QPushButton::clicked,&dialog,[message,&dialog]{Source sound(QString::fromUtf8(soundName));if(sound.p){dialog.accept();obs_frontend_open_source_properties(sound.p);}else message->setText("Create / use the Altron show first.");});
  auto *state=new QLabel;state->setWordWrap(true);layout->addWidget(state);
  QTimer update(&dialog);connect(&update,&QTimer::timeout,&dialog,[this,state]{state->setText(listening()?QString("Built-in Lumia control: ready · %1\n%2").arg(busy?"teleport running":"idle").arg(config.value("programScene").toString()):last.value("status").toString());});update.start(500);
  auto *done=new QDialogButtonBox(QDialogButtonBox::Close);connect(done,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);layout->addWidget(done);dialog.exec();
}

namespace {
void startNative()
{
  if(runtime||!qobject_cast<QApplication*>(QCoreApplication::instance()))return;
  auto *window=static_cast<QWidget*>(obs_frontend_get_main_window());if(!window)return;
  AltronHost host;
  host.pulseWeaver=window->findChild<QComboBox*>("PulseWeaverStageSelector")!=nullptr;
  char *file=obs_module_get_config_path(nativeModule,"command-centre.json");host.settingsPath=QString::fromUtf8(file?file:"");bfree(file);
  if(host.pulseWeaver)host.stagePath=QDir::cleanPath(QFileInfo(host.settingsPath).dir().filePath("../../pulseweaver-stages.json"));
  host.currentScene=[] {Source scene(obs_frontend_get_current_scene());return name(scene.p);};
  host.outputsActive=[] {bool active=false;obs_enum_outputs([](void *p,obs_output_t *output){if(obs_output_active(output))*static_cast<bool*>(p)=true;return true;},&active);return active;};
  host.save=[] {obs_frontend_save();};
  QPointer<QWidget> main=window;
  host.select=[main](const QString &backend,const QString &stage,const QString &scene){
    require(main,"Pulse Weaver is closing.");
    if(backend=="obs"){Source target(scene);require(target.p,"The dedicated show scene does not exist.");obs_frontend_set_current_scene(target.p);return;}
    auto *selector=main->findChild<QComboBox*>("PulseWeaverStageSelector");require(selector,"Choose OBS scene mode when running in standard OBS.");
    int target=-1;for(int i=0;i<selector->count();++i){QString n=selector->itemData(i,Qt::UserRole+1).toString();if(n.isEmpty())n=selector->itemText(i);if(n==stage){target=i;break;}}
    require(target>=0,"The Altron Stage is not ready. Create / use it in Tools > Altron Command Centre, then retry.");
    if(selector->currentIndex()==target){
      Source current(obs_frontend_get_current_scene());
      if(name(current.p)!=scene)QMetaObject::invokeMethod(selector,"currentIndexChanged",Qt::DirectConnection,Q_ARG(int,target));
    }else selector->setCurrentIndex(target);
  };
  quint16 port=18767;
  QFile settings(host.settingsPath);
  if(settings.open(QIODevice::ReadOnly)){int p=QJsonDocument::fromJson(settings.readAll()).object().value("listenPort").toInt(18767);if(p>=1024&&p<=65535)port=quint16(p);}
  runtime=new AltronControl(std::move(host),window,port);
  auto *action=static_cast<QAction*>(obs_frontend_add_tools_menu_qaction("Altron Command Centre"));
  if(action)QObject::connect(action,&QAction::triggered,runtime,[] {if(runtime)runtime->showDialog();});
  blog(LOG_INFO,"[Altron] Built-in command centre control %s; no Node/Lua bridge process",runtime->listening()?"ready":"port unavailable");
}
void frontendEvent(obs_frontend_event event,void *)
{
  if(event==OBS_FRONTEND_EVENT_FINISHED_LOADING)startNative();
  if(event==OBS_FRONTEND_EVENT_SCRIPTING_SHUTDOWN||event==OBS_FRONTEND_EVENT_EXIT){delete runtime;runtime=nullptr;}
}
}

void altronNativeLoad(obs_module_t *module)
{
  nativeModule=module;
  if(qobject_cast<QApplication*>(QCoreApplication::instance())){obs_frontend_add_event_callback(frontendEvent,nullptr);frontendRegistered=true;}
}
void altronNativeUnload()
{
  if(frontendRegistered){obs_frontend_remove_event_callback(frontendEvent,nullptr);frontendRegistered=false;}
  delete runtime;runtime=nullptr;
}
