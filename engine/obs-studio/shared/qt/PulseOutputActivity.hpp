#pragma once

#include <obs.h>

// OBS frontend activity covers its primary outputs. Pulse Weaver also owns
// independent YouTube, Kick and portrait outputs, which need the same guards.
inline bool PulseHasActiveOutputs()
{
	bool active = false;
	obs_enum_outputs([](void *opaque, obs_output_t *output) {
		if (!obs_output_active(output))
			return true;
		*static_cast<bool *>(opaque) = true;
		return false;
	}, &active);
	return active;
}
