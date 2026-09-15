#pragma once

#include <obs.h>
#include <QMap>
#include <QString>

// Keys are persistent OBS names; audio annotations belong only to the UI.
inline QMap<QString, bool> PulseStageExclusionSources(obs_scene_t *scene)
{
	QMap<QString, bool> sources;
	if (scene)
		obs_scene_enum_items(scene, [](obs_scene_t *, obs_sceneitem_t *item, void *opaque) {
			obs_source_t *source = obs_sceneitem_get_source(item);
			if (source)
				static_cast<QMap<QString, bool> *>(opaque)->insert(
					QString::fromUtf8(obs_source_get_name(source)),
					obs_source_get_type(source) == OBS_SOURCE_TYPE_INPUT &&
						(obs_source_get_output_flags(source) & OBS_SOURCE_AUDIO));
			return true;
		}, &sources);
	// Audio is mixed across active scenes and global devices. Include inactive
	// inputs too so a Stage can exclude them before they become active.
	obs_enum_sources([](void *opaque, obs_source_t *source) {
		if (obs_source_get_type(source) == OBS_SOURCE_TYPE_INPUT &&
		    (obs_source_get_output_flags(source) & OBS_SOURCE_AUDIO))
			static_cast<QMap<QString, bool> *>(opaque)->insert(
				QString::fromUtf8(obs_source_get_name(source)), true);
		return true;
	}, &sources);
	return sources;
}
