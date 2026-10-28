#include "../../engine/obs-studio/frontend/widgets/PulseHudOutput.hpp"
#include "../../engine/obs-studio/shared/qt/PulseEditorCanvas.hpp"
#include <QApplication>
#include <QElapsedTimer>
#include <QImage>
#include <QScreen>
#include <QThread>
#include <QVector>
#include <obs.hpp>
#include <graphics/graphics.h>
#include <graphics/vec4.h>
#include <windows.h>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <string>

static int checks = 0;
static void check(bool okay, const char *message)
{
	if (!okay) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
	++checks;
}
static void events(int ms)
{
	QElapsedTimer timer; timer.start();
	while (timer.elapsed() < ms) { QApplication::processEvents(); QThread::msleep(2); }
}
static void waitForHud(PulseHudOutput &hud)
{
	QElapsedTimer timer; timer.start();
	while (!hud.Running() && timer.elapsed() < 3000) events(10);
	check(hud.Running(), "GPU HUD presents while no stream/recording encoder is running");
}
static OBSSourceAutoRelease input(const char *name, int variant)
{
	OBSDataAutoRelease settings = obs_data_create(); obs_data_set_int(settings, "variant", variant);
	return OBSSourceAutoRelease(obs_source_create("hud_test_pattern", name, settings, nullptr));
}

int main(int argc, char **argv)
{
	QApplication app(argc, argv);
	check(argc == 2, "pass the inert runtime root");
	const std::string runtime = argv[1];
	check(obs_startup("en-US", nullptr, nullptr), "OBS starts");
	obs_add_data_path((runtime + "/data/libobs/").c_str());
	obs_audio_info audio{48000, SPEAKERS_STEREO};
	check(obs_reset_audio(&audio), "inert audio engine starts");
	const std::string graphics = runtime + "/bin/64bit/libobs-d3d11.dll";
	obs_video_info info{}; info.graphics_module = graphics.c_str();
	info.base_width = info.output_width = 320; info.base_height = info.output_height = 180;
	info.fps_num = 30; info.fps_den = 1; info.output_format = VIDEO_FORMAT_BGRA;
	check(obs_reset_video(&info) == OBS_VIDEO_SUCCESS, "native D3D11 starts");
	obs_source_info type{}; type.id = "hud_test_pattern"; type.type = OBS_SOURCE_TYPE_INPUT;
	type.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_CUSTOM_DRAW;
	type.get_name = [](void *) { return "Inert HUD pattern"; };
	type.create = [](obs_data_t *s, obs_source_t *) -> void * { return new int(int(obs_data_get_int(s, "variant"))); };
	type.destroy = [](void *p) { delete static_cast<int *>(p); };
	type.get_width = [](void *) -> uint32_t { return 64; };
	type.get_height = [](void *) -> uint32_t { return 32; };
	type.video_render = [](void *p, gs_effect_t *) {
		const int variant = *static_cast<int *>(p);
		vec4 color{};
		if (variant == 0) vec4_set(&color, 1, 0, 0, .5f);
		else if (variant == 1) vec4_set(&color, 0, 1, 0, 1);
		else vec4_set(&color, 0, 0, 1, 1);
		auto *solid = obs_get_base_effect(OBS_EFFECT_SOLID);
		const bool previous = gs_framebuffer_srgb_enabled();
		gs_enable_framebuffer_srgb(gs_get_linear_srgb());
		gs_effect_set_vec4(gs_effect_get_param_by_name(solid, "color"), &color);
		while (gs_effect_loop(solid, "Solid")) gs_draw_sprite(nullptr, 0, 64, 32);
		gs_enable_framebuffer_srgb(previous);
	};
	obs_register_source(&type);
	{
		OBSSceneAutoRelease mainScene = obs_scene_create("Main programme");
		auto green = input("Main green", 1); obs_scene_add(mainScene, green);
		obs_set_output_source(0, obs_scene_get_source(mainScene));
		info.base_width = info.output_width = 180; info.base_height = info.output_height = 320;
		OBSCanvasAutoRelease portrait = obs_canvas_create("Pulse Weaver Vertical", &info, PROGRAM);
		OBSSceneAutoRelease portraitScene = obs_canvas_scene_create(portrait, "Portrait programme");
		auto blue = input("Portrait blue", 2); obs_scene_add(portraitScene, blue);
		obs_canvas_set_channel(portrait, 0, obs_scene_get_source(portraitScene));
		info.base_width = info.output_width = 256; info.base_height = info.output_height = 128;
		OBSCanvasAutoRelease hudCanvas = obs_canvas_create("Pulse Weaver HUD", nullptr, SCENE_REF);
		check(hudCanvas != nullptr, "HUD has independent native canvas");
		check(!obs_canvas_has_video(hudCanvas), "HUD adds no video output mix or encoder");
		OBSSceneAutoRelease hudScene = obs_canvas_scene_create(hudCanvas, "HUD effects");
		check(obs_scene_set_custom_size(hudScene, 256, 128), "HUD takes display dimensions without a video reset");
		auto red = input("HUD translucent red", 0); auto *item = obs_scene_add(hudScene, red);
		vec2 pos{32, 16}; obs_sceneitem_set_pos(item, &pos);
		check(PulseEditor::IsHud(obs_scene_get_source(hudScene)), "HUD ownership identified");
		check(!PulseEditor::IsAvailableInEditor(obs_scene_get_source(hudScene), false), "HUD hidden from landscape editor");
		check(!PulseEditor::IsAvailableInEditor(obs_scene_get_source(hudScene), true), "HUD hidden from portrait editor");
		auto screen = PulseHudOutput::Describe(QGuiApplication::primaryScreen());
		QVector<QRect> nativeScreens;
		EnumDisplayMonitors(nullptr, nullptr, [](HMONITOR monitor, HDC, LPRECT, LPARAM value) -> BOOL {
			MONITORINFO info{}; info.cbSize = sizeof(info);
			if (GetMonitorInfoW(monitor, &info)) reinterpret_cast<QVector<QRect> *>(value)->push_back(
				QRect(info.rcMonitor.left, info.rcMonitor.top, info.rcMonitor.right - info.rcMonitor.left, info.rcMonitor.bottom - info.rcMonitor.top));
			return TRUE;
		}, reinterpret_cast<LPARAM>(&nativeScreens));
		QVector<QRect> resolvedScreens;
		for (QScreen *candidate : QGuiApplication::screens()) {
			const auto physical = PulseHudOutput::Describe(candidate);
			check(physical.valid() && nativeScreens.contains(physical.pixels), "screen tile maps to a real native monitor rectangle");
			check(!resolvedScreens.contains(physical.pixels), "different screens do not map to the same monitor");
			resolvedScreens.push_back(physical.pixels);
		}
		check(screen.valid(), "native display physical dimensions resolve");
		check(screen.pixels.width() > 0 && screen.pixels.height() > 0, "display defines usable pixel extent");
		// A small surface exercises the native renderer without covering a user's display.
		screen.pixels = QRect(screen.pixels.x() + 200, screen.pixels.y() + 100, 256, 128);
		PulseHudOutput hud;
		QString error;
		hud.stateChanged = [&error](bool, const QString &value) { if (!value.isEmpty()) error = value; };
		check(!hud.Start(nullptr, screen).isEmpty() && !hud.Active(), "invalid start stays off");
		check(hud.Start(obs_scene_get_source(hudScene), screen).isEmpty(), "HUD accepts matching screen-size canvas");
		waitForHud(hud); events(150);
		check(error.isEmpty(), "no native composition failure");
		const QImage frame = hud.ReadbackForTest();
		check(frame.size() == QSize(256, 128), "GPU surface uses HUD dimensions");
		check(qAlpha(frame.pixel(0, 0)) == 0, "empty canvas is fully transparent");
		const QRgb translucent = frame.pixel(40, 24);
		check(qAlpha(translucent) >= 126 && qAlpha(translucent) <= 129, "partial alpha retained");
		check(qRed(translucent) >= 126 && qRed(translucent) <= 129 && qGreen(translucent) == 0 && qBlue(translucent) == 0,
		      "premultiplied glow color retained without an opaque black matte");
		auto hwnd = reinterpret_cast<HWND>(hud.WindowForTest());
		check(IsWindow(hwnd) && IsWindowVisible(hwnd), "native overlay is visible after presentation");
		check((GetWindowLongPtrW(hwnd, GWL_EXSTYLE) & WS_EX_NOACTIVATE) != 0, "HUD cannot steal activation");
		POINT at{screen.pixels.x() + 40, screen.pixels.y() + 24};
		check(WindowFromPoint(at) != hwnd, "Windows routes input to the window underneath HUD effects");
		check(OBSSourceAutoRelease(obs_get_output_source(0)) == obs_scene_get_source(mainScene), "main programme unchanged");
		check(OBSSourceAutoRelease(obs_canvas_get_channel(portrait, 0)) == obs_scene_get_source(portraitScene), "portrait programme unchanged");
		{
			OBSSceneAutoRelease otherStage = obs_scene_create("Another stage or Look");
			obs_scene_add(otherStage, green);
			obs_set_output_source(0, obs_scene_get_source(otherStage));
			obs_canvas_set_channel(portrait, 0, nullptr);
			const auto previous = hud.FramesForTest(); events(100);
			check(hud.Running() && hud.FramesForTest() > previous, "stage and Look programme changes leave the same HUD running");
			const auto unchanged = hud.ReadbackForTest();
			check(qAlpha(unchanged.pixel(40, 24)) >= 126 && qAlpha(unchanged.pixel(0, 0)) == 0,
			      "broadcast programme changes do not replace HUD effects or transparency");
			obs_set_output_source(0, obs_scene_get_source(mainScene));
			obs_canvas_set_channel(portrait, 0, obs_scene_get_source(portraitScene));
			obs_source_remove(obs_scene_get_source(otherStage));
		}
		const auto before = hud.FramesForTest(); events(250);
		check(hud.FramesForTest() - before <= 10, "multiple video mixes do not multiply HUD presentation rate");
		hud.Stop(); const auto stopped = hud.FramesForTest(); events(100);
		check(!hud.Active() && !hud.Running() && !IsWindow(hwnd), "off destroys output window and clears actual running state");
		check(hud.FramesForTest() == stopped, "off performs no background HUD renders");
		check(!obs_source_showing(red), "off holds no visibility reference on HUD sources");
		const int64_t stableItemId = obs_sceneitem_get_id(item);
		obs_transform_info beforeResize{}, afterResize{};
		obs_sceneitem_get_info2(item, &beforeResize);
		check(obs_scene_set_custom_size(hudScene, 320, 160), "HUD can resize independently of active programme canvases");
		obs_sceneitem_get_info2(item, &afterResize);
		check(std::abs(afterResize.pos.x - beforeResize.pos.x) < 0.001f && std::abs(afterResize.pos.y - beforeResize.pos.y) < 0.001f &&
		      std::abs(afterResize.scale.x - beforeResize.scale.x) < 0.001f && std::abs(afterResize.scale.y - beforeResize.scale.y) < 0.001f,
		      "switching displays preserves the HUD layers' pixel placement and scale");
		check(obs_scene_find_sceneitem_by_id(hudScene, stableItemId) == item && obs_sceneitem_get_source(item) == red.Get(),
		      "HUD resize preserves item and shared input identities");
		check(obs_scene_set_custom_size(hudScene, 256, 128), "HUD returns to selected display size");
		check(hud.Start(obs_scene_get_source(hudScene), screen).isEmpty(), "HUD restarts");
		QThread::msleep(90); // Let the video thread queue its first-frame notification without dispatching Qt events.
		hud.Stop(); // Leave a pending first-frame notification, then immediately restart.
		check(hud.Start(obs_scene_get_source(hudScene), screen).isEmpty(), "rapid start/stop can restart safely");
		waitForHud(hud); events(80); hud.Stop();
		obs_set_output_source(0, nullptr);
		obs_canvas_set_channel(portrait, 0, nullptr); obs_canvas_set_channel(hudCanvas, 0, nullptr);
		obs_source_remove(obs_scene_get_source(mainScene));
		obs_source_remove(obs_scene_get_source(portraitScene)); obs_source_remove(obs_scene_get_source(hudScene));
		obs_source_remove(green); obs_source_remove(blue); obs_source_remove(red);
		obs_canvas_remove(portrait); obs_canvas_remove(hudCanvas);
	}
	obs_wait_for_destroy_queue(); obs_shutdown();
	std::printf("PASS: %d native HUD transparency, input routing, isolation and lifecycle checks.\n", checks);
	return 0;
}
