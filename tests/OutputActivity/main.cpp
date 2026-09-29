#include "../../engine/obs-studio/shared/qt/PulseOutputActivity.hpp"
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <string>

static void check(bool passed, const char *message)
{
	if (!passed) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
int main(int argc, char **argv)
{
	check(argc == 2, "pass the test OBS runtime directory");
	check(obs_startup("en-US", nullptr, nullptr), "OBS startup");
	obs_audio_info audio{48000, SPEAKERS_STEREO};
	check(obs_reset_audio(&audio), "fake output audio clock");
	const std::string runtime = argv[1];
	obs_add_data_path((runtime + "/data/libobs/").c_str());
	const std::string graphics = runtime + "/bin/64bit/libobs-d3d11.dll";
	obs_video_info video{};
	video.graphics_module = graphics.c_str();
	video.fps_num = 30; video.fps_den = 1;
	video.base_width = video.output_width = 32;
	video.base_height = video.output_height = 32;
	video.output_format = VIDEO_FORMAT_RGBA;
	check(obs_reset_video(&video) == OBS_VIDEO_SUCCESS, "local output video context");
	obs_output_info type{};
	type.id = "pulse_activity_fixture";
	type.flags = OBS_OUTPUT_AUDIO;
	type.get_name = [](void *) { return "Local activity fixture"; };
	type.create = [](obs_data_t *, obs_output_t *output) -> void * { return output; };
	type.destroy = [](void *) {};
	type.start = [](void *opaque) { return obs_output_begin_data_capture(static_cast<obs_output_t *>(opaque), 0); };
	type.stop = [](void *opaque, uint64_t) { obs_output_end_data_capture(static_cast<obs_output_t *>(opaque)); };
	type.raw_audio = [](void *, audio_data *) {};
	obs_register_output(&type);
	check(!PulseHasActiveOutputs(), "idle app permits settings");
	for (const auto *name : {"Twitch", "pulse_weaver_youtube_output_primary", "pulse_weaver_youtube_output_secondary",
		"pulse_weaver_kick_output", "pulse_weaver_recording_output_horizontal", "pulse_weaver_recording_output_vertical"}) {
		auto *output = obs_output_create(type.id, name, nullptr, nullptr);
		check(output != nullptr, "create local fixture");
		check(!PulseHasActiveOutputs(), "inactive output permits settings");
		obs_output_set_media(output, nullptr, obs_get_audio());
		check(obs_output_start(output), "start local fixture");
		check(PulseHasActiveOutputs(), "independent active output protects settings");
		obs_output_force_stop(output);
		obs_output_release(output);
		obs_wait_for_destroy_queue();
		check(!PulseHasActiveOutputs(), "stopped output permits settings again");
	}
	obs_shutdown();
	std::puts("PASS: all six native and independent output routes protect settings while active.");
}
