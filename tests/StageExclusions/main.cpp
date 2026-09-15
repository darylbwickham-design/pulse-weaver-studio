#include "../../engine/obs-studio/shared/qt/PulseStageExclusions.hpp"
#include <cstdio>
#include <cstdlib>
#include <atomic>
#include <chrono>
#include <thread>

static std::atomic<unsigned> destroyed{0};

static void check(bool ok, const char *message)
{
	if (!ok) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}

static void register_type(const char *id, uint32_t flags)
{
	obs_source_info type{};
	type.id = id;
	type.type = OBS_SOURCE_TYPE_INPUT;
	type.output_flags = flags;
	type.get_name = [](void *) { return "Stage exclusion fixture"; };
	type.create = [](obs_data_t *, obs_source_t *) -> void * { return new int(1); };
	type.destroy = [](void *data) { delete static_cast<int *>(data); ++destroyed; };
	type.get_width = [](void *) -> uint32_t { return 1920; };
	type.get_height = [](void *) -> uint32_t { return 1080; };
	type.video_render = [](void *, gs_effect_t *) {};
	obs_register_source(&type);
}

int main()
{
	check(obs_startup("en-US", nullptr, nullptr), "OBS startup");
	obs_audio_info audioInfo{};
	audioInfo.samples_per_sec = 48000;
	audioInfo.speakers = SPEAKERS_STEREO;
	check(obs_reset_audio(&audioInfo), "audio callbacks for scene-item cleanup");
	register_type("exclusion_audio", OBS_SOURCE_AUDIO);
	register_type("exclusion_video", OBS_SOURCE_VIDEO);
	obs_scene_t *scene = obs_scene_create_private("Landscape");
	obs_source_t *audio = obs_source_create("exclusion_audio", "spotifysound (audio)", nullptr, nullptr);
	obs_source_t *inactive = obs_source_create("exclusion_audio", "Other scene music", nullptr, nullptr);
	obs_source_t *internal = obs_source_create_private("exclusion_audio", "Internal stinger", nullptr);
	obs_source_t *visual = obs_source_create("exclusion_video", "Camera", nullptr, nullptr);
	obs_source_t *unrelated = obs_source_create("exclusion_video", "Unrelated camera", nullptr, nullptr);
	check(scene && audio && inactive && internal && visual && unrelated, "create fixtures");
	obs_scene_add(scene, visual);
	obs_set_output_source(1, audio);
	auto sources = PulseStageExclusionSources(scene);
	check(sources.size() == 3, "scene visual plus global and inactive audio only");
	check(sources.value("spotifysound (audio)"), "global audio found with exact original name");
	check(sources.value("Other scene music"), "inactive audio available for future Stage routing");
	check(sources.contains("Camera") && !sources.value("Camera"), "scene visual retained without audio annotation");
	check(!sources.contains("Internal stinger"), "private internal audio omitted");
	check(!sources.contains("Unrelated camera"), "unrelated visual omitted");
	obs_scene_add(scene, audio);
	obs_scene_add(scene, audio);
	check(PulseStageExclusionSources(scene).size() == 3, "repeated scene/global audio appears once");
	check(PulseStageExclusionSources(nullptr).size() == 2, "audio collection works without a scene");
	obs_set_output_source(1, nullptr);
	obs_scene_release(scene);
	for (auto source : {audio, inactive, internal, visual, unrelated}) obs_source_release(source);
	for (unsigned i = 0; i < 100 && destroyed.load() != 5; ++i) {
		obs_wait_for_destroy_queue();
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}
	check(destroyed.load() == 5, "fixture sources released before shutdown");
	obs_shutdown();
	std::puts("PASS: global/inactive audio, original names, visual scoping, deduplication and private-source isolation.");
}
