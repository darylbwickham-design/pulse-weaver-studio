#include "../../engine/obs-studio/plugins/pulse-weaver-core/pulse-motion-engine.hpp"
#include "../../engine/obs-studio/plugins/pulse-weaver-core/pulse-scene-item-ref.hpp"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <util/base.h>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

static int checks = 0, runtimeErrors = 0;
static std::atomic<uint32_t> fixtureWidth{640}, fixtureHeight{480};

static void check(bool ok, const char *message)
{
	++checks;
	if (!ok) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}

static void logMessage(int level, const char *format, va_list arguments, void *)
{
	// This deliberately isolated engine has no frontend window/callback owner.
	// The frontend API returns null for these expected window catalogue reads.
	if (std::strcmp(format, "Tried to call %s with no callbacks!") == 0) return;
	if (level <= LOG_ERROR) {
		++runtimeErrors;
		std::vfprintf(stderr, format, arguments);
		std::fputc('\n', stderr);
	}
}

static QByteArray readFile(const QString &path)
{
	QFile file(path);
	check(file.open(QIODevice::ReadOnly), "read fixture");
	return file.readAll();
}

static void writeFile(const QString &path, const QByteArray &bytes)
{
	QFile file(path);
	check(file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size(), "write fixture");
}

struct PulseMotionEngineTestAccess {
	static bool save(PulseMotionEngine &engine) { return engine.save(); }
	static bool valid(PulseMotionEngine &engine) { return engine.originalStoreValid; }
	static QJsonArray actions(PulseMotionEngine &engine) { return engine.actions; }
	static void setActions(PulseMotionEngine &engine, const QJsonArray &actions) { engine.actions = actions; }
	static void remove(PulseMotionEngine &engine, const QString &id) { engine.editingId = id; engine.deleteEditorAction(); }
	static QString editing(PulseMotionEngine &engine) { return engine.editingId; }
	static void draft(PulseMotionEngine &engine, obs_scene_t *scene, obs_sceneitem_t *item)
	{
		engine.draftScene = scene;
		engine.draftContainer = "fixture";
		engine.mainContainer = "fixture";
		engine.draftIds.insert(obs_sceneitem_get_id(item), obs_sceneitem_get_id(item));
	}
	static QJsonArray snapshot(PulseMotionEngine &engine) { return engine.draftSnapshot(); }
	static void apply(PulseMotionEngine &engine, const QJsonArray &snapshot) { engine.applyDraftSnapshot(snapshot); }
	static QJsonObject savedTransform(const QJsonObject &row, obs_source_t *source)
	{
		return PulseMotionEngine::serialize(PulseMotionEngine::savedTransformForSource(row, source));
	}
	static OBSSceneItem resolve(PulseMotionEngine &engine, const QString &container, qint64 id, const QString &name,
				const QString &uuid, bool recursive)
	{
		return engine.resolveItem(container, id, name, recursive, uuid);
	}
	static void active(PulseMotionEngine &engine, obs_sceneitem_t *item)
	{
		PulseMotionEngine::Track track;
		track.item = item;
		track.baseline = engine.capture(item);
		engine.active = std::make_unique<PulseMotionEngine::Execution>();
		engine.active->tracks.push_back(track);
		engine.active->phase = "holding";
		engine.active->action.insert("returnStage", true);
		engine.lastRestore.push_back(track);
		engine.requestResults.insert("old-collection", {{"ok", true}});
		engine.animationTimer.start();
		engine.stageTimer.start(60000);
		engine.previewTimer.start(60000);
	}
	static bool cleared(PulseMotionEngine &engine)
	{
		return !engine.active && engine.lastRestore.empty() && engine.requestResults.isEmpty() &&
			!engine.animationTimer.isActive() && !engine.stageTimer.isActive() && !engine.previewTimer.isActive() &&
			!engine.draftScene && !engine.previewScene && engine.parkedDrafts.isEmpty() && engine.draftIds.isEmpty();
	}
};

static QJsonObject draftAction()
{
	return {{"id", "fixture-look"}, {"name", "Fixture"}, {"kind", "layout"}, {"draft", true}};
}

static void storageTests()
{
	QTemporaryDir directory;
	check(directory.isValid(), "private storage fixtures");
	const QList<QByteArray> invalid{
		"{broken", "[]", "{}", "{\"version\":2,\"actions\":[]}",
		"{\"version\":1.5,\"actions\":[]}", "{\"version\":1,\"actions\":{}}",
		"{\"version\":1,\"actions\":[false]}", "{\"version\":1,\"actions\":[],\"originals\":{}}",
		"{\"version\":1,\"actions\":[],\"originalScenes\":[]}"};
	int index = 0;
	for (const auto &bytes : invalid) {
		const QString path = directory.filePath(QString::number(index++) + ".json");
		writeFile(path, bytes);
		PulseMotionEngine engine(nullptr, path);
		check(!PulseMotionEngineTestAccess::valid(engine), "unsupported or malformed storage is read-only");
		check(!PulseMotionEngineTestAccess::save(engine), "unsupported storage cannot be overwritten");
		const auto result = engine.importDocument({{"version", 1}, {"actions", QJsonArray{draftAction()}}});
		check(!result.value("ok").toBool() && result.value("imported").toInt() == 0, "failed import reports failure");
		check(PulseMotionEngineTestAccess::actions(engine).isEmpty(), "failed import rolls back catalogue memory");
		check(readFile(path) == bytes, "failed edit leaves original bytes intact");
	}
	const QString validPath = directory.filePath("valid.json");
	QJsonObject original{{"version", 1}, {"actions", QJsonArray{draftAction()}},
		{"extension", QJsonObject{{"keep", QJsonArray{1, "future"}}}}};
	writeFile(validPath, QJsonDocument(original).toJson());
	{
		PulseMotionEngine engine(nullptr, validPath);
		check(PulseMotionEngineTestAccess::valid(engine), "legacy v1 without optional originals loads");
		check(PulseMotionEngineTestAccess::save(engine), "supported store saves");
		check(QJsonDocument::fromJson(readFile(validPath)).object().value("extension") == original.value("extension"),
			"unknown supported-schema fields survive save");
		PulseMotionEngineTestAccess::remove(engine, "fixture-look");
		check(PulseMotionEngineTestAccess::actions(engine).isEmpty(), "successful delete removes action");
		check(QJsonDocument::fromJson(readFile(validPath)).object().value("actions").toArray().isEmpty(), "delete persisted");
	}
	const QString blocker = directory.filePath("ordinary-file");
	writeFile(blocker, "This is not a directory.");
	PulseMotionEngine failed(nullptr, blocker + "/motion.json");
	PulseMotionEngineTestAccess::setActions(failed, QJsonArray{draftAction()});
	PulseMotionEngineTestAccess::remove(failed, "fixture-look");
	check(PulseMotionEngineTestAccess::actions(failed) == QJsonArray{draftAction()}, "failed delete restores catalogue");
	check(PulseMotionEngineTestAccess::editing(failed) == "fixture-look", "failed delete retains selected look");
	const auto result = failed.importDocument({{"version", 1}, {"actions", QJsonArray{draftAction()}}});
	check(!result.value("ok").toBool() && PulseMotionEngineTestAccess::actions(failed).size() == 1,
		"failed import preserves existing actions");
}

static void sceneTests()
{
	QTemporaryDir directory;
	PulseMotionEngine engine(nullptr, directory.filePath("motion.json"));
	OBSSceneAutoRelease scene = obs_scene_create("Motion audit scene");
	obs_sceneitem_t *group = obs_scene_add_group(scene, "Motion audit group");
	obs_source_info type{};
	type.id = "pulse_motion_audit_fixture";
	type.type = OBS_SOURCE_TYPE_INPUT;
	type.output_flags = OBS_SOURCE_VIDEO;
	type.get_name = [](void *) { return "Inert motion fixture"; };
	type.get_width = [](void *) { return fixtureWidth.load(); };
	type.get_height = [](void *) { return fixtureHeight.load(); };
	type.create = [](obs_data_t *, obs_source_t *) -> void * { return new int(1); };
	type.destroy = [](void *data) { delete static_cast<int *>(data); };
	obs_register_source(&type);
	OBSSourceAutoRelease source = obs_source_create_private(type.id, "Original source", nullptr);
	obs_sceneitem_t *nested = obs_scene_add(obs_sceneitem_group_get_scene(group), source);
	const QString uuid = QString::fromUtf8(obs_source_get_uuid(source));
	const qint64 id = obs_sceneitem_get_id(nested);
	obs_source_set_name(source, "Renamed source");
	check(PulseRuntimeSafety::findSceneItemByUuid(scene, uuid.toUtf8().constData(), true) == nested,
		"recursive UUID lookup survives grouped source rename");
	check(!PulseRuntimeSafety::findSceneItemByUuid(scene, uuid.toUtf8().constData(), false), "direct lookup stays within its container");
	check(!PulseRuntimeSafety::findSceneItemByUuid(scene, ""), "empty identity does not select a source");
	check(PulseMotionEngineTestAccess::resolve(engine, "Motion audit scene", id, "Original source", uuid, true) == nested,
		"production action resolver finds renamed grouped source");
	check(!PulseMotionEngineTestAccess::resolve(engine, "Motion audit scene", id, "Original source", uuid, false),
		"nonrecursive layout lookup cannot jump into a group");

	OBSSceneAutoRelease draft = obs_scene_create_private("Motion audit draft");
	obs_sceneitem_t *item = obs_scene_add(draft, source);
	obs_sceneitem_set_bounds_type(item, OBS_BOUNDS_NONE);
	vec2 scale{0.5f, -0.75f};
	obs_sceneitem_set_scale(item, &scale);
	obs_sceneitem_crop crop{20, 10, 30, 15};
	obs_sceneitem_set_crop(item, &crop);
	PulseMotionEngineTestAccess::draft(engine, draft, item);
	const QJsonArray saved = PulseMotionEngineTestAccess::snapshot(engine);
	check(saved.first().toObject().value("sourceUuid").toString() == uuid, "undo snapshot retains source identity");
	check(saved.first().toObject().value("sourceWidth").toInt() == 640 && saved.first().toObject().value("sourceHeight").toInt() == 480,
		"undo snapshot retains source dimensions");
	fixtureWidth = 1280;
	fixtureHeight = 960;
	PulseMotionEngineTestAccess::apply(engine, saved);
	obs_sceneitem_get_scale(item, &scale);
	obs_sceneitem_get_crop(item, &crop);
	check(std::abs(scale.x - 0.25f) < 0.0001f && std::abs(scale.y + 0.375f) < 0.0001f,
		"loading resized source preserves displayed size and flip");
	check(crop.left == 40 && crop.top == 20 && crop.right == 60 && crop.bottom == 30,
		"loading resized source adjusts crop in source pixels");
	const QJsonObject live = PulseMotionEngineTestAccess::savedTransform(saved.first().toObject(), source);
	check(std::abs(live.value("scaleX").toDouble() - scale.x) < 0.0001 && live.value("cropLeft").toInt() == crop.left,
		"editor and live playback use identical source-size adjustment");
	QJsonObject wrong = saved.first().toObject();
	wrong.insert("sourceUuid", "replaced-source");
	vec2 position{77, 88};
	obs_sceneitem_set_pos(item, &position);
	PulseMotionEngineTestAccess::apply(engine, QJsonArray{wrong});
	obs_sceneitem_get_pos(item, &position);
	check(position.x == 77 && position.y == 88, "mismatched source cannot receive saved transform");
	PulseMotionEngineTestAccess::active(engine, item);
	position = {177, 188};
	obs_sceneitem_set_pos(item, &position);
	engine.frontendEvent(OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGING);
	obs_sceneitem_get_pos(item, &position);
	check(position.x == 77 && position.y == 88, "collection switch restores active temporary movement before save");
	check(PulseMotionEngineTestAccess::cleared(engine), "collection switch clears timers, old references and request cache");
	PulseMotionEngineTestAccess::active(engine, item);
	position = {277, 288};
	obs_sceneitem_set_pos(item, &position);
	engine.frontendEvent(OBS_FRONTEND_EVENT_SCENE_COLLECTION_CLEANUP);
	obs_sceneitem_get_pos(item, &position);
	check(position.x == 277 && position.y == 288, "cleanup releases references without editing removed sources");
	check(PulseMotionEngineTestAccess::cleared(engine), "cleanup also cancels pending movement");
	obs_source_remove(obs_scene_get_source(scene));
	obs_source_remove(obs_sceneitem_get_source(group));
}

int main(int argc, char **argv)
{
	QApplication application(argc, argv);
	check(argc == 2, "pass an existing OBS runtime directory");
	base_set_log_handler(logMessage, nullptr);
	check(obs_startup("en-US", nullptr, nullptr), "isolated OBS startup");
	obs_audio_info audio{48000, SPEAKERS_STEREO};
	check(obs_reset_audio(&audio), "inert audio graph startup");
	const std::string runtime = argv[1];
	obs_add_data_path((runtime + "/data/libobs/").c_str());
	const std::string graphics = runtime + "/bin/64bit/libobs-d3d11.dll";
	obs_video_info video{};
	video.graphics_module = graphics.c_str();
	video.fps_num = 30; video.fps_den = 1;
	video.base_width = video.output_width = 640;
	video.base_height = video.output_height = 480;
	video.output_format = VIDEO_FORMAT_RGBA;
	check(obs_reset_video(&video) == OBS_VIDEO_SUCCESS, "isolated graphics startup");
	storageTests();
	sceneTests();
	obs_wait_for_destroy_queue();
	obs_shutdown();
	check(runtimeErrors == 0, "no unexpected OBS runtime errors");
	std::printf("PASS: %d motion preservation checks; no platform calls or installed settings used.\n", checks);
}
