#pragma once

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QSet>
#include <QSettings>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <utility>

namespace PulseChatPanel {

inline const QString LumiaDockUrl = QStringLiteral("https://lumiastream.com/dock/chat");
inline const QString ControlRoomUrl = QStringLiteral("https://lumiastream.com/dock");
inline const QString LumiaControlRoomUrl = ControlRoomUrl;

inline QString normalizeLink(const QString &value)
{
	const QString text = value.trimmed();
	const QUrl url(text, QUrl::StrictMode);
	if (!url.isValid() || url.isRelative() || url.host().isEmpty() ||
	    (url.scheme() != "https" && url.scheme() != "http") || !url.userInfo().isEmpty())
		return {};
	return url.toString(QUrl::FullyEncoded);
}

inline bool isLumiaLink(const QString &value)
{
	const QString link = normalizeLink(value);
	if (link.isEmpty())
		return false;
	const QString host = QUrl(link).host();
	return host == "lumiastream.com" || host == "www.lumiastream.com";
}

inline QString defaultPanelName(const QString &url)
{
	if (isLumiaLink(url))
		return QStringLiteral("Lumia");
	const QString host = QUrl(normalizeLink(url)).host();
	return host.isEmpty() ? QStringLiteral("Chat") : host;
}

inline int defaultZoomSteps(const QString &url)
{
	return isLumiaLink(url) ? -4 : 0;
}

// Retained solely for the pre-version-2 generic-dock migration.
inline bool isGenericLumiaLink(const QString &value)
{
	const QUrl url(normalizeLink(value));
	return url.scheme() == "https" &&
		(url.host() == "lumiastream.com" || url.host() == "www.lumiastream.com") &&
		(url.path() == "/dock" || url.path() == "/dock/") &&
		url.port() == -1 && !url.hasQuery() && !url.hasFragment();
}

struct Panel {
	QString id;
	QString name;
	QString url;
	int zoomSteps = 0;
};

struct Preferences {
	QString provider = QStringLiteral("native");
	QList<Panel> panels;

	QString url() const
	{
		if (provider != "native") {
			for (const Panel &panel : panels) {
				if (panel.id == provider)
					return panel.url;
			}
		}
		return {};
	}
};

inline const Panel *findPanel(const Preferences &preferences, const QString &id)
{
	if (id == "native")
		return nullptr;
	for (const Panel &panel : preferences.panels) {
		if (panel.id == id)
			return &panel;
	}
	return nullptr;
}

inline Panel *findPanel(Preferences &preferences, const QString &id)
{
	if (id == "native")
		return nullptr;
	for (Panel &panel : preferences.panels) {
		if (panel.id == id)
			return &panel;
	}
	return nullptr;
}

inline const Panel *selectedPanel(const Preferences &preferences)
{
	return findPanel(preferences, preferences.provider);
}

inline Panel *selectedPanel(Preferences &preferences)
{
	return findPanel(preferences, preferences.provider);
}

inline QString normalizePanelName(const QString &value, const QString &url)
{
	QString name;
	for (const QChar character : value.simplified()) {
		if (character.category() != QChar::Other_Control)
			name += character;
	}
	name = name.simplified();
	if (name.isEmpty())
		name = defaultPanelName(url);
	name = name.left(80);
	if (!name.isEmpty() && name.at(name.size() - 1).isHighSurrogate())
		name.chop(1);
	return name.trimmed();
}

inline Preferences normalize(Preferences preferences)
{
	QList<Panel> panels;
	QSet<QString> ids;
	for (Panel panel : preferences.panels) {
		panel.id = panel.id.trimmed();
		panel.url = normalizeLink(panel.url);
		if (panel.id.isEmpty() || panel.id == "native" || panel.url.isEmpty() || ids.contains(panel.id))
			continue;
		panel.name = normalizePanelName(panel.name, panel.url);
		panel.zoomSteps = qBound(-7, panel.zoomSteps, 8);
		ids.insert(panel.id);
		panels.append(panel);
	}
	preferences.panels = std::move(panels);
	preferences.provider = preferences.provider.trimmed();
	if (preferences.provider != "native" && !ids.contains(preferences.provider))
		preferences.provider = "native";
	return preferences;
}

inline int zoomSteps(const Preferences &preferences)
{
	const Panel *panel = selectedPanel(preferences);
	return panel ? qBound(-7, panel->zoomSteps, 8) : 0;
}

inline void setZoomSteps(Preferences &preferences, int steps)
{
	if (Panel *panel = selectedPanel(preferences))
		panel->zoomSteps = qBound(-7, steps, 8);
}

inline int zoomPercent(const Preferences &preferences)
{
	// These discrete steps match QCefWidget::zoomPage's supported page sizes.
	constexpr int percentages[]{25, 33, 50, 67, 75, 80, 90, 100, 110, 125, 150, 175, 200, 250, 300, 400};
	return percentages[qBound(-7, zoomSteps(preferences), 8) + 7];
}

inline QStringList enabledProviders(const Preferences &value)
{
	const Preferences preferences = normalize(value);
	QStringList providers{"native"};
	for (const Panel &panel : preferences.panels)
		providers << panel.id;
	return providers;
}

inline QString nextProvider(const Preferences &preferences)
{
	const QStringList providers = enabledProviders(preferences);
	return providers[(providers.indexOf(preferences.provider) + 1) % providers.size()];
}

inline QString title(const Preferences &preferences)
{
	const Panel *panel = selectedPanel(preferences);
	return panel ? normalizePanelName(panel->name, panel->url) : QStringLiteral("Chat");
}

inline Preferences removeProvider(Preferences preferences, const QString &provider)
{
	if (provider != "native") {
		for (auto panel = preferences.panels.begin(); panel != preferences.panels.end();) {
			if (panel->id == provider)
				panel = preferences.panels.erase(panel);
			else
				++panel;
		}
	}
	if (provider != "native" && preferences.provider == provider)
		preferences.provider = "native";
	return normalize(preferences);
}

inline Preferences load(QSettings &settings)
{
	Preferences preferences;
	preferences.provider = settings.value("chat/panel_provider", "native").toString();
	const int version = settings.value("chat/panel_version", 0).toInt();
	if (version >= 3) {
		// Once migrated, this list is authoritative even when absent, empty or
		// malformed. Old keys are recovery material, not enabled panels.
		const QJsonDocument document = QJsonDocument::fromJson(
			settings.value("chat/panels_json").toString().toUtf8());
		if (document.isArray()) {
			for (const QJsonValue &value : document.array()) {
				if (!value.isObject())
					continue;
				const QJsonObject object = value.toObject();
				Panel panel{object.value("id").toString(), object.value("name").toString(),
					    object.value("url").toString(), 0};
				const QJsonValue zoom = object.value("zoomSteps");
				const int fallback = defaultZoomSteps(panel.url);
				if (zoom.isDouble() && zoom.toDouble() <= -7)
					panel.zoomSteps = -7;
				else if (zoom.isDouble() && zoom.toDouble() >= 8)
					panel.zoomSteps = 8;
				else
					panel.zoomSteps = zoom.toInt(fallback);
				preferences.panels.append(panel);
			}
		}
		return normalize(preferences);
	}

	const QString oldLumiaUrl = normalizeLink(settings.value("chat/lumia_dock_url", LumiaDockUrl).toString());
	const bool genericLumia = isGenericLumiaLink(oldLumiaUrl);
	const bool previouslyConfigured = preferences.provider == "lumia" ||
		(!oldLumiaUrl.isEmpty() && oldLumiaUrl != LumiaDockUrl && !genericLumia);
	if (settings.value("chat/lumia_enabled", previouslyConfigured).toBool()) {
		const QString lumiaUrl = oldLumiaUrl.isEmpty() || (version < 2 && genericLumia) ?
			LumiaDockUrl : oldLumiaUrl;
		preferences.panels.append({QStringLiteral("lumia"), QStringLiteral("Lumia"), lumiaUrl,
			settings.value("chat/lumia_zoom_steps", -4).toInt()});
	}
	const QString customUrl = normalizeLink(settings.value("chat/custom_dock_url").toString());
	if (!customUrl.isEmpty()) {
		preferences.panels.append({QStringLiteral("custom"), QStringLiteral("Other chat"), customUrl,
			settings.value("chat/custom_zoom_steps", 0).toInt()});
	}
	return normalize(preferences);
}

inline void save(QSettings &settings, const Preferences &value)
{
	const Preferences preferences = normalize(value);
	QJsonArray panels;
	for (const Panel &panel : preferences.panels) {
		panels.append(QJsonObject{{"id", panel.id}, {"name", panel.name}, {"url", panel.url},
					 {"zoomSteps", panel.zoomSteps}});
	}
	settings.setValue("chat/panel_provider", preferences.provider);
	settings.setValue("chat/panels_json", QString::fromUtf8(QJsonDocument(panels).toJson(QJsonDocument::Compact)));
	settings.setValue("chat/panel_version", 3);
	settings.sync();
}

enum class BrowserAction { Release, Wait, Unavailable, Create, Navigate, Keep };

inline BrowserAction browserAction(const Preferences &preferences, bool startupReady, bool browserAvailable,
				  bool showActive, const QString &loadedUrl)
{
	const Preferences normalized = normalize(preferences);
	if (normalized.provider == "native" || !showActive)
		return BrowserAction::Release;
	if (!startupReady)
		return BrowserAction::Wait;
	if (!browserAvailable)
		return BrowserAction::Unavailable;
	if (loadedUrl.isEmpty())
		return BrowserAction::Create;
	return loadedUrl == normalized.url() ? BrowserAction::Keep : BrowserAction::Navigate;
}

} // namespace PulseChatPanel
