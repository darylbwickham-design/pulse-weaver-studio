#include "../../engine/obs-studio/plugins/pulse-weaver-core/pulse-light-request.hpp"
#include "../../engine/obs-studio/plugins/pulse-weaver-core/pulse-lights-navigation.hpp"
#include "../../engine/obs-studio/shared/qt/PulseLightsUiAudit.hpp"
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTabWidget>
#include <QSslSocket>
#include <QSslConfiguration>
#include <QPointer>
#include <cstdio>
#include <cstdlib>

static unsigned checks;
static void check(bool ok, const char *message) { if (!ok) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); } ++checks; }
int main(int argc, char **argv)
{
	QApplication app(argc, argv); app.setStyle("windowsvista");
	QFile theme(qEnvironmentVariable("PULSEWEAVER_CONTROL_THEME_QSS"));
	if (!theme.fileName().isEmpty()) { check(theme.open(QIODevice::ReadOnly), "resolved theme readable"); app.setStyleSheet(QString::fromUtf8(theme.readAll())); }
	else app.setStyleSheet("QPushButton { height:22px; max-height:22px; margin:2px 0; padding:4px 16px; border:1px solid #3b485a; }");
	using PulseLightRequest::make;
	const QColor colour("#123456");
	auto wled = make("WLED", "192.0.2.80", "", "", colour, 255, true);
	check(wled.valid() && wled.method == "POST" && wled.network.url() == QUrl("http://192.0.2.80/json/state"), "WLED POST uses documented state endpoint");
	check(wled.body.keys() == QStringList({"bri", "on", "seg"}) && wled.body["bri"] == 255 && wled.body["on"] == true, "WLED native keys and brightness retained");
	check(wled.body["seg"].toArray()[0].toObject()["col"].toArray()[0].toArray() == QJsonArray({18,52,86}), "WLED exact RGB segment payload");
	auto hue = make("Philips Hue", "192.0.2.81", "fixture-user", "3", colour, 255, false);
	check(hue.valid() && hue.method == "PUT" && hue.network.url() == QUrl("https://192.0.2.81/api/fixture-user/lights/3/state"), "Hue uses secure documented v1 PUT endpoint");
	check(hue.body.keys() == QStringList({"bri","hue","on","sat"}) && hue.body["bri"] == 254 && hue.body["on"] == false, "Hue brightness uses supported maximum and native colour keys");
	check(hue.network.sslConfiguration().peerVerifyMode() != QSslSocket::VerifyNone, "Hue never disables normal TLS certificate verification");
	check(!make("Philips Hue", "192.0.2.81", "", "3", colour, 1, true).valid(), "Hue missing credential rejected without network");
	auto ha = make("Home Assistant", "https://ha.example.invalid:8123", "fixture-token", "light.fixture", colour, 73, true);
	check(ha.valid() && ha.method == "POST" && ha.network.url().path() == "/api/services/light/turn_on", "Home Assistant on service endpoint");
	check(ha.network.rawHeader("Authorization") == "Bearer fixture-token" && ha.body.keys() == QStringList({"brightness","entity_id","rgb_color"}), "Home Assistant exact service fields and bearer auth");
	check(ha.body["brightness"] == 73 && ha.body["rgb_color"].toArray() == QJsonArray({18,52,86}), "Home Assistant RGB and brightness values preserved");
	auto off = make("Home Assistant", "192.0.2.82:8123", "fixture-token", "light.fixture", colour, 255, false);
	check(off.valid() && off.network.url().path() == "/api/services/light/turn_off" && off.body == QJsonObject({{"entity_id","light.fixture"}}), "Home Assistant off contains no unsupported colour/brightness or generic keys");
	check(!make("Home Assistant", "192.0.2.82", "bad\r\ntoken", "light.fixture", colour, 73, true).valid(), "invalid header credential rejected");
	for (const auto &provider : {"HTTP / DIY", "Display / Pixel device"}) {
		auto generic = make(provider, "192.0.2.83:8080", "", "look", colour, 1, true);
		check(generic.valid() && generic.method == "POST" && generic.network.url().path() == "/look" && generic.body.keys() == QStringList({"bri","on","rgb"}), "generic provider JSON contract retained");
	}
	check(!make("unknown", "192.0.2.80", "", "", colour, 1, true).valid(), "unknown provider rejected");
	check(!make("WLED", "file:///device", "", "", colour, 1, true).valid(), "non-HTTP device URL rejected");
	check(ha.network.attribute(QNetworkRequest::RedirectPolicyAttribute).toInt() == QNetworkRequest::ManualRedirectPolicy, "credentials cannot follow a redirect automatically");
	check(PulseLightRequest::accepted("Philips Hue", 200, true, R"([{"success":{"/lights/3/state/on":true}}])"), "Hue successful state response accepted");
	check(!PulseLightRequest::accepted("Philips Hue", 200, true, R"([{"error":{"type":1,"description":"unauthorized"}}])"), "Hue HTTP200 error is not reported as success");
	check(!PulseLightRequest::accepted("Philips Hue", 200, true, "broken") && !PulseLightRequest::accepted("WLED", 500, true, ""), "malformed Hue and HTTP failures rejected");

	QWidget window; window.setAttribute(Qt::WA_DontShowOnScreen); window.resize(1148, 500);
	auto *outer = new QVBoxLayout(&window); auto *surface = new QWidget(&window); surface->setObjectName("PulseWeaverLightsControls"); outer->addWidget(surface); auto *layout = new QVBoxLayout(surface);
	auto *setup = new QWidget; auto *setupLayout = new QVBoxLayout(setup); auto *devices = new QTreeWidget(setup); devices->setObjectName("PulseWeaverLightDevices"); devices->setColumnCount(4); setupLayout->addWidget(devices);
	new QTreeWidgetItem(devices, {"Fixture light","WLED","192.0.2.80","Saved"});
	auto *controls = new QWidget; auto *controlLayout = new QVBoxLayout(controls);
	auto *brightness = new QSlider(Qt::Horizontal, controls); brightness->setObjectName("PulseWeaverLightBrightness"); brightness->setRange(1,255); brightness->setValue(180); controlLayout->addWidget(brightness);
	auto *power = new QPushButton("Power on", controls); power->setObjectName("PulseWeaverLightPower"); power->setCheckable(true); power->setChecked(true); controlLayout->addWidget(power);
	auto *colourButton = new QPushButton("Colour #FFFFFF", controls); colourButton->setObjectName("PulseWeaverLightColour"); controlLayout->addWidget(colourButton);
	auto *credential = new QLineEdit(controls); credential->setObjectName("PulseWeaverLightCredential"); credential->setEchoMode(QLineEdit::Password); controlLayout->addWidget(credential);
	auto *entity = new QLineEdit(controls); entity->setObjectName("PulseWeaverLightEntity"); controlLayout->addWidget(entity);
	QObject::connect(devices, &QTreeWidget::currentItemChanged, &window, [credential,entity] { credential->setText("fixture-only-not-a-secret"); entity->setText("fixture/entity"); });
	auto *looksPage = new QWidget; auto *lookLayout = new QVBoxLayout(looksPage); auto *looks = new QListWidget(looksPage); looks->setObjectName("PulseWeaverLightLooks"); lookLayout->addWidget(looks);
	auto *look = new QListWidgetItem("Fixture calm", looks); look->setData(Qt::UserRole,"Fixture calm|#123456|73|0");
	QObject::connect(looks, &QListWidget::itemDoubleClicked, &window, [brightness,power,colourButton](QListWidgetItem *item) { const auto fields=item->data(Qt::UserRole).toString().split('|'); brightness->setValue(fields[2].toInt()); power->setChecked(fields[3]=="1"); colourButton->setText("Colour " + fields[1]); });
	int actionClicks=0;
	for (const auto &action : {"test-device","apply-look","discover"}) {
		auto *button=new QPushButton(action, action==QString("apply-look")?controls:setup); button->setProperty("pulseWeaverLightAction",action);
		(action==QString("apply-look")?controlLayout:setupLayout)->addWidget(button);
		QObject::connect(button,&QPushButton::clicked,&window,[&]{++actionClicks;});
	}
	auto *remove = new QPushButton("Remove device",setup);
	auto *legacyTabs = new QTabWidget(&window); legacyTabs->addTab(setup,"Devices"); legacyTabs->addTab(controls,"Control"); legacyTabs->addTab(looksPage,"Looks");
	auto *navigation = new PulseLightsNavigation::Page({setup,controls,looksPage,devices,looks,remove},surface); layout->addWidget(navigation);
	QPointer<QWidget> observedSetup=setup, observedControls=controls, observedLooks=looksPage;
	delete legacyTabs;
	check(observedSetup && observedControls && observedLooks && navigation->isAncestorOf(setup) && navigation->isAncestorOf(controls) && navigation->isAncestorOf(looksPage), "legacy-tab deletion preserves all reparented controls and authoritative models");
	window.show(); app.processEvents();
	check(!devices->isVisibleTo(&window) && !looks->isVisibleTo(&window), "authoritative models are hidden rather than exposed as lists");
	check(navigation->findChildren<QPushButton *>("PulseWeaverLightDeviceCard").size()==1 && navigation->findChildren<QPushButton *>("PulseWeaverLightLookCard").size()==1, "saved models produce device and look cards");
	QTemporaryDir output(QDir::currentPath() + "/lights-audit-XXXXXX"); check(output.isValid(), "private audit artifact directory created"); qputenv("PULSEWEAVER_LIGHTS_FIXTURE_AUDIT","1");
	const auto audit=PulseLightsUiAudit::inspect(&window,output.path());
	for(const auto &guard : {"lightsMounted","lightsModelsHidden","lightsSliderAvailable","lightsCredentialMasked","lightsDeviceSelection","lightsFixtureRestored","lightsSavedLookLoadsLocally","lightsBackToDevices","lightsNoHardwareActions"}) check(audit[guard].toBool(),guard);
	check(actionClicks==0,"card and look navigation never invokes hardware actions");
	new QTreeWidgetItem(devices,{"Second device","HTTP / DIY","192.0.2.84","Saved"}); app.processEvents();
	check(navigation->findChildren<QPushButton *>("PulseWeaverLightDeviceCard").size()==2,"new saved devices update card grid from authoritative model");
	delete devices->takeTopLevelItem(1); app.processEvents();
	check(navigation->findChildren<QPushButton *>("PulseWeaverLightDeviceCard").size()==1,"removed model devices disappear from card grid");
	for(auto *card:navigation->findChildren<QPushButton *>("PulseWeaverLightDeviceCard")) check(card->sizeHint().height()==86 && card->height()==86,"native theme cannot cap custom card geometry");
	check(actionClicks==0,"model changes never trigger network actions");
	std::printf("PASS: %u offline Lights request, click-through and passive audit checks; no network requests or lighting commands sent.\n",checks);
}
