#pragma once
#include <stdbool.h>
#include <stdint.h>

/* Only display swapchains use this clock. Source ticks, canvases and encoders
 * continue at their configured rate. Call under the display info mutex. */
static inline bool pulse_display_frame_due(uint64_t now, uint32_t fps, uint64_t *next)
{
	if (!fps) {
		*next = 0;
		return true;
	}
	const uint64_t interval = 1000000000ULL / fps;
	if (*next && now < *next && *next - now > 1000000ULL && *next - now <= interval)
		return false;
	/* Keep cadence across small scheduling jitter, reset after a long stall. */
	*next = !*next || now > *next + interval || now + interval < *next ? now + interval : *next + interval;
	return true;
}
