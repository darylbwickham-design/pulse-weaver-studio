#pragma once

#include <obs.h>
#include <QJsonArray>
#include <QJsonObject>
#include <QSet>
#include <QString>
#include <QUrl>
#include <cstring>
#include <functional>

// Overlay Designer uses this fixed loopback endpoint. Other local services,
// including Lumia Chat Physics on its own port, remain eligible.
inline bool pulseIsNativeOverlayUrl(const QString &text)
{
	const QUrl url(text.trimmed());
	const QString host = url.host().toLower();
	const bool loopback = host == "127.0.0.1" || host == "localhost" || host == "::1";
	const QString path = url.adjusted(QUrl::NormalizePathSegments).path();
	if (url.isLocalFile())
		return path.contains("/pulse-weaver-core/show-graphics/", Qt::CaseInsensitive) &&
			path.endsWith(".html", Qt::CaseInsensitive);
	return loopback && url.port() == 18754 &&
		(path.startsWith("/overlay/") || path.startsWith("/preview/"));
}

inline bool pulseIsNativeOverlaySource(obs_source_t *source)
{
	if (!source || std::strcmp(obs_source_get_unversioned_id(source), "browser_source") != 0)
		return false;
	obs_data_t *settings = obs_source_get_settings(source);
	const QString file = QString::fromUtf8(obs_data_get_string(settings, "local_file")).replace('\\', '/');
	const bool native = obs_data_get_bool(settings, "is_local_file") ?
		file.contains("/pulse-weaver-core/show-graphics/", Qt::CaseInsensitive) &&
		file.endsWith(".html", Qt::CaseInsensitive) :
		pulseIsNativeOverlayUrl(QString::fromUtf8(obs_data_get_string(settings, "url")));
	obs_data_release(settings);
	return native;
}

inline bool pulseWizardOverlayUrlAllowed(const QString &text)
{
	const QUrl url(text.trimmed());
	return url.isValid() && !url.host().isEmpty() &&
		(url.scheme() == "http" || url.scheme() == "https") && !pulseIsNativeOverlayUrl(text);
}

inline bool pulseWizardOverlaySourceAllowed(obs_source_t *source)
{
	if (!source || std::strcmp(obs_source_get_unversioned_id(source), "browser_source") != 0)
		return false;
	obs_data_t *settings = obs_source_get_settings(source);
	const bool allowed = !obs_data_get_bool(settings, "is_local_file") &&
		pulseWizardOverlayUrlAllowed(QString::fromUtf8(obs_data_get_string(settings, "url")));
	obs_data_release(settings);
	return allowed;
}

// Saved scenes may hide native overlays inside camera units or groups. The
// clone only hides disabled inputs; it does not omit them. Check every retained
// input after replacements, including disabled layers, without blocking normal
// local browser feeds or newly drafted sources validated by the caller.
inline bool pulseSceneUsesNativeOverlay(obs_source_t *root, const QJsonObject &bindings = {},
	const QJsonArray & = {})
{
	QSet<obs_source_t *> seen;
	std::function<bool(obs_source_t *)> contains;
	contains = [&](obs_source_t *source) {
		if (!source || seen.contains(source)) return false;
		seen.insert(source);
		const QString uuid = QString::fromUtf8(obs_source_get_uuid(source));
		if (bindings.contains(uuid)) {
			const QString replacement = bindings.value(uuid).toString();
			if (replacement.startsWith("new:")) return false;
			obs_source_t *assigned = obs_get_source_by_uuid(replacement.toUtf8().constData());
			const bool native = pulseIsNativeOverlaySource(assigned);
			obs_source_release(assigned);
			return native;
		}
		if (pulseIsNativeOverlaySource(source)) return true;
		auto *scene = obs_group_from_source(source); if (!scene) scene = obs_scene_from_source(source);
		if (!scene) return false;
		struct Scan { std::function<bool(obs_source_t *)> *contains; bool found = false; } scan{&contains};
		obs_scene_enum_items(scene, [](obs_scene_t *, obs_sceneitem_t *item, void *opaque) {
			auto &scan = *static_cast<Scan *>(opaque);
			if ((*scan.contains)(obs_sceneitem_get_source(item))) { scan.found = true; return false; }
			return true;
		}, &scan);
		return scan.found;
	};
	return contains(root);
}

// Check the inputs a saved scene will retain after its requested replacements
// and explicit omissions, including sources inside camera units and groups.
inline bool pulseSceneUsesSource(obs_source_t *root, const QString &candidate,
	const QJsonObject &bindings = {}, const QJsonArray &disabled = {})
{
	if (!root || candidate.isEmpty()) return false;
	QSet<obs_source_t *> seen;
	std::function<bool(obs_source_t *)> contains;
	contains = [&](obs_source_t *source) {
		if (!source || seen.contains(source)) return false;
		seen.insert(source);
		const QString uuid = QString::fromUtf8(obs_source_get_uuid(source));
		if (disabled.contains(uuid)) return false;
		if (bindings.value(uuid).toString(uuid) == candidate) return true;
		auto *scene = obs_group_from_source(source); if (!scene) scene = obs_scene_from_source(source);
		if (!scene) return false;
		struct Scan { std::function<bool(obs_source_t *)> *contains; bool found = false; } scan{&contains};
		obs_scene_enum_items(scene, [](obs_scene_t *, obs_sceneitem_t *item, void *opaque) {
			auto &scan = *static_cast<Scan *>(opaque);
			if ((*scan.contains)(obs_sceneitem_get_source(item))) { scan.found = true; return false; }
			return true;
		}, &scan);
		return scan.found;
	};
	return contains(root);
}

// Full-canvas browser artwork needs its matching orientation. Ordinary feeds,
// square artwork and sources without known viewport dimensions remain usable.
inline bool pulseBrowserMatchesCanvas(obs_source_t *source, bool portrait)
{
	if (!source || std::strcmp(obs_source_get_unversioned_id(source), "browser_source") != 0)
		return true;
	obs_data_t *settings = obs_source_get_settings(source);
	const int64_t width = obs_data_get_int(settings, "width");
	const int64_t height = obs_data_get_int(settings, "height");
	obs_data_release(settings);
	return width <= 0 || height <= 0 || width == height || portrait == (height > width);
}
