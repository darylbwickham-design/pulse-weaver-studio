#include "../../engine/obs-studio/shared/qt/PulseEditorCanvas.hpp"
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>
#include <atomic>
#include <cmath>
#include <util/base.h>
#include <cstdarg>
#include "../../engine/obs-studio/plugins/pulse-weaver-core/pulse-overlay-migration.hpp"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFile>
#include <QJsonArray>
#include <QDir>

static std::atomic<int> runtimeErrors{0};
static bool privateCollectionTest = false;
static void testLog(int level, const char *format, va_list args, void *)
{
	if (level <= LOG_ERROR) ++runtimeErrors;
	if (privateCollectionTest) return; // Never log source names/settings from real profiles.
	std::vfprintf(stderr, format, args);
	std::fputc('\n', stderr);
}

static void check(bool ok, const char *message);

static void checkPrivateCollection(const QString &fileName, obs_canvas_t *portrait)
{
	QFile file(fileName); check(file.open(QIODevice::ReadOnly), "private collection readable");
	const auto originalBytes=file.readAll(); file.close();
	const auto collection=QJsonDocument::fromJson(originalBytes).object();
	check(!collection.isEmpty(), "private collection parses");
	QJsonArray inputs=collection.value("sources").toArray();
	for(const auto &group:collection.value("groups").toArray()) inputs.append(group);
	for(auto it=collection.begin();it!=collection.end();++it) {
		const auto global=it.value().toObject();
		if(global.contains("id") && global.contains("uuid")) inputs.append(global);
	}
	QJsonArray offline; QSet<QString> ids; int originalItems=0;
	for(const auto &value:inputs) {
		auto source=value.toObject(); const QString id=source.value("id").toString();
		// Keep actual IDs/settings/scene graph, but use inert inputs: no cameras,
		// microphones, accounts or external browser URLs can be opened by this test.
		if(id!="scene" && id!="group") { source.insert("id",id=="browser_source"?"browser_source":"color_source_v3"); source.insert("versioned_id",source.value("id")); }
		else { for(const auto &item:source.value("settings").toObject().value("items").toArray())
			if(!item.toObject().value("group_item_backup").toBool()) ++originalItems;
			const QString canvasId=source.value("canvas_uuid").toString();
			if(!canvasId.isEmpty() && canvasId!="6c69626f-6273-4c00-9d88-c5136d61696e") source.insert("canvas_uuid",QString::fromUtf8(obs_canvas_get_uuid(portrait))); }
		source.remove("filters");
		if(id=="browser_source") {
			const QString url=source.value("settings").toObject().value("url").toString();
			const QString prefix="http://127.0.0.1:18754/overlay/";
			if(url.startsWith(prefix)) ids.insert(url.mid(prefix.size()).split('?').first().split('#').first());
		}
		offline.append(source);
	}
	OBSDataAutoRelease wrapper=obs_data_create_from_json(QJsonDocument(QJsonObject{{"sources",offline}}).toJson().constData());
	OBSDataArrayAutoRelease data=obs_data_get_array(wrapper,"sources");
	std::vector<OBSSource> loaded;
	obs_load_sources(data,[](void *context,obs_source_t *source){static_cast<std::vector<OBSSource> *>(context)->emplace_back(source);},&loaded);
	check(loaded.size()==size_t(offline.size()),"all private scene/source identities load in offline harness");
	QHash<QString,QJsonObject> before; int loadedItems=0;
	for(auto &source:loaded) { OBSDataAutoRelease saved=obs_save_source(source); auto object=QJsonDocument::fromJson(obs_data_get_json(saved)).object(); before.insert(QString::fromUtf8(obs_source_get_uuid(source)),object);
		if(obs_scene_from_source(source) || obs_group_from_source(source))
			for(const auto &item:object.value("settings").toObject().value("items").toArray())
				if(!item.toObject().value("group_item_backup").toBool()) ++loadedItems; }
	if(loadedItems!=originalItems) std::printf("Offline item comparison: stored=%d loaded=%d\n",originalItems,loadedItems);
	check(loadedItems==originalItems,"real scene items are not lost during offline load");
	QTemporaryDir journal; const QString capability(64,'b');
	const int migrated=PulseOverlay::migrateManagedSources(journal.path(),18754,capability,ids);
	check(migrated>=0,"real managed browser definitions migrate");
	for(auto &source:loaded) {
		OBSDataAutoRelease saved=obs_save_source(source); auto after=QJsonDocument::fromJson(obs_data_get_json(saved)).object();
		auto expected=before.value(QString::fromUtf8(obs_source_get_uuid(source)));
		auto settings=expected.value("settings").toObject();
		if(after.value("settings").toObject().value("url")!=settings.value("url")) {settings.insert("url",settings.value("url").toString()+"#token="+capability);settings.insert("webpage_control_level",0);expected.insert("settings",settings);}
		check(after==expected,"real source/scene identity and every loaded transform retained during migration");
	}
	check(PulseOverlay::migrateManagedSources(journal.path(),18754,capability,ids)==0,"real collection migration is idempotent");
	for(auto &source:loaded) obs_source_remove(source);
	loaded.clear(); obs_wait_for_destroy_queue();
	check(file.open(QIODevice::ReadOnly) && file.readAll()==originalBytes,"installed collection remains byte-identical");
	std::printf("Private collection PASS: %lld sources/scenes, %d items, %d managed URLs migrated; inert input engines only.\n",(long long)offline.size(),loadedItems,migrated);
}

static void checkManagedMigration(obs_canvas_t *canvas)
{
	QTemporaryDir directory;
	check(directory.isValid(), "migration temporary directory");
	const QSet<QString> ids{"existing-overlay"};
	const QString token(64, 'a');
	check(PulseOverlay::migrateManagedSources(directory.path(), 18754, token, ids) == 0,
	      "pre-load migration with no sources does not consume a one-shot flag");
	for (int collection = 0; collection < 2; ++collection) {
		OBSDataAutoRelease data = obs_data_create_from_json(R"({"id":"browser_source","versioned_id":"browser_source","name":"Existing browser","settings":{"url":"http://127.0.0.1:18754/overlay/existing-overlay?layout=portrait","width":1080,"height":1920,"webpage_control_level":4,"css":"original browser CSS","future":123}})");
		OBSSourceAutoRelease browser = obs_load_source(data);
		obs_source_load2(browser);
		OBSSceneAutoRelease scene = obs_canvas_scene_create(canvas, "Migrated portrait");
		obs_sceneitem_t *item = obs_scene_add(scene, browser);
		obs_transform_info transform{};
		obs_sceneitem_get_info2(item, &transform);
		transform.pos = {137.5f, -42.25f}; transform.scale = {-0.75f, 1.25f}; transform.rot = 23.5f;
		obs_sceneitem_set_info2(item, &transform);
		obs_sceneitem_crop crop{11, 23, 37, 41}; obs_sceneitem_set_crop(item, &crop);
		OBSDataAutoRelease beforeScene = obs_save_source(obs_scene_get_source(scene));
		OBSDataAutoRelease beforeSource = obs_save_source(browser);
		const auto before = QJsonDocument::fromJson(obs_data_get_json(beforeSource)).object();
		check(PulseOverlay::migrateManagedSources(directory.path() + "/missing/parent", 18754, token, ids) < 0,
		      "journal write failure refuses migration");
		OBSDataAutoRelease afterFailure = obs_save_source(browser);
		check(QJsonDocument::fromJson(obs_data_get_json(afterFailure)).object() == before,
		      "failed journal preserves complete browser source");
		check(PulseOverlay::migrateManagedSources(directory.path(), 18754, token, ids) == 1,
		      "post-load and later collection migration find existing source");
		OBSDataAutoRelease afterScene = obs_save_source(obs_scene_get_source(scene));
		check(QJsonDocument::fromJson(obs_data_get_json(afterScene)) == QJsonDocument::fromJson(obs_data_get_json(beforeScene)),
		      "migration preserves all scene items, source references and transforms");
		OBSDataAutoRelease afterSource = obs_save_source(browser);
		auto expected = before;
		auto settings = expected.value("settings").toObject();
		settings.insert("url", settings.value("url").toString() + "#token=" + token);
		settings.insert("webpage_control_level", 0); expected.insert("settings", settings);
		check(QJsonDocument::fromJson(obs_data_get_json(afterSource)).object() == expected,
		      "migration changes only capability URL and control level, retaining source UUID");
		check(PulseOverlay::migrateManagedSources(directory.path(), 18754, token, ids) == 0, "repeat migration is idempotent");
		obs_source_remove(obs_scene_get_source(scene)); obs_source_remove(browser);
		scene = nullptr; browser = nullptr; obs_wait_for_destroy_queue();
	}
}

// Pre-native-editor scene-item JSON: deliberately no new editor metadata.
static void checkLegacyLayout(obs_canvas_t *canvas, obs_source_t *input, bool absolute)
{
	OBSDataAutoRelease coordinateMode = obs_data_create();
	obs_data_set_bool(coordinateMode, "AbsoluteCoordinates", absolute);
	obs_apply_private_data(coordinateMode);
	OBSDataAutoRelease data = obs_data_create_from_json(R"({
		"id":"scene", "versioned_id":"scene", "name":"Legacy layout",
		"settings":{"id_counter":7,"items":[{
			"id":7,"visible":false,"locked":true,"rot":23.5,"align":5,
			"pos":{"x":137.5,"y":412.5},"scale":{"x":-0.75,"y":1.25},
			"bounds_type":2,"bounds_align":5,"bounds_crop":true,
			"bounds":{"x":713.5,"y":1021.5},
			"crop_left":11,"crop_top":23,"crop_right":37,"crop_bottom":41,
			"scale_filter":"bicubic","blend_method":"default","blend_type":"normal"
		}]},"private_settings":{"pulseweaver_linked_scene":"legacy-landscape"}
	})");
	obs_data_set_string(data, "canvas_uuid", obs_canvas_get_uuid(canvas));
	OBSDataAutoRelease settings = obs_data_get_obj(data, "settings");
	OBSDataArrayAutoRelease items = obs_data_get_array(settings, "items");
	OBSDataAutoRelease itemData = obs_data_array_item(items, 0);
	obs_data_set_string(itemData, "name", obs_source_get_name(input));
	obs_data_set_string(itemData, "source_uuid", obs_source_get_uuid(input));
	std::string uuid;
	for (int pass = 0; pass < 2; ++pass) {
		OBSSourceAutoRelease source = obs_load_source(data);
		check(source != nullptr, "legacy scene loads");
		obs_source_load2(source);
		if (pass == 0) uuid = obs_source_get_uuid(source);
		check(uuid == obs_source_get_uuid(source), "upgrade round trip retains scene UUID");
		OBSCanvasAutoRelease loadedCanvas = obs_source_get_canvas(source);
		check(loadedCanvas == canvas, "legacy scene retains its canvas");
		obs_sceneitem_t *item = obs_scene_find_sceneitem_by_id(obs_scene_from_source(source), 7);
		check(item && obs_sceneitem_get_source(item) == input, "legacy item identity and source reference retained");
		obs_transform_info transform{};
		obs_sceneitem_get_info2(item, &transform);
		auto near = [](float a, float b) { return std::fabs(a - b) < 0.001f; };
		check(near(transform.pos.x,137.5f) && near(transform.pos.y,412.5f), "upgrade retains position");
		check(near(transform.scale.x,-0.75f) && near(transform.scale.y,1.25f), "upgrade retains scale and flip");
		check(near(transform.rot,23.5f) && transform.alignment == 5, "upgrade retains rotation and alignment");
		check(transform.bounds_type == 2 && transform.bounds_alignment == 5 && transform.crop_to_bounds &&
			near(transform.bounds.x,713.5f) && near(transform.bounds.y,1021.5f), "upgrade retains bounds");
		obs_sceneitem_crop crop{};
		obs_sceneitem_get_crop(item, &crop);
		check(crop.left == 11 && crop.top == 23 && crop.right == 37 && crop.bottom == 41, "upgrade retains crop");
		check(!obs_sceneitem_visible(item) && obs_sceneitem_locked(item), "upgrade retains visibility and lock");
		check(obs_sceneitem_get_scale_filter(item) == OBS_SCALE_BICUBIC, "upgrade retains scale filter");
		OBSDataAutoRelease privateData = obs_source_get_private_settings(source);
		check(std::string(obs_data_get_string(privateData, "pulseweaver_linked_scene")) == "legacy-landscape",
			"upgrade retains private metadata");
		data = obs_save_source(source);
		obs_source_remove(source);
		source = nullptr;
		obs_wait_for_destroy_queue();
	}
}

static void check(bool ok, const char *message)
{
	if (!ok) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}

int main(int argc, char **argv)
{
	QCoreApplication application(argc, argv);
	privateCollectionTest=argc==4 && std::string(argv[2])=="--collection";
	check(argc >= 2, "pass the OBS runtime path, optionally followed by a fixture JSON output path");
	base_set_log_handler(testLog, nullptr);
	const std::string runtime = argv[1];
	check(obs_startup("en-US", nullptr, nullptr), "OBS startup");
	obs_audio_info audio{48000, SPEAKERS_STEREO};
	check(obs_reset_audio(&audio), "audio startup");
	obs_add_data_path((runtime + "/data/libobs/").c_str());
	const std::string graphics = runtime + "/bin/64bit/libobs-d3d11.dll";
	obs_video_info video{};
	video.graphics_module = graphics.c_str();
	video.fps_num = 30; video.fps_den = 1;
	video.base_width = video.output_width = 1920;
	video.base_height = video.output_height = 1080;
	video.output_format = VIDEO_FORMAT_RGBA;
	check(obs_reset_video(&video) == OBS_VIDEO_SUCCESS, "landscape graphics startup");
	video.base_width = video.output_width = 1080;
	video.base_height = video.output_height = 1920;
	obs_canvas_t *canvas = obs_canvas_create("Pulse Weaver Vertical", &video, PROGRAM);
	check(canvas != nullptr, "portrait canvas creation");
	obs_source_info colourType{};
	colourType.id = "color_source_v3";
	colourType.type = OBS_SOURCE_TYPE_INPUT;
	colourType.output_flags = OBS_SOURCE_VIDEO;
	colourType.get_name = [](void *) { return "Test colour"; };
	colourType.create = [](obs_data_t *, obs_source_t *) -> void * { return new int(1); };
	colourType.destroy = [](void *data) { delete static_cast<int *>(data); };
	colourType.get_width = [](void *) -> uint32_t { return 640; };
	colourType.get_height = [](void *) -> uint32_t { return 360; };
	obs_register_source(&colourType);
	obs_source_info browserType = colourType;
	browserType.id = "browser_source";
	obs_register_source(&browserType);
	if(privateCollectionTest) {
		checkPrivateCollection(QString::fromLocal8Bit(argv[3]),canvas);
		obs_canvas_remove(canvas);obs_canvas_release(canvas);obs_shutdown();
		check(runtimeErrors==0,"no native errors in copied-profile test"); return 0;
	}
	checkManagedMigration(canvas);
	{
		OBSSceneAutoRelease landscape = obs_scene_create("Same name");
		OBSSceneAutoRelease live = obs_canvas_scene_create(canvas, "Portrait programme");
		OBSSceneAutoRelease edit = obs_canvas_scene_create(canvas, "Same name");
		OBSDataAutoRelease colourSettings = obs_data_create();
		obs_data_set_int(colourSettings, "width", 640);
		obs_data_set_int(colourSettings, "height", 360);
		obs_data_set_int(colourSettings, "color", 0xffffa020);
		OBSSourceAutoRelease colour = obs_source_create("color_source_v3", "Shared camera fixture", colourSettings, nullptr);
		{
			OBSCanvasAutoRelease mainCanvas = obs_get_main_canvas();
			for (bool absolute : {true, false}) {
				checkLegacyLayout(canvas, colour, absolute);
				checkLegacyLayout(mainCanvas, colour, absolute);
			}
		}
		obs_sceneitem_t *wideItem = obs_scene_add(landscape, colour);
		obs_sceneitem_t *tallItem = obs_scene_add(edit, colour);
		vec2 position{120.0f, 340.0f};
		obs_sceneitem_set_pos(tallItem, &position);
		vec2 widePosition{};
		obs_sceneitem_get_pos(wideItem, &widePosition);
		check(widePosition.x == 0 && widePosition.y == 0, "shared source placement stays independent per canvas");
		OBSDataAutoRelease transforms = obs_scene_save_transform_states(edit, true);
		check(std::string(obs_data_get_string(transforms, "scene_uuid")) == obs_source_get_uuid(obs_scene_get_source(edit)),
		      "transform undo identifies its portrait editor scene");
		vec2 changed{600.0f, 900.0f};
		obs_sceneitem_set_pos(tallItem, &changed);
		obs_scene_load_transform_states(obs_data_get_json(transforms));
		obs_sceneitem_get_pos(tallItem, &changed);
		check(changed.x == position.x && changed.y == position.y, "portrait transform undo restores placement");
		obs_set_output_source(0, obs_scene_get_source(landscape));
		obs_canvas_set_channel(canvas, 0, obs_scene_get_source(live));
		check(!PulseEditor::IsPortrait(obs_scene_get_source(landscape)), "landscape canvas identity");
		check(PulseEditor::IsPortrait(obs_scene_get_source(edit)), "portrait canvas identity");
		obs_video_info info{};
		check(PulseEditor::VideoInfo(true, &info) && info.base_width == 1080 && info.base_height == 1920,
		      "portrait transform and preview dimensions");
		check(PulseEditor::VideoInfo(false, &info) && info.base_width == 1920 && info.base_height == 1080,
		      "landscape transform and preview dimensions");
		PulseEditor::Selection selection;
		selection.SetShowing(true);
		selection.Select(edit);
		check(selection.Get() == edit.Get(), "selected scene is available to native source tree and renderer");
		{
			OBSSourceAutoRelease programme = obs_canvas_get_channel(canvas, 0);
			OBSSourceAutoRelease mainProgramme = obs_get_output_source(0);
			check(programme == obs_scene_get_source(live), "portrait selection does not change portrait programme");
			check(mainProgramme == obs_scene_get_source(landscape), "portrait selection does not change landscape programme");
		}
		selection.SetShowing(false);
		check(selection.Get() == edit.Get(), "switching tabs retains portrait selection");
		std::atomic<bool> done{false};
		std::thread renderer([&] {
			while (!done) {
				OBSScene scene = selection.Get();
				if (scene) check(obs_source_get_width(obs_scene_get_source(scene)) == 1080, "renderer owns valid portrait scene");
			}
		});
		for (int i = 0; i < 1000; ++i) { selection.Select(live); selection.Select(edit); }
		done = true;
		renderer.join();
		OBSSceneAutoRelease duplicate = obs_scene_duplicate(edit, "Portrait copy", OBS_SCENE_DUP_REFS);
		check(PulseEditor::IsPortrait(obs_scene_get_source(duplicate)), "native duplication retains canvas");
		OBSDataAutoRelease saved = obs_save_source(obs_scene_get_source(duplicate));
		const std::string uuid = obs_source_get_uuid(obs_scene_get_source(duplicate));
		obs_source_remove(obs_scene_get_source(duplicate));
		duplicate = nullptr;
		obs_wait_for_destroy_queue();
		OBSSourceAutoRelease restored = obs_load_source(saved);
		obs_source_load2(restored);
		check(PulseEditor::IsPortrait(restored), "undo recreation restores canvas identity");
		check(uuid == obs_source_get_uuid(restored), "undo recreation retains scene UUID");
		{
			OBSSourceAutoRelease match = obs_get_source_by_uuid(obs_source_get_uuid(obs_scene_get_source(edit)));
			check(match == obs_scene_get_source(edit), "same-name scene undo resolves by UUID");
		}
		if (argc > 2) {
			OBSDataAutoRelease collection = obs_data_create();
			obs_data_set_string(collection, "name", "Camera Editor Test");
			obs_data_set_string(collection, "current_scene", "Same name");
			obs_data_set_string(collection, "current_program_scene", "Same name");
			OBSDataArrayAutoRelease sources = obs_data_array_create();
			OBSDataAutoRelease colourData = obs_save_source(colour);
			obs_data_array_push_back(sources, colourData);
			for (obs_scene_t *scene : {landscape.Get(), live.Get(), edit.Get()}) {
				OBSDataAutoRelease data = obs_save_source(obs_scene_get_source(scene));
				obs_data_array_push_back(sources, data);
			}
			obs_data_set_array(collection, "sources", sources);
			OBSDataArrayAutoRelease canvases = obs_data_array_create();
			OBSDataAutoRelease wrapper = obs_data_create();
			OBSDataAutoRelease canvasData = obs_save_canvas(canvas);
			obs_data_set_obj(wrapper, "info", canvasData);
			obs_data_array_push_back(canvases, wrapper);
			obs_data_set_array(collection, "canvases", canvases);
			check(obs_data_save_json(collection, argv[2]), "write isolated GUI fixture");
		}
		selection.Clear();
		obs_set_output_source(0, nullptr);
		obs_canvas_set_channel(canvas, 0, nullptr);
		// Like frontend collection teardown, remove sources before releasing the
		// final fixture references so render/tick callbacks cannot reacquire them.
		obs_source_remove(restored);
		obs_source_remove(obs_scene_get_source(edit));
		obs_source_remove(obs_scene_get_source(live));
		obs_source_remove(obs_scene_get_source(landscape));
		obs_source_remove(colour);
	}
	obs_canvas_remove(canvas);
	obs_canvas_release(canvas);
	obs_shutdown();
	check(runtimeErrors == 0, "libobs reported no ownership or shutdown errors");
	std::puts("PASS: legacy landscape/portrait layout retention, canvas dimensions, same-name identity, programme isolation, selection retention, concurrent rendering, duplication and undo restore");
}
