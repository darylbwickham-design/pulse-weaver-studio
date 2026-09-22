#include "../../engine/obs-studio/shared/qt/PulseStageExclusions.hpp"
#include "../../engine/obs-studio/shared/qt/PulseOutputSceneSync.hpp"
#include <cstdio>
#include <cstdlib>
#include <atomic>
#include <chrono>
#include <thread>
#include <cmath>
#include <string>

static std::atomic<unsigned> destroyed{0};
static std::atomic<unsigned> frames{0};

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
	type.video_render = [](void *, gs_effect_t *) {
		gs_effect_t *effect = obs_get_base_effect(OBS_EFFECT_SOLID);
		vec4 green{0.0f, 1.0f, 0.0f, 1.0f};
		gs_effect_set_vec4(gs_effect_get_param_by_name(effect, "color"), &green);
		while (gs_effect_loop(effect, "Solid")) gs_draw_sprite(nullptr, 0, 1920, 1080);
	};
	obs_register_source(&type);
}

int main(int argc, char **argv)
{
	check(argc == 2, "pass the OBS runtime directory as the sole argument");
	const std::string runtime = argv[1];
	const std::string graphics = runtime + "/bin/64bit/libobs-d3d11.dll";
	check(obs_startup("en-US", nullptr, nullptr), "OBS startup");
	obs_audio_info audioInfo{};
	audioInfo.samples_per_sec = 48000;
	audioInfo.speakers = SPEAKERS_STEREO;
	check(obs_reset_audio(&audioInfo), "audio callbacks for scene-item cleanup");
	obs_add_data_path((runtime + "/data/libobs/").c_str());
	obs_video_info videoInfo{};
	videoInfo.graphics_module = graphics.c_str();
	videoInfo.fps_num = 30;
	videoInfo.fps_den = 1;
	videoInfo.base_width = videoInfo.output_width = 1920;
	videoInfo.base_height = videoInfo.output_height = 1080;
	videoInfo.output_format = VIDEO_FORMAT_RGBA;
	check(obs_reset_video(&videoInfo) == OBS_VIDEO_SUCCESS, "landscape video startup");
	register_type("exclusion_audio", OBS_SOURCE_AUDIO);
	register_type("exclusion_video", OBS_SOURCE_VIDEO | OBS_SOURCE_CUSTOM_DRAW);
	obs_scene_t *scene = obs_scene_create_private("Landscape");
	obs_source_t *audio = obs_source_create("exclusion_audio", "spotifysound (audio)", nullptr, nullptr);
	obs_source_t *inactive = obs_source_create("exclusion_audio", "Other scene music", nullptr, nullptr);
	obs_source_t *internal = obs_source_create_private("exclusion_audio", "Internal stinger", nullptr);
	obs_source_t *visual = obs_source_create("exclusion_video", "Camera", nullptr, nullptr);
	obs_source_t *unrelated = obs_source_create("exclusion_video", "Unrelated camera", nullptr, nullptr);
	check(scene && audio && inactive && internal && visual && unrelated, "create fixtures");
	obs_sceneitem_t *landscapeCamera = obs_scene_add(scene, visual);
	check(!PulseStageNeedsVideoExclusion(scene, {"spotifysound (audio)"}), "global audio needs no video duplicate");
	check(!PulseStageNeedsVideoExclusion(scene, {"Missing source"}), "stale exclusion needs no video duplicate");
	check(PulseStageNeedsVideoExclusion(scene, {"Camera"}), "scene camera requires video exclusion");
	obs_set_output_source(1, audio);
	auto sources = PulseStageExclusionSources(scene);
	check(sources.size() == 3, "scene visual plus global and inactive audio only");
	check(sources.value("spotifysound (audio)"), "global audio found with exact original name");
	check(sources.value("Other scene music"), "inactive audio available for future Stage routing");
	check(sources.contains("Camera") && !sources.value("Camera"), "scene visual retained without audio annotation");
	check(!sources.contains("Internal stinger"), "private internal audio omitted");
	check(!sources.contains("Unrelated camera"), "unrelated visual omitted");
	obs_scene_add(scene, audio);
	check(!PulseStageNeedsVideoExclusion(scene, {"spotifysound (audio)"}), "scene audio needs no video duplicate");
	obs_scene_add(scene, audio);
	check(PulseStageExclusionSources(scene).size() == 3, "repeated scene/global audio appears once");
	check(PulseStageExclusionSources(nullptr).size() == 2, "audio collection works without a scene");
	obs_scene_t *landscapeCopy = obs_scene_duplicate(scene, "Excluded landscape", OBS_SCENE_DUP_PRIVATE_REFS);
	check(landscapeCopy != nullptr, "private landscape exclusion scene");
	obs_sceneitem_t *copiedLandscapeCamera = obs_scene_find_source(landscapeCopy, "Camera");
	{
		PulseOutputSceneTransformSync transformSync(scene, landscapeCopy);
		vec2 livePosition{420.0f, 260.0f}, liveScale{0.65f, 0.65f};
		obs_sceneitem_set_pos(landscapeCamera, &livePosition);
		obs_sceneitem_set_scale(landscapeCamera, &liveScale);
		vec2 copiedPosition{}, copiedScale{};
		obs_sceneitem_get_pos(copiedLandscapeCamera, &copiedPosition);
		obs_sceneitem_get_scale(copiedLandscapeCamera, &copiedScale);
		check(std::fabs(copiedPosition.x - livePosition.x) < 0.01f &&
			      std::fabs(copiedPosition.y - livePosition.y) < 0.01f,
		      "live 16:9 camera position reaches the active exclusion copy");
		check(std::fabs(copiedScale.x - liveScale.x) < 0.001f &&
			      std::fabs(copiedScale.y - liveScale.y) < 0.001f,
		      "live 16:9 camera scale reaches the active exclusion copy");
		/* Model an animation plugin changing a transform without a corresponding
		 * source-scene signal: the frame reconciliation must repair a stale output
		 * copy without requiring a Stage change. */
		vec2 stalePosition{12.0f, 34.0f};
		obs_sceneitem_set_pos(copiedLandscapeCamera, &stalePosition);
		for (unsigned i = 0; i < 30; ++i) {
			obs_sceneitem_get_pos(copiedLandscapeCamera, &copiedPosition);
			if (std::fabs(copiedPosition.x - livePosition.x) < 0.01f &&
			    std::fabs(copiedPosition.y - livePosition.y) < 0.01f)
				break;
			std::this_thread::sleep_for(std::chrono::milliseconds(20));
		}
		check(std::fabs(copiedPosition.x - livePosition.x) < 0.01f &&
			      std::fabs(copiedPosition.y - livePosition.y) < 0.01f,
		      "frame sync repairs a stale 16:9 output transform without a Stage change");
	}
	obs_scene_release(landscapeCopy);
	videoInfo.base_width = videoInfo.output_width = 1080;
	videoInfo.base_height = videoInfo.output_height = 1920;
	obs_canvas_t *portrait = obs_canvas_create("Portrait regression", &videoInfo, ACTIVATE | EPHEMERAL);
	check(portrait != nullptr, "portrait canvas");
	obs_scene_t *portraitScene = obs_canvas_scene_create(portrait, "Portrait scene");
	obs_sceneitem_t *camera = obs_scene_add(portraitScene, visual);
	vec2 position{180.0f, 240.0f}, scale{0.375f, 0.375f};
	obs_sceneitem_set_pos(camera, &position);
	obs_sceneitem_set_scale(camera, &scale);
	obs_scene_t *copy = obs_scene_duplicate(portraitScene, "Excluded portrait", OBS_SCENE_DUP_PRIVATE_REFS);
	check(copy != nullptr, "private exclusion scene");
	obs_source_t *copySource = obs_scene_get_source(copy);
	check(obs_source_get_width(copySource) == 1080 && obs_source_get_height(copySource) == 1920,
	      "private portrait retains 1080x1920 coordinate space");
	obs_sceneitem_t *copiedCamera = obs_scene_find_source(copy, "Camera");
	vec2 copiedPosition{}, copiedScale{};
	obs_sceneitem_get_pos(copiedCamera, &copiedPosition);
	obs_sceneitem_get_scale(copiedCamera, &copiedScale);
	check(std::fabs(copiedPosition.x - position.x) < 0.01f && std::fabs(copiedPosition.y - position.y) < 0.01f,
	      "portrait copy preserves camera position");
	check(std::fabs(copiedScale.x - scale.x) < 0.001f && std::fabs(copiedScale.y - scale.y) < 0.001f,
	      "portrait copy preserves camera scale");
	{
		PulseOutputSceneTransformSync transformSync(portraitScene, copy);
		vec2 livePosition{360.0f, 480.0f}, liveScale{0.5f, 0.5f};
		obs_sceneitem_crop liveCrop{8, 16, 24, 32};
		/* Crop setters are folded into the next item-transform notification by
		 * libobs, just as the editor's deferred transform update does. */
		obs_sceneitem_set_crop(camera, &liveCrop);
		obs_sceneitem_set_pos(camera, &livePosition);
		obs_sceneitem_set_scale(camera, &liveScale);
		obs_sceneitem_get_pos(copiedCamera, &copiedPosition);
		obs_sceneitem_get_scale(copiedCamera, &copiedScale);
		obs_sceneitem_crop copiedCrop{};
		obs_sceneitem_get_crop(copiedCamera, &copiedCrop);
		check(std::fabs(copiedPosition.x - livePosition.x) < 0.01f &&
			      std::fabs(copiedPosition.y - livePosition.y) < 0.01f,
		      "live camera position reaches the active exclusion copy");
		check(std::fabs(copiedScale.x - liveScale.x) < 0.001f &&
			      std::fabs(copiedScale.y - liveScale.y) < 0.001f,
		      "live camera scale reaches the active exclusion copy");
		check(copiedCrop.left == liveCrop.left && copiedCrop.top == liveCrop.top &&
			      copiedCrop.right == liveCrop.right && copiedCrop.bottom == liveCrop.bottom,
		      "live camera crop reaches the active exclusion copy");
		obs_sceneitem_crop noCrop{};
		obs_sceneitem_set_crop(camera, &noCrop);
		obs_sceneitem_set_pos(camera, &position);
		obs_sceneitem_set_scale(camera, &scale);
	}
	obs_sceneitem_set_visible(copiedCamera, false);
	check(obs_sceneitem_visible(camera), "output exclusions leave original scene visible");
	obs_sceneitem_set_visible(copiedCamera, true);
	obs_canvas_set_channel(portrait, 0, copySource);
	check(obs_canvas_get_video(portrait) != nullptr, "routed portrait supplies encoder video");
	auto capture = [](void *opaque, video_data *frame) {
		const auto *inside = frame->data[0] + 300 * frame->linesize[0] + 300 * 4;
		const auto *outside = frame->data[0] + 100 * frame->linesize[0] + 100 * 4;
		const auto *edge = frame->data[0] + 600 * frame->linesize[0] + 850 * 4;
		if (inside[1] > 200 && edge[1] > 200 && outside[1] < 20)
			++frames;
	};
	video_t *outputVideo = obs_canvas_get_video(portrait);
	obs_output_info outputType{};
	outputType.id = "portrait_test_output";
	outputType.flags = OBS_OUTPUT_VIDEO;
	outputType.get_name = [](void *) { return "Local portrait capture"; };
	outputType.create = [](obs_data_t *, obs_output_t *output) -> void * { return output; };
	outputType.destroy = [](void *) {};
	outputType.start = [](void *data) { return obs_output_begin_data_capture(static_cast<obs_output_t *>(data), 0); };
	outputType.stop = [](void *data, uint64_t) { obs_output_end_data_capture(static_cast<obs_output_t *>(data)); };
	outputType.raw_video = capture;
	obs_register_output(&outputType);
	obs_output_t *output = obs_output_create("portrait_test_output", "Local capture", nullptr, nullptr);
	check(output != nullptr, "create local video output");
	obs_output_set_media(output, outputVideo, nullptr);
	check(obs_output_start(output), "start local portrait output");
	for (unsigned i = 0; i < 100 && frames.load() < 3; ++i)
		std::this_thread::sleep_for(std::chrono::milliseconds(30));
	obs_output_stop(output);
	obs_output_release(output);
	check(frames.load() >= 3, "portrait output frames retain camera size and position");
	obs_canvas_set_channel(portrait, 0, nullptr);
	obs_scene_release(copy);
	obs_canvas_scene_remove(portraitScene);
	obs_scene_release(portraitScene);
	obs_canvas_release(portrait);
	obs_set_output_source(1, nullptr);
	obs_scene_release(scene);
	for (auto source : {audio, inactive, internal, visual, unrelated}) obs_source_release(source);
	for (unsigned i = 0; i < 100 && destroyed.load() != 5; ++i) {
		obs_wait_for_destroy_queue();
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}
	check(destroyed.load() == 5, "fixture sources released before shutdown");
	obs_shutdown();
	std::puts("PASS: exclusions, portrait output, scene isolation, and live transform synchronisation.");
}
