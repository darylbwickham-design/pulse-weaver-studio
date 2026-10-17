#pragma once
#include <QApplication>
#include <QJsonObject>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSlider>
#include <QStackedWidget>
#include <QTreeWidget>

// Only called by the explicit one-shot, inert native UI fixture audit.
// Browsing and loading a look changes pending UI values only. This never clicks
// Test, Discover, Apply, Save or Remove, and never writes settings or sends I/O.
namespace PulseLightsUiAudit {
inline QJsonObject inspect(QWidget *window, const QString &directory)
{
	auto *surface = window->findChild<QWidget *>("PulseWeaverLightsControls");
	auto *navigation = window->findChild<QWidget *>("PulseWeaverLightsNavigation");
	auto *pages = window->findChild<QStackedWidget *>("PulseWeaverLightingPages");
	auto *devices = window->findChild<QTreeWidget *>("PulseWeaverLightDevices");
	auto *looks = window->findChild<QListWidget *>("PulseWeaverLightLooks");
	auto *brightness = window->findChild<QSlider *>("PulseWeaverLightBrightness");
	auto *power = window->findChild<QPushButton *>("PulseWeaverLightPower");
	auto *colour = window->findChild<QPushButton *>("PulseWeaverLightColour");
	auto *credential = window->findChild<QLineEdit *>("PulseWeaverLightCredential");
	auto *entity = window->findChild<QLineEdit *>("PulseWeaverLightEntity");
	const bool mounted = surface && surface->isVisibleTo(window) && navigation && pages && devices && looks && brightness && power && colour && credential && entity;
	QJsonObject result{{"lightsMounted", mounted}};
	if (!mounted) return result;
	result.insert("lightsModelsHidden", !devices->isVisibleTo(window) && !looks->isVisibleTo(window));
	result.insert("lightsSliderAvailable", brightness->orientation() == Qt::Horizontal && brightness->minimum() == 1 && brightness->maximum() == 255);
	result.insert("lightsCredentialMasked", credential->echoMode() == QLineEdit::Password);
	result.insert("lightsDeviceCards", navigation->findChildren<QPushButton *>("PulseWeaverLightDeviceCard").size());
	result.insert("lightsLookCards", navigation->findChildren<QPushButton *>("PulseWeaverLightLookCard").size());
	window->grab().save(directory + "/lights-devices.png");
	// Synthetic fixture values are opt-in; never select an installed show device.
	if (!qEnvironmentVariableIsSet("PULSEWEAVER_LIGHTS_FIXTURE_AUDIT")) return result;
	struct ActionObserver {
		int clicks = 0;
		QList<QMetaObject::Connection> connections;
		~ActionObserver() { for (const auto &connection : connections) QObject::disconnect(connection); }
	} observer;
	for (auto *button : surface->findChildren<QPushButton *>()) {
		if (!button->property("pulseWeaverLightAction").isValid() && button->objectName() != "PulseWeaverLightDiscover") continue;
		observer.connections.append(QObject::connect(button, &QPushButton::clicked, window, [&observer] { ++observer.clicks; }));
	}
	QPushButton *fixture = nullptr;
	for (auto *card : navigation->findChildren<QPushButton *>("PulseWeaverLightDeviceCard"))
		if (card->property("pulseLightEndpoint").toString() == "192.0.2.80") fixture = card;
	if (!fixture) { result.insert("lightsFixtureRestored", false); return result; }
	fixture->click(); QApplication::processEvents();
	result.insert("lightsDeviceSelection", pages->currentWidget()->objectName() == "PulseWeaverLightingSelected");
	result.insert("lightsFixtureRestored", entity->text() == "fixture/entity" && credential->text() == "fixture-only-not-a-secret" && credential->echoMode() == QLineEdit::Password);
	window->grab().save(directory + "/lights-control.png");
	for (auto *button : navigation->findChildren<QPushButton *>("PulseWeaverLightSavedLooks"))
		if (button->isVisibleTo(window)) { button->click(); break; }
	QApplication::processEvents(); window->grab().save(directory + "/lights-looks.png");
	for (auto *card : navigation->findChildren<QPushButton *>("PulseWeaverLightLookCard"))
		if (card->property("pulseLightLook").toString() == "Fixture calm") { card->click(); break; }
	QApplication::processEvents();
	result.insert("lightsSavedLookLoadsLocally", pages->currentWidget()->objectName() == "PulseWeaverLightingSelected" && brightness->value() == 73 && !power->isChecked() && colour->text().contains("#123456", Qt::CaseInsensitive));
	for (auto *button : navigation->findChildren<QPushButton *>("PulseWeaverLightBack"))
		if (button->isVisibleTo(window)) { button->click(); break; }
	QApplication::processEvents(); result.insert("lightsBackToDevices", pages->currentWidget()->objectName() == "PulseWeaverLightingHome");
	result.insert("lightsNoHardwareActions", observer.clicks == 0);
	return result;
}
} // namespace PulseLightsUiAudit
