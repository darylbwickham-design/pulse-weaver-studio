#include "../../shared/qt/PulseChat.hpp"
#include "../../shared/qt/PulseInteractionFeedback.hpp"
#include "../../shared/qt/PulseWindowChrome.hpp"
#include <QActionGroup>
#include "../../shared/qt/PulseLumiaOutput.hpp"
#include "../../shared/qt/PulseStageExclusions.hpp"
#include <obs-output-timing.h>
/******************************************************************************
    Pulse Weaver native product shell

    This file is part of the Pulse Weaver OBS Studio fork and is distributed
    under the GPL-2.0-or-later terms used by the upstream frontend.
******************************************************************************/

#include "OBSBasic.hpp"
#include "OBSApp.hpp"
#include "PulseVerticalEditor.hpp"
#include <oauth/YoutubeAuth.hpp>
#include <utility/YoutubeApiWrappers.hpp>

#include <components/VolumeMeter.hpp>
#include <components/VolumeControl.hpp>
#include <models/SceneCollection.hpp>
#include <widgets/OBSQTDisplay.hpp>

#include <QFrame>
#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDockWidget>
#include <QFileDialog>
#include <QGridLayout>
#include <QHeaderView>
#include <QHash>
#include <QHBoxLayout>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenuBar>
#include <QMenu>
#include <QMap>
#include <QMainWindow>
#include <QMouseEvent>
#include <QMessageBox>
#include <QPixmap>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollBar>
#include <QScrollArea>
#include <QSaveFile>
#include <QSettings>
#include <QSlider>
#include <QSpinBox>
#include <QSplitter>
#include <QSizePolicy>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QStyle>
#include <QTableWidget>
#include <QTabWidget>
#include <QTabBar>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QStandardPaths>

#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>
#include <thread>
#include <mutex>
#include <utility>

namespace {

#ifdef YOUTUBE_ENABLED
std::shared_ptr<YoutubeApiWrappers> pulseYouTubeAuth;
std::mutex pulseYouTubeChatRequestMutex;
constexpr qint64 pulseYouTubeChatMessageLifetimeMs = 15000;

class PulseYouTubeChatWorkerCompletion {
public:
	explicit PulseYouTubeChatWorkerCompletion(std::shared_ptr<PulseYouTubeChatWorkerBarrier> barrier)
		: barrier(std::move(barrier))
	{
	}

	~PulseYouTubeChatWorkerCompletion() { barrier->finish(); }

private:
	std::shared_ptr<PulseYouTubeChatWorkerBarrier> barrier;
};

template<typename Function>
bool pulseStartYouTubeChatWorker(const std::shared_ptr<PulseYouTubeChatWorkerBarrier> &barrier,
				 Function &&function)
{
	barrier->begin();
	try {
		std::thread([barrier, function = std::forward<Function>(function)]() mutable {
			PulseYouTubeChatWorkerCompletion completion(barrier);
			try {
				function();
			} catch (...) {
				/* A detached integration worker must never take down the
				 * frontend. Its completion guard still releases shutdown. */
			}
		}).detach();
		return true;
	} catch (...) {
		barrier->finish();
		return false;
	}
}
#endif
QString pulseYouTubeStreamKey;
QString pulseYouTubeBroadcastId;
obs_output_t *pulseYouTubeOutput = nullptr;
obs_service_t *pulseYouTubeOwnedService = nullptr;
obs_encoder_t *pulseYouTubeOwnedVideoEncoder = nullptr;
obs_encoder_t *pulseYouTubeOwnedAudioEncoder = nullptr;
QString pulseYouTubeSecondStreamKey;
QString pulseYouTubeSecondBroadcastId;
obs_output_t *pulseYouTubeSecondOutput = nullptr;
obs_service_t *pulseYouTubeSecondService = nullptr;
obs_encoder_t *pulseYouTubeSecondVideoEncoder = nullptr;
obs_encoder_t *pulseYouTubeSecondAudioEncoder = nullptr;
QString pulseYouTubePreparedMode;
QHash<QString, obs_canvas_t *> pulseOutputCanvases;
QHash<QString, OBSSource> pulseCanvasStingerTransitions;
QHash<QString, uint32_t> pulseAudioMixerBaselines;
uint32_t pulseOwnedAudioMixerMask = 0;
obs_output_t *pulseRecordingHorizontalOutput = nullptr;
obs_encoder_t *pulseRecordingHorizontalVideoEncoder = nullptr;
obs_encoder_t *pulseRecordingHorizontalAudioEncoder = nullptr;
obs_output_t *pulseRecordingVerticalOutput = nullptr;
obs_encoder_t *pulseRecordingVerticalVideoEncoder = nullptr;
obs_encoder_t *pulseRecordingVerticalAudioEncoder = nullptr;

/* A private output canvas owns a complete additional scene render.  Release
 * it as soon as a route returns to the native programme canvas, but only
 * while no encoder can still be bound to its video context. */
static void pulseReleaseOutputCanvas(const QString &key)
{
	if (obs_canvas_t *canvas = pulseOutputCanvases.take(key)) {
		obs_canvas_remove(canvas);
		obs_canvas_release(canvas);
	}
	/* Stingers are cached as "route|transition-uuid", not by the bare
	 * route key.  Drop every cached copy for this canvas when its private
	 * render route is released so repeated stage/output changes cannot leave
	 * old transition sources resident for the rest of the session. */
	const QString prefix = key + '|';
	for (auto it = pulseCanvasStingerTransitions.begin(); it != pulseCanvasStingerTransitions.end();) {
		if (it.key().startsWith(prefix))
			it = pulseCanvasStingerTransitions.erase(it);
		else
			++it;
	}
}

enum PulseChatDataRole {
	PulseChatPlatformRole = Qt::UserRole + 140,
	PulseChatUserRole,
	PulseChatUserIdRole,
	PulseChatMessageIdRole,
	PulseChatTextRole,
};

constexpr int PulseChatLiveChatIdRole = Qt::UserRole + 180;

static QColor pulseChatColour(const QString &platform, const QString &requested)
{
	QColor colour(requested);
	if (colour.isValid() && colour.lightness() > 50)
		return colour;
	if (platform == "twitch") return QColor("#B78CFF");
	if (platform == "youtube") return QColor("#FF7B8C");
	if (platform == "kick") return QColor("#6EE7B7");
	return QColor("#D9E3F2");
}

static bool pulseChatAtBottom(QListWidget *feed)
{
	if (!feed || !feed->verticalScrollBar())
		return true;
	auto *bar = feed->verticalScrollBar();
	return bar->value() >= bar->maximum() - 3;
}

static void pulseApplyChatFilter(QListWidget *feed, const QString &filter)
{
	if (!feed)
		return;
	feed->setProperty("pulseWeaverChatFilter", filter);
	for (int index = 0; index < feed->count(); ++index) {
		auto *item = feed->item(index);
		item->setHidden(filter != "all" && item->data(PulseChatPlatformRole).toString() != filter);
	}
	if (pulseChatAtBottom(feed)) {
		feed->setProperty("pulseWeaverUnreadCount", 0);
		if (auto *button = feed->window()->findChild<QPushButton *>("PulseWeaverChatNewMessages"))
			button->hide();
	}
}

static void pulseAppendUnifiedChat(QListWidget *feed, const QString &platform, const QString &user,
    const QString &message, const QString &colour = {}, const QStringList &badges = {},
    const QString &userId = {}, const QString &messageId = {}, bool own = false,
    const QString &liveChatId = {}, const QString &route = {})
{
    auto *item = PulseChat::append(feed, platform, user, message, colour, badges, {}, userId, messageId, own, {}, route);
	if (item && !liveChatId.isEmpty())
		item->setData(PulseChatLiveChatIdRole, liveChatId);
}
bool pulseEncoderAvailable(const char *requestedId)
{
	const char *id = nullptr;
	for (size_t index = 0; obs_enum_encoder_types(index, &id); ++index) {
		if (id && strcmp(id, requestedId) == 0)
			return true;
	}
	return false;
}

obs_encoder_t *pulseCreateH264Encoder(const QByteArray &name, int bitrate, bool recordingQuality)
{
	obs_data_t *settings = obs_data_create();
	obs_encoder_t *encoder = nullptr;
	if (pulseEncoderAvailable("obs_nvenc_h264_tex")) {
		obs_data_set_string(settings, "rate_control", recordingQuality ? "cqp" : "cbr");
		obs_data_set_int(settings, "cqp", 20);
		obs_data_set_int(settings, "bitrate", bitrate);
		obs_data_set_int(settings, "keyint_sec", 2);
		/* Keep high-quality recording at P5 quarter-resolution two-pass, while
		 * additional live destinations use P4 single-pass. This preserves the
		 * configured bitrate/resolution and gives simultaneous outputs useful
		 * encoder headroom. */
		obs_data_set_string(settings, "preset", recordingQuality ? "p5" : "p4");
		obs_data_set_string(settings, "multipass", recordingQuality ? "qres" : "disabled");
		/* These are independent platform/recording encoders.  Never inherit an
		 * expensive look-ahead default intended for a single dedicated encode. */
		obs_data_set_bool(settings, "lookahead", false);
		obs_data_set_string(settings, "profile", "high");
		encoder = obs_video_encoder_create("obs_nvenc_h264_tex", name.constData(), settings, nullptr);
	}
	if (!encoder) {
		obs_data_clear(settings);
		obs_data_set_int(settings, "bitrate", bitrate);
		obs_data_set_int(settings, "keyint_sec", 2);
		obs_data_set_string(settings, "preset", "veryfast");
		obs_data_set_string(settings, "profile", "high");
		encoder = obs_video_encoder_create("obs_x264", name.constData(), settings, nullptr);
	}
	obs_data_release(settings);
	return encoder;
}

QString pulseWeaverStagePath()
{
	char path[512] = {};
	if (GetAppConfigPath(path, sizeof(path), "obs-studio/pulseweaver-stages.json") <= 0)
		return QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) + "/pulseweaver-stages.json";
	return QString::fromUtf8(path);
}

struct PulseStageCache {
	QString path;
	QJsonArray stages;
	bool valid = false;
	QFileSystemWatcher watcher;
	PulseStageCache()
	{
		QObject::connect(&watcher, &QFileSystemWatcher::fileChanged, &watcher,
			[this] { valid = false; });
		QObject::connect(&watcher, &QFileSystemWatcher::directoryChanged, &watcher,
			[this] { valid = false; });
	}
};

PulseStageCache &pulseStageCache()
{
	static PulseStageCache cache;
	return cache;
}

QJsonArray loadPulseWeaverStages()
{
	auto &cache = pulseStageCache();
	const QString path = pulseWeaverStagePath();
	if (cache.valid && cache.path == path)
		return cache.stages;
	if (cache.path != path) {
		if (!cache.watcher.files().isEmpty()) cache.watcher.removePaths(cache.watcher.files());
		if (!cache.watcher.directories().isEmpty()) cache.watcher.removePaths(cache.watcher.directories());
		cache.path = path;
	}
	// Watch the directory too: QSaveFile and external editors replace files atomically.
	const QString directory = QFileInfo(path).absolutePath();
	if (!cache.watcher.directories().contains(directory)) cache.watcher.addPath(directory);
	if (!cache.watcher.files().contains(path) && QFileInfo::exists(path)) cache.watcher.addPath(path);
	QFile file(path);
	if (file.open(QIODevice::ReadOnly)) {
		const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
		if (document.isArray()) {
			cache.stages = document.array();
			cache.valid = true;
			return cache.stages;
		}
	}
	cache.valid = false;
	return {};
}

void savePulseWeaverStages(const QJsonArray &stages)
{
	pulseStageCache().valid = false;
	const QString path = pulseWeaverStagePath();
	QDir().mkpath(QFileInfo(path).absolutePath());
	QSaveFile file(path);
	if (file.open(QIODevice::WriteOnly)) {
		file.write(QJsonDocument(stages).toJson(QJsonDocument::Indented));
		file.commit();
	}
}

QString pulseWeaverUiSettingsPath()
{
	char path[512] = {};
	if (GetAppConfigPath(path, sizeof(path), "obs-studio/pulseweaver-ui.ini") <= 0)
		return QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) + "/pulseweaver-ui.ini";
	return QString::fromUtf8(path);
}

QJsonObject pulseStageAssignment(const QJsonObject &stage, const QString &provider, const QString &requestedCanvas = {})
{
	const QJsonObject assignments = stage.value("assignments").toObject();
	if (!requestedCanvas.isEmpty()) {
		const QString routedKey = provider + "_" + requestedCanvas;
		if (assignments.contains(routedKey))
			return assignments.value(routedKey).toObject();
	}
	if (assignments.contains(provider))
	{
		const QJsonObject legacy = assignments.value(provider).toObject();
		if (requestedCanvas.isEmpty() || legacy.value("canvas").toString() == requestedCanvas)
			return legacy;
	}
	QString canvas;
	if (provider == "twitch")
		canvas = !stage.value("horizontal").toString().isEmpty() ? "horizontal" : "vertical";
	else
		canvas = stage.value(provider).toString();
	if (!requestedCanvas.isEmpty() && canvas != requestedCanvas)
		return {};
	if (canvas != "horizontal" && canvas != "vertical")
		return {};
	const QString scene = stage.value(canvas).toString();
	if (scene.isEmpty())
		return {};
	return QJsonObject{{"canvas", canvas}, {"scene", scene}, {"excluded", QJsonArray{}}};
}

QString pulseOutputCanvasName(const QString &provider, const QString &canvas)
{
	return "Pulse Weaver Output " + provider.toLower() + " " + canvas.toLower();
}

obs_source_t *pulseCachedCanvasStinger(const QString &routeKey, obs_source_t *transitionTemplate)
{
	if (!transitionTemplate || strcmp(obs_source_get_id(transitionTemplate), "obs_stinger_transition") != 0)
		return nullptr;
	const QString prefix = routeKey + '|';
	const QString cacheKey = prefix + QString::fromUtf8(obs_source_get_uuid(transitionTemplate));
	for (auto it = pulseCanvasStingerTransitions.begin(); it != pulseCanvasStingerTransitions.end();) {
		if (it.key().startsWith(prefix) && it.key() != cacheKey)
			it = pulseCanvasStingerTransitions.erase(it);
		else
			++it;
	}
	auto found = pulseCanvasStingerTransitions.find(cacheKey);
	if (found != pulseCanvasStingerTransitions.end())
		return found.value();
	const QByteArray name = QString("Pulse Weaver %1 Stinger").arg(routeKey).toUtf8();
	obs_source_t *copy = obs_source_duplicate(transitionTemplate, name.constData(), true);
	if (!copy)
		return nullptr;
	pulseCanvasStingerTransitions.insert(cacheKey, OBSSource(copy));
	obs_source_release(copy);
	return pulseCanvasStingerTransitions.value(cacheKey);
}

obs_canvas_t *pulseConfigureOutputCanvas(const QString &provider, const QJsonObject &assignment,
					 int transitionDuration = 0, bool *transitionStarted = nullptr, obs_source_t *transitionTemplate = nullptr)
{
	if (transitionStarted)
		*transitionStarted = false;
	const QString route = assignment.value("canvas").toString();
	const QString sceneName = assignment.value("scene").toString();
	if ((route != "horizontal" && route != "vertical") || sceneName.isEmpty())
		return nullptr;
	const QString key = provider.toLower() + ":" + route;
	obs_canvas_t *canvas = pulseOutputCanvases.value(key, nullptr);
	if (!canvas) {
		obs_video_info ovi = {};
		if (!obs_get_video_info(&ovi))
			return nullptr;
		if (route == "vertical") {
			ovi.base_width = ovi.output_width = 1080;
			ovi.base_height = ovi.output_height = 1920;
		}
		const QByteArray name = pulseOutputCanvasName(provider, route).toUtf8();
		canvas = obs_canvas_create(name.constData(), &ovi, ACTIVATE | SCENE_REF | EPHEMERAL);
		if (!canvas)
			return nullptr;
		pulseOutputCanvases.insert(key, canvas);
	}
	obs_source_t *source = nullptr;
	if (route == "vertical") {
		obs_canvas_t *vertical = PulseWeaverGetVerticalCanvas();
		source = vertical ? obs_canvas_get_source_by_name(vertical, sceneName.toUtf8().constData()) : nullptr;
		obs_canvas_release(vertical);
	} else {
		source = obs_get_source_by_name(sceneName.toUtf8().constData());
	}
	obs_scene_t *base = source ? obs_scene_from_source(source) : nullptr;
	if (!base) {
		obs_source_release(source);
		return nullptr;
	}
	QSet<QString> excluded;
	for (const QJsonValue &value : assignment.value("excluded").toArray())
		excluded.insert(value.toString());
	/* The common path needs no scene copy at all.  Routing the native scene
	 * directly preserves the vertical canvas' exact coordinate space and
	 * avoids reinterpreting portrait transforms through a private scene that
	 * has no owning canvas.  A private duplicate is only necessary when this
	 * platform deliberately excludes video sources in this scene. Audio-only
	 * exclusions are applied by the output mixer and need no video copy. */
	obs_scene_t *duplicate = nullptr;
	obs_source_t *target = source;
	if (PulseStageNeedsVideoExclusion(base, excluded)) {
		const QByteArray duplicateName = QString("Pulse Weaver %1 %2 programme").arg(provider, route).toUtf8();
		duplicate = obs_scene_duplicate(base, duplicateName.constData(), OBS_SCENE_DUP_PRIVATE_REFS);
		if (!duplicate) {
			obs_source_release(source);
			return nullptr;
		}
		obs_scene_enum_items(duplicate, [](obs_scene_t *, obs_sceneitem_t *item, void *opaque) {
			auto *names = static_cast<QSet<QString> *>(opaque);
			obs_source_t *itemSource = obs_sceneitem_get_source(item);
			if (itemSource && names->contains(QString::fromUtf8(obs_source_get_name(itemSource))))
				obs_sceneitem_set_visible(item, false);
			return true;
		}, &excluded);
		target = obs_scene_get_source(duplicate);
	}

	/* Stingers create an internal media source. Reusing one private copy per
	 * output canvas gives that media time to preload and avoids creating a new
	 * decoder at the exact moment the operator changes Stage. */
	obs_source_t *cachedStinger = pulseCachedCanvasStinger(key, transitionTemplate);
	bool started = false;
	/* A Stinger has a fixed duration supplied by its media source. Its caller
	 * therefore passes 0 here; the presence of a transition template, rather
	 * than a Stage duration, decides whether this canvas should transition. */
	obs_source_t *current = (transitionTemplate || transitionDuration > 0) ? obs_canvas_get_channel(canvas, 0) : nullptr;
	if (current && current != target) {
		OBSSourceAutoRelease ownedTransition = cachedStinger ? nullptr : transitionTemplate ?
			obs_source_duplicate(transitionTemplate, "Pulse Weaver Output Transition", true) :
			obs_source_create_private("fade_transition", "Pulse Weaver Output Fade", nullptr);
		obs_source_t *transition = cachedStinger ? cachedStinger : ownedTransition.Get();
		obs_video_info canvasInfo = {};
		if (transition && obs_canvas_get_video_info(canvas, &canvasInfo)) {
			obs_transition_set_size(transition, canvasInfo.base_width, canvasInfo.base_height);
			// The canvas still owns this cached stinger during an interrupted
			// stage. start() settles its old destination before restarting it.
			if (current != transition)
				obs_transition_set(transition, current);
			started = obs_transition_start(transition, OBS_TRANSITION_MODE_AUTO, transitionDuration, target);
			if (started)
				obs_canvas_set_channel(canvas, 0, transition);
		}
	}
	obs_source_release(current);
	if (!started)
		obs_canvas_set_channel(canvas, 0, target);
	if (transitionStarted)
		*transitionStarted = started;
	obs_scene_release(duplicate);
	obs_source_release(source);
	return canvas;
}

QString pulseDefaultObsDataPath()
{
#ifdef __APPLE__
	return QDir::homePath() + "/Library/Application Support/obs-studio";
#endif
	const QString roaming = qEnvironmentVariable("APPDATA");
	return roaming.isEmpty() ? QString() : QDir(roaming).filePath("obs-studio");
}

QString pulseNormaliseObsDataPath(QString path)
{
	path = QDir::cleanPath(path.trimmed());
	if (QDir(QDir(path).filePath("basic")).exists())
		return path;
	const QString child = QDir(path).filePath("obs-studio");
	if (QDir(QDir(child).filePath("basic")).exists())
		return QDir::cleanPath(child);
	return path;
}

bool pulseCopyDirectory(const QString &sourcePath, const QString &destinationPath, QString &error)
{
	const QDir source(sourcePath);
	if (!source.exists()) {
		error = "Source directory does not exist: " + QDir::toNativeSeparators(sourcePath);
		return false;
	}
	if (!QDir().mkpath(destinationPath)) {
		error = "Could not create isolated destination: " + QDir::toNativeSeparators(destinationPath);
		return false;
	}

	QDirIterator iterator(sourcePath, QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot,
			      QDirIterator::Subdirectories);
	while (iterator.hasNext()) {
		const QString sourceItem = iterator.next();
		const QString relative = source.relativeFilePath(sourceItem);
		const QString destinationItem = QDir(destinationPath).filePath(relative);
		const QFileInfo info(sourceItem);
		if (info.isDir()) {
			if (!QDir().mkpath(destinationItem)) {
				error = "Could not create imported directory: " + QDir::toNativeSeparators(destinationItem);
				return false;
			}
		} else {
			QDir().mkpath(QFileInfo(destinationItem).absolutePath());
			if (!QFile::copy(sourceItem, destinationItem)) {
				error = "Could not copy " + QDir::toNativeSeparators(sourceItem);
				return false;
			}
		}
	}
	return true;
}

QString pulseUniqueDirectory(const QString &parent, const QString &base)
{
	QString safe = base.trimmed();
	if (safe.isEmpty())
		safe = "OBS Import";
	QString candidate = safe + "-pulse-import";
	int suffix = 2;
	while (QFileInfo::exists(QDir(parent).filePath(candidate)))
		candidate = safe + "-pulse-import-" + QString::number(suffix++);
	return candidate;
}

obs_source_t *firstCanvasScene(obs_canvas_t *canvas)
{
	obs_source_t *first = nullptr;
	obs_canvas_enum_scenes(
		canvas,
		[](void *opaque, obs_source_t *source) {
			auto **result = static_cast<obs_source_t **>(opaque);
			*result = obs_source_get_ref(source);
			return false;
		},
		&first);
	return first;
}

// Assets are refreshed only when appearance changes; Qt caches rendered SVG icons.
QString pulseAssetPath(const QString &name)
{
    const auto *theme = App()->GetTheme();
    const QString preset = theme && theme->id == "com.pulseweaver.Marquee" ? "marquee" :
        theme && theme->id == "com.pulseweaver.Electric" ? "electric" : "backstage";
    return "theme:PulseWeaver/" + preset + "/icons/" + name + ".svg";
}

void pulseIcon(QAbstractButton *button, const QString &name, int size = 24)
{
    auto update = [button, name, size] {
        QString path = pulseAssetPath(name);
        if (size < 30)
            path.replace("/icons/", "/glyphs/");
        button->setIcon(QIcon(path));
        button->setIconSize(QSize(size, size));
    };
    QObject::connect(App(), &OBSApp::StyleChanged, button, update);
    update();
}

QLabel *pulseEmblem(const QString &name, int size = 32)
{
    auto *label = new QLabel;
    label->setFixedSize(size, size);
    label->setAttribute(Qt::WA_TransparentForMouseEvents);
    auto update = [label, name, size] {
        const qreal ratio = label->devicePixelRatioF();
        label->setPixmap(QIcon(pulseAssetPath(name)).pixmap(QSize(size, size), ratio));
    };
    QObject::connect(App(), &OBSApp::StyleChanged, label, update);
    update();
    return label;
}

QFrame *pulseBanner(const QString &title, const QString &copy, const QString &icon)
{
    auto *frame = new QFrame;
    frame->setObjectName("PulseWeaverBanner");
    auto *row = new QHBoxLayout(frame);
    row->setContentsMargins(12, 8, 12, 8);
    row->setSpacing(12);
    row->addWidget(pulseEmblem(icon, 48));
    auto *text = new QVBoxLayout;
    auto *heading = new QLabel(title);
    heading->setObjectName("PulseWeaverHeading");
    auto *description = new QLabel(copy);
    description->setObjectName("PulseWeaverMuted");
    description->setWordWrap(true);
    text->addWidget(heading);
    text->addWidget(description);
    row->addLayout(text, 1);
    return frame;
}

QFrame *card(const QString &title, const QString &copy, const QString &accent, QWidget *parent = nullptr)
{
	auto *frame = new QFrame(parent);
	frame->setObjectName("PulseWeaverCard");
	Q_UNUSED(accent);
	auto *layout = new QVBoxLayout(frame);
	layout->setContentsMargins(12, 8, 12, 8);
	layout->setSpacing(8);
	auto *heading = new QLabel(title);
	heading->setObjectName("PulseWeaverCardTitle");
	auto *titleRow = new QHBoxLayout;
	titleRow->setSpacing(8);
    titleRow->addWidget(pulseEmblem(title.contains("Chat") ? "chat" : "lights", 30));
    titleRow->addWidget(heading, 1);
    layout->addLayout(titleRow);
	auto *description = new QLabel(copy);
	description->setObjectName("PulseWeaverMuted");
	description->setWordWrap(true);
	layout->addWidget(description);
	return frame;
}

class PulsePreviewDisplay final : public OBSQTDisplay {
public:
	using OBSQTDisplay::OBSQTDisplay;
	std::function<void()> clicked;

protected:
	void mousePressEvent(QMouseEvent *event) override
	{
		if (event->button() == Qt::LeftButton && clicked)
			clicked();
		OBSQTDisplay::mousePressEvent(event);
	}
};

class PulseStatsCard final : public QFrame {
public:
	using QFrame::QFrame;
	std::function<void()> clicked;

protected:
	void mousePressEvent(QMouseEvent *event) override
	{
		if (event->button() == Qt::LeftButton && clicked)
			clicked();
		QFrame::mousePressEvent(event);
	}
};

class PulseAspectHost final : public QWidget {
public:
	PulseAspectHost(QWidget *content, int aspectWidth, int aspectHeight, QWidget *parent = nullptr)
		: QWidget(parent), content(content), aspectWidth(aspectWidth), aspectHeight(aspectHeight)
	{
		content->setParent(this);
		content->show();
		setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
		setMinimumSize(160, 120);
	}

protected:
	void resizeEvent(QResizeEvent *event) override
	{
		QWidget::resizeEvent(event);
		if (!content || aspectWidth <= 0 || aspectHeight <= 0)
			return;
		const QSize available = event->size();
		int width = available.width();
		int height = qRound(double(width) * double(aspectHeight) / double(aspectWidth));
		if (height > available.height()) {
			height = available.height();
			width = qRound(double(height) * double(aspectWidth) / double(aspectHeight));
		}
		content->setGeometry((available.width() - width) / 2, (available.height() - height) / 2,
				     std::max(1, width), std::max(1, height));
	}

private:
	QPointer<QWidget> content;
	int aspectWidth;
	int aspectHeight;
};

class PulseResponsiveStageBar final : public QWidget {
public:
	explicit PulseResponsiveStageBar(QWidget *parent = nullptr) : QWidget(parent), grid(new QGridLayout(this))
	{
		grid->setContentsMargins(0, 0, 0, 0);
		grid->setHorizontalSpacing(7);
		grid->setVerticalSpacing(6);
	}

	void addWidget(QWidget *widget, int = 0)
	{
		controls.push_back(widget);
		if (controls.size() == 10)
			arrange(true);
	}

protected:
	void resizeEvent(QResizeEvent *event) override
	{
		QWidget::resizeEvent(event);
		arrange(false);
	}

private:
	void arrange(bool force)
	{
		if (controls.size() != 10)
			return;
		const bool compact = width() < 1180;
		if (!force && arranged && compact == compactLayout)
			return;
		while (QLayoutItem *item = grid->takeAt(0))
			delete item;
		for (int column = 0; column < 10; ++column)
			grid->setColumnStretch(column, 0);
		if (compact) {
			/* Keep the wide-screen row unchanged. At 4:3, put the two
			 * aspect-specific transition controls on a second row so the
			 * Stage name and action buttons remain readable. */
			grid->addWidget(controls[0], 0, 0);
			grid->addWidget(controls[1], 0, 1, 1, 5);
			grid->addWidget(controls[8], 0, 6);
			grid->addWidget(controls[9], 0, 7);
			grid->addWidget(controls[2], 1, 0);
			grid->addWidget(controls[3], 1, 1);
			grid->addWidget(controls[4], 1, 2);
			grid->addWidget(controls[5], 1, 3);
			grid->addWidget(controls[6], 1, 4);
			grid->addWidget(controls[7], 1, 5);
			grid->setColumnStretch(1, 1);
		} else {
			for (int column = 0; column < controls.size(); ++column)
				grid->addWidget(controls[column], 0, column);
			grid->setColumnStretch(1, 1);
		}
		compactLayout = compact;
		arranged = true;
	}

	QGridLayout *grid;
	QList<QWidget *> controls;
	bool arranged = false;
	bool compactLayout = false;
};

// Keep transport controls visible while the working surface scrolls on small displays.
class PulseTransportBar final : public QWidget {
    QGridLayout *grid;
    QList<QWidget *> controls;
    bool compact = false;
    bool arranged = false;
    void arrange() {
        if (controls.size() != 6) return;
        const bool nextCompact = width() < 650;
        if (arranged && nextCompact == compact) return;
        while (auto *item = grid->takeAt(0)) delete item;
        for (int i = 0; i < 6; ++i) grid->setColumnStretch(i, 0);
        if (nextCompact) {
            for (int i = 0; i < 2; ++i) grid->addWidget(controls[i], 0, i);
            grid->addWidget(controls[2], 0, 2, 1, 3);
            grid->addWidget(controls[3], 1, 0, 1, 3);
            grid->addWidget(controls[4], 1, 3);
            grid->addWidget(controls[5], 1, 4);
            grid->setColumnStretch(2, 1);
        } else {
            for (int i : {0, 1, 3, 4, 5}) grid->addWidget(controls[i], 0, i);
            grid->setColumnStretch(3, 1);
        }
        compact = nextCompact;
        arranged = true;
    }
protected:
    void resizeEvent(QResizeEvent *event) override { QWidget::resizeEvent(event); arrange(); }
public:
    explicit PulseTransportBar(QWidget *parent = nullptr) : QWidget(parent), grid(new QGridLayout(this)) {
        setObjectName("PulseWeaverTransport");
        grid->setContentsMargins(0, 6, 0, 0);
        grid->setHorizontalSpacing(7);
        grid->setVerticalSpacing(6);
    }
    void addWidget(QWidget *widget) { controls.append(widget); arrange(); }
    void addStretch() {}
};

QWidget *displayCard(const QString &kicker, const QString &title, OBSQTDisplay *display, const QString &accent,
		     int aspectWidth, int aspectHeight, QLabel **titleLabel = nullptr)
{
	auto *frame = new QFrame;
	frame->setObjectName("PulseWeaverStage");
	Q_UNUSED(accent);
	auto *layout = new QVBoxLayout(frame);
	layout->setContentsMargins(12, 8, 12, 8);
	layout->setSpacing(8);
	auto *head = new QGridLayout;
	auto *ratio = new QLabel(kicker);
	ratio->setObjectName("PulseWeaverKicker");
	auto *name = new QLabel(title);
	name->setTextFormat(Qt::RichText);
	name->setObjectName("PulseWeaverKicker");
	name->setAlignment(Qt::AlignCenter);
	/* Destination/scene names change while cycling a fixed preview surface.
	 * Ignore the text width so the programme canvas geometry stays fixed. */
	name->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
	name->setMinimumWidth(0);
	if (titleLabel)
		*titleLabel = name;
	auto *native = new QLabel("Program");
	native->setObjectName("PulseWeaverBadge");
	native->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
	head->addWidget(ratio, 0, 0);
	head->addWidget(name, 0, 1);
	head->addWidget(native, 0, 2);
	head->setColumnStretch(0, 1);
	head->setColumnStretch(1, 4);
	head->setColumnStretch(2, 1);
	layout->addLayout(head);
	display->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
	display->setMinimumSize(1, 1);
	display->SetDisplayBackgroundColor(QColor("#03050A"));
	layout->addWidget(new PulseAspectHost(display, aspectWidth, aspectHeight, frame), 1);
	return frame;
}

void renderSourceFit(uint32_t canvasWidth, uint32_t canvasHeight, uint32_t displayWidth,
		     uint32_t displayHeight)
{
	if (!canvasWidth || !canvasHeight || !displayWidth || !displayHeight)
		return;

	const float scale = std::min(float(displayWidth) / float(canvasWidth),
				     float(displayHeight) / float(canvasHeight));
	const int width = int(float(canvasWidth) * scale);
	const int height = int(float(canvasHeight) * scale);
	const int x = (int(displayWidth) - width) / 2;
	const int y = (int(displayHeight) - height) / 2;

	gs_viewport_push();
	gs_projection_push();
	gs_ortho(0.0f, float(canvasWidth), 0.0f, float(canvasHeight), -100.0f, 100.0f);
	gs_set_viewport(x, y, width, height);
	// output_frames runs before render_displays, so this includes this frame's transition.
	obs_render_main_texture_src_color_only();
	gs_reset_viewport();
	gs_projection_pop();
	gs_viewport_pop();
}

void renderCanvasFit(obs_canvas_t *canvas, uint32_t canvasWidth, uint32_t canvasHeight, uint32_t displayWidth,
		     uint32_t displayHeight)
{
	if (!canvas || !canvasWidth || !canvasHeight || !displayWidth || !displayHeight)
		return;

	const float scale = std::min(float(displayWidth) / float(canvasWidth),
				     float(displayHeight) / float(canvasHeight));
	const int width = int(float(canvasWidth) * scale);
	const int height = int(float(canvasHeight) * scale);
	const int x = (int(displayWidth) - width) / 2;
	const int y = (int(displayHeight) - height) / 2;

	gs_viewport_push();
	gs_projection_push();
	gs_ortho(0.0f, float(canvasWidth), 0.0f, float(canvasHeight), -100.0f, 100.0f);
	gs_set_viewport(x, y, width, height);
	// Reuse the programme composition, including transitions and source exclusions.
	// A canvas without a video mix still needs the direct-view preview path.
	if (obs_canvas_has_video(canvas))
		obs_render_canvas_texture_src_color_only(canvas);
	else
		obs_canvas_render(canvas);
	gs_reset_viewport();
	gs_projection_pop();
	gs_viewport_pop();
}

} // namespace

void OBSBasic::InitPulseWeaverShell()
{
	setProperty("pulseWeaverGoLiveSession", false);
	setObjectName("PulseWeaverStudio");
	setWindowTitle("PULSE WEAVER — Streaming Studio");
	setWindowIcon(QIcon(":/res/images/pulseweaver.png"));
	setMinimumSize(1180, 720);
	/* OBS remains the native engine, but its legacy product chrome is not
	 * part of Pulse Weaver's interface. Commands are retained in the branded
	 * Studio menu below and operational state lives in Show Control. */
	ui->menubar->hide();
	ui->statusbar->hide();

	auto *root = ui->centralwidget;
	// The application theme also owns dialogs and plugin workspaces.

	/* Move OBS's real editor into the Camera page.  Nothing is imitated:
	 * preview selection outlines, source interaction and context toolbars remain
	 * the upstream native widgets. */
	ui->verticalLayout->removeWidget(ui->canvasEditor);
	ui->verticalLayout->removeWidget(ui->contextContainer);

	auto *header = new QFrame(root);
	header->setObjectName("PulseWeaverHeader");
	auto *headerLayout = new QHBoxLayout(header);
	headerLayout->setContentsMargins(8, 7, 8, 7);
	headerLayout->setSpacing(7);
	auto *brandMark = new QLabel(header);
	brandMark->setPixmap(QPixmap(":/res/images/pulseweaver.png")
				     .scaled(38, 38, Qt::KeepAspectRatio, Qt::SmoothTransformation));
	brandMark->setFixedSize(42, 42);
	brandMark->setAlignment(Qt::AlignCenter);
	headerLayout->addWidget(brandMark);
	auto *brand = new QLabel("PULSE WEAVER", header);
	brand->setObjectName("PulseWeaverBrand");
	brand->setTextFormat(Qt::PlainText);
	headerLayout->addWidget(brand);
	auto *home = new QPushButton("Show Control", header);
	home->setObjectName("PulseWeaverHome");
	home->setProperty("pulseWorkspaceIndex", 0);
	pulseIcon(home, "show", 28);
	headerLayout->addWidget(home);

	auto addWorkspace = [this, header, headerLayout](const QString &text, const char *name, int index) {
		auto *button = new QPushButton(text, header);
		button->setObjectName("PulseWeaverNav");
		button->setProperty("workspace", name);
		button->setProperty("pulseWorkspaceIndex", index);
		pulseIcon(button, name, 28);
		button->setCursor(Qt::PointingHandCursor);
		connect(button, &QPushButton::clicked, this, [this, index] { SetPulseWeaverWorkspace(index); });
		headerLayout->addWidget(button, 1);
	};
	addWorkspace("Lights", "lights", 1);
	addWorkspace("Camera", "camera", 2);
	addWorkspace("Action", "action", 3);
	headerLayout->addStretch();
	auto *pulseAi = new QPushButton("PULSE AI", header);
	pulseAi->setObjectName("PulseWeaverAiButton");
	pulseAi->setToolTip("Ask the locally authenticated Codex agent to modify the current show through validated Pulse Weaver operations");
	pulseAi->hide();
	headerLayout->addWidget(pulseAi);
	auto *studioMenuButton = new QPushButton("Studio", header);
	studioMenuButton->setObjectName("PulseWeaverUtility");
	pulseIcon(studioMenuButton, "studio");
	auto *studioMenu = new QMenu(studioMenuButton);
	auto *appearanceMenu = studioMenu->addMenu("Appearance");
	appearanceMenu->setObjectName("PulseWeaverAppearanceMenu");
	auto *themeActions = new QActionGroup(appearanceMenu);
	for (const auto &preset : QList<QPair<QString, QString>>{
		{"Backstage", "com.pulseweaver.Studio"}, {"Marquee", "com.pulseweaver.Marquee"},
		{"Electric", "com.pulseweaver.Electric"}}) {
		auto *action = appearanceMenu->addAction(preset.first);
		action->setCheckable(true);
		action->setData(preset.second);
		themeActions->addAction(action);
		connect(action, &QAction::triggered, this, [id = preset.second] {
			if (App()->SetTheme(id)) {
				config_set_string(App()->GetUserConfig(), "Appearance", "Theme", id.toUtf8().constData());
				config_save_safe(App()->GetUserConfig(), "tmp", nullptr);
			}
		});
	}
	auto syncThemeActions = [themeActions] {
		for (auto *action : themeActions->actions())
			action->setChecked(App()->GetTheme() && action->data().toString() == App()->GetTheme()->id);
	};
	connect(App(), &OBSApp::StyleChanged, this, syncThemeActions);
	syncThemeActions();
	appearanceMenu->addSeparator();
	auto *feedback = appearanceMenu->addAction("Interaction feedback");
	feedback->setCheckable(true);
	config_set_default_bool(App()->GetUserConfig(), "Appearance", "InteractionFeedback", true);
	feedback->setChecked(config_get_bool(App()->GetUserConfig(), "Appearance", "InteractionFeedback"));
	qApp->setProperty("pulseWeaverInteractionFeedback", feedback->isChecked());
	new PulseVisual::InteractionFeedback(this);
	new PulseVisual::WindowChrome(this);
	connect(feedback, &QAction::toggled, this, [](bool enabled) {
		qApp->setProperty("pulseWeaverInteractionFeedback", enabled);
		config_set_bool(App()->GetUserConfig(), "Appearance", "InteractionFeedback", enabled);
		config_save_safe(App()->GetUserConfig(), "tmp", nullptr);
	});
	studioMenu->addSeparator();
	studioMenu->addMenu(ui->menu_File);
	studioMenu->addMenu(ui->menuBasic_MainMenu_Edit);
	studioMenu->addMenu(ui->viewMenu);
	studioMenu->addMenu(ui->menuDocks);
	studioMenu->addMenu(ui->profileMenu);
	studioMenu->addMenu(ui->sceneCollectionMenu);
	studioMenu->addMenu(ui->menuTools);
	studioMenu->addMenu(ui->menuBasic_MainMenu_Help);
	studioMenuButton->setMenu(studioMenu);
	studioMenuButton->setToolTip("Profiles, scene collections, files, docks and native studio tools");
	headerLayout->addWidget(studioMenuButton);
	auto *plugins = new QPushButton("Plugins", header);
	plugins->setObjectName("PulseWeaverUtility");
	pulseIcon(plugins, "plugins");
	connect(plugins, &QPushButton::clicked, this, [this] { ui->actionOpenPluginManager->trigger(); });
	headerLayout->addWidget(plugins);
	auto *settings = new QPushButton("Settings", header);
	settings->setObjectName("PulseWeaverUtility");
	pulseIcon(settings, "settings");
	connect(settings, &QPushButton::clicked, this, &OBSBasic::on_action_Settings_triggered);
	headerLayout->addWidget(settings);
	connect(home, &QPushButton::clicked, this, [this] { SetPulseWeaverWorkspace(0); });
	ui->verticalLayout->insertWidget(0, header);

	pulsePages = new QStackedWidget(root);
	pulsePages->setObjectName("PulseWeaverPages");
	ui->verticalLayout->addWidget(pulsePages, 1);

	/* SHOW CONTROL */
	pulseShowPage = new QWidget(pulsePages);
	pulseShowPage->setObjectName("PulseWeaverPage");
	auto *showLayout = new QVBoxLayout(pulseShowPage);
	showLayout->setContentsMargins(12, 12, 12, 8);
	showLayout->setSpacing(10);
	auto *showBanner = new QFrame;
    showBanner->setObjectName("PulseWeaverBanner");
    auto *showHead = new QHBoxLayout(showBanner);
    showHead->setContentsMargins(12, 3, 12, 3);
    showHead->setSpacing(12);
    showHead->addWidget(pulseEmblem("show", 32));
	auto *showTitleBox = new QVBoxLayout;
	auto *showKicker = new QLabel("Live production");
	showKicker->setObjectName("PulseWeaverKicker");
	showTitleBox->addWidget(showKicker);
	showKicker->hide();
	auto *showTitle = new QLabel("Show Control");
	showTitle->setObjectName("PulseWeaverHeading");
	showTitleBox->addWidget(showTitle);
	showHead->addLayout(showTitleBox);
	showHead->addStretch();
	pulseEngineStatus = new QLabel("Preparing studio…");
	pulseEngineStatus->setObjectName("PulseWeaverStatus");
	showHead->addWidget(pulseEngineStatus);
	showLayout->addWidget(showBanner);

	/* Programme surfaces and operating controls form the main workspace. Chat
	 * is a genuine full-height, horizontally resizable operator rail. */
	auto *showMain = new QWidget;
	auto *showMainLayout = new QVBoxLayout(showMain);
	showMainLayout->setContentsMargins(0, 0, 0, 0);
	showMainLayout->setSpacing(8);
	auto *showGrid = new QGridLayout;
	showGrid->setContentsMargins(0, 0, 0, 0);
	showGrid->setSpacing(10);
	showGrid->setColumnMinimumWidth(0, 230);
	showGrid->setColumnMinimumWidth(1, 460);
	showGrid->setColumnStretch(0, 1);
	showGrid->setColumnStretch(1, 3);
	showGrid->setRowStretch(0, 1);

	auto *verticalColumn = new QWidget;
	auto *verticalLayout = new QVBoxLayout(verticalColumn);
	verticalLayout->setContentsMargins(0, 0, 0, 0);
	verticalLayout->setSpacing(0);
	auto *verticalPreview = new PulsePreviewDisplay;
	verticalPreview->clicked = [this] { CyclePulseWeaverPlatformPreview(true, 1); };
	pulseVerticalDisplay = verticalPreview;
	pulseVerticalDisplay->setObjectName("PulseWeaverVerticalPreview");
	pulseVerticalDisplay->setMinimumSize(180, 260);
	QLabel *verticalPreviewTitle = nullptr;
	auto *verticalCard = displayCard("9:16", "Portrait program", pulseVerticalDisplay, "#7C3FA0", 9, 16,
					 &verticalPreviewTitle);
	pulseVerticalPreviewCard = verticalCard;
	pulseVerticalPreviewTitle = verticalPreviewTitle;
	verticalCard->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
	verticalLayout->addWidget(verticalCard, 1);
	/* Keep this state available to the engine without permanently consuming
	 * portrait canvas height.  A repair action is surfaced only if provisioning
	 * genuinely fails. */
	pulseVerticalStatus = new QLabel(verticalColumn);
	pulseVerticalStatus->setObjectName("PulseWeaverMuted");
	pulseVerticalStatus->setVisible(false);
	verticalLayout->addWidget(pulseVerticalStatus);
	pulseRepairVerticalButton = new QPushButton("Set up portrait canvas", verticalColumn);
	pulseRepairVerticalButton->setObjectName("PulseWeaverControl");
	pulseRepairVerticalButton->setVisible(false);
	connect(pulseRepairVerticalButton, &QPushButton::clicked, this, &OBSBasic::EnsurePulseWeaverVerticalCanvas);
	verticalLayout->addWidget(pulseRepairVerticalButton);
	showGrid->addWidget(verticalColumn, 0, 0);

	auto *horizontalPreview = new PulsePreviewDisplay;
	horizontalPreview->clicked = [this] { CyclePulseWeaverPlatformPreview(false, 1); };
	pulseHorizontalDisplay = horizontalPreview;
	pulseHorizontalDisplay->setObjectName("PulseWeaverHorizontalPreview");
	QLabel *horizontalPreviewTitle = nullptr;
	auto *horizontalCard = displayCard("16:9", "Landscape program", pulseHorizontalDisplay, "#31516D", 16, 9,
					   &horizontalPreviewTitle);
	pulseHorizontalPreviewCard = horizontalCard;
	pulseHorizontalPreviewTitle = horizontalPreviewTitle;
	horizontalCard->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
	showGrid->addWidget(horizontalCard, 0, 1);

	auto *statsCard = new PulseStatsCard(showMain);
	statsCard->setObjectName("PulseWeaverStreamStats");
	statsCard->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
	statsCard->setCursor(Qt::PointingHandCursor);
	statsCard->setToolTip("Click to cycle configured live outputs");
	statsCard->clicked = [this] { CyclePulseWeaverStreamStats(1); };
	pulseStreamStatsCard = statsCard;
	auto *statsLayout = new QVBoxLayout(statsCard);
	statsLayout->setContentsMargins(12, 4, 12, 4);
	statsLayout->setSpacing(8);
	auto *statsHeader = new QHBoxLayout;
	statsHeader->setSpacing(12);
	auto *identity = new QWidget(statsCard);
	auto *identityLayout = new QHBoxLayout(identity);
	identityLayout->setContentsMargins(0, 0, 0, 0);
	identityLayout->setSpacing(8);
	pulseStreamStatsTitle = new QLabel("Output · None selected", statsCard);
	pulseStreamStatsTitle->setObjectName("PulseWeaverStatsTitle");
	identityLayout->addWidget(pulseEmblem("stats", 22));
	identityLayout->addWidget(pulseStreamStatsTitle);
	// Reserve the longest output label so cycling platforms cannot move metrics.
	auto sizeIdentity = [identity, title = pulseStreamStatsTitle] {
		title->ensurePolished();
		identity->setFixedWidth(title->fontMetrics().horizontalAdvance("Output · None configured") + 38);
	};
	sizeIdentity();
	connect(App(), &OBSApp::StyleChanged, identity, sizeIdentity);
	statsHeader->addWidget(identity);
	statsLayout->addLayout(statsHeader);
	auto *metrics = new QHBoxLayout;
	metrics->setContentsMargins(0, 0, 0, 0);
	metrics->setSpacing(10);
	auto addMetric = [statsCard, metrics](const QString &name, QPointer<QLabel> &value) {
		auto *column = new QVBoxLayout;
		column->setSpacing(0);
		column->setContentsMargins(0, 0, 0, 0);
		auto *label = new QLabel(name, statsCard);
		label->setObjectName("PulseWeaverStatsLabel");
		value = new QLabel("—", statsCard);
		value->setObjectName("PulseWeaverStatsValue");
		value->setMinimumWidth(0);
		value->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
		column->addWidget(label);
		column->addWidget(value);
		metrics->addLayout(column, 1);
	};
	addMetric("Status", pulseStreamStatsState);
	addMetric("Uptime", pulseStreamStatsUptime);
	addMetric("Bitrate", pulseStreamStatsBitrate);
	addMetric("Dropped frames", pulseStreamStatsDropped);
	statsHeader->addLayout(metrics, 1);
	auto *detailsButton = new QPushButton("Details", statsCard);
	detailsButton->setFixedWidth(76);
	detailsButton->setMinimumHeight(32);
	statsHeader->addWidget(detailsButton);
	auto *details = new QWidget(statsCard);
	auto *detailsLayout = new QHBoxLayout(details);
	detailsLayout->setContentsMargins(0, 0, 0, 0);
	detailsLayout->setSpacing(12);
	detailsLayout->addWidget(new QLabel("Data sent", details));
	pulseStreamStatsSent = new QLabel("—", details);
	detailsLayout->addWidget(pulseStreamStatsSent);
	auto *droppedDetail = new QLabel("Dropped frames: 0", details);
	droppedDetail->setObjectName("PulseWeaverDroppedDetail");
	detailsLayout->addWidget(droppedDetail);
	detailsLayout->addStretch();
	statsLayout->addWidget(details);
	details->hide();
	connect(detailsButton, &QPushButton::clicked, details, [details, detailsButton] {
		details->setVisible(details->isHidden());
		detailsButton->setText(details->isHidden() ? "Details" : "Less");
	});
	showMainLayout->addWidget(statsCard);

	auto *activityCard = card("Chat", "Twitch · YouTube · Kick",
				  "#F97316");
	auto *audienceTabs = new QTabWidget(activityCard);
	auto *chatPage = new QWidget(audienceTabs);
	auto *chatLayout = new QVBoxLayout(chatPage);
	chatLayout->setContentsMargins(0, 0, 0, 0);
	chatLayout->setSpacing(5);
	auto *chatStatus = new QLabel("No connected chats yet.");
	chatStatus->setObjectName("PulseWeaverChatStatus");
	// Colour and type follow the selected theme.
	chatStatus->setWordWrap(true);
	chatStatus->setMinimumWidth(0);
	chatStatus->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
	pulseChatStatus = chatStatus;
	auto *chatFeed = new PulseChat::Feed(chatPage);
	chatFeed->setObjectName("PulseWeaverChatFeed");
	pulseChatFeed = chatFeed;
	chatFeed->setMinimumHeight(100);
	chatFeed->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
	chatFeed->setSelectionMode(QAbstractItemView::NoSelection);
	chatFeed->setProperty("pulseWeaverChatFilter", "all");
	chatFeed->setProperty("pulseWeaverChatAutoScroll", true);
	// Explicit local fixture for UI regression tests; never sends platform messages.
	const QString chatFixture = qEnvironmentVariable("PULSEWEAVER_CHAT_FIXTURE");
	if (!chatFixture.isEmpty()) QTimer::singleShot(800, chatFeed, [chatFeed, chatFixture] {
		QFile file(chatFixture);
		if (!file.open(QIODevice::ReadOnly)) return;
		for (const auto &value : QJsonDocument::fromJson(file.readAll()).array()) {
			const auto row = value.toObject();
			QStringList badges; for (const auto &badge : row.value("badges").toArray()) badges << badge.toString();
			QHash<QString, QUrl> images;
			const auto urls = row.value("images").toObject();
			for (auto it = urls.begin(); it != urls.end(); ++it) images.insert(it.key(), QUrl(it.value().toString()));
			PulseChat::append(chatFeed, row.value("platform").toString(), row.value("user").toString(),
				row.value("message").toString(), row.value("colour").toString(), badges, images,
				row.value("user_id").toString(), row.value("message_id").toString(), false, row.value("fragments").toArray());
		}
	});
	auto *chatFilters = new QHBoxLayout;
	chatFilters->setContentsMargins(0, 0, 0, 0);
	chatFilters->setSpacing(3);
	auto *readFilter = new QComboBox(chatPage);
    readFilter->setAccessibleName("Read chat from");
    readFilter->addItem("All chats", "all");
    readFilter->addItem("Twitch", "twitch");
    readFilter->addItem("YouTube", "youtube");
    readFilter->addItem("Kick", "kick");
    connect(readFilter, &QComboBox::currentIndexChanged, this, [chatFeed, readFilter] {
        pulseApplyChatFilter(chatFeed, readFilter->currentData().toString());
        chatFeed->setProperty("pulseWeaverChatAutoScroll", true);
        chatFeed->scrollToBottom();
    });
    chatFilters->addWidget(readFilter, 1);
    auto *timestamps = new QToolButton(chatPage);
    timestamps->setText("Timestamps"); timestamps->setAccessibleName("Show timestamps"); timestamps->setCheckable(true);
    pulseIcon(timestamps, "clock", 24);
    timestamps->setToolTip("Show message timestamps");
    connect(timestamps, &QToolButton::toggled, this, [chatFeed](bool enabled) {
        chatFeed->setProperty("pulseWeaverChatTimestamps", enabled); chatFeed->doItemsLayout(); chatFeed->viewport()->update();
    });
    chatFilters->addWidget(timestamps);
    chatLayout->addLayout(chatFilters);
	chatLayout->addWidget(chatFeed, 1);
	auto *chatToolbar = new QHBoxLayout;
	chatToolbar->setContentsMargins(0, 0, 0, 0);
	chatToolbar->addWidget(new QLabel("Send to", chatPage));
	pulseChatProvider = new QComboBox(chatPage);
	pulseChatProvider->setObjectName("PulseWeaverChatProvider");
	pulseChatProvider->setAccessibleName("Chat destination");
	pulseChatProvider->addItem("All chats", "all");
	pulseChatProvider->addItem("Twitch", "twitch");
	pulseChatProvider->addItem("YouTube", "youtube");
	pulseChatProvider->addItem("Kick", "kick");
	pulseChatProvider->setToolTip("Chat destination");
	pulseChatProvider->setMaximumWidth(130);
	chatToolbar->addWidget(pulseChatProvider);
	chatLayout->removeWidget(chatStatus);
	chatLayout->addWidget(chatStatus);
	chatLayout->addLayout(chatToolbar);
	auto *newMessages = new QPushButton(chatPage);
	newMessages->setObjectName("PulseWeaverChatNewMessages");
	newMessages->hide();
	connect(newMessages, &QPushButton::clicked, this, [chatFeed, newMessages] {
		chatFeed->setProperty("pulseWeaverChatAutoScroll", true);
		chatFeed->scrollToBottom();
		chatFeed->setProperty("pulseWeaverUnreadCount", 0);
		newMessages->hide();
	});
	chatLayout->insertWidget(2, newMessages, 0, Qt::AlignHCenter);
	connect(chatFeed->verticalScrollBar(), &QScrollBar::valueChanged, this, [chatFeed, newMessages](int) {
		if (pulseChatAtBottom(chatFeed)) {
			chatFeed->setProperty("pulseWeaverUnreadCount", 0);
			newMessages->hide();
		}
	});
	connect(chatFeed, &QListWidget::customContextMenuRequested, this, [this, chatFeed](const QPoint &position) {
		auto *item = chatFeed->itemAt(position);
		if (!item)
			return;
		const QString platform = item->data(PulseChatPlatformRole).toString();
		/* Twitch is handled by the native Twitch runtime, which owns its OAuth
		 * token. YouTube is handled here because its account/token lives in the
		 * frontend wrapper; Kick exposes its creator moderation page. */
		if (platform == "twitch" || platform == "kick")
			return;
		const QString user = item->data(PulseChatUserRole).toString();
		const QString userId = item->data(PulseChatUserIdRole).toString();
		const QString messageId = item->data(PulseChatMessageIdRole).toString();
		const QString liveChatId = item->data(PulseChatLiveChatIdRole).toString();
		QMenu menu(chatFeed);
		menu.addSection(platform.toUpper() + " · " + user);
		auto *copy = menu.addAction("Copy message");
		connect(copy, &QAction::triggered, this, [text = item->data(PulseChatTextRole).toString()] { QApplication::clipboard()->setText(text); });
		if (platform == "youtube") {
			auto *deleteMessage = menu.addAction("Delete message");
			menu.addSeparator();
			auto *timeoutTen = menu.addAction("Timeout 10 minutes");
			auto *timeoutHour = menu.addAction("Timeout 1 hour");
			auto *ban = menu.addAction("Ban user");
			const bool ready = !liveChatId.isEmpty();
			const QStringList roles = item->data(PulseChat::Badges).toStringList();
			const bool protectedUser = roles.contains("owner") || roles.contains("moderator") || roles.contains("mod");
			deleteMessage->setEnabled(ready && !messageId.isEmpty() && !item->data(PulseChat::Deleted).toBool());
			timeoutTen->setEnabled(ready && !userId.isEmpty() && !protectedUser);
			timeoutHour->setEnabled(timeoutTen->isEnabled()); ban->setEnabled(timeoutTen->isEnabled());
			if (!ready) menu.addSection("YouTube moderation requires its active live chat");
			const QAction *choice = menu.exec(chatFeed->viewport()->mapToGlobal(position));
			if (choice == deleteMessage) ModeratePulseWeaverYouTubeChat(messageId, {}, -1, liveChatId);
			else if (choice == timeoutTen) ModeratePulseWeaverYouTubeChat({}, userId, 600, liveChatId);
			else if (choice == timeoutHour) ModeratePulseWeaverYouTubeChat({}, userId, 3600, liveChatId);
			else if (choice == ban) ModeratePulseWeaverYouTubeChat({}, userId, 0, liveChatId);
		} else if (platform == "kick") {
			auto *openDashboard = menu.addAction("Open Kick Creator Dashboard");
			if (menu.exec(chatFeed->viewport()->mapToGlobal(position)) == openDashboard)
				QDesktopServices::openUrl(QUrl("https://kick.com/dashboard"));
		}
	});
	auto *composer = new QHBoxLayout;
	composer->setContentsMargins(0, 0, 0, 0);
	composer->setSpacing(4);
	auto *chatInput = new QLineEdit(chatPage);
	chatInput->setObjectName("PulseWeaverChatInput");
	chatInput->setAccessibleName("Chat message");
	chatInput->setPlaceholderText("Message connected chat…");
	chatInput->setEnabled(false);
	pulseChatInput = chatInput;
	auto *chatSend = new QPushButton(chatPage);
    pulseIcon(chatSend, "send", 24);
    chatSend->setAccessibleName("Send message");
    chatSend->setToolTip("Send message");
	chatSend->setObjectName("PulseWeaverChatSend");
	chatSend->setFixedSize(34, 30);
	chatSend->setToolTip("Send message");
	chatSend->setEnabled(false);
	pulseChatSend = chatSend;
	composer->addWidget(chatInput, 1);
	composer->addWidget(chatSend);
	connect(pulseChatProvider, &QComboBox::currentIndexChanged, this, [this](int) {
		RefreshPulseWeaverChatComposer();
	});
	connect(chatSend, &QPushButton::clicked, this, [this] {
		if (!pulseChatProvider || !pulseChatInput)
			return;
		const QString provider = pulseChatProvider->currentData().toString();
		if (provider == "all")
			pulseChatInput->setProperty("pulseWeaverBroadcastMessage", pulseChatInput->text().trimmed());
		if (provider == "youtube" || provider == "all")
			SendPulseWeaverYouTubeChat();
		if (provider == "all")
			QTimer::singleShot(0, pulseChatInput, [input = QPointer<QLineEdit>(pulseChatInput)] {
				if (input) { input->clear(); input->setProperty("pulseWeaverBroadcastMessage", QVariant()); }
			});
	});
	connect(chatInput, &QLineEdit::returnPressed, this, [this] {
		if (!pulseChatProvider || !pulseChatInput)
			return;
		const QString provider = pulseChatProvider->currentData().toString();
		if (provider == "all")
			pulseChatInput->setProperty("pulseWeaverBroadcastMessage", pulseChatInput->text().trimmed());
		if (provider == "youtube" || provider == "all")
			SendPulseWeaverYouTubeChat();
		if (provider == "all")
			QTimer::singleShot(0, pulseChatInput, [input = QPointer<QLineEdit>(pulseChatInput)] {
				if (input) { input->clear(); input->setProperty("pulseWeaverBroadcastMessage", QVariant()); }
			});
	});
	chatLayout->addLayout(composer);
	audienceTabs->addTab(chatPage, "CHAT");
	auto *activityPage = new QWidget(audienceTabs);
	auto *activityLayout = new QVBoxLayout(activityPage);
	activityLayout->setContentsMargins(0, 0, 0, 0);
	auto *activity = new QListWidget(activityPage);
	activity->setObjectName("PulseWeaverActivity");
	activity->addItem("Pulse Weaver native event bus is ready for connections.");
	activityLayout->addWidget(activity);
	audienceTabs->addTab(activityPage, "EVENTS");
	/* The event surface is retained for the expansion module, but it is not an
	 * operator-ready feature yet.  Keep Show Control focused on working chat. */
	audienceTabs->setTabVisible(1, false);
	audienceTabs->tabBar()->hide();
	activityCard->layout()->addWidget(audienceTabs);
	activityCard->setMinimumWidth(270);
	activityCard->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
	showMainLayout->addLayout(showGrid, 1);
	auto *showBody = new QSplitter(Qt::Horizontal, pulseShowPage);
	showBody->setObjectName("PulseWeaverShowSplitter");
	showBody->setChildrenCollapsible(false);
	auto *operatingPanel = new QWidget(showBody);
	auto *operatingLayout = new QVBoxLayout(operatingPanel);
	operatingLayout->setContentsMargins(0, 0, 0, 0);
	operatingLayout->setSpacing(0);
	auto *workScroll = new QScrollArea(operatingPanel);
	workScroll->setObjectName("PulseWeaverWorkSurface");
	workScroll->setFrameShape(QFrame::NoFrame);
	workScroll->setWidgetResizable(true);
	workScroll->setMinimumWidth(680);
	// OBS previews own native windows and suppress native ancestor creation.
	// Give them a scrolling native parent and a viewport that clips it, so
	// their surfaces cannot stay behind or overlap controls during scrolling.
	workScroll->viewport()->setAttribute(Qt::WA_NativeWindow);
	showMain->setAttribute(Qt::WA_NativeWindow);
	workScroll->setWidget(showMain);
	auto *previewMixerSplit = new QSplitter(Qt::Vertical, operatingPanel);
	previewMixerSplit->setObjectName("PulseWeaverPreviewMixerSplit");
	previewMixerSplit->setChildrenCollapsible(false);
	previewMixerSplit->setHandleWidth(8);
	previewMixerSplit->addWidget(workScroll);
	operatingLayout->addWidget(previewMixerSplit, 1);
	showBody->addWidget(operatingPanel);
	showBody->addWidget(activityCard);
	showBody->setStretchFactor(0, 5);
	showBody->setStretchFactor(1, 2);
	showBody->setSizes({1100, 360});
	showLayout->addWidget(showBody, 1);

	auto *stageBar = new PulseResponsiveStageBar(showMain);
	auto *stageLabel = new QLabel("Stage");
	stageLabel->setObjectName("PulseWeaverKicker");
	stageBar->addWidget(stageLabel);
	pulseStageSelector = new QComboBox;
	pulseStageSelector->setObjectName("PulseWeaverStageSelector");
	pulseStageSelector->setAccessibleName("Stage selector");
	pulseStageSelector->setMinimumWidth(300);
	pulseStageSelector->setToolTip("One Stage changes every assigned canvas together; unassigned canvases remain live and unchanged");
	stageBar->addWidget(pulseStageSelector, 1);
	auto *transitionLabel = new QLabel("16:9 TRANSITION");
	transitionLabel->setObjectName("PulseWeaverKicker");
	stageBar->addWidget(transitionLabel);
	pulseStageTransitionSelector = new QComboBox;
	pulseStageTransitionSelector->setObjectName("PulseWeaverStageTransition");
	pulseStageTransitionSelector->setToolTip("Transition used when this Stage changes 16:9 programmes");
	stageBar->addWidget(pulseStageTransitionSelector);
	pulseStageTransitionDuration = new QSpinBox;
	pulseStageTransitionDuration->setObjectName("PulseWeaverStageDuration");
	pulseStageTransitionDuration->setRange(100, 5000);
	pulseStageTransitionDuration->setSingleStep(50);
	pulseStageTransitionDuration->setSuffix(" ms");
	pulseStageTransitionDuration->setValue(500);
	pulseStageTransitionDuration->setToolTip("16:9 transition duration for this Stage");
	stageBar->addWidget(pulseStageTransitionDuration);
	auto *verticalTransitionLabel = new QLabel("9:16 TRANSITION");
	verticalTransitionLabel->setObjectName("PulseWeaverKicker");
	stageBar->addWidget(verticalTransitionLabel);
	pulseVerticalStageTransitionSelector = new QComboBox;
	pulseVerticalStageTransitionSelector->setObjectName("PulseWeaverVerticalStageTransition");
	pulseVerticalStageTransitionSelector->setToolTip("Transition used when this Stage changes 9:16 programmes");
	stageBar->addWidget(pulseVerticalStageTransitionSelector);
	pulseVerticalStageTransitionDuration = new QSpinBox;
	pulseVerticalStageTransitionDuration->setObjectName("PulseWeaverVerticalStageDuration");
	pulseVerticalStageTransitionDuration->setRange(100, 5000);
	pulseVerticalStageTransitionDuration->setSingleStep(50);
	pulseVerticalStageTransitionDuration->setSuffix(" ms");
	pulseVerticalStageTransitionDuration->setValue(500);
	pulseVerticalStageTransitionDuration->setToolTip("9:16 transition duration for this Stage");
	stageBar->addWidget(pulseVerticalStageTransitionDuration);
	RefreshPulseWeaverTransitionSelector();
	auto saveStageTransition = [this](bool vertical) {
		if (pulseStageUpdating || !pulseStageSelector)
			return;
		QJsonArray stages = loadPulseWeaverStages();
		const int row = pulseStageSelector->currentIndex();
		if (row < 0 || row >= stages.size())
			return;
		QJsonObject stage = stages[row].toObject();
		QComboBox *selector = vertical ? pulseVerticalStageTransitionSelector.data() : pulseStageTransitionSelector.data();
		QSpinBox *duration = vertical ? pulseVerticalStageTransitionDuration.data() : pulseStageTransitionDuration.data();
		stage[vertical ? "verticalTransition" : "horizontalTransition"] = selector ? selector->currentData().toString() : QString("fade");
		stage[vertical ? "verticalDurationMs" : "horizontalDurationMs"] = duration ? duration->value() : 500;
		stages[row] = stage;
		savePulseWeaverStages(stages);
		pulseStageSelector->setItemData(row, QString::fromUtf8(QJsonDocument(stage).toJson(QJsonDocument::Compact)));
		if (duration && selector)
			duration->setEnabled(selector->currentData().toString() != "cut");
	};
	connect(pulseStageTransitionSelector, &QComboBox::currentIndexChanged, this, [saveStageTransition](int) {
		saveStageTransition(false);
	});
	connect(pulseStageTransitionDuration, &QSpinBox::valueChanged, this, [saveStageTransition](int) {
		saveStageTransition(false);
	});
	connect(pulseVerticalStageTransitionSelector, &QComboBox::currentIndexChanged, this, [saveStageTransition](int) {
		saveStageTransition(true);
	});
	connect(pulseVerticalStageTransitionDuration, &QSpinBox::valueChanged, this, [saveStageTransition](int) {
		saveStageTransition(true);
	});
	connect(this, &OBSBasic::TransitionAdded, this, [this](const QString &, const QString &) {
		RefreshPulseWeaverTransitionSelector();
	});
	connect(this, &OBSBasic::TransitionRenamed, this, [this](const QString &, const QString &) {
		RefreshPulseWeaverTransitionSelector();
	});
	connect(this, &OBSBasic::TransitionRemoved, this, [this](const QString &) {
		RefreshPulseWeaverTransitionSelector();
	});
	auto *captureStage = new QPushButton("Capture current");
	captureStage->setObjectName("PulseWeaverControl");
	pulseIcon(captureStage, "save");
	connect(captureStage, &QPushButton::clicked, this, &OBSBasic::CapturePulseWeaverStage);
	stageBar->addWidget(captureStage);
	auto *manageStages = new QPushButton("Manage stages");
	manageStages->setObjectName("PulseWeaverControl");
	pulseIcon(manageStages, "stages");
	connect(manageStages, &QPushButton::clicked, this, &OBSBasic::ManagePulseWeaverStages);
	stageBar->addWidget(manageStages);
	/* Stage transition configuration belongs in Manage Stages, where the
	 * horizontal and vertical assignments are edited together. Keep the
	 * existing controls alive for stage-state synchronisation, but do not
	 * duplicate that editing surface in operational Show Control. */
	transitionLabel->hide();
	pulseStageTransitionSelector->hide();
	pulseStageTransitionDuration->hide();
	verticalTransitionLabel->hide();
	pulseVerticalStageTransitionSelector->hide();
	pulseVerticalStageTransitionDuration->hide();
	captureStage->hide();
	connect(pulseStageSelector, &QComboBox::currentIndexChanged, this, &OBSBasic::ActivatePulseWeaverStage);
	showMainLayout->addWidget(stageBar);

	auto *destinations = new QHBoxLayout;
	auto *destinationLabel = new QLabel("Outputs");
	destinationLabel->setObjectName("PulseWeaverKicker");
	destinations->addWidget(destinationLabel);
	/* Accounts and output routing are one product concept. Keep the provider
	 * implementations behind one control instead of leaking three unrelated
	 * setup fragments into Show Control. */
	auto *platformsButton = new QPushButton("Connections");
	platformsButton->setObjectName("PulseWeaverControl");
	pulseIcon(platformsButton, "connect");
	platformsButton->setAccessibleName("Manage Connections");
	platformsButton->setToolTip("Open the single Connections page for Twitch, YouTube and Kick");
	destinations->addWidget(platformsButton);
	QSettings destinationSettings(pulseWeaverUiSettingsPath(), QSettings::IniFormat);
	auto makeDestination = [this, &destinations, &destinationSettings](const QString &label, const QString &key,
		bool defaultValue, bool dualCapable) {
		auto *route = new QComboBox;
		route->setObjectName("PulseWeaverDestination" + label);
		route->setAccessibleName(label + " output mode");
		route->setSizeAdjustPolicy(QComboBox::AdjustToContents);
		route->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
		route->addItem(label.toUpper() + "  ·  OFF", "off");
		route->addItem(label.toUpper() + "  ·  16:9", "horizontal");
		if (key != "kick")
			route->addItem(label.toUpper() + "  ·  9:16", "vertical");
		if (dualCapable)
			route->addItem(label.toUpper() + "  ·  DUAL", "dual");
		QString savedMode = destinationSettings.value("destinations/" + key + "_mode").toString();
		if (savedMode.isEmpty())
			savedMode = destinationSettings.value("destinations/" + key, defaultValue).toBool() ? "horizontal" : "off";
		const int savedIndex = route->findData(savedMode);
		route->setCurrentIndex(savedIndex >= 0 ? savedIndex : 0);
		route->setProperty("pulseWeaverPreviousMode", route->currentData());
		route->setToolTip(key == "kick" ? "Kick currently supports Pulse Weaver's 16:9 output only" :
			"Choose whether " + label + " receives the 16:9 canvas, 9:16 canvas, both, or stays off");
		connect(route, &QComboBox::currentIndexChanged, this, [this, route, key, label](int) {
			const QString previousMode = route->property("pulseWeaverPreviousMode").toString();
			const QString mode = route->currentData().toString();
			route->setProperty("pulseWeaverPreviousMode", mode);
			QSettings settings(pulseWeaverUiSettingsPath(), QSettings::IniFormat);
			settings.setValue("destinations/" + key + "_mode", mode);
			settings.setValue("destinations/" + key, mode != "off");
			if (label == "YouTube" && pulseYouTubeCanvas) {
				const int item = pulseYouTubeCanvas->findData(mode);
				if (item >= 0)
					pulseYouTubeCanvas->setCurrentIndex(item);
			}
			if (label == "Kick") {
				if (auto *nativeRoute = findChild<QComboBox *>("PulseWeaverKickStageRoute")) {
					const int item = nativeRoute->findData(mode);
					if (item >= 0)
						nativeRoute->setCurrentIndex(item);
				}
			}
			if (label == "Twitch" && pulseDualFormatStatus)
				pulseDualFormatStatus->setText(mode == "dual" ? "TWITCH 16:9 + 9:16" : mode == "vertical" ? "TWITCH 9:16" : mode == "horizontal" ? "TWITCH 16:9" : "TWITCH OFF");
			/* Output canvases are provisioned lazily. Re-apply the active Stage
			 * when a route is enabled so an Off destination consumes no video
			 * resources while newly enabled routes are immediately ready. */
			QTimer::singleShot(0, this, [this, key, previousMode, mode] {
				if (pulseStageSelector && pulseStageSelector->currentIndex() >= 0)
					ApplyPulseWeaverStage(pulseStageSelector->currentIndex(), false);
				if (property("pulseWeaverGoLiveSession").toBool())
					ApplyPulseWeaverLiveDestinationChange(key, previousMode, mode);
			});
		});
		destinations->addWidget(route);
		return route;
	};
	pulseTwitchDestination = makeDestination("Twitch", "twitch", true, true);
	pulseYouTubeDestination = makeDestination("YouTube", "youtube", false, true);
	pulseKickDestination = makeDestination("Kick", "kick", false, false);
	connect(this, &OBSBasic::StreamingStarting, this, [this](bool) {
		const QString mode = pulseTwitchDestination ? pulseTwitchDestination->currentData().toString() : QString("off");
		if (mode == "off")
			return;
		const QString canvasName = property(mode == "vertical" ? "pulseWeaverTwitchVerticalCanvasName" :
			"pulseWeaverTwitchHorizontalCanvasName").toString();
		obs_canvas_t *canvas = canvasName.isEmpty() ? (mode == "vertical" ? PulseWeaverGetVerticalCanvas() : obs_get_main_canvas()) :
			obs_get_canvas_by_name(canvasName.toUtf8().constData());
		obs_output_t *output = canvas ? obs_frontend_get_streaming_output() : nullptr;
		obs_encoder_t *encoder = output ? obs_output_get_video_encoder(output) : nullptr;
		if (encoder)
			obs_encoder_set_video(encoder, obs_canvas_get_video(canvas));
		obs_output_release(output);
		obs_canvas_release(canvas);
	});
	connect(platformsButton, &QPushButton::clicked, this, [this] {
		SetPulseWeaverWorkspace(3);
		if (auto *platformTabs = findChild<QTabWidget *>("PulseWeaverPlatformTabs"))
			platformTabs->setCurrentIndex(0);
	});
	pulseYouTubeButton = new QPushButton("CONNECT YOUTUBE IN BROWSER", pulsePages);
	pulseYouTubeButton->setObjectName("PulseWeaverYouTubeConnectButton");
	pulseYouTubeButton->setProperty("pulseWeaverRole", "youtube-connect");
	pulseYouTubeButton->setToolTip("Sign in through your normal browser; Pulse Weaver never asks for your Google password or a stream key");
	connect(pulseYouTubeButton, &QPushButton::clicked, this, &OBSBasic::ConnectPulseWeaverYouTube);
	pulseYouTubeButton->hide();
	pulseYouTubeCanvas = new QComboBox(pulsePages);
	pulseYouTubeCanvas->setObjectName("PulseWeaverYouTubeCanvasRoute");
	pulseYouTubeCanvas->addItem("YouTube · Off", "off");
	pulseYouTubeCanvas->addItem("YouTube · 16:9", "horizontal");
	pulseYouTubeCanvas->addItem("YouTube · 9:16", "vertical");
	pulseYouTubeCanvas->addItem("YouTube · Dual 16:9 + 9:16", "dual");
	pulseYouTubeCanvas->setToolTip("Choose which native canvas is sent to YouTube");
	pulseYouTubeCanvas->hide();
	if (pulseYouTubeDestination) {
		const int route = pulseYouTubeCanvas->findData(pulseYouTubeDestination->currentData());
		pulseYouTubeCanvas->setCurrentIndex(route >= 0 ? route : 0);
		connect(pulseYouTubeCanvas, &QComboBox::currentIndexChanged, this, [this](int) {
			if (!pulseYouTubeDestination)
				return;
			const int route = pulseYouTubeDestination->findData(pulseYouTubeCanvas->currentData());
			if (route >= 0 && route != pulseYouTubeDestination->currentIndex())
				pulseYouTubeDestination->setCurrentIndex(route);
		});
	}
	pulseKickOutputControl = new QPushButton(pulsePages);
	pulseKickOutputControl->setObjectName("PulseWeaverKickOutputControl");
	pulseKickOutputControl->hide();
	pulseDestinationStatus = new QLabel("Accounts and output routing");
	pulseDestinationStatus->setObjectName("PulseWeaverDestinationStatus");
	pulseDestinationStatus->setWordWrap(true);
	pulseDestinationStatus->setMinimumWidth(0);
	pulseDestinationStatus->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
	destinations->addWidget(pulseDestinationStatus, 1);
	showMainLayout->addLayout(destinations);
	/* Show Control uses the same proven OBS vertical controls as Camera, in a
	 * compact horizontally scrolling band. */
	auto *mixerCard = new QFrame;
	mixerCard->setObjectName("PulseWeaverCard");
	// Mixer strips share one continuous surface.
	mixerCard->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
	/* 58 px strips plus the native horizontal scrollbar need more than the old
	 * 65 px viewport. That mismatch was the persistent 5–10 px vertical scroll
	 * and clipped control row reported in Show Control. */
	mixerCard->setFixedHeight(260);
	auto *mixerLayout = new QVBoxLayout(mixerCard);
	mixerLayout->setContentsMargins(12, 7, 12, 7);
	mixerLayout->setSpacing(4);
	auto *mixerHeader = new QHBoxLayout;
	mixerHeader->setSpacing(8);
	auto *mixerTitle = new QLabel("Audio mixer");
	mixerTitle->setObjectName("PulseWeaverCardTitle");
	mixerHeader->addWidget(pulseEmblem("mixer", 30));
	mixerHeader->addWidget(mixerTitle);
	auto *mixerHint = new QLabel("Levels & monitoring");
	mixerHint->setObjectName("PulseWeaverMuted");
	mixerHeader->addWidget(mixerHint);
	mixerHeader->addStretch();
	auto *collapseMixer = new QPushButton("Collapse", mixerCard);
	collapseMixer->setObjectName("PulseWeaverControl");
	collapseMixer->setMinimumHeight(42);
    pulseIcon(collapseMixer, "collapse", 22);
	collapseMixer->setToolTip("Collapse Quick Mix without changing any audio routing");
	auto *openFullMixer = new QPushButton("Full mixer");
	openFullMixer->setObjectName("PulseWeaverControl");
    pulseIcon(openFullMixer, "mixer", 24);
	openFullMixer->setMinimumSize(164, 42);
	openFullMixer->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
	openFullMixer->setToolTip("Open Camera to use the complete native OBS audio mixer");
	connect(openFullMixer, &QPushButton::clicked, this, [this] {
		SetPulseWeaverWorkspace(2);
		SetPulseWeaverCameraOutput(false);
		if (ui->mixerDock) {
			ui->mixerDock->show();
			ui->mixerDock->raise();
		}
	});
	mixerHeader->addWidget(collapseMixer);
	mixerHeader->addWidget(openFullMixer);
	mixerLayout->addLayout(mixerHeader);
	auto *mixerScroll = new QScrollArea(mixerCard);
	mixerScroll->setObjectName("PulseWeaverCompactMixer");
	mixerScroll->setWidgetResizable(true);
	mixerScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
	mixerScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
	mixerScroll->setMinimumHeight(204);
	mixerScroll->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
	pulseCompactMixerBody = new QWidget(mixerScroll);
	pulseCompactMixerBody->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Fixed);
	pulseCompactMixerBody->setLayout(new QHBoxLayout);
	pulseCompactMixerBody->layout()->setContentsMargins(0, 0, 0, 0);
	pulseCompactMixerBody->layout()->setSpacing(7);
	mixerScroll->setWidget(pulseCompactMixerBody);
	mixerLayout->addWidget(mixerScroll);
	auto setMixerCollapsed = [mixerCard, mixerLayout, mixerScroll, collapseMixer, previewMixerSplit](bool collapsed) {
		if (collapsed && !mixerScroll->isHidden())
			mixerCard->setProperty("expandedHeight", mixerCard->height());
		mixerScroll->setVisible(!collapsed);
		collapseMixer->setText(collapsed ? "Expand" : "Collapse");
		collapseMixer->setToolTip(collapsed ? "Expand audio mixer" : "Collapse audio mixer");
		mixerScroll->ensurePolished();
		mixerScroll->setMinimumHeight(180 + mixerScroll->style()->pixelMetric(QStyle::PM_ScrollBarExtent)
			+ 2 * mixerScroll->frameWidth() + 4);
		// Include the themed frame and header controls instead of clipping them
		// inside the old 58px collapsed height.
		mixerLayout->invalidate();
		const int requiredHeight = mixerLayout->totalSizeHint().height();
		const QMargins margins = mixerLayout->contentsMargins();
		const int expandedMinimum = mixerScroll->minimumHeight() + qMax(42, collapseMixer->sizeHint().height())
			+ margins.top() + margins.bottom() + mixerLayout->spacing() + 2 * mixerCard->frameWidth();
		mixerCard->setMinimumHeight(collapsed ? requiredHeight : expandedMinimum);
		mixerCard->setMaximumHeight(collapsed ? requiredHeight : QWIDGETSIZE_MAX);
		if (previewMixerSplit->count() == 2) {
			const int mixerHeight = collapsed ? requiredHeight : qMax(expandedMinimum, mixerCard->property("expandedHeight").toInt());
			previewMixerSplit->refresh();
			previewMixerSplit->setSizes({qMax(1, previewMixerSplit->height() - mixerHeight - 8), mixerHeight});
		}
		QSettings settings(pulseWeaverUiSettingsPath(), QSettings::IniFormat);
		settings.setValue("showControl/quickMixCollapsed", collapsed);
	};
	connect(collapseMixer, &QPushButton::clicked, mixerCard, [mixerScroll, setMixerCollapsed] {
		setMixerCollapsed(!mixerScroll->isHidden());
	});
	setMixerCollapsed(destinationSettings.value("showControl/quickMixCollapsed", false).toBool());
	connect(App(), &OBSApp::StyleChanged, mixerCard, [mixerScroll, setMixerCollapsed] {
		setMixerCollapsed(mixerScroll->isHidden());
	});
	previewMixerSplit->addWidget(mixerCard);
	previewMixerSplit->setStretchFactor(0, 1);
	previewMixerSplit->setStretchFactor(1, 0);
	previewMixerSplit->setSizes({600, 220});
	const QByteArray savedSplit = destinationSettings.value("showControl/previewMixerSplit").toByteArray();
	if (!savedSplit.isEmpty()) previewMixerSplit->restoreState(savedSplit);
	connect(previewMixerSplit, &QSplitter::splitterMoved, previewMixerSplit, [previewMixerSplit] {
		QSettings settings(pulseWeaverUiSettingsPath(), QSettings::IniFormat);
		settings.setValue("showControl/previewMixerSplit", previewMixerSplit->saveState());
	});

	auto *controls = new PulseTransportBar;
	pulseReplayButton = new QPushButton("Replay buffer");
	pulseVirtualCameraButton = new QPushButton("Virtual camera");
    pulseIcon(pulseReplayButton, "replay");
    pulseIcon(pulseVirtualCameraButton, "camera");
	for (QPushButton *button : {pulseReplayButton.data(), pulseVirtualCameraButton.data()}) {
		button->setObjectName("PulseWeaverControl");
		controls->addWidget(button);
	}
	connect(pulseReplayButton, &QPushButton::clicked, this, &OBSBasic::ReplayBufferActionTriggered);
	connect(pulseVirtualCameraButton, &QPushButton::clicked, this, &OBSBasic::VirtualCamActionTriggered);
	pulseDualFormatStatus = new QLabel;
	pulseDualFormatStatus->setObjectName("PulseWeaverMuted");
	if (pulseTwitchDestination) {
		const QString mode = pulseTwitchDestination->currentData().toString();
		pulseDualFormatStatus->setText(mode == "dual" ? "TWITCH 16:9 + 9:16" : mode == "vertical" ?
			"TWITCH 9:16" : mode == "horizontal" ? "TWITCH 16:9" : "TWITCH OFF");
	}
	controls->addWidget(pulseDualFormatStatus);
	pulseDualFormatStatus->hide();
	controls->addStretch();
	pulseRecordingDestination = new QComboBox;
	pulseRecordingDestination->setObjectName("PulseWeaverRecordingDestination");
	pulseRecordingDestination->setAccessibleName("Recording output mode, unstable preview");
	pulseRecordingDestination->addItem("Recording · Standard", "standard");
	pulseRecordingDestination->addItem("Recording · Off", "off");
	pulseRecordingDestination->addItem("16:9 · Experimental", "horizontal");
	pulseRecordingDestination->addItem("9:16 · Experimental", "vertical");
	pulseRecordingDestination->addItem("Dual · Experimental", "dual");
	pulseRecordingDestination->setToolTip(
		"UNSTABLE preview: record the Stage-routed 16:9 canvas, 9:16 canvas, or both as separate MKV files. "
		"Standard keeps the proven OBS recording path and settings.");
	/* The environment override is intentionally test-only: it lets the smoke
	 * harness exercise a routed recorder without scripting a native combo box.
	 * Normal launches always use the operator's saved selection. */
	const QString testRecordingMode = qEnvironmentVariable("PULSEWEAVER_RECORDING_TEST_MODE");
	const QString savedRecordingMode = testRecordingMode.isEmpty() ?
		destinationSettings.value("recording/mode", "standard").toString() : testRecordingMode;
	const int savedRecordingIndex = pulseRecordingDestination->findData(savedRecordingMode);
	pulseRecordingDestination->setCurrentIndex(savedRecordingIndex >= 0 ? savedRecordingIndex : 0);
	connect(pulseRecordingDestination, &QComboBox::currentIndexChanged, this, [this](int) {
		QSettings settings(pulseWeaverUiSettingsPath(), QSettings::IniFormat);
		settings.setValue("recording/mode", pulseRecordingDestination->currentData().toString());
		QTimer::singleShot(0, this, [this] {
			if (pulseStageSelector && pulseStageSelector->currentIndex() >= 0)
				ApplyPulseWeaverStage(pulseStageSelector->currentIndex(), false);
		});
	});
	controls->addWidget(pulseRecordingDestination);
	pulseRecordingDestination->setMinimumWidth(150);
	pulseRecordingDestination->setMaximumWidth(240);
	pulseRecordButton = new QPushButton("Record");
	pulseRecordButton->setObjectName("PulseWeaverRecord");
	pulseIcon(pulseRecordButton, "record");
	connect(pulseRecordButton, &QPushButton::clicked, this, [this] {
		if (PulseWeaverRecordingActive()) {
			StopPulseWeaverRecordings();
			return;
		}
		if (obs_frontend_recording_active()) {
			RecordActionTriggered();
			return;
		}
		const QString mode = pulseRecordingDestination ? pulseRecordingDestination->currentData().toString() :
			QString("standard");
		if (mode == "standard") {
			RecordActionTriggered();
		} else if (mode == "off") {
			QMessageBox::information(this, "Recording is Off",
				"Choose Standard, 16:9, 9:16 or Dual before starting a recording.");
		} else {
			StartPulseWeaverRecordings();
		}
	});
	controls->addWidget(pulseRecordButton);
	pulseStreamButton = new QPushButton("Go live");
	pulseStreamButton->setObjectName("PulseWeaverLive");
	pulseIcon(pulseStreamButton, "live");
	connect(pulseStreamButton, &QPushButton::clicked, this, [this] {
		const bool lumiaConfirmed = property("pulseWeaverLumiaGoLiveConfirmed").toBool();
		setProperty("pulseWeaverLumiaGoLiveConfirmed", false);
		setProperty("pulseWeaverControlAccepted", false);
		setProperty("pulseWeaverControlResult", QString());
		const QString twitchMode = pulseTwitchDestination ? pulseTwitchDestination->currentData().toString() : QString("off");
		const QString youtubeMode = pulseYouTubeDestination ? pulseYouTubeDestination->currentData().toString() : QString("off");
		const QString kickMode = pulseKickDestination ? pulseKickDestination->currentData().toString() : QString("off");
		const bool twitch = twitchMode != "off";
		const bool youtube = youtubeMode != "off";
		const bool kick = kickMode != "off";
		const bool secondaryLive = (pulseYouTubeOutput && obs_output_active(pulseYouTubeOutput)) ||
			(pulseYouTubeSecondOutput && obs_output_active(pulseYouTubeSecondOutput));
		if (property("pulseWeaverGoLiveSession").toBool() || obs_frontend_streaming_active() || secondaryLive ||
		    property("pulseWeaverKickLive").toBool()) {
			setProperty("pulseWeaverGoLiveSession", false);
			setProperty("pulseWeaverControlAccepted", true);
			setProperty("pulseWeaverControlResult", "Pulse Weaver is stopping all live outputs.");
			StopPulseWeaverSecondaryOutputs();
			if (pulseKickOutputControl) { pulseKickOutputControl->setProperty("command", "stop"); pulseKickOutputControl->click(); }
			if (obs_frontend_streaming_active())
				StreamActionTriggered();
		} else {
			if (!twitch && !youtube && !kick) {
				const QString message = "Select Twitch, YouTube or Kick before going live.";
				setProperty("pulseWeaverControlResult", message);
				if (!lumiaConfirmed)
					QMessageBox::information(this, "Choose a destination", message);
				return;
			}
#ifdef YOUTUBE_ENABLED
			if (youtube && !pulseYouTubeAuth) {
				if (pulseDestinationStatus)
					pulseDestinationStatus->setText("YouTube is selected but its account is not connected.");
				const QString message = "YouTube is selected, but its account is not connected.";
				setProperty("pulseWeaverControlResult", message);
				if (!lumiaConfirmed)
					QMessageBox::warning(this, "Connect YouTube", message +
						" Open Action → Connections → YouTube and connect the account first.");
				return;
			}
#endif
			if (kick && !property("pulseWeaverKickReady").toBool()) {
				if (pulseDestinationStatus)
					pulseDestinationStatus->setText("Kick is selected but its account destination is not ready.");
				const QString message = "Kick is selected, but its account destination is not ready.";
				setProperty("pulseWeaverControlResult", message);
				if (!lumiaConfirmed)
					QMessageBox::warning(this, "Connect Kick", message +
						" Reconnect it in Action → Connections → Kick.");
				return;
			}
			if (twitch) {
				obs_service_t *service = GetService();
				obs_data_t *settings = service ? obs_service_get_settings(service) : nullptr;
				const QString serviceName = settings ? QString::fromUtf8(obs_data_get_string(settings, "service")) : QString();
				const char *streamKey = settings ? obs_data_get_string(settings, "key") : nullptr;
				const bool hasDestination = streamKey && *streamKey;
				obs_data_release(settings);
				if (serviceName.compare("Twitch", Qt::CaseInsensitive) != 0 || !hasDestination) {
					if (pulseDestinationStatus)
						pulseDestinationStatus->setText("Twitch is selected but its private broadcast destination is not ready.");
					const QString message = "Twitch is selected, but its private broadcast destination is not ready.";
					setProperty("pulseWeaverControlResult", message);
					if (!lumiaConfirmed)
						QMessageBox::warning(this, "Connect Twitch", message +
							" Reconnect Twitch in Action → Connections and wait for the ready message.");
					return;
				}
			}

			auto routeLabel = [](const QString &mode) {
				return mode == "dual" ? QString("16:9 + 9:16") : mode == "vertical" ? QString("9:16") :
					mode == "horizontal" ? QString("16:9") : QString("OFF");
			};
			QStringList outputPlan;
			outputPlan << (twitch ? "✓ Twitch · " + routeLabel(twitchMode) + " · READY" : "— Twitch · OFF");
			outputPlan << (youtube ? "✓ YouTube · " + routeLabel(youtubeMode) + " · SIGNED IN" : "— YouTube · OFF");
			outputPlan << (kick ? "✓ Kick · " + routeLabel(kickMode) + " · READY" : "— Kick · OFF");
			const int destinationCount = int(twitch) + int(youtube) + int(kick);
			const QString stage = pulseStageSelector ? pulseStageSelector->currentText() : QString("Current Show");
			if (!lumiaConfirmed) {
				QMessageBox confirmation(this);
				confirmation.setObjectName("PulseWeaverGoLivePreflight");
				confirmation.setWindowTitle("Confirm your output plan");
				confirmation.setIcon(QMessageBox::Question);
				confirmation.setText(QString("Start your show on %1 destination%2?")
					.arg(destinationCount).arg(destinationCount == 1 ? "" : "s"));
				confirmation.setInformativeText("STAGE\n" + stage + "\n\nOUTPUT PLAN\n" + outputPlan.join("\n") +
					(youtube ? "\n\nYouTube will prepare its account-owned broadcast after confirmation." : QString()));
				QAbstractButton *startShow = confirmation.addButton("GO LIVE NOW", QMessageBox::AcceptRole);
				QPushButton *cancelShow = confirmation.addButton("CANCEL", QMessageBox::RejectRole);
				confirmation.setDefaultButton(cancelShow);
				confirmation.setEscapeButton(cancelShow);
				confirmation.exec();
				if (confirmation.clickedButton() != startShow) {
					if (pulseDestinationStatus)
						pulseDestinationStatus->setText("Go Live cancelled · no outputs were started.");
					return;
				}
			}
#ifdef YOUTUBE_ENABLED
			if (youtube && pulseYouTubeAuth &&
			    (pulseYouTubeStreamKey.isEmpty() || pulseYouTubePreparedMode != youtubeMode))
				PreparePulseWeaverYouTube();
			if (youtube && (pulseYouTubeStreamKey.isEmpty() || pulseYouTubePreparedMode != youtubeMode)) {
				const QString message = pulseDestinationStatus ? pulseDestinationStatus->text() :
					"Pulse Weaver could not create the selected YouTube broadcast.";
				setProperty("pulseWeaverControlResult", message);
				if (!lumiaConfirmed)
					QMessageBox::warning(this, "YouTube could not prepare", message);
				return;
			}
#endif
			setProperty("pulseWeaverGoLiveSession", true);
			for (const char *provider : {"twitch", "kick", "youtube"})
				setProperty((QByteArray("pulseWeaverLumiaStop_") + provider).constData(), false);
			if (twitch) {
				const bool dual = twitchMode == "dual";
				/* Pulse Weaver's GO LIVE flow is already explicitly confirmed. Keep
				 * Twitch Dual enabled while accepting its advisory GPU warning, so
				 * an upstream recommendation does not interrupt the operator twice.
				 * Actual preparation and encoder failures still report normally. */
				setProperty("pulseWeaverAutoAcceptEnhancedBroadcastingWarning", dual);
				config_set_bool(Config(), "Stream1", "EnableMultitrackVideo", dual);
				if (dual) {
					EnsurePulseWeaverVerticalCanvas();
					const QString canvasName = property("pulseWeaverTwitchVerticalCanvasName").toString();
					obs_canvas_t *extra = canvasName.isEmpty() ? PulseWeaverGetVerticalCanvas() :
						obs_get_canvas_by_name(canvasName.toUtf8().constData());
					if (extra) {
						config_set_string(Config(), "Stream1", "MultitrackExtraCanvas", obs_canvas_get_uuid(extra));
						obs_canvas_release(extra);
					}
				}
				activeConfiguration.SaveSafe("tmp");
				ResetOutputs();
				StreamActionTriggered();
			}
			setProperty("pulseWeaverControlAccepted", true);
			setProperty("pulseWeaverControlResult", QString("Go Live confirmed by %1 for Stage ‘%2’. Starting %3 destination%4.")
				.arg(lumiaConfirmed ? "Lumia" : "Pulse Weaver", stage).arg(destinationCount)
				.arg(destinationCount == 1 ? "" : "s"));
			QTimer::singleShot(twitch ? 1200 : 50, this, [this, youtube, kick] {
				if (!property("pulseWeaverGoLiveSession").toBool()) return;
				if (youtube && !property("pulseWeaverLumiaStop_youtube").toBool())
					StartPulseWeaverSecondaryOutputs();
				if (kick && !property("pulseWeaverLumiaStop_kick").toBool() && pulseKickOutputControl) { pulseKickOutputControl->setProperty("command", "start"); pulseKickOutputControl->click(); }
			});
		}
	});
	controls->addWidget(pulseStreamButton);
	operatingLayout->addWidget(controls);
	pulsePages->addWidget(pulseShowPage);

	/* LIGHTS is retained as an expansion surface without presenting unfinished
	 * device controls as if they were ready for live use. */
	pulseLightsHost = new QWidget(pulsePages);
	pulseLightsHost->setObjectName("PulseWeaverLightsHost");
	auto *lightsLayout = new QVBoxLayout(pulseLightsHost);
	lightsLayout->setContentsMargins(12, 12, 12, 8);
	lightsLayout->setSpacing(10);
    lightsLayout->addWidget(pulseBanner("Lights", "Lighting and atmosphere", "lights"));
    lightsLayout->addWidget(card("Lighting control", "Lighting controls are not available in this release.", "#D946EF"));
	lightsLayout->addStretch(1);
	auto *lightsMount = new QWidget;
	lightsMount->setObjectName("PulseWeaverLightsPluginMount");
	lightsMount->setProperty("pulseWeaverComingSoon", true);
	lightsMount->setLayout(new QVBoxLayout);
	lightsMount->hide();
	lightsLayout->addWidget(lightsMount);
	pulsePages->addWidget(pulseLightsHost);

	/* CAMERA — the untouched native OBS horizontal editor plus an equally
	 * direct editor for the imported/native vertical libobs canvas. */
	pulseCameraPage = new QWidget(pulsePages);
	pulseCameraPage->setObjectName("PulseWeaverPage");
	auto *cameraLayout = new QVBoxLayout(pulseCameraPage);
	cameraLayout->setContentsMargins(12, 12, 12, 8);
	cameraLayout->setSpacing(10);
	auto *cameraToolbar = new QWidget(pulseCameraPage);
	cameraToolbar->setObjectName("PulseWeaverCameraToolbar");
	cameraToolbar->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
	auto *outputSelector = new QHBoxLayout(cameraToolbar);
	outputSelector->setContentsMargins(0, 0, 0, 0);
	outputSelector->setSpacing(6);
	auto *horizontalOutput = new QPushButton("Landscape · 16:9");
	auto *verticalOutput = new QPushButton("Portrait · 9:16");
    pulseIcon(horizontalOutput, "horizontal");
	auto *outputGroup = new QButtonGroup(pulseCameraPage);
    pulseIcon(verticalOutput, "vertical");
	outputGroup->setExclusive(true);
	for (QPushButton *button : {horizontalOutput, verticalOutput}) {
		button->setObjectName("PulseWeaverControl");
		button->setCheckable(true);
		outputGroup->addButton(button);
		outputSelector->addWidget(button);
	}
	horizontalOutput->setChecked(true);
	auto *overlays = new QPushButton("Overlay designer");
	overlays->setObjectName("PulseWeaverOverlayButton");
    pulseIcon(overlays, "overlay");
	overlays->setProperty("class", "primary");
	overlays->setToolTip("Build native Pulse Weaver overlays and add them to the active scene");
	outputSelector->addWidget(overlays);
	outputSelector->addStretch();
	auto *unlockDocks = new QPushButton("Unlock layout");
	unlockDocks->setObjectName("PulseWeaverDockLockToggle");
    pulseIcon(unlockDocks, "lock");
	unlockDocks->setToolTip("Toggle whether the native OBS docks can be moved, floated and redocked");
	auto updateDockButton = [unlockDocks](bool locked) {
		unlockDocks->setText(locked ? "Unlock layout" : "Lock layout");
		unlockDocks->setProperty("docksLocked", locked);
		unlockDocks->style()->unpolish(unlockDocks);
		unlockDocks->style()->polish(unlockDocks);
	};
	updateDockButton(ui->lockDocks ? ui->lockDocks->isChecked() : true);
	if (ui->lockDocks)
		connect(ui->lockDocks, &QAction::toggled, unlockDocks, updateDockButton);
	connect(unlockDocks, &QPushButton::clicked, this, [this, updateDockButton] {
		if (!ui->lockDocks)
			return;
		const bool lock = !ui->lockDocks->isChecked();
		ui->lockDocks->setChecked(lock);
		on_lockDocks_toggled(lock);
		updateDockButton(lock);
	});
	outputSelector->addWidget(unlockDocks);
	auto *resetDocks = new QPushButton("Reset layout");
	resetDocks->setObjectName("PulseWeaverControl");
    pulseIcon(resetDocks, "reset");
	resetDocks->setToolTip("Restore the standard editable Camera workspace dock layout");
	connect(resetDocks, &QPushButton::clicked, this, [this] {
		const bool wasLocked = ui->lockDocks && ui->lockDocks->isChecked();
		on_resetDocks_triggered(true);
		if (ui->lockDocks)
			ui->lockDocks->setChecked(wasLocked);
		on_lockDocks_toggled(wasLocked);
		QTimer::singleShot(0, this, [this] { SetPulseWeaverWorkspace(2); });
	});
	outputSelector->addWidget(resetDocks);
	auto *importObs = new QPushButton("Import from OBS");
	importObs->setObjectName("PulseWeaverControl");
    pulseIcon(importObs, "import");
	importObs->setToolTip("Copy profiles, scenes, sources, filters and settings from normal OBS into Pulse Weaver");
	connect(importObs, &QPushButton::clicked, this, &OBSBasic::ImportPulseWeaverObsSetup);
	outputSelector->addWidget(importObs);
	auto *outputHint = new QLabel;
	outputHint->setObjectName("PulseWeaverMuted");
    outputHint->setToolTip("The black boundary marks the program area.");
	outputHint->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
	outputSelector->addWidget(outputHint);
	auto updateCanvasHint = [this, outputHint](bool vertical) {
		uint32_t width = vertical ? 1080 : 1920;
		uint32_t height = vertical ? 1920 : 1080;
		if (vertical) {
			obs_canvas_t *canvas = PulseWeaverGetVerticalCanvas();
			obs_video_info info = {};
			if (canvas && obs_canvas_get_video_info(canvas, &info)) { width = info.base_width; height = info.base_height; }
			obs_canvas_release(canvas);
		} else {
			obs_video_info info = {};
			if (obs_get_video_info(&info)) { width = info.base_width; height = info.base_height; }
		}
		outputHint->setText(QString("%1 · %2 × %3")
			.arg(vertical ? "Portrait" : "Landscape").arg(width).arg(height));
	};
	updateCanvasHint(false);
	cameraToolbar->setFixedHeight(54);
	cameraLayout->addWidget(cameraToolbar);
	pulseCameraEditors = new QStackedWidget;
	auto *horizontalEditor = new QWidget;
	auto *horizontalLayout = new QVBoxLayout(horizontalEditor);
	horizontalLayout->setContentsMargins(0, 0, 0, 0);
	horizontalLayout->setSpacing(0);
	horizontalLayout->addWidget(ui->canvasEditor, 1);
	horizontalLayout->addWidget(ui->contextContainer);
	pulseCameraEditors->addWidget(horizontalEditor);
	pulseVerticalEditor = new PulseVerticalEditor(this);
	pulseCameraEditors->addWidget(pulseVerticalEditor);
	/* A real QMainWindow is required here. QDockWidget snap targets only belong
	 * to their owning QMainWindow; keeping the canvas in Camera while leaving
	 * the docks on OBSBasic made the drop outlines appear outside this page (or
	 * not at all). This host gives the unmodified OBS docks their normal Qt/OBS
	 * docking behaviour while containing them inside Pulse Weaver's Camera UI. */
	pulseCameraDockHost = new QMainWindow(pulseCameraPage);
	/* QMainWindow defaults to a window-type flag even when constructed with a
	 * parent. Clear it so this is a true embedded workspace managed by the
	 * Camera page layout, not an invisible child top-level window. */
	pulseCameraDockHost->setWindowFlags(Qt::Widget);
	pulseCameraDockHost->setObjectName("PulseWeaverCameraDockHost");
	pulseCameraDockHost->setAccessibleName("Camera dock workspace");
	pulseCameraDockHost->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
	pulseCameraDockHost->setMinimumSize(0, 0);
	pulseCameraDockHost->setDockOptions(QMainWindow::AnimatedDocks | QMainWindow::AllowNestedDocks |
					QMainWindow::AllowTabbedDocks | QMainWindow::GroupedDragging);
	pulseCameraDockHost->setDockNestingEnabled(true);
	/* The editor's surrounding workspace is intentionally charcoal while the
	 * actual programme canvas is pure black.  This makes the canvas size and
	 * crop boundary readable even for scenes whose sources are mostly black. */
	ui->previewContainer->setStyleSheet(QString());
	ui->preview->setStyleSheet(QString());
	ui->preview->SetDisplayBackgroundColor(QColor("#0A0B0F"));
	pulseCameraDockHost->setCentralWidget(pulseCameraEditors);
	cameraLayout->addWidget(pulseCameraDockHost, 1);
	connect(horizontalOutput, &QPushButton::toggled, this, [this, updateCanvasHint](bool checked) {
		if (checked) {
			SetPulseWeaverCameraOutput(false);
			updateCanvasHint(false);
		}
	});
	connect(verticalOutput, &QPushButton::toggled, this, [this, updateCanvasHint](bool checked) {
		if (checked) {
			SetPulseWeaverCameraOutput(true);
			updateCanvasHint(true);
		}
	});
	pulsePages->addWidget(pulseCameraPage);

	/* ACTION currently exposes only working account, route and stream metadata
	 * controls. Event feeds and automation builders remain compiled but hidden. */
	pulseActionHost = new QWidget(pulsePages);
	pulseActionHost->setObjectName("PulseWeaverActionHost");
	auto *actionLayout = new QVBoxLayout(pulseActionHost);
	actionLayout->setContentsMargins(12, 12, 12, 8);
	actionLayout->setSpacing(10);
    actionLayout->addWidget(pulseBanner("Action", "Connections, output routing and stream details", "action"));
	auto *actionMount = new QWidget;
	actionMount->setObjectName("PulseWeaverActionPluginMount");
	actionMount->setLayout(new QVBoxLayout);
	actionLayout->addWidget(actionMount, 1);
	pulsePages->addWidget(pulseActionHost);

	connect(pulseHorizontalDisplay, &OBSQTDisplay::DisplayCreated, this, [this](OBSQTDisplay *display) {
		obs_display_add_draw_callback(display->GetDisplay(), OBSBasic::RenderPulseHorizontal, this);
		obs_display_set_enabled(display->GetDisplay(), pulsePages && pulsePages->currentIndex() == 0);
	});
	connect(pulseVerticalDisplay, &OBSQTDisplay::DisplayCreated, this, [this](OBSQTDisplay *display) {
		obs_display_add_draw_callback(display->GetDisplay(), OBSBasic::RenderPulseVertical, this);
		obs_display_set_enabled(display->GetDisplay(), pulsePages && pulsePages->currentIndex() == 0);
	});
	connect(ui->preview, &OBSQTDisplay::DisplayCreated, this, [this](OBSQTDisplay *display) {
		const bool horizontalCamera = pulsePages && pulsePages->currentIndex() == 2 && pulseCameraEditors &&
					      pulseCameraEditors->currentIndex() == 0;
		obs_display_set_enabled(display->GetDisplay(), horizontalCamera &&
					(previewEnabled || IsPreviewProgramMode()));
	});

	pulseStatusTimer = new QTimer(this);
	/* Status stays responsive without rebuilding shell data roughly 86 times a
	 * minute. Expensive page data is also refreshed only for the visible page. */
	pulseStatusTimer->setInterval(1000);
	connect(pulseStatusTimer, &QTimer::timeout, this, &OBSBasic::UpdatePulseWeaverShell);
	pulseStatusTimer->start();
	pulseYouTubeChatTimer = new QTimer(this);
	pulseYouTubeChatTimer->setInterval(4000);
	connect(pulseYouTubeChatTimer, &QTimer::timeout, this, &OBSBasic::PollPulseWeaverYouTubeChat);
	SetPulseWeaverWorkspace(0);
	QTimer::singleShot(0, this, &OBSBasic::RefreshPulseWeaverChatComposer);
}

void OBSBasic::RestorePulseWeaverYouTubeAccount()
{
#ifdef YOUTUBE_ENABLED
	/* OBSBasic::Get() is not valid while the OBSBasic constructor is still
	 * running. Restore only after OBSApp owns the window and InitBasicConfig
	 * has supplied the isolated Pulse Weaver configuration. */
	if (pulseYouTubeAuth || !Config())
		return;
	auto saved = std::make_shared<YoutubeApiWrappers>(youtubeServices.at(1));
	if (!saved->LoadPulseWeaverAccount())
		return;
	pulseYouTubeAuth = saved;
	if (pulseYouTubeButton)
		pulseYouTubeButton->setText("YOUTUBE CONNECTED");
	setProperty("pulseWeaverYouTubeReady", true);
	if (pulseDestinationStatus)
		pulseDestinationStatus->setText("YouTube account restored · destination prepares when you go live");
	RefreshPulseWeaverChatComposer();
#endif
}

void OBSBasic::UpdatePulseWeaverPreviewVisibility()
{
	if (!pulsePages)
		return;
	const int index = pulsePages->currentIndex();
	const bool visible = isVisible() && !isMinimized();
	const bool show = visible && index == 0;
	const bool camera = visible && index == 2;
	const bool verticalCamera = camera && pulseCameraEditors && pulseCameraEditors->currentIndex() == 1;
	/* OBS displays continue drawing on Windows after their Qt page is hidden.
	 * Keep only the visible workspace's preview surfaces active so streaming
	 * outputs get the GPU time instead of rendering three hidden previews. */
	if (pulseHorizontalDisplay && pulseHorizontalDisplay->GetDisplay())
		obs_display_set_enabled(pulseHorizontalDisplay->GetDisplay(), show);
	if (pulseVerticalDisplay && pulseVerticalDisplay->GetDisplay())
		obs_display_set_enabled(pulseVerticalDisplay->GetDisplay(), show);
	if (ui->preview && ui->preview->GetDisplay())
		obs_display_set_enabled(ui->preview->GetDisplay(), camera && !verticalCamera &&
					(previewEnabled || IsPreviewProgramMode()));
	if (program && program->GetDisplay())
		obs_display_set_enabled(program->GetDisplay(), false);
	if (pulseVerticalEditor)
		pulseVerticalEditor->SetDisplayEnabled(verticalCamera);
}

void OBSBasic::SetPulseWeaverWorkspace(int index)
{
	if (!pulsePages || index < 0 || index >= pulsePages->count())
		return;
	pulsePages->setCurrentIndex(index);
	for (auto *button : findChildren<QPushButton *>()) {
		if (button->property("pulseWorkspaceIndex").isValid()) {
			const bool selected = button->property("pulseWorkspaceIndex").toInt() == index;
			if (button->property("pulseWorkspaceActive").toBool() != selected) {
				button->setProperty("pulseWorkspaceActive", selected);
				button->style()->unpolish(button);
				button->style()->polish(button);
				button->update();
			}
		}
	}
	UpdatePulseWeaverPreviewVisibility();
	const bool camera = index == 2;
	const bool verticalCamera = camera && pulseCameraEditors && pulseCameraEditors->currentIndex() == 1;
	/* Studio Mode remains active underneath Camera so scene selection is
	 * preview-only, but its duplicate programme surface belongs in Show
	 * Control, not beside the selected editor canvas. */
	if (programOptions)
		programOptions->setVisible(false);
	if (programWidget)
		programWidget->setVisible(false);
	if (ui->previewLabel)
		ui->previewLabel->setVisible(false);
	if (camera && !verticalCamera) {
		/* Camera is an editor, so its canvas must always follow the available
		 * panel rather than retaining OBS's last manual zoom percentage. */
		setPreviewScalingWindow();
	}
	if (ui->scenesDock)
		ui->scenesDock->setVisible(camera && !verticalCamera);
	if (ui->sourcesDock)
		ui->sourcesDock->setVisible(camera && !verticalCamera);
	if (ui->transitionsDock)
		ui->transitionsDock->setVisible(camera && !verticalCamera);
	if (ui->mixerDock) {
		ui->mixerDock->setMinimumHeight(0);
		ui->mixerDock->setMaximumHeight(QWIDGETSIZE_MAX);
		ui->mixerDock->setVisible(camera && !verticalCamera);
	}
	/* Show Control owns live-output operations. The upstream OBS Controls dock
	 * remains constructed for engine compatibility but is never exposed in
	 * Camera, preventing a second Start Streaming path. */
	if (controlsDock)
		controlsDock->setVisible(false);
	if (camera && !verticalCamera && pulseCameraDockHost)
		pulseCameraDockHost->setDockNestingEnabled(true);
	UpdatePulseWeaverShell();
}

void OBSBasic::SetPulseWeaverCameraOutput(bool vertical)
{
	if (!pulseCameraEditors)
		return;
	const bool wasVertical = pulseCameraEditors->currentIndex() == 1;
	/* Hiding every native dock lets QMainWindow redistribute their space. Save
	 * the user's exact horizontal arrangement before entering the independent
	 * vertical editor, then restore it after the docks are visible again. */
	if (vertical && !wasVertical && pulseCameraDockHost)
		pulseCameraHorizontalDockLayout = pulseCameraDockHost->saveState(1);
	pulseCameraEditors->setCurrentIndex(vertical ? 1 : 0);
	if (vertical && pulseVerticalEditor)
		pulseVerticalEditor->Refresh();
	if (pulsePages && pulsePages->currentIndex() == 2)
		SetPulseWeaverWorkspace(2);
	if (!vertical && wasVertical && pulseCameraDockHost && !pulseCameraHorizontalDockLayout.isEmpty()) {
		QTimer::singleShot(0, this, [this] {
			if (pulseCameraDockHost && !pulseCameraHorizontalDockLayout.isEmpty())
				pulseCameraDockHost->restoreState(pulseCameraHorizontalDockLayout, 1);
			setPreviewScalingWindow();
		});
	}
}

static void pulseRestoreImportedVerticalTransforms(const QJsonObject &root)
{
	QStringList verticalCanvasUuids;
	for (const QJsonValue &value : root.value("canvases").toArray()) {
		const QJsonObject info = value.toObject().value("info").toObject();
		const QString name = info.value("name").toString();
		if (name.contains("vertical", Qt::CaseInsensitive))
			verticalCanvasUuids << info.value("uuid").toString();
	}
	obs_canvas_t *canvas = PulseWeaverGetVerticalCanvas();
	if (!canvas)
		return;
	for (const QJsonValue &sourceValue : root.value("sources").toArray()) {
		const QJsonObject sourceJson = sourceValue.toObject();
		if (!verticalCanvasUuids.contains(sourceJson.value("canvas_uuid").toString()))
			continue;
		obs_source_t *sceneSource = obs_canvas_get_source_by_name(
			canvas, sourceJson.value("name").toString().toUtf8().constData());
		obs_scene_t *scene = sceneSource ? obs_scene_from_source(sceneSource) : nullptr;
		if (!scene) {
			obs_source_release(sceneSource);
			continue;
		}
		for (const QJsonValue &itemValue : sourceJson.value("settings").toObject().value("items").toArray()) {
			const QJsonObject itemJson = itemValue.toObject();
			const QByteArray uuid = itemJson.value("source_uuid").toString().toUtf8();
			struct Match { QByteArray uuid; obs_sceneitem_t *item = nullptr; } match{uuid};
			obs_scene_enum_items(scene, [](obs_scene_t *, obs_sceneitem_t *item, void *opaque) {
				auto *match = static_cast<Match *>(opaque);
				obs_source_t *source = obs_sceneitem_get_source(item);
				if (source && match->uuid == obs_source_get_uuid(source)) {
					match->item = item;
					obs_sceneitem_addref(item);
					return false;
				}
				return true;
			}, &match);
			if (!match.item)
				continue;
			obs_transform_info transform = {};
			obs_sceneitem_get_info2(match.item, &transform);
			const QJsonObject pos = itemJson.value("pos").toObject();
			const QJsonObject scale = itemJson.value("scale").toObject();
			const QJsonObject bounds = itemJson.value("bounds").toObject();
			transform.pos = {float(pos.value("x").toDouble()), float(pos.value("y").toDouble())};
			transform.scale = {float(scale.value("x").toDouble(1.0)), float(scale.value("y").toDouble(1.0))};
			transform.rot = float(itemJson.value("rot").toDouble());
			transform.alignment = uint32_t(itemJson.value("align").toInt(transform.alignment));
			transform.bounds_type = obs_bounds_type(itemJson.value("bounds_type").toInt(int(transform.bounds_type)));
			transform.bounds_alignment = uint32_t(itemJson.value("bounds_align").toInt(transform.bounds_alignment));
			transform.bounds = {float(bounds.value("x").toDouble()), float(bounds.value("y").toDouble())};
			transform.crop_to_bounds = itemJson.value("bounds_crop").toBool(transform.crop_to_bounds);
			obs_sceneitem_set_info2(match.item, &transform);
			obs_sceneitem_crop crop{itemJson.value("crop_left").toInt(), itemJson.value("crop_top").toInt(),
				itemJson.value("crop_right").toInt(), itemJson.value("crop_bottom").toInt()};
			obs_sceneitem_set_crop(match.item, &crop);
			obs_sceneitem_release(match.item);
		}
		obs_source_release(sceneSource);
	}
	obs_canvas_release(canvas);
}

void OBSBasic::ImportPulseWeaverObsSetup()
{
	QDialog dialog(this);
	dialog.setWindowTitle("Import an existing OBS setup");
	dialog.setMinimumWidth(680);
	auto *layout = new QVBoxLayout(&dialog);

	auto *heading = new QLabel("Import from OBS");
	heading->setObjectName("PulseWeaverHeading");
	layout->addWidget(heading);
	auto *description = new QLabel(
		"Pulse Weaver reads an OBS data folder and creates new isolated copies of its profiles and scene "
		"collections. Sources, filters, transitions, hotkeys, audio, video and encoder settings are retained. "
		"The original OBS folder is never written to or removed. Close normal OBS before importing so its files "
		"cannot change during the copy.");
	description->setWordWrap(true);
	description->setObjectName("PulseWeaverMuted");
	layout->addWidget(description);

	const char *savedPathValue = config_get_string(App()->GetUserConfig(), "PulseWeaver", "ObsImportPath");
	QString initialPath = savedPathValue ? QString::fromUtf8(savedPathValue).trimmed() : QString();
	if (initialPath.isEmpty())
		initialPath = pulseDefaultObsDataPath();

	auto *pathRow = new QHBoxLayout;
	auto *path = new QLineEdit(initialPath);
	path->setPlaceholderText("C:\\Users\\you\\AppData\\Roaming\\obs-studio");
	auto *useDefault = new QPushButton("Use default");
	auto *browse = new QPushButton("Browse…");
	pathRow->addWidget(path, 1);
	pathRow->addWidget(useDefault);
	pathRow->addWidget(browse);
	layout->addLayout(pathRow);

	auto *detected = new QLabel;
	detected->setWordWrap(true);
	detected->setObjectName("PulseWeaverMuted");
	layout->addWidget(detected);
	auto *profilesOption = new QCheckBox("Import OBS profiles (video, audio, output and encoder settings)");
	auto *servicesOption = new QCheckBox("Include copied stream service/account destination data (service.json)");
	auto *scenesOption = new QCheckBox("Import scene collections (scenes, sources, filters, transitions and hotkeys)");
	auto *activateOption = new QCheckBox("Switch Pulse Weaver to the first imported profile and scene collection");
	profilesOption->setChecked(true);
	servicesOption->setChecked(true);
	scenesOption->setChecked(true);
	activateOption->setChecked(true);
	for (QCheckBox *option : {profilesOption, servicesOption, scenesOption, activateOption})
		layout->addWidget(option);
	connect(profilesOption, &QCheckBox::toggled, servicesOption, &QCheckBox::setEnabled);

	auto *warning = new QLabel(
		"Not copied: normal OBS global.ini, browser cookies/cache, installed plug-in binaries or logs. "
		"Sources that require a plug-in not installed in Pulse Weaver may remain unavailable until a compatible "
		"plug-in is added.");
	warning->setWordWrap(true);
	warning->setObjectName("PulseWeaverWarning");
	layout->addWidget(warning);

	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel | QDialogButtonBox::Ok);
	buttons->button(QDialogButtonBox::Ok)->setText("Import copy");
	layout->addWidget(buttons);
	connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

	auto updateDetection = [path, detected, profilesOption, scenesOption, buttons] {
		const QString root = pulseNormaliseObsDataPath(path->text());
		const QDir profiles(QDir(root).filePath("basic/profiles"));
		const QDir scenes(QDir(root).filePath("basic/scenes"));
		const int profileCount = profiles.exists()
					 ? profiles.entryList(QDir::Dirs | QDir::NoDotAndDotDot).size()
					 : 0;
		const int sceneCount = scenes.exists() ? scenes.entryList(QStringList{"*.json"}, QDir::Files).size() : 0;
		const bool valid = (profilesOption->isChecked() && profileCount > 0) ||
				   (scenesOption->isChecked() && sceneCount > 0);
		detected->setText(valid ? QString("Detected %1 profile(s) and %2 scene collection(s) in %3")
						.arg(profileCount)
						.arg(sceneCount)
						.arg(QDir::toNativeSeparators(root))
					 : "No compatible OBS profiles or scene collections were found at this location.");
		buttons->button(QDialogButtonBox::Ok)->setEnabled(valid);
	};
	connect(path, &QLineEdit::textChanged, &dialog, [updateDetection] { updateDetection(); });
	connect(profilesOption, &QCheckBox::toggled, &dialog, [updateDetection] { updateDetection(); });
	connect(scenesOption, &QCheckBox::toggled, &dialog, [updateDetection] { updateDetection(); });
	connect(useDefault, &QPushButton::clicked, &dialog, [path] { path->setText(pulseDefaultObsDataPath()); });
	connect(browse, &QPushButton::clicked, &dialog, [&dialog, path] {
		const QString selected = QFileDialog::getExistingDirectory(
			&dialog, "Choose the OBS data folder", path->text(),
			QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
		if (!selected.isEmpty())
			path->setText(pulseNormaliseObsDataPath(selected));
	});
	connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
	updateDetection();
	if (dialog.exec() != QDialog::Accepted)
		return;

	const QString sourceRoot = pulseNormaliseObsDataPath(path->text());
	char destinationBuffer[1024] = {};
	if (GetAppConfigPath(destinationBuffer, sizeof(destinationBuffer), "obs-studio") <= 0) {
		OBSMessageBox::critical(this, "OBS import", "Pulse Weaver could not locate its isolated configuration folder.");
		return;
	}
	const QString destinationRoot = QDir::cleanPath(QString::fromUtf8(destinationBuffer));
	const QString sourceCanonical = QFileInfo(sourceRoot).canonicalFilePath();
	const QString destinationCanonical = QFileInfo(destinationRoot).canonicalFilePath();
	if (sourceCanonical.isEmpty() || sourceCanonical.compare(destinationCanonical, Qt::CaseInsensitive) == 0) {
		OBSMessageBox::warning(this, "OBS import", "Choose normal OBS's data folder, not Pulse Weaver's isolated config folder.");
		return;
	}

	if (OBSMessageBox::question(
		    this, "Import OBS setup",
		    "Create new copies inside Pulse Weaver now? Existing Pulse Weaver profiles and scenes will not be overwritten.\n\nSource: " +
			    QDir::toNativeSeparators(sourceRoot)) != QMessageBox::Yes)
		return;

	config_set_string(App()->GetUserConfig(), "PulseWeaver", "ObsImportPath", sourceRoot.toUtf8().constData());
	config_save_safe(App()->GetUserConfig(), "tmp", nullptr);

	const QString destinationProfiles = QDir(destinationRoot).filePath("basic/profiles");
	const QString destinationScenes = QDir(destinationRoot).filePath("basic/scenes");
	QDir().mkpath(destinationProfiles);
	QDir().mkpath(destinationScenes);
	QStringList importedProfileNames;
	QStringList importedCollectionNames;
	QStringList failures;
	QJsonObject firstImportedSceneData;

	if (profilesOption->isChecked()) {
		const QDir profilesSource(QDir(sourceRoot).filePath("basic/profiles"));
		for (const QFileInfo &profileDirectory :
		     profilesSource.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
			const QString sourceBasic = QDir(profileDirectory.absoluteFilePath()).filePath("basic.ini");
			if (!QFileInfo::exists(sourceBasic))
				continue;
			QSettings originalSettings(sourceBasic, QSettings::IniFormat);
			QString originalName = originalSettings.value("General/Name", profileDirectory.fileName()).toString().trimmed();
			if (originalName.isEmpty())
				originalName = profileDirectory.fileName();
			QString importedName = originalName + " (OBS Import)";
			int suffix = 2;
			while (GetProfileByName(importedName.toStdString()) || importedProfileNames.contains(importedName))
				importedName = originalName + " (OBS Import " + QString::number(suffix++) + ")";

			const QString directoryName = pulseUniqueDirectory(destinationProfiles, profileDirectory.fileName());
			const QString destination = QDir(destinationProfiles).filePath(directoryName);
			QString error;
			if (!pulseCopyDirectory(profileDirectory.absoluteFilePath(), destination, error)) {
				QDir(destination).removeRecursively();
				failures << profileDirectory.fileName() + ": " + error;
				continue;
			}
			if (!servicesOption->isChecked())
				QFile::remove(QDir(destination).filePath("service.json"));
			QSettings importedSettings(QDir(destination).filePath("basic.ini"), QSettings::IniFormat);
			importedSettings.setValue("General/Name", importedName);
			importedSettings.sync();
			if (importedSettings.status() != QSettings::NoError) {
				QDir(destination).removeRecursively();
				failures << profileDirectory.fileName() + ": imported basic.ini could not be updated";
				continue;
			}
			importedProfileNames << importedName;
		}
	}

	if (scenesOption->isChecked()) {
		const QDir scenesSource(QDir(sourceRoot).filePath("basic/scenes"));
		for (const QFileInfo &sceneFile :
		     scenesSource.entryInfoList(QStringList{"*.json"}, QDir::Files, QDir::Name)) {
			QFile input(sceneFile.absoluteFilePath());
			if (!input.open(QIODevice::ReadOnly)) {
				failures << sceneFile.fileName() + ": could not be read";
				continue;
			}
			QJsonParseError parseError;
			QJsonDocument document = QJsonDocument::fromJson(input.readAll(), &parseError);
			if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
				failures << sceneFile.fileName() + ": invalid scene collection JSON";
				continue;
			}
			QJsonObject root = document.object();
			QString originalName = root.value("name").toString(sceneFile.completeBaseName()).trimmed();
			if (originalName.isEmpty())
				originalName = sceneFile.completeBaseName();
			QString importedName = originalName + " (OBS Import)";
			int suffix = 2;
			while (GetSceneCollectionByName(importedName.toStdString()) || importedCollectionNames.contains(importedName))
				importedName = originalName + " (OBS Import " + QString::number(suffix++) + ")";
			root.insert("name", importedName);
			const QString stemBase = sceneFile.completeBaseName() + "-pulse-import";
			QString fileStem = stemBase;
			int fileSuffix = 2;
			while (QFileInfo::exists(QDir(destinationScenes).filePath(fileStem + ".json")))
				fileStem = stemBase + "-" + QString::number(fileSuffix++);
			QSaveFile output(QDir(destinationScenes).filePath(fileStem + ".json"));
			if (!output.open(QIODevice::WriteOnly) ||
			    output.write(QJsonDocument(root).toJson(QJsonDocument::Indented)) < 0 || !output.commit()) {
				failures << sceneFile.fileName() + ": could not be saved in the isolated config";
				continue;
			}
			if (firstImportedSceneData.isEmpty())
				firstImportedSceneData = root;
			importedCollectionNames << importedName;
		}
	}

	RefreshProfiles(true);
	RefreshSceneCollections(true);
	if (activateOption->isChecked()) {
		if (!importedProfileNames.isEmpty()) {
			if (auto importedProfile = GetProfileByName(importedProfileNames.first().toStdString()))
				ActivateProfile(*importedProfile, true);
		}
		if (!importedCollectionNames.isEmpty()) {
			if (auto importedCollection = GetSceneCollectionByName(importedCollectionNames.first().toStdString()))
				ActivateSceneCollection(*importedCollection);
		}
	}
	/* Activation may expose an Aitum/native vertical canvas after Pulse
	 * Weaver's own canvas already exists. Re-run migration now so its scene
	 * item position, scale, crop, bounds and rotation are copied verbatim. */
	EnsurePulseWeaverVerticalCanvas();
	pulseRestoreImportedVerticalTransforms(firstImportedSceneData);

	QString summary = QString("Imported %1 profile(s) and %2 scene collection(s) into Pulse Weaver's isolated config.")
				  .arg(importedProfileNames.size())
				  .arg(importedCollectionNames.size());
	if (!failures.isEmpty())
		summary += "\n\nSkipped items:\n- " + failures.join("\n- ");
	summary += "\n\nNormal OBS was not modified.";
	OBSMessageBox::information(this, "OBS import complete", summary);
	RefreshPulseWeaverSceneSelectors();
	if (pulseVerticalEditor)
		pulseVerticalEditor->Refresh();
}

void OBSBasic::ShutdownPulseWeaverShell()
{
	setProperty("pulseWeaverGoLiveSession", false);
	/* Cancel detached YouTube work before shutdown starts releasing any state.
	 * StopPulseWeaverSecondaryOutputs drains an in-flight request through the
	 * request mutex before it cleans up the broadcasts. */
	pulseYouTubeChatCancellation->fetch_add(1, std::memory_order_acq_rel);
	++pulseYouTubeChatGeneration;
	pulseYouTubeChatWorkers->waitUntilIdle();
	/* These are additional native libobs displays owned by the product shell.
	 * They must be released before obs_shutdown(), just like OBS's primary
	 * preview and program displays, or the process can remain alive after the
	 * last window has disappeared. */
	if (pulseStatusTimer)
		pulseStatusTimer->stop();
	StopPulseWeaverRecordings();
	StopPulseWeaverSecondaryOutputs();
	RestorePulseWeaverAudioRouting();
	if (pulseKickOutputControl) {
		pulseKickOutputControl->setProperty("command", "stop");
		pulseKickOutputControl->click();
	}
	for (obs_canvas_t *canvas : std::as_const(pulseOutputCanvases)) {
		obs_canvas_remove(canvas);
		obs_canvas_release(canvas);
	}
	pulseOutputCanvases.clear();
	pulseCanvasStingerTransitions.clear();
	if (pulseHorizontalDisplay)
		pulseHorizontalDisplay->DestroyDisplay();
	if (pulseVerticalDisplay)
		pulseVerticalDisplay->DestroyDisplay();
	if (pulseVerticalEditor)
		pulseVerticalEditor->Shutdown();
	/* OBSBasic and its generated UI retain pointers to these native docks.
	 * Return ownership to OBSBasic before the nested Camera host is destroyed;
	 * this also prevents the shutdown double-destruction seen in older builds. */
	if (pulseCameraDockHost) {
		const QList<QPair<QDockWidget *, Qt::DockWidgetArea>> docks{
			{ui->scenesDock, Qt::LeftDockWidgetArea}, {ui->sourcesDock, Qt::RightDockWidgetArea},
			{ui->mixerDock, Qt::BottomDockWidgetArea}, {ui->transitionsDock, Qt::BottomDockWidgetArea},
			{controlsDock, Qt::BottomDockWidgetArea}};
		for (const auto &[dock, area] : docks) {
			dock->setFloating(false);
			pulseCameraDockHost->removeDockWidget(dock);
			dock->setParent(this);
			addDockWidget(area, dock);
		}
	}
}

void OBSBasic::UpdatePulseWeaverShell()
{
	UpdatePulseWeaverPreviewVisibility();
	/* Some upstream actions can request these widgets again. Keep native
	 * commands functional while preventing the old OBS chrome from leaking
	 * back into the Pulse Weaver shell. */
	if (ui->menubar->isVisible())
		ui->menubar->hide();
	if (ui->statusbar->isVisible())
		ui->statusbar->hide();
	const bool streaming = obs_frontend_streaming_active() ||
		(pulseYouTubeOutput && obs_output_active(pulseYouTubeOutput)) ||
		(pulseYouTubeSecondOutput && obs_output_active(pulseYouTubeSecondOutput)) || property("pulseWeaverKickLive").toBool();
	setProperty("pulseWeaverAnyLive", streaming);
	if (controlsDock && controlsDock->isVisible())
		controlsDock->hide();
	const bool unstableRecording = PulseWeaverRecordingActive();
	const bool recording = obs_frontend_recording_active() || unstableRecording;
	setProperty("pulseWeaverRecordingActive", recording);
	const bool replay = obs_frontend_replay_buffer_active();
	const bool virtualCamera = obs_frontend_virtualcam_active();
	if (pulseEngineStatus)
		pulseEngineStatus->setText(QString("Stream %1   ·   Recording %2")
					   .arg(streaming ? "live" : "ready")
					   .arg(recording ? "on" : "ready"));
	auto reflectActive = [](QPushButton *button, bool active) {
		if (button->property("pulseActive").toBool() != active) {
			button->setProperty("pulseActive", active);
			button->style()->unpolish(button);
			button->style()->polish(button);
			button->update();
		}
	};
	if (pulseStreamButton) {
		const bool live = streaming || property("pulseWeaverGoLiveSession").toBool();
		pulseStreamButton->setText(live ? "End stream" : "Go live");
		reflectActive(pulseStreamButton, live);
	}
	if (pulseRecordButton) {
		pulseRecordButton->setText(recording ? "Stop recording" : "Record");
		reflectActive(pulseRecordButton, recording);
	}
	if (pulseRecordingDestination)
		pulseRecordingDestination->setEnabled(!recording);
	if (pulseReplayButton)
		pulseReplayButton->setText(replay ? "Stop replay buffer" : "Replay buffer");
	if (pulseVirtualCameraButton)
		pulseVirtualCameraButton->setText(virtualCamera ? "Stop virtual camera" : "Virtual camera");

	obs_canvas_t *vertical = PulseWeaverGetVerticalCanvas();
	/* Provision the product-owned canvas after profile/video initialisation.
	 * The immediate constructor refresh is intentionally too early; the first
	 * timer tick is the safe point and makes vertical a default capability. */
	if (!vertical && Config()) {
		EnsurePulseWeaverVerticalCanvas();
		vertical = PulseWeaverGetVerticalCanvas();
	}
	if (pulseVerticalStatus)
		pulseVerticalStatus->setText(vertical ? QString("Native vertical canvas is ready: %1.")
							.arg(QString::fromUtf8(obs_canvas_get_name(vertical)))
						      : "No vertical canvas yet. Create one without installing Aitum.");
	if (pulseRepairVerticalButton)
		pulseRepairVerticalButton->setVisible(!vertical);
	const int workspace = pulsePages && isVisible() && !isMinimized() ? pulsePages->currentIndex() : -1;
	if (workspace == 0) {
		RefreshPulseWeaverStages();
		RefreshPulseWeaverPlatformPreviews();
		RefreshPulseWeaverStreamStats();
		RefreshPulseWeaverCompactMixer();
	} else if (workspace == 2) {
		RefreshPulseWeaverSceneSelectors();
	}

	if (pulseDualFormatStatus && pulseTwitchDestination) {
		const QString mode = pulseTwitchDestination->currentData().toString();
		pulseDualFormatStatus->setText(mode == "dual" ? "TWITCH 16:9 + 9:16" : mode == "vertical" ?
			"TWITCH 9:16" : mode == "horizontal" ? "TWITCH 16:9" : "TWITCH OFF");
	}
	obs_canvas_release(vertical);
}

void OBSBasic::RefreshPulseWeaverCompactMixer()
{
	if (!pulseCompactMixerBody || !pulseCompactMixerBody->layout())
		return;
	struct AudioSource {
		QString uuid;
	};
	std::vector<AudioSource> sources;
	obs_enum_sources(
		[](void *opaque, obs_source_t *source) {
			/* Match the native OBS mixer definition of Active. A source can
			 * remain generally active while its audio path is inactive. */
			if ((obs_source_get_output_flags(source) & OBS_SOURCE_AUDIO) && obs_source_active(source) &&
			    obs_source_audio_active(source))
				static_cast<std::vector<AudioSource> *>(opaque)->push_back(
					{QString::fromUtf8(obs_source_get_uuid(source))});
			return true;
		},
		&sources);
	QStringList signatureParts;
	for (const auto &source : sources)
		signatureParts << source.uuid;
	const QString signature = signatureParts.join('|');
	if (signature == pulseCompactMixerSignature)
		return;
	pulseCompactMixerSignature = signature;
	QLayout *layout = pulseCompactMixerBody->layout();
	while (QLayoutItem *item = layout->takeAt(0)) {
		delete item->widget();
		delete item;
	}
	layout->setAlignment(Qt::AlignLeft);
	for (const AudioSource &entry : sources) {
		obs_source_t *source = obs_get_source_by_uuid(entry.uuid.toUtf8().constData());
		if (!source)
			continue;
		auto *control = new VolumeControl(source, pulseCompactMixerBody, true);
		control->setObjectName("PulseWeaverNativeMixerStrip");
		control->setMinimumWidth(96);
		control->setMaximumWidth(132);
		control->setFixedHeight(180);
		control->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
		layout->addWidget(control);
		obs_source_release(source);
	}
	if (sources.empty())
		layout->addWidget(new QLabel("No active audio sources. Add one in Camera → Sources."));
}

void OBSBasic::RefreshPulseWeaverSceneSelectors()
{
	if (!pulseHorizontalSceneSelector || !pulseVerticalSceneSelector)
		return;
	auto updateCombo = [](QComboBox *combo, const QStringList &names, const QString &current) {
		QStringList existing;
		for (int i = 0; i < combo->count(); ++i)
			existing.push_back(combo->itemData(i).toString());
		QSignalBlocker blocker(combo);
		if (existing != names) {
			combo->clear();
			for (const QString &name : names)
				combo->addItem(name, name);
		}
		const int selected = combo->findData(current);
		if (selected >= 0 && combo->currentIndex() != selected)
			combo->setCurrentIndex(selected);
		combo->setEnabled(!names.isEmpty());
	};

	QStringList horizontalNames;
	obs_enum_scenes(
		[](void *opaque, obs_source_t *scene) {
			static_cast<QStringList *>(opaque)->push_back(QString::fromUtf8(obs_source_get_name(scene)));
			return true;
		},
		&horizontalNames);
	obs_source_t *horizontalCurrent = obs_frontend_get_current_scene();
	const QString horizontalName = horizontalCurrent ? QString::fromUtf8(obs_source_get_name(horizontalCurrent)) : QString();
	obs_source_release(horizontalCurrent);
	updateCombo(pulseHorizontalSceneSelector, horizontalNames, horizontalName);

	QStringList verticalNames;
	obs_canvas_t *canvas = PulseWeaverGetVerticalCanvas();
	if (canvas)
		obs_canvas_enum_scenes(
			canvas,
			[](void *opaque, obs_source_t *scene) {
				static_cast<QStringList *>(opaque)->push_back(QString::fromUtf8(obs_source_get_name(scene)));
				return true;
			},
			&verticalNames);
	obs_source_t *verticalCurrent = canvas ? obs_canvas_get_channel(canvas, 0) : nullptr;
	const QString verticalName = verticalCurrent ? QString::fromUtf8(obs_source_get_name(verticalCurrent)) : QString();
	obs_source_release(verticalCurrent);
	obs_canvas_release(canvas);
	updateCombo(pulseVerticalSceneSelector, verticalNames, verticalName);
}

void OBSBasic::RefreshPulseWeaverTransitionSelector()
{
	if (!pulseStageTransitionSelector && !pulseVerticalStageTransitionSelector)
		return;
	QMap<QString, QString> stingers;
	for (const auto &[uuid, transition] : transitions) {
		obs_source_t *source = transition;
		if (!source || strcmp(obs_source_get_id(source), "obs_stinger_transition") != 0)
			continue;
		const QString name = QString::fromUtf8(obs_source_get_name(source));
		if (!name.isEmpty()) stingers.insert(name, "stinger:" + name);
	}
	auto populate = [&stingers](QComboBox *selector) {
		if (!selector)
			return;
		const QString selected = selector->currentData().toString();
		QSignalBlocker blocker(selector);
		selector->clear();
		selector->addItem("Fade", "fade");
		selector->addItem("Cut", "cut");
		for (auto it = stingers.cbegin(); it != stingers.cend(); ++it)
			selector->addItem("Stinger · " + it.key(), it.value());
		const int index = selector->findData(selected);
		selector->setCurrentIndex(index >= 0 ? index : 0);
	};
	populate(pulseStageTransitionSelector);
	populate(pulseVerticalStageTransitionSelector);
}

void OBSBasic::RefreshPulseWeaverStages()
{
	if (!pulseStageSelector || pulseStageUpdating)
		return;
	QJsonArray stages = loadPulseWeaverStages();
	if (stages.isEmpty()) {
		obs_source_t *horizontal = obs_frontend_get_current_scene();
		obs_canvas_t *canvas = PulseWeaverGetVerticalCanvas();
		obs_source_t *vertical = canvas ? obs_canvas_get_channel(canvas, 0) : nullptr;
		const QString horizontalName = horizontal ? QString::fromUtf8(obs_source_get_name(horizontal)) : QString();
		const QString verticalName = vertical ? QString::fromUtf8(obs_source_get_name(vertical)) : QString();
		QJsonObject assignments;
		auto seed = [&assignments, &horizontalName, &verticalName](const QString &provider, const QString &mode) {
			if ((mode == "horizontal" || mode == "dual") && !horizontalName.isEmpty())
				assignments.insert(provider + "_horizontal", QJsonObject{{"canvas", "horizontal"}, {"scene", horizontalName}, {"excluded", QJsonArray{}}});
			if ((mode == "vertical" || mode == "dual") && !verticalName.isEmpty())
				assignments.insert(provider + "_vertical", QJsonObject{{"canvas", "vertical"}, {"scene", verticalName}, {"excluded", QJsonArray{}}});
		};
		seed("twitch", pulseTwitchDestination ? pulseTwitchDestination->currentData().toString() : QString("horizontal"));
		seed("youtube", pulseYouTubeDestination ? pulseYouTubeDestination->currentData().toString() : QString("off"));
		seed("kick", pulseKickDestination ? pulseKickDestination->currentData().toString() : QString("off"));
		seed("recording", pulseRecordingDestination ? pulseRecordingDestination->currentData().toString() : QString("standard"));
		stages.append(QJsonObject{{"name", "Current Show"},
			{"horizontal", horizontalName}, {"vertical", verticalName},
			{"horizontalTransition", "fade"}, {"horizontalDurationMs", 500},
			{"verticalTransition", "fade"}, {"verticalDurationMs", 500}, {"assignments", assignments}});
		obs_source_release(horizontal);
		obs_source_release(vertical);
		obs_canvas_release(canvas);
		savePulseWeaverStages(stages);
	}
	const QByteArray signature = QJsonDocument(stages).toJson(QJsonDocument::Compact);
	if (signature == pulseStageSignature)
		return;
	pulseStageSignature = signature;
	const QString currentName = pulseStageSelector->currentData(Qt::UserRole + 1).toString();
	QSignalBlocker blocker(pulseStageSelector);
	pulseStageUpdating = true;
	pulseStageSelector->clear();
	for (const QJsonValue &value : stages) {
		const QJsonObject stage = value.toObject();
		const QString name = stage.value("name").toString("Stage");
		QStringList summary;
		for (const QString &provider : {QString("twitch"), QString("youtube"), QString("kick"), QString("recording")}) {
			const bool horizontal = !pulseStageAssignment(stage, provider, "horizontal").isEmpty();
			const bool vertical = !pulseStageAssignment(stage, provider, "vertical").isEmpty();
			if (horizontal || vertical)
				summary << provider.left(1).toUpper() + provider.mid(1) + " " +
					(horizontal && vertical ? "Dual" : vertical ? "9:16" : "16:9");
		}
		pulseStageSelector->addItem(name + (summary.isEmpty() ? "  ·  no assignments" : "  ·  " + summary.join(" + ")),
			QString::fromUtf8(QJsonDocument(stage).toJson(QJsonDocument::Compact)));
		pulseStageSelector->setItemData(pulseStageSelector->count() - 1, name, Qt::UserRole + 1);
	}
	const int previous = pulseStageSelector->findData(currentName, Qt::UserRole + 1);
	if (previous >= 0)
		pulseStageSelector->setCurrentIndex(previous);
	const int selected = std::max(0, pulseStageSelector->currentIndex());
	if (selected < pulseStageSelector->count()) {
		const QJsonObject active = QJsonDocument::fromJson(pulseStageSelector->itemData(selected).toString().toUtf8()).object();
		const QString legacyTransition = active.value("transition").toString("fade");
		const int legacyDuration = std::clamp(active.value("durationMs").toInt(500), 100, 5000);
		auto updateTransition = [](QComboBox *selector, QSpinBox *durationControl,
			const QString &transition, int duration) {
			if (selector) {
				QSignalBlocker transitionBlocker(selector);
				const int transitionIndex = selector->findData(transition);
				selector->setCurrentIndex(transitionIndex >= 0 ? transitionIndex : 0);
			}
			if (durationControl) {
				QSignalBlocker durationBlocker(durationControl);
				durationControl->setValue(duration);
				durationControl->setEnabled(transition != "cut");
			}
		};
		updateTransition(pulseStageTransitionSelector, pulseStageTransitionDuration,
			active.value("horizontalTransition").toString(legacyTransition),
			std::clamp(active.value("horizontalDurationMs").toInt(legacyDuration), 100, 5000));
		updateTransition(pulseVerticalStageTransitionSelector, pulseVerticalStageTransitionDuration,
			active.value("verticalTransition").toString(legacyTransition),
			std::clamp(active.value("verticalDurationMs").toInt(legacyDuration), 100, 5000));
	}
	pulseStageUpdating = false;
}

void OBSBasic::RefreshPulseWeaverPlatformPreviews()
{
	if (!pulseStageSelector || pulseStageSelector->currentIndex() < 0)
		return;
	const QJsonObject stage = QJsonDocument::fromJson(
		pulseStageSelector->currentData().toString().toUtf8()).object();
	auto refresh = [this, &stage](const QString &route, QString &provider, QLabel *label, OBSQTDisplay *display) {
		auto modeFor = [this](const QString &candidate) {
			if (candidate == "twitch") return pulseTwitchDestination ? pulseTwitchDestination->currentData().toString() : QString("off");
			if (candidate == "youtube") return pulseYouTubeDestination ? pulseYouTubeDestination->currentData().toString() : QString("off");
			if (candidate == "kick") return pulseKickDestination ? pulseKickDestination->currentData().toString() : QString("off");
			return pulseRecordingDestination ? pulseRecordingDestination->currentData().toString() : QString("standard");
		};
		QStringList providers;
		for (const QString &candidate : {QString("twitch"), QString("youtube"), QString("kick"), QString("recording")}) {
			const QString mode = modeFor(candidate);
			const bool enabled = mode == route || mode == "dual";
			if (enabled && !pulseStageAssignment(stage, candidate, route).isEmpty())
				providers << candidate;
		}
		if (!providers.contains(provider))
			provider = providers.isEmpty() ? QString() : providers.first();
		QString title = route == "vertical" ? "Portrait program" : "Landscape program";
		if (!provider.isEmpty()) {
			const QString scene = pulseStageAssignment(stage, provider, route).value("scene").toString();
			bool live = false;
			bool ready = false;
			if (provider == "twitch") {
				live = StreamingActive();
				ready = property("pulseWeaverTwitchReady").toBool();
			} else if (provider == "youtube") {
				live = (pulseYouTubeOutput && obs_output_active(pulseYouTubeOutput)) ||
				       (pulseYouTubeSecondOutput && obs_output_active(pulseYouTubeSecondOutput));
				ready = property("pulseWeaverYouTubeReady").toBool();
			} else if (provider == "kick") {
				live = property("pulseWeaverKickLive").toBool();
				ready = property("pulseWeaverKickReady").toBool();
			} else if (provider == "recording") {
				live = route == "vertical" ?
					(pulseRecordingVerticalOutput && obs_output_active(pulseRecordingVerticalOutput)) :
					(pulseRecordingHorizontalOutput && obs_output_active(pulseRecordingHorizontalOutput));
				ready = true;
			}
			const QString state = live ? "LIVE" : ready ? "READY" : "OFF";
			title = provider.toUpper() + " · " + scene + " · " + state;
		}
		const QString richTitle = title.toHtmlEscaped();
		if (label && label->text() != richTitle)
			label->setText(richTitle);
		QWidget *card = route == "vertical" ? pulseVerticalPreviewCard.data() : pulseHorizontalPreviewCard.data();
		if (card) {
			if (card->property("pulseWeaverPreviewProvider").toString() != provider) {
				card->setProperty("pulseWeaverPreviewProvider", provider);
				card->style()->unpolish(card);
				card->style()->polish(card);
				card->update();
			}
		}
		const QString toolTip = providers.size() > 1 ? "Click to cycle platforms" :
			"Current Stage output for " + (provider.isEmpty() ? QString("this canvas") : provider.toUpper());
		if (display && display->toolTip() != toolTip)
			display->setToolTip(toolTip);
	};
	refresh("horizontal", pulseHorizontalPreviewProvider, pulseHorizontalPreviewTitle, pulseHorizontalDisplay);
	refresh("vertical", pulseVerticalPreviewProvider, pulseVerticalPreviewTitle, pulseVerticalDisplay);
}

void OBSBasic::CyclePulseWeaverPlatformPreview(bool vertical, int direction)
{
	if (!pulseStageSelector || pulseStageSelector->currentIndex() < 0)
		return;
	const QJsonObject stage = QJsonDocument::fromJson(
		pulseStageSelector->currentData().toString().toUtf8()).object();
	const QString route = vertical ? "vertical" : "horizontal";
	auto modeFor = [this](const QString &candidate) {
		if (candidate == "twitch") return pulseTwitchDestination ? pulseTwitchDestination->currentData().toString() : QString("off");
		if (candidate == "youtube") return pulseYouTubeDestination ? pulseYouTubeDestination->currentData().toString() : QString("off");
		if (candidate == "kick") return pulseKickDestination ? pulseKickDestination->currentData().toString() : QString("off");
		return pulseRecordingDestination ? pulseRecordingDestination->currentData().toString() : QString("standard");
	};
	QStringList providers;
	for (const QString &candidate : {QString("twitch"), QString("youtube"), QString("kick"), QString("recording")}) {
		const QString mode = modeFor(candidate);
		if ((mode == route || mode == "dual") && !pulseStageAssignment(stage, candidate, route).isEmpty())
			providers << candidate;
	}
	if (providers.isEmpty())
		return;
	QString &selected = vertical ? pulseVerticalPreviewProvider : pulseHorizontalPreviewProvider;
	const int current = providers.indexOf(selected);
	const int start = current < 0 ? 0 : current;
	selected = providers.at((start + (direction < 0 ? -1 : 1) + providers.size()) % providers.size());
	RefreshPulseWeaverPlatformPreviews();
}

void OBSBasic::RefreshPulseWeaverStreamStats()
{
	// The shell is shown during construction, before the frontend output handler exists.
	if (!pulseStreamStatsTitle || !outputHandler)
		return;
	struct StreamStatOutput {
		QString key;
		QString title;
		QString accent;
		obs_output_t *output = nullptr;
	};
	QVector<StreamStatOutput> outputs;
	obs_output_t *twitchOutput = nullptr;
	obs_output_t *kickOutput = nullptr;
	const QString twitchMode = pulseTwitchDestination ? pulseTwitchDestination->currentData().toString() : "off";
	const QString youtubeMode = pulseYouTubeDestination ? pulseYouTubeDestination->currentData().toString() : "off";
	const QString kickMode = pulseKickDestination ? pulseKickDestination->currentData().toString() : "off";
	if (twitchMode != "off") {
		twitchOutput = obs_frontend_get_streaming_output();
		const QString route = twitchMode == "dual" ? "DUAL" : twitchMode == "vertical" ? "9:16" : "16:9";
		outputs.push_back({"twitch", "TWITCH · " + route, "#9146FF", twitchOutput});
	}
	if (youtubeMode == "horizontal" || youtubeMode == "dual")
		outputs.push_back({"youtube-horizontal", "YOUTUBE · 16:9", "#FF3B30", pulseYouTubeOutput});
	if (youtubeMode == "vertical")
		outputs.push_back({"youtube-vertical", "YOUTUBE · 9:16", "#FF3B30", pulseYouTubeOutput});
	else if (youtubeMode == "dual")
		outputs.push_back({"youtube-vertical", "YOUTUBE · 9:16", "#FF3B30", pulseYouTubeSecondOutput});
	if (kickMode != "off") {
		kickOutput = obs_get_output_by_name("pulse_weaver_kick_output");
		outputs.push_back({"kick", "KICK · 16:9", "#53FC18", kickOutput});
	}

	if (outputs.isEmpty()) {
		pulseStreamStatsSelection.clear();
		pulseStreamStatsTitle->setText("Output · None configured");
		for (QLabel *label : {pulseStreamStatsState.data(), pulseStreamStatsUptime.data(),
			pulseStreamStatsBitrate.data(), pulseStreamStatsDropped.data(), pulseStreamStatsSent.data()})
			if (label) label->setText("—");
		obs_output_release(twitchOutput);
		obs_output_release(kickOutput);
		return;
	}
	QStringList keys;
	for (const StreamStatOutput &item : std::as_const(outputs))
		keys << item.key;
	if (!keys.contains(pulseStreamStatsSelection)) {
		auto active = std::find_if(outputs.cbegin(), outputs.cend(), [](const StreamStatOutput &item) {
			return item.output && obs_output_active(item.output);
		});
		pulseStreamStatsSelection = active == outputs.cend() ? outputs.first().key : active->key;
	}

	const qint64 now = QDateTime::currentMSecsSinceEpoch();
	for (const StreamStatOutput &item : std::as_const(outputs)) {
		const bool active = item.output && obs_output_active(item.output);
		const quint64 bytes = item.output ? obs_output_get_total_bytes(item.output) : 0;
		const qint64 previousAt = pulseStreamStatsPreviousSampleMs.value(item.key);
		const quint64 previousBytes = pulseStreamStatsPreviousBytes.value(item.key, bytes);
		if (active && previousAt > 0 && now > previousAt && bytes >= previousBytes)
			pulseStreamStatsBitrateKbps[item.key] = double(bytes - previousBytes) * 8.0 / double(now - previousAt);
		else if (!active)
			pulseStreamStatsBitrateKbps[item.key] = 0.0;
		pulseStreamStatsPreviousBytes[item.key] = bytes;
		pulseStreamStatsPreviousSampleMs[item.key] = now;
	}
	const StreamStatOutput &selected = *std::find_if(outputs.cbegin(), outputs.cend(), [this](const StreamStatOutput &item) {
		return item.key == pulseStreamStatsSelection;
	});
	const bool active = selected.output && obs_output_active(selected.output);
	const int totalFrames = selected.output ? obs_output_get_total_frames(selected.output) : 0;
	const int droppedFrames = selected.output ? obs_output_get_frames_dropped(selected.output) : 0;
	const quint64 bytes = selected.output ? obs_output_get_total_bytes(selected.output) : 0;
	const qint64 uptimeSeconds = active ? qint64(obs_output_get_video_uptime_usec(selected.output) / 1000000) : 0;
	const QString uptime = QString("%1:%2:%3").arg(uptimeSeconds / 3600, 2, 10, QLatin1Char('0'))
		.arg((uptimeSeconds / 60) % 60, 2, 10, QLatin1Char('0')).arg(uptimeSeconds % 60, 2, 10, QLatin1Char('0'));
	const double dropPercent = totalFrames > 0 ? (double(droppedFrames) * 100.0 / double(totalFrames)) : 0.0;
	const double megabytes = double(bytes) / (1024.0 * 1024.0);
	pulseStreamStatsTitle->setText("Output · " + selected.title);
	pulseStreamStatsState->setText(active ? "LIVE" : "READY");
	pulseStreamStatsUptime->setText(active ? uptime : "00:00:00");
	pulseStreamStatsBitrate->setText(active ? QString::number(pulseStreamStatsBitrateKbps.value(selected.key), 'f', 0) + " kbps" : "0 kbps");
	pulseStreamStatsDropped->setText(QString("%1%").arg(dropPercent, 0, 'f', 1));
	if (auto *detail = pulseStreamStatsCard->findChild<QLabel *>("PulseWeaverDroppedDetail"))
		detail->setText(QString("Dropped frames: %1").arg(droppedFrames));
	pulseStreamStatsSent->setText(megabytes >= 1024.0 ? QString::number(megabytes / 1024.0, 'f', 2) + " GB" :
		QString::number(megabytes, 'f', megabytes >= 10.0 ? 0 : 1) + " MB");
	if (pulseStreamStatsCard && pulseStreamStatsCard->property("pulseWeaverStatsAccent").toString() != selected.accent) {
		pulseStreamStatsCard->setProperty("pulseWeaverStatsAccent", selected.accent);
		pulseStreamStatsCard->style()->unpolish(pulseStreamStatsCard);
		pulseStreamStatsCard->style()->polish(pulseStreamStatsCard);
		pulseStreamStatsCard->update();
	}
	obs_output_release(twitchOutput);
	obs_output_release(kickOutput);
}

void OBSBasic::CyclePulseWeaverStreamStats(int direction)
{
	QStringList keys;
	const QString twitchMode = pulseTwitchDestination ? pulseTwitchDestination->currentData().toString() : "off";
	const QString youtubeMode = pulseYouTubeDestination ? pulseYouTubeDestination->currentData().toString() : "off";
	const QString kickMode = pulseKickDestination ? pulseKickDestination->currentData().toString() : "off";
	if (twitchMode != "off") keys << "twitch";
	if (youtubeMode == "horizontal" || youtubeMode == "dual") keys << "youtube-horizontal";
	if (youtubeMode == "vertical" || youtubeMode == "dual") keys << "youtube-vertical";
	if (kickMode != "off") keys << "kick";
	if (keys.isEmpty())
		return;
	const int current = keys.indexOf(pulseStreamStatsSelection);
	const int start = current < 0 ? 0 : current;
	pulseStreamStatsSelection = keys.at((start + (direction < 0 ? -1 : 1) + keys.size()) % keys.size());
	RefreshPulseWeaverStreamStats();
}

void OBSBasic::ActivatePulseWeaverStage(int index)
{
	ApplyPulseWeaverStage(index, true);
}

void OBSBasic::ApplyPulseWeaverStage(int index, bool runTransitions)
{
	if (pulseStageUpdating || !pulseStageSelector || index < 0)
		return;
	const QJsonObject stage = QJsonDocument::fromJson(pulseStageSelector->itemData(index).toString().toUtf8()).object();
	const QString legacyTransition = stage.value("transition").toString("fade");
	const int legacyDuration = std::clamp(stage.value("durationMs").toInt(500), 100, 5000);
	const QString horizontalTransitionId = stage.value("horizontalTransition").toString(legacyTransition);
	const QString verticalTransitionId = stage.value("verticalTransition").toString(legacyTransition);
	const bool horizontalStinger = horizontalTransitionId.startsWith("stinger:");
	const bool verticalStinger = verticalTransitionId.startsWith("stinger:");
	/* OBS Stingers are fixed-duration transitions. Passing the Stage's old
	 * millisecond value into their start path could race their configured A/B
	 * point; Fade alone is Stage-duration driven. */
	const int horizontalDuration = !runTransitions || horizontalTransitionId == "cut" || horizontalStinger ? 0 :
		std::clamp(stage.value("horizontalDurationMs").toInt(legacyDuration), 100, 5000);
	const int verticalDuration = !runTransitions || verticalTransitionId == "cut" || verticalStinger ? 0 :
		std::clamp(stage.value("verticalDurationMs").toInt(legacyDuration), 100, 5000);
	auto findTransition = [this](const QString &transitionId) -> obs_source_t * {
		if (transitionId.startsWith("stinger:"))
			return FindTransition(transitionId.mid(QStringLiteral("stinger:").size()).toUtf8().constData());
		const char *sourceId = transitionId == "cut" ? "cut_transition" : "fade_transition";
		return FindTransition(obs_source_get_display_name(sourceId));
	};
	obs_source_t *horizontalTransition = runTransitions ? findTransition(horizontalTransitionId) : nullptr;
	obs_source_t *verticalTransition = runTransitions ? findTransition(verticalTransitionId) : nullptr;
	const quint64 transitionSerial = ++pulseStageTransitionSerial;
	QString horizontalName = stage.value("horizontal").toString();
	QString verticalName = stage.value("vertical").toString();
	setProperty("pulseWeaverTwitchHorizontalCanvasName", QVariant());
	setProperty("pulseWeaverTwitchVerticalCanvasName", QVariant());
	QHash<QString, QJsonObject> routed;
	bool outputTransitionStarted = false;
	auto modeFor = [this](const QString &provider) {
		if (provider == "twitch") return pulseTwitchDestination ? pulseTwitchDestination->currentData().toString() : QString("off");
		if (provider == "youtube") return pulseYouTubeDestination ? pulseYouTubeDestination->currentData().toString() : QString("off");
		if (provider == "kick") return pulseKickDestination ? pulseKickDestination->currentData().toString() : QString("off");
		return pulseRecordingDestination ? pulseRecordingDestination->currentData().toString() : QString("standard");
	};
	const auto providerOutputActive = [this](const QString &provider) {
		if (provider == "twitch")
			return obs_frontend_streaming_active();
		if (provider == "youtube")
			return (pulseYouTubeOutput && obs_output_active(pulseYouTubeOutput)) ||
			       (pulseYouTubeSecondOutput && obs_output_active(pulseYouTubeSecondOutput));
		if (provider == "kick")
			return property("pulseWeaverKickLive").toBool();
		return PulseWeaverRecordingActive();
	};
	for (const QString &provider : {QString("twitch"), QString("youtube"), QString("kick"), QString("recording")}) {
		const QString mode = modeFor(provider);
		for (const QString &canvas : {QString("horizontal"), QString("vertical")}) {
			if (mode != canvas && mode != "dual") {
				if (!providerOutputActive(provider))
					pulseReleaseOutputCanvas(provider + ":" + canvas);
				continue;
			}
			const QJsonObject assignment = pulseStageAssignment(stage, provider, canvas);
			if (assignment.isEmpty())
				continue;
			const QString key = provider + ":" + canvas;
			const QString nativeScene = canvas == "vertical" ? verticalName : horizontalName;
			/* Most Stages deliberately align every destination.  In that common
			 * case each RTMP encoder can read the already-rendered native programme
			 * canvas.  Do not build another active canvas merely to show the same
			 * scene.  A private canvas is retained whenever a destination differs,
			 * excludes a source, or is already live and bound to one. */
			const bool usesNativeProgramme = assignment.value("excluded").toArray().isEmpty() &&
				!nativeScene.isEmpty() && assignment.value("scene").toString() == nativeScene;
			const bool providerActive = providerOutputActive(provider);
			if (usesNativeProgramme && !(providerActive && pulseOutputCanvases.contains(key))) {
				if (!providerActive)
					pulseReleaseOutputCanvas(key);
				continue;
			}
			routed.insert(provider + "_" + canvas, assignment);
			bool started = false;
			const int routeDuration = canvas == "vertical" ? verticalDuration : horizontalDuration;
			const QString transitionId = canvas == "vertical" ? verticalTransitionId : horizontalTransitionId;
			obs_source_t *routeTransition = !runTransitions || transitionId == "cut" ? nullptr :
				(canvas == "vertical" ? verticalTransition : horizontalTransition);
			obs_canvas_t *configured = pulseConfigureOutputCanvas(provider, assignment, routeDuration, &started, routeTransition);
			outputTransitionStarted = outputTransitionStarted || started;
			if (provider == "twitch" && configured)
				setProperty(canvas == "vertical" ? "pulseWeaverTwitchVerticalCanvasName" :
					"pulseWeaverTwitchHorizontalCanvasName", QString::fromUtf8(obs_canvas_get_name(configured)));
		}
	}
	ConfigurePulseWeaverAudioRouting(stage);
	auto firstScene = [&routed](const QString &canvas) {
		for (const QString &provider : {QString("twitch"), QString("youtube"), QString("kick"), QString("recording")}) {
			const QJsonObject assignment = routed.value(provider + "_" + canvas);
			if (!assignment.isEmpty())
				return assignment.value("scene").toString();
		}
		return QString();
	};
	if (horizontalName.isEmpty())
		horizontalName = firstScene("horizontal");
	if (verticalName.isEmpty())
		verticalName = firstScene("vertical");
	if (pulseStageTransitionSelector) {
		QSignalBlocker blocker(pulseStageTransitionSelector);
		const int transitionIndex = pulseStageTransitionSelector->findData(horizontalTransitionId);
		pulseStageTransitionSelector->setCurrentIndex(transitionIndex >= 0 ? transitionIndex : 0);
	}
	if (pulseStageTransitionDuration) {
		QSignalBlocker blocker(pulseStageTransitionDuration);
		pulseStageTransitionDuration->setValue(horizontalDuration > 0 ? horizontalDuration :
			stage.value("horizontalDurationMs").toInt(legacyDuration));
		pulseStageTransitionDuration->setEnabled(horizontalTransitionId != "cut");
	}
	if (pulseVerticalStageTransitionSelector) {
		QSignalBlocker blocker(pulseVerticalStageTransitionSelector);
		const int transitionIndex = pulseVerticalStageTransitionSelector->findData(verticalTransitionId);
		pulseVerticalStageTransitionSelector->setCurrentIndex(transitionIndex >= 0 ? transitionIndex : 0);
	}
	if (pulseVerticalStageTransitionDuration) {
		QSignalBlocker blocker(pulseVerticalStageTransitionDuration);
		pulseVerticalStageTransitionDuration->setValue(verticalDuration > 0 ? verticalDuration :
			stage.value("verticalDurationMs").toInt(legacyDuration));
		pulseVerticalStageTransitionDuration->setEnabled(verticalTransitionId != "cut");
	}
	if (runTransitions && horizontalTransition)
		SetTransition(OBSSource(horizontalTransition));
	if (horizontalDuration > 0)
		SetTransitionDuration(horizontalDuration);
	if (!horizontalName.isEmpty()) {
		obs_source_t *scene = obs_get_source_by_name(horizontalName.toUtf8().constData());
		if (scene) {
			/* In normal mode SetCurrentScene both runs the transition and moves
			 * OBS's selected/current scene. Calling TransitionToScene directly
			 * left Show Control rendering the old horizontal scene. Studio mode
			 * intentionally changes Program without replacing Preview. */
			if (IsPreviewProgramMode())
				TransitionToScene(OBSSource(scene), !runTransitions, true, horizontalDuration);
			else
				SetCurrentScene(OBSSource(scene), !runTransitions);
		}
		obs_source_release(scene);
	}
	bool verticalTransitionStarted = false;
	if (!verticalName.isEmpty()) {
		obs_canvas_t *canvas = PulseWeaverGetVerticalCanvas();
		obs_source_t *scene = canvas ? obs_canvas_get_source_by_name(canvas, verticalName.toUtf8().constData()) : nullptr;
		obs_source_t *current = canvas ? obs_canvas_get_channel(canvas, 0) : nullptr;
		obs_source_t *cachedStinger = runTransitions ? pulseCachedCanvasStinger("vertical-program", verticalTransition) : nullptr;
		if (runTransitions && canvas && scene && verticalTransitionId != "cut" && current && current != scene) {
			OBSSourceAutoRelease ownedTransition = cachedStinger ? nullptr : verticalTransition ?
				obs_source_duplicate(verticalTransition, "Pulse Weaver Vertical Stage Transition", true) :
				obs_source_create_private("fade_transition", "Pulse Weaver Stage Fade", nullptr);
			obs_source_t *transition = cachedStinger ? cachedStinger : ownedTransition.Get();
			if (transition) {
				obs_transition_set_size(transition, 1080, 1920);
				if (current != transition)
					obs_transition_set(transition, current);
				verticalTransitionStarted = obs_transition_start(transition, OBS_TRANSITION_MODE_AUTO, verticalDuration, scene);
				if (verticalTransitionStarted)
					obs_canvas_set_channel(canvas, 0, transition);
			}
		}
		if (canvas && scene && !verticalTransitionStarted)
			obs_canvas_set_channel(canvas, 0, scene);
		obs_source_release(current);
		obs_source_release(scene);
		obs_canvas_release(canvas);
	}
	if (!verticalTransitionStarted && pulseVerticalEditor)
		pulseVerticalEditor->Refresh();
	if (verticalTransitionStarted || outputTransitionStarted)
		QTimer::singleShot(50, this, [this, transitionSerial,
			verticalScene = verticalTransitionStarted ? verticalName : QString(),
			pendingRoutes = outputTransitionStarted ? routed : QHash<QString, QJsonObject>{}] {
			FinalizePulseWeaverStageTransitions(transitionSerial, verticalScene, pendingRoutes);
		});
	RefreshPulseWeaverPlatformPreviews();
}

void OBSBasic::FinalizePulseWeaverStageTransitions(quint64 serial, const QString &verticalScene,
						   const QHash<QString, QJsonObject> &routed)
{
	if (serial != pulseStageTransitionSerial)
		return;
	bool active = false;
	if (!verticalScene.isEmpty()) {
		obs_canvas_t *canvas = PulseWeaverGetVerticalCanvas();
		obs_source_t *current = canvas ? obs_canvas_get_channel(canvas, 0) : nullptr;
		active = current && obs_transition_is_active(current);
		obs_source_release(current);
		obs_canvas_release(canvas);
	}
	for (auto it = routed.cbegin(); it != routed.cend(); ++it) {
		const QString provider = it.key().section('_', 0, 0);
		const QString route = it.value().value("canvas").toString();
		obs_canvas_t *canvas = pulseOutputCanvases.value(provider + ':' + route, nullptr);
		obs_source_t *current = canvas ? obs_canvas_get_channel(canvas, 0) : nullptr;
		active = active || (current && obs_transition_is_active(current));
		obs_source_release(current);
	}
	if (active) {
		QTimer::singleShot(50, this, [this, serial, verticalScene, routed] {
			FinalizePulseWeaverStageTransitions(serial, verticalScene, routed);
		});
		return;
	}
	if (!verticalScene.isEmpty()) {
		obs_canvas_t *canvas = PulseWeaverGetVerticalCanvas();
		obs_source_t *scene = canvas ? obs_canvas_get_source_by_name(canvas, verticalScene.toUtf8().constData()) : nullptr;
		if (canvas && scene)
			obs_canvas_set_channel(canvas, 0, scene);
		obs_source_release(scene);
		obs_canvas_release(canvas);
		if (pulseVerticalEditor)
			pulseVerticalEditor->Refresh();
	}
	for (auto it = routed.cbegin(); it != routed.cend(); ++it) {
		const QString provider = it.key().section('_', 0, 0);
		pulseConfigureOutputCanvas(provider, it.value());
	}
	RefreshPulseWeaverPlatformPreviews();
}

void OBSBasic::RestorePulseWeaverAudioRouting()
{
	if (!pulseOwnedAudioMixerMask)
		return;
	obs_enum_sources([](void *, obs_source_t *source) {
		const QString id = QString::fromUtf8(obs_source_get_uuid(source));
		const auto it = pulseAudioMixerBaselines.constFind(id);
		if (it != pulseAudioMixerBaselines.cend()) {
			const uint32_t current = obs_source_get_audio_mixers(source);
			obs_source_set_audio_mixers(source, (current & ~pulseOwnedAudioMixerMask) | (*it & pulseOwnedAudioMixerMask));
		}
		return true;
	}, nullptr);
	pulseOwnedAudioMixerMask = 0;
	pulseAudioMixerBaselines.clear();
}

void OBSBasic::ConfigurePulseWeaverAudioRouting(const QJsonObject &stage)
{
	const char *modeText = Config() ? config_get_string(Config(), "Output", "Mode") : nullptr;
	const bool advanced = modeText && strcmp(modeText, "Advanced") == 0;
	const int streamMix = std::clamp(advanced ? int(config_get_int(Config(), "AdvOut", "TrackIndex")) - 1 : 0, 0, 5);
	uint32_t protectedMask = advanced ? uint32_t(config_get_int(Config(), "AdvOut", "RecTracks")) :
		uint32_t(config_get_int(Config(), "SimpleOutput", "RecTracks"));
	protectedMask |= 1u << streamMix;
	const bool vodEnabled = Config() && config_get_bool(Config(), advanced ? "AdvOut" : "SimpleOutput", "VodTrackEnabled");
	if (vodEnabled) {
		const int vodMix = advanced ? std::clamp(int(config_get_int(Config(), "AdvOut", "VodTrackIndex")) - 1, 0, 5) : 1;
		protectedMask |= 1u << vodMix;
	}
	auto modeFor = [this](const QString &provider) {
		if (provider == "twitch") return pulseTwitchDestination ? pulseTwitchDestination->currentData().toString() : QString("off");
		if (provider == "youtube") return pulseYouTubeDestination ? pulseYouTubeDestination->currentData().toString() : QString("off");
		if (provider == "kick") return pulseKickDestination ? pulseKickDestination->currentData().toString() : QString("off");
		return pulseRecordingDestination ? pulseRecordingDestination->currentData().toString() : QString("standard");
	};
	auto exclusions = [&stage](const QString &provider, const QString &route) {
		QSet<QString> result;
		for (const QJsonValue &value : pulseStageAssignment(stage, provider, route).value("excluded").toArray())
			result.insert(value.toString());
		return result;
	};
	auto signature = [](const QSet<QString> &names) {
		QStringList sorted = names.values();
		sorted.sort(Qt::CaseInsensitive);
		return sorted.join(QChar(0x1f));
	};
	QSet<QString> twitchExcluded;
	const QString twitchMode = modeFor("twitch");
	if (twitchMode == "horizontal" || twitchMode == "dual") twitchExcluded.unite(exclusions("twitch", "horizontal"));
	if (twitchMode == "vertical" || twitchMode == "dual") twitchExcluded.unite(exclusions("twitch", "vertical"));
	const QString twitchSignature = signature(twitchExcluded);
	struct Route { QString property; QSet<QString> excluded; };
	QVector<Route> routes;
	auto addRoutes = [&](const QString &provider, const QString &mode, const QString &prefix) {
		if (mode == "horizontal" || mode == "dual") routes.push_back({prefix + "HorizontalAudioMix", exclusions(provider, "horizontal")});
		if (mode == "vertical" || mode == "dual") routes.push_back({prefix + "VerticalAudioMix", exclusions(provider, "vertical")});
	};
	const QString kickMode = modeFor("kick");
	if (kickMode == "horizontal" || kickMode == "vertical")
		routes.push_back({"pulseWeaverKickAudioMix", exclusions("kick", kickMode)});
	addRoutes("youtube", modeFor("youtube"), "pulseWeaverYouTube");
	addRoutes("recording", modeFor("recording"), "pulseWeaverRecording");
	QHash<QString, int> allocated;
	QHash<int, QSet<QString>> mixExclusions;
	QStringList warnings;
	uint32_t newOwnedMask = twitchExcluded.isEmpty() ? 0u : (1u << streamMix);
	if (!twitchExcluded.isEmpty()) mixExclusions.insert(streamMix, twitchExcluded);
	for (const Route &route : routes) {
		const QString routeSignature = signature(route.excluded);
		int mix = streamMix;
		if (routeSignature != twitchSignature) {
			if (allocated.contains(routeSignature)) {
				mix = allocated.value(routeSignature);
			} else {
				mix = -1;
				for (int candidate = 5; candidate >= 0; --candidate) {
					if (!(protectedMask & (1u << candidate)) && !(newOwnedMask & (1u << candidate))) {
						mix = candidate;
						break;
					}
				}
				if (mix >= 0) {
					allocated.insert(routeSignature, mix);
					mixExclusions.insert(mix, route.excluded);
					newOwnedMask |= 1u << mix;
				} else {
					mix = streamMix;
					warnings << route.property;
				}
			}
		}
		setProperty(route.property.toUtf8().constData(), mix);
	}
	struct ApplyData { uint32_t oldMask; int reference; QHash<int, QSet<QString>> routes; };
	ApplyData apply{pulseOwnedAudioMixerMask, streamMix, mixExclusions};
	obs_enum_sources([](void *opaque, obs_source_t *source) {
		auto *data = static_cast<ApplyData *>(opaque);
		const QString id = QString::fromUtf8(obs_source_get_uuid(source));
		const uint32_t current = obs_source_get_audio_mixers(source);
		if (!pulseAudioMixerBaselines.contains(id)) pulseAudioMixerBaselines.insert(id, current);
		uint32_t &baseline = pulseAudioMixerBaselines[id];
		baseline = (baseline & data->oldMask) | (current & ~data->oldMask);
		uint32_t mixers = (current & ~data->oldMask) | (baseline & data->oldMask);
		const QString name = QString::fromUtf8(obs_source_get_name(source));
		for (auto it = data->routes.cbegin(); it != data->routes.cend(); ++it) {
			const uint32_t bit = 1u << it.key();
			const bool included = (baseline & (1u << data->reference)) && !it.value().contains(name);
			mixers = included ? (mixers | bit) : (mixers & ~bit);
		}
		obs_source_set_audio_mixers(source, mixers);
		return true;
	}, &apply);
	pulseOwnedAudioMixerMask = newOwnedMask;
	setProperty("pulseWeaverAudioRoutingWarning", warnings.isEmpty() ? QVariant() :
		QVariant("Not enough unused OBS audio tracks for every distinct exclusion mix."));
}

void OBSBasic::CapturePulseWeaverStage()
{
	bool accepted = false;
	const QString name = QInputDialog::getText(this, "Capture current Stage", "Stage name", QLineEdit::Normal,
		"New Stage", &accepted).trimmed();
	if (!accepted || name.isEmpty())
		return;
	obs_source_t *horizontal = obs_frontend_get_current_scene();
	obs_canvas_t *canvas = PulseWeaverGetVerticalCanvas();
	obs_source_t *vertical = canvas ? obs_canvas_get_channel(canvas, 0) : nullptr;
	QJsonArray stages = loadPulseWeaverStages();
	const QString horizontalName = horizontal ? QString::fromUtf8(obs_source_get_name(horizontal)) : QString();
	const QString verticalName = vertical ? QString::fromUtf8(obs_source_get_name(vertical)) : QString();
	const QString twitchMode = pulseTwitchDestination ? pulseTwitchDestination->currentData().toString() : QString("off");
	const QString youtubeMode = pulseYouTubeDestination ? pulseYouTubeDestination->currentData().toString() : QString("off");
	const QString kickMode = pulseKickDestination ? pulseKickDestination->currentData().toString() : QString("off");
	const QString recordingMode = pulseRecordingDestination ? pulseRecordingDestination->currentData().toString() : QString("standard");
	QJsonObject assignments;
	auto addAssignments = [&assignments, &horizontalName, &verticalName](const QString &provider, const QString &mode) {
		if ((mode == "horizontal" || mode == "dual") && !horizontalName.isEmpty())
			assignments.insert(provider + "_horizontal", QJsonObject{{"canvas", "horizontal"}, {"scene", horizontalName}, {"excluded", QJsonArray{}}});
		if ((mode == "vertical" || mode == "dual") && !verticalName.isEmpty())
			assignments.insert(provider + "_vertical", QJsonObject{{"canvas", "vertical"}, {"scene", verticalName}, {"excluded", QJsonArray{}}});
	};
	addAssignments("twitch", twitchMode);
	addAssignments("youtube", youtubeMode);
	addAssignments("kick", kickMode);
	addAssignments("recording", recordingMode);
	stages.append(QJsonObject{{"name", name},
		{"horizontal", horizontalName}, {"vertical", verticalName},
		{"assignments", assignments},
		{"horizontalTransition", pulseStageTransitionSelector ? pulseStageTransitionSelector->currentData().toString() : QString("fade")},
		{"horizontalDurationMs", pulseStageTransitionDuration ? pulseStageTransitionDuration->value() : 500},
		{"verticalTransition", pulseVerticalStageTransitionSelector ? pulseVerticalStageTransitionSelector->currentData().toString() : QString("fade")},
		{"verticalDurationMs", pulseVerticalStageTransitionDuration ? pulseVerticalStageTransitionDuration->value() : 500}});
	obs_source_release(horizontal);
	obs_source_release(vertical);
	obs_canvas_release(canvas);
	savePulseWeaverStages(stages);
	RefreshPulseWeaverStages();
	const int index = pulseStageSelector ? pulseStageSelector->findData(name, Qt::UserRole + 1) : -1;
	if (index >= 0)
		pulseStageSelector->setCurrentIndex(index);
}

void OBSBasic::ManagePulseWeaverStages()
{
	QDialog dialog(this);
	dialog.setWindowTitle("Pulse Weaver — Manage Stages");
	dialog.resize(1560, 760);
	auto *layout = new QVBoxLayout(&dialog);
	auto *copy = new QLabel("Each Stage assigns an exact scene to each platform output. Leave an output unchanged when this cue must not touch it; Exclude Sources creates a non-destructive platform-specific programme copy.");
	copy->setWordWrap(true);
	copy->setObjectName("PulseWeaverMuted");
	layout->addWidget(copy);
	auto *table = new QTableWidget;
	table->setColumnCount(9);
	table->setHorizontalHeaderLabels({"Stage", "TWITCH PROGRAMME", "YOUTUBE PROGRAMME", "KICK PROGRAMME",
		"RECORDING · UNSTABLE", "16:9 TRANSITION", "16:9 DURATION", "9:16 TRANSITION", "9:16 DURATION"});
	table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
	table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
	table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
	/* All three programme columns need the same usable width.  Letting Kick
	 * shrink to its contents made its 16:9 control drift and misalign with the
	 * Twitch/YouTube cells at normal window widths. */
	table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
	table->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);
	table->horizontalHeader()->setSectionResizeMode(5, QHeaderView::ResizeToContents);
	table->horizontalHeader()->setSectionResizeMode(6, QHeaderView::ResizeToContents);
	table->horizontalHeader()->setSectionResizeMode(7, QHeaderView::ResizeToContents);
	table->horizontalHeader()->setSectionResizeMode(8, QHeaderView::ResizeToContents);
	table->setColumnWidth(5, 108);
	table->setColumnWidth(6, 112);
	table->setColumnWidth(7, 108);
	table->setColumnWidth(8, 112);
	table->verticalHeader()->setVisible(false);
	table->verticalHeader()->setDefaultSectionSize(132);
	layout->addWidget(table, 1);

	QStringList horizontalNames;
	obs_enum_scenes([](void *opaque, obs_source_t *scene) {
		static_cast<QStringList *>(opaque)->append(QString::fromUtf8(obs_source_get_name(scene)));
		return true;
	}, &horizontalNames);
	QStringList verticalNames;
	obs_canvas_t *canvas = PulseWeaverGetVerticalCanvas();
	if (canvas)
		obs_canvas_enum_scenes(canvas, [](void *opaque, obs_source_t *scene) {
			static_cast<QStringList *>(opaque)->append(QString::fromUtf8(obs_source_get_name(scene)));
			return true;
		}, &verticalNames);
	obs_canvas_release(canvas);
	QList<QPair<QString, QString>> transitionOptions{{"Fade", "fade"}, {"Cut", "cut"}};
	QMap<QString, QString> stingerOptions;
	for (const auto &[uuid, transition] : transitions) {
		obs_source_t *source = transition;
		if (source && strcmp(obs_source_get_id(source), "obs_stinger_transition") == 0) {
			const QString name = QString::fromUtf8(obs_source_get_name(source));
			if (!name.isEmpty())
				stingerOptions.insert(name, "stinger:" + name);
		}
	}
	for (auto it = stingerOptions.cbegin(); it != stingerOptions.cend(); ++it)
		transitionOptions.append({"Stinger · " + it.key(), it.value()});

	auto assignmentFromCell = [](QWidget *cell, const QString &canvas) {
		QJsonObject assignment;
		if (!cell)
			return assignment;
		const QString suffix = canvas == "vertical" ? "Vertical" : "Horizontal";
		auto *combo = cell->findChild<QComboBox *>("StageOutput" + suffix);
		auto *exclude = cell->findChild<QPushButton *>("StageOutputExclude" + suffix);
		const QString scene = combo ? combo->currentData().toString() : QString();
		if (!scene.isEmpty()) {
			assignment.insert("canvas", canvas);
			assignment.insert("scene", scene);
			QJsonArray excluded;
			for (const QString &name : exclude ? exclude->property("excluded").toStringList() : QStringList{})
				excluded.append(name);
			assignment.insert("excluded", excluded);
		}
		return assignment;
	};
	auto saveTable = [table, assignmentFromCell] {
		QJsonArray stages;
		for (int row = 0; row < table->rowCount(); ++row) {
			auto *nameItem = table->item(row, 0);
			auto *horizontalTransition = qobject_cast<QComboBox *>(table->cellWidget(row, 5));
			auto *horizontalDuration = qobject_cast<QSpinBox *>(table->cellWidget(row, 6));
			auto *verticalTransition = qobject_cast<QComboBox *>(table->cellWidget(row, 7));
			auto *verticalDuration = qobject_cast<QSpinBox *>(table->cellWidget(row, 8));
			const QString name = nameItem ? nameItem->text().trimmed() : QString();
			if (!name.isEmpty()) {
				QJsonObject assignments;
				QString horizontal;
				QString vertical;
				for (int column = 1; column <= 4; ++column) {
					const QString provider = column == 1 ? "twitch" : column == 2 ? "youtube" :
						column == 3 ? "kick" : "recording";
					for (const QString &canvas : {QString("horizontal"), QString("vertical")}) {
						const QJsonObject assignment = assignmentFromCell(table->cellWidget(row, column), canvas);
						if (!assignment.isEmpty()) {
							assignments.insert(provider + "_" + canvas, assignment);
							if (canvas == "horizontal" && horizontal.isEmpty()) horizontal = assignment.value("scene").toString();
							if (canvas == "vertical" && vertical.isEmpty()) vertical = assignment.value("scene").toString();
						}
					}
				}
				stages.append(QJsonObject{{"name", name}, {"horizontal", horizontal}, {"vertical", vertical}, {"assignments", assignments},
					{"horizontalTransition", horizontalTransition ? horizontalTransition->currentData().toString() : QString("fade")},
					{"horizontalDurationMs", horizontalDuration ? horizontalDuration->value() : 500},
					{"verticalTransition", verticalTransition ? verticalTransition->currentData().toString() : QString("fade")},
					{"verticalDurationMs", verticalDuration ? verticalDuration->value() : 500}});
			}
		}
		savePulseWeaverStages(stages);
	};
	auto addRow = [table, horizontalNames, verticalNames, transitionOptions, saveTable](const QJsonObject &stage) {
		const int row = table->rowCount();
		table->insertRow(row);
		table->setItem(row, 0, new QTableWidgetItem(stage.value("name").toString("New Stage")));
		auto makeOutput = [table, horizontalNames, verticalNames, saveTable](const QJsonObject &stage, const QString &provider) {
			auto *cell = new QWidget(table);
			auto *cellLayout = new QVBoxLayout(cell);
			cellLayout->setContentsMargins(2, 2, 2, 2);
			cellLayout->setSpacing(2);
			auto addRoute = [cell, cellLayout, table, saveTable](const QString &canvas, const QStringList &sceneNames,
				const QJsonObject &assignment) {
				const QString suffix = canvas == "vertical" ? "Vertical" : "Horizontal";
				auto *row = new QHBoxLayout;
				auto *combo = new QComboBox(cell);
				combo->setObjectName("StageOutput" + suffix);
				combo->setAccessibleName((canvas == "vertical" ? "9:16" : "16:9") + QString(" scene"));
				combo->addItem((canvas == "vertical" ? "9:16" : "16:9") + QString(" · Leave unchanged"), QString());
				for (const QString &scene : sceneNames)
					combo->addItem((canvas == "vertical" ? "9:16 · " : "16:9 · ") + scene, scene);
				const int selected = combo->findData(assignment.value("scene").toString());
				combo->setCurrentIndex(selected >= 0 ? selected : 0);
				auto *exclude = new QPushButton("EXCLUDE", cell);
				exclude->setObjectName("StageOutputExclude" + suffix);
				exclude->setMaximumWidth(84);
				QStringList excluded;
				for (const QJsonValue &value : assignment.value("excluded").toArray()) excluded << value.toString();
				exclude->setProperty("excluded", excluded);
				auto refreshExclude = [exclude] {
					const int count = exclude->property("excluded").toStringList().size();
					exclude->setText(count ? QString("EXCLUDE %1").arg(count) : "EXCLUDE");
				};
				refreshExclude();
				QObject::connect(combo, &QComboBox::currentIndexChanged, table, [saveTable](int) { saveTable(); });
				QObject::connect(exclude, &QPushButton::clicked, table, [table, combo, exclude, refreshExclude, saveTable, canvas] {
				const QString sceneName = combo->currentData().toString();
				if (sceneName.isEmpty()) {
					QMessageBox::information(table, "Choose a programme", "Choose a scene for this output before excluding sources.");
					return;
				}
				obs_source_t *sceneSource = nullptr;
				if (canvas == "vertical") {
					obs_canvas_t *canvas = PulseWeaverGetVerticalCanvas();
					sceneSource = canvas ? obs_canvas_get_source_by_name(canvas, sceneName.toUtf8().constData()) : nullptr;
					obs_canvas_release(canvas);
				} else {
					sceneSource = obs_get_source_by_name(sceneName.toUtf8().constData());
				}
				obs_scene_t *scene = sceneSource ? obs_scene_from_source(sceneSource) : nullptr;
				const auto sources = PulseStageExclusionSources(scene);
				obs_source_release(sceneSource);
				QDialog picker(table);
				picker.setWindowTitle("Exclude sources from this output");
				picker.resize(440, 480);
				auto *pickerLayout = new QVBoxLayout(&picker);
				auto *explanation = new QLabel("Checked sources are excluded from this output. Sources marked (audio) also include audio inputs from other scenes and global devices.");
				explanation->setWordWrap(true);
				pickerLayout->addWidget(explanation);
				auto *list = new QListWidget(&picker);
				const QStringList currentExcluded = exclude->property("excluded").toStringList();
				const QSet<QString> selectedNames(currentExcluded.cbegin(), currentExcluded.cend());
				for (auto source = sources.cbegin(); source != sources.cend(); ++source) {
					const QString &name = source.key();
					auto *item = new QListWidgetItem(name + (source.value() ? " (audio)" : ""), list);
					item->setData(Qt::UserRole, name);
					item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
					item->setCheckState(selectedNames.contains(name) ? Qt::Checked : Qt::Unchecked);
				}
				pickerLayout->addWidget(list, 1);
				auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &picker);
				QObject::connect(buttons, &QDialogButtonBox::accepted, &picker, &QDialog::accept);
				QObject::connect(buttons, &QDialogButtonBox::rejected, &picker, &QDialog::reject);
				pickerLayout->addWidget(buttons);
				if (picker.exec() == QDialog::Accepted) {
					QStringList result;
					for (int i = 0; i < list->count(); ++i)
						if (list->item(i)->checkState() == Qt::Checked)
							result << list->item(i)->data(Qt::UserRole).toString();
					exclude->setProperty("excluded", result);
					refreshExclude();
					saveTable();
				}
				});
				row->addWidget(combo, 1);
				row->addWidget(exclude);
				cellLayout->addLayout(row);
			};
			addRoute("horizontal", horizontalNames, pulseStageAssignment(stage, provider, "horizontal"));
			if (provider != "kick") {
				addRoute("vertical", verticalNames, pulseStageAssignment(stage, provider, "vertical"));
			} else {
				/* Use the same two-control row as every other programme cell.
				 * The old bare label did not share the combo/exclude geometry,
				 * which made Kick look vertically and horizontally offset. */
				auto *unsupportedRow = new QHBoxLayout;
				auto *unsupported = new QComboBox(cell);
				unsupported->addItem("9:16 · Not supported by Kick");
				unsupported->setEnabled(false);
				unsupported->setAccessibleName("Kick 9:16 is not supported");
				auto *capability = new QPushButton("UNAVAILABLE", cell);
				capability->setEnabled(false);
				capability->setMaximumWidth(84);
				unsupportedRow->addWidget(unsupported, 1);
				unsupportedRow->addWidget(capability);
				cellLayout->addLayout(unsupportedRow);
			}
			return cell;
		};
		table->setCellWidget(row, 1, makeOutput(stage, "twitch"));
		table->setCellWidget(row, 2, makeOutput(stage, "youtube"));
		table->setCellWidget(row, 3, makeOutput(stage, "kick"));
		table->setCellWidget(row, 4, makeOutput(stage, "recording"));
		const QString legacyTransition = stage.value("transition").toString("fade");
		const int legacyDuration = std::clamp(stage.value("durationMs").toInt(500), 100, 5000);
		auto addTransitionControls = [table, row, &stage, &transitionOptions, saveTable, &legacyTransition,
			legacyDuration](int transitionColumn, int durationColumn, const QString &transitionKey,
			const QString &durationKey) {
			auto *transition = new QComboBox(table);
			for (const auto &option : transitionOptions)
				transition->addItem(option.first, option.second);
			const QString selectedTransition = stage.value(transitionKey).toString(legacyTransition);
			const int transitionIndex = transition->findData(selectedTransition);
			transition->setCurrentIndex(transitionIndex >= 0 ? transitionIndex : 0);
			table->setCellWidget(row, transitionColumn, transition);
			auto *duration = new QSpinBox(table);
			duration->setRange(100, 5000);
			duration->setSingleStep(50);
			duration->setSuffix(" ms");
			duration->setValue(std::clamp(stage.value(durationKey).toInt(legacyDuration), 100, 5000));
			auto updateDuration = [transition, duration] {
				/* A Stinger owns both its duration and its A/B transition point in
				 * its OBS properties. The Stage duration must not imply that it can
				 * override either setting. Cut also has no duration. */
				const QString id = transition->currentData().toString();
				const bool stageDurationApplies = id == "fade";
				duration->setVisible(stageDurationApplies);
				duration->setEnabled(stageDurationApplies);
			};
			table->setCellWidget(row, durationColumn, duration);
			updateDuration();
			QObject::connect(transition, &QComboBox::currentIndexChanged, table, [updateDuration, saveTable](int) {
				updateDuration();
				saveTable();
			});
			QObject::connect(duration, &QSpinBox::valueChanged, table, [saveTable](int) { saveTable(); });
		};
		addTransitionControls(5, 6, "horizontalTransition", "horizontalDurationMs");
		addTransitionControls(7, 8, "verticalTransition", "verticalDurationMs");
	};
	for (const QJsonValue &value : loadPulseWeaverStages())
		addRow(value.toObject());
	connect(table, &QTableWidget::itemChanged, &dialog, [saveTable](QTableWidgetItem *) { saveTable(); });

	auto *tools = new QHBoxLayout;
	auto *add = new QPushButton("New stage");
    pulseIcon(add, "add");
	auto *duplicate = new QPushButton("Duplicate");
    pulseIcon(duplicate, "duplicate");
	auto *remove = new QPushButton("Delete");
    pulseIcon(remove, "delete");
	tools->addWidget(add);
	tools->addWidget(duplicate);
	tools->addWidget(remove);
	tools->addStretch();
	auto *close = new QPushButton("Done");
    pulseIcon(close, "done");
	close->setObjectName("PulseWeaverControl");
	tools->addWidget(close);
	layout->addLayout(tools);
	connect(add, &QPushButton::clicked, &dialog, [addRow, saveTable] {
		addRow(QJsonObject{{"name", "New Stage"}});
		saveTable();
	});
	connect(duplicate, &QPushButton::clicked, &dialog, [table, addRow, saveTable] {
		const int row = table->currentRow();
		if (row < 0)
			return;
		saveTable();
		const QJsonArray stages = loadPulseWeaverStages();
		if (row >= stages.size())
			return;
		QJsonObject copy = stages[row].toObject();
		copy["name"] = copy.value("name").toString("Stage") + " Copy";
		addRow(copy);
		saveTable();
	});
	connect(remove, &QPushButton::clicked, &dialog, [table, saveTable] {
		if (table->currentRow() >= 0) {
			table->removeRow(table->currentRow());
			saveTable();
		}
	});
	connect(close, &QPushButton::clicked, &dialog, &QDialog::accept);
	dialog.exec();
	RefreshPulseWeaverStages();
	if (pulseStageSelector && pulseStageSelector->currentIndex() >= 0)
		ApplyPulseWeaverStage(pulseStageSelector->currentIndex(), false);
}

void OBSBasic::EnsurePulseWeaverVerticalCanvas()
{
	obs_video_info info = {};
	if (!obs_get_video_info(&info)) {
		if (pulseVerticalStatus)
			pulseVerticalStatus->setText("Finish the first-run video setup before creating the vertical canvas.");
		return;
	}
	info.base_width = info.output_width = 1080;
	info.base_height = info.output_height = 1920;

	obs_canvas_t *canvas = PulseWeaverGetVerticalCanvas();
	bool createdCanvas = false;
	if (canvas) {
		if (!obs_canvas_has_video(canvas))
			obs_canvas_reset_video(canvas, &info);
	} else {
		canvas = obs_frontend_add_canvas("Pulse Weaver Vertical", &info, PROGRAM);
		createdCanvas = canvas != nullptr;
	}
	obs_source_t *firstImported = nullptr;
	if (canvas) {
		obs_canvas_t *aitum = nullptr;
		obs_enum_canvases(
			[](void *opaque, obs_canvas_t *candidate) {
				auto **result = static_cast<obs_canvas_t **>(opaque);
				const QString name = QString::fromUtf8(obs_canvas_get_name(candidate));
				if (name.contains("aitum", Qt::CaseInsensitive) && name.contains("vertical", Qt::CaseInsensitive)) {
					*result = obs_canvas_get_ref(candidate);
					return false;
				}
				return true;
			},
			&aitum);
		if (aitum) {
			struct Migration {
				obs_canvas_t *target = nullptr;
				obs_source_t *first = nullptr;
			} migration{canvas};
			obs_canvas_enum_scenes(
				aitum,
				[](void *opaque, obs_source_t *scene) {
					auto *migration = static_cast<Migration *>(opaque);
					obs_source_t *existing = obs_canvas_get_source_by_name(
						migration->target, obs_source_get_name(scene));
					if (existing) {
						obs_source_release(existing);
						return true;
					}
					obs_scene_t *copy = PulseWeaverDuplicateToVertical(
						scene, obs_source_get_name(scene), false, false);
					if (copy && !migration->first)
						migration->first = obs_source_get_ref(obs_scene_get_source(copy));
					obs_scene_release(copy);
					return true;
				},
				&migration);
			firstImported = migration.first;
			obs_canvas_release(aitum);
		}
	}
	if (canvas && createdCanvas) {
		if (!firstImported) {
			obs_source_t *horizontal = obs_frontend_get_current_scene();
			if (horizontal) {
				const QString name = QString::fromUtf8(obs_source_get_name(horizontal)) + " — Vertical";
				obs_scene_t *copy = PulseWeaverDuplicateToVertical(horizontal, name.toUtf8().constData(), true, true);
				firstImported = copy ? obs_source_get_ref(obs_scene_get_source(copy)) : nullptr;
				obs_scene_release(copy);
			}
			obs_source_release(horizontal);
		}
		if (!firstImported) {
			obs_scene_t *scene = obs_canvas_scene_create(canvas, "Vertical Starting Soon");
			firstImported = scene ? obs_source_get_ref(obs_scene_get_source(scene)) : nullptr;
			obs_scene_release(scene);
		}
		if (firstImported)
			obs_canvas_set_channel(canvas, 0, firstImported);
	}
	obs_source_release(firstImported);
	obs_source_t *existingCurrent = canvas ? obs_canvas_get_channel(canvas, 0) : nullptr;
	if (canvas && !existingCurrent) {
		obs_source_t *scene = firstCanvasScene(canvas);
		if (scene)
			obs_canvas_set_channel(canvas, 0, scene);
		obs_source_release(scene);
	}
	obs_source_release(existingCurrent);
	if (canvas)
		obs_canvas_release(canvas);
	if (pulseVerticalEditor)
		pulseVerticalEditor->Refresh();
	UpdatePulseWeaverShell();
}

void OBSBasic::ConfigurePulseWeaverTwitchDualFormat()
{
	// Routing must preserve the operator's Enhanced Broadcasting bandwidth and
	// track limits. These are independent of the normal single-encoder bitrate.
	if (Active()) {
		QMessageBox::information(this, "Twitch dual format", "Stop streaming and recording before changing output routing.");
		return;
	}
	EnsurePulseWeaverVerticalCanvas();
	obs_canvas_t *canvas = PulseWeaverGetVerticalCanvas();
	if (!canvas)
		return;
	obs_service_t *service = GetService();
	obs_data_t *settings = service ? obs_service_get_settings(service) : nullptr;
	const QString serviceName = settings ? QString::fromUtf8(obs_data_get_string(settings, "service")) : QString();
	const bool twitch = serviceName.compare("Twitch", Qt::CaseInsensitive) == 0;
	obs_data_release(settings);
	if (!twitch) {
		obs_canvas_release(canvas);
		QMessageBox::information(this, "Connect Twitch first",
					 "Open Settings → Stream, choose Twitch and connect your account. Then return here and press Twitch Dual Format.");
		return;
	}
	config_set_bool(Config(), "Stream1", "EnableMultitrackVideo", true);
	config_set_string(Config(), "Stream1", "MultitrackExtraCanvas", obs_canvas_get_uuid(canvas));
	activeConfiguration.SaveSafe("tmp");
	ResetOutputs();
	obs_canvas_release(canvas);
	if (pulseDualFormatStatus)
		pulseDualFormatStatus->setText("16:9 + 9:16 READY");
	QMessageBox::information(this, "Twitch dual format ready",
				 "Enhanced Broadcasting is enabled and Pulse Weaver Vertical is routed as the additional 9:16 canvas. One Go Live action will start both formats when Twitch accepts the negotiated configuration.");
}

void OBSBasic::ConnectPulseWeaverYouTube()
{
#ifdef YOUTUBE_ENABLED
	if (obs_frontend_streaming_active()) {
		QMessageBox::information(this, "Connect YouTube", "End the current show before changing broadcast accounts.");
		return;
	}
	OAuth::DeleteCookies("YouTube - RTMPS");
	std::shared_ptr<Auth> login = YoutubeAuth::Login(this, "YouTube - RTMPS");
	auto youtube = std::dynamic_pointer_cast<YoutubeApiWrappers>(login);
	if (!youtube) {
		if (pulseDestinationStatus)
			pulseDestinationStatus->setText("YouTube sign-in was cancelled or did not complete.");
		return;
	}
	pulseYouTubeAuth = youtube;
	pulseYouTubeAuth->SavePulseWeaverAccount();
	activeConfiguration.SaveSafe("tmp");
	setProperty("pulseWeaverYouTubeReady", true);
	if (pulseYouTubeButton)
		pulseYouTubeButton->setText("YOUTUBE CONNECTED");
	RefreshPulseWeaverChatComposer();
	if (pulseDestinationStatus)
		pulseDestinationStatus->setText("YouTube connected · broadcast will be created only when you press GO LIVE.");
#else
	QMessageBox::information(this, "YouTube unavailable", "This development build was compiled without YouTube account support.");
#endif
}

QJsonObject OBSBasic::PulseWeaverLumiaDestination(const QString &provider, bool start)
{
	auto result = [](bool ok, const QString &message) {
		return QJsonObject{{"ok", ok}, {"accepted", ok}, {"message", message}};
	};
	QComboBox *selector = provider == "twitch" ? pulseTwitchDestination.data() :
		provider == "youtube" ? pulseYouTubeDestination.data() : provider == "kick" ? pulseKickDestination.data() : nullptr;
	if (!selector) return result(false, "Unknown destination.");
	setProperty(("pulseWeaverLumiaStop_" + provider).toUtf8().constData(), !start);
	const QString mode = selector->currentData().toString();
	obs_output_t *output = provider == "twitch" ? obs_frontend_get_streaming_output() :
		obs_get_output_by_name(provider == "kick" ? "pulse_weaver_kick_output" : "pulse_weaver_youtube_output_primary");
	const bool active = output && obs_output_active(output);
	obs_output_release(output);
	if (!start) {
		if (provider == "twitch") StopStreaming();
		else if (provider == "youtube") StopPulseWeaverSecondaryOutputs();
		else if (pulseKickOutputControl) {
			pulseKickOutputControl->setProperty("command", "stop");
			pulseKickOutputControl->click();
		}
		return result(true, active ? "Stop requested for " + provider + "." : provider + " is stopped.");
	}
	if (active || (provider == "twitch" && property("pulseWeaverStreamPreparing").toBool()))
		return result(true, provider + " is already active or connecting.");
	if (mode == "off" || mode.isEmpty())
		return result(false, "Choose an output mode for " + provider + " inside Pulse Weaver first.");
	if (provider == "kick" && !property("pulseWeaverKickReady").toBool())
		return result(false, "Connect Kick inside Pulse Weaver first.");
	if (provider == "twitch") {
		obs_service_t *service = GetService();
		obs_data_t *settings = service ? obs_service_get_settings(service) : nullptr;
		const bool ready = settings && QString::fromUtf8(obs_data_get_string(settings, "service")).compare("Twitch", Qt::CaseInsensitive) == 0 &&
			*obs_data_get_string(settings, "key");
		obs_data_release(settings);
		if (!ready) return result(false, "Connect Twitch inside Pulse Weaver first.");
	}
	if (provider == "youtube") {
#ifdef YOUTUBE_ENABLED
		if (!pulseYouTubeAuth) return result(false, "Connect YouTube inside Pulse Weaver first.");
#else
		return result(false, "This build does not support YouTube.");
#endif
	}
	if (pulseStageSelector && pulseStageSelector->currentIndex() >= 0)
		ApplyPulseWeaverStage(pulseStageSelector->currentIndex(), false);
	// Use the saved mode without moving the destination selector or saving a new plan.
	ApplyPulseWeaverLiveDestinationChange(provider, "off", mode);
	output = provider == "twitch" ? obs_frontend_get_streaming_output() :
		obs_get_output_by_name(provider == "kick" ? "pulse_weaver_kick_output" : "pulse_weaver_youtube_output_primary");
	const bool accepted = output && obs_output_active(output);
	obs_output_release(output);
	// Twitch's Enhanced Broadcasting preparation is asynchronous.
	const bool pending = provider == "twitch";
	if (accepted || pending) setProperty("pulseWeaverGoLiveSession", true);
	return result(accepted || pending, accepted || pending ? "Start requested for " + provider + "." :
		(pulseDestinationStatus ? pulseDestinationStatus->text() : "The output could not start."));
}

void OBSBasic::ApplyPulseWeaverLiveDestinationChange(const QString &provider, const QString &previousMode,
							      const QString &mode)
{
	const bool wasEnabled = previousMode != "off";
	const bool enabled = mode != "off";
	if (wasEnabled == enabled)
		return;

	if (provider == "twitch") {
		if (!enabled) {
			if (obs_frontend_streaming_active())
				StreamActionTriggered();
			return;
		}
		if (obs_frontend_streaming_active())
			return;
		obs_service_t *service = GetService();
		obs_data_t *settings = service ? obs_service_get_settings(service) : nullptr;
		const QString serviceName = settings ? QString::fromUtf8(obs_data_get_string(settings, "service")) : QString();
		const char *streamKey = settings ? obs_data_get_string(settings, "key") : nullptr;
		const bool ready = serviceName.compare("Twitch", Qt::CaseInsensitive) == 0 && streamKey && *streamKey;
		obs_data_release(settings);
		if (!ready) {
			if (pulseDestinationStatus)
				pulseDestinationStatus->setText("Twitch could not be added live because its destination is not ready.");
			return;
		}
		const bool dual = mode == "dual";
		setProperty("pulseWeaverAutoAcceptEnhancedBroadcastingWarning", dual);
		config_set_bool(Config(), "Stream1", "EnableMultitrackVideo", dual);
		if (dual) {
			EnsurePulseWeaverVerticalCanvas();
			const QString canvasName = property("pulseWeaverTwitchVerticalCanvasName").toString();
			obs_canvas_t *extra = canvasName.isEmpty() ? PulseWeaverGetVerticalCanvas() :
				obs_get_canvas_by_name(canvasName.toUtf8().constData());
			if (extra) {
				config_set_string(Config(), "Stream1", "MultitrackExtraCanvas", obs_canvas_get_uuid(extra));
				obs_canvas_release(extra);
			}
		}
		activeConfiguration.SaveSafe("tmp");
		ResetOutputs();
		StreamActionTriggered();
		return;
	}

	if (provider == "youtube") {
		if (!enabled) {
			StopPulseWeaverSecondaryOutputs();
			return;
		}
#ifdef YOUTUBE_ENABLED
		if (!pulseYouTubeAuth) {
			if (pulseDestinationStatus)
				pulseDestinationStatus->setText("YouTube could not be added live because its account is not connected.");
			return;
		}
		PreparePulseWeaverYouTube();
		if (pulseYouTubeStreamKey.isEmpty() || pulseYouTubePreparedMode != mode ||
		    (mode == "dual" && pulseYouTubeSecondStreamKey.isEmpty()))
			return;
		StartPulseWeaverSecondaryOutputs();
#endif
		return;
	}

	if (provider == "kick" && pulseKickOutputControl) {
		if (enabled && !property("pulseWeaverKickReady").toBool()) {
			if (pulseDestinationStatus)
				pulseDestinationStatus->setText("Kick could not be added live because its destination is not ready.");
			return;
		}
		pulseKickOutputControl->setProperty("command", enabled ? "start" : "stop");
		pulseKickOutputControl->click();
	}
}

void OBSBasic::PreparePulseWeaverYouTube()
{
#ifdef YOUTUBE_ENABLED
	if (!pulseYouTubeAuth)
		return;
	const QString mode = pulseYouTubeDestination ? pulseYouTubeDestination->currentData().toString() : QString("off");
	if (mode == "off")
		return;
	if (pulseYouTubeOutput || pulseYouTubeSecondOutput)
		return;
	if (pulseYouTubePreparedMode == mode && !pulseYouTubeStreamKey.isEmpty() &&
	    (mode != "dual" || !pulseYouTubeSecondStreamKey.isEmpty()))
		return;
	pulseYouTubeChatCancellation->fetch_add(1, std::memory_order_acq_rel);
	++pulseYouTubeChatGeneration;
	pulseYouTubeChatSessions.clear();
	pulseYouTubeChatQueue.clear();
	pulseYouTubeSeenMessageIds.clear();
	pulseYouTubeChatSending = false;
	pulseYouTubeChatWorkers->waitUntilIdle();
	if (pulseYouTubeChatTimer)
		pulseYouTubeChatTimer->stop();
	QString youtubePrepareError;
	auto discardPreparedBroadcast = [this, &youtubePrepareError](QString &broadcastId) {
		if (broadcastId.isEmpty())
			return true;
		std::lock_guard<std::mutex> chatRequestLock(pulseYouTubeChatRequestMutex);
		if (!pulseYouTubeAuth->DeleteBroadcast(broadcastId)) {
			youtubePrepareError = pulseYouTubeAuth->GetLastError();
			return false;
		}
		broadcastId.clear();
		return true;
	};
	if (!discardPreparedBroadcast(pulseYouTubeBroadcastId) ||
	    !discardPreparedBroadcast(pulseYouTubeSecondBroadcastId)) {
		if (pulseDestinationStatus)
			pulseDestinationStatus->setText("YouTube could not clear an earlier prepared broadcast: " +
				youtubePrepareError);
		return;
	}
	pulseYouTubeStreamKey.clear();
	pulseYouTubeSecondStreamKey.clear();
	if (pulseDestinationStatus)
		pulseDestinationStatus->setText(mode == "dual" ? "Preparing private YouTube 16:9 and 9:16 broadcasts…" :
			"Preparing a private YouTube broadcast…");
	auto prepareRoute = [this, &youtubePrepareError](const QString &route, QString &streamKey, QString &broadcastId) {
		std::lock_guard<std::mutex> chatRequestLock(pulseYouTubeChatRequestMutex);
		StreamDescription stream;
		stream.title = "Pulse Weaver " + (route == "vertical" ? QString("9:16") : QString("16:9"));
		if (!pulseYouTubeAuth->InsertStream(stream)) {
			youtubePrepareError = pulseYouTubeAuth->GetLastError();
			return false;
		}
		BroadcastDescription broadcast;
		const QString configuredTitle = property("pulseWeaverYouTubeBroadcastTitle").toString().trimmed();
		broadcast.title = configuredTitle.isEmpty() ?
			("Pulse Weaver show · " + (route == "vertical" ? QString("9:16") : QString("16:9"))) :
			(configuredTitle + " · " + (route == "vertical" ? QString("9:16") : QString("16:9")));
		broadcast.description = "Created by Pulse Weaver";
		broadcast.privacy = "unlisted";
		broadcast.latency = "low";
		broadcast.made_for_kids = false;
		broadcast.auto_start = true;
		broadcast.auto_stop = true;
		broadcast.dvr = true;
		broadcast.schedul_for_later = false;
		broadcast.schedul_date_time = QDateTime::currentDateTimeUtc().addSecs(60).toString(Qt::ISODate);
		broadcast.projection = "rectangular";
		if (!pulseYouTubeAuth->InsertBroadcast(broadcast)) {
			youtubePrepareError = pulseYouTubeAuth->GetLastError();
			return false;
		}
		/* Retain the ID before binding so a bind failure can still delete the
		 * newly-created broadcast instead of abandoning it in Upcoming. */
		broadcastId = broadcast.id;
		if (!pulseYouTubeAuth->BindStream(broadcast.id, stream.id)) {
			youtubePrepareError = pulseYouTubeAuth->GetLastError();
			return false;
		}
		streamKey = stream.name;
		return true;
	};
	const QString firstRoute = mode == "vertical" ? "vertical" : "horizontal";
	if (!prepareRoute(firstRoute, pulseYouTubeStreamKey, pulseYouTubeBroadcastId) ||
	    (mode == "dual" && !prepareRoute("vertical", pulseYouTubeSecondStreamKey, pulseYouTubeSecondBroadcastId))) {
		if (pulseDestinationStatus)
			pulseDestinationStatus->setText("YouTube output could not be prepared: " + youtubePrepareError);
		discardPreparedBroadcast(pulseYouTubeBroadcastId);
		discardPreparedBroadcast(pulseYouTubeSecondBroadcastId);
		pulseYouTubeStreamKey.clear();
		pulseYouTubeSecondStreamKey.clear();
		pulseYouTubePreparedMode.clear();
		return;
	}
	pulseYouTubePreparedMode = mode;
	pulseYouTubeChatSessions = PulseYouTubeChat::create(mode, pulseYouTubeBroadcastId,
		pulseYouTubeSecondBroadcastId);
	if (pulseYouTubeChatTimer) {
		pulseYouTubeChatTimer->setInterval(1000);
		pulseYouTubeChatTimer->start();
	}
	QTimer::singleShot(0, this, &OBSBasic::PollPulseWeaverYouTubeChat);
	if (pulseDestinationStatus)
		pulseDestinationStatus->setText(QStringLiteral("YouTube ready · unlisted · ") +
			(mode == "dual" ? "16:9 + 9:16" : mode == "vertical" ? "9:16" : "16:9"));
#endif
}

void OBSBasic::StartPulseWeaverSecondaryOutputs()
{
#ifdef YOUTUBE_ENABLED
	const QString mode = pulseYouTubeDestination ? pulseYouTubeDestination->currentData().toString() : QString("off");
	if (mode == "off" || !pulseYouTubeAuth || pulseYouTubeStreamKey.isEmpty() || pulseYouTubeOutput ||
	    pulseYouTubePreparedMode != mode || (mode == "dual" && pulseYouTubeSecondStreamKey.isEmpty()))
		return;
	auto startRoute = [this](const QString &route, const QString &streamKey, const QString &suffix,
		obs_output_t *&output, obs_service_t *&service, obs_encoder_t *&videoEncoder, obs_encoder_t *&audioEncoder) {
		obs_canvas_t *canvas = obs_get_canvas_by_name(pulseOutputCanvasName("youtube", route).toUtf8().constData());
		if (!canvas)
			canvas = route == "vertical" ? PulseWeaverGetVerticalCanvas() : obs_get_main_canvas();
		videoEncoder = pulseCreateH264Encoder(("pulse_weaver_youtube_video_" + suffix).toUtf8(),
			route == "vertical" ? 4000 : 5500, false);
		if (videoEncoder && canvas)
			obs_encoder_set_video(videoEncoder, obs_canvas_get_video(canvas));
		obs_canvas_release(canvas);
		obs_data_t *audioSettings = obs_data_create();
		obs_data_set_int(audioSettings, "bitrate", 160);
		const QByteArray audioMixProperty = route == "vertical" ? "pulseWeaverYouTubeVerticalAudioMix" :
			"pulseWeaverYouTubeHorizontalAudioMix";
		const int audioMix = std::clamp(property(audioMixProperty.constData()).toInt(), 0, 5);
		audioEncoder = obs_audio_encoder_create("ffmpeg_aac", ("pulse_weaver_youtube_audio_" + suffix).toUtf8().constData(), audioSettings, audioMix, nullptr);
		obs_data_release(audioSettings);
		if (audioEncoder)
			obs_encoder_set_audio(audioEncoder, obs_get_audio());
		obs_data_t *serviceSettings = obs_data_create();
		obs_data_set_string(serviceSettings, "server", "rtmps://a.rtmps.youtube.com/live2");
		obs_data_set_string(serviceSettings, "key", streamKey.toUtf8().constData());
		service = obs_service_create("rtmp_custom", ("pulse_weaver_youtube_service_" + suffix).toUtf8().constData(), serviceSettings, nullptr);
		obs_data_release(serviceSettings);
		output = obs_output_create("rtmp_output", ("pulse_weaver_youtube_output_" + suffix).toUtf8().constData(), nullptr, nullptr);
		PulseLumia::watchOutput(output);
		bool started = false;
		if (output && service && videoEncoder && audioEncoder) {
			obs_output_set_service(output, service);
			obs_output_set_video_encoder(output, videoEncoder);
			obs_output_set_audio_encoder(output, audioEncoder, 0);
			obs_output_set_reconnect_settings(output, 10, 2);
			started = obs_output_start(output);
			if (!started && pulseDestinationStatus) {
				const char *error = obs_output_get_last_error(output);
				PulseLumia::publish("destination_state", {{"platform", "youtube"}, {"output", "pulse_weaver_youtube_output_" + suffix},
					{"state", "failed"}, {"message", QString::fromUtf8(error ? error : "The output could not start.")}});
				pulseDestinationStatus->setText("YouTube " + (route == "vertical" ? QString("9:16") : QString("16:9")) +
					" failed: " + QString::fromUtf8(error ? error : "unknown error"));
			}
		}
		return started;
	};
	const QString firstRoute = mode == "vertical" ? "vertical" : "horizontal";
	bool started = startRoute(firstRoute, pulseYouTubeStreamKey, "primary", pulseYouTubeOutput, pulseYouTubeOwnedService,
		pulseYouTubeOwnedVideoEncoder, pulseYouTubeOwnedAudioEncoder);
	if (!started) {
		StopPulseWeaverSecondaryOutputs();
		return;
	}
	/* Two YouTube RTMPS handshakes launched in the same event-loop turn are
	 * observably unreliable on Windows: the vertical route failed TLS while
	 * the 16:9 route succeeded.  Start the second encoder only after the first
	 * output has had time to establish, and never tear down a good 16:9 feed
	 * because its companion route has failed. */
	if (mode == "dual") {
		if (pulseDestinationStatus)
			pulseDestinationStatus->setText("YouTube connecting · 16:9, then 9:16…");
		QTimer::singleShot(1500, this, [this, startRoute] {
			if (!pulseYouTubeOutput || !obs_output_active(pulseYouTubeOutput)) {
				if (pulseDestinationStatus)
					pulseDestinationStatus->setText("YouTube 16:9 did not connect; 9:16 was not started.");
				return;
			}
			const bool verticalStarted = startRoute("vertical", pulseYouTubeSecondStreamKey, "vertical",
				pulseYouTubeSecondOutput, pulseYouTubeSecondService, pulseYouTubeSecondVideoEncoder,
				pulseYouTubeSecondAudioEncoder);
			if (pulseDestinationStatus)
				pulseDestinationStatus->setText(verticalStarted ? "YouTube connecting · 16:9 + 9:16" :
					"YouTube 16:9 is live; 9:16 could not start. See the status/log for its error.");
		});
	} else if (pulseDestinationStatus) {
		pulseDestinationStatus->setText("YouTube connecting · " + (mode == "vertical" ? QString("9:16") : QString("16:9")));
	}
	/* Each broadcast owns a different liveChatId.  The one-second session
	 * worker keeps resolving both IDs until YouTube exposes them, including
	 * the delayed 9:16 broadcast in Dual mode. */
	if (pulseYouTubeChatTimer && !pulseYouTubeChatTimer->isActive())
		pulseYouTubeChatTimer->start(1000);
	QTimer::singleShot(0, this, &OBSBasic::PollPulseWeaverYouTubeChat);
	RefreshPulseWeaverChatComposer();
#endif
}

bool OBSBasic::PulseWeaverRecordingActive() const
{
	return (pulseRecordingHorizontalOutput && obs_output_active(pulseRecordingHorizontalOutput)) ||
	       (pulseRecordingVerticalOutput && obs_output_active(pulseRecordingVerticalOutput));
}

void OBSBasic::StartPulseWeaverRecordings()
{
	const QString mode = pulseRecordingDestination ? pulseRecordingDestination->currentData().toString() :
		QString("standard");
	if (mode != "horizontal" && mode != "vertical" && mode != "dual")
		return;
	/* Clean up an encoder/output left behind by a failed or externally stopped
	 * attempt before allocating a new recording pipeline. */
	StopPulseWeaverRecordings();
	if (pulseStageSelector && pulseStageSelector->currentIndex() >= 0)
		ApplyPulseWeaverStage(pulseStageSelector->currentIndex(), false);

	const char *outputMode = Config() ? config_get_string(Config(), "Output", "Mode") : nullptr;
	const bool advanced = outputMode && strcmp(outputMode, "Advanced") == 0;
	const char *configuredPath = Config() ? config_get_string(
		Config(), advanced ? "AdvOut" : "SimpleOutput", advanced ? "RecFilePath" : "FilePath") : nullptr;
	QString directory = configuredPath ? QString::fromUtf8(configuredPath).trimmed() : QString();
	/* Test-only process environment override keeps native recording validation
	 * isolated without changing any user profile or visible setting. */
	const QString testDirectory = qEnvironmentVariable("PULSEWEAVER_RECORDING_TEST_DIR");
	if (!testDirectory.isEmpty())
		directory = testDirectory;
	if (directory.isEmpty())
		directory = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
	if (directory.isEmpty())
		directory = QDir::homePath();
	if (!QDir().mkpath(directory)) {
		QMessageBox::warning(this, "Recording could not start",
			"Pulse Weaver could not create the configured recording folder:\n" + QDir::toNativeSeparators(directory));
		return;
	}

	QStringList startedFiles;
	QStringList failures;
	auto startRoute = [this, &directory, &startedFiles, &failures](const QString &route, obs_output_t *&output,
		obs_encoder_t *&videoEncoder, obs_encoder_t *&audioEncoder) {
		obs_canvas_t *canvas = obs_get_canvas_by_name(pulseOutputCanvasName("recording", route).toUtf8().constData());
		if (!canvas)
			canvas = route == "vertical" ? PulseWeaverGetVerticalCanvas() : obs_get_main_canvas();
		if (!canvas || !obs_canvas_has_video(canvas)) {
			obs_canvas_release(canvas);
			failures << (route == "vertical" ? "9:16 canvas is unavailable" : "16:9 canvas is unavailable");
			return false;
		}

		const QByteArray suffix = route.toUtf8();
		videoEncoder = pulseCreateH264Encoder("pulse_weaver_recording_video_" + suffix,
			route == "vertical" ? 10000 : 14000, true);
		if (videoEncoder)
			obs_encoder_set_video(videoEncoder, obs_canvas_get_video(canvas));
		obs_canvas_release(canvas);

		obs_data_t *audioSettings = obs_data_create();
		obs_data_set_int(audioSettings, "bitrate", 192);
		const QByteArray audioMixProperty = route == "vertical" ? "pulseWeaverRecordingVerticalAudioMix" :
			"pulseWeaverRecordingHorizontalAudioMix";
		const int audioMix = std::clamp(property(audioMixProperty.constData()).toInt(), 0, 5);
		audioEncoder = obs_audio_encoder_create("ffmpeg_aac", ("pulse_weaver_recording_audio_" + suffix).constData(),
			audioSettings, audioMix, nullptr);
		obs_data_release(audioSettings);
		if (audioEncoder)
			obs_encoder_set_audio(audioEncoder, obs_get_audio());

		const QString ratio = route == "vertical" ? "9x16" : "16x9";
		const QString fileName = QDateTime::currentDateTime().toString("yyyy-MM-dd HH-mm-ss-zzz") +
			" - Pulse Weaver - " + ratio + ".mkv";
		const QString filePath = QDir(directory).filePath(fileName);
		obs_data_t *outputSettings = obs_data_create();
		obs_data_set_string(outputSettings, "path", filePath.toUtf8().constData());
		obs_data_set_string(outputSettings, "muxer_settings", "");
		output = obs_output_create("ffmpeg_muxer", ("pulse_weaver_recording_output_" + suffix).constData(),
			outputSettings, nullptr);
		PulseLumia::watchOutput(output);
		obs_data_release(outputSettings);
		bool started = false;
		if (output && videoEncoder && audioEncoder) {
			obs_output_set_video_encoder(output, videoEncoder);
			obs_output_set_audio_encoder(output, audioEncoder, 0);
			started = obs_output_start(output);
		}
		if (started) {
			startedFiles << filePath;
		} else {
			const char *lastError = output ? obs_output_get_last_error(output) : nullptr;
			PulseLumia::publish("recording_state", {{"platform", "recording"}, {"output", QString::fromUtf8("pulse_weaver_recording_output_" + suffix)},
				{"state", "failed"}, {"message", QString::fromUtf8(lastError ? lastError : "The recording could not start.")}});
			failures << (ratio + " failed: " + QString::fromUtf8(lastError && *lastError ? lastError :
				"the encoder or MKV output could not start"));
		}
		return started;
	};

	if (mode == "horizontal" || mode == "dual")
		startRoute("horizontal", pulseRecordingHorizontalOutput, pulseRecordingHorizontalVideoEncoder,
			pulseRecordingHorizontalAudioEncoder);
	if (mode == "vertical" || mode == "dual")
		startRoute("vertical", pulseRecordingVerticalOutput, pulseRecordingVerticalVideoEncoder,
			pulseRecordingVerticalAudioEncoder);
	if (startedFiles.isEmpty()) {
		StopPulseWeaverRecordings();
		QMessageBox::warning(this, "Recording could not start", failures.join("\n"));
		return;
	}
	if (pulseDestinationStatus) {
		pulseDestinationStatus->setText(QString("Recording %1 · UNSTABLE · %2")
			.arg(mode == "dual" ? "16:9 + 9:16" : mode == "vertical" ? "9:16" : "16:9")
			.arg(QDir::toNativeSeparators(directory)));
	}
	if (!failures.isEmpty())
		QMessageBox::warning(this, "Part of the recording could not start",
			"The remaining recording is still running.\n\n" + failures.join("\n"));
	UpdatePulseWeaverShell();
}

void OBSBasic::StopPulseWeaverRecordings()
{
	auto releaseRoute = [](obs_output_t *&output, obs_encoder_t *&videoEncoder, obs_encoder_t *&audioEncoder) {
		if (output) {
			if (obs_output_active(output))
				obs_output_stop(output);
			obs_output_release(output);
			output = nullptr;
		}
		if (videoEncoder) {
			obs_encoder_release(videoEncoder);
			videoEncoder = nullptr;
		}
		if (audioEncoder) {
			obs_encoder_release(audioEncoder);
			audioEncoder = nullptr;
		}
	};
	releaseRoute(pulseRecordingHorizontalOutput, pulseRecordingHorizontalVideoEncoder,
		pulseRecordingHorizontalAudioEncoder);
	releaseRoute(pulseRecordingVerticalOutput, pulseRecordingVerticalVideoEncoder,
		pulseRecordingVerticalAudioEncoder);
	if (pulseDestinationStatus)
		pulseDestinationStatus->setText("Recording stopped.");
}

void OBSBasic::StopPulseWeaverSecondaryOutputs()
{
	/* Invalidate first. Workers waiting for the mutex will observe the new
	 * epoch and leave without making another request. A worker already inside
	 * an API call completes before broadcast cleanup takes the same mutex. */
	pulseYouTubeChatCancellation->fetch_add(1, std::memory_order_acq_rel);
	++pulseYouTubeChatGeneration;
	if (pulseYouTubeChatTimer)
		pulseYouTubeChatTimer->stop();
	pulseYouTubeChatSessions.clear();
	pulseYouTubeChatQueue.clear();
	pulseYouTubeSeenMessageIds.clear();
	pulseYouTubeChatSending = false;
	pulseYouTubeChatWorkers->waitUntilIdle();
	if (pulseYouTubeOutput) {
		if (obs_output_active(pulseYouTubeOutput))
			obs_output_stop(pulseYouTubeOutput);
		obs_output_release(pulseYouTubeOutput);
		pulseYouTubeOutput = nullptr;
	}
	if (pulseYouTubeSecondOutput) {
		if (obs_output_active(pulseYouTubeSecondOutput))
			obs_output_stop(pulseYouTubeSecondOutput);
		obs_output_release(pulseYouTubeSecondOutput);
		pulseYouTubeSecondOutput = nullptr;
	}
	if (pulseYouTubeOwnedService) {
		obs_service_release(pulseYouTubeOwnedService);
		pulseYouTubeOwnedService = nullptr;
	}
	if (pulseYouTubeSecondService) {
		obs_service_release(pulseYouTubeSecondService);
		pulseYouTubeSecondService = nullptr;
	}
	if (pulseYouTubeOwnedVideoEncoder) {
		obs_encoder_release(pulseYouTubeOwnedVideoEncoder);
		pulseYouTubeOwnedVideoEncoder = nullptr;
	}
	if (pulseYouTubeOwnedAudioEncoder) {
		obs_encoder_release(pulseYouTubeOwnedAudioEncoder);
		pulseYouTubeOwnedAudioEncoder = nullptr;
	}
	if (pulseYouTubeSecondVideoEncoder) {
		obs_encoder_release(pulseYouTubeSecondVideoEncoder);
		pulseYouTubeSecondVideoEncoder = nullptr;
	}
	if (pulseYouTubeSecondAudioEncoder) {
		obs_encoder_release(pulseYouTubeSecondAudioEncoder);
		pulseYouTubeSecondAudioEncoder = nullptr;
	}
#ifdef YOUTUBE_ENABLED
	bool youtubeCleanupOk = true;
	QString youtubeCleanupError;
	std::lock_guard<std::mutex> chatRequestLock(pulseYouTubeChatRequestMutex);
	auto finishOrDiscardBroadcast = [this, &youtubeCleanupOk, &youtubeCleanupError](QString &broadcastId) {
		if (!pulseYouTubeAuth || broadcastId.isEmpty())
			return;
		const bool cleaned = pulseYouTubeAuth->FinishBroadcast(broadcastId);
		if (cleaned)
			broadcastId.clear();
		else {
			youtubeCleanupOk = false;
			youtubeCleanupError = pulseYouTubeAuth->GetLastError();
		}
	};
	finishOrDiscardBroadcast(pulseYouTubeBroadcastId);
	finishOrDiscardBroadcast(pulseYouTubeSecondBroadcastId);
	if (!youtubeCleanupOk && pulseDestinationStatus)
		pulseDestinationStatus->setText("YouTube stopped, but broadcast cleanup must be retried: " +
			youtubeCleanupError);
#endif
	pulseYouTubeStreamKey.clear();
	pulseYouTubeSecondStreamKey.clear();
	pulseYouTubePreparedMode.clear();
	RefreshPulseWeaverChatComposer();
}

void OBSBasic::RefreshPulseWeaverChatComposer()
{
	if (!pulseChatProvider || !pulseChatInput || !pulseChatSend)
		return;
	const QString provider = pulseChatProvider->currentData().toString();
	const bool twitchReady = property("pulseWeaverTwitchChatReady").toBool();
	const bool kickReady = property("pulseWeaverKickReady").toBool();
	bool youtubeAccount = false;
#ifdef YOUTUBE_ENABLED
	youtubeAccount = bool(pulseYouTubeAuth);
#endif
	const bool youtubeReady = youtubeAccount && PulseWeaverYouTubeChatsReady();
	const bool youtubeAvailable = youtubeAccount && PulseWeaverYouTubeChatAvailable();
	bool enabled = false;
	QString status;
	if (provider == "all") {
		enabled = twitchReady || kickReady || youtubeAvailable;
		status = enabled ? (youtubeAccount && !pulseYouTubeChatSessions.isEmpty() && !youtubeReady ?
			"Post to connected chats · YouTube live chat is connecting…" : "Post to every connected chat.") :
			"Connect a chat account in Action → Connections.";
	} else if (provider == "twitch") {
		enabled = twitchReady;
		status = enabled ? "Twitch chat connected." : "Connect Twitch in Action → Connections.";
	} else if (provider == "kick") {
		enabled = kickReady;
		status = enabled ? "Kick send ready · incoming chat requires the Kick event relay." :
			"Connect Kick in Action → Connections.";
	} else {
		enabled = youtubeAvailable;
		status = !youtubeAccount ? "Connect YouTube in Action → Connections." :
			youtubeReady ? "YouTube live chat connected." :
			youtubeAvailable ? "YouTube chat connected · another active broadcast is still connecting…" :
			!pulseYouTubeChatSessions.isEmpty() ? "YouTube live chat is connecting…" :
			"YouTube connected · chat opens when its broadcast is prepared.";
	}
	pulseChatInput->setEnabled(enabled);
	pulseChatSend->setEnabled(enabled);
	if (pulseChatStatus)
		pulseChatStatus->setText(status);
}

bool OBSBasic::PulseWeaverYouTubeChatsReady() const
{
	return PulseYouTubeChat::ready(pulseYouTubeChatSessions);
}

bool OBSBasic::PulseWeaverYouTubeChatAvailable() const
{
	return PulseYouTubeChat::available(pulseYouTubeChatSessions);
}

void OBSBasic::PollPulseWeaverYouTubeChat()
{
#ifdef YOUTUBE_ENABLED
	if (!pulseYouTubeAuth || pulseYouTubeChatSessions.isEmpty())
		return;
	const qint64 now = QDateTime::currentMSecsSinceEpoch();
	const auto auth = pulseYouTubeAuth;
	const quint64 generation = pulseYouTubeChatGeneration;
	const auto cancellation = pulseYouTubeChatCancellation;
	const quint64 requestEpoch = cancellation->load(std::memory_order_acquire);
	const QStringList routes = pulseYouTubeChatSessions.keys();
	for (const QString &route : routes) {
		auto sessionIt = pulseYouTubeChatSessions.find(route);
		if (sessionIt == pulseYouTubeChatSessions.end() || sessionIt->requestPending ||
		    sessionIt->nextRequestMs > now)
			continue;
		sessionIt->requestPending = true;
		const QString broadcastId = sessionIt->broadcastId;
		const QString chatId = sessionIt->liveChatId;
		const QString page = sessionIt->pageToken;
		QPointer<OBSBasic> guard(this);
		if (chatId.isEmpty()) {
			sessionIt->nextRequestMs = now + 2000;
			const bool started = pulseStartYouTubeChatWorker(pulseYouTubeChatWorkers,
				[guard, auth, route, broadcastId, generation, cancellation, requestEpoch] {
				QString resolvedChatId;
				QString resolveError;
				bool found = false;
				{
					std::lock_guard<std::mutex> chatRequestLock(pulseYouTubeChatRequestMutex);
					if (cancellation->load(std::memory_order_acquire) != requestEpoch)
						return;
					found = auth->GetLiveChatId(broadcastId, resolvedChatId);
					if (!found)
						resolveError = auth->GetLastError();
				}
				if (!guard)
					return;
				QMetaObject::invokeMethod(guard, [guard, route, broadcastId, generation, found, resolvedChatId,
						resolveError] {
					if (!guard || guard->pulseYouTubeChatGeneration != generation)
						return;
					auto current = guard->pulseYouTubeChatSessions.find(route);
					if (current == guard->pulseYouTubeChatSessions.end() || current->broadcastId != broadcastId)
						return;
					current->requestPending = false;
					if (!found || resolvedChatId.isEmpty()) {
						++current->failures;
						current->lastError = resolveError;
						current->nextRequestMs = QDateTime::currentMSecsSinceEpoch() + 2000;
						if (current->failures >= 5 && guard->pulseChatStatus) {
							const QString routeName = route == "vertical" ? "9:16" : "16:9";
							guard->pulseChatStatus->setText(resolveError.isEmpty() ?
								"YouTube " + routeName + " chat is not available yet; still retrying." :
								"YouTube " + routeName + " chat is reconnecting: " + resolveError);
						}
						return;
					}
					current->liveChatId = resolvedChatId;
					current->pageToken.clear();
					current->nextRequestMs = 0;
					current->failures = 0;
					current->lastError.clear();
					guard->RefreshPulseWeaverChatComposer();
					guard->FlushPulseWeaverYouTubeChatQueue();
				}, Qt::QueuedConnection);
			});
			if (!started) {
				sessionIt->requestPending = false;
				sessionIt->nextRequestMs = now + 2000;
				if (pulseChatStatus)
					pulseChatStatus->setText("YouTube chat worker could not start; retrying.");
			}
			continue;
		}

		sessionIt->nextRequestMs = now + 4000;
		const bool pollSubscribers = broadcastId == pulseYouTubeBroadcastId &&
			now >= pulseYouTubeNextSubscriberPoll;
		const bool started = pulseStartYouTubeChatWorker(pulseYouTubeChatWorkers,
			[guard, auth, route, broadcastId, chatId, page, generation, pollSubscribers,
			 cancellation, requestEpoch] {
			QString next = page;
			QVector<YoutubeChatEvent> events;
			QVector<YoutubeSubscriber> subscribers;
			int interval = 4000;
			bool ok = false;
			bool subscribersOk = false;
			QString pollError;
			{
				std::lock_guard<std::mutex> chatRequestLock(pulseYouTubeChatRequestMutex);
				if (cancellation->load(std::memory_order_acquire) != requestEpoch)
					return;
				ok = auth->GetLiveChatMessages(chatId, next, events, interval);
				if (!ok)
					pollError = auth->GetLastError();
				if (pollSubscribers) {
					if (cancellation->load(std::memory_order_acquire) != requestEpoch)
						return;
					subscribersOk = auth->GetRecentSubscribers(subscribers);
				}
			}
			if (!guard)
				return;
			QMetaObject::invokeMethod(guard, [guard, route, broadcastId, chatId, generation, ok, next,
					events, interval, pollSubscribers, subscribersOk, subscribers, pollError] {
				if (!guard || guard->pulseYouTubeChatGeneration != generation)
					return;
				auto currentSession = guard->pulseYouTubeChatSessions.find(route);
				if (currentSession == guard->pulseYouTubeChatSessions.end() ||
				    currentSession->broadcastId != broadcastId || currentSession->liveChatId != chatId)
					return;
				currentSession->requestPending = false;
				currentSession->nextRequestMs = QDateTime::currentMSecsSinceEpoch() +
					std::clamp(interval, 1000, 15000);
				if (!ok) {
					++currentSession->failures;
					currentSession->lastError = pollError;
					if (currentSession->failures >= 3 && guard->pulseChatStatus) {
						const QString routeName = route == "vertical" ? "9:16" : "16:9";
						guard->pulseChatStatus->setText("YouTube " + routeName + " chat lost connection; retrying" +
							(pollError.isEmpty() ? QString(".") : ": " + pollError));
					}
					if (currentSession->failures >= 5) {
						currentSession->liveChatId.clear();
						currentSession->pageToken.clear();
						currentSession->nextRequestMs = QDateTime::currentMSecsSinceEpoch() + 2000;
						guard->RefreshPulseWeaverChatComposer();
					}
					return;
				}
				currentSession->failures = 0;
				currentSession->lastError.clear();
				currentSession->pageToken = next;
				if (pollSubscribers)
					guard->pulseYouTubeNextSubscriberPoll = QDateTime::currentMSecsSinceEpoch() + 90000;
				if (subscribersOk) {
					QSet<QString> subscriberIds;
					for (const YoutubeSubscriber &subscriber : subscribers) {
						subscriberIds.insert(subscriber.id);
						if (!guard->pulseYouTubeSubscribersSeeded ||
						    guard->pulseYouTubeSubscriberIds.contains(subscriber.id))
							continue;
						const QJsonObject payload{{"id", subscriber.id}, {"user", subscriber.name},
							{"visibility", "public_subscriber"}};
						calldata_t data; calldata_init(&data);
						calldata_set_string(&data, "platform", "youtube");
						calldata_set_string(&data, "type", "channel.subscribe");
						const QByteArray json = QJsonDocument(payload).toJson(QJsonDocument::Compact);
						calldata_set_string(&data, "json", json.constData());
						proc_handler_call(obs_get_proc_handler(), "pulseweaver_publish", &data);
						calldata_free(&data);
					}
					guard->pulseYouTubeSubscriberIds = subscriberIds;
					guard->pulseYouTubeSubscribersSeeded = true;
				}
				for (const YoutubeChatEvent &event : events) {
					if (!event.id.isEmpty() && guard->pulseYouTubeSeenMessageIds.contains(event.id))
						continue;
					if (!event.id.isEmpty())
						guard->pulseYouTubeSeenMessageIds.insert(event.id);
					QString type = "chat.message";
					if (event.type == "newSponsorEvent") type = "membership.received";
					else if (event.type == "memberMilestoneChatEvent") type = "membership.milestone";
					else if (event.type == "membershipGiftingEvent" || event.type == "giftMembershipReceivedEvent") type = "membership.gift";
					else if (event.type == "superChatEvent") type = "super_chat.received";
					else if (event.type == "superStickerEvent") type = "super_sticker.received";
					const QJsonObject payload{{"id", event.id}, {"user", event.user}, {"message", event.message},
						{"amount", event.amount}, {"youtubeType", event.type}, {"route", route},
						{"broadcastId", broadcastId}};
					calldata_t data; calldata_init(&data);
					calldata_set_string(&data, "platform", "youtube");
					calldata_set_string(&data, "type", type.toUtf8().constData());
					const QByteArray json = QJsonDocument(payload).toJson(QJsonDocument::Compact);
					calldata_set_string(&data, "json", json.constData());
					proc_handler_call(obs_get_proc_handler(), "pulseweaver_publish", &data);
					calldata_free(&data);
					if (event.type == "messageDeletedEvent" && !event.deleted_message_id.isEmpty())
						PulseChat::markDeleted(guard->pulseChatFeed, "youtube", event.deleted_message_id);
					if (event.type == "userBannedEvent" && !event.banned_user_id.isEmpty())
						PulseChat::markDeleted(guard->pulseChatFeed, "youtube", {}, event.banned_user_id);
					if (guard->pulseChatFeed && event.type == "textMessageEvent")
						pulseAppendUnifiedChat(guard->pulseChatFeed, "youtube", event.user, event.message,
							event.user_colour, event.badges, event.user_id, event.id, false, chatId, route);
				}
				if (guard->pulseYouTubeSeenMessageIds.size() > 4000)
					guard->pulseYouTubeSeenMessageIds.clear();
			}, Qt::QueuedConnection);
		});
		if (!started) {
			sessionIt->requestPending = false;
			sessionIt->nextRequestMs = now + 2000;
			if (pulseChatStatus)
				pulseChatStatus->setText("YouTube chat worker could not start; retrying.");
		}
	}
	FlushPulseWeaverYouTubeChatQueue();
#endif
}

void OBSBasic::SendPulseWeaverYouTubeChat()
{
#ifdef YOUTUBE_ENABLED
	if (!pulseYouTubeAuth || !pulseChatInput)
		return;
	const bool all = pulseChatProvider && pulseChatProvider->currentData().toString() == "all";
	const QString message = (all ? pulseChatInput->property("pulseWeaverBroadcastMessage").toString() :
		pulseChatInput->text()).trimmed();
	if (message.isEmpty() || pulseYouTubeChatSessions.isEmpty())
		return;
	if (pulseYouTubeChatQueue.size() >= 10) {
		if (pulseChatStatus)
			pulseChatStatus->setText("YouTube chat is still connecting; its message queue is full.");
		return;
	}
	PulseYouTubePendingChatMessage pending;
	pending.id = ++pulseYouTubeChatMessageSequence;
	pending.message = message;
	pending.all = all;
	pending.queuedMs = QDateTime::currentMSecsSinceEpoch();
	pulseYouTubeChatQueue.append(pending);
	if (!all)
		pulseChatInput->clear();
	if (pulseYouTubeChatTimer && !pulseYouTubeChatTimer->isActive())
		pulseYouTubeChatTimer->start(1000);
	FlushPulseWeaverYouTubeChatQueue();
#endif
}

void OBSBasic::FlushPulseWeaverYouTubeChatQueue()
{
#ifdef YOUTUBE_ENABLED
	if (pulseYouTubeChatSending || !pulseYouTubeAuth || pulseYouTubeChatQueue.isEmpty())
		return;
	const qint64 now = QDateTime::currentMSecsSinceEpoch();
	auto restoreFailedMessage = [this](const PulseYouTubePendingChatMessage &failed) {
		if (!failed.all && pulseChatInput && pulseChatProvider && pulseChatInput->text().isEmpty() &&
		    pulseChatProvider->currentData().toString() == "youtube") {
			pulseChatInput->setText(failed.message);
			return true;
		}
		return false;
	};

	/* A dual-route message may already have reached one chat while the other
	 * broadcast is still resolving. Scan the whole queue so that state cannot
	 * hold later messages back from routes that are ready. */
	int dispatchIndex = -1;
	QVector<QPair<QString, QString>> targets;
	QSet<QString> blockedRoutes;
	for (int index = 0; index < pulseYouTubeChatQueue.size();) {
		auto &candidate = pulseYouTubeChatQueue[index];
		bool complete = !pulseYouTubeChatSessions.isEmpty();
		for (auto session = pulseYouTubeChatSessions.cbegin();
		     complete && session != pulseYouTubeChatSessions.cend(); ++session)
			complete = candidate.deliveredRoutes.contains(session.key());
		const bool expired = now - candidate.queuedMs >= pulseYouTubeChatMessageLifetimeMs;
		if (complete || expired) {
			const PulseYouTubePendingChatMessage finished = candidate;
			pulseYouTubeChatQueue.removeAt(index);
			if (expired && finished.deliveredRoutes.isEmpty()) {
				const bool restored = restoreFailedMessage(finished);
				if (pulseChatStatus)
					pulseChatStatus->setText(restored ?
						"YouTube message could not be sent; it has been restored for you to retry." :
						"YouTube message could not be sent.");
			} else if (expired && pulseChatStatus) {
				pulseChatStatus->setText("YouTube message sent to the available live chat; another broadcast did not connect.");
			}
			continue;
		}
		if (candidate.nextAttemptMs <= now) {
			const auto availableTargets = PulseYouTubeChat::pendingTargets(pulseYouTubeChatSessions,
				candidate.deliveredRoutes, blockedRoutes);
			if (!availableTargets.isEmpty()) {
				dispatchIndex = index;
				for (auto target = availableTargets.cbegin(); target != availableTargets.cend(); ++target)
					targets.push_back({target.key(), target.value()});
				break;
			}
		}
		blockedRoutes.unite(PulseYouTubeChat::owedRoutes(pulseYouTubeChatSessions,
			candidate.deliveredRoutes));
		++index;
	}
	if (dispatchIndex < 0) {
		if (!pulseYouTubeChatQueue.isEmpty() && pulseChatStatus)
			pulseChatStatus->setText("YouTube live chat is connecting; message queued.");
		return;
	}
	auto &queued = pulseYouTubeChatQueue[dispatchIndex];
	const quint64 messageId = queued.id;
	const QString message = queued.message;
	const bool all = queued.all;
	pulseYouTubeChatSending = true;
	if (pulseChatStatus)
		pulseChatStatus->setText(targets.size() > 1 ? "Sending to both YouTube live chats…" : "Sending to YouTube…");
	const auto auth = pulseYouTubeAuth;
	const quint64 generation = pulseYouTubeChatGeneration;
	const auto cancellation = pulseYouTubeChatCancellation;
	const quint64 requestEpoch = cancellation->load(std::memory_order_acquire);
	QPointer<OBSBasic> guard(this);
	const bool started = pulseStartYouTubeChatWorker(pulseYouTubeChatWorkers,
		[guard, auth, targets, messageId, message, all, generation, cancellation, requestEpoch] {
		QStringList deliveredRoutes;
		QString error;
		{
			std::lock_guard<std::mutex> chatRequestLock(pulseYouTubeChatRequestMutex);
			for (const auto &target : targets) {
				if (cancellation->load(std::memory_order_acquire) != requestEpoch)
					return;
				if (auth->SendLiveChatMessage(target.second, message))
					deliveredRoutes.push_back(target.first);
				else if (error.isEmpty())
					error = auth->GetLastError();
			}
		}
		if (!guard)
			return;
		QMetaObject::invokeMethod(guard, [guard, deliveredRoutes, attempted = targets.size(), error,
				messageId, message, all, generation] {
			if (!guard || guard->pulseYouTubeChatGeneration != generation)
				return;
			guard->pulseYouTubeChatSending = false;
			auto queuedIt = std::find_if(guard->pulseYouTubeChatQueue.begin(), guard->pulseYouTubeChatQueue.end(),
				[messageId](const auto &candidate) { return candidate.id == messageId; });
			if (queuedIt == guard->pulseYouTubeChatQueue.end())
				return;
			for (const QString &route : deliveredRoutes)
				queuedIt->deliveredRoutes.insert(route);
			const qint64 completedAt = QDateTime::currentMSecsSinceEpoch();
			if (deliveredRoutes.size() < attempted)
				queuedIt->nextAttemptMs = completedAt + 3000;
			bool complete = !guard->pulseYouTubeChatSessions.isEmpty();
			for (auto session = guard->pulseYouTubeChatSessions.cbegin();
			     complete && session != guard->pulseYouTubeChatSessions.cend(); ++session)
				complete = queuedIt->deliveredRoutes.contains(session.key());
			const bool expired = completedAt - queuedIt->queuedMs >= pulseYouTubeChatMessageLifetimeMs;
			const int delivered = queuedIt->deliveredRoutes.size();
			const bool giveUp = expired && delivered == 0;
			const bool expiredPartial = expired && delivered > 0;
			if (complete || expired)
				guard->pulseYouTubeChatQueue.erase(queuedIt);
			if (guard->pulseChatStatus) {
				if (complete)
					guard->pulseChatStatus->setText(delivered > 1 ? "YouTube message sent to both live chats." :
						"YouTube message sent.");
				else if (giveUp)
					guard->pulseChatStatus->setText("YouTube message could not be sent.");
				else if (expiredPartial)
					guard->pulseChatStatus->setText("YouTube message sent to the available live chat; another broadcast did not connect.");
				else if (deliveredRoutes.size() < attempted)
					guard->pulseChatStatus->setText("YouTube send failed; retrying shortly: " + error);
				else
					guard->pulseChatStatus->setText("YouTube message sent to the active chat; waiting for another broadcast…");
			}
			if (giveUp && !all && guard->pulseChatInput && guard->pulseChatProvider &&
			    guard->pulseChatInput->text().isEmpty() &&
			    guard->pulseChatProvider->currentData().toString() == "youtube") {
				guard->pulseChatInput->setText(message);
				if (guard->pulseChatStatus)
					guard->pulseChatStatus->setText("YouTube message could not be sent; it has been restored for you to retry.");
			}
			guard->FlushPulseWeaverYouTubeChatQueue();
		}, Qt::QueuedConnection);
	});
	if (!started) {
		pulseYouTubeChatSending = false;
		queued.nextAttemptMs = QDateTime::currentMSecsSinceEpoch() + 3000;
		if (pulseChatStatus)
			pulseChatStatus->setText("YouTube send worker could not start; retrying shortly.");
	}
#endif
}

void OBSBasic::ModeratePulseWeaverYouTubeChat(const QString &messageId, const QString &userId,
					      int durationSeconds, const QString &liveChatId)
{
#ifdef YOUTUBE_ENABLED
	QString chatId = liveChatId;
	if (chatId.isEmpty()) {
		const QStringList targets = PulseYouTubeChat::targets(pulseYouTubeChatSessions);
		if (!targets.isEmpty())
			chatId = targets.first();
	}
	const bool deleting = !messageId.isEmpty();
	if (!pulseYouTubeAuth || (!deleting && chatId.isEmpty())) {
		if (pulseChatStatus) pulseChatStatus->setText("YouTube moderation requires an active live chat.");
		return;
	}
	if (pulseChatStatus) pulseChatStatus->setText("Applying YouTube moderation…");
	const auto auth = pulseYouTubeAuth;
	const quint64 generation = pulseYouTubeChatGeneration;
	const auto cancellation = pulseYouTubeChatCancellation;
	const quint64 requestEpoch = cancellation->load(std::memory_order_acquire);
	QPointer<OBSBasic> guard(this);
	const bool started = pulseStartYouTubeChatWorker(pulseYouTubeChatWorkers,
		[guard, auth, chatId, messageId, userId, durationSeconds, deleting, generation,
		 cancellation, requestEpoch] {
		bool ok = false;
		QString error;
		{
			std::lock_guard<std::mutex> chatRequestLock(pulseYouTubeChatRequestMutex);
			if (cancellation->load(std::memory_order_acquire) != requestEpoch)
				return;
			ok = deleting ? auth->DeleteLiveChatMessage(messageId) :
				auth->ModerateLiveChatUser(chatId, userId, durationSeconds);
			error = auth->GetLastError();
		}
		if (!guard)
			return;
		QMetaObject::invokeMethod(guard, [guard, ok, deleting, durationSeconds, error, messageId, userId,
			generation] {
			if (!guard || guard->pulseYouTubeChatGeneration != generation || !guard->pulseChatStatus) return;
			const QString success = deleting ? "YouTube message deleted." :
				(durationSeconds > 0 ? QString("YouTube user timed out for %1 minutes.").arg(durationSeconds / 60) : "YouTube user banned.");
			guard->pulseChatStatus->setText(ok ? success : "YouTube moderation failed: " + error);
			if (ok) PulseChat::markDeleted(guard->pulseChatFeed, "youtube", messageId, userId);
		}, Qt::QueuedConnection);
	});
	if (!started && pulseChatStatus)
		pulseChatStatus->setText("YouTube moderation worker could not start; try again.");
#else
	Q_UNUSED(messageId); Q_UNUSED(userId); Q_UNUSED(durationSeconds); Q_UNUSED(liveChatId);
#endif
}

void OBSBasic::RenderPulseHorizontal(void *data, uint32_t cx, uint32_t cy)
{
	auto *self = static_cast<OBSBasic *>(data);
	if (self && !self->pulseHorizontalPreviewProvider.isEmpty()) {
		const QByteArray name = pulseOutputCanvasName(self->pulseHorizontalPreviewProvider, "horizontal").toUtf8();
		obs_canvas_t *canvas = obs_get_canvas_by_name(name.constData());
		obs_video_info canvasInfo = {};
		if (canvas && obs_canvas_get_video_info(canvas, &canvasInfo)) {
			renderCanvasFit(canvas, canvasInfo.base_width, canvasInfo.base_height, cx, cy);
			obs_canvas_release(canvas);
			return;
		}
		obs_canvas_release(canvas);
	}
	obs_video_info info = {};
	if (!obs_get_video_info(&info))
		return;
	renderSourceFit(info.base_width, info.base_height, cx, cy);
}

void OBSBasic::RenderPulseVertical(void *data, uint32_t cx, uint32_t cy)
{
	auto *self = static_cast<OBSBasic *>(data);
	if (self && !self->pulseVerticalPreviewProvider.isEmpty()) {
		const QByteArray name = pulseOutputCanvasName(self->pulseVerticalPreviewProvider, "vertical").toUtf8();
		obs_canvas_t *platformCanvas = obs_get_canvas_by_name(name.constData());
		obs_video_info platformInfo = {};
		if (platformCanvas && obs_canvas_get_video_info(platformCanvas, &platformInfo)) {
			renderCanvasFit(platformCanvas, platformInfo.base_width, platformInfo.base_height, cx, cy);
			obs_canvas_release(platformCanvas);
			return;
		}
		obs_canvas_release(platformCanvas);
	}
	obs_canvas_t *canvas = PulseWeaverGetVerticalCanvas();
	if (!canvas)
		return;
	obs_video_info info = {};
	if (obs_canvas_get_video_info(canvas, &info))
		renderCanvasFit(canvas, info.base_width, info.base_height, cx, cy);
	obs_canvas_release(canvas);
}
