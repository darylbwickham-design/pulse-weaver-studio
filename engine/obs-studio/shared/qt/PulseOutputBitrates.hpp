#pragma once

#include <util/config-file.h>
#include <algorithm>

namespace PulseOutputBitrates {
inline constexpr const char *Section = "PulseWeaverOutputs";
inline constexpr const char *Keys[] = {"KickHorizontal", "KickVertical", "YouTubeHorizontal", "YouTubeVertical"};
inline constexpr int Defaults[] = {4500, 4500, 5500, 4000};

// Read at output creation, never mutate a running encoder. Old profiles retain
// the previous fixed bitrates; validate persisted values as well as UI input.
inline int Read(config_t *config, int route)
{
	if (!config || !config_has_user_value(config, Section, Keys[route]))
		return Defaults[route];
	return int(std::clamp<int64_t>(config_get_int(config, Section, Keys[route]), 500, 51000));
}
}
