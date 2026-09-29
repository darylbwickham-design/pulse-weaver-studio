#include <QtWidgets>
#include <QtNetwork>
#include <functional>
#include <vector>
#include <memory>
#include <iostream>
#include <stdexcept>
#include "../../engine/obs-studio/shared/qt/PulseChat.hpp"
#include "../../engine/obs-studio/shared/qt/PulseKickProtocol.hpp"
#include "../../engine/obs-studio/shared/qt/PulsePlatformApplicationIds.hpp"
#include "../../engine/obs-studio/plugins/pulse-weaver-core/pulse-runtime-safety.hpp"
static QWidget *testWindow;
static QString settingsFile;
static void *obs_frontend_get_main_window() { return testWindow; }
struct obs_output_t { int references=1; }; struct obs_service_t {}; struct obs_encoder_t {};
struct calldata_t { obs_output_t *output; int code=0; const char *error=""; };
static void *calldata_ptr(calldata_t *data,const char *) {return data->output;}
static int calldata_int(calldata_t *data,const char *) {return data->code;}
static const char *calldata_string(calldata_t *data,const char *) {return data->error;}
static obs_output_t *obs_output_get_ref(obs_output_t *output) {if(output)++output->references;return output;}
static void obs_output_release(obs_output_t *output) {if(output)--output->references;}
static constexpr int OBS_OUTPUT_SUCCESS=0, LOG_ERROR=2;
static constexpr int LOG_WARNING = 1;
static void blog(int, const char *, ...) {}
static QString pulseSettingsPath() { return settingsFile; }
static QString protectCredential(const QString &s) { return s; }
static QString unprotectCredential(const QString &s) { return s; }
class FakeReply : public QNetworkReply {
 QByteArray data; qint64 offset=0;
public:
 FakeReply(QObject *parent,const QNetworkRequest &request,int code,const QByteArray &body) : QNetworkReply(parent),data(body) {
  setRequest(request);setUrl(request.url());setAttribute(QNetworkRequest::HttpStatusCodeAttribute,code);open(ReadOnly);
  QTimer::singleShot(0,this,[this]{setFinished(true);emit readyRead();emit finished();});
 }
 void abort() override {}
 qint64 bytesAvailable() const override {return data.size()-offset+QNetworkReply::bytesAvailable();}
 qint64 readData(char *out,qint64 max) override {auto n=qMin(max,data.size()-offset);if(n<=0)return -1;memcpy(out,data.constData()+offset,size_t(n));offset+=n;return n;}
};
class FakeNetwork : public QNetworkAccessManager {
public:
 using QNetworkAccessManager::QNetworkAccessManager;
 struct Response{int code;QByteArray body;};QQueue<Response> responses;int requests=0;
 QNetworkReply *createRequest(Operation,const QNetworkRequest &request,QIODevice *) override {
  ++requests;auto r=responses.isEmpty()?Response{503,"{}"}:responses.dequeue();return new FakeReply(this,request,r.code,r.body);
 }
};
#include "runtime-under-test.hpp"
static int checks=0;
static void check(bool ok,const char *message){++checks;if(!ok)throw std::runtime_error(message);}
static void drain(){for(int i=0;i<16;++i)QCoreApplication::processEvents();}
int main(int argc,char **argv){
 qputenv("QT_QPA_PLATFORM","minimal");QApplication app(argc,argv);QTemporaryDir temp;settingsFile=temp.filePath("isolated.ini");
 QWidget window;testWindow=&window;QLabel status;QLineEdit draft;QComboBox provider(&window);provider.setObjectName("PulseWeaverChatProvider");provider.addItem("Kick","kick");
 try{
  KickRuntime runtime(nullptr,{});runtime.status=&status;runtime.chatInput=&draft;runtime.accessToken="old";runtime.refreshToken="refresh";runtime.broadcasterUserId=42;
  int refreshed=0;runtime.network.responses.enqueue({200,R"({"access_token":"new","refresh_token":"new-refresh","expires_in":3600})"});
  runtime.refreshAccessToken([&](bool ok){if(ok)++refreshed;});runtime.refreshAccessToken([&](bool ok){if(ok)++refreshed;});
  check(runtime.network.requests==1,"Concurrent refresh issued duplicate requests");drain();check(refreshed==2&&runtime.accessToken=="new","Refresh waiters were lost");
  runtime.network.responses.enqueue({200,R"({"access_token":"late","refresh_token":"late-refresh","expires_in":3600})"});
  runtime.refreshAccessToken([&](bool){++refreshed;});runtime.clearLogin();drain();
  check(runtime.accessToken.isEmpty()&&refreshed==2&&!runtime.refreshInFlight,"Late refresh restored disconnected credentials or actions");
  runtime.accessToken="test";runtime.broadcasterUserId=42;
  int before=runtime.network.requests;runtime.network.responses.enqueue({503,"{}"});runtime.fetchChannel();runtime.fetchChannel();
  check(runtime.network.requests==before+1,"Channel loads overlapped");runtime.clearLogin();drain();check(!runtime.channelInFlight&&runtime.accountName.isEmpty(),"Late channel response restored account");
  runtime.accessToken="test";runtime.broadcasterUserId=42;runtime.network.responses.enqueue({200,R"({"token":"relay","cursor":7})"});
  before=runtime.network.requests;runtime.connectRelay();runtime.clearLogin();drain();
  check(runtime.relaySessionToken.isEmpty()&&!runtime.relayPollTimer.isActive()&&runtime.network.requests==before+1,"Late relay session restarted chat after disconnect");
  runtime.accessToken="test";runtime.broadcasterUserId=42;runtime.relaySessionToken="relay";int events=0;runtime.eventCallback=[&](const QString &,const QJsonObject &){++events;};
  runtime.network.responses.enqueue({200,R"({"events":[{"sequence":1,"type":"chat.message.sent","payload":{}},{"sequence":1,"type":"chat.message.sent","payload":{}}],"cursor":1})"});
  before=runtime.network.requests;runtime.pollRelayEvents();runtime.pollRelayEvents();drain();
  check(events==1&&runtime.network.requests==before+1,"Relay replay duplicated event or in-flight poll");runtime.relayPollTimer.stop();
  for(int n=0;n<8;++n){runtime.network.responses.enqueue({503,"{}"});runtime.pollRelayEvents();drain();runtime.relayPollTimer.stop();}
  check(runtime.relayPollTimer.interval()==30000,"Relay error backoff did not cap at 30 seconds");
  runtime.network.responses.enqueue({200,R"({"events":[],"cursor":1})"});runtime.pollRelayEvents();drain();
  check(runtime.relayPollTimer.interval()==1500&&runtime.relayFailures==0,"Healthy relay did not restore normal delivery");runtime.relayPollTimer.stop();
  runtime.network.responses.enqueue({200,R"({"events":[{"sequence":2,"type":"chat.message.sent","payload":{}}],"cursor":2})"});
  runtime.pollRelayEvents();runtime.stopRelay();drain();check(events==1&&!runtime.relayPollTimer.isActive(),"Stopped relay delivered stale event");
  runtime.accessToken="test";runtime.broadcasterUserId=42;draft.setText("keep draft");runtime.network.responses.enqueue({500,"{}"});before=runtime.network.requests;
  runtime.sendMessage();runtime.sendMessage();drain();check(draft.text()=="keep draft"&&runtime.network.requests==before+1&&!runtime.sendingChat,"Failed send lost draft or duplicated message");
  runtime.network.responses.enqueue({200,"{}"});runtime.sendMessage();draft.setText("new draft");drain();check(draft.text()=="new draft","Send completion erased newer draft");
  runtime.network.responses.enqueue({200,"{}"});runtime.sendMessage();drain();check(draft.text().isEmpty(),"Confirmed send did not clear matching draft");
  runtime.accessToken="test";runtime.broadcasterUserId=42;runtime.network.responses.enqueue({503,"{}"});runtime.connectRelay();drain();
  check(runtime.relayRetryTimer.isActive(),"Failed relay setup did not retry");runtime.clearLogin();check(!runtime.relayRetryTimer.isActive(),"Disconnect retained relay retry");
  runtime.accessToken="test";runtime.broadcasterUserId=42;runtime.network.responses.enqueue({200,"{}"});before=runtime.network.requests;
  runtime.subscribeEvents();runtime.subscribeEvents();drain();runtime.subscribeEvents();
  check(runtime.network.requests==before+1&&runtime.subscriptionsReady,"Subscriptions were duplicated within one account session");runtime.clearLogin();
  check(!runtime.subscriptionsReady&&!runtime.subscriptionsInFlight,"Disconnect retained old subscription state");
  check(runtime.callback.listen(QHostAddress::LocalHost,0),"Local callback fixture could not listen");runtime.stateToken="state";runtime.codeVerifier="test-verifier";
  QTcpSocket ignored;ignored.connectToHost(QHostAddress::LocalHost,runtime.callback.serverPort());check(ignored.waitForConnected(1000),"Callback fixture did not connect");
  ignored.write("GET /favicon.ico HTTP/1.1\r\nHost: localhost\r\n\r\n");ignored.flush();for(int n=0;n<50;++n){drain();QThread::msleep(1);}
  check(runtime.callback.isListening()&&runtime.stateToken=="state","Unrelated callback consumed OAuth login");
  QTcpSocket browser;browser.connectToHost(QHostAddress::LocalHost,runtime.callback.serverPort());check(browser.waitForConnected(1000),"Split callback fixture did not connect");
  browser.write("GET /auth/callback?code=fake-code&state=state HTTP/1.1\r\nHost:");browser.flush();for(int n=0;n<50;++n){drain();QThread::msleep(1);}
  check(runtime.stateToken=="state"&&runtime.callback.isListening(),"Fragmented callback prematurely consumed login");
  runtime.network.responses.enqueue({400,"{}"});before=runtime.network.requests;browser.write(" localhost\r\n\r\n");browser.flush();for(int n=0;n<50;++n){drain();QThread::msleep(1);}
  check(runtime.stateToken.isEmpty()&&!runtime.callback.isListening()&&runtime.network.requests==before+1,"Complete callback did not exchange exactly once");
  runtime.relaySessionToken="relay";runtime.eventCallback=[&](const QString &,const QJsonObject &){++events;runtime.stopRelay();};
  runtime.network.responses.enqueue({200,R"({"events":[{"sequence":2,"type":"chat.message.sent","payload":{}},{"sequence":3,"type":"chat.message.sent","payload":{}}],"cursor":3})"});
  runtime.pollRelayEvents();drain();check(events==2&&runtime.relayCursor==0,"Event callback stopped relay but remaining old events still ran");
  obs_output_t oldOutput,newOutput;calldata_t oldEvent{&oldOutput},newEvent{&newOutput};
  runtime.output=&oldOutput;runtime.activeOutputRoute="16:9";runtime.outputStarted(&runtime,&oldEvent);runtime.output=&newOutput;
  window.setProperty("pulseWeaverKickLive",false);drain();check(!window.property("pulseWeaverKickLive").toBool()&&oldOutput.references==1,"Stale start marked replacement output live or leaked output reference");
  runtime.outputStarted(&runtime,&newEvent);drain();check(window.property("pulseWeaverKickLive").toBool(),"Current output start was discarded");
  runtime.outputStopped(&runtime,&oldEvent);drain();check(window.property("pulseWeaverKickLive").toBool(),"Old output stop cleared replacement output live state");
  runtime.outputStopped(&runtime,&newEvent);drain();check(!window.property("pulseWeaverKickLive").toBool()&&newOutput.references==1,"Current output stop was discarded or leaked output reference");runtime.output=nullptr;
  std::cout<<"PASS: "<<checks<<" Kick runtime checks (fake HTTP; no platform requests)\n";
 }catch(const std::exception &error){std::cerr<<"FAIL: "<<error.what()<<'\n';return 1;}return 0;
}
