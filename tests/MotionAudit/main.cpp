#include "../../engine/obs-studio/plugins/pulse-weaver-core/pulse-motion-engine.hpp"
#include "../../engine/obs-studio/plugins/pulse-weaver-core/pulse-scene-item-ref.hpp"
#include "../../engine/obs-studio/plugins/pulse-weaver-core/pulse-show-templates.hpp"
#include "../../engine/obs-studio/plugins/pulse-weaver-core/pulse-setup-flexible.hpp"
#include "../../engine/obs-studio/plugins/pulse-weaver-core/pulse-setup-plan.hpp"
#include "../../engine/obs-studio/plugins/pulse-weaver-core/pulse-setup-cards.hpp"
#include "../../engine/obs-studio/plugins/pulse-weaver-core/pulse-setup-select.hpp"

#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QMessageBox>
#include <QImage>
#include <QPainter>
#include <QFileInfo>
#include <QDirIterator>
#include <QCryptographicHash>
#include <QEventLoop>
#include <QScrollBar>
#include <QWizardPage>
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
static std::atomic<int> imageFixtureCreates{0};

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
	// Audio is advertised but never emitted. This makes mixer-mask assertions
	// meaningful: OBS ignores those setters/getters for video-only fixtures.
	obs_source_info type{}; type.id = "browser_source"; type.type = OBS_SOURCE_TYPE_INPUT; type.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_AUDIO;
	type.get_name = [](void *) { return "Inert browser fixture"; };
	type.create = [](obs_data_t *settings, obs_source_t *) -> void * {
		return new QSize(int(obs_data_get_int(settings, "width")), int(obs_data_get_int(settings, "height")));
	};
	type.destroy = [](void *p) { delete static_cast<QSize *>(p); };
	type.get_width = [](void *p) { return uint32_t(static_cast<QSize *>(p)->width()); };
	type.get_height = [](void *p) { return uint32_t(static_cast<QSize *>(p)->height()); };
	obs_register_source(&type);
}

static void registerInertImageAndCaptureInputs()
{
	obs_source_info image{}; image.id = "image_source"; image.type = OBS_SOURCE_TYPE_INPUT; image.output_flags = OBS_SOURCE_VIDEO;
	image.get_name = [](void *) { return "Inert PNG image fixture"; };
	image.create = [](obs_data_t *settings, obs_source_t *) -> void * {
		++imageFixtureCreates;
		const QImage pixels(QString::fromUtf8(obs_data_get_string(settings, "file")));
		return new QSize(pixels.isNull() ? QSize(640, 480) : pixels.size());
	};
	image.update = [](void *opaque, obs_data_t *settings) {
		const QImage pixels(QString::fromUtf8(obs_data_get_string(settings, "file")));
		*static_cast<QSize *>(opaque) = pixels.isNull() ? QSize(640, 480) : pixels.size();
	};
	image.destroy = [](void *opaque) { delete static_cast<QSize *>(opaque); };
	image.get_width = [](void *opaque) { return uint32_t(static_cast<QSize *>(opaque)->width()); };
	image.get_height = [](void *opaque) { return uint32_t(static_cast<QSize *>(opaque)->height()); };
	obs_register_source(&image);
	for (const char *kind : {"dshow_input", "game_capture", "monitor_capture", "window_capture", "ffmpeg_source"}) {
		obs_source_info type{}; type.id = kind; type.type = OBS_SOURCE_TYPE_INPUT; type.output_flags = OBS_SOURCE_VIDEO;
		type.get_name = [](void *) { return "Inert capture fixture"; };
		type.create = [](obs_data_t *settings, obs_source_t *) -> void * {
			const int width = int(obs_data_get_int(settings, "width")), height = int(obs_data_get_int(settings, "height"));
			return new QSize(width > 0 ? width : 640, height > 0 ? height : 480);
		};
		type.destroy = [](void *opaque) { delete static_cast<QSize *>(opaque); };
		type.get_width = [](void *opaque) { return uint32_t(static_cast<QSize *>(opaque)->width()); };
		type.get_height = [](void *opaque) { return uint32_t(static_cast<QSize *>(opaque)->height()); };
		obs_register_source(&type);
	}
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
	static QJsonObject canonicalTransform(const QJsonObject &value) {return PulseMotionEngine::serialize(PulseMotionEngine::deserialize(value));}
	static QJsonObject visualTransform(const QJsonObject &value, uint32_t width, uint32_t height) {
		return PulseMotionEngine::serialize(PulseMotionEngine::visualTransform(PulseMotionEngine::deserialize(value), width, height));
	}
	static QJsonObject reflowFrame(obs_sceneitem_t *item, const QJsonObject &from, const QJsonObject &to, double progress, float width, float height) {
		PulseMotionEngine::Track track; track.item = item;
		track.sourceUuid = QString::fromUtf8(obs_source_get_uuid(obs_sceneitem_get_source(item)));
		track.from = PulseMotionEngine::deserialize(from); track.target = PulseMotionEngine::deserialize(to);
		return PulseMotionEngine::serialize(PulseMotionEngine::reflowFrame(track, progress, width, height));
	}
	static void applyTransform(obs_sceneitem_t *item, const QJsonObject &value, bool final = true) {
		PulseMotionEngine::apply(item, PulseMotionEngine::deserialize(value), final);
	}
	static void movementFrame(obs_sceneitem_t *item, const QJsonObject &from, const QJsonObject &to, double progress, bool reflow) {
		PulseMotionEngine::Execution execution; execution.action.insert("kind", "layout");
		if (!reflow) execution.action.insert("motionStyle", "legacy");
		PulseMotionEngine::Track track; track.item = item;
		track.sourceUuid = QString::fromUtf8(obs_source_get_uuid(obs_sceneitem_get_source(item)));
		track.from = PulseMotionEngine::deserialize(from); track.target = PulseMotionEngine::deserialize(to);
		execution.tracks.push_back(track);
		PulseMotionEngine::apply(item, track.from, true);
		obs_sceneitem_set_visible(item, track.from.visible || track.target.visible);
		PulseMotionEngine::applyMovementFrame(execution, progress);
	}
	static QString editorStyle(PulseMotionEngine &engine, const QString &style) {
		engine.editingId = "reflow-editor-fixture";
		engine.actions = QJsonArray{QJsonObject{{"id",engine.editingId},{"motionStyle",style}}};
		QComboBox kind; kind.addItem("Layout","layout");engine.kindField=&kind;
		const QString result=engine.editorAction().value("motionStyle").toString();engine.kindField=nullptr;return result;
	}
	static int reflowCoverageFrame(PulseMotionEngine &engine, const std::vector<obs_sceneitem_t *> &items,
		const QJsonArray &from, const QJsonArray &to, double progress) {
		PulseMotionEngine::Execution execution;execution.action.insert("kind","layout");
		for(size_t i=0;i<items.size();++i) {
			PulseMotionEngine::Track track;track.item=items[i];track.container="coverage";track.graphic=i==items.size()-1;
			track.sourceUuid=QString::fromUtf8(obs_source_get_uuid(obs_sceneitem_get_source(items[i])));
			track.from=PulseMotionEngine::deserialize(from[int(i)].toObject());track.target=PulseMotionEngine::deserialize(to[int(i)].toObject());
			PulseMotionEngine::apply(items[i],track.from,true);
			if(!track.graphic || track.from.visible==track.target.visible)
				obs_sceneitem_set_visible(items[i],track.from.visible||track.target.visible);
			execution.tracks.push_back(track);
		}
		engine.prepareCoveragePairs(execution);PulseMotionEngine::applyMovementFrame(execution,progress);
		for(auto *item:items)obs_sceneitem_force_update_transform(item);
		return int(execution.coveragePairs.size());
	}
	static void openWizard(PulseMotionEngine &engine) { engine.openShowWizard(); }
	static void openDetailedWizard(PulseMotionEngine &engine) { engine.openDetailedShowWizard(); }
	static QString selectedStage(PulseMotionEngine &engine, const QString &stage) {
		QComboBox field;field.addItem("Unrelated old Stage","Unrelated old Stage");
		engine.stageField=&field;engine.selectEditorStage(stage);engine.populateStages();
		return field.currentData().toString();
	}
	static bool save(PulseMotionEngine &engine) { return engine.save(); }
	static bool valid(PulseMotionEngine &engine) { return engine.originalStoreValid; }
	static QJsonArray actions(PulseMotionEngine &engine) { return engine.actions; }
	static void settleLayout(PulseMotionEngine &engine, const QJsonObject &action) {
		PulseMotionEngine::Execution execution; execution.action=action; QString error;
		// The headless harness has no frontend Stage switcher. Keep the real
		// saved item/scene resolution and final movement path under test.
		execution.action.insert("policy","current"); execution.action.remove("collection");
		check(engine.prepareExecution(execution,error),"generated Look prepares through the real runtime");
		for(auto &track:execution.tracks) engine.apply(track.item,track.target,true);
		for(const auto &track:execution.tracks) if(track.target.visible)
			check(engine.capture(track.item).order==track.target.order,"settled visible layers retain their requested stack after hidden layers are applied");
	}
	static QJsonObject frame(obs_sceneitem_t *item) { return PulseMotionEngine::serialize(PulseMotionEngine::capture(item)); }
	static bool graphic(PulseMotionEngine &engine, const QJsonObject &action) {
		PulseMotionEngine::Execution execution; execution.action = action;
		QString error;
		check(engine.prepareExecution(execution, error) && !execution.tracks.empty(), "prepare policy fixture");
		return execution.tracks.front().graphic;
	}
	static void movement(PulseMotionEngine &engine, const QJsonObject &from, const QJsonObject &to)
	{
		const auto actionsBefore=engine.actions;const auto editingBefore=engine.editingId;
		auto legacy=to;legacy.insert("motionStyle","legacy");engine.editingId=to.value("id").toString();
		for(int i=0;i<engine.actions.size();++i) if(engine.actions[i].toObject().value("id")==to.value("id"))engine.actions[i]=legacy;
		const QString container = to.value("container").toString();
		OBSSceneAutoRelease scene = obs_scene_create_private("Inert movement preview");
		PulseMotionEngine::Execution execution;
		execution.action=legacy;
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
		engine.actions=actionsBefore;engine.editingId=editingBefore;
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
	{
		OBSCanvasAutoRelease hudCanvas = obs_canvas_create("Pulse Weaver HUD", nullptr, SCENE_REF);
		OBSSceneAutoRelease hud = obs_canvas_scene_create(hudCanvas, "HUD");
		bool listed = false;
		for (const auto &entry : engine.sceneCatalogue()) if (entry.toObject().value("name") == "HUD") listed = true;
		check(!listed, "the independent HUD is absent from stage and Look container choices");
		check(engine.itemCatalogue("HUD").isEmpty(), "broadcast Look actions cannot resolve the HUD container");
		obs_source_remove(obs_scene_get_source(hud));
		obs_canvas_remove(hudCanvas);
	}
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
		setup.setSharedLayersEnabled(true);
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

static QJsonArray wizardRenderEvidence;
static int wizardGeometryFailures = 0;
class WizardCaptureWindows final : public QObject {
protected:
	bool eventFilter(QObject *object, QEvent *event) override {
		if (event->type() == QEvent::Polish)
			if (auto *widget = qobject_cast<QWidget *>(object); widget && widget->isWindow())
				widget->setAttribute(Qt::WA_DontShowOnScreen);
		return false;
	}
};
static void settleWizard()
{
	QApplication::processEvents();
	QEventLoop loop; QTimer::singleShot(0, &loop, &QEventLoop::quit); loop.exec();
	QApplication::processEvents();
}
static QJsonArray wizardRect(QWidget *widget, QWidget *window)
{
	const QPoint origin = widget->mapTo(window, QPoint());
	return {origin.x(), origin.y(), widget->width(), widget->height()};
}
static QScrollArea *wizardScrollAncestor(QWidget *widget, QWidget *window)
{
	for (auto *parent = widget->parentWidget(); parent && parent != window; parent = parent->parentWidget())
		if (auto *scroll = qobject_cast<QScrollArea *>(parent)) return scroll;
	return nullptr;
}
static void captureWizardWindow(QWidget *window, const QString &stem)
{
	const QSize previous = window->size();
	const QString directory = qEnvironmentVariable("PULSE_WIZARD_EVIDENCE_DIR");
	if (!directory.isEmpty()) check(QDir().mkpath(directory), "create synthetic wizard evidence directory");
	for (const QSize size : {QSize(1100, 800), QSize(780, 620), QSize(740, 560)}) {
		window->resize(size); settleWizard();
		for (auto *scroll : window->findChildren<QScrollArea *>()) if (scroll->isVisibleTo(window)) scroll->verticalScrollBar()->setValue(scroll->verticalScrollBar()->minimum());
		settleWizard();
		QJsonArray chrome, cards, scrolls, utilities, primaryColors; bool chromeContained = true, cardsFit = true, primaryReady = true; int offScroll = 0;
		for (auto *button : window->findChildren<QPushButton *>()) {
			if (!button->isVisibleTo(window) || button->size().isEmpty()) continue;
			if (qEnvironmentVariableIsSet("PULSE_WIZARD_TEST_QSS") && button->property("setupPrimary").toBool() && button->isEnabled()) {
				const QString foreground = button->palette().color(QPalette::ButtonText).name();
				const bool readable = foreground == "#061d2a";
				primaryReady = primaryReady && readable;
				primaryColors.append(QJsonObject{{"label", button->text()}, {"foreground", foreground}, {"themedPrimaryForeground", readable}});
			}
			const QRect bounds(button->mapTo(window, QPoint()), button->size());
			if (button->objectName() == "PulseWeaverSetupChoiceCard") {
				auto *scroll = wizardScrollAncestor(button, window);
				const QRect viewport = scroll ? QRect(scroll->viewport()->mapTo(window, QPoint()), scroll->viewport()->size()) : window->rect();
				const bool horizontal = bounds.left() >= viewport.left() && bounds.right() <= viewport.right();
				const bool textFits = !button->hasHeightForWidth() || button->height() >= button->heightForWidth(button->width());
				const bool offscreen = bounds.top() < viewport.top() || bounds.bottom() > viewport.bottom();
				if (offscreen) ++offScroll;
				cardsFit = cardsFit && horizontal && textFits;
				cards.append(QJsonObject{{"label", button->accessibleName()}, {"rect", wizardRect(button, window)},
					{"viewport", QJsonArray{viewport.x(), viewport.y(), viewport.width(), viewport.height()}},
					{"parentRect", wizardRect(button->parentWidget(), window)},
					{"parentMinimumWidth", button->parentWidget()->minimumWidth()},
					{"parentMinimumHintWidth", button->parentWidget()->minimumSizeHint().width()},
					{"horizontalContained", horizontal}, {"textHeightFits", textFits}, {"offScrollContent", offscreen}, {"checked", button->isChecked()}});
			} else if (!wizardScrollAncestor(button, window)) {
				const bool contained = window->rect().contains(bounds);
				chromeContained = chromeContained && contained;
				chrome.append(QJsonObject{{"label", button->text()}, {"rect", wizardRect(button, window)}, {"contained", contained}});
			}
		}
		for (auto *label : window->findChildren<QLabel *>()) {
			if (!label->isVisibleTo(window) || label->size().isEmpty() || wizardScrollAncestor(label, window)) continue;
			const QRect bounds(label->mapTo(window, QPoint()), label->size());
			const bool contained = window->rect().contains(bounds);
			const bool textFits = label->text().isEmpty() || (label->wordWrap() ?
				label->height() >= label->heightForWidth(label->width()) : label->fontMetrics().horizontalAdvance(label->text()) <= label->contentsRect().width());
			chromeContained = chromeContained && contained && textFits;
			chrome.append(QJsonObject{{"label", label->text()}, {"rect", wizardRect(label, window)}, {"contained", contained}, {"textHeightFits", textFits}});
		}
		for (auto *scroll : window->findChildren<QScrollArea *>()) {
			if (!scroll->isVisibleTo(window) || !scroll->widget()) continue;
			const bool horizontal = scroll->widget()->width() <= scroll->viewport()->width() && scroll->horizontalScrollBar()->maximum() == 0;
			cardsFit = cardsFit && horizontal;
			scrolls.append(QJsonObject{{"viewport", wizardRect(scroll->viewport(), window)}, {"body", wizardRect(scroll->widget(), window)},
				{"horizontalContained", horizontal}, {"verticalScrollMaximum", scroll->verticalScrollBar()->maximum()}});
		}
		if (stem == "wizard-4-review" || stem == "guided-5-review") {
			const QStringList titles = stem == "guided-5-review" ? QStringList{"Change inputs…","Change screens…","Change alert layers…","Change included views…","Check assignments again"} : QStringList{"Edit sections","Edit Looks","Edit sources","Check again"};
			for (const QString &title : titles) {
				QPushButton *button = nullptr;
				for (auto *candidate : window->findChildren<QPushButton *>()) if (candidate->text() == title) { button = candidate; break; }
				bool reachable = button && button->isVisibleTo(window);
				if (button) {
					auto *scroll = wizardScrollAncestor(button, window);
					auto *body = scroll ? scroll->widget() : window;
					const QRect inBody(button->mapTo(body, QPoint()), button->size());
					reachable = reachable && body->rect().contains(inBody);
					for (auto *parent = button->parentWidget(); parent && parent != body; parent = parent->parentWidget()) {
						const QRect inParent(button->mapTo(parent, QPoint()), button->size());
						reachable = reachable && parent->contentsRect().contains(inParent);
					}
					utilities.append(QJsonObject{{"label", title}, {"rect", wizardRect(button, window)},
						{"visible", button->isVisibleTo(window)}, {"reachableInScrollExtent", reachable}});
				} else utilities.append(QJsonObject{{"label", title}, {"missing", true}});
				cardsFit = cardsFit && reachable;
			}
		}
		QJsonObject report{{"capture", stem}, {"requestedSize", QJsonArray{size.width(), size.height()}},
			{"actualSize", QJsonArray{window->width(), window->height()}}, {"chrome", chrome}, {"cards", cards}, {"scrolls", scrolls}, {"reviewUtilities", utilities}, {"primaryButtonColors", primaryColors}, {"themedPrimaryReady", primaryReady},
			{"offScrollCards", offScroll}, {"chromeContained", chromeContained}, {"cardsHorizontallyContained", cardsFit},
			{"platform", QGuiApplication::platformName()}, {"theme", qEnvironmentVariableIsSet("PULSE_WIZARD_TEST_QSS") ? "supplied stylesheet; origin recorded separately; exact frontend theme reviewed separately" : "platform-default; exact frontend theme reviewed separately"}};
		wizardRenderEvidence.append(report);
		if (!chromeContained || !cardsFit || !primaryReady || window->size() != size)
			std::fprintf(stderr, "Wizard render diagnostics: %s\n", QJsonDocument(report).toJson(QJsonDocument::Compact).constData());
		if (!directory.isEmpty()) {
			const QString prefix = directory + "/" + stem + QString("-%1x%2").arg(size.width()).arg(size.height());
			check(window->grab().save(prefix + "-top.png"), "save actual synthetic Qt wizard capture");
			for (auto *scroll : window->findChildren<QScrollArea *>()) {
				if (!scroll->isVisibleTo(window) || !scroll->verticalScrollBar()->maximum()) continue;
				const int before = scroll->verticalScrollBar()->value();
				scroll->verticalScrollBar()->setValue(scroll->verticalScrollBar()->maximum()); settleWizard();
				check(window->grab().save(prefix + "-bottom.png"), "save reachable lower wizard content");
				scroll->verticalScrollBar()->setValue(before); settleWizard(); break;
			}
			writeFile(QDir(directory).filePath("wizard-layout-state-evidence.partial.json"), QJsonDocument(wizardRenderEvidence).toJson());
		}
		// Collect all page failures in one run, while still rejecting the run.
		// A clipped options card must not hide a separate Sources/Review defect.
		if (!chromeContained || !cardsFit || !primaryReady || window->size() != size) ++wizardGeometryFailures;
	}
	window->resize(previous); settleWizard();
}
static QSet<QString> wizardSourceIdentities()
{
	QSet<QString> identities;
	obs_enum_sources([](void *opaque, obs_source_t *source) {
		static_cast<QSet<QString> *>(opaque)->insert(QString::fromUtf8(obs_source_get_uuid(source))); return true;
	}, &identities);
	obs_enum_canvases([](void *opaque, obs_canvas_t *canvas) {
		obs_canvas_enum_scenes(canvas, [](void *data, obs_source_t *source) {
			static_cast<QSet<QString> *>(data)->insert(QString::fromUtf8(obs_source_get_uuid(source))); return true;
		}, opaque);
		return true;
	}, &identities);
	return identities;
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
		setup.problem(true).contains("landscape artwork", Qt::CaseInsensitive), "new wizard artwork starts unassigned and requires an explicit overlay choice");
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
	check(setup.problem(true).contains("portrait artwork", Qt::CaseInsensitive), "wizard asks for required portrait artwork when that answer is empty");
	vertical->setCurrentIndex(vertical->findData(obs_source_get_uuid(portrait)));
	horizontal->setCurrentIndex(horizontal->findData(""));
	check(setup.problem(true).contains("landscape artwork", Qt::CaseInsensitive), "wizard asks for required landscape artwork when that answer is empty");
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
	const QString landscapeDraft = horizontal->currentData().toString();
	check(vertical->findData(landscapeDraft) < 0 && combo("Portrait overlay 1")->findData(landscapeDraft) < 0 &&
		combo("Landscape overlay 1")->findData(landscapeDraft) >= 0,
		"landscape artwork drafts stay reusable on landscape without appearing in portrait artwork lists");
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
	setup.setSharedLayersEnabled(true);
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
	setup.update({"Chat"});
	auto *initialNavigator = setupField<QComboBox>(&sourcePage, "Source to assign");
	const int feedQuestions = initialNavigator->count();
	check(!setup.sharedLayersEnabled() && feedQuestions == 1, "simple Chat setup starts with one assignment and no shared-layer questions");
	auto *dormantLayer = setupField<QComboBox>(&sourcePage, "Landscape overlay 1");
	auto *dormantUrl = setupField<QLineEdit>(&sourcePage, "Landscape overlay 1 browser URL");
	dormantLayer->setCurrentIndex(dormantLayer->findData("url"));
	dormantUrl->setText("https://overlays.lumiastream.com/audit/optional-shared-layer");
	check(setup.draft("Deferred extras").value("overlays").toArray().isEmpty() &&
		setup.draft("Deferred extras").value("newSources").toObject().isEmpty(), "dormant layer answers do not leak sources into a deferred build");
	setup.setSharedLayersEnabled(true);
	check(initialNavigator->count() == feedQuestions + 6 && setup.draft("Enabled extras").value("overlays").toArray().size() == 1,
		"opting into shared layers exposes six optional questions and the chosen assignment");
	setup.setSharedLayersEnabled(false);
	check(initialNavigator->count() == feedQuestions && setup.draft("Deferred again").value("overlays").toArray().isEmpty() &&
		setup.draft("Deferred again").value("newSources").toObject().isEmpty(), "deferring extras again removes unused browser definitions and questions");
	setup.setSharedLayersEnabled(true);
	check(dormantLayer->currentData() == "url" && dormantUrl->text().endsWith("optional-shared-layer") &&
		setup.draft("Restored extras").value("overlays").toArray().size() == 1, "re-enabling extras restores the user's dormant answer");
	setup.setSharedLayersEnabled(false);
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
	auto *preview = setupField<QComboBox>(&lookPage, "Look layout to preview");
	for (int i = 0; i < setup.tabs->count(); ++i) if (setup.tabs->tabText(i) == "Projects") setup.tabs->setCurrentIndex(i);
	check(preview->currentData() == "setup-projects/activity", "switching stage tabs updates the diagram to that stage's selected Look");
	preview->setCurrentIndex(preview->findData("setup-chat/talk"));
	check(setup.tabs->tabText(setup.tabs->currentIndex()) == "Chat", "choosing a Look to preview selects its matching options tab");
	QCheckBox *gameplay = nullptr; for (auto *box : lookPage.findChildren<QCheckBox *>()) if (box->text() == "Gameplay") gameplay = box;
	check(gameplay, "gameplay selection is available");
	for (int i = 0; i < setup.tabs->count(); ++i) if (setup.tabs->tabText(i) == "Game") setup.tabs->setCurrentIndex(i);
	gameplay->setChecked(false);
	check(preview->currentIndex() == -1 && setup.tabs->tabText(setup.tabs->currentIndex()) == "Game" && setup.problem(false).contains("Game"),
		"an empty stage stays selected so the user can correct it without an unrelated diagram");
	gameplay->setChecked(true);
	int gameQuestion = -1;
	for (int index = 0; index < jump->count(); ++index)
		if (jump->itemText(index).startsWith("Game ·") && jump->itemText(index).contains("(Feed B)")) gameQuestion = index;
	check(gameQuestion >= 0, "source navigator explains the job of each stage assignment");
	jump->setCurrentIndex(gameQuestion);
	const QVariant gameQuestionIdentity = jump->currentData();
	check(preview->currentData() == "setup-game/play", "source navigation reviews the Look that uses the requested feed");
	setup.update({"Chat", "Game"});
	check(jump->currentData() == gameQuestionIdentity, "changing groups preserves the current question by identity");
	auto *attention = setupField<QPushButton>(&sourcePage, "Next source needing attention");
	attention->click(); check(jump->currentText().startsWith("Chat ·") && jump->currentText().contains("(Feed A)"), "attention navigation wraps to an unfinished required source");
	QPushButton *nextSource = nullptr;
	for (auto *button : sourcePage.findChildren<QPushButton *>()) if (button->text() == "Next source") nextSource = button;
	check(nextSource && !nextSource->isEnabled(), "source clickthrough asks for a required assignment before advancing");
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
	check(nextSource->isEnabled(), "a valid created source enables the next clickthrough step");
	check(sourceKey.startsWith("new:") && gameFeed->findData(sourceKey) >= 0, "new source is reusable across stage groups before creation");
	check(setupField<QComboBox>(&sourcePage, "Starting · Countdown · Landscape artwork")->findData(sourceKey) < 0 &&
		setupField<QComboBox>(&sourcePage, "Landscape overlay 1")->findData(sourceKey) < 0,
		"drafted image feeds do not become selectable artwork or shared Lumia layers");
	OBSSourceAutoRelease uncreated = obs_get_source_by_name("Wizard draft image"); check(!uncreated, "drafting a source does not create or activate a native input");
	QTimer::singleShot(0, [&] {
		auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget()); check(dialog, "source card chooser opens");
		setupField<QPushButton>(dialog, "New: Wizard draft image")->click();
	});
	setupField<QPushButton>(&sourcePage, "Game · Feed B use existing source")->click();
	check(!attention->isEnabled() && setupField<QLabel>(&sourcePage, "Source assignment progress")->text().contains("2 of 2"),
		"optional empty feeds and overlays do not prevent required-source completion");
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
	check(QFile::rename(editedPath, editedPath + ".moved"), "temporarily move the staged file fixture");
	check(setup.problem(true).contains("readable file"), "a staged file disappearing before Finish is reported before creating the show");
	check(QFile::rename(editedPath + ".moved", editedPath), "restore the staged file fixture");
	QTimer::singleShot(0, [&] { auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget()); check(dialog, "cancel source dialog opens"); dialog->reject(); });
	create->click(); check(setup.draft("New Source Trial").value("newSources").toObject().size() == 1, "cancelled source addition leaves the plan unchanged");
	check(setup.problem(true).isEmpty(), "new image sources satisfy required stage feeds");

	auto *addStinger = setupField<QPushButton>(&lookPage, "Chat horizontal add stinger");
	QTimer::singleShot(0, [&] {
		auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget()); check(dialog, "add stinger dialog opens inside the wizard");
		setupField<QLineEdit>(dialog, "New stinger name")->setText("Wizard staged stinger");
		setupField<QLineEdit>(dialog, "New stinger video file")->setText(imagePath);
		check(!dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Save)->isEnabled(), "the stinger dialog rejects an image before saving a draft");
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

static void setupWizardRoundTripTests(obs_source_t *landscape, obs_source_t *portrait)
{
	QTemporaryDir files;
	const QString storage = files.filePath("config/obs-studio/plugin_config/pulse-weaver-core/motion.json");
	check(QDir().mkpath(QFileInfo(storage).absolutePath()), "private full-wizard storage directory");
	writeFile(storage, QJsonDocument(QJsonObject{{"version", 1}, {"actions", QJsonArray{}},
		{"originals", QJsonArray{}}, {"originalScenes", QJsonObject{}}, {"auditSentinel", "unchanged"}}).toJson());
	const QByteArray beforeStorage = readFile(storage);
	const auto beforeSources = wizardSourceIdentities();
	PulseMotionEngine engine(nullptr, storage);
	bool completed = false;
	QTimer::singleShot(0, [&] {
		auto *wizard = qobject_cast<QWizard *>(QApplication::activeModalWidget());
		check(wizard && wizard->objectName() == "PulseWeaverSetupWizard", "production five-page wizard opens in isolated test");
		settleWizard();
		check(wizard->pageIds() == QList<int>{0,1,2,3,4} && wizard->currentId() == 0, "wizard has distinct Start, Sections, Looks, Sources and Review pages");
		check(!wizard->button(QWizard::CustomButton1)->isVisibleTo(wizard), "Return to review is absent before the first review");
		auto *start = setupField<QComboBox>(wizard, "Show starting point");
		auto *sections = wizard->page(1), *looks = wizard->page(2), *sources = wizard->page(3), *review = wizard->page(4);
		QMap<QString, QCheckBox *> groups;
		for (auto *box : sections->findChildren<QCheckBox *>())
			for (const auto &group : PulseShow::setupCatalogue()) if (box->text() == group.name) groups.insert(group.name, box);
		auto selected = [&] { QSet<QString> result; for (auto it = groups.cbegin(); it != groups.cend(); ++it) if (it.value()->isChecked()) result.insert(it.key()); return result; };
		const QSet<QString> full{"Starting","Chat","Game","Celebration","Intermission"};
		check(groups.size() == 6 && selected() == full && start->currentData() == "full", "fresh full-show preset selects exactly the five advertised sections");
		captureWizardWindow(wizard, "wizard-0-start");
		for (const auto &preset : {std::pair{"game","Game"}, {"chat","Chat"}}) {
			start->setCurrentIndex(start->findData(preset.first));
			check(selected() == QSet<QString>{preset.second}, "simple preset selects only its advertised section");
			for (auto it = groups.cbegin(); it != groups.cend(); ++it)
				check(setupField<QPushButton>(sections, it.key())->isChecked() == it.value()->isChecked(), "preset visibly synchronizes every section card with its model");
		}
		setupField<QPushButton>(wizard, "Full show starting point")->click();
		check(selected() == full && start->currentData() == "full", "visible Full show card and section models stay synchronized");
		for (auto it = groups.cbegin(); it != groups.cend(); ++it)
			check(setupField<QPushButton>(sections, it.key())->isChecked() == it.value()->isChecked(), "restoring Full show also updates the visible section cards");
		wizard->next(); settleWizard(); check(wizard->currentId() == 1, "Start advances to Sections");
		groups["Projects"]->setChecked(true);
		check(start->currentIndex() < 0 && !setupField<QPushButton>(wizard, "Full show starting point")->isChecked(), "custom section edits clear the preset's checked claim");
		for (auto *box : groups) box->setChecked(false);
		check(!wizard->currentPage()->isComplete() && !wizard->button(QWizard::NextButton)->isEnabled(), "empty section selection blocks continuation");
		groups["Starting"]->setChecked(true);
		auto *name = setupField<QLineEdit>(wizard, "Show name"); name->clear();
		check(!wizard->currentPage()->isComplete(), "blank show name is rejected locally");
		name->setText("Cancelled wizard audit");
		check(wizard->currentPage()->isComplete(), "repairing the name restores section readiness");
		captureWizardWindow(wizard, "wizard-1-sections");
		wizard->next(); settleWizard(); check(wizard->currentId() == 2, "Sections advance to Looks");
		auto *welcome = setupField<QCheckBox>(looks, "Starting · Welcome include");
		check(!welcome->isChecked(), "alternate Look starts excluded");
		QTimer::singleShot(0, [&] {
			auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
			check(dialog && dialog->objectName() == "PulseWeaverLookPreviewDialog", "Preview opens a focused read-only Look diagram");
			check(setupField<QWidget>(dialog, "Landscape and portrait layout diagram"), "Preview contains the real schematic widget");
			captureWizardWindow(dialog, "wizard-look-preview-excluded"); dialog->reject();
		});
		setupField<QPushButton>(looks, "Starting · Welcome preview")->click();
		check(!welcome->isChecked(), "previewing an excluded Look never includes it");
		QTimer::singleShot(0, [&] {
			auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
			check(dialog && dialog->objectName() == "PulseWeaverLookPreviewDialog", "selected Look Preview uses the same focused surface");
			captureWizardWindow(dialog, "wizard-look-preview-included"); dialog->reject();
		});
		setupField<QPushButton>(looks, "Starting · Countdown preview")->click();
		captureWizardWindow(wizard, "wizard-2-looks");
		QTimer::singleShot(0, [&] {
			auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
			check(dialog && dialog->objectName() == "PulseWeaverStageTransitionsDialog", "stage transitions open in their focused production dialog");
			auto *formats = dialog->findChild<QTabWidget *>("PulseSetupTransitionFormatTabs");
			check(formats && formats->count() == 2, "transitions expose separate landscape and portrait format tabs");
			auto *horizontal = setupField<QComboBox>(dialog, "Starting horizontal stage transition");
			auto *vertical = setupField<QComboBox>(dialog, "Starting vertical stage transition");
			horizontal->setCurrentIndex(horizontal->findData("cut"));
			for (int index = 0; index < formats->count(); ++index) {
				formats->setCurrentIndex(index); settleWizard();
				const QString route = index ? "vertical" : "horizontal";
				check(formats->currentWidget()->property("pulseCanvas").toString() == route && formats->currentWidget()->isVisibleTo(dialog), "each transition format page is reachable and identifies its canvas");
				bool visibleChoice = false;
				for (auto *button : formats->currentWidget()->findChildren<QPushButton *>("PulseWeaverSetupChoiceCard"))
					visibleChoice = visibleChoice || (button->isVisibleTo(dialog) && button->accessibleName().contains("Starting " + route + " stage transition"));
				check(visibleChoice, "the active transition tab shows its actual canvas choice controls");
				captureWizardWindow(dialog, "wizard-stage-transitions-" + route);
			}
			check(horizontal->currentData() == "cut" && vertical->currentData().toString().isEmpty(), "switching format tabs preserves independent transition selections");
			dialog->reject();
		});
		setupField<QPushButton>(looks, "Starting stage transition options")->click();
		QTimer::singleShot(0, [&] {
			auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
			check(dialog && dialog->objectName() == "PulseWeaverLookConfigureDialog", "Look configuration opens as a focused production dialog");
			check(dialog->findChild<QWidget *>("PulseSetupConfigureDiagram"), "focused Look options retain their schematic");
			auto *composition = setupField<QComboBox>(dialog, "Starting · Countdown landscape composition");
			composition->setCurrentIndex(composition->findData("rail"));
			captureWizardWindow(dialog, "wizard-look-configure"); dialog->reject();
		});
		setupField<QPushButton>(looks, "Starting · Countdown layout options")->click();
		check(setupField<QComboBox>(looks, "Starting · Countdown landscape composition")->currentData() == "rail", "leaving focused options preserves the chosen layout in the draft");
		wizard->next(); settleWizard(); check(wizard->currentId() == 3, "Looks advance to Sources");
		auto *jump = setupField<QComboBox>(sources, "Source to assign");
		auto *extra = sources->findChild<QCheckBox *>("PulseSetupSharedLayersToggle");
		check(extra && !extra->isChecked(), "optional shared layers start deferred");
		for (int index = 0; index < jump->count(); ++index) check(!jump->itemText(index).contains("overlay "), "deferred extras add no mandatory-looking source questions");
		const QString horizontalLabel = "Starting · Countdown · Landscape artwork", verticalLabel = "Starting · Countdown · Portrait artwork";
		auto *horizontal = setupField<QComboBox>(sources, horizontalLabel), *vertical = setupField<QComboBox>(sources, verticalLabel);
		check(horizontal->findData(obs_source_get_uuid(landscape)) >= 0 && vertical->findData(obs_source_get_uuid(portrait)) >= 0,
			"production source assignment offers the matching inert artwork for each canvas");
		auto *url = setupField<QLineEdit>(sources, horizontalLabel + " browser URL");
		jump->setCurrentIndex(jump->findText(horizontalLabel)); horizontal->setCurrentIndex(horizontal->findData("url"));
		auto *progress = setupField<QLabel>(sources, "Source assignment progress");
		auto *selectedSource = horizontal->parentWidget()->findChild<QLabel *>("PulseSetupSelectedSource");
		auto *nextSource = static_cast<QPushButton *>(nullptr);
		for (auto *button : sources->findChildren<QPushButton *>()) if (button->text() == "Next source") nextSource = button;
		check(nextSource && !nextSource->isEnabled() && selectedSource && selectedSource->text().contains("Enter a browser link"), "empty link mode requests an answer without claiming it was pasted or ready");
		check(!wizard->currentPage()->isComplete(), "unassigned source questions block Review");
		wizard->next(); check(wizard->currentId() == 3, "programmatic Next cannot bypass incomplete source validation");
		captureWizardWindow(wizard, "wizard-3-sources-needs-answer");
		url->setText("https://overlays.lumiastream.com/audit/cancelled-landscape");
		check(nextSource->isEnabled() && progress->text().contains("1 of 2") && selectedSource->text().contains("not tested"), "typing a valid link repairs Next and progress without toggling source mode");
		vertical->setCurrentIndex(vertical->findData(obs_source_get_uuid(portrait)));
		check(wizard->currentPage()->isComplete() && progress->text().contains("2 of 2"), "independent portrait assignment completes required sources");
		url->setText("not a browser URL");
		check(!wizard->currentPage()->isComplete() && !nextSource->isEnabled() && progress->text().contains("1 of 2"), "editing an accepted answer to invalid text immediately revokes readiness");
		url->setText("https://overlays.lumiastream.com/audit/cancelled-landscape");
		check(wizard->currentPage()->isComplete() && nextSource->isEnabled(), "repairing the same field immediately restores readiness");
		captureWizardWindow(wizard, "wizard-3-sources-complete");
		wizard->next(); settleWizard(); check(wizard->currentId() == 4, "complete source assignments reach a separate Review page");
		check(wizard->button(QWizard::FinishButton)->isEnabled() && !wizard->button(QWizard::CustomButton1)->isVisibleTo(wizard), "Create is gated at Review while Return is hidden there");
		captureWizardWindow(wizard, "wizard-4-review");
		QTimer::singleShot(0, [&] {
			auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
			check(dialog && dialog->objectName() == "PulseWeaverReviewGroupDialog", "Review Stage card opens actual assignment details");
			captureWizardWindow(dialog, "wizard-review-stage");
			bool linkShown = false;
			for (auto *link : dialog->findChildren<QLineEdit *>("PulseSetupReviewUrl")) if (link->text() == url->text()) linkShown = link->isReadOnly();
			check(linkShown, "Review displays the actual entered link read-only without claiming it was tested");
			QPushButton *edit = nullptr;
			for (auto *button : dialog->findChildren<QPushButton *>("PulseSetupReviewEdit")) if (button->accessibleName().contains(horizontalLabel)) { edit = button; break; }
			check(edit && edit->property("pulseQuestionIndex").isValid(), "Review provides an exact assignment edit target"); edit->click();
		});
		setupField<QPushButton>(review, "Review Starting")->click(); settleWizard();
		check(wizard->currentId() == 3 && jump->currentText() == horizontalLabel && horizontal->currentData() == "url", "Review Edit returns to the intended source question with its answer preserved");
		const QString preserved = url->text();
		check(!wizard->button(QWizard::CustomButton1)->isVisibleTo(wizard), "Sources keeps its direct Review action without a duplicate footer shortcut");
		wizard->button(QWizard::NextButton)->click(); settleWizard(); check(wizard->currentId() == 4, "valid edited source returns directly to Review");
		QPushButton *editLooks = nullptr;
		for (auto *button : review->findChildren<QPushButton *>()) if (button->text() == "Edit Looks") editLooks = button;
		check(editLooks, "Review offers a section-specific Looks edit"); editLooks->click(); settleWizard();
		check(wizard->currentId() == 2 && url->text() == preserved && vertical->currentData() == obs_source_get_uuid(portrait), "Back through Looks retains independent source answers");
		check(wizard->button(QWizard::CustomButton1)->isVisibleTo(wizard), "visited Review exposes its shortcut while editing Looks");
		wizard->button(QWizard::CustomButton1)->click(); settleWizard(); check(wizard->currentId() == 4, "Return to review traverses only valid remaining pages");
		check(wizardSourceIdentities() == beforeSources, "review and Back navigation create no OBS input or scene");
		completed = true; wizard->button(QWizard::CancelButton)->click();
	});
	PulseMotionEngineTestAccess::openDetailedWizard(engine);
	check(completed && readFile(storage) == beforeStorage && wizardSourceIdentities() == beforeSources,
		"cancelling the full production wizard leaves source identities and persistent motion storage unchanged");
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
	const QString nonVideo = localFiles.filePath("not-video.txt"); writeFile(nonVideo, "not a video");
	for (const QString &failure : {QString("file"), QString("format"), QString("timing"), QString("frontend")}) {
		auto spec = stingerSpec; if (failure == "file") spec.insert("path", localFiles.filePath("absent.webm"));
		if (failure == "format") spec.insert("path", nonVideo);
		if (failure == "timing") spec.insert("transitionPointMs", -1);
		stingerRequest.insert("newStingers", QJsonObject{{"transition", spec}});
		const auto failed = engine.createGuidedShow(stingerRequest);
		if (failure == "format") check(failed.value("message").toString().contains("image or document"), "a readable document is rejected as a new stinger before frontend creation");
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
		check(action.value("durationMs").toInt()==PulseShow::DefaultMovementDurationMs,"new Looks use the shared smooth movement duration");
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
	setupWizardRoundTripTests(landscapeGraphic, portraitGraphic);
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

// Generated-output checks use the real show transaction and serialized native
// actions. The capture/browser inputs are inert and never access a device/URL.
static QJsonArray guidedOutputEvidence;

static bool collectAuditSource(void *opaque, obs_source_t *source)
{
	auto &sources = *static_cast<std::vector<OBSSource> *>(opaque);
	for (const auto &existing : sources) if (existing.Get() == source) return true;
	sources.emplace_back(source); return true;
}

static std::vector<OBSSource> auditSources()
{
	std::vector<OBSSource> sources;
	obs_enum_sources(collectAuditSource, &sources);
	obs_enum_canvases([](void *opaque, obs_canvas_t *canvas) {
		obs_canvas_enum_scenes(canvas, collectAuditSource, opaque); return true;
	}, &sources);
	return sources;
}

static void removeAuditAdditions(const QSet<QString> &baseline)
{
	auto sources = auditSources();
	for (auto it = sources.rbegin(); it != sources.rend(); ++it)
		if (!baseline.contains(QString::fromUtf8(obs_source_get_uuid(*it)))) obs_source_remove(*it);
	sources.clear(); obs_wait_for_destroy_queue();
	check(wizardSourceIdentities() == baseline, "generated-show fixture releases every added input and both canvas scenes");
}

static QMap<QString, QByteArray> generatedFileSnapshot(const QString &directory)
{
	QMap<QString, QByteArray> snapshot;
	QDirIterator files(directory, QDir::Files, QDirIterator::Subdirectories);
	while (files.hasNext()) {
		const QString path = files.next();
		snapshot.insert(QDir(directory).relativeFilePath(path), QCryptographicHash::hash(readFile(path), QCryptographicHash::Sha256));
	}
	return snapshot;
}

static QString labelledInput(const QString &directory, const QString &name, const QSize &size, const QColor &color)
{
	QImage image(size, QImage::Format_ARGB32_Premultiplied); image.fill(color);
	QPainter painter(&image); painter.setPen(QColor(255, 255, 255, 80));
	for (int x = 0; x < size.width(); x += qMax(1, size.width() / 12)) painter.drawLine(x, 0, x, size.height());
	for (int y = 0; y < size.height(); y += qMax(1, size.height() / 8)) painter.drawLine(0, y, size.width(), y);
	const int marker = qMax(12, qMin(size.width(), size.height()) / 7);
	painter.fillRect(0, 0, marker, marker, Qt::red); painter.fillRect(size.width() - marker, 0, marker, marker, Qt::green);
	painter.fillRect(0, size.height() - marker, marker, marker, Qt::blue);
	painter.fillRect(size.width() - marker, size.height() - marker, marker, marker, Qt::yellow);
	QFont font = painter.font(); font.setPixelSize(qMax(24, size.height() / 10)); font.setBold(true); painter.setFont(font);
	painter.setPen(Qt::white); painter.drawText(image.rect().adjusted(marker, marker, -marker, -marker), Qt::AlignCenter | Qt::TextWordWrap, name);
	painter.end();
	const QString path = QDir(directory).filePath(name + ".png"); check(image.save(path), "labelled source fixture PNG saves"); return path;
}

static QString resolvePlannedInput(const QString &id, const QJsonObject &definitions, const QString &route = {})
{
	if (!id.startsWith("new:")) return id;
	const auto definition = definitions.value(id.mid(4)).toObject();
	QString name = definition.value("name").toString();
	if (definition.value("kind") == "browser_source" || definition.value("kind") == "setup_card") name += " · " + route;
	OBSSourceAutoRelease source = obs_get_source_by_name(name.toUtf8().constData());
	check(bool(source), "planned source definition resolves to its created native input");
	return QString::fromUtf8(obs_source_get_uuid(source));
}

static QJsonObject preservedInputState(obs_source_t *source)
{
	OBSDataAutoRelease settings = obs_source_get_settings(source);
	return {{"settings", QJsonDocument::fromJson(obs_data_get_json(settings)).object()},
		{"mixers", int(obs_source_get_audio_mixers(source))}, {"muted", obs_source_muted(source)},
		{"volume", obs_source_get_volume(source)}, {"monitoring", int(obs_source_get_monitoring_type(source))}};
}

static QSet<QString> expectedViewFeeds(const QString &view, const QString &camera, const QString &content, const QString &detail)
{
	if (view == "content") return {content};
	if (view == "camera") return {camera};
	if (view == "detail") return {detail};
	if (view == "content-camera") return {content, camera};
	if (view == "content-detail") return {content, detail};
	if (view == "two-cameras") return {camera, detail};
	if (view == "three-up") return {content, camera, detail};
	check(false, "quality scenario names an independently understood operating view"); return {};
}

static QJsonObject checkGuidedOutput(PulseMotionEngine &engine, const PulseSetupPlan::Options &options,
	const QJsonObject &request, const QSet<QString> &baseline)
{
	const auto definitions = request.value("newSources").toObject();
	QMap<QString, QString> backgrounds;
	QMap<QString, QSet<QByteArray>> fingerprints;
	QSet<QString> emittedViews, emittedScreens;
	QJsonArray actions;
	int mainLooks = 0, screenLooks = 0, cardInputs = 0, newBrowserInputs = 0;
	QMap<QString, QString> alertIds;
	for (int layer = 0; layer < options.alerts.size(); ++layer) if (options.alerts[layer].enabled) {
		for (const QString &route : {QString("horizontal"), QString("vertical")}) {
			const QString planned = route == "horizontal" ? options.alerts[layer].horizontal : options.alerts[layer].vertical;
			if (!planned.isEmpty()) alertIds.insert(route + QString::number(layer + 1), resolvePlannedInput(planned, definitions, route));
		}
	}
	for (const auto &entry : PulseMotionEngineTestAccess::actions(engine)) {
		const auto action = entry.toObject(); const QString stage = action.value("stage").toString();
		if (!stage.startsWith(options.name + " · ")) continue;
		actions.append(action);
		const QString templateId = action.value("templateId").toString(), view = templateId.section('/', 1);
		const bool main = templateId.startsWith("guided-main/");
		if (main) { ++mainLooks; check(options.views.contains(view) && !emittedViews.contains(view), "each generated action belongs to one distinct selected operating view"); emittedViews.insert(view); }
		else { ++screenLooks; const QString phase = templateId.section('/',0,0); check(!emittedScreens.contains(phase),"each lifecycle phase is created once"); emittedScreens.insert(phase); }
		for (const QString &route : {QString("horizontal"), QString("vertical")}) {
			const QString camera = options.sources.camera.isEmpty() ? QString() : resolvePlannedInput(options.sources.camera, definitions, route);
			const QString content = options.sources.content.isEmpty() ? QString() : resolvePlannedInput(options.sources.content, definitions, route);
			const QString detail = options.sources.detail.isEmpty() ? QString() : resolvePlannedInput(options.sources.detail, definitions, route);
			QSet<QString> knownFeeds; for (const QString &id : {camera,content,detail}) if (!id.isEmpty()) knownFeeds.insert(id);
			const bool portrait = route == "vertical"; const double width = portrait ? 1080 : 1920, height = portrait ? 1920 : 1080;
			QSet<QString> visibleFeeds, visibleAlerts; QMap<int, QString> stack;
			QMap<int, QJsonObject> visualFeeds; int highestPicture = -1, lowestFeed = 9999, backdropOrder = -1; bool foundPresentation = false;
			for (const auto &targetValue : action.value("items").toArray()) {
				const auto target = targetValue.toObject();
				if (target.value("container").toString().endsWith(" · Portrait") != portrait) continue;
				const auto transform = target.value("transform").toObject();
				if (!transform.value("visible").toBool()) continue;
				const QString id = target.value("sourceUuid").toString();
				OBSSourceAutoRelease source = obs_get_source_by_uuid(id.toUtf8().constData());
				check(source && (obs_source_get_output_flags(source) & OBS_SOURCE_VIDEO), "every visible generated item resolves to a real video input");
				for (const char *field : {"positionX", "positionY", "boundsWidth", "boundsHeight", "scaleX", "scaleY", "rotation"})
					check(transform.value(field).isDouble() && std::isfinite(transform.value(field).toDouble()), "generated transforms contain finite native numbers");
				const double x = transform.value("positionX").toDouble(), y = transform.value("positionY").toDouble();
				const double w = transform.value("boundsWidth").toDouble(), h = transform.value("boundsHeight").toDouble();
				check(x >= -.01 && y >= -.01 && w > 0 && h > 0 && x + w <= width + .51 && y + h <= height + .51,
					"generated visible panel bounds remain positive and inside the actual 16:9 or 9:16 canvas");
				check(transform.value("scaleX").toDouble() > 0 && transform.value("scaleY").toDouble() > 0 && transform.value("rotation").toDouble() == 0,
					"default generated panels do not introduce flips or rotation");
				for (const char *field : {"cropLeft", "cropTop", "cropRight", "cropBottom"}) check(transform.value(field).toInt() >= 0, "generated crop values are nonnegative");
				check(transform.value("cropLeft").toInt() + transform.value("cropRight").toInt() < int(obs_source_get_width(source)) &&
					transform.value("cropTop").toInt() + transform.value("cropBottom").toInt() < int(obs_source_get_height(source)), "generated crop retains actual source pixels");
				const int order = transform.value("order").toInt(); check(!stack.contains(order), "visible generated stack positions are unique"); stack.insert(order, id);
				if (knownFeeds.contains(id)) {
					check(!visibleFeeds.contains(id), "a selected input appears only once within a generated view and canvas"); visibleFeeds.insert(id);
					highestPicture = qMax(highestPicture, order); lowestFeed = qMin(lowestFeed, order);
					auto visual = transform; visual.remove("order"); visual.remove("locked");
					visualFeeds.insert(order, QJsonObject{{"source", id}, {"transform", visual}});
					if (id == content || (id == camera && !options.cropCamera) || (id == detail && !options.cropDetail)) {
						check(transform.value("boundsType").toInt() == OBS_BOUNDS_SCALE_INNER && !transform.value("cropToBounds").toBool() &&
							transform.value("cropLeft").toInt() == 0 && transform.value("cropTop").toInt() == 0 && transform.value("cropRight").toInt() == 0 && transform.value("cropBottom").toInt() == 0,
							"fit retains all source corners including game HUD and shared-screen edges");
					}
					if ((id == camera && options.cropCamera) || (id == detail && options.cropDetail))
						check(transform.value("boundsType").toInt()==OBS_BOUNDS_SCALE_OUTER&&transform.value("cropToBounds").toBool(),"explicit camera/detail cropping fills its frame rather than silently retaining Fit");
					continue;
				}
				bool alert = false;
				for (int layer = 0; layer < options.alerts.size(); ++layer) if (options.alerts[layer].enabled && alertIds.value(route + QString::number(layer + 1)) == id) {
					check(main || options.alerts[layer].scope == "all", "a main-only alert never leaks onto Starting, Break or Ending");
					check(!visibleAlerts.contains(id), "each shared alert slot has one visible scene item per canvas"); visibleAlerts.insert(id); alert = true;
					check(transform.value("locked").toBool() && x == 0 && y == 0 && std::abs(w - width) < .01 && std::abs(h - height) < .01,
						"alert layers retain locked full-canvas placement");
				}
				if (alert) continue;
				const QString artwork = options.artwork.value(templateId.section('/', 0, 0)).toObject().value(route).toString();
				if (!main && !artwork.isEmpty() && id == resolvePlannedInput(artwork, definitions, route)) {
					check(std::strcmp(obs_source_get_unversioned_id(source), "browser_source") == 0 && x == 0 && y == 0 && std::abs(w - width) < .01 && std::abs(h - height) < .01,
						"an explicitly chosen browser artwork source occupies its requested format's full screen");
					foundPresentation = true; highestPicture=qMax(highestPicture,order); continue;
				}
				OBSDataAutoRelease metadata = obs_source_get_private_settings(source);
				check(obs_data_get_bool(metadata, "pulseweaver.setup_card") && std::strcmp(obs_source_get_unversioned_id(source), "image_source") == 0,
					"generated static presentation layers are actual image inputs, not hidden browsers or placeholders");
				OBSDataAutoRelease settings = obs_source_get_settings(source);
				const QString path = QString::fromUtf8(obs_data_get_string(settings, "file")); const QImage pixels(path);
				check(!pixels.isNull() && pixels.size() == QSize(int(width), int(height)) && obs_source_get_width(source) == uint32_t(width) && obs_source_get_height(source) == uint32_t(height),
					"generated PNG and actual native source dimensions match their canvas");
				check(qAlpha(pixels.pixel(0, 0)) == 255 && qAlpha(pixels.pixel(pixels.width() - 1, pixels.height() - 1)) == 255,
					"generated screens/backgrounds cover their frame with opaque pixels");
				check(x == 0 && y == 0 && std::abs(w - width) < .01 && std::abs(h - height) < .01, "generated screen graphics are not squeezed into a camera panel");
				const bool background = obs_data_get_bool(metadata, "pulseweaver.setup_card.backgroundOnly");
				if (background) {
					check(main, "the neutral live backdrop is not an unexplained extra layer on a holding screen"); backdropOrder = order;
					check(!backgrounds.contains(route) || backgrounds.value(route) == id, "one generated backdrop input is reused across all live Looks on its canvas"); backgrounds.insert(route, id);
				} else {
					check(!main, "live operating views do not contain an unrequested holding card"); foundPresentation = true; highestPicture=qMax(highestPicture,order);
					const QString expectedText = templateId.startsWith("guided-starting/") ? options.startingText : templateId.startsWith("guided-break/") ? options.breakText : options.endingText;
					check(QString::fromUtf8(obs_data_get_string(metadata, "pulseweaver.setup_card.text")) == expectedText, "generated screen retains the user's own text for that phase");
					int lightPixels = 0;
					for (int py = pixels.height()/4; py < pixels.height()*3/4; py += 2) for (int px = pixels.width()/10; px < pixels.width()*9/10; px += 2) {
						const QColor value = pixels.pixelColor(px, py); if (value.red() > 205 && value.green() > 205 && value.blue() > 205) ++lightPixels;
					}
					check(lightPixels > 50, "generated card contains visible high-contrast text pixels inside its central content area");
				}
				const QString evidence = qEnvironmentVariable("PULSE_GUIDED_OUTPUT_EVIDENCE_DIR");
				if (!evidence.isEmpty()) {
					const QString file = QDir(evidence).filePath(options.name + "-" + templateId.section('/', 0, 0) + "-" + route + (background ? "-background.png" : "-card.png"));
					check(pixels.save(file), "actual generated PNG copied into the optional quality evidence directory");
				}
			}
			if (main) {
				check(visibleFeeds == expectedViewFeeds(view, camera, content, detail), "generated view contains exactly the feeds promised by its task label");
				check(backdropOrder >= 0 && backdropOrder < lowestFeed, "neutral live backdrop stays below every feed");
				QJsonArray fingerprint; for (auto visual = visualFeeds.cbegin(); visual != visualFeeds.cend(); ++visual) fingerprint.append(visual.value());
				const QByteArray signature = QJsonDocument(fingerprint).toJson(QJsonDocument::Compact);
				check(!fingerprints[route].contains(signature), "selected live views have genuinely different source visibility or framing on each canvas"); fingerprints[route].insert(signature);
			} else check(foundPresentation, "each requested lifecycle phase has a usable static card or its exact explicit custom artwork");
			QStringList expectedStack;
			for (int layer = 0; layer < options.alerts.size(); ++layer) if (options.alerts[layer].enabled && (main || options.alerts[layer].scope == "all")) {
				const QString id = alertIds.value(route + QString::number(layer + 1)); if (!id.isEmpty()) expectedStack.append(id);
			}
			QStringList actualStack;
			for (auto item = stack.cbegin(); item != stack.cend(); ++item) if (visibleAlerts.contains(item.value())) { actualStack.append(item.value()); check(item.key() > highestPicture, "alert layers remain above live pictures and holding artwork"); }
			check(actualStack == expectedStack && visibleAlerts.size() == expectedStack.size(), "one, two or three alert layers preserve their chosen order and scope on each canvas");
		}
	}
	check(mainLooks == options.views.size(), "creation yields exactly the selected operating views without extra duplicated presets");
	check(emittedViews==options.views,"every requested operating view is present in the final action catalogue");
	check(screenLooks == int(options.starting) + int(options.brb) + int(options.ending), "creation yields only the requested Starting, Break and Ending phases");
	QSet<QString> expectedScreens;if(options.starting)expectedScreens.insert("guided-starting");if(options.brb)expectedScreens.insert("guided-break");if(options.ending)expectedScreens.insert("guided-ending");
	check(emittedScreens==expectedScreens,"every requested lifecycle phase is present with its correct phase identity");
	QJsonArray sources;
	for (const auto &source : auditSources()) if (!baseline.contains(QString::fromUtf8(obs_source_get_uuid(source)))) {
		const QString type = QString::fromUtf8(obs_source_get_unversioned_id(source));
		if (type == "image_source") ++cardInputs;
		if (type == "browser_source") {
			++newBrowserInputs; OBSDataAutoRelease settings = obs_source_get_settings(source);
			check(obs_source_get_audio_mixers(source) == 0 && obs_source_get_monitoring_type(source) == OBS_MONITORING_TYPE_NONE,
				"new browser alerts are actually silent and do not start an audio monitor");
			check(!obs_data_get_bool(settings, "shutdown") && !obs_data_get_bool(settings, "restart_when_active") && obs_data_get_bool(settings, "reroute_audio"),
				"new browser alerts stay warm and do not restart on Look changes");
		}
		OBSDataAutoRelease saved = obs_save_source(source);
		sources.append(QJsonDocument::fromJson(obs_data_get_json(saved)).object());
	}
	return {{"name", options.name}, {"actions", actions}, {"createdSources", sources}, {"imageInputs", cardInputs}, {"browserInputs", newBrowserInputs}};
}

static void staticSetupCardPixelTests()
{
	const QString evidence = qEnvironmentVariable("PULSE_GUIDED_OUTPUT_EVIDENCE_DIR");
	for (const QSize &size : {QSize(1920, 1080), QSize(1080, 1920)}) {
		QSet<QByteArray> palettes;
		for (const QString &style : {QString("navy"), QString("cyan"), QString("violet"), QString("amber")}) {
			QJsonObject spec{{"kind", "setup_card"}, {"name", "Readable native card"}, {"text", "Starting soon"},
				{"subtitle", "A real show screen without an external service"}, {"style", style}};
			const QImage image = PulseSetupCards::render(spec, size);
			check(!image.isNull() && image.size() == size, "each advertised static-card style renders at both real canvas resolutions");
			const auto digest = QCryptographicHash::hash(QByteArray(reinterpret_cast<const char *>(image.constBits()), image.sizeInBytes()), QCryptographicHash::Sha256);
			check(!palettes.contains(digest), "every advertised card colour style changes the actual output pixels"); palettes.insert(digest);
			if (!evidence.isEmpty()) check(image.save(QDir(evidence).filePath("static-card-" + style + (size.height() > size.width() ? "-portrait.png" : "-landscape.png"))), "static-card pixel evidence saves");
			if (style != "navy") continue;
			QJsonObject altered = spec; altered.insert("text", "Thanks for watching, see you next time");
			const QImage different = PulseSetupCards::render(altered, size);
			check(image != different, "changing a card's message changes its real PNG pixels, not only source metadata");
			QJsonObject longest = spec; longest.insert("text", QString(160, QChar('W'))); longest.insert("subtitle", QString(320, QChar('W')));
			const QImage wrapped = PulseSetupCards::render(longest, size);
			QJsonObject background = longest; background.insert("backgroundOnly", true);
			const QImage plain = PulseSetupCards::render(background, size);
			check(!wrapped.isNull() && !plain.isNull(), "long unbroken supported title/subtitle render without a browser");
			int changed = 0, outside = 0;
			const QRect safe(qRound(size.width() * .05), qRound(size.height() * .05), qRound(size.width() * .90), qRound(size.height() * .90));
			for (int y = 0; y < size.height(); ++y) for (int x = 0; x < size.width(); ++x) {
				if (wrapped.pixel(x, y) != plain.pixel(x, y)) { ++changed; if (!safe.contains(x, y)) ++outside; }
			}
			check(changed > 1000 && outside == 0, "long card text produces real visible pixels and stays within the frame's five-percent margin");
			if (!evidence.isEmpty()) check(wrapped.save(QDir(evidence).filePath(size.height() > size.width() ? "static-card-long-portrait.png" : "static-card-long-landscape.png")), "long-text card pixel evidence saves");
		}
	}
}

static QPushButton *guidedButton(QWidget *root, const QString &text)
{
	for (auto *button : root->findChildren<QPushButton *>()) if (button->text() == text) return button;
	check(false, qPrintable("missing guided button: " + text)); return nullptr;
}
static QCheckBox *guidedCheck(QWidget *root, const QString &text)
{
	for (auto *box : root->findChildren<QCheckBox *>()) if (box->text() == text) return box;
	check(false, qPrintable("missing guided checkbox: " + text)); return nullptr;
}
static QMap<QString, QPushButton *> guidedViews(QWidget *root)
{
	QMap<QString, QPushButton *> result;
	for (auto *button : root->findChildren<QPushButton *>()) if (button->property("pulseGuidedView").isValid()) result.insert(button->property("pulseGuidedView").toString(), button);
	return result;
}

// Retain compatibility coverage for the previous six-page editor. Draft edits, modal previews and
// review repairs must remain side-effect free until the user chooses Create.
static void guidedWizardInteractionTests()
{
	QTemporaryDir files;
	const QString storage = files.filePath("config/obs-studio/plugin_config/pulse-weaver-core/motion.json");
	check(QDir().mkpath(QFileInfo(storage).absolutePath()), "private task-first wizard storage");
	writeFile(storage, QJsonDocument(QJsonObject{{"version",1},{"actions",QJsonArray{}},{"originals",QJsonArray{}},{"originalScenes",QJsonObject{}},{"auditSentinel","unchanged"}}).toJson());
	const auto createImage = [&](const QString &name, const QSize &size) {
		OBSDataAutoRelease settings = obs_data_create();
		const auto path = labelledInput(files.path(), name, size, QColor("#286b80")); obs_data_set_string(settings, "file", path.toUtf8().constData());
		OBSSourceAutoRelease source = obs_source_create("image_source", name.toUtf8().constData(), settings, nullptr); return OBSSource(source);
	};
	OBSSource content = createImage("Wizard game input", {1920,1080}), detail = createImage("Wizard detail input", {900,1600}), card = createImage("Forbidden generated setup card", {1920,1080});
	OBSDataAutoRelease cardMetadata = obs_source_get_private_settings(card); obs_data_set_bool(cardMetadata,"pulseweaver.setup_card",true);
	OBSDataAutoRelease nativeSettings = obs_data_create();obs_data_set_int(nativeSettings,"width",1920);obs_data_set_int(nativeSettings,"height",1080);obs_data_set_string(nativeSettings,"url","http://127.0.0.1:18754/overlay/audit-token");
	OBSSourceAutoRelease native = obs_source_create("browser_source","Forbidden native overlay feed",nativeSettings,nullptr);
	OBSDataAutoRelease priorAlertSettings=obs_data_create();obs_data_set_int(priorAlertSettings,"width",1920);obs_data_set_int(priorAlertSettings,"height",1080);obs_data_set_string(priorAlertSettings,"url","https://example.invalid/existing-show-alert");
	OBSSourceAutoRelease priorAlert=obs_source_create("browser_source","Alert layer 1 · landscape · horizontal",priorAlertSettings,nullptr);
	const QString contentId = obs_source_get_uuid(content), detailId = obs_source_get_uuid(detail);
	const auto beforeSources = wizardSourceIdentities(); const auto beforeFiles = generatedFileSnapshot(files.path());
	PulseMotionEngine engine(nullptr, storage); bool completed = false;
	QTimer::singleShot(0, [&] {
		auto *wizard = dynamic_cast<PulseGuidedWizard *>(QApplication::activeModalWidget());
		check(wizard && wizard->property("pulseTaskFirst").toBool(), "legacy task-first wizard remains compatible"); settleWizard();
		check(wizard->pageIds() == QList<int>{0,1,2,3,4,5} && wizard->currentId() == 0, "task-first flow has six focused phases");
		check(!wizard->currentPage()->isComplete() && !wizard->button(QWizard::NextButton)->isEnabled(), "activity selection is required before choosing inputs");
		captureWizardWindow(wizard, "guided-0-activity");
		QPushButton *gaming = nullptr;
		for (auto *button : wizard->findChildren<QPushButton *>()) if (button->property("pulseGuidedActivity") == "gaming") gaming = button;
		check(gaming, "Games activity is visible as a direct choice"); gaming->click(); settleWizard();
		check(wizard->currentId() == 1 && !wizard->currentPage()->isComplete(), "activity choice advances directly to real inputs and blocks a blank game show");
		const auto reuse = [&](const QString &role, const QString &name) {
			QTimer::singleShot(0, [&] {
				auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget()); check(dialog && dialog->objectName() == "PulseGuidedChooseInput", "input selection opens a two-action choice");
				QTimer::singleShot(0, [&] {
					auto *picker = qobject_cast<QDialog *>(QApplication::activeModalWidget()); check(picker && picker->objectName() == "PulseWeaverSetupChoiceDialog", "reuse uses the production click-through chooser");
					for(auto *button:picker->findChildren<QPushButton *>("PulseWeaverSetupChoiceCard"))check(button->accessibleName()!="Forbidden generated setup card"&&button->accessibleName()!="Forbidden native overlay feed","feed chooser excludes presentation-only native overlays and generated cards rejected by the backend");
					setupField<QPushButton>(picker, name)->click();
				});
				guidedButton(dialog, "Use an existing input")->click();
			});
			setupField<QPushButton>(wizard, "Choose " + role + " input")->click(); settleWizard();
		};
		reuse("content", "Wizard game input"); check(wizard->options().sources.content == contentId && wizard->options().sources.camera.isEmpty(), "one content binding is enough without a mandatory camera");
		wizard->next(); settleWizard();
		check(wizard->currentId() == 2 && guidedViews(wizard->page(2)).keys() == QStringList{"content"} && wizard->options().views == QSet<QString>{"content"}, "one input yields one usable view with no empty pretend alternatives");
		captureWizardWindow(wizard, "guided-2-single-input"); wizard->back(); settleWizard();
		QTimer::singleShot(0, [&] {
			auto *choice = qobject_cast<QDialog *>(QApplication::activeModalWidget()); check(choice && choice->objectName() == "PulseGuidedChooseInput", "new input has the same clear entry point");
			captureWizardWindow(choice, "guided-input-entry");
			QTimer::singleShot(0, [&] {
				auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget()); check(dialog && dialog->objectName() == "PulseGuidedSourceDialog", "new source is configured in a focused draft editor");
				auto *type = setupField<QComboBox>(dialog, "Input type"); check(type->findData("image_source") >= 0, "registered image input is offered");
				type->setCurrentIndex(type->findData("browser_source"));setupField<QLineEdit>(dialog,"Input browser link")->setText("http://127.0.0.1:18754/overlay/audit-token");settleWizard();check(!guidedButton(dialog,"Use this input")->isEnabled(),"native presentation URL cannot be drafted as a feed the backend rejects");
				type->setCurrentIndex(type->findData("image_source"));
				setupField<QLineEdit>(dialog, "Input name")->setText("Draft camera with a deliberately longer meaningful name");
				setupField<QLineEdit>(dialog, "Input file")->setText(labelledInput(files.path(), "Draft-camera-file", {1200,900}, QColor("#754d80")));
				settleWizard(); captureWizardWindow(dialog, "guided-input-draft");
				check(guidedButton(dialog, "Use this input")->isEnabled(), "complete file draft can be selected"); guidedButton(dialog, "Use this input")->click();
			}); guidedButton(choice, "Add a new input")->click();
		}); setupField<QPushButton>(wizard, "Choose camera input")->click(); settleWizard();
		check(wizard->options().sources.camera.startsWith("new:") && wizardSourceIdentities() == beforeSources, "new camera remains a draft without opening native inputs");
		reuse("detail", "Wizard detail input"); check(wizard->options().sources.detail == detailId, "third independent input is bound once");
		captureWizardWindow(wizard, "guided-1-three-inputs"); wizard->next(); settleWizard();
		check(guidedViews(wizard->page(2)).size() == 7 && wizard->options().views == QSet<QString>{"content","content-camera","camera"}, "three inputs offer seven functional views and a small useful initial set");
		for(auto *button:guidedViews(wizard->page(2)))if(button->isChecked())button->click();
		check(!wizard->currentPage()->isComplete(),"deliberately clearing every view blocks continuation");wizard->back();wizard->next();settleWizard();
		check(wizard->options().views.isEmpty()&&!wizard->currentPage()->isComplete(),"Back and Next preserve an intentionally empty view selection instead of undoing the user's decision");
		guidedViews(wizard->page(2)).value("camera")->click();check(!wizard->currentPage()->isComplete(),"Games cannot continue with only a camera view that hides the chosen game");
		guidedViews(wizard->page(2)).value("content")->click();guidedViews(wizard->page(2)).value("content-camera")->click();check(wizard->currentPage()->isComplete(),"restoring a content-visible view repairs the decision on the same page");
		const auto viewsBeforePreview = wizard->options().views;
		QTimer::singleShot(0, [&] {
			auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget()); check(dialog && dialog->objectName() == "PulseGuidedViewPreview" && setupField<QWidget>(dialog,"Landscape and portrait fit illustration"), "view preview explains the actual horizontal and portrait framing");
			captureWizardWindow(dialog, "guided-view-preview"); dialog->reject();
		});
		for (auto *button : wizard->page(2)->findChildren<QPushButton *>()) if (button->text() == "Preview this view") { button->click(); break; }
		check(wizard->options().views == viewsBeforePreview, "read-only view preview never adds or removes a view");
		QTimer::singleShot(0, [&] {
			auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget()); check(dialog && dialog->objectName() == "PulseGuidedFraming", "cropping is one shared framing decision");
			guidedCheck(dialog, "Crop camera to fill")->setChecked(true); captureWizardWindow(dialog, "guided-framing"); guidedButton(dialog, "Use framing")->click();
		}); guidedButton(wizard->page(2), "Framing…")->click(); check(wizard->options().cropCamera && !wizard->options().cropDetail, "only explicitly requested camera cropping changes");
		QTimer::singleShot(0, [&] {
			auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget()); check(dialog && dialog->objectName() == "PulseGuidedTransitions", "screen transitions remain available in a focused surface");
			auto *h = setupField<QComboBox>(dialog,"horizontal screen transition"), *v = setupField<QComboBox>(dialog,"vertical screen transition"); h->setCurrentIndex(h->findData("cut"));
			auto *tabs = dialog->findChild<QTabWidget *>(); check(tabs && tabs->count() == 2, "transition formats remain independent");
			for (int i=0;i<2;++i) { tabs->setCurrentIndex(i); settleWizard(); captureWizardWindow(dialog,"guided-transitions-"+QString::number(i)); }
			check(v->currentData() == "fade", "editing landscape does not overwrite portrait transition"); guidedButton(dialog,"Use transitions")->click();
		}); guidedButton(wizard->page(2), "Screen transitions…")->click();
		captureWizardWindow(wizard,"guided-2-views"); wizard->next(); settleWizard(); check(wizard->currentId()==3,"views advance to surrounding screens");
		setupField<QLineEdit>(wizard,"Show name")->setText("Cancelled task-first test");
		auto *startText=setupField<QLineEdit>(wizard,"Starting screen message"); startText->clear(); check(!wizard->currentPage()->isComplete(),"included built-in screen requires meaningful visible text");
		startText->setText("We will be live shortly"); auto *brb=setupField<QCheckBox>(wizard,"Include break screen"); brb->setChecked(false); brb->setChecked(true);
		check(setupField<QLineEdit>(wizard,"Break screen message")->text()=="Be right back","disabling a screen does not erase its message");
		captureWizardWindow(wizard,"guided-3-screens"); wizard->next(); settleWizard(); check(wizard->currentId()==4,"screens advance to optional alerts");
		for(int layer=1;layer<=3;++layer) {
			QTimer::singleShot(0,[&,layer] {
				auto *dialog=qobject_cast<QDialog *>(QApplication::activeModalWidget()); check(dialog&&dialog->objectName()=="PulseGuidedOverlayDialog","each alert has the same clear editor");
				setupField<QLineEdit>(dialog,"Landscape browser link")->setText(QString("https://example.invalid/alert-%1-h").arg(layer));
				guidedCheck(dialog,"Use the same browser link for portrait")->setChecked(layer!=2);
				if(layer==2)setupField<QLineEdit>(dialog,"Portrait browser link")->setText("https://example.invalid/alert-2-v");
				guidedCheck(dialog,"Show on every screen")->setChecked(layer!=1);
				if(layer==2)captureWizardWindow(dialog,"guided-alert-editor"); guidedButton(dialog,"Use these overlays")->click();
			}); setupField<QPushButton>(wizard,"Edit alert layer "+QString::number(layer))->click();
		}
		const auto planned=wizard->options(); check(planned.alerts.size()==3&&planned.alerts[0].scope=="main"&&planned.alerts[1].scope=="all"&&planned.alerts[2].enabled,"three layers preserve independent scope");
		const QString draftedAlertName=planned.newSources.value(planned.alerts[0].horizontal.mid(4)).toObject().value("name").toString();
		OBSSourceAutoRelease nameConflict=obs_get_source_by_name((draftedAlertName+" · horizontal").toUtf8().constData());check(!nameConflict,"a second show automatically avoids an existing browser viewport name without needing a hidden rename field");
		check(planned.alerts[0].horizontal==planned.alerts[0].vertical&&planned.alerts[1].horizontal!=planned.alerts[1].vertical,"same-link and separate portrait choices are represented faithfully");
		setupField<QPushButton>(wizard,"Remove alert layer 1")->click();
		check(!PulseSetupPlan::draft(wizard->options()).value("newSources").toObject().contains(planned.alerts[0].horizontal.mid(4)),"disabled layer drafts are pruned from the generated result");
		QTimer::singleShot(0,[&]{auto *dialog=qobject_cast<QDialog *>(QApplication::activeModalWidget());guidedButton(dialog,"Use these overlays")->click();});setupField<QPushButton>(wizard,"Edit alert layer 1")->click();
		check(wizard->options().alerts[0].horizontal==planned.alerts[0].horizontal&&wizard->options().alerts[0].scope==planned.alerts[0].scope,"restoring a removed layer preserves its provider links and main-only scope");
		const auto beforeCancelledOverlay=PulseSetupPlan::draft(wizard->options());
		QTimer::singleShot(0,[&]{auto *dialog=qobject_cast<QDialog *>(QApplication::activeModalWidget());setupField<QLineEdit>(dialog,"Landscape browser link")->setText("https://example.invalid/unwanted-edit");guidedCheck(dialog,"Show on every screen")->setChecked(false);dialog->reject();});setupField<QPushButton>(wizard,"Edit alert layer 3")->click();
		check(PulseSetupPlan::draft(wizard->options())==beforeCancelledOverlay,"cancelling an alert edit preserves its scope, bindings and source definitions");
		captureWizardWindow(wizard,"guided-4-alerts");wizard->next();settleWizard();check(wizard->currentId()==5&&wizard->button(QWizard::FinishButton)->isEnabled()&&wizard->finalProblem().isEmpty(),"review permits a complete usable show");
		captureWizardWindow(wizard,"guided-5-review");
		guidedButton(wizard->page(5),"Change screens…")->click();settleWizard();check(wizard->currentId()==3,"review repairs return directly to the screen decision");
		const auto artworkButton=[&]{return guidedButton(startText->parentWidget(),"Use my own browser artwork…");};
		QTimer::singleShot(0,[&]{auto *dialog=qobject_cast<QDialog *>(QApplication::activeModalWidget());guidedCheck(dialog,"Use the same browser link for portrait")->setChecked(false);setupField<QLineEdit>(dialog,"Landscape browser link")->setText("https://example.invalid/starting");guidedButton(dialog,"Use these overlays")->click();});artworkButton()->click();
		check(wizard->options().artwork.value("guided-starting").toObject().value("vertical").toString().isEmpty(),"custom landscape retains a real built-in portrait fallback");
		startText->clear();check(!wizard->currentPage()->isComplete(),"mixed custom/static screen still requires its fallback message");startText->setText("Portrait fallback");
		QTimer::singleShot(0,[&]{auto *dialog=qobject_cast<QDialog *>(QApplication::activeModalWidget());check(!guidedCheck(dialog,"Use the same browser link for portrait")->isChecked(),"reopening one-format artwork preserves its separate format choice");guidedButton(dialog,"Use these overlays")->click();});artworkButton()->click();
		check(wizard->options().artwork.value("guided-starting").toObject().value("vertical").toString().isEmpty(),"accepting unchanged artwork does not fill the absent format");
		QTimer::singleShot(0,[&]{auto *dialog=qobject_cast<QDialog *>(QApplication::activeModalWidget());guidedCheck(dialog,"Use the same browser link for portrait")->setChecked(true);guidedButton(dialog,"Use these overlays")->click();});artworkButton()->click();
		startText->clear();check(wizard->currentPage()->isComplete(),"fully custom H/V screen does not demand an unused built-in message");
		check(wizard->button(QWizard::CustomButton1)->isVisibleTo(wizard),"editing after Review exposes a clear return action");wizard->button(QWizard::CustomButton1)->click();settleWizard();check(wizard->currentId()==5&&wizard->button(QWizard::FinishButton)->isEnabled(),"review return preserves the repaired custom artwork");
		guidedButton(wizard->page(5),"Change inputs…")->click();settleWizard();setupField<QPushButton>(wizard,"Remove detail input")->click();wizard->next();settleWizard();
		check(guidedViews(wizard->page(2)).size()==3&&!guidedViews(wizard->page(2)).contains("three-up"),"removing an input removes unsupported views instead of leaving blanks");
		check(wizardSourceIdentities()==beforeSources,"six-page planning, preview and repair never create native sources or scenes"); completed=true;wizard->reject();
	});
	PulseGuidedWizard legacy(nullptr, {}); legacy.exec();
	// The draft file itself is a test fixture; no product-generated PNG or state is allowed.
	check(completed&&wizardSourceIdentities()==beforeSources,"task-first wizard completes cancellation without native graph changes");
	check(QCryptographicHash::hash(readFile(storage),QCryptographicHash::Sha256)==beforeFiles.value(QDir(files.path()).relativeFilePath(storage)),"cancelled task-first planning preserves persistent state byte for byte");
	auto afterFiles=generatedFileSnapshot(files.path());afterFiles.remove("Draft-camera-file.png");
	check(afterFiles==beforeFiles,"cancelled task-first planning adds or changes no persisted files beyond the explicit test-owned input image");
	check(!QFileInfo::exists(QDir(QFileInfo(storage).absolutePath()).filePath("show-graphics")),"cancelled task-first wizard creates no generated artwork directory");
	obs_source_remove(content);obs_source_remove(detail);obs_source_remove(card);obs_source_remove(native);obs_source_remove(priorAlert);content=nullptr;detail=nullptr;card=nullptr;obs_wait_for_destroy_queue();
}

static void guidedGeneratedOutputTests(const obs_video_info &initialVideo)
{
	const bool finishOnly=qEnvironmentVariableIsSet("PULSE_GUIDED_FINISH_ONLY");
	obs_video_info video = initialVideo; video.base_width = video.output_width = 1920; video.base_height = video.output_height = 1080;
	check(obs_reset_video(&video) == OBS_VIDEO_SUCCESS, "quality fixtures use a real 1920 by 1080 programme canvas");
	obs_video_info verticalVideo = video; verticalVideo.base_width = verticalVideo.output_width = 1080; verticalVideo.base_height = verticalVideo.output_height = 1920;
	OBSCanvasAutoRelease canvas = obs_canvas_create("Pulse Weaver Vertical", &verticalVideo, PROGRAM); check(bool(canvas), "quality fixtures use a real 1080 by 1920 portrait canvas");
	QTemporaryDir inputs; check(inputs.isValid(), "quality source-image directory");
	const QString cameraFile = labelledInput(inputs.path(), "PRESENTER 4x3", {1200, 900}, QColor("#533469"));
	const QString contentFile = labelledInput(inputs.path(), "GAME HUD 16x9", {1920, 1080}, QColor("#244862"));
	const QString wideFile = labelledInput(inputs.path(), "SLIDES ULTRAWIDE", {2520, 1080}, QColor("#37573d"));
	const QString detailFile = labelledInput(inputs.path(), "DETAIL PORTRAIT", {900, 1600}, QColor("#673b35"));
	auto image = [](const QString &name, const QString &file) {
		OBSDataAutoRelease settings = obs_data_create(); obs_data_set_string(settings, "file", file.toUtf8().constData());
		OBSSourceAutoRelease created = obs_source_create("image_source", name.toUtf8().constData(), settings, nullptr);
		return OBSSource(created);
	};
	OBSSource camera = image("Quality presenter", cameraFile), content = image("Quality game", contentFile), detail = image("Quality detail", detailFile), wide = image("Quality shared screen", wideFile);
	check(camera && content && detail && wide, "quality source images become actual native image inputs");
	OBSDataAutoRelease callSettings = obs_data_create(); obs_data_set_int(callSettings, "width", 1920); obs_data_set_int(callSettings, "height", 1080);
	obs_data_set_string(callSettings, "window", "audit-call-window-no-device");
	OBSSourceAutoRelease call = obs_source_create("window_capture", "Quality guest call window", callSettings, nullptr); check(bool(call), "guest-call case uses an existing inert window capture");
	const QString cameraId = obs_source_get_uuid(camera), contentId = obs_source_get_uuid(content), detailId = obs_source_get_uuid(detail), wideId = obs_source_get_uuid(wide), callId = obs_source_get_uuid(call);
	std::vector<OBSSource> alerts;
	for (int layer : {1, 3}) for (bool portrait : {false, true}) {
		OBSDataAutoRelease settings = obs_data_create(); obs_data_set_int(settings, "width", portrait ? 1080 : 1920); obs_data_set_int(settings, "height", portrait ? 1920 : 1080);
		const QString url = QString("https://alerts.invalid/inert/%1/%2").arg(layer).arg(portrait ? "vertical" : "horizontal"); obs_data_set_string(settings, "url", url.toUtf8().constData());
		const QString name = QString("Quality alert %1 %2").arg(layer).arg(portrait ? "portrait" : "landscape");
		OBSSourceAutoRelease source = obs_source_create("browser_source", name.toUtf8().constData(), settings, nullptr); check(bool(source), "existing inert alert source creates");
		obs_source_set_audio_mixers(source, portrait ? 0 : 5); obs_source_set_volume(source, .37f); obs_source_set_muted(source, true); alerts.emplace_back(source);
	}
	QMap<QString, QJsonObject> preserved;
	for (const auto &source : auditSources()) if (!obs_scene_from_source(source)) preserved.insert(obs_source_get_uuid(source), preservedInputState(source));
	const QString evidence = qEnvironmentVariable("PULSE_GUIDED_OUTPUT_EVIDENCE_DIR");
	if (!evidence.isEmpty()) check(QDir().mkpath(evidence), "quality evidence directory is available");
	if(!finishOnly)staticSetupCardPixelTests();
	QVector<PulseSetupPlan::Options> cases;
	auto add = [&](const QString &name, const QString &activity, const PulseSetupPlan::Sources &sources, const QSet<QString> &views) -> PulseSetupPlan::Options & {
		PulseSetupPlan::Options options; options.name = name; options.activity = activity; options.sources = sources; options.views = views;
		options.starting = options.brb = options.ending = false; cases.append(options); return cases.last();
	};
	add("QA 01 game only", "gaming", {{}, contentId, {}}, {"content"});
	add("QA 02 game and presenter", "gaming", {cameraId, contentId, {}}, {"content", "content-camera", "camera"});
	add("QA 03 conversation", "talk", {cameraId, {}, {}}, {"camera"}).cropCamera = true;
	add("QA 04 reaction and slides", "talk", {cameraId, wideId, {}}, {"content", "content-camera", "camera"});
	add("QA 05 making and detail", "creative", {{}, contentId, detailId}, {"content", "content-detail", "detail"});
	add("QA 06 guest call", "guests", {cameraId, callId, {}}, {"content", "content-camera", "camera"});
	auto &full = add("QA 07 three feeds and alerts", "creative", {cameraId, contentId, detailId}, {"content", "content-camera", "camera", "detail", "two-cameras", "three-up", "content-detail"});
	full.starting = full.brb = full.ending = true; full.startingText = "STARTING SHORTLY"; full.breakText = "BACK IN FIVE"; full.endingText = "THANK YOU, EVERYONE";
	full.newSources.insert("alert-two-h", QJsonObject{{"kind", "browser_source"}, {"name", "Quality new alert two landscape"}, {"url", "https://alerts.invalid/new/landscape"}});
	full.newSources.insert("alert-two-v", QJsonObject{{"kind", "browser_source"}, {"name", "Quality new alert two portrait"}, {"url", "https://alerts.invalid/new/portrait"}});
	full.alerts = {{true, QString::fromUtf8(obs_source_get_uuid(alerts[0])), QString::fromUtf8(obs_source_get_uuid(alerts[1])), "all"},
		{true, "new:alert-two-h", "new:alert-two-v", "main"}, {true, QString::fromUtf8(obs_source_get_uuid(alerts[2])), QString::fromUtf8(obs_source_get_uuid(alerts[3])), "all"}};
	auto &cards = add("QA 08 no integration cards", "talk", {cameraId, {}, {}}, {"camera"});
	cards.starting = cards.brb = cards.ending = true; cards.startingText = "Welcome to my studio"; cards.breakText = "A quick break"; cards.endingText = "See you next time";
	auto &mixed = add("QA 09 mixed custom artwork", "talk", {cameraId, {}, {}}, {"camera"}); mixed.starting = true;
	mixed.artwork.insert("guided-starting", QJsonObject{{"horizontal", QString::fromUtf8(obs_source_get_uuid(alerts[0]))}});
	auto &responsive = add("QA 10 responsive artwork", "talk", {cameraId, {}, {}}, {"camera"}); responsive.starting = true;
	responsive.newSources.insert("responsive", QJsonObject{{"kind", "browser_source"}, {"name", "Quality responsive starting"}, {"url", "https://artwork.invalid/responsive"}});
	responsive.artwork.insert("guided-starting", QJsonObject{{"horizontal", "new:responsive"}, {"vertical", "new:responsive"}});
	auto &fresh = add("QA 11 reusable source drafts", "gaming", {"new:presenter", "new:game", {}}, {"content", "content-camera", "camera"});
	fresh.starting = fresh.brb = fresh.ending = true;
	fresh.newSources = {{"presenter", QJsonObject{{"kind", "image_source"}, {"name", "Quality drafted presenter"}, {"target", cameraFile}}},
		{"game", QJsonObject{{"kind", "image_source"}, {"name", "Quality drafted content"}, {"target", contentFile}}}};
	auto &oneAlert = add("QA 12 one landscape alert", "gaming", {{}, contentId, {}}, {"content"});
	oneAlert.alerts = {{true, QString::fromUtf8(obs_source_get_uuid(alerts[0])), {}, "main"}};
	auto &twoAlerts = add("QA 13 two scoped alerts", "talk", {cameraId, {}, {}}, {"camera"}); twoAlerts.brb = true;
	twoAlerts.alerts = {{true, QString::fromUtf8(obs_source_get_uuid(alerts[0])), QString::fromUtf8(obs_source_get_uuid(alerts[1])), "main"},
		{true, QString::fromUtf8(obs_source_get_uuid(alerts[2])), QString::fromUtf8(obs_source_get_uuid(alerts[3])), "all"}};
	add("QA 14 explicit detail crop", "creative", {{}, wideId, detailId}, {"content", "content-detail", "detail"}).cropDetail = true;
	auto &browser = add("QA 15 reusable browser content", "talk", {cameraId,"new:shared-browser",{}},{"content","content-camera","camera"});
	browser.newSources.insert("shared-browser",QJsonObject{{"kind","browser_source"},{"name","Quality browser content"},{"url","https://example.invalid/shared-content"}});
	if(!finishOnly)for (const auto &options : cases) {
		QTemporaryDir directory; check(QDir().mkpath(directory.filePath("plugin_config/pulse-weaver-core")), "guided scenario private storage directory");
		const QString storage = directory.filePath("plugin_config/pulse-weaver-core/motion.json"), stages = directory.filePath("pulseweaver-stages.json"); writeFile(stages, "[]");
		const auto baseline = wizardSourceIdentities();
		{
			PulseMotionEngine engine(nullptr, storage);
			const QString problem = PulseSetupPlan::problem(options); check(problem.isEmpty(), qPrintable(options.name + " must be a complete task plan: " + problem));
			const auto request = PulseSetupPlan::draft(options); check(!request.isEmpty(), "complete task plan produces a draft");
			const auto result = engine.createGuidedShow(request); check(result.value("ok").toBool(), qPrintable(options.name + " generated show failed: " + result.value("message").toString()));
			guidedOutputEvidence.append(checkGuidedOutput(engine, options, request, baseline));
			for (auto it = preserved.cbegin(); it != preserved.cend(); ++it) {
				OBSSourceAutoRelease source = obs_get_source_by_uuid(it.key().toUtf8().constData()); check(source && preservedInputState(source) == it.value(), "creating a useful show never changes reused input settings, mixers, mute, volume or monitor mode");
			}
			for (const QString &id : {options.sources.camera, options.sources.content, options.sources.detail}) if (id.startsWith("new:")) {
				const auto spec=options.newSources.value(id.mid(4)).toObject();const QString name=spec.value("name").toString();
				for(const QString &route:spec.value("kind")=="browser_source"?QStringList{"horizontal","vertical"}:QStringList{QString()}) {
					const QString actualName=route.isEmpty()?name:name+" · "+route;int matches=0;
					for(const auto &source:auditSources())if(QString::fromUtf8(obs_source_get_name(source))==actualName)++matches;
					check(matches==1,"each drafted common input creates once per required viewport and is shared across views and Stages");
				}
			}
		}
		removeAuditAdditions(baseline);
	}
	// Bypass the planner to prove the backend also rejects invalid guided graphs
	// before making any source, scene, generated image or catalogue change.
	if(!finishOnly)for (const QString &fault : {QString("missing-third"), QString("duplicate-third"), QString("camera-alert"), QString("card-feed"), QString("fourth-alert"), QString("invalid-card")}) {
		QTemporaryDir directory; check(QDir().mkpath(directory.filePath("plugin_config/pulse-weaver-core")), "invalid guided request storage");
		const QString storage = directory.filePath("plugin_config/pulse-weaver-core/motion.json"), stagePath = directory.filePath("pulseweaver-stages.json"); writeFile(stagePath, "[]");
		PulseMotionEngine engine(nullptr, storage); PulseSetupPlan::Options options;
		options.name = "QA reject " + fault; options.activity = "creative"; options.sources = {cameraId, contentId, detailId}; options.views = {"three-up"};
		options.starting = options.brb = options.ending = false;
		auto request = PulseSetupPlan::draft(options); check(!request.isEmpty(), "negative backend test begins with a valid complete generated plan");
		auto stages = request.value("stages").toArray(); auto stage = stages.first().toObject(); auto roles = stage.value("roles").toObject();
		if (fault == "missing-third") roles.remove("feedC");
		if (fault == "duplicate-third") roles.insert("feedC", roles.value("feedA"));
		if (fault == "card-feed") roles.insert("feedA", stage.value("background"));
		stage.insert("roles", roles); stages[0] = stage; request.insert("stages", stages);
		if (fault == "camera-alert" || fault == "fourth-alert") request.insert("overlays", QJsonArray{QJsonObject{{"canvas", "horizontal"}, {"order", fault == "fourth-alert" ? 4 : 1},
			{"source", fault == "camera-alert" ? cameraId : QString::fromUtf8(obs_source_get_uuid(alerts[0]))}, {"looks", QJsonArray{"guided-main/three-up"}}}});
		if (fault == "invalid-card") {
			auto definitions = request.value("newSources").toObject(); const QString key = stage.value("background").toString().mid(4); auto card = definitions.value(key).toObject();
			card.insert("style", "unknown-style"); definitions.insert(key, card); request.insert("newSources", definitions);
		}
		const auto beforeSources = wizardSourceIdentities(); const auto beforeFiles = generatedFileSnapshot(directory.path()); const auto beforeActions = PulseMotionEngineTestAccess::actions(engine);
		check(!engine.createGuidedShow(request).value("ok").toBool(), qPrintable("invalid generated request rejects " + fault));
		check(wizardSourceIdentities() == beforeSources && generatedFileSnapshot(directory.path()) == beforeFiles && PulseMotionEngineTestAccess::actions(engine) == beforeActions,
			"invalid guided request is rejected before sources, PNG files or persisted catalogues mutate");
	}
	if(!finishOnly)for (bool preexistingArtwork : {false, true}) {
		QTemporaryDir directory; const QString parent = directory.filePath("plugin_config/pulse-weaver-core"); check(QDir().mkpath(parent), "commit-failure fixture parent");
		const QString storage = QDir(parent).filePath("motion.json"), assetDirectory = QDir(parent).filePath("show-graphics"), stagePath = directory.filePath("pulseweaver-stages.json"); writeFile(stagePath, "[]");
		if (preexistingArtwork) { check(QDir().mkpath(assetDirectory), "preexisting artwork directory"); writeFile(QDir(assetDirectory).filePath("retained-artwork.bin"), "preserve this user-owned artwork exactly"); }
		PulseMotionEngine engine(nullptr, storage);
		// Construct with a valid absent store, then make the commit destination a
		// directory. This forces save failure after real card PNG/source creation.
		check(QDir().mkpath(storage), "read-only commit destination is represented by an owned directory blocker");
		PulseSetupPlan::Options options; options.name = preexistingArtwork ? "QA rollback retained artwork" : "QA rollback new cards";
		options.activity = "talk"; options.sources.camera = cameraId; options.views = {"camera"}; options.starting = options.brb = options.ending = true;
		const auto beforeSources = wizardSourceIdentities();
		const auto beforeActions = PulseMotionEngineTestAccess::actions(engine);
		const auto beforeFiles = generatedFileSnapshot(assetDirectory); const auto beforeStages = readFile(stagePath); const int imagesBefore = imageFixtureCreates.load();
		const auto result = engine.createGuidedShow(PulseSetupPlan::draft(options));
		check(!result.value("ok").toBool() && result.value("message").toString().contains("save", Qt::CaseInsensitive) && imageFixtureCreates.load() > imagesBefore,
			"forced commit failure occurs after actual generated PNG inputs were created");
		obs_wait_for_destroy_queue();
		check(wizardSourceIdentities() == beforeSources && PulseMotionEngineTestAccess::actions(engine) == beforeActions && readFile(stagePath) == beforeStages,
			"failed generated-show commit rolls back both canvas scenes, inputs, in-memory actions and persistent Stages");
		check(generatedFileSnapshot(assetDirectory) == beforeFiles && QDir(assetDirectory).exists() == preexistingArtwork,
			"failed generated-show commit removes its new PNG files while retaining existing artwork and directory identity");
	}
	for (const auto &source : alerts) obs_source_remove(source);
	obs_source_remove(call); obs_source_remove(camera); obs_source_remove(content); obs_source_remove(detail); obs_source_remove(wide);
	obs_canvas_remove(canvas); obs_wait_for_destroy_queue();
	obs_video_info restored = initialVideo;
	check(obs_reset_video(&restored) == OBS_VIDEO_SUCCESS, "quality fixture restores the initial inert video dimensions");
}

#include "select-wizard-tests.hpp"
#include "reflow-motion-tests.hpp"

int main(int argc, char **argv)
{
	QApplication application(argc, argv);
	WizardCaptureWindows captureWindows;
	if (qEnvironmentVariableIsSet("PULSE_WIZARD_EVIDENCE_DIR")) application.installEventFilter(&captureWindows);
	const QString stylesheet = qEnvironmentVariable("PULSE_WIZARD_TEST_QSS");
	if (!stylesheet.isEmpty()) application.setStyleSheet(QString::fromUtf8(readFile(stylesheet)));
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
	registerInertImageAndCaptureInputs();
	if (qEnvironmentVariableIsSet("PULSE_REFLOW_ONLY")) {
		reflowMotionTests();
	} else if (qEnvironmentVariableIsSet("PULSE_GUIDED_OUTPUT_ONLY")||qEnvironmentVariableIsSet("PULSE_GUIDED_FINISH_ONLY")) {
		guidedGeneratedOutputTests(video);
	} else if (qEnvironmentVariableIsSet("PULSE_SELECT_ONLY")) {
		selectionFirstTests(video);
	} else if (qEnvironmentVariableIsSet("PULSE_GUIDED_UI_ONLY")) {
		selectionFirstTests(video);
		guidedWizardInteractionTests();
	} else if (qEnvironmentVariableIsSet("PULSE_WIZARD_ONLY")) {
		OBSDataAutoRelease settings = obs_data_create();
		obs_data_set_int(settings, "width", 1920); obs_data_set_int(settings, "height", 1080);
		obs_data_set_string(settings, "url", "https://overlays.lumiastream.com/audit/inert-landscape");
		OBSSourceAutoRelease horizontal = obs_source_create("browser_source", "Inert landscape artwork", settings, nullptr);
		OBSDataAutoRelease portraitSettings = obs_data_create();
		obs_data_set_int(portraitSettings, "width", 1080); obs_data_set_int(portraitSettings, "height", 1920);
		obs_data_set_string(portraitSettings, "url", "https://overlays.lumiastream.com/audit/inert-portrait");
		OBSSourceAutoRelease vertical = obs_source_create("browser_source", "Inert portrait artwork", portraitSettings, nullptr);
		setupWizardRoundTripTests(horizontal, vertical);
		obs_source_remove(horizontal); obs_source_remove(vertical);
	} else {
		std::fprintf(stderr, "Motion audit: reflow geometry and movement\n"); reflowMotionTests(); obs_wait_for_destroy_queue();
		wizardOverlayPolicyTests();
		std::fprintf(stderr, "Motion audit: storage preservation\n"); storageTests();
		std::fprintf(stderr, "Motion audit: scene isolation\n"); sceneTests(); obs_wait_for_destroy_queue();
		std::fprintf(stderr, "Motion audit: reference builder\n"); referenceBuilderTests(video); obs_wait_for_destroy_queue();
		std::fprintf(stderr, "Motion audit: setup wizard\n"); setupBuilderTests(video); obs_wait_for_destroy_queue();
		std::fprintf(stderr, "Motion audit: themed builder\n"); themedBuilderTests(video); obs_wait_for_destroy_queue();
		std::fprintf(stderr, "Motion audit: selection-first outputs and Finish\n"); selectionFirstTests(video);
		std::fprintf(stderr, "Motion audit: generated task-first outputs\n"); guidedGeneratedOutputTests(video);
		std::fprintf(stderr, "Motion audit: task-first interaction and geometry\n"); guidedWizardInteractionTests();
	}
	const QString evidence = qEnvironmentVariable("PULSE_WIZARD_EVIDENCE_DIR");
	if (!evidence.isEmpty()) writeFile(QDir(evidence).filePath("wizard-layout-state-evidence.json"), QJsonDocument(wizardRenderEvidence).toJson());
	const QString outputEvidence = qEnvironmentVariable("PULSE_GUIDED_OUTPUT_EVIDENCE_DIR");
	if (!outputEvidence.isEmpty()) writeFile(QDir(outputEvidence).filePath("generated-output-state.json"), QJsonDocument(guidedOutputEvidence).toJson());
	obs_wait_for_destroy_queue();
	obs_shutdown();
	check(runtimeErrors == 0, "no unexpected OBS runtime errors");
	check(wizardGeometryFailures == 0, "all wizard geometry, visible utilities and themed primary controls pass at normal/constrained/minimum sizes");
	std::printf("PASS: %d motion preservation checks; no platform calls or installed settings used.\n", checks);
}
