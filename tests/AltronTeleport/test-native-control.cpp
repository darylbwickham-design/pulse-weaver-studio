#define NOMINMAX
#include <windows.h>
#include "native-control.hpp"
#include <obs-module.h>
#include <util/platform.h>
#include <QCoreApplication>
#include <QEventLoop>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QThread>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <fstream>

static void check(bool ok,const char *message){if(!ok){std::fprintf(stderr,"FAIL: %s\n",message);std::exit(2);}}
static void wait(int ms){QEventLoop loop;QTimer::singleShot(ms,&loop,&QEventLoop::quit);loop.exec();}
struct Response{int code;QJsonObject data;};
static Response result(QNetworkReply *r){QEventLoop loop;QTimer timeout;timeout.setSingleShot(true);QObject::connect(r,&QNetworkReply::finished,&loop,&QEventLoop::quit);QObject::connect(&timeout,&QTimer::timeout,&loop,&QEventLoop::quit);timeout.start(14000);if(!r->isFinished())loop.exec();check(r->isFinished(),"HTTP request completed");Response v{r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(),QJsonDocument::fromJson(r->readAll()).object()};r->deleteLater();return v;}
static QNetworkReply *post(QNetworkAccessManager &n,const QString &url,const QJsonObject &body){QNetworkRequest request{QUrl(url)};request.setHeader(QNetworkRequest::ContentTypeHeader,"application/json");return n.post(request,QJsonDocument(body).toJson(QJsonDocument::Compact));}
static void module(const QString &dll,const QString &data){obs_module_t *m=nullptr;check(obs_open_module(&m,dll.toUtf8().constData(),data.toUtf8().constData())==MODULE_SUCCESS,"module opens");check(obs_init_module(m),"module initializes");}
static std::atomic<float> audioPeak{0};
static void audio(void *,size_t,audio_data *d){auto *samples=reinterpret_cast<float*>(d->data[0]);float p=0;for(uint32_t i=0;i<d->frames;++i)p=std::max(p,std::abs(samples[i]));float old=audioPeak;while(p>old&&!audioPeak.compare_exchange_weak(old,p)){};}
static void video(void *,video_data *){}
static void render(void *,gs_effect_t *){auto *e=obs_get_base_effect(OBS_EFFECT_SOLID);vec4 colour{.12f,.55f,.9f,1};gs_effect_set_vec4(gs_effect_get_param_by_name(e,"color"),&colour);while(gs_effect_loop(e,"Solid")){gs_matrix_push();gs_matrix_translate3f(240,70,0);gs_draw_sprite(nullptr,0,150,260);gs_matrix_pop();}}
static void log(int level,const char *f,va_list args,void *){if(level<=LOG_WARNING){vfprintf(stderr,f,args);fputc('\n',stderr);}}
static void tone(const QString &path){std::ofstream out(path.toStdString(),std::ios::binary);auto put=[&](const auto &v){out.write(reinterpret_cast<const char*>(&v),sizeof(v));};uint32_t bytes=38400*2,size=bytes+36,fmt=16,rate=48000,speed=96000;uint16_t format=1,channels=1,align=2,bits=16;out.write("RIFF",4);put(size);out.write("WAVEfmt ",8);put(fmt);put(format);put(channels);put(rate);put(speed);put(align);put(bits);out.write("data",4);put(bytes);for(int i=0;i<38400;++i){int16_t value=int16_t(std::sin(i*440*6.283185307/48000)*16000);put(value);}}

int main(int argc,char **argv)
{
  check(argc==3,"usage: test app-root native-runtime-root");QString app=QString::fromLocal8Bit(argv[1]),native=QString::fromLocal8Bit(argv[2]);
  SetDllDirectoryA((app+"/bin/64bit").toLocal8Bit().constData());QCoreApplication application(argc,argv);base_set_log_handler(log,nullptr);
  check(obs_startup("en-US",nullptr,nullptr),"libobs startup");obs_add_data_path((app+"/data/libobs/").toUtf8().constData());
  obs_audio_info ai{48000,SPEAKERS_STEREO};check(obs_reset_audio(&ai),"audio initialization");
  auto graphics=(app+"/bin/64bit/libobs-d3d11.dll").toUtf8();obs_video_info vi{};vi.graphics_module=graphics.constData();vi.fps_num=60;vi.fps_den=1;vi.base_width=vi.output_width=640;vi.base_height=vi.output_height=360;vi.output_format=VIDEO_FORMAT_RGBA;vi.colorspace=VIDEO_CS_709;vi.range=VIDEO_RANGE_FULL;
  check(obs_reset_video(&vi)==OBS_VIDEO_SUCCESS,"D3D11 initializes");
  module(app+"/obs-plugins/64bit/obs-ffmpeg.dll",app+"/data/obs-plugins/obs-ffmpeg");
  module(native+"/obs-plugins/64bit/altron-teleport.dll",native+"/data/obs-plugins/altron-teleport");obs_post_load_modules();
  obs_source_info fake{};fake.id="altron_control_test_camera";fake.type=OBS_SOURCE_TYPE_INPUT;fake.output_flags=OBS_SOURCE_VIDEO|OBS_SOURCE_CUSTOM_DRAW;
  fake.get_name=[](void *){return "Test cutout";};fake.create=[](obs_data_t *,obs_source_t *)->void*{return reinterpret_cast<void*>(1);};fake.destroy=[](void *){};fake.get_width=[](void *){return 640u;};fake.get_height=[](void *){return 360u;};fake.video_render=render;obs_register_source(&fake);
  fake.id="browser_source";fake.get_name=[](void *){return "Test browser";};fake.get_width=[](void *){return 1920u;};fake.get_height=[](void *){return 1080u;};fake.video_render=[](void *,gs_effect_t *){};obs_register_source(&fake);
  auto *cutout=obs_source_create("altron_control_test_camera","Original Cutout",nullptr,nullptr);
  auto *original=obs_scene_create_private("Original Scene");auto *originalItem=obs_scene_add(original,cutout);
  obs_transform_info baseline{};obs_sceneitem_get_info2(originalItem,&baseline);obs_set_output_source(0,obs_scene_get_source(original));
  QString current="Original Scene";int selected=0,saves=0;QTemporaryDir temp;check(temp.isValid(),"isolated settings directory");
  QString stagesPath=temp.path()+"/stages.json";QJsonObject existing{{"name","Existing Stage"},{"horizontal","Original Scene"},{"extra",QJsonObject{{"keep",true}}}};
  {QFile f(stagesPath);check(f.open(QIODevice::WriteOnly),"stage fixture opens");f.write(QJsonDocument(QJsonArray{existing}).toJson());}
  QTcpServer overlay;check(overlay.listen(QHostAddress::LocalHost,0),"Lumia mock listening");QString location="room";QJsonObject cameraBox;QStringList events;
  QObject::connect(&overlay,&QTcpServer::newConnection,&application,[&]{while(auto *s=overlay.nextPendingConnection()){
    auto buffer=std::make_shared<QByteArray>();QObject::connect(s,&QTcpSocket::disconnected,s,&QObject::deleteLater);
    QObject::connect(s,&QTcpSocket::readyRead,&application,[&,s,buffer]{buffer->append(s->readAll());int end=buffer->indexOf("\r\n\r\n");if(end<0)return;int length=0;auto lines=buffer->left(end).split('\n');for(auto l:lines)if(l.toLower().startsWith("content-length:"))length=l.mid(l.indexOf(':')+1).trimmed().toInt();if(buffer->size()<end+4+length)return;auto first=lines[0].trimmed().split(' ');QString route=QString::fromLatin1(first[1]).section('/',-1);auto body=QJsonDocument::fromJson(buffer->mid(end+4,length)).object();QJsonObject response{{"ok",true}};
      if(route=="state")response={{"current",location},{"cameraBox",cameraBox}};
      if(route=="prepare"){QString kind=body.value("kind").toString(),id=body.value("id").toString("moon");if(kind=="next"||kind=="random")id=location=="moon"?"freezer":"moon";response={{"id",id},{"camera",QJsonObject{{"x",id=="room"?.035:.505},{"y",.42},{"width",.46},{"height",.58},{"anchor",id=="room"?"bottom-left":"bottom-right"}}}};}
      if(route=="locations")response={{"room",QJsonObject{{"x",.035},{"y",.42},{"width",.46},{"height",.58},{"anchor","bottom-left"}}}};
      if(route=="phase"){QString type=body.value("type").toString();events.append(type+":"+body.value("kind").toString());if(type=="switch"||type=="reset")location=body.value("id").toString("room");if(type=="camera")cameraBox=body.value("camera").toObject();}
      auto data=QJsonDocument(response).toJson(QJsonDocument::Compact);s->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nConnection: close\r\nContent-Length: "+QByteArray::number(data.size())+"\r\n\r\n"+data);s->disconnectFromHost();
    });
  }});
  {
    AltronHost host;host.settingsPath=temp.path()+"/control.json";host.stagePath=stagesPath;host.currentScene=[&]{return current;};host.outputsActive=[] {return false;};host.save=[&]{++saves;};
    host.select=[&](const QString &,const QString &,const QString &scene){auto *target=obs_get_source_by_name(scene.toUtf8().constData());check(target,"native Stage target exists");obs_set_output_source(0,target);obs_source_release(target);current=scene;++selected;};
    AltronControl controller(std::move(host),nullptr,0);check(controller.listening(),"built-in native controller starts in the app process");QNetworkAccessManager client;QString root=QString("http://127.0.0.1:%1").arg(controller.port());
    auto response=result(client.get(QNetworkRequest(QUrl(root+"/status"))));check(response.code==200&&response.data.value("nodeRequired")==false,"native status advertises no Node dependency");
    response=result(post(client,root+"/register",{{"port",overlay.serverPort()}}));check(response.code==200,"existing Lumia registration accepted");
    controller.setup("Original Cutout","");check(saves==1,"native setup persisted");
    {QFile f(stagesPath);check(f.open(QIODevice::ReadOnly),"stage catalogue readable");auto array=QJsonDocument::fromJson(f.readAll()).array();check(array.size()==2&&array[0].toObject()==existing,"only a new Stage is appended; existing definitions unchanged");}
    response=result(post(client,root+"/out",{}));check(response.code==409,"out rejected while outside dedicated show");
    QNetworkRequest origin(QUrl(root+"/register"));origin.setRawHeader("Origin","https://untrusted.example");origin.setHeader(QNetworkRequest::ContentTypeHeader,"application/json");check(result(client.post(origin,"{}")).code==403,"cross-origin control blocked");
    response=result(post(client,root+"/enter",{}));check(response.code==200&&location=="room"&&current=="PW White Teleport", "Enter selects dedicated Stage and prepares room");
    auto *sound=obs_get_source_by_name("Altron Teleport Audio");check(sound,"native audio is included in the show");auto *settings=obs_source_get_settings(sound);
    auto file=temp.path()+"/teleport-test.wav";tone(file);obs_data_set_string(settings,"arrival_file",file.toUtf8().constData());obs_data_set_string(settings,"departure_file",file.toUtf8().constData());obs_source_update(sound,settings);obs_data_release(settings);obs_source_release(sound);
    obs_add_raw_video_callback(nullptr,video,nullptr);audio_convert_info audioInfo{};audioInfo.format=AUDIO_FORMAT_FLOAT_PLANAR;audioInfo.samples_per_sec=48000;audioInfo.speakers=SPEAKERS_STEREO;obs_add_raw_audio_callback(0,&audioInfo,audio,nullptr);wait(100);
    auto *arrival=post(client,root+"/arrive",{{"destination","moon"}});wait(90);check(controller.status().value("busy").toBool(),"native cue owns timeline");
    check(result(post(client,root+"/out",{})).code==409,"overlap is rejected");check(result(post(client,root+"/register",{{"bridge",QJsonObject{{"actorScene","Unrelated"}}}})).code==409,"busy configuration update blocked");
    response=result(arrival);check(response.code==200&&location=="moon", "arrival completes with destination and placement");check(audioPeak>.1,"native service cue plays sound into real stream mixer");
    auto *actor=obs_get_source_by_name("PW Teleport Actor");auto *item=obs_scene_find_source(obs_scene_from_source(actor),"PW Teleport AI Camera");vec2 position{};obs_sceneitem_get_pos(item,&position);position.x+=30;obs_sceneitem_set_pos(item,&position);obs_source_release(actor);auto before=cameraBox;wait(900);check(cameraBox.value("x").toDouble()>before.value("x").toDouble()+.02,"observer follows actual camera movement");
    response=result(post(client,root+"/depart",{{"destination","freezer"}}));check(response.code==200&&location=="freezer", "full out-switch-in sequence runs in one show scene");
    response=result(post(client,root+"/return",{}));check(response.code==200&&location=="room","return home completes");
    check(events.contains("start:out")&&events.contains("start:in"),"native sequence sends overlay phases");
    response=result(post(client,root+"/reset",{}));check(response.code==200&&!controller.status().value("busy").toBool(),"native reset returns idle");
    obs_transform_info after{};obs_sceneitem_get_info2(originalItem,&after);check(after.pos.x==baseline.pos.x&&after.pos.y==baseline.pos.y&&after.scale.x==baseline.scale.x&&after.scale.y==baseline.scale.y,"reused original camera transform unchanged");
    obs_remove_raw_video_callback(video,nullptr);obs_remove_raw_audio_callback(0,audio,nullptr);
    std::printf("PASS: in-process native HTTP/Lumia integration, automatic controller startup, native show setup, Stage preservation, enter/arrival/out-switch-in/return/reset, real sound, overlap/config guards, camera observer and origin guard.\n");std::fflush(stdout);
  }
  obs_set_output_source(0,nullptr);obs_source_remove(obs_scene_get_source(original));obs_scene_release(original);obs_source_release(cutout);
  obs_enum_all_sources([](void *,obs_source_t *s){obs_source_remove(s);return true;},nullptr);wait(200);obs_wait_for_destroy_queue();obs_shutdown();
  return 0;
}
