#pragma once
#include <stddef.h>
#include <stdint.h>

/* Relative track timestamps already incorporate each encoder's timebase. */
static inline int64_t pulse_longest_track_time_usec(const int64_t *times, size_t count)
{
	int64_t longest = 0;
	for (size_t index = 0; index < count; ++index)
		if (times[index] > longest)
			longest = times[index];
	return longest;
}
