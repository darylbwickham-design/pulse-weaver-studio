#include <QtWidgets>
#include <QtNetwork>
#include <atomic>
#include <thread>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <Windows.h>
#include <winhttp.h>
#include "../../engine/obs-studio/shared/qt/PulseChat.hpp"
#include "../../engine/obs-studio/shared/qt/PulsePlatformApplicationIds.hpp"

static QWidget *testWindow;
static QString settingsFile;
static void *obs_frontend_get_main_window() { return testWindow; }
static bool obs_frontend_streaming_active() { return false; }
struct obs_data_t {};
struct obs_service_t {};
static obs_data_t *obs_data_create() { return nullptr; }
static void obs_data_set_string(obs_data_t *, const char *, const char *) {}
static void obs_data_release(obs_data_t *) {}
static obs_service_t *obs_frontend_get_streaming_service() { return nullptr; }
static const char *obs_service_get_type(obs_service_t *) { return ""; }
static void obs_service_update(obs_service_t *, obs_data_t *) {}
static obs_service_t *obs_service_create(const char *, const char *, obs_data_t *, void *) { return nullptr; }
static void obs_frontend_set_streaming_service(obs_service_t *) {}
static void obs_service_release(obs_service_t *) {}
static void obs_frontend_save_streaming_service() {}
static constexpr int LOG_WARNING = 1;
static void blog(int, const char *, ...) {}
static QString pulseSettingsPath() { return settingsFile; }
static QString protectCredential(const QString &s) { return s; }
static QString unprotectCredential(const QString &s) { return s; }
static QByteArray formBody(std::initializer_list<std::pair<QString, QString>> values) {
 QUrlQuery q; for (const auto &v : values) q.addQueryItem(v.first, v.second); return q.query(QUrl::FullyEncoded).toUtf8();
}
static QJsonObject flattenEvent(const QJsonObject &event) { return event; }
static constexpr auto PulseChatPlatformRole = PulseChat::Platform;
static constexpr auto PulseChatUserRole = PulseChat::User;
static constexpr auto PulseChatUserIdRole = PulseChat::UserId;
static constexpr auto PulseChatMessageIdRole = PulseChat::MessageId;
static constexpr auto PulseChatTextRole = PulseChat::Text;
static void pulseAppendUnifiedChat(QListWidget *feed, const QString &platform, const QString &user,
 const QString &message, const QString &colour, const QStringList &badges, const QHash<QString,QUrl> &images,
 const QString &userId, const QString &messageId, bool own, const QJsonArray &fragments) {
 PulseChat::append(feed,platform,user,message,colour,badges,images,userId,messageId,own,fragments);
}
// Never open a real Twitch socket in the deterministic suite.
static HINTERNET fakeWinHttpOpen(LPCWSTR,DWORD,LPCWSTR,LPCWSTR,DWORD) { return nullptr; }
#define WinHttpOpen fakeWinHttpOpen
class FakeReply : public QNetworkReply {
 QByteArray data; qint64 offset = 0;
public:
 FakeReply(QObject *parent, const QNetworkRequest &req, int code, QByteArray body) : QNetworkReply(parent), data(body) {
  setRequest(req); setUrl(req.url()); setAttribute(QNetworkRequest::HttpStatusCodeAttribute,code); open(ReadOnly);
  QTimer::singleShot(0,this,[this]{setFinished(true); emit readyRead(); emit finished();});
 }
 void abort() override {}
 qint64 bytesAvailable() const override { return data.size()-offset+QNetworkReply::bytesAvailable(); }
 qint64 readData(char *out,qint64 max) override { const auto n=qMin(max,data.size()-offset); if(n<=0)return -1; memcpy(out,data.constData()+offset,size_t(n)); offset+=n; return n; }
};
class FakeNetwork : public QNetworkAccessManager {
public:
 using QNetworkAccessManager::QNetworkAccessManager;
 struct Response {int code; QByteArray body;};
 QQueue<Response> responses;
 int requests=0;
 QNetworkReply *createRequest(Operation,const QNetworkRequest &req,QIODevice *) override {
  ++requests;
  auto r=responses.isEmpty()?Response{503,"{}"}:responses.dequeue();
  return new FakeReply(this,req,r.code,r.body);
 }
};
#include "runtime-under-test.hpp"
static void check(bool ok,const char *message){if(!ok)throw std::runtime_error(message);}
static void drain(){for(int i=0;i<12;++i)QCoreApplication::processEvents();}
int main(int argc,char **argv){
 qputenv("QT_QPA_PLATFORM","minimal"); QApplication app(argc,argv); QTemporaryDir temp;
 settingsFile=temp.filePath("isolated.ini"); QWidget window; testWindow=&window;
 QComboBox provider(&window); provider.setObjectName("PulseWeaverChatProvider"); provider.addItem("Twitch","twitch");
 QLineEdit input(&window); QLabel status(&window); QListWidget feed(&window);
 try {
  TwitchRuntime runtime(nullptr,{},{}); runtime.chatInput=&input; runtime.chatStatus=&status; runtime.chatFeed=&feed;
  runtime.accessToken="test-access"; runtime.refreshToken="test-refresh"; runtime.userId="42";
  runtime.updateUi(); check(!window.property("pulseWeaverTwitchChatReady").toBool(),"Credentials alone must not mark chat ready");
  runtime.socketConnected=true; runtime.chatSubscribed=true; runtime.updateUi(); check(window.property("pulseWeaverTwitchChatReady").toBool(),"Subscribed chat not ready");
  runtime.scheduleReconnect("dropped"); check(!window.property("pulseWeaverTwitchChatReady").toBool()&&runtime.retryTimer.isActive(),"Disconnect did not clear readiness or schedule recovery");
  runtime.retryTimer.stop(); runtime.retryAttempt=50; runtime.scheduleReconnect("dropped"); check(runtime.retryTimer.interval()==30000,"Retry backoff is unbounded"); runtime.retryTimer.stop();
  input.setText("keep this draft"); runtime.network.responses.enqueue({500,"{\"message\":\"unavailable\"}"}); runtime.sendFromComposer(); drain();
  check(input.text()=="keep this draft"&&status.text().contains("failed"),"Failed send lost draft or hid error");
  runtime.network.responses.enqueue({200,"{\"data\":[{\"is_sent\":true}]}"}); runtime.sendFromComposer(); drain(); check(input.text().isEmpty(),"Confirmed send did not clear matching draft");
  input.setText("original"); runtime.network.responses.enqueue({200,"{\"data\":[{\"is_sent\":true}]}"}); runtime.sendFromComposer(); input.setText("new draft"); drain(); check(input.text()=="new draft","Successful send erased newer draft");
  runtime.network.responses.enqueue({503,"{}"}); runtime.validateToken(false); drain(); check(runtime.accessToken=="test-access"&&runtime.tokenTimer.isActive(),"Transient validation failure erased credentials"); runtime.tokenTimer.stop();
  runtime.network.responses.enqueue({503,"{}"}); runtime.refreshAccessToken(); drain(); check(runtime.refreshToken=="test-refresh","Transient refresh failure erased token"); runtime.tokenTimer.stop();
  input.setText("retry expired send"); const int beforeRetry=runtime.network.requests;
  runtime.network.responses.enqueue({401,"{}"});
  runtime.network.responses.enqueue({200,"{\"access_token\":\"renewed\",\"refresh_token\":\"renewed-refresh\"}"});
  runtime.network.responses.enqueue({503,"{}"}); // Validation temporarily unavailable.
  runtime.network.responses.enqueue({200,"{\"data\":[{\"is_sent\":true}]}"});
  runtime.sendFromComposer(); drain();
  check(input.text().isEmpty()&&runtime.network.requests==beforeRetry+4,"Expired send did not refresh and retry exactly once"); runtime.retryTimer.stop();
  runtime.network.responses.enqueue({200,QJsonDocument(QJsonObject{{"client_id",runtime.clientId},{"user_id","42"},{"login","test"},{"expires_in",1800},{"scopes",QJsonArray{"user:read:chat","user:write:chat"}}}).toJson()});
  runtime.validateToken(false); drain(); check(runtime.tokenTimer.interval()==1680000,"Token renewal was not scheduled before expiry"); runtime.tokenTimer.stop();
  runtime.network.responses.enqueue({200,"{\"access_token\":\"late-token\",\"refresh_token\":\"late-refresh\"}"}); runtime.refreshAccessToken(); runtime.clearLogin(); drain(); check(runtime.accessToken.isEmpty(),"Late refresh resurrected disconnected account");
  runtime.accessToken="test-access"; runtime.userId="42"; runtime.socketConnected=true; runtime.chatSubscribed=true;
  runtime.handleSocketMessage(R"({"metadata":{"message_type":"revocation"},"payload":{"subscription":{"type":"channel.chat.message","status":"authorization_revoked"}}})");
  check(runtime.accessToken.isEmpty()&&!runtime.chatSubscribed,"Revocation left chat authorized");
  int events=0; runtime.eventCallback=[&](const QString &,const QJsonObject &){++events;};
  const QByteArray notification=R"({"metadata":{"message_type":"notification","message_id":"event-1"},"payload":{"subscription":{"type":"channel.follow"},"event":{}}})";
  runtime.handleSocketMessage(notification); runtime.handleSocketMessage(notification); check(events==1,"Duplicate delivery ran automation twice");
  runtime.accessToken="test-access"; runtime.userId="42"; runtime.stopping=false;
  runtime.socketConnected=true; runtime.chatSubscribed=true;
  auto oldSocket=std::make_shared<TwitchRuntime::SocketWorker>(); oldSocket->generation=runtime.socketGeneration.load();
  auto newSocket=std::make_shared<TwitchRuntime::SocketWorker>(); newSocket->generation=runtime.socketGeneration.load();
  runtime.activeSocket=oldSocket; runtime.pendingSocket=newSocket;
  const int requestsBefore=runtime.network.requests;
  runtime.deliverSocketMessage(newSocket,R"({"metadata":{"message_type":"session_welcome"},"payload":{"session":{"id":"resumed","keepalive_timeout_seconds":10}}})");
  check(oldSocket->cancelled&&runtime.activeSocket==newSocket&&!runtime.pendingSocket&&runtime.chatSubscribed,"Handover lost subscription or kept wrong socket");
  check(runtime.network.requests==requestsBefore,"Handover recreated transferred subscriptions");
  runtime.stopSocket(); runtime.deliverSocketMessage(newSocket,notification); check(events==1,"Old socket delivered after stop");
  runtime.stopping=false; runtime.chatSubscribed=true; runtime.socketConnected=true;
  runtime.lastSocketMessage=QDateTime::currentMSecsSinceEpoch()-30000; runtime.keepaliveMs=12000;
  QMetaObject::invokeMethod(&runtime.watchdog,"timeout",Qt::DirectConnection);
  check(runtime.retryTimer.isActive()&&!runtime.chatSubscribed,"Missed keepalive did not recover"); runtime.retryTimer.stop();
  runtime.stopping=false; runtime.network.responses.enqueue({403,"{\"message\":\"missing permission\"}"});
  for(int i=0;i<12;++i)runtime.network.responses.enqueue({202,"{}"});
  runtime.subscribeEvents("test-session"); drain();
  check(!runtime.chatSubscribed&&status.text().contains("grant chat access"),"Other subscriptions masked chat failure");
  std::cout<<"PASS: readiness, reconnect/backoff, send failure/success/new drafts, transient auth failures, disconnect race, revocation and event deduplication\n";
 }catch(const std::exception &e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
 return 0;
}
