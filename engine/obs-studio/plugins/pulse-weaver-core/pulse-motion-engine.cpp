#include "pulse-motion-engine.hpp"

#include "pulse-scene-item-ref.hpp"

#include <obs.h>

#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSaveFile>
#include <QSpinBox>
#include <QSplitter>
#include <QTreeWidget>
#include <QUuid>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <utility>

namespace {

double finiteNumber(const QJsonValue &value, double fallback = 0.0)
{
	const double number = value.toDouble(fallback);
	return std::isfinite(number) ? number : fallback;
}

double smoothStep(double value)
{
	value = std::clamp(value, 0.0, 1.0);
	return value * value * (3.0 - 2.0 * value);
}

QString cleanName(QString name, const QString &fallback)
{
	name = name.trimmed();
	return name.isEmpty() ? fallback : name.left(120);
}

QString rawStageName(const QComboBox *selector, int index)
{
	if (!selector || index < 0 || index >= selector->count())
		return {};
	const QString stored = selector->itemData(index, Qt::UserRole + 1).toString();
	return stored.isEmpty() ? selector->itemText(index).section("  ·  ", 0, 0) : stored;
}

QJsonObject jsonObject(const QJsonValue &value)
{
	return value.isObject() ? value.toObject() : QJsonObject{};
}

} // namespace

PulseMotionEngine::PulseMotionEngine(QObject *parent, QString path, EventCallback callback)
	: QObject(parent), storagePath(std::move(path)), eventCallback(std::move(callback))
{
	load();
	syncHotkeys();
	animationTimer.setTimerType(Qt::PreciseTimer);
	animationTimer.setInterval(16);
	connect(&animationTimer, &QTimer::timeout, this, [this] { tick(); });
	stageTimer.setSingleShot(true);
	connect(&stageTimer, &QTimer::timeout, this, [this] { beginAfterStage(); });
	if (auto *selector = stageSelector()) {
		connect(selector, &QComboBox::currentIndexChanged, this, [this](int index) {
			if (index == expectedStageIndex) {
				expectedStageIndex = -1;
				return;
			}
			cancelForManualStageChange();
			populateStages();
		});
	}
}

PulseMotionEngine::~PulseMotionEngine()
{
	animationTimer.stop();
	stageTimer.stop();
	if (active && active->phase != "changing_stage") {
		for (Track &track : active->tracks)
			if (track.item) apply(track.item, track.baseline, true);
	}
	active.reset();
	for (quint64 id : std::as_const(hotkeys))
		obs_hotkey_unregister(obs_hotkey_id(id));
	hotkeys.clear();
	hotkeyActions.clear();
}

void PulseMotionEngine::load()
{
	QFile file(storagePath);
	if (!file.open(QIODevice::ReadOnly))
		return;
	QJsonParseError error;
	const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
	if (error.error != QJsonParseError::NoError || !document.isObject())
		return;
	const QJsonObject root = document.object();
	if (root.value("version").toInt() == 1 && root.value("actions").isArray())
		actions = root.value("actions").toArray();
}

void PulseMotionEngine::save()
{
	QDir().mkpath(QFileInfo(storagePath).absolutePath());
	QSaveFile file(storagePath);
	if (!file.open(QIODevice::WriteOnly)) {
		setStatus("Could not save motion actions: " + file.errorString(), true);
		return;
	}
	file.write(QJsonDocument(QJsonObject{{"version", 1}, {"actions", actions}}).toJson(QJsonDocument::Indented));
	if (!file.commit()) {
		setStatus("Could not finish saving motion actions.", true);
		return;
	}
	syncHotkeys();
}

void PulseMotionEngine::syncHotkeys()
{
	QSet<QString> wanted;
	for (const QJsonValue &value : actions) {
		const QJsonObject action = value.toObject();
		const QString id = action.value("id").toString();
		if (!id.isEmpty() && !action.value("draft").toBool()) wanted.insert(id);
	}
	for (auto iterator = hotkeys.begin(); iterator != hotkeys.end();) {
		if (wanted.contains(iterator.key())) {
			++iterator;
			continue;
		}
		obs_hotkey_unregister(obs_hotkey_id(iterator.value()));
		hotkeyActions.remove(iterator.value());
		iterator = hotkeys.erase(iterator);
	}
	for (const QJsonValue &value : actions) {
		const QJsonObject action = value.toObject();
		const QString actionId = action.value("id").toString();
		if (!wanted.contains(actionId) || hotkeys.contains(actionId)) continue;
		const QByteArray key = ("PulseWeaver.Motion." + actionId).toUtf8();
		const QByteArray description = ("Pulse Weaver Motion: " + action.value("name").toString()).toUtf8();
		const obs_hotkey_id registered = obs_hotkey_register_frontend(key.constData(), description.constData(),
			&PulseMotionEngine::hotkeyTriggered, this);
		if (registered == OBS_INVALID_HOTKEY_ID) continue;
		hotkeys.insert(actionId, quint64(registered));
		hotkeyActions.insert(quint64(registered), actionId);
	}
}

void PulseMotionEngine::hotkeyTriggered(void *data, obs_hotkey_id id, obs_hotkey_t *, bool pressed)
{
	if (!pressed || !data) return;
	auto *engine = static_cast<PulseMotionEngine *>(data);
	const QString actionId = engine->hotkeyActions.value(quint64(id));
	if (actionId.isEmpty()) return;
	QPointer<PulseMotionEngine> guarded(engine);
	QMetaObject::invokeMethod(engine, [guarded, actionId] {
		if (guarded) guarded->runAction(actionId);
	}, Qt::QueuedConnection);
}

QWidget *PulseMotionEngine::createEditor(QWidget *parent)
{
	if (editor)
		return editor;
	editor = new QWidget(parent);
	auto *root = new QVBoxLayout(editor);
	root->setContentsMargins(8, 8, 8, 8);
	root->setSpacing(8);

	auto *intro = new QLabel("Create one named action, then run it from Show Control, Lumia Stream, LumiCon, Stream Deck or a hotkey.");
	intro->setWordWrap(true);
	intro->setObjectName("Muted");
	root->addWidget(intro);

	auto *splitter = new QSplitter;
	root->addWidget(splitter, 1);
	auto *libraryPage = new QWidget;
	auto *libraryLayout = new QVBoxLayout(libraryPage);
	libraryLayout->setContentsMargins(0, 0, 0, 0);
	libraryLayout->addWidget(new QLabel("SAVED ACTIONS"));
	actionList = new QListWidget;
	actionList->setMinimumWidth(210);
	libraryLayout->addWidget(actionList, 1);
	auto *newRow = new QHBoxLayout;
	auto *newPunch = new QPushButton("NEW CLOSE-UP");
	auto *newLayout = new QPushButton("NEW LAYOUT");
	newRow->addWidget(newPunch);
	newRow->addWidget(newLayout);
	libraryLayout->addLayout(newRow);
	auto *import = new QPushButton("IMPORT MOVE / LUMIA JSON…");
	libraryLayout->addWidget(import);
	splitter->addWidget(libraryPage);

	auto *details = new QWidget;
	auto *detailsLayout = new QVBoxLayout(details);
	detailsLayout->setContentsMargins(8, 0, 0, 0);
	auto *form = new QFormLayout;
	nameField = new QLineEdit;
	nameField->setPlaceholderText("For example: Camera close-up");
	kindField = new QComboBox;
	kindField->addItem("Zoom inside a camera frame", "punch");
	kindField->addItem("Move to a saved layout", "layout");
	policyField = new QComboBox;
	policyField->addItem("Switch to this Stage, then run", "switch");
	policyField->addItem("Only run when this Stage is showing", "only");
	policyField->addItem("Use the current Stage", "current");
	stageField = new QComboBox;
	sceneField = new QComboBox;
	sourceField = new QComboBox;
	zoomField = new QSpinBox;
	zoomField->setRange(100, 400);
	zoomField->setValue(150);
	zoomField->setSuffix(" %");
	durationField = new QSpinBox;
	durationField->setRange(0, 15000);
	durationField->setValue(750);
	durationField->setSuffix(" ms");
	holdField = new QSpinBox;
	holdField->setRange(0, 60000);
	holdField->setValue(5000);
	holdField->setSuffix(" ms");
	restoreField = new QCheckBox("Restore the original framing when finished");
	restoreField->setChecked(true);
	returnStageField = new QCheckBox("Return to the previous Stage afterwards");
	form->addRow("Action name", nameField);
	form->addRow("What should happen?", kindField);
	form->addRow("Stage behaviour", policyField);
	form->addRow("Assigned Stage", stageField);
	form->addRow("Scene or group", sceneField);
	form->addRow("Camera / source", sourceField);
	form->addRow("How close?", zoomField);
	form->addRow("Movement time", durationField);
	form->addRow("Stay close for", holdField);
	form->addRow("", restoreField);
	form->addRow("", returnStageField);
	detailsLayout->addLayout(form);

	itemTree = new QTreeWidget;
	itemTree->setHeaderLabels({"CONTROL", "SOURCE", "CURRENT STATE"});
	itemTree->header()->setSectionResizeMode(1, QHeaderView::Stretch);
	itemTree->setMinimumHeight(145);
	detailsLayout->addWidget(itemTree);
	summaryLabel = new QLabel;
	summaryLabel->setWordWrap(true);
	summaryLabel->setObjectName("Muted");
	detailsLayout->addWidget(summaryLabel);
	auto *buttons = new QHBoxLayout;
	saveButton = new QPushButton("SAVE ACTION");
	saveButton->setObjectName("Primary");
	auto *run = new QPushButton("RUN / PREVIEW ON OUTPUT");
	auto *stop = new QPushButton("STOP + RESTORE");
	auto *remove = new QPushButton("DELETE");
	buttons->addWidget(saveButton);
	buttons->addWidget(run);
	buttons->addWidget(stop);
	buttons->addStretch();
	buttons->addWidget(remove);
	detailsLayout->addLayout(buttons);
	statusLabel = new QLabel("Choose an action or create one. Running changes the real output; editing does not.");
	statusLabel->setWordWrap(true);
	statusLabel->setObjectName("Muted");
	detailsLayout->addWidget(statusLabel);
	splitter->addWidget(details);
	splitter->setStretchFactor(1, 1);

	connect(newPunch, &QPushButton::clicked, this, [this] {
		editingId.clear();
		loadActionIntoEditor(QJsonObject{{"kind", "punch"}, {"name", "Camera close-up"}, {"policy", "switch"},
			{"zoomPercent", 150}, {"durationMs", 250}, {"holdMs", 5000}, {"restore", true}});
	});
	connect(newLayout, &QPushButton::clicked, this, [this] {
		editingId.clear();
		loadActionIntoEditor(QJsonObject{{"kind", "layout"}, {"name", "New layout"}, {"policy", "switch"},
			{"durationMs", 750}, {"restore", false}});
	});
	connect(import, &QPushButton::clicked, this, [this] { importFromFile(); });
	connect(actionList, &QListWidget::currentItemChanged, this, [this](QListWidgetItem *item) {
		if (!item) return;
		loadActionIntoEditor(actionByIdentity(item->data(Qt::UserRole).toString()));
	});
	connect(kindField, &QComboBox::currentIndexChanged, this, [this] { populateItems(); refreshSummary(); });
	connect(policyField, &QComboBox::currentIndexChanged, this, [this] { refreshSummary(); });
	connect(stageField, &QComboBox::currentIndexChanged, this, [this] { refreshSummary(); });
	connect(sceneField, &QComboBox::currentIndexChanged, this, [this] { populateSources(); populateItems(); refreshSummary(); });
	connect(sourceField, &QComboBox::currentIndexChanged, this, [this] { refreshSummary(); });
	connect(zoomField, &QSpinBox::valueChanged, this, [this] { refreshSummary(); });
	connect(durationField, &QSpinBox::valueChanged, this, [this] { refreshSummary(); });
	connect(holdField, &QSpinBox::valueChanged, this, [this] { refreshSummary(); });
	connect(restoreField, &QCheckBox::toggled, this, [this] { refreshSummary(); });
	connect(returnStageField, &QCheckBox::toggled, this, [this] { refreshSummary(); });
	connect(saveButton, &QPushButton::clicked, this, [this] { saveEditorAction(); });
	connect(remove, &QPushButton::clicked, this, [this] { deleteEditorAction(); });
	connect(run, &QPushButton::clicked, this, [this] {
		if (editingId.isEmpty()) saveEditorAction();
		if (editingId.isEmpty()) return;
		const QJsonObject result = runAction(editingId);
		setStatus(result.value("message").toString(), !result.value("ok").toBool());
	});
	connect(stop, &QPushButton::clicked, this, [this] {
		const QJsonObject result = stopAction({}, true);
		setStatus(result.value("message").toString(), !result.value("ok").toBool());
	});

	refreshEditor();
	if (actions.isEmpty())
		newPunch->click();
	return editor;
}

void PulseMotionEngine::refreshEditor()
{
	populateStages();
	populateScenes();
	if (actionList) {
		const QString selected = editingId;
		actionList->clear();
		for (const QJsonValue &value : actions) {
			const QJsonObject action = value.toObject();
			auto *item = new QListWidgetItem(action.value("name").toString("Unnamed action"), actionList);
			item->setData(Qt::UserRole, action.value("id").toString());
			item->setToolTip(action.value("summary").toString());
			if (action.value("draft").toBool()) item->setText(item->text() + "  ·  REVIEW");
			if (action.value("id").toString() == selected) actionList->setCurrentItem(item);
		}
	}
	populateSources();
	populateItems();
	refreshSummary();
}

QJsonArray PulseMotionEngine::sceneCatalogue() const
{
	QJsonArray result;
	QSet<QString> known;
	obs_frontend_source_list scenes{};
	obs_frontend_get_scenes(&scenes);
	for (size_t index = 0; index < scenes.sources.num; ++index) {
		obs_source_t *source = scenes.sources.array[index];
		const QString name = QString::fromUtf8(obs_source_get_name(source));
		if (!name.isEmpty() && !known.contains(name)) {
			known.insert(name);
			result.append(QJsonObject{{"name", name}, {"kind", "scene"}});
		}
	}
	obs_frontend_source_list_free(&scenes);
	QPair<QJsonArray *, QSet<QString> *> context(&result, &known);
	obs_enum_all_sources([](void *data, obs_source_t *source) {
		auto *pair = static_cast<QPair<QJsonArray *, QSet<QString> *> *>(data);
		if (!obs_group_from_source(source)) return true;
		const QString name = QString::fromUtf8(obs_source_get_name(source));
		if (!name.isEmpty() && !pair->second->contains(name)) {
			pair->second->insert(name);
			pair->first->append(QJsonObject{{"name", name}, {"kind", "group"}});
		}
		return true;
	}, &context);
	return result;
}

QJsonArray PulseMotionEngine::itemCatalogue(const QString &container) const
{
	QJsonArray result;
	obs_source_t *source = obs_get_source_by_name(container.toUtf8().constData());
	obs_scene_t *scene = source ? obs_scene_from_source(source) : nullptr;
	if (!scene && source) scene = obs_group_from_source(source);
	if (scene) {
		obs_scene_enum_items(scene, [](obs_scene_t *, obs_sceneitem_t *item, void *data) {
			auto *array = static_cast<QJsonArray *>(data);
			obs_source_t *source = obs_sceneitem_get_source(item);
			const uint32_t flags = source ? obs_source_get_output_flags(source) : 0;
			if (source && (flags & OBS_SOURCE_VIDEO)) {
				array->append(QJsonObject{{"itemId", QString::number(obs_sceneitem_get_id(item))},
					{"source", QString::fromUtf8(obs_source_get_name(source))},
					{"visible", obs_sceneitem_visible(item)},
					{"locked", obs_sceneitem_locked(item)},
					{"group", obs_sceneitem_is_group(item)}});
			}
			return true;
		}, &result);
	}
	obs_source_release(source);
	return result;
}

void PulseMotionEngine::populateStages()
{
	if (!stageField) return;
	const QString selected = stageField->currentData().toString();
	stageField->blockSignals(true);
	stageField->clear();
	if (auto *selector = stageSelector()) {
		for (int index = 0; index < selector->count(); ++index) {
			const QString name = rawStageName(selector, index);
			stageField->addItem(name, name);
		}
	}
	const int row = stageField->findData(selected);
	if (row >= 0) stageField->setCurrentIndex(row);
	stageField->blockSignals(false);
}

void PulseMotionEngine::populateScenes()
{
	if (!sceneField) return;
	const QString selected = sceneField->currentData().toString();
	sceneField->blockSignals(true);
	sceneField->clear();
	for (const QJsonValue &value : sceneCatalogue()) {
		const QJsonObject row = value.toObject();
		const QString name = row.value("name").toString();
		sceneField->addItem(name + (row.value("kind") == "group" ? "  ·  group" : ""), name);
	}
	int row = sceneField->findData(selected);
	if (row < 0) {
		OBSSourceAutoRelease current = obs_frontend_get_current_scene();
		row = current ? sceneField->findData(QString::fromUtf8(obs_source_get_name(current))) : -1;
	}
	if (row >= 0) sceneField->setCurrentIndex(row);
	sceneField->blockSignals(false);
}

void PulseMotionEngine::populateSources()
{
	if (!sourceField || !sceneField) return;
	const QString selected = sourceField->currentData().toString();
	sourceField->blockSignals(true);
	sourceField->clear();
	for (const QJsonValue &value : itemCatalogue(sceneField->currentData().toString())) {
		const QJsonObject row = value.toObject();
		const QString encoded = row.value("itemId").toString() + "|" + row.value("source").toString();
		sourceField->addItem(row.value("source").toString(), encoded);
	}
	const int row = sourceField->findData(selected);
	if (row >= 0) sourceField->setCurrentIndex(row);
	sourceField->blockSignals(false);
}

void PulseMotionEngine::populateItems()
{
	if (!itemTree || !sceneField || !kindField) return;
	QSet<QString> selected;
	for (int row = 0; row < itemTree->topLevelItemCount(); ++row) {
		QTreeWidgetItem *item = itemTree->topLevelItem(row);
		if (item->checkState(0) == Qt::Checked) selected.insert(item->data(0, Qt::UserRole).toString());
	}
	itemTree->clear();
	const bool layout = kindField->currentData().toString() == "layout";
	itemTree->setVisible(layout);
	if (!layout) return;
	for (const QJsonValue &value : itemCatalogue(sceneField->currentData().toString())) {
		const QJsonObject row = value.toObject();
		const QString key = row.value("itemId").toString();
		auto *item = new QTreeWidgetItem(itemTree, {"", row.value("source").toString(),
			row.value("visible").toBool() ? "Shown" : "Hidden"});
		item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
		item->setCheckState(0, selected.contains(key) ? Qt::Checked : Qt::Unchecked);
		item->setData(0, Qt::UserRole, key);
		item->setData(1, Qt::UserRole, row.value("source").toString());
	}
}

QJsonObject PulseMotionEngine::actionByIdentity(const QString &identity) const
{
	for (const QJsonValue &value : actions) {
		const QJsonObject action = value.toObject();
		if (action.value("id").toString() == identity || action.value("name").toString().compare(identity, Qt::CaseInsensitive) == 0)
			return action;
	}
	return {};
}

void PulseMotionEngine::loadActionIntoEditor(const QJsonObject &action)
{
	if (!nameField) return;
	editingId = action.value("id").toString();
	nameField->setText(action.value("name").toString());
	kindField->setCurrentIndex(std::max(0, kindField->findData(action.value("kind").toString("punch"))));
	policyField->setCurrentIndex(std::max(0, policyField->findData(action.value("policy").toString("switch"))));
	const int stageRow = stageField->findData(action.value("stage").toString());
	if (stageRow >= 0) stageField->setCurrentIndex(stageRow);
	const int sceneRow = sceneField->findData(action.value("container").toString(action.value("scene").toString()));
	if (sceneRow >= 0) sceneField->setCurrentIndex(sceneRow);
	populateSources();
	const QString target = action.value("itemId").toString() + "|" + action.value("source").toString();
	const int sourceRow = sourceField->findData(target);
	if (sourceRow >= 0) sourceField->setCurrentIndex(sourceRow);
	zoomField->setValue(action.value("zoomPercent").toInt(150));
	durationField->setValue(action.value("durationMs").toInt(action.value("kind") == "punch" ? 250 : 750));
	holdField->setValue(action.value("holdMs").toInt(5000));
	restoreField->setChecked(action.value("restore").toBool(action.value("kind") == "punch"));
	returnStageField->setChecked(action.value("returnStage").toBool());
	populateItems();
	const QJsonArray items = action.value("items").toArray();
	for (int row = 0; row < itemTree->topLevelItemCount(); ++row) {
		QTreeWidgetItem *treeItem = itemTree->topLevelItem(row);
		const QString id = treeItem->data(0, Qt::UserRole).toString();
		const bool checked = std::any_of(items.begin(), items.end(), [&id](const QJsonValue &value) {
			return value.toObject().value("itemId").toString() == id;
		});
		treeItem->setCheckState(0, checked ? Qt::Checked : Qt::Unchecked);
	}
	refreshSummary();
	setStatus(action.value("draft").toBool() ? "Imported draft: review the targets and press Save Action before running it." : "Editing does not change the live output.");
}

QJsonObject PulseMotionEngine::editorAction() const
{
	QJsonObject action{{"version", 1},
		{"id", editingId.isEmpty() ? QUuid::createUuid().toString(QUuid::WithoutBraces) : editingId},
		{"name", cleanName(nameField ? nameField->text() : QString(), "Unnamed action")},
		{"kind", kindField ? kindField->currentData().toString() : QString("punch")},
		{"policy", policyField ? policyField->currentData().toString() : QString("switch")},
		{"stage", stageField ? stageField->currentData().toString() : QString()},
		{"container", sceneField ? sceneField->currentData().toString() : QString()},
		{"zoomPercent", zoomField ? zoomField->value() : 150},
		{"durationMs", durationField ? durationField->value() : 750},
		{"holdMs", holdField ? holdField->value() : 5000},
		{"restore", restoreField && restoreField->isChecked()},
		{"returnStage", returnStageField && returnStageField->isChecked()},
		{"repeat", "restart"}};
	if (sourceField) {
		const QStringList target = sourceField->currentData().toString().split('|');
		action.insert("itemId", target.value(0));
		action.insert("source", target.mid(1).join("|"));
	}
	QJsonArray items;
	if (action.value("kind") == "layout" && itemTree) {
		const QString container = action.value("container").toString();
		for (int row = 0; row < itemTree->topLevelItemCount(); ++row) {
			QTreeWidgetItem *treeItem = itemTree->topLevelItem(row);
			if (treeItem->checkState(0) != Qt::Checked) continue;
			const qint64 id = treeItem->data(0, Qt::UserRole).toString().toLongLong();
			const QString source = treeItem->data(1, Qt::UserRole).toString();
			OBSSceneItem item = resolveItem(container, id, source, false);
			if (!item) continue;
			items.append(QJsonObject{{"container", container}, {"itemId", QString::number(id)}, {"source", source},
				{"transform", serialize(capture(item))}});
		}
	}
	action.insert("items", items);
	QString summary;
	if (action.value("policy") == "switch") summary = "Switch to " + action.value("stage").toString() + ", then ";
	else if (action.value("policy") == "only") summary = "On " + action.value("stage").toString() + ", ";
	else summary = "On the current Stage, ";
	if (action.value("kind") == "punch")
		summary += "zoom " + action.value("source").toString() + " to " + QString::number(action.value("zoomPercent").toInt()) + "%";
	else
		summary += "move " + QString::number(items.size()) + " source" + (items.size() == 1 ? "" : "s") + " to the saved layout";
	if (action.value("restore").toBool()) summary += ", then restore";
	action.insert("summary", summary + ".");
	return action;
}

void PulseMotionEngine::saveEditorAction()
{
	QJsonObject action = editorAction();
	if (action.value("container").toString().isEmpty()) {
		setStatus("Choose a scene or group first.", true);
		return;
	}
	if (action.value("kind") == "punch" && action.value("source").toString().isEmpty()) {
		setStatus("Choose the camera or source to zoom.", true);
		return;
	}
	if (action.value("kind") == "layout" && action.value("items").toArray().isEmpty()) {
		setStatus("Tick at least one source that this layout may control.", true);
		return;
	}
	action.remove("draft");
	bool replaced = false;
	for (int index = 0; index < actions.size(); ++index) {
		if (actions[index].toObject().value("id") == action.value("id")) {
			actions[index] = action;
			replaced = true;
			break;
		}
	}
	if (!replaced) actions.append(action);
	editingId = action.value("id").toString();
	save();
	refreshEditor();
	setStatus("Saved “" + action.value("name").toString() + "”. It is now available to Lumia and other controllers.");
	emitEvent("motion_catalogue_changed", {{"action", editingId}});
}

void PulseMotionEngine::deleteEditorAction()
{
	if (editingId.isEmpty()) return;
	for (int index = 0; index < actions.size(); ++index) {
		if (actions[index].toObject().value("id") == editingId) {
			actions.removeAt(index);
			break;
		}
	}
	editingId.clear();
	save();
	refreshEditor();
	setStatus("Action deleted. Existing external bindings will report that it no longer exists.");
	emitEvent("motion_catalogue_changed");
}

void PulseMotionEngine::refreshSummary()
{
	if (!summaryLabel || !kindField) return;
	const QJsonObject action = editorAction();
	summaryLabel->setText("THIS BUTTON WILL:  " + action.value("summary").toString());
	const bool punch = action.value("kind") == "punch";
	sourceField->setVisible(punch);
	zoomField->setVisible(punch);
	holdField->setVisible(punch);
	restoreField->setVisible(punch);
	stageField->setEnabled(action.value("policy") != "current");
	returnStageField->setEnabled(action.value("policy") == "switch");
}

void PulseMotionEngine::setStatus(const QString &text, bool error)
{
	if (!statusLabel) return;
	statusLabel->setText(text);
	statusLabel->setStyleSheet(error ? "color:#fb7185" : QString());
}

PulseMotionEngine::Transform PulseMotionEngine::capture(obs_sceneitem_t *item)
{
	Transform value;
	if (!item) return value;
	obs_sceneitem_get_pos(item, &value.pos);
	obs_sceneitem_get_scale(item, &value.scale);
	obs_sceneitem_get_bounds(item, &value.bounds);
	value.rotation = obs_sceneitem_get_rot(item);
	obs_sceneitem_get_crop(item, &value.crop);
	value.boundsType = obs_sceneitem_get_bounds_type(item);
	value.alignment = obs_sceneitem_get_alignment(item);
	value.boundsAlignment = obs_sceneitem_get_bounds_alignment(item);
	value.boundsCrop = obs_sceneitem_get_bounds_crop(item);
	value.visible = obs_sceneitem_visible(item);
	value.locked = obs_sceneitem_locked(item);
	value.order = obs_sceneitem_get_order_position(item);
	return value;
}

QJsonObject PulseMotionEngine::serialize(const Transform &value)
{
	return QJsonObject{{"positionX", value.pos.x}, {"positionY", value.pos.y},
		{"scaleX", value.scale.x}, {"scaleY", value.scale.y}, {"rotation", value.rotation},
		{"boundsWidth", value.bounds.x}, {"boundsHeight", value.bounds.y}, {"boundsType", int(value.boundsType)},
		{"alignment", int(value.alignment)}, {"boundsAlignment", int(value.boundsAlignment)}, {"cropToBounds", value.boundsCrop},
		{"cropLeft", value.crop.left}, {"cropTop", value.crop.top}, {"cropRight", value.crop.right}, {"cropBottom", value.crop.bottom},
		{"visible", value.visible}, {"locked", value.locked}, {"order", value.order}};
}

PulseMotionEngine::Transform PulseMotionEngine::deserialize(const QJsonObject &object)
{
	Transform value;
	const QJsonObject position = jsonObject(object.value("pos"));
	const QJsonObject scale = jsonObject(object.value("scale"));
	const QJsonObject bounds = jsonObject(object.value("bounds"));
	const QJsonObject crop = jsonObject(object.value("crop"));
	value.pos.x = float(finiteNumber(object.value("positionX"), finiteNumber(position.value("x"))));
	value.pos.y = float(finiteNumber(object.value("positionY"), finiteNumber(position.value("y"))));
	value.scale.x = float(finiteNumber(object.value("scaleX"), finiteNumber(scale.value("x"), 1.0)));
	value.scale.y = float(finiteNumber(object.value("scaleY"), finiteNumber(scale.value("y"), 1.0)));
	value.rotation = float(finiteNumber(object.value("rotation"), finiteNumber(object.value("rot"))));
	value.bounds.x = float(finiteNumber(object.value("boundsWidth"), finiteNumber(bounds.value("x"))));
	value.bounds.y = float(finiteNumber(object.value("boundsHeight"), finiteNumber(bounds.value("y"))));
	value.boundsType = obs_bounds_type(object.value("boundsType").toInt(int(OBS_BOUNDS_NONE)));
	value.alignment = uint32_t(object.value("alignment").toInt());
	value.boundsAlignment = uint32_t(object.value("boundsAlignment").toInt());
	value.boundsCrop = object.value("cropToBounds").toBool();
	value.crop.left = int(finiteNumber(object.value("cropLeft"), finiteNumber(crop.value("left"))));
	value.crop.top = int(finiteNumber(object.value("cropTop"), finiteNumber(crop.value("top"))));
	value.crop.right = int(finiteNumber(object.value("cropRight"), finiteNumber(crop.value("right"))));
	value.crop.bottom = int(finiteNumber(object.value("cropBottom"), finiteNumber(crop.value("bottom"))));
	value.visible = object.value("visible").toBool(object.value("enabled").toBool(true));
	value.locked = object.value("locked").toBool();
	value.order = object.value("order").toInt(object.value("sceneItemIndex").toInt());
	return value;
}

PulseMotionEngine::Transform PulseMotionEngine::interpolate(const Transform &from, const Transform &to, double progress)
{
	const double t = smoothStep(progress);
	auto between = [t](double a, double b) { return float(a + (b - a) * t); };
	Transform value = from;
	value.pos = {between(from.pos.x, to.pos.x), between(from.pos.y, to.pos.y)};
	value.scale = {between(from.scale.x, to.scale.x), between(from.scale.y, to.scale.y)};
	value.bounds = {between(from.bounds.x, to.bounds.x), between(from.bounds.y, to.bounds.y)};
	value.rotation = between(from.rotation, to.rotation);
	value.crop.left = int(std::lround(between(from.crop.left, to.crop.left)));
	value.crop.top = int(std::lround(between(from.crop.top, to.crop.top)));
	value.crop.right = int(std::lround(between(from.crop.right, to.crop.right)));
	value.crop.bottom = int(std::lround(between(from.crop.bottom, to.crop.bottom)));
	if (progress >= 1.0) value = to;
	return value;
}

void PulseMotionEngine::apply(obs_sceneitem_t *item, const Transform &value, bool finalFrame)
{
	if (!item) return;
	obs_sceneitem_set_pos(item, &value.pos);
	obs_sceneitem_set_scale(item, &value.scale);
	obs_sceneitem_set_rot(item, value.rotation);
	obs_sceneitem_set_crop(item, &value.crop);
	obs_sceneitem_set_bounds_type(item, value.boundsType);
	obs_sceneitem_set_bounds_alignment(item, value.boundsAlignment);
	obs_sceneitem_set_bounds_crop(item, value.boundsCrop);
	obs_sceneitem_set_bounds(item, &value.bounds);
	obs_sceneitem_set_alignment(item, value.alignment);
	if (finalFrame) {
		obs_sceneitem_set_order_position(item, value.order);
		obs_sceneitem_set_visible(item, value.visible);
		obs_sceneitem_set_locked(item, value.locked);
	}
}

PulseMotionEngine::Transform PulseMotionEngine::punchTarget(obs_sceneitem_t *item, const Transform &original,
									 double zoom, double focusX, double focusY)
{
	Transform target = original;
	obs_source_t *source = item ? obs_sceneitem_get_source(item) : nullptr;
	const int width = source ? int(obs_source_get_width(source)) : 0;
	const int height = source ? int(obs_source_get_height(source)) : 0;
	const int visibleWidth = width - original.crop.left - original.crop.right;
	const int visibleHeight = height - original.crop.top - original.crop.bottom;
	if (width <= 0 || height <= 0 || visibleWidth <= 0 || visibleHeight <= 0)
		return target;
	zoom = std::clamp(zoom, 1.0, 4.0);
	focusX = std::clamp(focusX, 0.0, 1.0);
	focusY = std::clamp(focusY, 0.0, 1.0);
	if (original.scale.x < 0) focusX = 1.0 - focusX;
	if (original.scale.y < 0) focusY = 1.0 - focusY;
	const int targetWidth = std::max(1, int(std::lround(visibleWidth / zoom)));
	const int targetHeight = std::max(1, int(std::lround(visibleHeight / zoom)));
	const int horizontal = visibleWidth - targetWidth;
	const int vertical = visibleHeight - targetHeight;
	target.crop.left = original.crop.left + int(std::lround(horizontal * focusX));
	target.crop.right = width - target.crop.left - targetWidth;
	target.crop.top = original.crop.top + int(std::lround(vertical * focusY));
	target.crop.bottom = height - target.crop.top - targetHeight;
	if (original.boundsType == OBS_BOUNDS_NONE) {
		target.scale.x = original.scale.x * float(visibleWidth) / float(targetWidth);
		target.scale.y = original.scale.y * float(visibleHeight) / float(targetHeight);
	}
	return target;
}

OBSSceneItem PulseMotionEngine::resolveItem(const QString &container, qint64 itemId, const QString &sourceName, bool recursive) const
{
	QString resolvedContainer = container;
	OBSSourceAutoRelease owner;
	if (resolvedContainer.isEmpty()) {
		owner = obs_frontend_get_current_scene();
		resolvedContainer = owner ? QString::fromUtf8(obs_source_get_name(owner)) : QString();
	} else {
		owner = obs_get_source_by_name(resolvedContainer.toUtf8().constData());
	}
	obs_scene_t *scene = owner ? obs_scene_from_source(owner) : nullptr;
	if (!scene && owner) scene = obs_group_from_source(owner);
	if (!scene) return {};
	OBSSceneItem item = itemId > 0 ? PulseRuntimeSafety::findSceneItemById(scene, itemId) : OBSSceneItem{};
	if (!item && !sourceName.isEmpty()) {
		if (recursive) {
			item = PulseRuntimeSafety::findSceneItem(scene, sourceName.toUtf8().constData());
		} else {
			struct Search { QByteArray name; OBSSceneItem result; } search{sourceName.toUtf8(), {}};
			obs_scene_enum_items(scene, [](obs_scene_t *, obs_sceneitem_t *candidate, void *data) {
				auto &search = *static_cast<Search *>(data);
				if (search.name == obs_source_get_name(obs_sceneitem_get_source(candidate))) search.result = candidate;
				return !search.result;
			}, &search);
			item = search.result;
		}
	}
	return item;
}

QComboBox *PulseMotionEngine::stageSelector() const
{
	QWidget *window = static_cast<QWidget *>(obs_frontend_get_main_window());
	return window ? window->findChild<QComboBox *>("PulseWeaverStageSelector") : nullptr;
}

QString PulseMotionEngine::activeStageName() const
{
	QComboBox *selector = stageSelector();
	return selector ? rawStageName(selector, selector->currentIndex()) : QString();
}

int PulseMotionEngine::findStage(const QString &name) const
{
	QComboBox *selector = stageSelector();
	if (!selector) return -1;
	for (int index = 0; index < selector->count(); ++index)
		if (rawStageName(selector, index).compare(name, Qt::CaseInsensitive) == 0) return index;
	return -1;
}

void PulseMotionEngine::switchStage(int index)
{
	if (QComboBox *selector = stageSelector()) {
		expectedStageIndex = index;
		selector->setCurrentIndex(index);
	}
}

int PulseMotionEngine::stageReadinessDelay(const QJsonObject &action) const
{
	QComboBox *selector = stageSelector();
	const int row = selector ? findStage(action.value("stage").toString()) : -1;
	if (row < 0) return 0;
	const QJsonObject stage = QJsonDocument::fromJson(selector->itemData(row).toString().toUtf8()).object();
	const int fallback = stage.value("durationMs").toInt(500);
	const int horizontal = stage.value("horizontalTransition").toString("fade") == "cut" ? 0 :
		stage.value("horizontalDurationMs").toInt(fallback);
	const int vertical = stage.value("verticalTransition").toString("fade") == "cut" ? 0 :
		stage.value("verticalDurationMs").toInt(fallback);
	return std::clamp(std::max(horizontal, vertical) + 100, 100, 5200);
}

bool PulseMotionEngine::prepareExecution(Execution &execution, QString &error)
{
	const QJsonObject action = execution.action;
	const QString policy = action.value("policy").toString("switch");
	const QString assignedStage = action.value("stage").toString();
	if ((policy == "switch" || policy == "only") && findStage(assignedStage) < 0) {
		error = "Stage “" + assignedStage + "” no longer exists. Choose an existing Stage and save the action again.";
		return false;
	}
	if (policy == "only" && activeStageName().compare(assignedStage, Qt::CaseInsensitive) != 0) {
		error = "This action only runs on “" + assignedStage + "”. The current Stage was left unchanged.";
		return false;
	}
	execution.previousStage = activeStageName();
	execution.actionStage = policy == "current" ? execution.previousStage : assignedStage;
	execution.durationMs = std::clamp(action.value("durationMs").toInt(750), 0, 15000);
	const QString kind = action.value("kind").toString();
	if (kind == "punch") {
		QString container = action.value("container").toString();
		if (policy == "current") container.clear();
		const qint64 itemId = policy == "current" ? 0 : action.value("itemId").toString().toLongLong();
		const QString sourceName = action.value("source").toString();
		OBSSceneItem item = resolveItem(container, itemId, sourceName, true);
		if (!item) {
			error = "“" + sourceName + "” was not found where this action expects it. Nothing was changed.";
			return false;
		}
		Track track;
		track.container = container;
		track.sourceName = sourceName;
		track.itemId = obs_sceneitem_get_id(item);
		track.item = item;
		track.baseline = track.from = capture(item);
		track.target = punchTarget(item, track.baseline, action.value("zoomPercent").toDouble(150.0) / 100.0,
			action.value("focusX").toDouble(0.5), action.value("focusY").toDouble(0.42));
		execution.tracks.push_back(std::move(track));
	} else if (kind == "layout") {
		for (const QJsonValue &value : action.value("items").toArray()) {
			const QJsonObject saved = value.toObject();
			const QString container = saved.value("container").toString(action.value("container").toString());
			const qint64 itemId = saved.value("itemId").toString().toLongLong();
			const QString sourceName = saved.value("source").toString();
			OBSSceneItem item = resolveItem(container, itemId, sourceName, false);
			if (!item) {
				error = "Layout source “" + sourceName + "” is missing from “" + container + "”. Nothing was changed.";
				return false;
			}
			Track track;
			track.container = container;
			track.sourceName = sourceName;
			track.itemId = obs_sceneitem_get_id(item);
			track.item = item;
			track.baseline = track.from = capture(item);
			track.target = deserialize(saved.value("transform").toObject());
			execution.tracks.push_back(std::move(track));
		}
		if (execution.tracks.empty()) {
			error = "This layout has no controlled sources. Edit it and tick at least one source.";
			return false;
		}
	} else {
		error = "This imported action type is not executable yet. Review or remove it in Motion.";
		return false;
	}
	return true;
}

QJsonObject PulseMotionEngine::runAction(const QString &idOrName, const QString &requestId)
{
	if (!requestId.isEmpty() && requestResults.contains(requestId))
		return requestResults.value(requestId);
	const QJsonObject action = actionByIdentity(idOrName);
	if (action.isEmpty())
		return {{"ok", false}, {"message", "Motion action was not found. Refresh the controller’s action list."}};
	if (action.value("draft").toBool())
		return {{"ok", false}, {"message", "This imported action is still a draft. Review and save it in Pulse Weaver first."}};
	if (active) {
		if (active->action.value("id") == action.value("id") && action.value("kind") == "punch") {
			active->holdUntil = std::max(active->holdUntil,
				QDateTime::currentMSecsSinceEpoch() + action.value("holdMs").toInt(5000));
			const QJsonObject result{{"ok", true}, {"accepted", true}, {"executionId", active->id},
				{"message", "Camera close-up timer restarted."}};
			if (!requestId.isEmpty()) requestResults.insert(requestId, result);
			return result;
		}
		return {{"ok", false}, {"message", "Another motion action is running. Stop or restore it before starting this one."},
			{"executionId", active->id}};
	}
	auto execution = std::make_unique<Execution>();
	execution->id = QUuid::createUuid().toString(QUuid::WithoutBraces);
	execution->requestId = requestId;
	execution->action = action;
	execution->generation = ++generation;
	QString error;
	if (!prepareExecution(*execution, error))
		return {{"ok", false}, {"message", error}};
	active = std::move(execution);
	const QString policy = action.value("policy").toString("switch");
	const int targetStage = policy == "switch" ? findStage(action.value("stage").toString()) : -1;
	if (targetStage >= 0 && stageSelector() && targetStage != stageSelector()->currentIndex()) {
		active->phase = "changing_stage";
		switchStage(targetStage);
		stageTimer.start(stageReadinessDelay(action));
		setStatus("Changing to Stage “" + action.value("stage").toString() + "”…");
		emitEvent("motion_state", {{"state", "changing_stage"}, {"executionId", active->id},
			{"action", action.value("id")}, {"name", action.value("name")}});
	} else {
		beginAfterStage();
	}
	const QJsonObject result{{"ok", true}, {"accepted", true}, {"executionId", active ? active->id : QString()},
		{"message", "Action accepted: “" + action.value("name").toString() + "”."}};
	if (!requestId.isEmpty()) requestResults.insert(requestId, result);
	return result;
}

void PulseMotionEngine::beginAfterStage()
{
	if (!active) return;
	active->phase = "moving";
	active->clock.restart();
	for (Track &track : active->tracks) {
		track.baseline = track.from = capture(track.item);
		if (active->action.value("kind") == "punch")
			track.target = punchTarget(track.item, track.baseline,
				active->action.value("zoomPercent").toDouble(150.0) / 100.0,
				active->action.value("focusX").toDouble(0.5), active->action.value("focusY").toDouble(0.42));
		obs_sceneitem_set_visible(track.item, true);
	}
	setStatus("Running “" + active->action.value("name").toString() + "”…");
	emitEvent("motion_state", {{"state", "running"}, {"executionId", active->id},
		{"action", active->action.value("id")}, {"name", active->action.value("name")}, {"stage", activeStageName()}});
	if (active->durationMs <= 0) {
		for (Track &track : active->tracks) apply(track.item, track.target, true);
		finishMove();
	} else {
		animationTimer.start();
	}
}

void PulseMotionEngine::tick()
{
	if (!active) {
		animationTimer.stop();
		return;
	}
	if (active->phase == "holding") {
		if (QDateTime::currentMSecsSinceEpoch() >= active->holdUntil)
			beginRestore("Hold finished");
		return;
	}
	if (active->phase != "moving" && active->phase != "restoring") return;
	const double progress = active->durationMs <= 0 ? 1.0 :
		std::min(1.0, double(active->clock.elapsed()) / double(active->durationMs));
	for (Track &track : active->tracks)
		apply(track.item, interpolate(track.from, track.target, progress), progress >= 1.0);
	if (progress < 1.0) return;
	if (active->phase == "restoring") {
		for (Track &track : active->tracks) apply(track.item, track.baseline, true);
		complete(true, "Original source state restored.");
	} else {
		finishMove();
	}
}

void PulseMotionEngine::finishMove()
{
	if (!active) return;
	for (Track &track : active->tracks) apply(track.item, track.target, true);
	if (active->action.value("kind") == "punch" && active->action.value("restore").toBool(true)) {
		active->phase = "holding";
		active->holdUntil = std::max(active->holdUntil,
			QDateTime::currentMSecsSinceEpoch() + std::max(0, active->action.value("holdMs").toInt(5000)));
		if (!animationTimer.isActive()) animationTimer.start();
		setStatus("Close-up active. Press Stop + Restore at any time.");
		return;
	}
	lastRestore = active->tracks;
	for (Track &track : lastRestore) track.item = nullptr;
	complete(true, "Reached the saved layout.");
}

void PulseMotionEngine::beginRestore(const QString &reason, bool operatorRequested)
{
	if (!active) return;
	stageTimer.stop();
	active->phase = "restoring";
	active->restoring = true;
	active->clock.restart();
	active->durationMs = operatorRequested ? std::min(active->durationMs, 350) : active->durationMs;
	for (Track &track : active->tracks) {
		track.from = capture(track.item);
		track.target = track.baseline;
		if (track.baseline.visible) obs_sceneitem_set_visible(track.item, true);
	}
	setStatus(reason + ". Restoring…");
	if (active->durationMs <= 0) {
		for (Track &track : active->tracks) apply(track.item, track.baseline, true);
		complete(true, "Original source state restored.");
	} else if (!animationTimer.isActive()) {
		animationTimer.start();
	}
}

void PulseMotionEngine::complete(bool ok, const QString &message)
{
	if (!active) return;
	animationTimer.stop();
	stageTimer.stop();
	const QString executionId = active->id;
	const QString actionId = active->action.value("id").toString();
	const QString name = active->action.value("name").toString();
	const QString requestId = active->requestId;
	const bool returnStage = ok && active->action.value("returnStage").toBool() &&
		!active->previousStage.isEmpty() && activeStageName() == active->actionStage;
	const QString previousStage = active->previousStage;
	active.reset();
	if (returnStage) {
		const int previous = findStage(previousStage);
		if (previous >= 0) switchStage(previous);
	}
	setStatus(message);
	const QJsonObject result{{"ok", ok}, {"executionId", executionId}, {"action", actionId}, {"name", name},
		{"message", message}, {"stage", activeStageName()}};
	if (!requestId.isEmpty()) requestResults.insert(requestId, result);
	while (requestResults.size() > 64) requestResults.erase(requestResults.begin());
	QJsonObject event = result;
	event.insert("state", ok ? "finished" : "failed");
	emitEvent("motion_state", event);
}

QJsonObject PulseMotionEngine::stopAction(const QString &executionId, bool restore)
{
	if (!active)
		return restore ? restoreLast() : QJsonObject{{"ok", true}, {"message", "No motion action is running."}};
	if (!executionId.isEmpty() && executionId != active->id)
		return {{"ok", false}, {"message", "That motion execution is no longer running."}};
	if (restore) {
		if (active->phase == "changing_stage") {
			const QString id = active->id;
			complete(true, "Motion stopped before source movement began.");
			return {{"ok", true}, {"accepted", true}, {"executionId", id},
				{"message", "Motion stopped before source movement began."}};
		}
		beginRestore("Stopped by the operator", true);
		return {{"ok", true}, {"accepted", true}, {"executionId", active ? active->id : QString()},
			{"message", "Stopping and restoring the sources affected by this action."}};
	}
	animationTimer.stop();
	stageTimer.stop();
	const QString id = active->id;
	active.reset();
	return {{"ok", true}, {"executionId", id}, {"message", "Motion stopped at its current position."}};
}

QJsonObject PulseMotionEngine::restoreLast()
{
	if (lastRestore.empty()) return {{"ok", true}, {"message", "There is no completed layout to restore."}};
	int restored = 0;
	for (Track &track : lastRestore) {
		OBSSceneItem item = resolveItem(track.container, track.itemId, track.sourceName, false);
		if (!item) continue;
		apply(item, track.baseline, true);
		++restored;
	}
	lastRestore.clear();
	const QString message = restored ? QString("Restored %1 source%2.").arg(restored).arg(restored == 1 ? "" : "s") :
		QString("The saved sources no longer exist, so nothing was changed.");
	setStatus(message, restored == 0);
	return {{"ok", restored > 0}, {"message", message}, {"restored", restored}};
}

void PulseMotionEngine::cancelForManualStageChange()
{
	if (!active) return;
	if (active->phase == "changing_stage") {
		const QString name = active->action.value("name").toString();
		active.reset();
		stageTimer.stop();
		setStatus("“" + name + "” was cancelled because you changed Stage.");
		emitEvent("motion_state", {{"state", "cancelled"}, {"name", name},
			{"message", "Cancelled because the operator changed Stage."}});
		return;
	}
	beginRestore("Stage changed by the operator", true);
}

void PulseMotionEngine::frontendEvent(obs_frontend_event event)
{
	if (event == OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGED || event == OBS_FRONTEND_EVENT_SCENE_LIST_CHANGED) {
		populateScenes();
		populateSources();
		populateItems();
	}
}

void PulseMotionEngine::emitEvent(QString event, QJsonObject values)
{
	values.insert("event", std::move(event));
	if (eventCallback) eventCallback(std::move(values));
}

QJsonObject PulseMotionEngine::catalogueJson() const
{
	QJsonArray catalogue;
	for (const QJsonValue &value : actions) {
		const QJsonObject action = value.toObject();
		if (action.value("draft").toBool()) continue;
		catalogue.append(QJsonObject{{"id", action.value("id")}, {"name", action.value("name")},
			{"kind", action.value("kind")}, {"stage", action.value("stage")}, {"summary", action.value("summary")}});
	}
	return {{"version", 1}, {"actions", catalogue}};
}

QJsonObject PulseMotionEngine::stateJson() const
{
	QJsonObject state = catalogueJson();
	state.insert("active", bool(active));
	if (active) state.insert("execution", QJsonObject{{"id", active->id}, {"action", active->action.value("id")},
		{"name", active->action.value("name")}, {"state", active->phase}, {"stage", activeStageName()}});
	return state;
}

QJsonArray PulseMotionEngine::importLumiaLayouts(const QJsonObject &document, QJsonArray &issues) const
{
	QJsonObject layouts = document.value("layouts").toObject();
	if (layouts.isEmpty() && document.value("layoutLibraryJson").isString()) {
		const QJsonDocument nested = QJsonDocument::fromJson(document.value("layoutLibraryJson").toString().toUtf8());
		layouts = nested.object().value("layouts").toObject();
	}
	QJsonArray imported;
	for (auto iterator = layouts.begin(); iterator != layouts.end() && imported.size() < 100; ++iterator) {
		const QJsonObject layout = iterator.value().toObject();
		if (layout.isEmpty()) continue;
		const QString baseName = cleanName(layout.value("name").toString(), "Imported Lumia layout " + iterator.key());
		const QString sceneName = layout.value("sceneName").toString();
		const bool triState = layout.value("triState").toBool() || layout.value("stateCount").toInt() == 3;
		for (const QString &stateName : triState ? QStringList{"A", "B", "C"} : QStringList{"A", "B"}) {
			QJsonArray items;
			for (const QJsonValue &itemValue : layout.value("items").toArray()) {
				const QJsonObject identity = itemValue.toObject();
				const QJsonObject state = identity.value("state" + stateName).toObject();
				if (state.isEmpty()) continue;
				const QString container = state.value("container").toString(identity.value("container").toString(sceneName));
				const QString source = state.value("sourceName").toString(identity.value("sourceName").toString());
				const QString id = state.value("sceneItemId").toVariant().toString().isEmpty() ?
					identity.value("sceneItemId").toVariant().toString() : state.value("sceneItemId").toVariant().toString();
				QJsonObject transform = state.value("finalTransform").toObject();
				if (transform.isEmpty()) transform = state.value("rawTransform").toObject();
				if (transform.isEmpty()) transform = state.value("transform").toObject();
				if (source.isEmpty() || transform.isEmpty()) continue;
				Transform converted = deserialize(transform);
				converted.visible = state.value("visibilityMode").toString() == "hide" ? false :
					state.value("visibilityMode").toString() == "keep" ? true : state.value("enabled").toBool(true);
				converted.order = state.value("sceneItemIndex").toInt(converted.order);
				items.append(QJsonObject{{"container", container}, {"itemId", id}, {"source", source},
					{"transform", serialize(converted)}});
			}
			if (items.isEmpty()) {
				issues.append(QJsonObject{{"name", baseName + " · State " + stateName},
					{"status", "unsupported"}, {"message", "No readable source transforms were found."}});
				continue;
			}
			const QString name = baseName + " · State " + stateName;
			imported.append(QJsonObject{{"version", 1}, {"id", QUuid::createUuid().toString(QUuid::WithoutBraces)},
				{"name", name}, {"kind", "layout"}, {"policy", "only"}, {"stage", QString()}, {"container", sceneName},
				{"durationMs", layout.value("durationMs").toInt(750)}, {"restore", false}, {"draft", true},
				{"items", items}, {"import", QJsonObject{{"source", "lumia_multistate"}, {"slot", iterator.key()}, {"state", stateName}}},
				{"summary", "Imported Lumia layout State " + stateName + "; choose its Stage and review source matches."}});
			issues.append(QJsonObject{{"name", name}, {"status", "needs_review"},
				{"message", "Transforms were converted. Choose a Stage and verify each source before saving."}});
		}
	}
	return imported;
}

QJsonArray PulseMotionEngine::importMoveFilters(const QJsonObject &document, QJsonArray &issues) const
{
	QJsonArray imported;
	std::function<void(const QJsonValue &, QString)> walk;
	walk = [&](const QJsonValue &value, QString owner) {
		if (imported.size() >= 150) return;
		if (value.isArray()) {
			for (const QJsonValue &child : value.toArray()) walk(child, owner);
			return;
		}
		if (!value.isObject()) return;
		const QJsonObject object = value.toObject();
		if (object.value("name").isString() && (object.contains("filters") || object.value("id").toString() == "scene"))
			owner = object.value("name").toString(owner);
		const QString id = object.value("id").toString(object.value("versioned_id").toString()).toLower();
		if (id.contains("move_source")) {
			const QJsonObject settings = object.value("settings").toObject();
			const QString source = settings.value("source").toString(settings.value("source_name").toString());
			const QString filterName = cleanName(object.value("name").toString(), "Imported Move Source");
			const bool relative = settings.value("transform_relative").toBool() ||
				jsonObject(settings.value("pos")).value("x_sign").toString().trimmed() == "+" ||
				jsonObject(settings.value("scale")).value("x_sign").toString().trimmed() == "*";
			if (owner.isEmpty() || source.isEmpty()) {
				issues.append(QJsonObject{{"name", filterName}, {"status", "unsupported"},
					{"message", "The owning scene or target source could not be identified."}});
			} else if (relative) {
				issues.append(QJsonObject{{"name", filterName}, {"status", "unsupported"},
					{"message", "Relative Move Source math is preserved in the import metadata but is not executable in this preview."}});
			} else {
				Transform target = deserialize(settings);
				QJsonObject saved{{"container", owner}, {"itemId", QString()}, {"source", source}, {"transform", serialize(target)}};
				QJsonObject compatibility{{"source", "move_source"}, {"originalId", object.value("id")},
					{"originalName", object.value("name")}, {"settings", settings}};
				imported.append(QJsonObject{{"version", 1}, {"id", QUuid::createUuid().toString(QUuid::WithoutBraces)},
					{"name", filterName}, {"kind", "layout"}, {"policy", "current"}, {"container", owner},
					{"durationMs", settings.value("duration").toInt(settings.value("duration_ms").toInt(750))},
					{"restore", false}, {"draft", true}, {"items", QJsonArray{saved}}, {"import", compatibility},
					{"summary", "Imported Move Source draft; review target, timing and triggers."}});
				issues.append(QJsonObject{{"name", filterName}, {"status", "needs_review"},
					{"message", "Absolute transform imported. Trigger chains, audio/media actions and exact easing still need review."}});
			}
		} else if (id.contains("move_") || id.contains("move-transition")) {
			issues.append(QJsonObject{{"name", cleanName(object.value("name").toString(), id)}, {"status", "unsupported"},
				{"message", "This Move feature is preserved in the original file but has no native adapter in this preview."}});
		}
		for (auto iterator = object.begin(); iterator != object.end(); ++iterator)
			if (iterator.value().isArray() || iterator.value().isObject()) walk(iterator.value(), owner);
	};
	walk(document, {});
	return imported;
}

QJsonObject PulseMotionEngine::importDocument(const QJsonObject &document, const QString &sourceLabel)
{
	QJsonArray issues;
	QJsonArray imported;
	if (document.value("version").toInt() == 1 && document.value("actions").isArray()) {
		for (const QJsonValue &value : document.value("actions").toArray()) {
			QJsonObject action = value.toObject();
			action.insert("id", QUuid::createUuid().toString(QUuid::WithoutBraces));
			action.insert("draft", true);
			action.insert("import", QJsonObject{{"source", "pulseweaver"}, {"label", sourceLabel}});
			imported.append(action);
		}
	} else {
		const QJsonArray lumia = importLumiaLayouts(document, issues);
		const QJsonArray move = importMoveFilters(document, issues);
		for (const QJsonValue &value : lumia) imported.append(value);
		for (const QJsonValue &value : move) imported.append(value);
	}
	for (const QJsonValue &value : imported) actions.append(value);
	if (!imported.isEmpty()) save();
	refreshEditor();
	if (!imported.isEmpty()) {
		editingId = imported.first().toObject().value("id").toString();
		loadActionIntoEditor(imported.first().toObject());
	}
	return {{"ok", !imported.isEmpty()}, {"imported", imported.size()}, {"issues", issues},
		{"message", imported.isEmpty() ? "No supported Move or Lumia layouts were found." :
			QString("Imported %1 draft action%2. Review and save each one before running it.")
				.arg(imported.size()).arg(imported.size() == 1 ? "" : "s")}};
}

void PulseMotionEngine::importFromFile()
{
	const QString path = QFileDialog::getOpenFileName(editor, "Import Move or Lumia setup", {}, "JSON files (*.json);;All files (*.*)");
	if (path.isEmpty()) return;
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly)) {
		setStatus("Could not open the selected file.", true);
		return;
	}
	if (file.size() > 16 * 1024 * 1024) {
		setStatus("The selected JSON file is larger than 16 MB. Export only the relevant scene collection or layout library.", true);
		return;
	}
	QJsonParseError error;
	const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
	if (error.error != QJsonParseError::NoError || !document.isObject()) {
		setStatus("The selected file is not a valid JSON object.", true);
		return;
	}
	const QJsonObject result = importDocument(document.object(), QFileInfo(path).fileName());
	setStatus(result.value("message").toString(), !result.value("ok").toBool());
	if (!result.value("issues").toArray().isEmpty()) {
		QStringList details;
		for (const QJsonValue &value : result.value("issues").toArray()) {
			const QJsonObject issue = value.toObject();
			details << issue.value("name").toString() + ": " + issue.value("message").toString();
		}
		QMessageBox::information(editor, "Import review", result.value("message").toString() + "\n\n" + details.join("\n"));
	}
}
