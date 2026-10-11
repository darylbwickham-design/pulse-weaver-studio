#pragma once

#include <obs.h>
#include <cstring>

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
