#include "../../engine/obs-studio/shared/qt/PulseKeypad.hpp"
#include "../../engine/obs-studio/plugins/pulse-weaver-core/pulse-lumia-bridge.hpp"
#include <QApplication>
#include <QTemporaryDir>
#include <QTcpServer>
#include <QThread>
#include <cstdio>
#include <cstdlib>
static int checks=0;
static void check(bool ok,const char *message){++checks;if(!ok){std::fprintf(stderr,"FAIL: %s\n",message);std::exit(1);}}
static void events(int ms=30){QElapsedTimer timer;timer.start();while(timer.elapsed()<ms){QCoreApplication::processEvents();QThread::msleep(1);}}
static QByteArray bytes(const QString &path){QFile file(path);check(file.open(QIODevice::ReadOnly),"fixture settings readable");return file.readAll();}
int main(int argc,char **argv){
	QApplication app(argc,argv);QTemporaryDir files;check(files.isValid(),"isolated temporary settings");
	const auto path=files.filePath("pulseweaver-ui.ini");
	{QSettings settings(path,QSettings::IniFormat);for(int slot=1;slot<=15;++slot)PulseKeypad::save(settings,slot,{QString("Button %1").arg(slot),PulseKeypad::iconNames()[slot%12],{}});}
	check(PulseKeypad::alertKey(1)=="keypad_01"&&PulseKeypad::alertKey(15)=="keypad_15"&&PulseKeypad::alertKey(16).isEmpty(),"15 stable numbered Lumia alert identities");
	check(PulseKeypad::imagePath(path,"../../outside.png").isEmpty(),"icon storage cannot escape configuration");
	QImage art(600,400,QImage::Format_ARGB32);art.fill(QColor("#38bbdb"));
	const auto customIcon=files.filePath("custom.png");check(art.save(customIcon),"isolated custom icon fixture");
	const auto scaled=PulseKeypad::readIcon(customIcon);check(!scaled.isNull()&&scaled.width()==256&&scaled.height()<=256,"custom images are decoded at bounded display size");
	check(PulseKeypad::readIcon(files.filePath("missing.png")).isNull(),"missing custom icon uses built-in fallback");
	for(const auto &glyph:PulseKeypad::iconNames())check(!PulseKeypad::icon(glyph).isNull(),"included icon is available");
	PulseKeypad::Panel panel(path);panel.show();
	for(int width:{246,270,320,420}){
		panel.resize(width,540);events();
		int previousWidth=0;QVector<QRect> positions;
		for(int slot=1;slot<=15;++slot){auto *key=panel.findChild<QToolButton *>(QString("PulseWeaverKeypad%1").arg(slot,2,10,QLatin1Char('0')));check(key&&key->isVisible(),"configured key remains visible");check(key->width()==key->height()&&key->width()<=112&&key->width()>=64,"calculated square button stays readable");check(!previousWidth||key->width()==previousWidth,"all button sizes are consistent");previousWidth=key->width();positions.push_back(QRect(key->mapTo(&panel,QPoint()),key->size()));}
		check(positions[0].top()==positions[2].top()&&positions[3].top()>positions[0].top(),"keypad lays out exactly three columns");
		check(positions[2].right()<panel.width(),"three-column row fits available panel width");
	}
	const auto original=bytes(path);
	auto *key=panel.findChild<QToolButton *>("PulseWeaverKeypad01");key->click();check(key->property("pulseKeypadTriggerRequested").toBool(),"operator press requests one Lumia event");key->click();check(!key->property("pulseKeypadTriggerRequested").toBool(),"rapid double press is suppressed");
	auto *edit=panel.findChild<QToolButton *>(QString(),Qt::FindDirectChildrenOnly);Q_UNUSED(edit);
	for(auto *button:panel.findChildren<QToolButton *>())if(button->accessibleName()=="Edit Lumia keypad buttons")button->setChecked(true);
	QTimer::singleShot(0,[]{auto *dialog=qobject_cast<QDialog *>(QApplication::activeModalWidget());check(dialog!=nullptr,"editing opens button dialog");auto *name=dialog->findChild<QLineEdit *>();name->setText("Cancelled edit");dialog->reject();});
	key->click();check(!key->property("pulseKeypadTriggerRequested").toBool(),"editing a button never fires its Lumia alert");check(bytes(path)==original,"cancel preserves saved names and icons");
	QTimer::singleShot(0,[]{auto *dialog=qobject_cast<QDialog *>(QApplication::activeModalWidget());dialog->findChild<QLineEdit *>()->setText("Gameplay");dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Save)->click();});
	key->click();{QSettings settings(path,QSettings::IniFormat);check(PulseKeypad::load(settings,1).name=="Gameplay"&&PulseKeypad::alertKey(1)=="keypad_01","rename persists without changing alert identity");}
	const auto emptyPath=files.filePath("empty.ini");PulseKeypad::Panel emptyPanel(emptyPath);emptyPanel.resize(270,540);emptyPanel.show();events();
	int visibleEmpty=0;for(auto *button:emptyPanel.findChildren<QToolButton *>())if(button->objectName().startsWith("PulseWeaverKeypad")&&button->isVisible())++visibleEmpty;
	check(visibleEmpty==1,"fresh keypad shows one add button instead of 15 empty placeholders");
	check(obs_startup("en-US",nullptr,nullptr),"isolated event bridge startup");
	{
		PulseLumiaBridge bridge(nullptr,[]{return QJsonObject{};});
		check(!bridge.pressKeypad(1,"Gameplay"),"offline presses are rejected rather than queued");
		QTcpServer server;check(server.listen(QHostAddress::LocalHost,0),"isolated loopback server");
		QTcpSocket client;client.connectToHost(QHostAddress::LocalHost,server.serverPort());check(client.waitForConnected(1000),"local subscriber connects");check(server.waitForNewConnection(1000),"local subscriber accepted");auto *peer=server.nextPendingConnection();bridge.subscribe(peer);events();client.readAll();
		check(!bridge.pressKeypad(1,"Gameplay"),"older plugins without keypad capability are rejected");
		peer->setProperty("pulseLumiaKeypad",true);
		check(!bridge.pressKeypad(0,"Bad")&&!bridge.pressKeypad(16,"Bad")&&!bridge.pressKeypad(1,"  "),"invalid button requests are rejected");
		check(bridge.pressKeypad(1,"Gameplay"),"one explicit press reaches a capable subscriber");events();const auto data=client.readAll();
		check(data.contains("\"event\":\"keypad_01\"")&&data.contains("\"buttonName\":\"Gameplay\"")&&data.contains("\"buttonIndex\":1"),"Lumia receives the exact numbered alert and button label");
		peer->disconnectFromHost();events();check(!bridge.pressKeypad(1,"Gameplay"),"disconnect stops delivery immediately");
	}
	obs_shutdown();std::printf("PASS: %d keypad sizing, persistence, edit safety, compatibility and event-delivery checks.\n",checks);
}
