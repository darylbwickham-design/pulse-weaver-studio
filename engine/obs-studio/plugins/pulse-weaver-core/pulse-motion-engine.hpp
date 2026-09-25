#pragma once

#include <obs-frontend-api.h>
#include <obs.hpp>

#include <QElapsedTimer>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QPointF>
#include <QPointer>
#include <QTimer>
#include <QWidget>

#include <functional>
#include <memory>
#include <vector>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QSpinBox;
class QTreeWidget;
class QSlider;
class QLayout;
class PulseMotionCanvas;

class PulseMotionEngine final : public QObject {
public:
	using EventCallback = std::function<void(QJsonObject)>;

	explicit PulseMotionEngine(QObject *parent, QString storagePath, EventCallback eventCallback = {});
	~PulseMotionEngine() override;

	QWidget *createEditor(QWidget *parent = nullptr);
	QJsonObject catalogueJson() const;
	QJsonObject stateJson() const;
	QJsonObject runAction(const QString &idOrName, const QString &requestId = {});
	QJsonObject stopAction(const QString &executionId = {}, bool restore = true);
	QJsonObject restoreLast();
	QJsonObject restoreOriginals();
	QJsonObject createShowStages();
	QJsonObject importDocument(const QJsonObject &document, const QString &sourceLabel = {});
	void frontendEvent(obs_frontend_event event);
	void refreshEditor();

private:
	struct Transform {
		vec2 pos{};
		vec2 scale{1.0f, 1.0f};
		vec2 bounds{};
		float rotation = 0.0f;
		obs_sceneitem_crop crop{};
		obs_bounds_type boundsType = OBS_BOUNDS_NONE;
		uint32_t alignment = 0;
		uint32_t boundsAlignment = 0;
		bool boundsCrop = false;
		bool visible = true;
		bool locked = false;
		int order = 0;
	};
	struct Track {
		QString container;
		QString sourceName;
		qint64 itemId = 0;
		OBSSceneItem item;
		Transform baseline;
		Transform from;
		Transform target;
	};
	struct Execution {
		QString id;
		QString requestId;
		QJsonObject action;
		QString previousStage;
		QString actionStage;
		QString phase;
		std::vector<Track> tracks;
		QElapsedTimer clock;
		qint64 holdUntil = 0;
		int durationMs = 0;
		quint64 generation = 0;
		bool restoring = false;
		bool orderCommitted = false;
		bool graphicCommitted = false;
	};

	QString storagePath;
	EventCallback eventCallback;
	QJsonArray actions;
	QJsonArray originals;
	QJsonObject originalScenes;
	bool originalStoreValid = true;
	std::unique_ptr<Execution> active;
	std::vector<Track> lastRestore;
	QTimer animationTimer;
	QTimer stageTimer;
	quint64 generation = 0;
	int expectedStageIndex = -1;
	QHash<QString, QJsonObject> requestResults;
	QHash<QString, quint64> hotkeys;
	QHash<quint64, QString> hotkeyActions;

	QPointer<QWidget> editor;
	QPointer<QListWidget> actionList;
	QPointer<QComboBox> kindField;
	QPointer<QLineEdit> nameField;
	QPointer<QComboBox> policyField;
	QPointer<QComboBox> stageField;
	QPointer<QComboBox> sceneField;
	QPointer<QComboBox> sourceField;
	QPointer<QSpinBox> zoomField;
	QPointer<QSpinBox> durationField;
	QPointer<QSpinBox> holdField;
	QPointer<QCheckBox> restoreField;
	QPointer<QCheckBox> returnStageField;
	QPointer<QTreeWidget> itemTree;
	QPointer<PulseMotionCanvas> visualCanvas;
	QPointer<PulseMotionCanvas> portraitCanvas;
	QPointer<QLabel> summaryLabel;
	QPointer<QLabel> statusLabel;
	QPointer<QPushButton> saveButton;
	QString editingId;
	QPointF editorFocus{0.5, 0.42};
	qint64 visualSelectedItem = -1;
	OBSScene draftScene;
	OBSScene previewScene;
	QString draftContainer;
	QString mainContainer;
	QString pairedContainer;
	struct ParkedDraft {
		OBSScene scene;
		QHash<qint64, qint64> ids;
		QSet<qint64> controlled;
		std::vector<QJsonArray> undo, redo;
	};
	QHash<QString, ParkedDraft> parkedDrafts;
	QHash<QString, ParkedDraft> pairedPreviews;
	QJsonArray loadedTargets;
	void activateDraft(const QString &container);
	void includeDraftItem(qint64 originalId);
	void addDraftSource(QString selected = {});
	void adaptPortrait();
	void pinOverlayAcrossLooks();
	QHash<qint64, qint64> draftIds;
	QHash<qint64, qint64> previewIds;
	QPointer<QSlider> previewProgress;
	QTimer previewTimer;
	QElapsedTimer previewClock;
	QPointer<QWidget> lookTools;
	QPointer<QWidget> stageCards;
	QPointer<QWidget> lookCards;
	QPointer<QComboBox> stagePicker;
	QPointer<QComboBox> lookPicker;
	QPointer<QWidget> sourceChips;
	QPointer<QLabel> selectedSourceLabel;
	QPointer<QWidget> advancedPanel;
	QString selectedStageGroup;
	std::vector<QJsonArray> undoStates;
	std::vector<QJsonArray> redoStates;
	bool loadingDraft = false;
	bool draftDirty = false;
	bool mayLeaveDraft();
	void resetDraft(const QJsonArray &saved = {});
	OBSSceneItem draftItem(qint64 originalId) const;
	QJsonArray draftSnapshot() const;
	void applyDraftSnapshot(const QJsonArray &snapshot);
	void rememberDraft();
	void editDraft(const QString &operation);
	void refreshDraftRows();
	void previewDraft(int progress);
	void createLook(const QString &name, bool duplicate);
	void createStarterStage();
	void refreshNavigation();
	void refreshSourceChips();

	void load();
	bool save();
	bool preserveOriginals(const Execution &execution, QString &error);
	QJsonObject actionByIdentity(const QString &idOrName) const;
	void setStatus(const QString &text, bool error = false);
	void emitEvent(QString event, QJsonObject values = {});
	void syncHotkeys();
	static void hotkeyTriggered(void *data, obs_hotkey_id id, obs_hotkey_t *, bool pressed);
	void populateStages();
	void populateScenes();
	void populateSources();
	void populateItems();
	void loadActionIntoEditor(const QJsonObject &action);
	QJsonObject editorAction() const;
	void saveEditorAction();
	void deleteEditorAction();
	void importFromFile();
	void refreshSummary();
	void syncVisualCanvas();
	void visualCanvasInteraction(qint64 itemId, const QPointF &focus, bool toggleLayoutItem);

	QJsonArray sceneCatalogue() const;
	QJsonArray itemCatalogue(const QString &container) const;
	static Transform capture(obs_sceneitem_t *item);
	static QJsonObject serialize(const Transform &value);
	static Transform deserialize(const QJsonObject &value);
	static Transform interpolate(const Transform &from, const Transform &to, double progress);
	static Transform transitionFrame(const Transform &from, const Transform &to, double progress, float canvasWidth);
	static void apply(obs_sceneitem_t *item, const Transform &value, bool finalFrame = false);
	static Transform punchTarget(obs_sceneitem_t *item, const Transform &original, double zoom, double focusX, double focusY);
	OBSSceneItem resolveItem(const QString &container, qint64 itemId, const QString &sourceName, bool recursive = true) const;
	bool prepareExecution(Execution &execution, QString &error);
	void beginAfterStage();
	void tick();
	void finishMove();
	void beginRestore(const QString &reason, bool operatorRequested = false);
	void complete(bool ok, const QString &message);
	void cancelForManualStageChange();
	int stageReadinessDelay(const QJsonObject &action) const;
	QString activeStageName() const;
	QComboBox *stageSelector() const;
	int findStage(const QString &name) const;
	void switchStage(int index);
	QJsonArray importLumiaLayouts(const QJsonObject &document, QJsonArray &issues) const;
	QJsonArray importMoveFilters(const QJsonObject &document, QJsonArray &issues) const;
};
