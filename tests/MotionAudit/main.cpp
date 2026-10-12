#include "../../engine/obs-studio/plugins/pulse-weaver-core/pulse-motion-engine.hpp"
#include "../../engine/obs-studio/plugins/pulse-weaver-core/pulse-scene-item-ref.hpp"
#include "../../engine/obs-studio/plugins/pulse-weaver-core/pulse-show-templates.hpp"
#include "../../engine/obs-studio/plugins/pulse-weaver-core/pulse-setup-flexible.hpp"

#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QImage>
#include <QSize>
#include <QTemporaryDir>
#include <QTimer>
#include <util/base.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <tuple>

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

static void registerInertBrowser()
{
	obs_source_info type{}; type.id = "browser_source"; type.type = OBS_SOURCE_TYPE_INPUT; type.output_flags = OBS_SOURCE_VIDEO;
	type.get_name = [](void *) { return "Inert browser fixture"; };
	type.create = [](obs_data_t *settings, obs_source_t *) -> void * {
		return new QSize(int(obs_data_get_int(settings, "width")), int(obs_data_get_int(settings, "height")));
	};
	type.destroy = [](void *p) { delete static_cast<QSize *>(p); };
	type.get_width = [](void *p) { return uint32_t(static_cast<QSize *>(p)->width()); };
	type.get_height = [](void *p) { return uint32_t(static_cast<QSize *>(p)->height()); };
	obs_register_source(&type);
}

static void wizardOverlayPolicyTests()
{
	for (const QString &url : {QString("https://overlays.lumiastream.com/audit/countdown"),
		QString("http://127.0.0.1:39270/"), QString("http://127.0.0.1:39270/portrait")})
		check(pulseWizardOverlayUrlAllowed(url), "Lumia hosted overlays and local Chat Physics links remain supported");
	for (const QString &url : {QString("http://127.0.0.1:18754/overlay/audit-token"),
		QString("http://localhost:18754/preview/session/audit-session")})
		check(pulseIsNativeOverlayUrl(url) && !pulseWizardOverlayUrlAllowed(url),
			"Pulse Weaver native overlay and preview links cannot bypass the Lumia wizard policy");
	check(!pulseWizardOverlayUrlAllowed("file:///C:/audit/native-title.html"), "local native HTML files are not wizard overlay links");
	OBSDataAutoRelease settings = obs_data_create();
	obs_data_set_bool(settings, "is_local_file", true);
	obs_data_set_string(settings, "local_file", "C:/audit/plugin_config/pulse-weaver-core/show-graphics/title.html");
	OBSSourceAutoRelease local = obs_source_create("browser_source", "Local browser policy fixture", settings, nullptr);
	check(pulseIsNativeOverlaySource(local) && !pulseWizardOverlaySourceAllowed(local), "generated native title files are recognized and excluded");
	obs_data_set_string(settings, "local_file", "C:/audit/ordinary-browser-feed.html"); obs_source_update(local, settings);
	check(!pulseIsNativeOverlaySource(local) && !pulseWizardOverlaySourceAllowed(local),
		"ordinary local browser feeds are not misidentified as native titles but still are not Lumia artwork");
	obs_source_remove(local);
}

struct PulseMotionEngineTestAccess {
	static QString selectedStage(PulseMotionEngine &engine, const QString &stage) {
		QComboBox field;field.addItem("Unrelated old Stage","Unrelated old Stage");
		engine.stageField=&field;engine.selectEditorStage(stage);engine.populateStages();
		return field.currentData().toString();
	}
	static bool save(PulseMotionEngine &engine) { return engine.save(); }
	static bool valid(PulseMotionEngine &engine) { return engine.originalStoreValid; }
	static QJsonArray actions(PulseMotionEngine &engine) { return engine.actions; }
	static QJsonObject frame(obs_sceneitem_t *item) { return PulseMotionEngine::serialize(PulseMotionEngine::capture(item)); }
	static bool graphic(PulseMotionEngine &engine, const QJsonObject &action) {
		PulseMotionEngine::Execution execution; execution.action = action;
		QString error;
		check(engine.prepareExecution(execution, error) && !execution.tracks.empty(), "prepare policy fixture");
		return execution.tracks.front().graphic;
	}
	static void movement(PulseMotionEngine &engine, const QJsonObject &from, const QJsonObject &to)
	{
		const QString container = to.value("container").toString();
		OBSSceneAutoRelease scene = obs_scene_create_private("Inert movement preview");
		PulseMotionEngine::Execution execution;
		QHash<qint64, QJsonObject> liveBefore;
		for (const auto &value : to.value("items").toArray()) {
			const auto row = value.toObject();
			if (row.value("container").toString() != container) continue;
			const qint64 id = row.value("itemId").toString().toLongLong();
			OBSSceneItem live = engine.resolveItem(container, id, row.value("source").toString(), false,
				row.value("sourceUuid").toString());
			check(bool(live), "movement fixture source resolves");
			liveBefore.insert(id, frame(live));
			PulseMotionEngine::Track track;
			track.container = container;
			track.itemId = id;
			track.item = obs_scene_add(scene, obs_sceneitem_get_source(live));
			track.target = engine.deserialize(row.value("transform").toObject());
			bool found = false;
			for (const auto &startValue : from.value("items").toArray()) {
				const auto start = startValue.toObject();
				if (start.value("container").toString() == container && start.value("itemId") == row.value("itemId")) {
					track.from = engine.deserialize(start.value("transform").toObject());
					found = true;
					break;
				}
			}
			check(found, "movement endpoints share the same scene item");
			execution.tracks.push_back(track);
		}
		std::stable_sort(execution.tracks.begin(), execution.tracks.end(), [](const auto &a, const auto &b) {
			return a.from.order < b.from.order;
		});
		QComboBox kind, start;
		kind.addItem("Layout", "layout"); start.addItem("Starting Look", from.value("id"));
		engine.kindField = &kind; engine.previewFromLook = &start;
		engine.draftScene = scene; engine.draftContainer = container; engine.draftIds.clear();
		for (const auto &track : execution.tracks) engine.draftIds.insert(track.itemId, obs_sceneitem_get_id(track.item));
		const float width = float(obs_source_get_width(obs_scene_get_source(scene)));
		const float height = float(obs_source_get_height(obs_scene_get_source(scene)));
		for (bool editorPreview : {false, true}) {
			for (int percent : {0, 10, 25, 35, 50, 65, 75, 90, 100, 50, 20, 80}) {
				if (editorPreview) {
					for (const auto &track : execution.tracks) engine.apply(track.item, track.target, true);
					engine.previewDraft(percent);
				} else {
					for (const auto &track : execution.tracks) engine.apply(track.item, track.from, true);
					execution.orderCommitted = execution.graphicCommitted = false;
					engine.prepareCoveragePairs(execution);
					check(execution.coveragePairs.size() == 1 && execution.coveragePairs[0].kind ==
						PulseMotionEngine::Execution::CoveragePair::Kind::InsetColumn,
						"inset-to-column movement uses canvas coverage choreography");
					engine.applyMovementFrame(execution, percent / 100.0);
				}
				std::vector<PulseMotionEngine::Transform> content;
				int chatOrder = -1, highest = -1;
				for (const auto &track : execution.tracks) {
					obs_sceneitem_t *item = track.item;
					if (editorPreview && engine.previewScene)
						item = obs_scene_find_sceneitem_by_id(engine.previewScene,
							engine.previewIds.value(obs_sceneitem_get_id(track.item), -1));
					check(item != nullptr, "movement preview keeps every Stage item");
					const auto current = engine.capture(item);
					if (track.target.order < 2) content.push_back(current);
					if (current.visible) highest = std::max(highest, current.order);
					if (track.target.order == int(execution.tracks.size()) - 1) {
						chatOrder = current.order;
						check(current.visible && current.pos.x == 0 && current.pos.y == 0 &&
							current.bounds.x == width && current.bounds.y == height,
							"chat stays visible at full-canvas bounds throughout movement");
					}
					if (percent == 0 || percent == 100)
						check(frame(item) == engine.serialize(percent == 0 ? track.from : track.target),
							"movement reaches exact saved endpoint including stack and crop");
				}
				bool covered = content.size() == 2;
				for (int y = 0; y <= 12; ++y) for (int x = 0; x <= 16; ++x) {
					const float px = width * x / 16, py = height * y / 12;
					bool filled = false;
					for (const auto &t : content) filled |= t.visible && px >= t.pos.x - 1 && py >= t.pos.y - 1 &&
						px <= t.pos.x + t.bounds.x + 1 && py <= t.pos.y + t.bounds.y + 1;
					covered &= filled;
				}
				check(covered, "camera/content panels leave no canvas gap at intermediate frames or backwards scrubs");
				check(chatOrder == highest, "chat remains topmost throughout movement");
			}
		}
		engine.previewScene = nullptr; engine.pairedPreviews.clear(); engine.previewIds.clear();
		engine.draftScene = nullptr; engine.draftIds.clear(); engine.kindField = nullptr; engine.previewFromLook = nullptr;
		for (const auto &track : execution.tracks) {
			OBSSceneItem live = engine.resolveItem(container, track.itemId, {}, false);
			check(live && frame(live) == liveBefore.value(track.itemId), "offline preview never changes the live scene");
		}
	}
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

static void referenceBuilderTests(const obs_video_info &video)
{
	QTemporaryDir directory;
	check(QDir().mkpath(directory.filePath("plugin_config/pulse-weaver-core")), "reference builder storage directory");
	const QString motionPath = directory.filePath("plugin_config/pulse-weaver-core/motion.json");
	const QString stagePath = directory.filePath("pulseweaver-stages.json");
	PulseMotionEngine engine(nullptr, motionPath);
	check(PulseMotionEngineTestAccess::selectedStage(engine,"Newly generated Stage")=="Newly generated Stage",
		"refresh preserves a newly generated Stage before the main selector reloads");
	obs_video_info portraitVideo = video;
	portraitVideo.base_width = portraitVideo.output_width = 480;
	portraitVideo.base_height = portraitVideo.output_height = 640;
	OBSCanvasAutoRelease canvas = obs_canvas_create("Pulse Weaver Vertical", &portraitVideo, PROGRAM);
	check(bool(canvas), "reference portrait canvas");
	OBSSceneAutoRelease horizontal = obs_scene_create("Reference Starting");
	OBSSceneAutoRelease portrait = obs_canvas_scene_create(canvas, "Reference Portrait");
	OBSSourceAutoRelease source = obs_source_create_private("pulse_motion_audit_fixture", "Reference camera", nullptr);
	obs_sceneitem_t *hItem = obs_scene_add(horizontal, source);
	obs_sceneitem_t *pItem = obs_scene_add(portrait, source);
	obs_sceneitem_set_id(hItem, 40);
	obs_sceneitem_set_id(pItem, 60);
	vec2 position{17, 29}; obs_sceneitem_set_pos(hItem, &position);
	obs_sceneitem_crop crop{7, 11, 13, 19}; obs_sceneitem_set_crop(hItem, &crop);
	position = {83, 97}; obs_sceneitem_set_pos(pItem, &position);
	obs_sceneitem_set_rot(pItem, 12);
	obs_sceneitem_set_locked(pItem, true);
	const QJsonObject hFrame = PulseMotionEngineTestAccess::frame(hItem);
	const QJsonObject pFrame = PulseMotionEngineTestAccess::frame(pItem);
	QJsonObject look{{"version", 1}, {"id", "reference-look"}, {"name", "Reference Starting · Cropped camera"},
		{"kind", "layout"}, {"container", "Reference Starting"}, {"durationMs", 850},
		{"items", QJsonArray{
			QJsonObject{{"container", "Reference Starting"}, {"itemId", "40"}, {"source", "Reference camera"}, {"transform", hFrame}},
			QJsonObject{{"container", "Reference Portrait"}, {"itemId", "60"}, {"source", "Reference camera"}, {"transform", pFrame}}}}};
	PulseMotionEngineTestAccess::setActions(engine, QJsonArray{look});
	QJsonObject reference{{"name", "Reference Starting"}, {"horizontal", "Reference Starting"}, {"vertical", "Reference Portrait"},
		{"horizontalTransition", "stinger:Fixture"}, {"verticalTransition", "stinger:Fixture"},
		{"assignments", QJsonObject{{"youtube_vertical", QJsonObject{{"canvas", "vertical"},
			{"scene", "Reference Portrait"}, {"excluded", QJsonArray{"Music"}}}}}}};
	const QByteArray initialStages = QJsonDocument(QJsonArray{reference}).toJson();
	writeFile(stagePath, initialStages);
	const QJsonObject choices{{"name", "Builder Trial"}, {"stages", QJsonArray{"Starting"}},
		{"references", QJsonObject{{"Starting", "Reference Starting"}}}};
	const auto result = engine.createGuidedShow(choices);
	check(result.value("ok").toBool() && result.value("looks").toInt() == 1, "reference builder creates paired editable look");
	OBSSourceAutoRelease generated = obs_get_source_by_name("Builder Trial · Starting");
	OBSSourceAutoRelease generatedPortrait = obs_canvas_get_source_by_name(canvas, "Builder Trial · Starting · Portrait");
	check(generated && generatedPortrait, "reference builder creates distinct landscape and portrait scenes");
	obs_sceneitem_t *hCopy = obs_scene_find_source(obs_scene_from_source(generated), "Reference camera");
	obs_sceneitem_t *pCopy = obs_scene_find_source(obs_scene_from_source(generatedPortrait), "Reference camera");
	check(PulseMotionEngineTestAccess::frame(hCopy) == hFrame, "landscape crop and position retained");
	check(PulseMotionEngineTestAccess::frame(pCopy) == pFrame, "portrait rotation lock and position retained independently");
	check(PulseMotionEngineTestAccess::frame(hItem) == hFrame && PulseMotionEngineTestAccess::frame(pItem) == pFrame,
		"creating a show does not alter reference geometry");
	const QJsonArray stored = QJsonDocument::fromJson(readFile(stagePath)).array();
	const QJsonObject createdStage = stored.last().toObject();
	check(createdStage.value("horizontalTransition") == reference.value("horizontalTransition"), "reference stinger retained");
	const QJsonObject assignment = createdStage.value("assignments").toObject().value("youtube_vertical").toObject();
	check(assignment.value("scene").toString() == "Builder Trial · Starting · Portrait" &&
		assignment.value("excluded").toArray() == QJsonArray{"Music"}, "routing targets new portrait scene while retaining exclusions");
	const QJsonObject clonedLook = PulseMotionEngineTestAccess::actions(engine).last().toObject();
	check(clonedLook.value("id") != look.value("id") && clonedLook.value("durationMs").toInt() == 850,
		"new look identity retains movement duration");
	check(clonedLook.value("items").toArray().first().toObject().value("itemId").toString() != "40",
		"saved look remaps reference scene-item identity");
	position = {300, 320}; obs_sceneitem_set_pos(hCopy, &position);
	check(PulseMotionEngineTestAccess::frame(hItem) == hFrame, "new layout editing cannot reposition reference item");
	const QByteArray beforeFailure = readFile(stagePath);
	const QJsonArray beforeActions = PulseMotionEngineTestAccess::actions(engine);
	check(!engine.createGuidedShow(choices).value("ok").toBool(), "existing destination names rejected");
	check(readFile(stagePath) == beforeFailure && PulseMotionEngineTestAccess::actions(engine) == beforeActions,
		"name collision preserves catalogue and looks");
	QJsonObject broken = look;
	QJsonArray brokenTargets = broken.value("items").toArray();
	QJsonObject bad = brokenTargets.first().toObject(); bad.insert("sourceUuid", "wrong-source-identity");
	brokenTargets.replace(0, bad); broken.insert("items", brokenTargets);
	PulseMotionEngineTestAccess::setActions(engine, QJsonArray{broken});
	QJsonObject invalidChoices = choices; invalidChoices.insert("name", "Broken Trial");
	check(!engine.createGuidedShow(invalidChoices).value("ok").toBool(), "stale reference source identity rejected");
	OBSSourceAutoRelease rolledBack = obs_get_source_by_name("Broken Trial · Starting");
	OBSSourceAutoRelease rolledBackPortrait = obs_canvas_get_source_by_name(canvas, "Broken Trial · Starting · Portrait");
	check(!rolledBack && !rolledBackPortrait && readFile(stagePath) == beforeFailure,
		"failed reference import rolls back both canvases without touching catalogue");
	QJsonObject freshChoices{{"name", "Collision"}, {"stages", QJsonArray{"Gameplay"}}, {"style", "corner"},
		{"roles", QJsonObject{{"game", "new:game"}}},
		{"newSources", QJsonObject{{"game", QJsonObject{{"role", "game"}, {"name", "Collision · Gameplay"}, {"kind", "game_capture"}}}}}};
	check(!engine.createGuidedShow(freshChoices).value("ok").toBool(), "planned source cannot take a generated Stage name");
	obs_source_remove(generated); obs_source_remove(generatedPortrait);
	obs_source_remove(obs_scene_get_source(horizontal)); obs_source_remove(obs_scene_get_source(portrait));
	obs_canvas_remove(canvas);
}

static void setupBuilderTests(const obs_video_info &video)
{
	QTemporaryDir directory;
	check(QDir().mkpath(directory.filePath("plugin_config/pulse-weaver-core")), "setup storage directory");
	const QString motionPath = directory.filePath("plugin_config/pulse-weaver-core/motion.json");
	const QString stagePath = directory.filePath("pulseweaver-stages.json");
	PulseMotionEngine engine(nullptr, motionPath);
	obs_video_info verticalVideo = video; verticalVideo.base_width = verticalVideo.output_width = 480;
	verticalVideo.base_height = verticalVideo.output_height = 640;
	OBSCanvasAutoRelease canvas = obs_canvas_create("Pulse Weaver Vertical", &verticalVideo, PROGRAM);
	OBSSceneAutoRelease horizontal = obs_scene_create("Setup Reference");
	OBSSceneAutoRelease portrait = obs_canvas_scene_create(canvas, "Setup Reference Portrait");
	OBSSceneAutoRelease wrapper = obs_scene_create("Setup Camera Unit");
	OBSSceneAutoRelease croppedCamera = obs_scene_create("Setup Camera Crop Unit");
	for (const auto &[scene, width, height] : std::vector<std::tuple<obs_scene_t *, int, int>>{
		{wrapper, 431, 277}, {croppedCamera, 391, 257}}) {
		OBSDataAutoRelease size = obs_data_create();
		obs_data_set_bool(size, "custom_size", true);
		obs_data_set_int(size, "cx", width); obs_data_set_int(size, "cy", height);
		obs_source_update(obs_scene_get_source(scene), size);
		obs_source_load(obs_scene_get_source(scene));
	}
	check(obs_source_get_width(obs_scene_get_source(wrapper)) == 431 && obs_source_get_height(obs_scene_get_source(wrapper)) == 277 &&
		obs_source_get_width(obs_scene_get_source(croppedCamera)) == 391 && obs_source_get_height(obs_scene_get_source(croppedCamera)) == 257,
		"reference fixture starts with deliberate nested custom coordinate spaces");
	OBSSourceAutoRelease camera = obs_source_create("pulse_motion_audit_fixture", "Setup camera", nullptr, nullptr);
	OBSSourceAutoRelease replacement = obs_source_create("pulse_motion_audit_fixture", "Replacement camera", nullptr, nullptr);
	OBSSourceAutoRelease border = obs_source_create("pulse_motion_audit_fixture", "Setup border", nullptr, nullptr);
	obs_sceneitem_t *child = obs_scene_add(croppedCamera, camera);
	obs_sceneitem_t *croppedItem = obs_scene_add(wrapper, obs_scene_get_source(croppedCamera));
	obs_sceneitem_t *frame = obs_scene_add(wrapper, border);
	obs_sceneitem_crop crop{7, 11, 13, 19}; obs_sceneitem_set_crop(child, &crop);
	vec2 scale{-0.7f, 0.85f}; obs_sceneitem_set_scale(child, &scale);
	obs_sceneitem_set_rot(croppedItem, 8); obs_sceneitem_set_locked(croppedItem, true);
	obs_sceneitem_set_scale_filter(croppedItem, OBS_SCALE_LANCZOS);
	obs_sceneitem_set_blending_mode(frame, OBS_BLEND_SCREEN);
	const QJsonObject childBefore = PulseMotionEngineTestAccess::frame(child);
	const QJsonObject cropUnitBefore = PulseMotionEngineTestAccess::frame(croppedItem);
	const QJsonObject borderBefore = PulseMotionEngineTestAccess::frame(frame);
	OBSCanvasAutoRelease mainCanvas = obs_get_main_canvas();
	QSet<QString> clonedUnits;
	auto checkUnit = [&](obs_source_t *unit, obs_canvas_t *destination) {
		check(unit && unit != obs_scene_get_source(wrapper), "camera unit isolated from reference");
		OBSCanvasAutoRelease unitCanvas = obs_source_get_canvas(unit);
		check(unitCanvas == destination, "generated camera unit belongs to its destination canvas");
		if (obs_source_get_width(unit) != 431 || obs_source_get_height(unit) != 277)
			std::fprintf(stderr, "Camera unit dimensions: %s %u x %u; reference %u x %u\n", obs_source_get_name(unit),
				obs_source_get_width(unit), obs_source_get_height(unit), obs_source_get_width(obs_scene_get_source(wrapper)),
				obs_source_get_height(obs_scene_get_source(wrapper)));
		check(obs_source_get_width(unit) == 431 && obs_source_get_height(unit) == 277,
			"canvas assignment preserves the camera unit's intended local dimensions");
		obs_sceneitem_t *nested = nullptr;
		obs_scene_enum_items(obs_scene_from_source(unit), [](obs_scene_t *, obs_sceneitem_t *item, void *opaque) {
			if (obs_scene_from_source(obs_sceneitem_get_source(item))) *static_cast<obs_sceneitem_t **>(opaque) = item;
			return true;
		}, &nested);
		check(nested && PulseMotionEngineTestAccess::frame(nested) == cropUnitBefore,
			"recursive camera clone preserves nested framing rotation and lock");
		obs_source_t *nestedUnit = obs_sceneitem_get_source(nested);
		OBSCanvasAutoRelease nestedCanvas = obs_source_get_canvas(nestedUnit);
		check(nestedCanvas == destination && nestedUnit != obs_scene_get_source(croppedCamera),
			"nested camera unit is isolated on the same destination canvas");
		check(obs_source_get_width(nestedUnit) == 391 && obs_source_get_height(nestedUnit) == 257,
			"recursive clone preserves nested custom dimensions");
		check(obs_sceneitem_get_scale_filter(nested) == OBS_SCALE_LANCZOS,
			"recursive clone preserves source-item sampling settings");
		obs_sceneitem_t *bound = obs_scene_find_source(obs_scene_from_source(nestedUnit), "Replacement camera");
		check(bound && PulseMotionEngineTestAccess::frame(bound) == childBefore && obs_sceneitem_get_source(bound) == replacement,
			"source replacement reuses raw capture and retains nested crop and flip");
		obs_sceneitem_t *decoration = obs_scene_find_source(obs_scene_from_source(unit), "Setup border");
		check(decoration && PulseMotionEngineTestAccess::frame(decoration) == borderBefore &&
			obs_sceneitem_get_source(decoration) == border && obs_sceneitem_get_blending_mode(decoration) == OBS_BLEND_SCREEN,
			"decoration remains attached with its original frame and blending settings");
		OBSDataAutoRelease savedUnit = obs_save_source(unit), savedChild = obs_save_source(nestedUnit);
		check(QString::fromUtf8(obs_data_get_string(savedUnit, "canvas_uuid")) == obs_canvas_get_uuid(destination) &&
			QString::fromUtf8(obs_data_get_string(savedChild, "canvas_uuid")) == obs_canvas_get_uuid(destination),
			"native serialization records correct canvas ownership at each camera nesting level");
		clonedUnits.insert(obs_source_get_uuid(unit)); clonedUnits.insert(obs_source_get_uuid(nestedUnit));
	};
	obs_sceneitem_t *h = obs_scene_add(horizontal, obs_scene_get_source(wrapper));
	obs_sceneitem_t *v = obs_scene_add(portrait, obs_scene_get_source(wrapper));
	vec2 position{83, 97}; obs_sceneitem_set_pos(v, &position);
	const QJsonObject hBefore = PulseMotionEngineTestAccess::frame(h), vBefore = PulseMotionEngineTestAccess::frame(v);
	QJsonObject look{{"id", "setup-first"}, {"name", "Setup Reference · Countdown"}, {"kind", "layout"},
		{"container", "Setup Reference"}, {"durationMs", 1100},
		{"items", QJsonArray{QJsonObject{{"container", "Setup Reference"}, {"itemId", QString::number(obs_sceneitem_get_id(h))},
			{"sourceUuid", obs_source_get_uuid(obs_scene_get_source(wrapper))}, {"transform", hBefore}},
			QJsonObject{{"container", "Setup Reference Portrait"}, {"itemId", QString::number(obs_sceneitem_get_id(v))},
			{"sourceUuid", obs_source_get_uuid(obs_scene_get_source(wrapper))}, {"transform", vBefore}}}}};
	QJsonObject alternate = look; alternate.insert("id", "setup-second"); alternate.insert("name", "Setup Reference · Swap focus");
	PulseMotionEngineTestAccess::setActions(engine, {look, alternate});
	writeFile(stagePath, QJsonDocument(QJsonArray{QJsonObject{{"name", "Setup Reference"}, {"horizontal", "Setup Reference"},
		{"vertical", "Setup Reference Portrait"}, {"horizontalTransition", "cut"}}}).toJson());
	QJsonObject choices{{"schema", 3}, {"name", "Setup Trial"}, {"stages", QJsonArray{"Starting"}},
		{"references", QJsonObject{{"Starting", "Setup Reference"}}},
		{"selectedLooks", QJsonObject{{"Starting", QJsonArray{"setup-first"}}}},
		{"sourceBindings", QJsonObject{{obs_source_get_uuid(camera), obs_source_get_uuid(replacement)}}}};
	const auto result = engine.createGuidedShow(choices);
	check(result.value("ok").toBool() && result.value("looks").toInt() == 1, "setup produces only selected operating Look");
	const QJsonObject generatedLook = PulseMotionEngineTestAccess::actions(engine).last().toObject();
	check(generatedLook.value("referenceLookId") == "setup-first" && generatedLook.value("durationMs") == 1100,
		"setup retains reference identity provenance and duration");
	const QJsonArray targets = generatedLook.value("items").toArray();
	check(targets.size() == 2 && targets[0].toObject().value("sourceUuid") != targets[1].toObject().value("sourceUuid"),
		"paired targets use distinct generated camera units on their own canvases");
	OBSSourceAutoRelease unit = obs_get_source_by_uuid(targets[0].toObject().value("sourceUuid").toString().toUtf8().constData());
	OBSSourceAutoRelease portraitUnit = obs_get_source_by_uuid(targets[1].toObject().value("sourceUuid").toString().toUtf8().constData());
	checkUnit(unit, mainCanvas); checkUnit(portraitUnit, canvas);
	for (obs_source_t *saved : {unit.Get(), portraitUnit.Get()}) {
		OBSDataAutoRelease serializedUnit = obs_save_source(saved);
		const bool vertical = saved == portraitUnit.Get();
		obs_data_set_string(serializedUnit, "name", vertical ? "Reloaded Portrait Unit" : "Reloaded Landscape Unit");
		obs_data_set_string(serializedUnit, "uuid", vertical ? "reloaded-portrait-unit" : "reloaded-landscape-unit");
		OBSSourceAutoRelease reloaded = obs_load_source(serializedUnit);
		obs_source_load2(reloaded);
		checkUnit(reloaded, vertical ? canvas.Get() : mainCanvas.Get());
		obs_source_remove(reloaded);
	}
	check(PulseMotionEngineTestAccess::frame(child) == childBefore && obs_sceneitem_get_source(child) == camera,
		"source assignment leaves reference capture and crop intact");
	check(targets[0].toObject().value("transform").toObject() == hBefore && targets[1].toObject().value("transform").toObject() == vBefore,
		"setup retains independent landscape and portrait saved targets");
	const QByteArray beforeFailure = readFile(stagePath);
	QJsonObject invalid = choices; invalid.insert("name", "Invalid Setup"); invalid.insert("selectedLooks", QJsonObject{{"Starting", QJsonArray{"missing"}}});
	check(!engine.createGuidedShow(invalid).value("ok").toBool(), "unknown selected Look rejected");
	invalid.insert("selectedLooks", choices.value("selectedLooks")); invalid.insert("sourceBindings", QJsonObject{{obs_source_get_uuid(camera), "missing"}});
	check(!engine.createGuidedShow(invalid).value("ok").toBool() && readFile(stagePath) == beforeFailure,
		"missing source rejected before catalogue mutation");
	QJsonObject second = choices; second.insert("name", "Two Looks"); second.insert("selectedLooks", QJsonObject{{"Starting", QJsonArray{"setup-first", "setup-second"}}});
	check(engine.createGuidedShow(second).value("ok").toBool(), "explicit operating focus swap creates alternate Look");
	const QJsonArray all = PulseMotionEngineTestAccess::actions(engine);
	OBSSourceAutoRelease secondUnit = obs_get_source_by_uuid(all.last().toObject().value("items").toArray().first().toObject().value("sourceUuid").toString().toUtf8().constData());
	check(all[all.size()-1].toObject().value("items") == all[all.size()-2].toObject().value("items"),
		"selected Looks share item identities for native Move");
	const auto secondTargets = all.last().toObject().value("items").toArray();
	OBSSourceAutoRelease secondPortraitUnit = obs_get_source_by_uuid(secondTargets[1].toObject().value("sourceUuid").toString().toUtf8().constData());
	check(secondUnit != secondPortraitUnit, "multiple saved Looks keep horizontal and portrait units separate");
	checkUnit(secondUnit, mainCanvas); checkUnit(secondPortraitUnit, canvas);
	QJsonObject savedStage{{"theme", "setup-starting"}, {"referenceStage", "Setup Reference"},
		{"referenceLooks", QJsonArray{"setup-first", "setup-second"}}, {"sourceBindings", choices.value("sourceBindings")}};
	QJsonObject mixed{{"schema", 2}, {"name", "Mixed Trial"}, {"stages", QJsonArray{
		QJsonObject{{"theme", "setup-chat"}, {"looks", QJsonArray{"just-chatting"}},
			{"roles", QJsonObject{{"feedA", obs_source_get_uuid(camera)}}}}, savedStage}}};
	// Resolve the catalogue ID rather than coupling this test to a display label.
	auto mixedStages = mixed.value("stages").toArray(); auto chatStage = mixedStages[0].toObject();
	chatStage.insert("looks", QJsonArray{PulseShow::theme("setup-chat")->looks.front().id}); mixedStages[0] = chatStage;
	mixed.insert("stages", mixedStages);
	const auto mixedResult = engine.createGuidedShow(mixed);
	check(mixedResult.value("ok").toBool() && mixedResult.value("looks").toInt() == 3, "single wizard transaction mixes flexible and saved stage layouts");
	const auto mixedActions = PulseMotionEngineTestAccess::actions(engine);
	const auto mixedLook = mixedActions.last().toObject();
	check(mixedLook.value("referenceLookId") == "setup-second" && mixedLook.value("durationMs") == 1100,
		"saved stage in broader wizard retains selected Look and movement duration");
	const auto mixedTargets = mixedLook.value("items").toArray();
	check(mixedTargets.size() == 2 && mixedTargets[0].toObject().value("transform").toObject() == hBefore &&
		mixedTargets[1].toObject().value("transform").toObject() == vBefore, "mixed wizard preserves independent saved H/V geometry");
	OBSSourceAutoRelease mixedUnit = obs_get_source_by_uuid(mixedTargets.first().toObject().value("sourceUuid").toString().toUtf8().constData());
	OBSSourceAutoRelease mixedPortraitUnit = obs_get_source_by_uuid(mixedTargets[1].toObject().value("sourceUuid").toString().toUtf8().constData());
	check(mixedUnit != mixedPortraitUnit, "mixed wizard keeps saved camera units separate for both canvases");
	checkUnit(mixedUnit, mainCanvas); checkUnit(mixedPortraitUnit, canvas);
	check(mixedActions[mixedActions.size()-2].toObject().value("items") == mixedTargets,
		"mixed wizard reuses each canvas unit across the selected saved Looks");
	auto generatedStages = QJsonDocument::fromJson(readFile(stagePath)).array();
	check(generatedStages.last().toObject().value("horizontalTransition") == "cut",
		"wizard keeps saved stage transition when no replacement is chosen");
	QJsonObject transitionTrial = mixed; transitionTrial.insert("name", "Transition Trial");
	QJsonArray transitionStages = mixed.value("stages").toArray();
	QJsonObject customTransitionStage = transitionStages[0].toObject();
	customTransitionStage.insert("transitions", QJsonObject{{"horizontalTransition", "cut"}, {"verticalTransition", "fade"}, {"verticalDurationMs", 730}});
	transitionStages[0] = customTransitionStage;
	QJsonObject savedTransitionStage = transitionStages[1].toObject();
	savedTransitionStage.insert("transitions", QJsonObject{{"horizontalTransition", "fade"}, {"horizontalDurationMs", 910}, {"verticalTransition", "cut"}});
	transitionStages[1] = savedTransitionStage; transitionTrial.insert("stages", transitionStages);
	check(engine.createGuidedShow(transitionTrial).value("ok").toBool(), "wizard accepts independent transitions for flexible and saved stages");
	generatedStages = QJsonDocument::fromJson(readFile(stagePath)).array();
	const auto customPolicy = generatedStages[generatedStages.size()-2].toObject(), savedPolicy = generatedStages.last().toObject();
	check(customPolicy.value("horizontalTransition") == "cut" && customPolicy.value("horizontalDurationMs") == 0 &&
		customPolicy.value("verticalTransition") == "fade" && customPolicy.value("verticalDurationMs") == 730,
		"flexible stage persists independent format choices and fade timing");
	check(savedPolicy.value("horizontalTransition") == "fade" && savedPolicy.value("horizontalDurationMs") == 910 &&
		savedPolicy.value("verticalTransition") == "cut" && savedPolicy.value("verticalDurationMs") == 0,
		"saved stage accepts explicit format overrides without changing Look movement");
	const auto beforeBadTransition = readFile(stagePath), beforeBadActions = QJsonDocument(PulseMotionEngineTestAccess::actions(engine)).toJson();
	savedTransitionStage.insert("transitions", QJsonObject{{"horizontalTransition", "stinger:Missing"}});
	transitionStages[1] = savedTransitionStage; transitionTrial.insert("name", "Missing Stinger Trial"); transitionTrial.insert("stages", transitionStages);
	check(!engine.createGuidedShow(transitionTrial).value("ok").toBool() && readFile(stagePath) == beforeBadTransition &&
		QJsonDocument(PulseMotionEngineTestAccess::actions(engine)).toJson() == beforeBadActions,
		"unavailable stinger rejected without creating stages or changing catalogues");
	OBSSourceAutoRelease missingTransitionScene = obs_get_source_by_name("Missing Stinger Trial · Chat");
	check(!missingTransitionScene, "invalid stinger leaves no partially created flexible stage");
	for (const QString &stageName : {QString("Transition Trial · Chat"), QString("Transition Trial · Starting")}) {
		OBSSourceAutoRelease hStage = obs_get_source_by_name(stageName.toUtf8().constData());
		OBSSourceAutoRelease vStage = obs_canvas_get_source_by_name(canvas, (stageName + " · Portrait").toUtf8().constData());
		if (hStage) obs_source_remove(hStage); if (vStage) obs_source_remove(vStage);
	}
	const auto transitionAction = PulseMotionEngineTestAccess::actions(engine).last().toObject();
	OBSSourceAutoRelease transitionUnit = obs_get_source_by_uuid(transitionAction.value("items").toArray().first().toObject().value("sourceUuid").toString().toUtf8().constData());
	OBSSourceAutoRelease transitionPortraitUnit = obs_get_source_by_uuid(transitionAction.value("items").toArray()[1].toObject().value("sourceUuid").toString().toUtf8().constData());
	checkUnit(transitionUnit, mainCanvas); checkUnit(transitionPortraitUnit, canvas);
	savedTransitionStage.insert("transitions", QJsonObject{{"verticalTransition", "fade"}, {"verticalDurationMs", -1}});
	transitionStages[1] = savedTransitionStage; transitionTrial.insert("stages", transitionStages);
	check(!engine.createGuidedShow(transitionTrial).value("ok").toBool() && readFile(stagePath) == beforeBadTransition,
		"invalid transition duration rejected without catalogue changes");
	const auto stagesBeforeInvalid = readFile(stagePath); const auto actionsBeforeInvalid = PulseMotionEngineTestAccess::actions(engine);
	savedStage.insert("referenceLooks", QJsonArray{"missing"}); mixedStages[1] = savedStage;
	mixed.insert("name", "Mixed Invalid"); mixed.insert("stages", mixedStages);
	check(!engine.createGuidedShow(mixed).value("ok").toBool() && readFile(stagePath) == stagesBeforeInvalid &&
		PulseMotionEngineTestAccess::actions(engine) == actionsBeforeInvalid, "invalid saved Look rolls back earlier flexible stage and both catalogues");
	OBSSourceAutoRelease rejectedScene = obs_get_source_by_name("Mixed Invalid · Chat");
	check(!rejectedScene, "failed mixed show leaves no earlier generated stage");
	QJsonObject layeredStage = savedStage; layeredStage.insert("referenceLooks", QJsonArray{"setup-first", "setup-second"});
	layeredStage.insert("disabledSources", QJsonArray{obs_source_get_uuid(border)});
	OBSSourceAutoRelease sharedOne = obs_source_create("pulse_motion_audit_fixture", "Shared first alert", nullptr, nullptr);
	OBSSourceAutoRelease sharedTwo = obs_source_create("pulse_motion_audit_fixture", "Shared second alert", nullptr, nullptr);
	OBSSourceAutoRelease sharedThree = obs_source_create("pulse_motion_audit_fixture", "Shared third alert", nullptr, nullptr);
	QJsonArray sharedLayers;
	for (const QString &route : {QString("horizontal"), QString("vertical")}) for (int slot = 1; slot <= 3; ++slot)
		sharedLayers.append(QJsonObject{{"canvas", route}, {"order", slot},
			{"source", obs_source_get_uuid(slot == 1 ? sharedOne.Get() : slot == 2 ? sharedTwo.Get() : sharedThree.Get())},
			{"looks", slot == 2 ? QJsonArray{"setup-starting/saved:setup-second"} :
				QJsonArray{"setup-starting/saved:setup-first", "setup-starting/saved:setup-second"}}});
	QJsonObject layered{{"schema", 2}, {"name", "Saved Shared Layers"}, {"stages", QJsonArray{layeredStage}}, {"overlays", sharedLayers}};
	QJsonObject duplicate = layered; duplicate.insert("name", "Duplicate Saved Alert");
	duplicate.insert("overlays", QJsonArray{QJsonObject{{"canvas", "horizontal"}, {"order", 1}, {"source", obs_source_get_uuid(replacement)},
		{"looks", QJsonArray{"setup-starting/saved:setup-second"}}}});
	const auto beforeOverlap = readFile(stagePath), beforeOverlapActions = QJsonDocument(PulseMotionEngineTestAccess::actions(engine)).toJson();
	const auto duplicateResult = engine.createGuidedShow(duplicate);
	check(!duplicateResult.value("ok").toBool() && duplicateResult.value("message").toString().contains("already exists in saved layout") &&
		readFile(stagePath) == beforeOverlap && QJsonDocument(PulseMotionEngineTestAccess::actions(engine)).toJson() == beforeOverlapActions,
		"shared overlay matching a nested replacement input is rejected before either catalogue changes");
	OBSSourceAutoRelease noDuplicateScene = obs_get_source_by_name("Duplicate Saved Alert · Starting");
	check(!noDuplicateScene, "duplicate saved alert validation creates no partial stage");
	check(!pulseSceneUsesSource(obs_scene_get_source(horizontal), obs_source_get_uuid(camera), layeredStage.value("sourceBindings").toObject()) &&
		!pulseSceneUsesSource(obs_scene_get_source(horizontal), obs_source_get_uuid(border), {}, layeredStage.value("disabledSources").toArray()),
		"replaced inputs and explicitly skipped nested sources do not produce false overlap errors");
	check(!pulseSceneUsesSource(obs_scene_get_source(horizontal), "new:unique-draft") &&
		pulseSceneUsesSource(obs_scene_get_source(horizontal), "new:replacement", {{obs_source_get_uuid(camera), "new:replacement"}}),
		"a new source conflicts only when that same definition also replaces a saved input");
	{
		OBSDataAutoRelease browserSettings = obs_data_create();
		obs_data_set_string(browserSettings, "url", "https://overlays.lumiastream.com/audit/saved-layer");
		obs_data_set_int(browserSettings, "width", 640); obs_data_set_int(browserSettings, "height", 480);
		OBSSourceAutoRelease savedBrowser = obs_source_create("browser_source", "Saved Lumia alert fixture", browserSettings, nullptr);
		obs_sceneitem_t *savedBrowserItem = obs_scene_add(croppedCamera, savedBrowser);
		obs_data_set_bool(browserSettings, "is_local_file", true);
		obs_data_set_string(browserSettings, "local_file", "C:/audit/plugin_config/pulse-weaver-core/show-graphics/saved-title.html");
		obs_source_update(savedBrowser, browserSettings);
		check(pulseSceneUsesNativeOverlay(obs_scene_get_source(horizontal)) &&
			pulseSceneUsesNativeOverlay(obs_scene_get_source(horizontal), {}, {obs_source_get_uuid(savedBrowser)}) &&
			!pulseSceneUsesNativeOverlay(obs_scene_get_source(horizontal), {{obs_source_get_uuid(savedBrowser), "new:lumia-replacement"}}),
			"saved-stage native overlays require replacement even when hidden inside a nested unit");
		const auto beforeNativeSaved = readFile(stagePath), beforeNativeSavedActions = QJsonDocument(PulseMotionEngineTestAccess::actions(engine)).toJson();
		QJsonObject nativeSaved{{"schema", 2}, {"overlayProvider", "lumia"}, {"name", "Native Saved Wizard"}, {"stages", QJsonArray{layeredStage}}};
		const auto nativeSavedResult = engine.createGuidedShow(nativeSaved);
		check(!nativeSavedResult.value("ok").toBool() && nativeSavedResult.value("message").toString().contains("Lumia", Qt::CaseInsensitive) &&
			readFile(stagePath) == beforeNativeSaved && QJsonDocument(PulseMotionEngineTestAccess::actions(engine)).toJson() == beforeNativeSavedActions,
			"saved layouts cannot bring nested native overlays into a Lumia wizard show");
		auto hiddenNativeStage = layeredStage;
		hiddenNativeStage.insert("disabledSources", QJsonArray{obs_source_get_uuid(border), obs_source_get_uuid(savedBrowser)});
		nativeSaved.insert("stages", QJsonArray{hiddenNativeStage});
		const auto hiddenNativeResult = engine.createGuidedShow(nativeSaved);
		check(!hiddenNativeResult.value("ok").toBool() && hiddenNativeResult.value("message").toString().contains("Lumia", Qt::CaseInsensitive) &&
			readFile(stagePath) == beforeNativeSaved && QJsonDocument(PulseMotionEngineTestAccess::actions(engine)).toJson() == beforeNativeSavedActions,
			"disabling a saved native overlay cannot bypass replacement because copied hidden items retain their sources");
		OBSDataAutoRelease replacementBrowserSettings = obs_data_create();
		obs_data_set_string(replacementBrowserSettings, "url", "https://overlays.lumiastream.com/audit/saved-replacement");
		obs_data_set_int(replacementBrowserSettings, "width", 480); obs_data_set_int(replacementBrowserSettings, "height", 640);
		OBSSourceAutoRelease wrongBrowser = obs_source_create("browser_source", "Portrait saved replacement fixture", replacementBrowserSettings, nullptr);
		OBSDataAutoRelease correctBrowserSettings = obs_data_create(); obs_data_apply(correctBrowserSettings, replacementBrowserSettings);
		obs_data_set_int(correctBrowserSettings, "width", 640); obs_data_set_int(correctBrowserSettings, "height", 480);
		OBSSourceAutoRelease correctBrowser = obs_source_create("browser_source", "Landscape saved replacement fixture", correctBrowserSettings, nullptr);
		check(!pulseBrowserMatchesCanvas(wrongBrowser, false) && pulseBrowserMatchesCanvas(correctBrowser, false),
			"saved replacement fixtures retain independent portrait and landscape viewports");
		auto replacementStage = layeredStage; auto bindings = replacementStage.value("sourceBindings").toObject();
		bindings.insert(obs_source_get_uuid(savedBrowser), obs_source_get_uuid(wrongBrowser)); replacementStage.insert("sourceBindings", bindings);
		nativeSaved.insert("stages", QJsonArray{replacementStage});
		const auto wrongReplacementResult = engine.createGuidedShow(nativeSaved);
		check(!wrongReplacementResult.value("ok").toBool() && wrongReplacementResult.value("message").toString().contains("Lumia", Qt::CaseInsensitive) &&
			readFile(stagePath) == beforeNativeSaved && QJsonDocument(PulseMotionEngineTestAccess::actions(engine)).toJson() == beforeNativeSavedActions,
			"saved native artwork cannot be replaced by a browser for the opposite canvas orientation");
		bindings.insert(obs_source_get_uuid(savedBrowser), obs_source_get_uuid(correctBrowser)); replacementStage.insert("sourceBindings", bindings);
		nativeSaved.insert("name", "Replaced Native Saved Wizard"); nativeSaved.insert("stages", QJsonArray{replacementStage});
		check(!pulseSceneUsesNativeOverlay(obs_scene_get_source(horizontal), bindings) && engine.createGuidedShow(nativeSaved).value("ok").toBool(),
			"matching Lumia browser replacement removes native artwork from a saved wizard build");
		for (const QString &route : {QString(), QString(" · Portrait")}) {
			const QString name = "Replaced Native Saved Wizard · Starting" + route;
			OBSSourceAutoRelease scene = route.isEmpty() ? obs_get_source_by_name(name.toUtf8().constData()) :
				obs_canvas_get_source_by_name(canvas, name.toUtf8().constData());
			check(scene && !pulseSceneUsesSource(scene, obs_source_get_uuid(savedBrowser)) &&
				pulseSceneUsesSource(scene, obs_source_get_uuid(correctBrowser)), "copied saved scenes retain only the Lumia replacement");
			obs_scene_enum_items(obs_scene_from_source(scene), [](obs_scene_t *, obs_sceneitem_t *item, void *opaque) {
				auto &units = *static_cast<QSet<QString> *>(opaque); auto *source = obs_sceneitem_get_source(item);
				if (auto *nested = obs_scene_from_source(source)) {
					units.insert(obs_source_get_uuid(source));
					obs_scene_enum_items(nested, [](obs_scene_t *, obs_sceneitem_t *child, void *context) {
						if (obs_scene_from_source(obs_sceneitem_get_source(child))) static_cast<QSet<QString> *>(context)->insert(obs_source_get_uuid(obs_sceneitem_get_source(child)));
						return true;
					}, &units);
				}
				return true;
			}, &clonedUnits);
			obs_source_remove(scene);
		}
		obs_source_remove(wrongBrowser); obs_source_remove(correctBrowser);
		obs_data_set_bool(browserSettings, "is_local_file", false); obs_data_set_string(browserSettings, "local_file", "");
		obs_source_update(savedBrowser, browserSettings);
		QWizard wizard; QWidget lookPage(&wizard), sourcePage(&wizard);
		auto *lookLayout = new QVBoxLayout(&lookPage), *sourceLayout = new QVBoxLayout(&sourcePage);
		FlexibleSetup setup(wizard, &lookPage, lookLayout, &sourcePage, sourceLayout,
			{{"groups", QJsonObject{{"Starting", QJsonObject{{"reference", "Setup Reference"}}}}}},
			QJsonDocument::fromJson(readFile(stagePath)).array(), PulseMotionEngineTestAccess::actions(engine));
		setup.update({"Starting", "Chat"});
		for (auto *box : lookPage.findChildren<QComboBox *>()) if (box->accessibleName() == "Starting layout preset") box->setCurrentIndex(1);
		QComboBox *overlay = nullptr, *scope = nullptr;
		for (auto *box : sourcePage.findChildren<QComboBox *>()) {
			if (box->accessibleName() == "Landscape overlay 1") overlay = box;
			if (box->accessibleName() == "Landscape overlay 1 visibility") scope = box;
			if (box->accessibleName() == "Chat · Feed A") { box->addItem("Shared fixture", obs_source_get_uuid(sharedOne)); box->setCurrentIndex(box->count() - 1); }
		}
		check(overlay && scope, "saved layouts expose shared layer selectors");
		check(overlay->findData(obs_source_get_uuid(camera)) < 0 && overlay->findData(obs_source_get_uuid(savedBrowser)) >= 0,
			"wizard shared layers offer browser overlays rather than arbitrary camera inputs");
		overlay->setCurrentIndex(overlay->findData(obs_source_get_uuid(savedBrowser)));
		check(setup.problem(true).contains("already exists in saved layout"), "wizard explains nested saved-layer overlap before Finish");
		scope->setCurrentIndex(scope->findData("setup-chat/talk"));
		check(setup.problem(true).isEmpty(), "scoping the shared layer to another stage clears the overlap error");
		overlay->setCurrentIndex(overlay->findData("")); scope->setCurrentIndex(scope->findData(""));
		check(setup.problem(true).isEmpty(), "leaving the overlapping shared slot empty keeps the saved layer intact");
		obs_sceneitem_remove(savedBrowserItem); obs_source_remove(savedBrowser);
	}
	check(engine.createGuidedShow(layered).value("ok").toBool(), "all three shared layers apply to selected saved Looks on both canvases");
	QMap<QString, QString> sharedItems;
	for (const auto &value : PulseMotionEngineTestAccess::actions(engine)) {
		const auto action = value.toObject(); if (action.value("stage") != "Saved Shared Layers · Starting") continue;
		const bool secondLook = action.value("referenceLookId") == "setup-second"; int layerCount = 0;
		for (const auto &entry : action.value("items").toArray()) {
			const auto item = entry.toObject(), transform = item.value("transform").toObject();
			if (!item.value("graphic").toBool()) {
				OBSSourceAutoRelease copied = obs_get_source_by_uuid(item.value("sourceUuid").toString().toUtf8().constData());
				auto *decoration = obs_scene_find_source(obs_scene_from_source(copied), "Setup border");
				check(decoration && !obs_sceneitem_visible(decoration), "skipping a nested optional source hides it inside the copied camera unit");
				clonedUnits.insert(obs_source_get_uuid(copied));
				obs_scene_enum_items(obs_scene_from_source(copied), [](obs_scene_t *, obs_sceneitem_t *child, void *opaque) {
					if (obs_scene_from_source(obs_sceneitem_get_source(child))) static_cast<QSet<QString> *>(opaque)->insert(obs_source_get_uuid(obs_sceneitem_get_source(child))); return true;
				}, &clonedUnits);
				continue;
			}
			++layerCount;
			const QString uuid = item.value("sourceUuid").toString();
			const int slot = uuid == obs_source_get_uuid(sharedOne) ? 1 : uuid == obs_source_get_uuid(sharedTwo) ? 2 : 3;
			check(transform.value("visible").toBool() == (slot != 2 || secondLook), "saved Look overlay visibility follows its selected scope");
			check(transform.value("order").toInt() == slot && transform.value("locked").toBool(), "saved overlay layer order stays above content and locked");
			const QString key = item.value("container").toString() + "/" + QString::number(slot);
			check(!sharedItems.contains(key) || sharedItems.value(key) == item.value("itemId").toString(), "saved Looks reuse each shared overlay scene-item identity");
			sharedItems.insert(key, item.value("itemId").toString());
		}
		check(layerCount == 6, "saved Look records all three layers on both formats including hidden targets");
	}
	check(obs_sceneitem_visible(frame), "skipping the copied nested widget preserves its original visibility");
	for (const char *name : {"Saved Shared Layers · Starting", "Saved Shared Layers · Starting · Portrait"}) {
		OBSSourceAutoRelease source = obs_get_source_by_name(name); if (!source) source = obs_canvas_get_source_by_name(canvas, name);
		if (source) obs_source_remove(source);
	}
	// Explicit policy takes precedence over names, and survives source serialization.
	OBSSourceAutoRelease graphic = obs_source_create("pulse_motion_audit_fixture", "brb scene", nullptr, nullptr);
	obs_sceneitem_t *graphicItem = obs_scene_add(horizontal, graphic);
	duplicate.insert("overlays", QJsonArray{QJsonObject{{"canvas", "horizontal"}, {"order", 1}, {"source", obs_source_get_uuid(graphic)},
		{"looks", QJsonArray{"setup-starting/saved:setup-first"}}}});
	check(engine.createGuidedShow(duplicate).value("message").toString().contains("already exists in saved layout"),
		"a top-level existing saved alert is also rejected rather than double-rendered");
	QJsonObject graphicAction{{"kind", "layout"}, {"policy", "current"}, {"container", "Setup Reference"},
		{"items", QJsonArray{QJsonObject{{"container", "Setup Reference"}, {"itemId", QString::number(obs_sceneitem_get_id(graphicItem))},
			{"source", "brb scene"}, {"sourceUuid", obs_source_get_uuid(graphic)}, {"transform", PulseMotionEngineTestAccess::frame(graphicItem)}}}}};
	check(PulseMotionEngineTestAccess::graphic(engine, graphicAction), "absent policy retains legacy graphic behavior");
	OBSDataAutoRelease metadata = obs_source_get_private_settings(graphic); obs_data_set_bool(metadata, "pulseweaver.motion_graphic", false);
	check(!PulseMotionEngineTestAccess::graphic(engine, graphicAction), "explicit false selects Move for legacy BRB name");
	obs_source_set_name(graphic, "stream ended");
	check(!PulseMotionEngineTestAccess::graphic(engine, graphicAction), "source rename cannot override explicit Move");
	OBSDataAutoRelease serialized = obs_save_source(graphic);
	OBSDataAutoRelease savedMetadata = obs_data_get_obj(serialized, "private_settings");
	check(obs_data_has_user_value(savedMetadata, "pulseweaver.motion_graphic") && !obs_data_get_bool(savedMetadata, "pulseweaver.motion_graphic"),
		"explicit Move metadata persists in native source serialization");
	obs_data_set_bool(metadata, "pulseweaver.motion_graphic", true);
	check(PulseMotionEngineTestAccess::graphic(engine, graphicAction), "explicit true selects graphic-switch policy");
	for (const char *name : {"Setup Trial · Starting", "Setup Trial · Starting · Portrait", "Two Looks · Starting", "Two Looks · Starting · Portrait",
		"Mixed Trial · Chat", "Mixed Trial · Chat · Portrait", "Mixed Trial · Starting", "Mixed Trial · Starting · Portrait"}) {
		OBSSourceAutoRelease source = obs_get_source_by_name(name); if (!source) source = obs_canvas_get_source_by_name(canvas, name);
		if (source) obs_source_remove(source);
	}
	for (const QString &uuid : clonedUnits) {
		OBSSourceAutoRelease clone = obs_get_source_by_uuid(uuid.toUtf8().constData());
		if (clone) obs_source_remove(clone);
	}
	obs_source_remove(graphic); obs_source_remove(camera); obs_source_remove(replacement); obs_source_remove(border);
	obs_source_remove(sharedOne); obs_source_remove(sharedTwo); obs_source_remove(sharedThree);
	obs_source_remove(obs_scene_get_source(wrapper)); obs_source_remove(obs_scene_get_source(croppedCamera));
	obs_source_remove(obs_scene_get_source(horizontal)); obs_source_remove(obs_scene_get_source(portrait));
	obs_canvas_remove(canvas);
}

template<typename T> static T *setupField(QWidget *root, const QString &name)
{
	for (auto *widget : root->findChildren<T *>()) if (widget->accessibleName() == name) return widget;
	check(false, qPrintable("Missing setup field: " + name)); return nullptr;
}

static void setupGraphicQuestionTests(obs_source_t *landscape, obs_source_t *portrait,
	obs_source_t *square, obs_source_t *unknown)
{
	OBSDataAutoRelease localSettings = obs_data_create();
	obs_data_set_bool(localSettings, "is_local_file", true); obs_data_set_string(localSettings, "local_file", "native-title.html");
	obs_data_set_int(localSettings, "width", 640); obs_data_set_int(localSettings, "height", 480);
	OBSSourceAutoRelease nativeTitle = obs_source_create("browser_source", "Existing native title fixture", localSettings, nullptr);
	OBSSourceAutoRelease arbitraryVideo = obs_get_source_by_name("Builder graphic fixture");
	QWizard wizard;
	QWidget lookPage(&wizard), sourcePage(&wizard);
	auto *lookLayout = new QVBoxLayout(&lookPage), *sourceLayout = new QVBoxLayout(&sourcePage);
	FlexibleSetup setup(wizard, &lookPage, lookLayout, &sourcePage, sourceLayout, {}, {}, {});
	for (auto *box : lookPage.findChildren<QCheckBox *>()) {
		if (box->text() == "Getting ready") box->setChecked(false);
		if (box->text() == "Welcome") box->setChecked(true);
	}
	setup.update({"Starting"});
	auto combo = [&](const QString &name) -> QComboBox * {
		for (auto *widget : sourcePage.findChildren<QComboBox *>()) if (widget->accessibleName() == name) return widget;
		return nullptr;
	};
	auto input = [&](const QString &name) -> QLineEdit * {
		for (auto *widget : sourcePage.findChildren<QLineEdit *>()) if (widget->accessibleName() == name) return widget;
		return nullptr;
	};
	const QString horizontalLabel = "Starting · Countdown · Landscape artwork", verticalLabel = "Starting · Countdown · Portrait artwork";
	QComboBox *horizontal = combo(horizontalLabel), *vertical = combo(verticalLabel);
	check(horizontal && vertical && horizontal != vertical,
		"wizard presents independent landscape and portrait graphic questions");
	check(horizontal->currentData().toString().isEmpty() && vertical->currentData().toString().isEmpty() &&
		setup.problem(true).contains("landscape artwork"), "new wizard artwork starts unassigned and requires an explicit overlay choice");
	check(horizontal->findData("builtin") < 0 && vertical->findData("builtin") < 0 &&
		setup.draft("Unassigned Artwork Trial").value("newSources").toObject().isEmpty(),
		"wizard offers no native title or countdown and does not silently generate artwork");
	check(horizontal->findData(obs_source_get_uuid(nativeTitle)) < 0 && horizontal->findData(obs_source_get_uuid(arbitraryVideo)) < 0,
		"artwork selectors exclude local native titles and non-browser inputs");
	check(horizontal->findData(obs_source_get_uuid(landscape)) >= 0 && horizontal->findData(obs_source_get_uuid(portrait)) < 0 &&
		vertical->findData(obs_source_get_uuid(portrait)) >= 0 && vertical->findData(obs_source_get_uuid(landscape)) < 0,
		"graphic dropdowns filter existing browser artwork by matching orientation");
	for (QComboBox *field : {horizontal,vertical})
		check(field->findData(obs_source_get_uuid(square)) >= 0 && field->findData(obs_source_get_uuid(unknown)) >= 0,
			"both format questions retain square and unknown-size compatible graphics");
	auto *welcomeHorizontal = combo("Starting · Welcome · Landscape artwork"), *welcomeVertical = combo("Starting · Welcome · Portrait artwork");
	check(welcomeHorizontal && welcomeVertical, "Welcome asks for its own independent overlay assignments");
	welcomeHorizontal->setCurrentIndex(welcomeHorizontal->findData(obs_source_get_uuid(square)));
	welcomeVertical->setCurrentIndex(welcomeVertical->findData(obs_source_get_uuid(unknown)));
	horizontal->setCurrentIndex(horizontal->findData(obs_source_get_uuid(landscape)));
	vertical->setCurrentIndex(vertical->findData(obs_source_get_uuid(portrait)));
	check(setup.problem(true).isEmpty(), "independent valid H/V artwork completes the selected Starting questions");
	QJsonObject draft = setup.draft("UI Questions Trial");
	check(draft.value("overlayProvider") == "lumia", "wizard request explicitly selects the Lumia overlay policy");
	QJsonObject roles = PulseShow::rolesForLook(draft.value("stages").toArray().first().toObject(), "countdown");
	check(roles.value("graphic").toString() == obs_source_get_uuid(landscape) &&
		roles.value("graphicPortrait").toString() == obs_source_get_uuid(portrait),
		"wizard draft stores each graphic assignment under its correct canvas role");
	check(roles.value("graphicFullCanvas").toBool() && roles.value("graphicPortraitFullCanvas").toBool(),
		"imported artwork defaults to full canvas independently for both formats");
	auto *horizontalPlacement = combo(horizontalLabel + " placement"), *verticalPlacement = combo(verticalLabel + " placement");
	check(horizontalPlacement && verticalPlacement, "each artwork question offers an explicit placement choice");
	horizontalPlacement->setCurrentIndex(horizontalPlacement->findData(false));
	draft = setup.draft("UI Questions Trial"); roles = PulseShow::rolesForLook(draft.value("stages").toArray().first().toObject(), "countdown");
	check(!roles.value("graphicFullCanvas").toBool() && roles.value("graphicPortraitFullCanvas").toBool(),
		"landscape panel placement leaves the portrait full-canvas choice unchanged");
	vertical->setCurrentIndex(vertical->findData(""));
	check(setup.problem(true).contains("portrait artwork"), "wizard asks for required portrait artwork when that answer is empty");
	vertical->setCurrentIndex(vertical->findData(obs_source_get_uuid(portrait)));
	horizontal->setCurrentIndex(horizontal->findData(""));
	check(setup.problem(true).contains("landscape artwork"), "wizard asks for required landscape artwork when that answer is empty");
	QTimer::singleShot(0, [&] {
		auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget()); check(dialog, "new artwork source dialog opens");
		auto *kind = setupField<QComboBox>(dialog, "New source type");
		check(kind->count() == 1 && kind->currentData() == "browser_source", "new artwork permits only browser sources");
		setupField<QLineEdit>(dialog, "New source name")->setText("Draft Lumia countdown");
		setupField<QLineEdit>(dialog, "New source browser URL")->setText("https://overlays.lumiastream.com/audit/countdown");
		dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
	});
	setupField<QPushButton>(&sourcePage, horizontalLabel + " create new source")->click();
	draft = setup.draft("UI Questions Trial");
	check(horizontal->currentData().toString().startsWith("new:") && setup.problem(true).isEmpty() &&
		draft.value("newSources").toObject().value(horizontal->currentData().toString().mid(4)).toObject().value("kind") == "browser_source",
		"creating artwork inside the wizard produces a browser overlay draft");
	horizontal->setCurrentIndex(horizontal->findData("url")); vertical->setCurrentIndex(vertical->findData("url"));
	QLineEdit *horizontalUrl = input(horizontalLabel+" browser URL"), *verticalUrl = input(verticalLabel+" browser URL");
	check(horizontalUrl && verticalUrl, "wizard provides a browser link field for each format");
	horizontalUrl->setText("https://overlays.lumiastream.com/audit/landscape"); verticalUrl->setText("https://overlays.lumiastream.com/audit/portrait");
	check(setup.problem(true).isEmpty(), "separate valid browser URLs complete the artwork questions");
	bool nativeDurationVisible = false;
	for (auto *widget : sourcePage.findChildren<QSpinBox *>())
		if (widget->accessibleName().endsWith("countdown duration") && !widget->isHidden()) nativeDurationVisible = true;
	check(!nativeDurationVisible, "Lumia artwork does not offer an ineffective native countdown duration setting");
	horizontalUrl->setText("https://api.lumiastream.com/shared/download/example");
	check(setup.problem(true).contains("template import link"), "wizard rejects a template share link with import instructions");
	horizontalUrl->setText("https://overlays.lumiastream.com/audit/landscape");
	setup.update({"Chat"}); setup.update({"Starting"});
	for (auto *button : sourcePage.findChildren<QPushButton *>()) if (button->text() == "Next source") button->click();
	for (auto *button : sourcePage.findChildren<QPushButton *>()) if (button->text() == "Previous source") button->click();
	check(horizontal->currentData() == "url" && vertical->currentData() == "url" &&
		horizontalUrl->text() == "https://overlays.lumiastream.com/audit/landscape" && verticalUrl->text() == "https://overlays.lumiastream.com/audit/portrait",
		"group and source back-navigation retains independent format answers");
	draft = setup.draft("UI Questions Trial");
	roles = PulseShow::rolesForLook(draft.value("stages").toArray().first().toObject(), "countdown");
	const auto links = draft.value("newSources").toObject();
	for (const auto &definition : links)
		check(definition.toObject().value("kind") == "browser_source", "wizard artwork definitions always use browser overlays");
	check(roles.value("graphic") == "new:Starting/graphic/countdown/horizontal" &&
		roles.value("graphicPortrait") == "new:Starting/graphic/countdown/vertical" &&
		links.value("Starting/graphic/countdown/horizontal").toObject().value("url") == "https://overlays.lumiastream.com/audit/landscape" &&
		links.value("Starting/graphic/countdown/vertical").toObject().value("url") == "https://overlays.lumiastream.com/audit/portrait",
		"wizard emits independent new browser definitions and format roles after navigating back");
	const auto welcome = PulseShow::rolesForLook(draft.value("stages").toArray().first().toObject(), "welcome");
	check(welcome.value("graphic").toString() == obs_source_get_uuid(square) && welcome.value("graphicPortrait").toString() == obs_source_get_uuid(unknown),
		"changing Countdown artwork leaves Welcome's separate artwork intact");
	auto *overlay = combo("Landscape overlay 1"), *scope = combo("Landscape overlay 1 visibility");
	check(overlay && scope && scope->findData("setup-starting/countdown") >= 0, "overlay can target a single selected Look");
	check(overlay->findData(obs_source_get_uuid(nativeTitle)) < 0 && overlay->findData(obs_source_get_uuid(arbitraryVideo)) < 0,
		"shared overlay selectors exclude local native titles and non-browser inputs");
	QTimer::singleShot(0, [&] {
		auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget()); check(dialog, "new shared overlay dialog opens");
		auto *kind = setupField<QComboBox>(dialog, "New source type");
		check(kind->count() == 1 && kind->currentData() == "browser_source", "new shared overlays permit only browser sources");
		dialog->reject();
	});
	setupField<QPushButton>(&sourcePage, "Landscape overlay 1 create new source")->click();
	overlay->setCurrentIndex(overlay->findData(obs_source_get_uuid(landscape)));
	scope->setCurrentIndex(scope->findData("setup-starting/countdown"));
	draft = setup.draft("Scoped Overlay Trial");
	check(draft.value("overlays").toArray().first().toObject().value("looks").toArray() == QJsonArray{"setup-starting/countdown"},
		"single-Look overlay excludes other Looks in the same stage");
	auto *secondOverlay = combo("Landscape overlay 2"), *secondScope = combo("Landscape overlay 2 visibility");
	secondOverlay->setCurrentIndex(secondOverlay->findData(obs_source_get_uuid(landscape)));
	secondScope->setCurrentIndex(secondScope->findData("setup-starting/countdown"));
	check(setup.problem(true).contains("already used in another shared layer"), "wizard rejects duplicate shared sources when their Look scopes overlap");
	secondScope->setCurrentIndex(secondScope->findData("setup-starting/welcome"));
	check(setup.problem(true).isEmpty(), "same shared source remains allowed in different slots for disjoint Looks");
	secondOverlay->setCurrentIndex(secondOverlay->findData(""));
	for (auto *box : lookPage.findChildren<QCheckBox *>()) if (box->text() == "Countdown") box->setChecked(false);
	check(setup.problem(true).contains("Choose a selected Look"), "deselecting an overlay target prompts retargeting instead of silently dropping it");
	overlay->setCurrentIndex(overlay->findData(""));
	check(setup.problem(true).isEmpty(), "skipping an orphaned overlay clears the validation error");
	obs_source_remove(nativeTitle);
}

static void setupQuestionFlowTests()
{
	QTemporaryDir files;
	const QString imagePath = files.filePath("source.png"), editedPath = files.filePath("edited.png");
	QImage image(8, 8, QImage::Format_ARGB32); image.fill(Qt::cyan);
	check(image.save(imagePath) && image.save(editedPath), "local source dialog fixtures");
	const QString stingerPath = files.filePath("transition.webm"); writeFile(stingerPath, "inert dialog fixture");
	QWizard wizard; QWidget lookPage(&wizard), sourcePage(&wizard);
	auto *lookLayout = new QVBoxLayout(&lookPage), *sourceLayout = new QVBoxLayout(&sourcePage);
	FlexibleSetup setup(wizard, &lookPage, lookLayout, &sourcePage, sourceLayout, {}, {}, {});
	setup.update({"Starting", "Chat", "Game", "Projects", "Celebration", "Intermission"});
	const auto initial = setup.draft("Default Selection"); int selected = 0;
	check(initial.value("newSources").toObject().isEmpty(), "default Looks do not generate Pulse Weaver native overlays");
	for (auto *field : sourcePage.findChildren<QComboBox *>()) {
		const QString label = field->accessibleName();
		if (!label.endsWith("Landscape artwork") && !label.endsWith("Portrait artwork")) continue;
		check(field->currentData().toString().isEmpty() && field->findData("builtin") < 0,
			"every stage artwork question requires a choice and omits native overlay options");
		field->setCurrentIndex(field->findData("url"));
		setupField<QLineEdit>(&sourcePage, label + " browser URL")->setText("https://overlays.lumiastream.com/audit/wizard-artwork");
	}
	const auto longNames = setup.draft(QString(90, 'X')).value("newSources").toObject();
	check(longNames.size() >= 2, "explicit browser artwork exercises generated source names for both formats");
	QSet<QString> names;
	for (const auto &value : longNames) {
		const QString name = value.toObject().value("name").toString();
		check(name.size() <= 120 && !names.contains(name), "long valid show names produce distinct valid source names"); names.insert(name);
		check(value.toObject().value("kind") == "browser_source", "new wizard artwork remains browser based for every selected Look");
	}
	for (const auto &value : initial.value("stages").toArray()) {
		const auto stage = value.toObject(); selected += stage.value("looks").toArray().size();
		check(!stage.value("roles").toObject().contains("feedC"), "initial selections do not require an unrequested third feed");
	}
	check(selected == 7, "defaults select one functional Look per group plus Ending rather than all alternate Looks");
	check(!setup.problem(true).isEmpty(), "an unassigned required feed prevents completion");
	auto *jump = setupField<QComboBox>(&sourcePage, "Source to assign");
	const int gameQuestion = jump->findText("Game · Feed B"); check(gameQuestion >= 0, "source navigator names each stage assignment");
	jump->setCurrentIndex(gameQuestion); setup.update({"Chat", "Game"});
	check(jump->currentText() == "Game · Feed B", "changing groups preserves the current question by identity");
	auto *create = setupField<QPushButton>(&sourcePage, "Chat · Feed A create new source");
	QTimer::singleShot(0, [&] {
		auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget()); check(dialog, "new source dialog opens inside the wizard");
		auto *kind = setupField<QComboBox>(dialog, "New source type");
		check(kind->findData("image_source") >= 0 && kind->findData("dshow_input") >= 0,
			"ordinary feeds retain media and camera creation alongside Lumia overlays");
		kind->setCurrentIndex(kind->findData("image_source"));
		setupField<QLineEdit>(dialog, "New source name")->setText("Wizard draft image");
		setupField<QLineEdit>(dialog, "New source file")->setText(imagePath);
		dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
	});
	create->click();
	auto *chatFeed = setupField<QComboBox>(&sourcePage, "Chat · Feed A"), *gameFeed = setupField<QComboBox>(&sourcePage, "Game · Feed B");
	const QString sourceKey = chatFeed->currentData().toString();
	check(sourceKey.startsWith("new:") && gameFeed->findData(sourceKey) >= 0, "new source is reusable across stage groups before creation");
	check(setupField<QComboBox>(&sourcePage, "Starting · Countdown · Landscape artwork")->findData(sourceKey) < 0 &&
		setupField<QComboBox>(&sourcePage, "Landscape overlay 1")->findData(sourceKey) < 0,
		"drafted image feeds do not become selectable artwork or shared Lumia layers");
	OBSSourceAutoRelease uncreated = obs_get_source_by_name("Wizard draft image"); check(!uncreated, "drafting a source does not create or activate a native input");
	gameFeed->setCurrentIndex(gameFeed->findData(sourceKey));
	auto draft = setup.draft("New Source Trial");
	check(draft.value("newSources").toObject().size() == 1 &&
		draft.value("stages").toArray()[0].toObject().value("roles").toObject().value("feedA") == sourceKey &&
		draft.value("stages").toArray()[1].toObject().value("roles").toObject().value("feedB") == sourceKey,
		"two stages reuse one staged source definition while keeping independent roles");
	QTimer::singleShot(0, [&] {
		auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget()); check(dialog, "source edit dialog opens");
		setupField<QLineEdit>(dialog, "New source name")->setText("Edited wizard draft image");
		setupField<QLineEdit>(dialog, "New source file")->setText(editedPath);
		dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
	});
	setupField<QPushButton>(&sourcePage, "Chat · Feed A edit new source")->click();
	draft = setup.draft("New Source Trial");
	check(draft.value("newSources").toObject().value(sourceKey.mid(4)).toObject().value("target") == editedPath &&
		gameFeed->currentText().contains("Edited wizard draft image"), "editing a shared staged source updates all selectors without duplicating it");
	QTimer::singleShot(0, [&] { auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget()); check(dialog, "cancel source dialog opens"); dialog->reject(); });
	create->click(); check(setup.draft("New Source Trial").value("newSources").toObject().size() == 1, "cancelled source addition leaves the plan unchanged");
	check(setup.problem(true).isEmpty(), "new image sources satisfy required stage feeds");

	auto *addStinger = setupField<QPushButton>(&lookPage, "Chat horizontal add stinger");
	QTimer::singleShot(0, [&] {
		auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget()); check(dialog, "add stinger dialog opens inside the wizard");
		setupField<QLineEdit>(dialog, "New stinger name")->setText("Wizard staged stinger");
		setupField<QLineEdit>(dialog, "New stinger video file")->setText(stingerPath);
		setupField<QSpinBox>(dialog, "New stinger cut point")->setValue(725);
		auto *save = dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Save); check(save->isEnabled(), "readable stinger and cut point enable acceptance"); save->click();
	});
	addStinger->click(); draft = setup.draft("Stinger Trial");
	auto *chatTransition = setupField<QComboBox>(&lookPage, "Chat horizontal stage transition");
	auto *portraitTransition = setupField<QComboBox>(&lookPage, "Chat vertical stage transition");
	auto *gameTransition = setupField<QComboBox>(&lookPage, "Game horizontal stage transition");
	const QString transitionKey = chatTransition->currentData().toString();
	check(transitionKey.startsWith("new-stinger:") && portraitTransition->currentData().toString().isEmpty() &&
		draft.value("newStingers").toObject().size() == 1, "new stinger is assigned only to the chosen stage and format");
	check(draft.value("newStingers").toObject().value(transitionKey.mid(12)).toObject().value("transitionPointMs") == 725,
		"stinger cut point is retained in the build draft");
	OBSSourceAutoRelease transition = obs_get_source_by_name("Wizard staged stinger"); check(!transition, "stinger draft creates no live transition");
	gameTransition->setCurrentIndex(gameTransition->findData(transitionKey)); chatTransition->setCurrentIndex(0);
	check(setup.draft("Stinger Trial").value("newStingers").toObject().size() == 1, "other stages can reuse a staged stinger");
	gameTransition->setCurrentIndex(0);
	check(setup.draft("Stinger Trial").value("newStingers").toObject().isEmpty(), "unused staged stinger is excluded from build request");
	QTimer::singleShot(0, [&] { auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget()); check(dialog, "cancel stinger dialog opens"); dialog->reject(); });
	addStinger->click(); check(setup.draft("Stinger Trial").value("newStingers").toObject().isEmpty(), "cancelling stinger addition leaves transitions unchanged");
}

static void themedBuilderTests(const obs_video_info &video)
{
	check(PulseShow::catalogue().size() == 6, "six builder themes");
	for (const auto &look : PulseShow::theme("viewer")->looks)
		check(look.main != "chat" && look.support != "chat", "viewer chat stays an overlay rather than a content panel");
	for (const auto &theme : PulseShow::catalogue()) {
		check(theme.looks.size() == 3, "three recommended Looks per Stage");
		for (bool portrait : {false, true}) for (size_t a = 0; a < theme.looks.size(); ++a) for (size_t b = a + 1; b < theme.looks.size(); ++b) {
			bool movement = false;
			for (const auto &from : PulseShow::layout(theme.looks[a], portrait, true))
				for (const auto &to : PulseShow::layout(theme.looks[b], portrait, true))
					if (from.role == to.role && from.rect != to.rect) movement = true;
			check(movement, "each Look pair moves at least one shared source on each canvas");
		}
	}
	for (const auto &theme : PulseShow::catalogue()) for (const auto &look : theme.looks) {
		for (bool portrait : {false, true}) for (bool supporting : {false, true}) {
			const auto panels = PulseShow::layout(look, portrait, supporting);
			check(!panels.empty(), "every look has content");
			for (const auto &panel : panels)
				check(panel.rect.x() >= 0 && panel.rect.y() >= 0 && panel.rect.width() > 0 &&
					panel.rect.height() > 0 && panel.rect.right() <= 1.001 && panel.rect.bottom() <= 1.001,
					"template geometry stays inside its canvas");
		}
	}
	const auto *starting = PulseShow::theme("starting");
	const auto start = PulseShow::layout(*PulseShow::look(*starting, "countdown-camera"), false, true);
	check(start[1].rect.center().x() < .5 && start[1].treated, "starting camera is left and scoped for treatment");
	const auto *craft = PulseShow::theme("craft");
	const auto stacked = PulseShow::layout(*PulseShow::look(*craft, "work-camera"), true, true);
	check(stacked[1].role == "presenter" && stacked[1].rect.y() == 0 &&
		std::abs(stacked[0].rect.y() - stacked[1].rect.bottom()) < .001, "portrait presenter sits directly above activity");
	const auto *game = PulseShow::theme("gameplay");
	check(!PulseShow::layout(*PulseShow::look(*game,"game-camera"),false,true)[0].fill, "game image fits without automatic crop");
	check(PulseShow::layout(*PulseShow::look(*game,"game"),false,false,{{"gameFill",true}})[0].fill,
		"full-canvas gameplay honours explicit crop");
	check(!PulseShow::layout(*PulseShow::look(*craft,"work"),true,false,{{"activityFill",false}})[0].fill,
		"full-canvas camera honours explicit fit");
	const auto custom = PulseShow::layout(*PulseShow::look(*craft,"work"), false, true,
		{{"presenterRect",QJsonArray{.8,.9,.5,.5}}});
	check(custom[1].rect.right() <= 1.001 && custom[1].rect.bottom() <= 1.001,
		"manual panel dimensions are clamped inside canvas");
	QTemporaryDir directory;
	check(QDir().mkpath(directory.filePath("plugin_config/pulse-weaver-core")), "themed storage directory");
	const QString motionPath = directory.filePath("plugin_config/pulse-weaver-core/motion.json");
	const QString stagePath = directory.filePath("pulseweaver-stages.json");
	writeFile(stagePath, "[]");
	PulseMotionEngine engine(nullptr, motionPath);
	obs_video_info portraitVideo = video; portraitVideo.base_width = portraitVideo.output_width = 480;
	portraitVideo.base_height = portraitVideo.output_height = 640;
	OBSCanvasAutoRelease canvas = obs_canvas_create("Pulse Weaver Vertical", &portraitVideo, PROGRAM);
	OBSSourceAutoRelease source = obs_source_create("pulse_motion_audit_fixture", "Builder graphic fixture", nullptr, nullptr);
	const QString uuid = obs_source_get_uuid(source);
	const uint32_t originalMixers = obs_source_get_audio_mixers(source);
	QJsonObject stage{{"theme","starting"},{"looks",QJsonArray{"countdown"}},{"roles",QJsonObject{{"graphic",uuid}}}};
	QJsonObject choices{{"schema",2},{"name","Themed Trial"},{"stages",QJsonArray{stage}}};
	const auto result = engine.createGuidedShow(choices);
	check(result.value("ok").toBool() && result.value("stages").toInt()==1 && result.value("looks").toInt()==1,
		"optional camera omitted; exactly one selected Stage and Look created");
	OBSSourceAutoRelease generated = obs_get_source_by_name("Themed Trial · Starting");
	OBSSourceAutoRelease generatedPortrait = obs_canvas_get_source_by_name(canvas,"Themed Trial · Starting · Portrait");
	check(generated && generatedPortrait, "themed paired scenes created");
	const auto targets = PulseMotionEngineTestAccess::actions(engine).last().toObject().value("items").toArray();
	check(targets.size()==2, "one source target per canvas without unselected layers");
	check(obs_source_get_audio_mixers(source) == originalMixers, "reused source audio untouched");
	const QByteArray before = readFile(stagePath);
	check(!engine.createGuidedShow(choices).value("ok").toBool(), "duplicate destination rejected");
	QJsonObject missing = choices; missing.insert("name","Missing Trial"); stage.insert("roles",QJsonObject{{"graphic","missing-uuid"}}); missing.insert("stages",QJsonArray{stage});
	check(!engine.createGuidedShow(missing).value("ok").toBool(), "missing UUID rejected");
	stage.insert("looks",QJsonArray{}); missing.insert("stages",QJsonArray{stage});
	check(!engine.createGuidedShow(missing).value("ok").toBool(), "empty Look selection rejected");
	QJsonObject badOverlay = choices; badOverlay.insert("name","Overlay Trial"); badOverlay.insert("overlays",QJsonArray{QJsonObject{{"canvas","horizontal"},{"order",4},{"source",uuid}}});
	check(!engine.createGuidedShow(badOverlay).value("ok").toBool(), "fourth full-canvas slot rejected");
	check(readFile(stagePath)==before, "validation failures preserve existing stages");
	// All source types are inert test fixtures, including browser sources. No URL
	// is fetched and no device is opened by these catalogue/transaction checks.
	for (const char *kind : {"dshow_input", "game_capture", "monitor_capture", "image_source", "ffmpeg_source"}) {
		obs_source_info type{}; type.id=kind; type.type=OBS_SOURCE_TYPE_INPUT; type.output_flags=OBS_SOURCE_VIDEO;
		type.get_name=[](void *){return "Inert builder fixture";}; type.get_width=[](void *){return 640u;}; type.get_height=[](void *){return 480u;};
		type.create=[](obs_data_t *,obs_source_t *)->void *{return new int(1);}; type.destroy=[](void *p){delete static_cast<int *>(p);};
		obs_register_source(&type);
	}
	for (const char *kind : {"color_filter", "mask_filter"}) {
		obs_source_info type{};type.id=kind;type.type=OBS_SOURCE_TYPE_FILTER;type.output_flags=OBS_SOURCE_VIDEO;
		type.get_name=[](void *){return "Inert filter fixture";};type.create=[](obs_data_t *,obs_source_t *)->void *{return new int(1);};type.destroy=[](void *p){delete static_cast<int *>(p);};obs_register_source(&type);
	}
	OBSSourceAutoRelease camera=obs_source_create("dshow_input","Builder camera fixture",nullptr,nullptr);
	OBSSourceAutoRelease screen=obs_source_create("monitor_capture","Builder screen fixture",nullptr,nullptr);
	OBSSourceAutoRelease gameSource=obs_source_create("game_capture","Builder game fixture",nullptr,nullptr);
	QTemporaryDir localFiles;
	const QString imagePath = localFiles.filePath("image.png"), mediaPath = localFiles.filePath("media.webm");
	QImage picture(8, 8, QImage::Format_ARGB32); picture.fill(Qt::cyan); check(picture.save(imagePath), "builder image fixture");
	writeFile(mediaPath, "inert media source fixture");
	QJsonObject mediaRequest{{"schema", 2}, {"overlayProvider", "lumia"}, {"name", "New Media Trial"}, {"stages", QJsonArray{
		QJsonObject{{"theme", "setup-chat"}, {"looks", QJsonArray{"talk"}}, {"roles", QJsonObject{{"feedA", "new:still"}}}},
		QJsonObject{{"theme", "setup-game"}, {"looks", QJsonArray{"play"}}, {"roles", QJsonObject{{"feedA", "new:still"}, {"feedB", "new:video"}}}}}},
		{"newSources", QJsonObject{{"still", QJsonObject{{"name", "Created still fixture"}, {"kind", "image_source"}, {"target", imagePath}}},
			{"video", QJsonObject{{"name", "Created media fixture"}, {"kind", "ffmpeg_source"}, {"target", mediaPath}, {"loop", true}}}}}};
	check(engine.createGuidedShow(mediaRequest).value("ok").toBool(), "new image and video sources can be created inside the show transaction");
	OBSSourceAutoRelease newImage = obs_get_source_by_name("Created still fixture"), newMedia = obs_get_source_by_name("Created media fixture");
	check(newImage && newMedia, "both staged media inputs are created once");
	OBSDataAutoRelease imageSettings = obs_source_get_settings(newImage), mediaSettings = obs_source_get_settings(newMedia);
	check(QString::fromUtf8(obs_data_get_string(imageSettings, "file")) == imagePath &&
		QString::fromUtf8(obs_data_get_string(mediaSettings, "local_file")) == mediaPath && obs_data_get_bool(mediaSettings, "looping"),
		"new media inputs preserve selected local files and loop choice");
	int imageUses = 0;
	for (const auto &value : PulseMotionEngineTestAccess::actions(engine)) {
		const auto action = value.toObject(); if (!action.value("stage").toString().startsWith("New Media Trial · ")) continue;
		for (const auto &entry : action.value("items").toArray()) if (entry.toObject().value("sourceUuid").toString() == obs_source_get_uuid(newImage)) ++imageUses;
	}
	check(imageUses == 4, "a new image definition is reused by two stages and both canvases without duplicate inputs");
	const auto beforeNewSourceFailure = readFile(stagePath), beforeNewSourceActions = QJsonDocument(PulseMotionEngineTestAccess::actions(engine)).toJson();
	QJsonObject badMedia = mediaRequest; badMedia.insert("name", "Missing Media Trial");
	badMedia.insert("newSources", QJsonObject{{"still", QJsonObject{{"name", "Missing local still"}, {"kind", "image_source"}, {"target", localFiles.filePath("absent.png")}}}});
	check(!engine.createGuidedShow(badMedia).value("ok").toBool() && readFile(stagePath) == beforeNewSourceFailure &&
		QJsonDocument(PulseMotionEngineTestAccess::actions(engine)).toJson() == beforeNewSourceActions,
		"missing staged image rejects the show without altering either catalogue");
	QJsonObject stingerStage{{"theme", "setup-chat"}, {"looks", QJsonArray{"talk"}}, {"roles", QJsonObject{{"feedA", obs_source_get_uuid(camera)}}},
		{"transitions", QJsonObject{{"horizontalTransition", "new-stinger:transition"}}}};
	QJsonObject stingerSpec{{"name", "New stinger fixture"}, {"path", mediaPath}, {"transitionPointMs", 725}};
	QJsonObject stingerRequest{{"schema", 2}, {"name", "Stinger Failure Trial"}, {"stages", QJsonArray{stingerStage}},
		{"newStingers", QJsonObject{{"transition", stingerSpec}}}};
	const auto graphicsBefore = QDir(directory.filePath("plugin_config/pulse-weaver-core/show-graphics")).entryList(QDir::Files);
	for (const QString &failure : {QString("file"), QString("timing"), QString("frontend")}) {
		auto spec = stingerSpec; if (failure == "file") spec.insert("path", localFiles.filePath("absent.webm"));
		if (failure == "timing") spec.insert("transitionPointMs", -1);
		stingerRequest.insert("newStingers", QJsonObject{{"transition", spec}});
		const auto failed = engine.createGuidedShow(stingerRequest);
		check(!failed.value("ok").toBool() && readFile(stagePath) == beforeNewSourceFailure &&
			QJsonDocument(PulseMotionEngineTestAccess::actions(engine)).toJson() == beforeNewSourceActions,
			"invalid stinger or unavailable frontend leaves both catalogues unchanged");
		check(QDir(directory.filePath("plugin_config/pulse-weaver-core/show-graphics")).entryList(QDir::Files) == graphicsBefore,
			"failed stinger creation rolls back generated graphic files");
	}
	OBSSourceAutoRelease failedStingerScene = obs_get_source_by_name("Stinger Failure Trial · Chat");
	check(!failedStingerScene, "failed stinger creation leaves no partial scene");
	for (const char *name : {"New Media Trial · Chat", "New Media Trial · Chat · Portrait", "New Media Trial · Game", "New Media Trial · Game · Portrait"}) {
		OBSSourceAutoRelease source = obs_get_source_by_name(name); if (!source) source = obs_canvas_get_source_by_name(canvas, name); if (source) obs_source_remove(source);
	}
	obs_source_remove(newImage); obs_source_remove(newMedia);
	QJsonArray allStages;int totalLooks=0;
	for(const auto &theme:PulseShow::catalogue()) {QJsonArray ids;for(const auto &look:theme.looks){ids.append(look.id);++totalLooks;}
		QJsonObject roles{{"presenter",obs_source_get_uuid(camera)},{"activity",obs_source_get_uuid(camera)},
			{"detail",obs_source_get_uuid(camera)},{"screen",obs_source_get_uuid(screen)},{"game",obs_source_get_uuid(gameSource)},
			{"chat",uuid},{"graphic",uuid}};
		allStages.append(QJsonObject{{"theme",theme.id},{"looks",ids},{"roles",roles}});
	}
	OBSSourceAutoRelease alerts=obs_source_create("pulse_motion_audit_fixture","Builder alerts fixture",nullptr,nullptr);
	OBSSourceAutoRelease stickers=obs_source_create("pulse_motion_audit_fixture","Builder stickers fixture",nullptr,nullptr);
	OBSSourceAutoRelease chat=obs_source_create("pulse_motion_audit_fixture","Builder chat overlay fixture",nullptr,nullptr);
	QJsonArray everyLook;for(const auto &theme:PulseShow::catalogue())for(const auto &look:theme.looks)everyLook.append(theme.id+"/"+look.id);
	QJsonArray overlays;
	for(const QString &route:{QString("horizontal"),QString("vertical")})for(int order=1;order<=3;++order)
		overlays.append(QJsonObject{{"canvas",route},{"order",order},{"source",obs_source_get_uuid(order==1?alerts.Get():order==2?stickers.Get():chat.Get())},{"looks",everyLook}});
	QJsonObject full{{"schema",2},{"name","All Themes"},{"stages",allStages},{"newSources",QJsonObject{}},{"overlays",overlays}};
	const QJsonObject fullResult=engine.createGuidedShow(full);
	check(fullResult.value("ok").toBool()&&fullResult.value("looks").toInt()==totalLooks,"all catalogue Looks create successfully with inert sources");
	const QJsonArray allActions=PulseMotionEngineTestAccess::actions(engine);
	QHash<QString,QJsonObject> craftActions;
	for(const auto &value:allActions){const auto action=value.toObject();if(action.value("stage").toString()=="All Themes · Craft focus")
		craftActions.insert(action.value("templateId").toString(),action);}
	for(const QString &destination:{QString("craft/work-camera"),QString("craft/presenter-work")}){
		check(craftActions.contains("craft/work")&&craftActions.contains(destination),"all Craft movement endpoints exist");
		PulseMotionEngineTestAccess::movement(engine,craftActions.value("craft/work"),craftActions.value(destination));
		PulseMotionEngineTestAccess::movement(engine,craftActions.value(destination),craftActions.value("craft/work"));
	}
	for(const auto &value:allActions){
		const auto action=value.toObject();if(!action.value("stage").toString().startsWith("All Themes · "))continue;
		for(const QString &suffix:{QString(""),QString(" · Portrait")}){
			const QString container=action.value("container").toString()+suffix;QJsonObject chatTarget;int highest=-1;
			for(const auto &value:action.value("items").toArray()){
				const auto item=value.toObject(),transform=item.value("transform").toObject();if(item.value("container").toString()!=container)continue;
				if(transform.value("visible").toBool())highest=qMax(highest,transform.value("order").toInt());
				if(item.value("sourceUuid").toString()==obs_source_get_uuid(chat))chatTarget=transform;
			}
			check(!chatTarget.isEmpty()&&chatTarget.value("visible").toBool()&&chatTarget.value("order").toInt()==highest,
				"chat overlay is visible and topmost in every Look on both canvases");
			check(chatTarget.value("positionX").toDouble()==0&&chatTarget.value("positionY").toDouble()==0&&
				chatTarget.value("boundsWidth").toInt()==(suffix.isEmpty()?640:480)&&chatTarget.value("boundsHeight").toInt()==(suffix.isEmpty()?480:640),
				"chat overlay keeps full-canvas bounds in every Look");
		}
	}
	for(const auto &theme:PulseShow::catalogue()){
		QSet<QString> allItems;for(const auto &value:allActions){const QJsonObject action=value.toObject();if(action.value("stage").toString()=="All Themes · "+theme.name)for(const auto &target:action.value("items").toArray())allItems.insert(target.toObject().value("container").toString()+target.toObject().value("itemId").toString());}
		for(const auto &value:allActions){const QJsonObject action=value.toObject();if(action.value("stage").toString()!="All Themes · "+theme.name)continue;
			check(action.value("items").toArray().size()==allItems.size(),"every Look explicitly controls all its Stage layers");}
		QJsonArray stageActions; for(const auto &value:allActions) if(value.toObject().value("stage").toString()=="All Themes · "+theme.name)stageActions.append(value);
		for(int a=0;a<stageActions.size();++a)for(int b=a+1;b<stageActions.size();++b)for(const QString &route:{QString(""),QString(" · Portrait")}){
			bool moved=false;const QString container="All Themes · "+theme.name+route;
			for(const auto &left:stageActions[a].toObject().value("items").toArray())for(const auto &right:stageActions[b].toObject().value("items").toArray()){
				const auto from=left.toObject(),to=right.toObject();
				if(from.value("container").toString()!=container || to.value("container").toString()!=container || from.value("itemId")!=to.value("itemId"))continue;
				const auto f=from.value("transform").toObject(),t=to.value("transform").toObject();
				if(f.value("visible").toBool()&&t.value("visible").toBool()&&
				   (f.value("boundsWidth")!=t.value("boundsWidth")||f.value("boundsHeight")!=t.value("boundsHeight")||
				    f.value("positionX")!=t.value("positionX")||f.value("positionY")!=t.value("positionY")))moved=true;
			}
			check(moved,"generated Look pairs keep a shared moving item on each canvas");
		}
	}
	check(obs_source_filter_count(camera)==0,"Starting treatment does not filter the reused camera");
	QJsonObject countdownStage{{"theme","starting"},{"looks",QJsonArray{"countdown","countdown-camera","introduction"}},
		{"roles",QJsonObject{{"presenter",obs_source_get_uuid(camera)},{"graphic","new:timer"}}}};
	QJsonObject countdownChoices{{"schema",2},{"name","Shared Countdown"},{"stages",QJsonArray{countdownStage}},
		{"newSources",QJsonObject{{"timer",QJsonObject{{"name","Shared countdown fixture"},{"kind","builtin_graphic"},{"text","Starting soon"},{"minutes",5}}}}}};
	QJsonObject forbiddenWizard = countdownChoices;
	forbiddenWizard.insert("overlayProvider", "lumia"); forbiddenWizard.insert("name", "Forbidden Native Wizard");
	const auto beforeNativeWizard = readFile(stagePath);
	const auto beforeNativeWizardActions = PulseMotionEngineTestAccess::actions(engine);
	const auto filesBeforeNativeWizard = QDir(directory.filePath("plugin_config/pulse-weaver-core/show-graphics")).entryList(QDir::Files);
	const auto nativeWizardResult = engine.createGuidedShow(forbiddenWizard);
	check(!nativeWizardResult.value("ok").toBool() && nativeWizardResult.value("message").toString().contains("Lumia", Qt::CaseInsensitive) &&
		readFile(stagePath) == beforeNativeWizard && PulseMotionEngineTestAccess::actions(engine) == beforeNativeWizardActions &&
		QDir(directory.filePath("plugin_config/pulse-weaver-core/show-graphics")).entryList(QDir::Files) == filesBeforeNativeWizard,
		"Lumia wizard rejects native graphic definitions before generating files or altering either catalogue");
	OBSSourceAutoRelease forbiddenScene = obs_get_source_by_name("Forbidden Native Wizard · Starting");
	check(!forbiddenScene, "native artwork rejection leaves no partial wizard scene");
	forbiddenWizard.insert("name", "Unused Native Wizard");
	forbiddenWizard.insert("stages", QJsonArray{QJsonObject{{"theme", "setup-chat"}, {"looks", QJsonArray{"talk"}},
		{"roles", QJsonObject{{"feedA", obs_source_get_uuid(camera)}}}}});
	check(!engine.createGuidedShow(forbiddenWizard).value("ok").toBool() && readFile(stagePath) == beforeNativeWizard &&
		PulseMotionEngineTestAccess::actions(engine) == beforeNativeWizardActions,
		"even an unused native graphic definition is rejected from a Lumia wizard request");
	check(engine.createGuidedShow(countdownChoices).value("ok").toBool(),"unmarked legacy API retains shared countdown compatibility");
	QSet<QString> graphicSources,treatedCameras;
	for(const auto &value:PulseMotionEngineTestAccess::actions(engine)){
		const auto action=value.toObject();if(action.value("stage").toString()!="Shared Countdown · Starting")continue;
		check(action.value("durationMs").toInt()==850,"new Looks use visible 850 ms movement");
		for(const auto &value:action.value("items").toArray()){
			const auto item=value.toObject();if(item.value("source").toString().startsWith("Shared countdown fixture"))graphicSources.insert(item.value("sourceUuid").toString());
			else treatedCameras.insert(item.value("sourceUuid").toString());
		}
	}
	check(graphicSources.size()==2,"countdown reuses one browser source per canvas across all three Looks");
	check(treatedCameras.size()==2,"grey presenter reuses one wrapper per canvas across all three Looks");
	OBSCanvasAutoRelease mainCanvas = obs_get_main_canvas();
	for (const QString &uuid : treatedCameras) {
		OBSSourceAutoRelease treatment = obs_get_source_by_uuid(uuid.toUtf8().constData());
		const bool vertical = QString::fromUtf8(obs_source_get_name(treatment)).contains(" · vertical camera");
		OBSCanvasAutoRelease owner = obs_source_get_canvas(treatment);
		check(owner == (vertical ? canvas.Get() : mainCanvas.Get()),
			"new treated camera wrapper belongs to its destination canvas");
		check(obs_source_filter_count(treatment) == 2 &&
			obs_scene_find_source(obs_scene_from_source(treatment), "Builder camera fixture") != nullptr,
			"canvas-specific treatment retains both wrapper filters and reuses raw capture");
		OBSDataAutoRelease saved = obs_save_source(treatment);
		check(QString::fromUtf8(obs_data_get_string(saved,"canvas_uuid")) == obs_canvas_get_uuid(owner),
			"treated wrapper persists correct native canvas ownership");
	}
	QJsonObject browserStage{{"theme","starting"},{"looks",QJsonArray{"countdown","introduction"}},
		{"roles",QJsonObject{{"graphic","new:browser"}}}};
	QJsonObject browserChoices{{"schema",2},{"overlayProvider","lumia"},{"name","Canvas Browser Trial"},{"stages",QJsonArray{browserStage}},
		{"newSources",QJsonObject{{"browser",QJsonObject{{"name","Canvas browser fixture"},{"kind","browser_source"},
			{"url","https://overlays.lumiastream.com/audit/new-browser"}}}}}};
	check(engine.createGuidedShow(browserChoices).value("ok").toBool(),
		"one browser definition creates both canvas variants without network activity");
	QHash<QString,QString> browserSources, browserItems;
	for (const auto &value : PulseMotionEngineTestAccess::actions(engine)) {
		const auto action = value.toObject();
		if(action.value("stage").toString() != "Canvas Browser Trial · Starting") continue;
		check(action.value("items").toArray().size() == 2, "browser-only Look contains one input per canvas");
		for (const auto &value : action.value("items").toArray()) {
			const auto target = value.toObject();
			const QString container = target.value("container").toString(), uuid = target.value("sourceUuid").toString();
			const bool vertical = container.endsWith(" · Portrait");
			OBSSourceAutoRelease browser = obs_get_source_by_uuid(uuid.toUtf8().constData());
			OBSDataAutoRelease settings = obs_source_get_settings(browser);
			check(!obs_data_get_bool(settings, "shutdown") && !obs_data_get_bool(settings, "restart_when_active"),
				"new imported browser overlays retain timer and widget state across Look switches");
			const int width = vertical ? 480 : 640, height = vertical ? 640 : 480;
			check(obs_source_get_width(browser) == width && obs_source_get_height(browser) == height &&
				obs_data_get_int(settings,"width") == width && obs_data_get_int(settings,"height") == height,
				"each browser renders with its destination canvas dimensions");
			check(QString::fromUtf8(obs_source_get_name(browser)) ==
				QString("Canvas browser fixture · ") + (vertical ? "vertical" : "horizontal"),
				"separate browser variants have collision-free canvas-specific names");
			check(!browserSources.contains(container) || browserSources.value(container) == uuid,
				"browser source UUID stays stable across Looks on the same canvas");
			check(!browserItems.contains(container) || browserItems.value(container) == target.value("itemId").toString(),
				"browser scene-item identity stays stable across Looks on the same canvas");
			browserSources.insert(container,uuid); browserItems.insert(container,target.value("itemId").toString());
		}
	}
	check(browserSources.size() == 2 && browserSources.values()[0] != browserSources.values()[1],
		"horizontal and portrait browser variants never share the same cached source");
	OBSDataAutoRelease landscapeBrowserSettings = obs_data_create(), portraitBrowserSettings = obs_data_create();
	obs_data_set_int(landscapeBrowserSettings,"width",640); obs_data_set_int(landscapeBrowserSettings,"height",480);
	obs_data_set_int(portraitBrowserSettings,"width",480); obs_data_set_int(portraitBrowserSettings,"height",640);
	obs_data_set_string(landscapeBrowserSettings,"url","https://overlays.lumiastream.com/audit/existing-landscape");
	obs_data_set_string(portraitBrowserSettings,"url","https://overlays.lumiastream.com/audit/existing-portrait");
	OBSSourceAutoRelease landscapeGraphic = obs_source_create("browser_source","Existing landscape graphic",landscapeBrowserSettings,nullptr);
	OBSSourceAutoRelease portraitGraphic = obs_source_create("browser_source","Existing portrait graphic",portraitBrowserSettings,nullptr);
	QJsonObject separateGraphics{{"schema",2},{"overlayProvider","lumia"},{"name","Separate Graphics Trial"},{"stages",QJsonArray{QJsonObject{
		{"theme","starting"},{"looks",QJsonArray{"countdown"}},
		{"roles",QJsonObject{{"graphic",obs_source_get_uuid(landscapeGraphic)},{"graphicPortrait",obs_source_get_uuid(portraitGraphic)}}}}}}};
	check(engine.createGuidedShow(separateGraphics).value("ok").toBool(),
		"wizard accepts independently assigned landscape and portrait graphic inputs");
	const auto separateTargets = PulseMotionEngineTestAccess::actions(engine).last().toObject().value("items").toArray();
	check(separateTargets.size() == 2 &&
		separateTargets[0].toObject().value("sourceUuid").toString() == obs_source_get_uuid(landscapeGraphic) &&
		separateTargets[1].toObject().value("sourceUuid").toString() == obs_source_get_uuid(portraitGraphic),
		"explicit portrait graphic is selected only on the portrait canvas");
	const QByteArray beforeGraphicFailure = readFile(stagePath);
	const QJsonArray beforeGraphicActions = PulseMotionEngineTestAccess::actions(engine);
	auto rejectedGraphic = [&](const QJsonObject &request, const char *message, bool lumiaPolicy = false) {
		const auto result = engine.createGuidedShow(request);
		check(!result.value("ok").toBool() && (!lumiaPolicy || result.value("message").toString().contains("Lumia", Qt::CaseInsensitive)) &&
			readFile(stagePath) == beforeGraphicFailure &&
			PulseMotionEngineTestAccess::actions(engine) == beforeGraphicActions,message);
	};
	QString nativeLandscape;
	for (const auto &id : graphicSources) {
		OBSSourceAutoRelease source = obs_get_source_by_uuid(id.toUtf8().constData());
		if (obs_source_get_width(source) > obs_source_get_height(source)) nativeLandscape = id;
	}
	check(!nativeLandscape.isEmpty(), "legacy native-title fixture matches landscape so rejection exercises overlay policy");
	for (const QString &kind : {QString("image_source"), QString("existing-camera"), QString("existing-native")}) {
		QJsonObject forbidden = separateGraphics; forbidden.insert("name", "Forbidden Wizard " + kind);
		auto forbiddenStage = forbidden.value("stages").toArray().first().toObject();
		auto forbiddenRoles = forbiddenStage.value("roles").toObject();
		QString forbiddenId = kind == "existing-camera" ? QString::fromUtf8(obs_source_get_uuid(camera)) :
			kind == "existing-native" ? nativeLandscape : "new:image";
		if (kind == "image_source") forbidden.insert("newSources", QJsonObject{{"image", QJsonObject{
			{"name", "Forbidden still artwork"}, {"kind", "image_source"}, {"target", imagePath}}}});
		forbiddenRoles.insert("graphic", forbiddenId); forbiddenStage.insert("roles", forbiddenRoles);
		forbidden.insert("stages", QJsonArray{forbiddenStage});
		rejectedGraphic(forbidden, "Lumia wizard artwork rejects new images, existing cameras and local native titles without mutations", true);
		forbiddenStage.insert("roles", separateGraphics.value("stages").toArray().first().toObject().value("roles"));
		forbidden.insert("stages", QJsonArray{forbiddenStage});
		forbidden.insert("overlays", QJsonArray{QJsonObject{{"canvas", "horizontal"}, {"order", 1}, {"source", forbiddenId},
			{"looks", QJsonArray{"starting/countdown"}}}});
		rejectedGraphic(forbidden, "Lumia shared layers enforce the same browser-only policy before mutations", true);
	}
	QJsonObject invalidGraphic = separateGraphics;
	invalidGraphic.insert("name","Missing Portrait Graphic Trial");
	QJsonObject invalidStage = invalidGraphic.value("stages").toArray().first().toObject();
	QJsonObject invalidRoles = invalidStage.value("roles").toObject();
	invalidRoles.insert("graphicPortrait",""); invalidStage.insert("roles",invalidRoles);
	invalidGraphic.insert("stages",QJsonArray{invalidStage});
	rejectedGraphic(invalidGraphic,"an explicitly unassigned required portrait graphic fails without catalogue mutation");
	invalidGraphic.insert("name","Wrong Landscape Graphic Trial");
	invalidRoles.insert("graphic",obs_source_get_uuid(portraitGraphic));
	invalidRoles.insert("graphicPortrait",obs_source_get_uuid(portraitGraphic)); invalidStage.insert("roles",invalidRoles);
	invalidGraphic.insert("stages",QJsonArray{invalidStage});
	rejectedGraphic(invalidGraphic,"existing portrait browser graphic cannot silently stretch onto landscape");
	invalidGraphic.insert("name","Wrong Portrait Graphic Trial");
	invalidRoles.insert("graphic",obs_source_get_uuid(landscapeGraphic));
	invalidRoles.insert("graphicPortrait",obs_source_get_uuid(landscapeGraphic)); invalidStage.insert("roles",invalidRoles);
	invalidGraphic.insert("stages",QJsonArray{invalidStage});
	rejectedGraphic(invalidGraphic,"existing landscape browser graphic cannot silently stretch onto portrait");
	for (const QString &route : {QString("horizontal"),QString("vertical")}) {
		QJsonObject invalidOverlay = separateGraphics;
		invalidOverlay.insert("name","Wrong Overlay "+route);
		invalidOverlay.insert("overlays",QJsonArray{QJsonObject{{"canvas",route},{"order",1},
			{"source",obs_source_get_uuid(route == "horizontal" ? portraitGraphic.Get() : landscapeGraphic.Get())},
			{"looks",QJsonArray{"starting/countdown"}}}});
		rejectedGraphic(invalidOverlay,"existing full-canvas browser overlay must match its assigned orientation");
	}
	OBSDataAutoRelease squareBrowserSettings = obs_data_create();
	obs_data_set_int(squareBrowserSettings,"width",480); obs_data_set_int(squareBrowserSettings,"height",480);
	obs_data_set_string(squareBrowserSettings,"url","https://overlays.lumiastream.com/audit/existing-square");
	OBSSourceAutoRelease squareGraphic = obs_source_create("browser_source","Square graphic fixture",squareBrowserSettings,nullptr);
	OBSDataAutoRelease unknownBrowserSettings = obs_data_create();
	obs_data_set_string(unknownBrowserSettings,"url","https://overlays.lumiastream.com/audit/existing-unknown");
	OBSSourceAutoRelease unknownGraphic = obs_source_create("browser_source","Unknown-size graphic fixture",unknownBrowserSettings,nullptr);
	setupGraphicQuestionTests(landscapeGraphic,portraitGraphic,squareGraphic,unknownGraphic);
	setupQuestionFlowTests();
	QJsonObject perLookStage{{"theme", "setup-intermission"}, {"looks", QJsonArray{"brb", "ending"}},
		{"lookArtwork", QJsonObject{
			{"brb", QJsonObject{{"graphic", obs_source_get_uuid(landscapeGraphic)}, {"graphicPortrait", obs_source_get_uuid(portraitGraphic)}}},
			{"ending", QJsonObject{{"graphic", obs_source_get_uuid(squareGraphic)}, {"graphicPortrait", obs_source_get_uuid(unknownGraphic)}}}}}};
	QJsonObject perLookRequest{{"schema", 2}, {"name", "Per Look Artwork"}, {"stages", QJsonArray{perLookStage}}};
	check(engine.createGuidedShow(perLookRequest).value("ok").toBool(), "BRB and Ending accept different artwork within one stage");
	for (const auto &value : PulseMotionEngineTestAccess::actions(engine)) {
		const auto action = value.toObject(); if (action.value("stage") != "Per Look Artwork · Intermission") continue;
		const bool ending = action.value("templateId").toString().endsWith("/ending");
		int visible = 0, hidden = 0;
		for (const auto &entry : action.value("items").toArray()) {
			const auto item = entry.toObject(); const bool shown = item.value("transform").toObject().value("visible").toBool();
			if (!shown) { ++hidden; continue; } ++visible;
			const bool portrait = item.value("container").toString().endsWith("Portrait");
			const auto expected = ending ? (portrait ? unknownGraphic.Get() : squareGraphic.Get()) :
				(portrait ? portraitGraphic.Get() : landscapeGraphic.Get());
			check(item.value("sourceUuid").toString() == obs_source_get_uuid(expected), "each Look displays its own artwork on the correct canvas");
		}
		check(visible == 2 && hidden == 2, "each Look hides the other Look's artwork in both directions");
	}
	QJsonObject duplicateSlots = perLookRequest; duplicateSlots.insert("name", "Overlapping Shared Slots");
	QJsonArray splitLayers{
		QJsonObject{{"canvas", "horizontal"}, {"order", 1}, {"source", obs_source_get_uuid(alerts)}, {"looks", QJsonArray{"setup-intermission/brb"}}},
		QJsonObject{{"canvas", "horizontal"}, {"order", 2}, {"source", obs_source_get_uuid(alerts)}, {"looks", QJsonArray{"setup-intermission/brb"}}}};
	duplicateSlots.insert("overlays", splitLayers);
	const auto beforeSlots = readFile(stagePath), beforeSlotActions = QJsonDocument(PulseMotionEngineTestAccess::actions(engine)).toJson();
	const auto duplicateSlotResult = engine.createGuidedShow(duplicateSlots);
	check(!duplicateSlotResult.value("ok").toBool() && duplicateSlotResult.value("message").toString().contains("more than one shared layer") &&
		readFile(stagePath) == beforeSlots && QJsonDocument(PulseMotionEngineTestAccess::actions(engine)).toJson() == beforeSlotActions,
		"duplicate shared source with overlapping scope is rejected before show creation");
	auto endingLayer = splitLayers[1].toObject(); endingLayer.insert("looks", QJsonArray{"setup-intermission/ending"}); splitLayers[1] = endingLayer;
	splitLayers.append(QJsonObject{{"canvas", "vertical"}, {"order", 1}, {"source", obs_source_get_uuid(alerts)},
		{"looks", QJsonArray{"setup-intermission/brb", "setup-intermission/ending"}}});
	duplicateSlots.insert("name", "Disjoint Shared Slots"); duplicateSlots.insert("overlays", splitLayers);
	check(engine.createGuidedShow(duplicateSlots).value("ok").toBool(), "same shared source is allowed for disjoint Looks and the other canvas");
	for (const auto &value : PulseMotionEngineTestAccess::actions(engine)) {
		const auto action = value.toObject(); if (action.value("stage") != "Disjoint Shared Slots · Intermission") continue;
		int shown = 0;
		for (const auto &entry : action.value("items").toArray()) if (entry.toObject().value("sourceUuid").toString() == obs_source_get_uuid(alerts) &&
			entry.toObject().value("transform").toObject().value("visible").toBool()) ++shown;
		check(shown == 2, "disjoint shared slots render the source once per canvas in each Look");
	}
	for (const char *name : {"Disjoint Shared Slots · Intermission", "Disjoint Shared Slots · Intermission · Portrait"}) {
		OBSSourceAutoRelease source = obs_get_source_by_name(name); if (!source) source = obs_canvas_get_source_by_name(canvas, name); if (source) obs_source_remove(source);
	}
	QJsonObject fullCanvasStage{{"theme", "setup-starting"}, {"looks", QJsonArray{"ready"}},
		{"roles", QJsonObject{{"feedA", obs_source_get_uuid(camera)}}}, {"lookArtwork", QJsonObject{{"ready", QJsonObject{
			{"graphic", obs_source_get_uuid(landscapeGraphic)}, {"graphicPortrait", obs_source_get_uuid(portraitGraphic)},
			{"graphicFullCanvas", true}, {"graphicPortraitFullCanvas", false}}}}}};
	check(engine.createGuidedShow({{"schema", 2}, {"name", "Artwork Placement Trial"}, {"stages", QJsonArray{fullCanvasStage}}}).value("ok").toBool(),
		"a Look can independently use full-canvas artwork or a layout panel in each format");
	for (const auto &entry : PulseMotionEngineTestAccess::actions(engine).last().toObject().value("items").toArray()) {
		const auto item = entry.toObject(), transform = item.value("transform").toObject(); if (!item.value("graphic").toBool()) continue;
		const bool vertical = item.value("container").toString().endsWith("Portrait");
		check(std::abs(transform.value("boundsWidth").toDouble() - (vertical ? 480 : 640)) < .01 &&
			std::abs(transform.value("boundsHeight").toDouble() - (vertical ? 640 * .17 : 480)) <= .25 &&
			std::abs(transform.value("positionY").toDouble() - (vertical ? 640 * .8 : 0)) <= .25,
			"full-canvas imported READY artwork is not squeezed into the footer while explicit panel artwork retains its layout");
	}
	for (const char *name : {"Artwork Placement Trial · Starting", "Artwork Placement Trial · Starting · Portrait"}) {
		OBSSourceAutoRelease source = obs_get_source_by_name(name); if (!source) source = obs_canvas_get_source_by_name(canvas, name); if (source) obs_source_remove(source);
	}
	const auto beforeArtworkFailure = PulseMotionEngineTestAccess::actions(engine);
	const auto beforeArtworkStages = readFile(stagePath);
	QJsonObject invalidPerLook = perLookStage;
	invalidPerLook.insert("lookArtwork", QJsonObject{{"brb", perLookStage.value("lookArtwork").toObject().value("brb")},
		{"ending", QJsonObject{{"graphic", obs_source_get_uuid(landscapeGraphic)}, {"graphicPortrait", obs_source_get_uuid(landscapeGraphic)}}}});
	check(!engine.createGuidedShow({{"schema", 2}, {"name", "Invalid Look Artwork"}, {"stages", QJsonArray{invalidPerLook}}}).value("ok").toBool() &&
		PulseMotionEngineTestAccess::actions(engine) == beforeArtworkFailure && readFile(stagePath) == beforeArtworkStages,
		"per-Look portrait mismatch is rejected without partial creation");
	QJsonObject invalidImport = browserChoices; invalidImport.insert("name", "Import URL Trial");
	invalidImport.insert("newSources", QJsonObject{{"browser", QJsonObject{{"name", "Import URL fixture"}, {"kind", "browser_source"},
		{"url", "https://api.lumiastream.com/shared/download/example"}}}});
	check(!engine.createGuidedShow(invalidImport).value("ok").toBool() && readFile(stagePath) == beforeArtworkStages,
		"native builder rejects template import links without mutating the show");
	for (const char *name : {"Per Look Artwork · Intermission", "Per Look Artwork · Intermission · Portrait"}) {
		OBSSourceAutoRelease source = obs_get_source_by_name(name); if (!source) source = obs_canvas_get_source_by_name(canvas, name);
		if (source) obs_source_remove(source);
	}
	for (const auto &[name, graphic] : std::vector<std::pair<QString,obs_source_t *>>{
		{"Square Graphics Trial",squareGraphic},{"Unknown Graphics Trial",unknownGraphic}}) {
		QJsonObject adaptable = separateGraphics; adaptable.insert("name",name);
		QJsonObject adaptableStage = adaptable.value("stages").toArray().first().toObject();
		adaptableStage.insert("roles",QJsonObject{{"graphic",obs_source_get_uuid(graphic)}});
		adaptable.insert("stages",QJsonArray{adaptableStage});
		check(engine.createGuidedShow(adaptable).value("ok").toBool(),
			"square or unknown-size browser graphics retain shared-source fallback for both formats");
		const auto adaptableTargets = PulseMotionEngineTestAccess::actions(engine).last().toObject().value("items").toArray();
		check(adaptableTargets.size() == 2 && adaptableTargets[0].toObject().value("sourceUuid") ==
			adaptableTargets[1].toObject().value("sourceUuid"),
			"omitting portrait assignment preserves compatible legacy fallback");
	}
	// Generic setup groups must work with role assignments unlike the saved
	// printer/presenter reference: here A is a screen and B is a camera.
	QJsonArray setupStages;
	int setupCount = 0;
	check(PulseShow::setupCatalogue().size() == 6, "flexible setup retains the six agreed stage groups");
	for (const auto &group : PulseShow::setupCatalogue()) {
		QJsonArray ids; QJsonObject frames;
		for (const auto &look : group.looks) {
			ids.append(look.id); ++setupCount;
			for (const QString &route : {QString("horizontal"), QString("vertical")})
				frames.insert(look.id + "/" + route, QJsonObject{{"corner", "bottom-right"}, {"divider", .45}, {"durationMs", 1200}});
		}
		setupStages.append(QJsonObject{{"theme", group.id}, {"looks", ids}, {"framing", frames},
			{"roles", QJsonObject{{"feedA", obs_source_get_uuid(screen)}, {"feedB", obs_source_get_uuid(camera)},
				{"feedC", obs_source_get_uuid(gameSource)}, {"graphic", uuid}}}});
	}
	QJsonObject flexible{{"schema", 2}, {"name", "Flexible Trial"}, {"stages", setupStages}, {"transition", "cut"},
		{"overlays", QJsonArray{QJsonObject{{"canvas", "horizontal"}, {"order", 1}, {"source", obs_source_get_uuid(alerts)},
			{"looks", QJsonArray{"setup-celebration/reaction", "setup-celebration/announcement"}}}}}};
	const auto flexibleResult = engine.createGuidedShow(flexible);
	check(flexibleResult.value("ok").toBool() && flexibleResult.value("looks").toInt() == setupCount,
		"all flexible Looks create with screens, cameras and games in neutral slots");
	for (const auto &value : PulseMotionEngineTestAccess::actions(engine)) {
		const auto action = value.toObject(); if (!action.value("stage").toString().startsWith("Flexible Trial · ")) continue;
		check(action.value("durationMs").toInt() == 1200, "flexible Look movement option reaches the native action");
		bool foundAlert = false;
		for (const auto &value : action.value("items").toArray()) {
			const auto item = value.toObject(), transform = item.value("transform").toObject();
			if (item.value("sourceUuid").toString() == obs_source_get_uuid(alerts) && transform.value("visible").toBool()) foundAlert = true;
			if (action.value("templateId").toString() != "setup-chat/reaction" || item.value("sourceUuid").toString() != obs_source_get_uuid(screen)) continue;
			if (item.value("container").toString().endsWith("Portrait"))
				check(std::abs(transform.value("boundsHeight").toDouble() - 640 * .45) < .01,
					"portrait panel proportion matches the selected option");
			// Native transforms snap to half pixels.
			else check(std::abs(transform.value("positionX").toDouble() - 640 * .73) <= .25 &&
				std::abs(transform.value("positionY").toDouble() - 480 * .68) <= .25,
				"landscape inset reaches the selected bottom-right corner");
		}
		check(foundAlert == action.value("stage").toString().endsWith("Celebration"),
			"an optional celebration overlay stays out of the other stage groups");
	}
	QJsonObject optional{{"schema", 2}, {"name", "Flexible No Camera"}, {"stages", QJsonArray{QJsonObject{
		{"theme", "setup-chat"}, {"looks", QJsonArray{"reaction"}}, {"roles", QJsonObject{{"feedB", uuid}, {"feedA", ""}}}}}}};
	check(engine.createGuidedShow(optional).value("ok").toBool(), "flexible content-only setup allows an omitted supporting camera");
	check(PulseMotionEngineTestAccess::actions(engine).last().toObject().value("items").toArray().size() == 2,
		"omitting support produces exactly one feed on each canvas");
	const QByteArray beforeMissingFeed = readFile(stagePath);
	optional.insert("name", "Flexible Missing Main"); optional.insert("stages", QJsonArray{QJsonObject{
		{"theme", "setup-chat"}, {"looks", QJsonArray{"reaction"}}, {"roles", QJsonObject{{"feedB", ""}}}}});
	check(!engine.createGuidedShow(optional).value("ok").toBool() && readFile(stagePath) == beforeMissingFeed,
		"a missing main feed is rejected without changing the stage catalogue");
	const auto *reaction = PulseShow::look(*PulseShow::theme("setup-chat"), "reaction");
	check(PulseShow::layout(*reaction, false, true, {{"composition", "split"}})[0].rect !=
		PulseShow::layout(*reaction, false, true, {{"composition", "corner"}})[0].rect,
		"composition choice changes the generated landscape layout");
	QJsonObject titles{{"schema", 2}, {"name", "Flexible Titles"}, {"stages", QJsonArray{QJsonObject{
		{"theme", "setup-starting"}, {"looks", QJsonArray{"countdown", "welcome"}}, {"roles", QJsonObject{{"graphic", "new:title"}}}}}},
		{"newSources", QJsonObject{{"title", QJsonObject{{"kind", "builtin_graphic"}, {"name", "Flexible title fixture"}, {"minutes", 4},
			{"textByLook", QJsonObject{{"countdown", "Starting in a moment"}, {"welcome", "Welcome everyone"}}}}}}}};
	check(engine.createGuidedShow(titles).value("ok").toBool(), "flexible Starting creates separate operating messages");
	QSet<QString> flexibleSources;
	for (const auto &value : PulseMotionEngineTestAccess::actions(engine)) {
		const auto action = value.toObject(); if (action.value("stage").toString() != "Flexible Titles · Starting") continue;
		for (const auto &value : action.value("items").toArray()) {
			const auto item = value.toObject(); if (!item.value("transform").toObject().value("visible").toBool()) continue;
			OBSSourceAutoRelease title = obs_get_source_by_uuid(item.value("sourceUuid").toString().toUtf8().constData());
			OBSDataAutoRelease settings = obs_source_get_settings(title); const QByteArray html = readFile(obs_data_get_string(settings, "local_file"));
			const bool countdown = action.value("templateId").toString().endsWith("/countdown");
			check(html.contains(countdown ? "Starting in a moment" : "Welcome everyone") && html.contains(countdown ? "textContent=true?" : "textContent=false?"),
				"Countdown runs a timer; Welcome displays its own message without one");
			if (countdown) check(html.contains("Date.now()+240*1000"), "selected countdown duration reaches both canvas assets");
			flexibleSources.insert(item.value("sourceUuid").toString());
		}
	}
	for (const auto &group : PulseShow::setupCatalogue()) for (const QString &route : {QString(""), QString(" · Portrait")}) {
		const QString name = "Flexible Trial · " + group.name + route;
		OBSSourceAutoRelease owner = route.isEmpty() ? obs_get_source_by_name(name.toUtf8().constData()) : obs_canvas_get_source_by_name(canvas, name.toUtf8().constData());
		if (owner) obs_source_remove(owner);
	}
	for (const QString &name : {QString("Flexible No Camera · Chat"), QString("Flexible Titles · Starting")}) for (const QString &route : {QString(""), QString(" · Portrait")}) {
		OBSSourceAutoRelease owner = route.isEmpty() ? obs_get_source_by_name((name + route).toUtf8().constData()) : obs_canvas_get_source_by_name(canvas, (name + route).toUtf8().constData());
		if (owner) obs_source_remove(owner);
	}
	for (const QString &id : flexibleSources) { OBSSourceAutoRelease title = obs_get_source_by_uuid(id.toUtf8().constData()); if (title) obs_source_remove(title); }
	for(const auto &theme:PulseShow::catalogue()){
		OBSSourceAutoRelease h=obs_get_source_by_name(("All Themes · "+theme.name).toUtf8().constData());
		OBSSourceAutoRelease p=obs_canvas_get_source_by_name(canvas,("All Themes · "+theme.name+" · Portrait").toUtf8().constData());obs_source_remove(h);obs_source_remove(p);
	}
	for(const QString &route:{QString("horizontal"),QString("vertical")}){OBSSourceAutoRelease treatment=obs_get_source_by_name(("All Themes · Starting · Presenter camera · "+route+" camera").toUtf8().constData());if(treatment)obs_source_remove(treatment);}
	for(const QString &name:{QString("Shared Countdown · Starting"),QString("Shared Countdown · Starting · Portrait"),
		QString("Canvas Browser Trial · Starting"),QString("Canvas Browser Trial · Starting · Portrait"),
		QString("Separate Graphics Trial · Starting"),QString("Separate Graphics Trial · Starting · Portrait"),
		QString("Square Graphics Trial · Starting"),QString("Square Graphics Trial · Starting · Portrait"),
		QString("Unknown Graphics Trial · Starting"),QString("Unknown Graphics Trial · Starting · Portrait")}){
		OBSSourceAutoRelease owner=name.endsWith(" · Portrait")?obs_canvas_get_source_by_name(canvas,name.toUtf8().constData()):obs_get_source_by_name(name.toUtf8().constData());if(owner)obs_source_remove(owner);
	}
	for(const QString &uuid:graphicSources+treatedCameras){OBSSourceAutoRelease source=obs_get_source_by_uuid(uuid.toUtf8().constData());if(source)obs_source_remove(source);}
	for(const QString &uuid:browserSources){OBSSourceAutoRelease browser=obs_get_source_by_uuid(uuid.toUtf8().constData());if(browser)obs_source_remove(browser);}
	obs_source_remove(landscapeGraphic); obs_source_remove(portraitGraphic);
	obs_source_remove(squareGraphic); obs_source_remove(unknownGraphic);
	obs_source_remove(camera);obs_source_remove(screen);obs_source_remove(gameSource);
	obs_source_remove(alerts);obs_source_remove(stickers);obs_source_remove(chat);
	obs_source_remove(generated); obs_source_remove(generatedPortrait); obs_source_remove(source); obs_canvas_remove(canvas);
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
	registerInertBrowser();
	wizardOverlayPolicyTests();
	std::fprintf(stderr, "Motion audit: storage preservation\n"); storageTests();
	std::fprintf(stderr, "Motion audit: scene isolation\n"); sceneTests(); obs_wait_for_destroy_queue();
	std::fprintf(stderr, "Motion audit: reference builder\n"); referenceBuilderTests(video); obs_wait_for_destroy_queue();
	std::fprintf(stderr, "Motion audit: setup wizard\n"); setupBuilderTests(video); obs_wait_for_destroy_queue();
	std::fprintf(stderr, "Motion audit: themed builder\n"); themedBuilderTests(video);
	obs_wait_for_destroy_queue();
	obs_shutdown();
	check(runtimeErrors == 0, "no unexpected OBS runtime errors");
	std::printf("PASS: %d motion preservation checks; no platform calls or installed settings used.\n", checks);
}
