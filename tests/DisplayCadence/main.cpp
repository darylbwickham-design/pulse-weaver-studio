#include "../../engine/obs-studio/libobs/util/pulse-display-cadence.h"
#include <cstdio>
#include <cstdlib>

static void check(bool result, const char *message)
{
	if (!result) {
		std::fprintf(stderr, "FAIL: %s\n", message);
		std::exit(1);
	}
}

static int framesAt(uint32_t previewFps, uint32_t sourceFps = 60)
{
	uint64_t next = 0;
	int rendered = 0;
	for (uint64_t frame = 0; frame < 10 * sourceFps; ++frame)
		rendered += pulse_display_frame_due(1000000000ULL + frame * 1000000000ULL / sourceFps, previewFps,
						  &next);
	return rendered;
}

int main()
{
	check(framesAt(0) == 600, "unlimited display follows all source ticks");
	check(framesAt(15) == 150, "15 FPS preview draws 150 of 600 source ticks");
	check(framesAt(30) == 300, "30 FPS preview draws 300 of 600 source ticks");
	check(framesAt(60) == 600, "default 60 FPS preview preserves all 60 FPS source ticks");
	check(framesAt(60, 30) == 300, "60 FPS preview does not add frames to a 30 FPS source");
	check(framesAt(60, 120) == 600, "60 FPS preview caps higher source rates");
	uint64_t next = 0;
	check(pulse_display_frame_due(1000000000ULL, 15, &next), "first frame draws");
	check(!pulse_display_frame_due(1001000000ULL, 15, &next), "early frame is skipped");
	check(pulse_display_frame_due(2000000000ULL, 15, &next), "display resumes after a stall");
	check(!pulse_display_frame_due(2001000000ULL, 15, &next), "stall does not cause a catch-up burst");
	next = 0;
	check(pulse_display_frame_due(2001000000ULL, 15, &next), "resize or cadence reset draws immediately");
	check(pulse_display_frame_due(1, 15, &next), "backwards clock reset recovers");
	check(pulse_display_frame_due(2, 0, &next) && next == 0, "unlimited mode clears previous cadence");
	std::puts("PASS: display-only preview cadence, 15/30/60 FPS, unrestricted and slower/faster sources; stall, resize and clock recovery.");
}
