#include "../../engine/obs-studio/plugins/pulse-weaver-core/pulse-motion-engine.hpp"
#include "../../engine/obs-studio/plugins/pulse-weaver-core/pulse-scene-item-ref.hpp"
#include "../../engine/obs-studio/plugins/pulse-weaver-core/pulse-show-templates.hpp"

#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <util/base.h>
#include <algorithm>
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
	static QString selectedStage(PulseMotionEngine &engine, const QString &stage) {
		QComboBox field;field.addItem("Unrelated old Stage","Unrelated old Stage");
		engine.stageField=&field;engine.selectEditorStage(stage);engine.populateStages();
		return field.currentData().toString();
	}
	static bool save(PulseMotionEngine &engine) { return engine.save(); }
	static bool valid(PulseMotionEngine &engine) { return engine.originalStoreValid; }
	static QJsonArray actions(PulseMotionEngine &engine) { return engine.actions; }
	static QJsonObject frame(obs_sceneitem_t *item) { return PulseMotionEngine::serialize(PulseMotionEngine::capture(item)); }
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
	for (const char *kind : {"dshow_input", "game_capture", "monitor_capture", "browser_source"}) {
		obs_source_info type{}; type.id=kind; type.type=OBS_SOURCE_TYPE_INPUT; type.output_flags=OBS_SOURCE_VIDEO;
		type.get_name=[](void *){return "Inert builder fixture";}; type.get_width=[](void *){return 640u;}; type.get_height=[](void *){return 480u;};
		type.create=[](obs_data_t *,obs_source_t *)->void *{return new int(1);}; type.destroy=[](void *p){delete static_cast<int *>(p);}; obs_register_source(&type);
	}
	for (const char *kind : {"color_filter", "mask_filter"}) {
		obs_source_info type{};type.id=kind;type.type=OBS_SOURCE_TYPE_FILTER;type.output_flags=OBS_SOURCE_VIDEO;
		type.get_name=[](void *){return "Inert filter fixture";};type.create=[](obs_data_t *,obs_source_t *)->void *{return new int(1);};type.destroy=[](void *p){delete static_cast<int *>(p);};obs_register_source(&type);
	}
	OBSSourceAutoRelease camera=obs_source_create("dshow_input","Builder camera fixture",nullptr,nullptr);
	OBSSourceAutoRelease screen=obs_source_create("monitor_capture","Builder screen fixture",nullptr,nullptr);
	OBSSourceAutoRelease gameSource=obs_source_create("game_capture","Builder game fixture",nullptr,nullptr);
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
	check(engine.createGuidedShow(countdownChoices).value("ok").toBool(),"shared countdown show creates");
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
	for(const auto &theme:PulseShow::catalogue()){
		OBSSourceAutoRelease h=obs_get_source_by_name(("All Themes · "+theme.name).toUtf8().constData());
		OBSSourceAutoRelease p=obs_canvas_get_source_by_name(canvas,("All Themes · "+theme.name+" · Portrait").toUtf8().constData());obs_source_remove(h);obs_source_remove(p);
	}
	for(const QString &route:{QString("horizontal"),QString("vertical")}){OBSSourceAutoRelease treatment=obs_get_source_by_name(("All Themes · Starting · Presenter camera · "+route+" camera").toUtf8().constData());if(treatment)obs_source_remove(treatment);}
	for(const QString &name:{QString("Shared Countdown · Starting"),QString("Shared Countdown · Starting · Portrait")}){
		OBSSourceAutoRelease owner=name.endsWith(" · Portrait")?obs_canvas_get_source_by_name(canvas,name.toUtf8().constData()):obs_get_source_by_name(name.toUtf8().constData());if(owner)obs_source_remove(owner);
	}
	for(const QString &uuid:graphicSources+treatedCameras){OBSSourceAutoRelease source=obs_get_source_by_uuid(uuid.toUtf8().constData());if(source)obs_source_remove(source);}
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
	storageTests();
	sceneTests();
	referenceBuilderTests(video);
	themedBuilderTests(video);
	obs_wait_for_destroy_queue();
	obs_shutdown();
	check(runtimeErrors == 0, "no unexpected OBS runtime errors");
	std::printf("PASS: %d motion preservation checks; no platform calls or installed settings used.\n", checks);
}
