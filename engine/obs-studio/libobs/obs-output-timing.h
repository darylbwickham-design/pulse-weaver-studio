#pragma once
#include "obs.h"

#ifdef __cplusplus
extern "C" {
#endif
/* Longest relative video-track timestamp for an active encoded A/V output.
 * Returns zero while stopped or before timestamped video has arrived.
 * This is media uptime, independent of track count and encoder frame rate. */
EXPORT uint64_t obs_output_get_video_uptime_usec(obs_output_t *output);
#ifdef __cplusplus
}
#endif
