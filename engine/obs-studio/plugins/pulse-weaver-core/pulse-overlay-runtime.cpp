#include "pulse-overlay-runtime.hpp"
#include "pulse-overlay-alerts.hpp"
#include "pulse-overlay-store.hpp"
#include "pulse-overlay-renderer.hpp"
#include "pulse-overlay-migration.hpp"
#include "../obs-browser/panel/browser-panel.hpp"

#include <obs-module.h>
#include <obs-frontend-api.h>
#include <util/bmem.h>
#include <util/platform.h>

#include <QCheckBox>
#include <QColor>
#include <QComboBox>
#include <QDesktopServices>
#include <QDateTime>
#include <QDialog>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFormLayout>
#include <QGraphicsItem>
#include <QGraphicsRectItem>
#include <QGraphicsScene>
#include <QGraphicsTextItem>
#include <QGraphicsView>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QQueue>
#include <QSaveFile>
#include <QSet>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QSplitter>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTabWidget>
#include <QTextEdit>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>
#include <QUuid>
#include <QVBoxLayout>

#include <algorithm>
#include <functional>
#include <vector>

namespace {

QString overlayConfigDirectory()
{
	char *path = obs_module_config_path("overlays");
	const QString result = path ? QString::fromUtf8(path) : QStringLiteral("overlays");
	bfree(path);
	QDir().mkpath(result);
	return result;
}

struct OverlayElement {
	QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
	QString type = "text";
	QString name = "Text";
	QString text = "Your text";
	QString binding;
	QString asset;
	QString color = "#FFFFFF";
	QString background = "#7C3AED";
	double x = 120.0;
	double y = 120.0;
	double width = 720.0;
	double height = 120.0;
	double rotation = 0.0;
	double opacity = 1.0;
	int fontSize = 48;
	bool visible = true;
	bool locked = false;
};

struct OverlayDocument {
	QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
	QString name = "Untitled Overlay";
	QString triggerEvent;
	int width = 1920;
	int height = 1080;
	int durationMs = 6000;
	bool eventDriven = false;
	QString externalUrl;
	QString customHtml;
	QString customCss;
	QString customJs;
	bool alertBox = false;
	QJsonArray alertVariations;
	QString enterAnimation = "slide-up";
	QString exitAnimation = "fade";
	QString fontFamily = "Segoe UI";
	bool textToSpeech = false;
	std::vector<OverlayElement> elements;
	QJsonObject raw;
	QJsonObject baseline;
};

QJsonObject elementJson(const OverlayElement &element)
{
	return {{"id", element.id}, {"type", element.type}, {"name", element.name}, {"text", element.text},
		{"binding", element.binding}, {"asset", element.asset}, {"color", element.color},
		{"background", element.background}, {"x", element.x}, {"y", element.y},
		{"width", element.width}, {"height", element.height}, {"rotation", element.rotation},
		{"opacity", element.opacity}, {"fontSize", element.fontSize}, {"visible", element.visible},
		{"locked", element.locked}};
}

OverlayElement elementFromJson(const QJsonObject &json)
{
	OverlayElement element;
	element.id = json.value("id").toString(element.id);
	element.type = json.value("type").toString(element.type);
	element.name = json.value("name").toString(element.name);
	element.text = json.value("text").toString(element.text);
	element.binding = json.value("binding").toString();
	element.asset = json.value("asset").toString();
	element.color = json.value("color").toString(element.color);
	element.background = json.value("background").toString(element.background);
	element.x = json.value("x").toDouble(element.x);
	element.y = json.value("y").toDouble(element.y);
	element.width = std::max(10.0, json.value("width").toDouble(element.width));
	element.height = std::max(10.0, json.value("height").toDouble(element.height));
	element.rotation = json.value("rotation").toDouble();
	element.opacity = std::clamp(json.value("opacity").toDouble(1.0), 0.0, 1.0);
	element.fontSize = std::clamp(json.value("fontSize").toInt(element.fontSize), 6, 300);
	element.visible = json.value("visible").toBool(true);
	element.locked = json.value("locked").toBool(false);
	return element;
}

QJsonObject projectedDocumentJson(const OverlayDocument &document)
{
	QJsonArray elements;
	for (const OverlayElement &element : document.elements)
		elements.append(elementJson(element));
	return {{"id", document.id}, {"name", document.name}, {"triggerEvent", document.triggerEvent},
		{"width", document.width}, {"height", document.height}, {"durationMs", document.durationMs},
		{"eventDriven", document.eventDriven}, {"externalUrl", document.externalUrl}, {"customHtml", document.customHtml},
		{"customCss", document.customCss}, {"customJs", document.customJs}, {"alertBox", document.alertBox},
		{"alertVariations", document.alertVariations}, {"enterAnimation", document.enterAnimation},
		{"exitAnimation", document.exitAnimation}, {"fontFamily", document.fontFamily},
		{"textToSpeech", document.textToSpeech}, {"elements", elements}};
}

QJsonObject documentJson(const OverlayDocument &document)
{
	return document.raw.isEmpty() ? projectedDocumentJson(document)
		: PulseOverlay::mergeEditedDocument(document.raw, document.baseline, projectedDocumentJson(document));
}

OverlayDocument documentFromJson(const QJsonObject &json)
{
	OverlayDocument document;
	document.id = json.value("id").toString(document.id);
	document.name = json.value("name").toString(document.name);
	document.triggerEvent = json.value("triggerEvent").toString();
	document.width = std::clamp(json.value("width").toInt(1920), 64, 7680);
	document.height = std::clamp(json.value("height").toInt(1080), 64, 7680);
	document.durationMs = std::clamp(json.value("durationMs").toInt(6000), 250, 120000);
	document.eventDriven = json.value("eventDriven").toBool(false);
	document.externalUrl = json.value("externalUrl").toString();
	document.customHtml = json.value("customHtml").toString();
	document.customCss = json.value("customCss").toString();
	document.customJs = json.value("customJs").toString();
	document.alertBox = json.value("alertBox").toBool(false);
	document.alertVariations = json.value("alertVariations").toArray();
	document.enterAnimation = json.value("enterAnimation").toString("slide-up");
	document.exitAnimation = json.value("exitAnimation").toString("fade");
	document.fontFamily = json.value("fontFamily").toString("Segoe UI");
	document.textToSpeech = json.value("textToSpeech").toBool(false);
	for (const QJsonValue &value : json.value("elements").toArray())
		document.elements.push_back(elementFromJson(value.toObject()));
	document.raw = json;
	document.baseline = projectedDocumentJson(document);
	return document;
}

QJsonObject alertVariation(const QString &id, const QString &name, const QString &event,
			   const QString &headline, const QString &message, const QString &color)
{
	return {{"id", id}, {"name", name}, {"event", event}, {"headline", headline}, {"message", message},
		{"color", color}, {"media", ""}, {"sound", ""}, {"minAmount", 0.0}, {"minViewers", 0}};
}

OverlayDocument alertBoxDocument()
{
	OverlayDocument document;
	document.name = "Alert Box";
	document.eventDriven = true;
	document.durationMs = 6500;
	document.alertBox = true;
	document.alertVariations = {
		alertVariation("follow", "Follow", "audience.followed", "NEW FOLLOWER", "Welcome {user}!", "#22D3EE"),
		alertVariation("subscription", "Subscription", "support.paid_subscription", "NEW SUBSCRIBER", "Thank you, {user}!", "#A855F7"),
		alertVariation("contribution", "Contribution", "support.contribution", "NEW CONTRIBUTION", "{user} sent {amount}", "#F59E0B"),
		alertVariation("raid", "Raid", "audience.raid", "RAID INCOMING", "{user} arrived with {viewers} viewers", "#EC4899"),
	};
	OverlayElement backdrop;
	backdrop.type = "shape";
	backdrop.name = "Alert backdrop";
	backdrop.text.clear();
	backdrop.x = 410;
	backdrop.y = 765;
	backdrop.width = 1100;
	backdrop.height = 230;
	backdrop.background = "#171126";
	backdrop.opacity = .96;
	OverlayElement media;
	media.type = "image";
	media.name = "Alert media";
	media.binding = "__pwAlert.media";
	media.x = 435;
	media.y = 790;
	media.width = 180;
	media.height = 180;
	OverlayElement headline;
	headline.name = "Alert headline";
	headline.binding = "__pwAlert.headline";
	headline.x = 650;
	headline.y = 800;
	headline.width = 800;
	headline.height = 65;
	headline.fontSize = 42;
	headline.color = "#22D3EE";
	OverlayElement message;
	message.name = "Alert message";
	message.binding = "__pwAlert.message";
	message.x = 650;
	message.y = 865;
	message.width = 800;
	message.height = 90;
	message.fontSize = 50;
	document.elements = {backdrop, media, headline, message};
	return document;
}

QByteArray jsonLine(const QJsonObject &json)
{
	return QJsonDocument(json).toJson(QJsonDocument::Compact);
}

QString cssEscaped(QString value)
{
	value.remove(';');
	value.remove('{');
	value.remove('}');
	return value;
}

void applyOverlayPlacement(const char *json)
{
	const QJsonObject command = QJsonDocument::fromJson(QByteArray(json)).object();
	const bool add = command.value("operation").toString() == "add";
	for (const QJsonValue &value : command.value("placements").toArray()) {
		const QJsonObject placement = value.toObject();
		obs_source_t *sceneSource = obs_get_source_by_uuid(placement.value("sceneUuid").toString().toUtf8().constData());
		obs_scene_t *scene = sceneSource ? obs_scene_from_source(sceneSource) : nullptr;
		if (!scene) {
			obs_source_release(sceneSource);
			continue;
		}
		const QByteArray sourceName = placement.value("sourceName").toString().toUtf8();
		if (add) {
			obs_data_t *settings = obs_data_create();
			obs_data_set_string(settings, "url", placement.value("url").toString().toUtf8().constData());
			obs_data_set_int(settings, "width", placement.value("width").toInt());
			obs_data_set_int(settings, "height", placement.value("height").toInt());
			obs_data_set_bool(settings, "reroute_audio", true);
			obs_data_set_int(settings, "webpage_control_level", 0);
			obs_source_t *source = obs_source_create("browser_source", sourceName.constData(), settings, nullptr);
			obs_data_release(settings);
			if (source) {
				obs_scene_add(scene, source);
				obs_source_release(source);
			}
		} else {
			obs_sceneitem_t *item = obs_scene_find_source(scene, sourceName.constData());
			if (item)
				obs_sceneitem_remove(item);
			obs_source_t *source = obs_get_source_by_name(sourceName.constData());
			if (source) {
				obs_source_remove(source);
				obs_source_release(source);
			}
		}
		obs_source_release(sceneSource);
	}
	obs_frontend_save();
}

class CanvasItem final : public QGraphicsRectItem {
public:
	CanvasItem(const OverlayElement &element, std::function<void(const QString &, QPointF)> moved)
		: id(element.id), movedCallback(std::move(moved))
	{
		setRect(0, 0, element.width, element.height);
		setPos(element.x, element.y);
		setRotation(element.rotation);
		setOpacity(element.opacity);
		setVisible(element.visible);
		setBrush(QColor(element.type == "text" ? "#24163A" : element.background));
		setPen(QPen(QColor("#A855F7"), 3));
		setFlag(ItemIsSelectable, true);
		setFlag(ItemIsMovable, !element.locked);
		setFlag(ItemSendsGeometryChanges, true);
		label = new QGraphicsTextItem(this);
		label->setDefaultTextColor(QColor(element.color));
		QFont font = label->font();
		font.setPointSize(std::clamp(element.fontSize / 2, 8, 72));
		font.setBold(element.type == "text");
		label->setFont(font);
		const QString content = element.binding.isEmpty() ? element.text : element.text + "  {{" + element.binding + "}}";
		label->setPlainText(element.type.toUpper() + "  " + content);
		label->setTextWidth(std::max(20.0, element.width - 16.0));
		label->setPos(8, 5);
		setToolTip(element.locked ? "Locked layer" : "Drag to position; use the inspector for size and rotation");
		setData(0, id);
	}

protected:
	QVariant itemChange(GraphicsItemChange change, const QVariant &value) override
	{
		if (change == ItemPositionHasChanged && movedCallback)
			movedCallback(id, value.toPointF());
		return QGraphicsRectItem::itemChange(change, value);
	}

private:
	QString id;
	QGraphicsTextItem *label = nullptr;
	std::function<void(const QString &, QPointF)> movedCallback;
};

} // namespace

struct PulseOverlayRuntime::Impl {
	struct AlertQueue {
		PulseOverlay::AlertLane lane;
		QPointer<QTimer> timer;
		QString activeEventKey;
		QJsonObject activePayload;
		qint64 activeStartedMs = 0;
		int activeDurationMs = 0;
	};
	PulseOverlayRuntime *owner;
	StatusCallback status;
	QString directory = overlayConfigDirectory();
	PulseOverlay::Store repository{directory};
	std::vector<OverlayDocument> documents;
	std::vector<OverlayDocument> publishedDocuments;
	QTcpServer server;
	quint16 port = 18754;
	QString liveCapability;
	QHash<QString, QList<QPointer<QTcpSocket>>> subscribers;
	QHash<QTcpSocket *, QString> subscriberLayouts;
	QHash<QString, AlertQueue> alertQueues;
	bool alertsMuted = false;
	QPointer<QDialog> dialog;
	QString previewSession;
	QCef *previewCef = nullptr;
	QPointer<QCefWidget> previewBrowser;
	QListWidget *documentList = nullptr;
	QListWidget *layers = nullptr;
	QGraphicsScene *scene = nullptr;
	QGraphicsView *view = nullptr;
	QLineEdit *name = nullptr;
	QLineEdit *trigger = nullptr;
	QLineEdit *externalUrl = nullptr;
	QSpinBox *canvasWidth = nullptr;
	QSpinBox *canvasHeight = nullptr;
	QSpinBox *duration = nullptr;
	QCheckBox *eventDriven = nullptr;
	QLineEdit *elementName = nullptr;
	QLineEdit *elementText = nullptr;
	QLineEdit *elementBinding = nullptr;
	QLineEdit *elementAsset = nullptr;
	QLineEdit *elementColor = nullptr;
	QLineEdit *elementBackground = nullptr;
	QDoubleSpinBox *elementX = nullptr;
	QDoubleSpinBox *elementY = nullptr;
	QDoubleSpinBox *elementWidth = nullptr;
	QDoubleSpinBox *elementHeight = nullptr;
	QDoubleSpinBox *elementRotation = nullptr;
	QDoubleSpinBox *elementOpacity = nullptr;
	QSpinBox *elementFont = nullptr;
	QPushButton *visibleButton = nullptr;
	QPushButton *lockedButton = nullptr;
	QComboBox *placementTarget = nullptr;
	QTextEdit *htmlCode = nullptr;
	QTextEdit *cssCode = nullptr;
	QTextEdit *jsCode = nullptr;
	QComboBox *alertVariationSelect = nullptr;
	QLineEdit *alertHeadline = nullptr;
	QLineEdit *alertMessage = nullptr;
	QLineEdit *alertMedia = nullptr;
	QLineEdit *alertSound = nullptr;
	QLineEdit *alertColor = nullptr;
	QDoubleSpinBox *alertMinimumAmount = nullptr;
	QSpinBox *alertMinimumViewers = nullptr;
	QComboBox *alertEnter = nullptr;
	QComboBox *alertExit = nullptr;
	QLineEdit *alertFont = nullptr;
	QCheckBox *alertTts = nullptr;
	QWidget *actionAlertsPage = nullptr;
	QComboBox *actionOverlaySelect = nullptr;
	QComboBox *actionVariationSelect = nullptr;
	QLineEdit *actionUser = nullptr;
	QDoubleSpinBox *actionAmount = nullptr;
	QSpinBox *actionViewers = nullptr;
	QCheckBox *actionMute = nullptr;
	QCheckBox *actionPause = nullptr;
	QLabel *actionStatus = nullptr;
	QLabel *editorStatus = nullptr;
	bool loading = false;

	explicit Impl(PulseOverlayRuntime *owner_, StatusCallback status_) : owner(owner_), status(std::move(status_))
	{
		load();
		loadCapability();
		startServer();
	}

	~Impl()
	{
		// QObject's base destructor runs after this implementation is gone. The
		// server owns sockets whose disconnect handlers capture Impl and access
		// subscriber maps; quiesce them before member destruction clears the maps.
		status = {};
		QObject::disconnect(&server, nullptr, owner, nullptr);
		server.close();
		for (QTcpSocket *socket : server.findChildren<QTcpSocket *>()) {
			QObject::disconnect(socket, nullptr, owner, nullptr);
			socket->abort();
		}
		for (auto &queue : alertQueues) {
			if (!queue.timer) continue;
			queue.timer->stop();
			QObject::disconnect(queue.timer, nullptr, owner, nullptr);
		}
		if (dialog) {
			QObject::disconnect(dialog, nullptr, owner, nullptr);
			if (previewBrowser) previewBrowser->closeBrowser();
			delete dialog;
		}
		delete previewCef;
		previewCef = nullptr;
	}

	void loadCapability()
	{
		const QString path = QDir(directory).filePath("live-render.capability");
		QFile existing(path);
		if (existing.open(QIODevice::ReadOnly))
			liveCapability = QString::fromUtf8(existing.readAll()).trimmed();
		if (liveCapability.size() >= 48)
			return;
		liveCapability = QUuid::createUuid().toString(QUuid::WithoutBraces).remove('-') +
			QUuid::createUuid().toString(QUuid::WithoutBraces).remove('-');
		QSaveFile output(path);
		if (!output.open(QIODevice::WriteOnly) || output.write(liveCapability.toUtf8()) != liveCapability.toUtf8().size() || !output.commit()) {
			if (status)
				status("Overlay render capability could not be persisted; managed source migration is disabled.");
			liveCapability.clear();
		}
	}

	OverlayDocument *currentDocument()
	{
		if (!documentList || documentList->currentRow() < 0 || documentList->currentRow() >= int(documents.size()))
			return nullptr;
		return &documents[size_t(documentList->currentRow())];
	}

	const OverlayDocument *findDocument(const QString &nameOrId) const
	{
		for (const OverlayDocument &document : documents) {
			if (document.id == nameOrId || document.name.compare(nameOrId, Qt::CaseInsensitive) == 0)
				return &document;
		}
		return nullptr;
	}

	const OverlayDocument *findPublishedDocument(const QString &nameOrId) const
	{
		for (const OverlayDocument &document : publishedDocuments) {
			if (document.id == nameOrId || document.name.compare(nameOrId, Qt::CaseInsensitive) == 0)
				return &document;
		}
		return nullptr;
	}

	OverlayDocument *findDocument(const QString &nameOrId)
	{
		return const_cast<OverlayDocument *>(std::as_const(*this).findDocument(nameOrId));
	}

	OverlayElement *currentElement()
	{
		OverlayDocument *document = currentDocument();
		if (!document || !layers || !layers->currentItem())
			return nullptr;
		const QString id = layers->currentItem()->data(Qt::UserRole).toString();
		for (OverlayElement &element : document->elements)
			if (element.id == id)
				return &element;
		return nullptr;
	}

	void seedDefaults()
	{
		OverlayDocument follow;
		follow.name = "Follower Spotlight";
		follow.triggerEvent = "twitch.channel.follow";
		follow.eventDriven = true;
		follow.elements = {
			{QUuid::createUuid().toString(QUuid::WithoutBraces), "shape", "Backdrop", "", "", "", "#FFFFFF", "#321457", 430, 760, 1060, 190, 0, 0.92, 32, true, false},
			{QUuid::createUuid().toString(QUuid::WithoutBraces), "text", "Headline", "NEW FOLLOWER", "", "", "#F0ABFC", "#000000", 540, 790, 840, 55, 0, 1, 32, true, false},
			{QUuid::createUuid().toString(QUuid::WithoutBraces), "text", "Follower name", "Welcome", "user_name", "", "#FFFFFF", "#000000", 540, 845, 840, 75, 0, 1, 50, true, false},
		};
		documents.push_back(follow);

		OverlayDocument starting;
		starting.name = "Starting Soon";
		starting.elements = {
			{QUuid::createUuid().toString(QUuid::WithoutBraces), "shape", "Backdrop", "", "", "", "#FFFFFF", "#12091F", 0, 0, 1920, 1080, 0, 1, 32, true, true},
			{QUuid::createUuid().toString(QUuid::WithoutBraces), "text", "Title", "THE SHOW STARTS SOON", "", "", "#F0ABFC", "#000000", 360, 420, 1200, 160, 0, 1, 72, true, false},
		};
		documents.push_back(starting);

		OverlayDocument vertical;
		vertical.name = "Vertical Alert";
		vertical.width = 1080;
		vertical.height = 1920;
		vertical.triggerEvent = "twitch.channel.raid";
		vertical.eventDriven = true;
		vertical.elements = {
			{QUuid::createUuid().toString(QUuid::WithoutBraces), "shape", "Alert panel", "", "", "", "#FFFFFF", "#5B174F", 90, 1260, 900, 300, 0, 0.94, 32, true, false},
			{QUuid::createUuid().toString(QUuid::WithoutBraces), "text", "Raid", "RAID INCOMING", "user_name", "", "#FFFFFF", "#000000", 150, 1340, 780, 130, 0, 1, 58, true, false},
		};
		documents.push_back(vertical);
	}

	QJsonArray documentArray() const
	{
		QJsonArray result;
		for (const OverlayDocument &document : documents)
			result.append(documentJson(document));
		return result;
	}

	void reloadFromRepository()
	{
		documents.clear();
		for (const QJsonValue &value : repository.draftDocuments())
			documents.push_back(documentFromJson(value.toObject()));
		reloadPublished();
	}

	void reloadPublished()
	{
		publishedDocuments.clear();
		for (const QJsonValue &value : repository.publishedDocuments())
			publishedDocuments.push_back(documentFromJson(value.toObject()));
	}

	void load()
	{
		PulseOverlay::StoreResult result = repository.load();
		if (result.state == PulseOverlay::StoreState::Created) {
			seedDefaults();
			result = repository.initialize(documentArray());
		}
		if (!result.ok()) {
			documents.clear();
			publishedDocuments.clear();
			if (status)
				status("Overlay recovery required: " + result.message);
			return;
		}
		reloadFromRepository();
		if (status && !result.message.isEmpty())
			status(result.message);
	}

	bool save()
	{
		const PulseOverlay::StoreResult result = repository.syncDrafts(documentArray());
		if (!result.ok()) {
			if (status)
				status("Overlay draft save failed: " + result.message);
			return false;
		}
		reloadPublished();
		return true;
	}

	bool publish(const QString &id)
	{
		const PulseOverlay::StoreResult result = repository.publish(id);
		if (!result.ok()) {
			if (status)
				status("Overlay publish failed: " + result.message);
			return false;
		}
		reloadFromRepository();
		const OverlayDocument *document = findPublishedDocument(id);
		if (document) {
			AlertQueue &queue = alertQueues[id];
			if (document->alertBox && queue.lane.isActive())
				queue.lane.markReloadPending();
			else
				emitTo(*document, "pulseweaver.overlay.reload", QJsonObject{{"__pwControl", "reload"}});
		}
		return true;
	}

	QString previewSubscriberKey(const QString &id) const
	{
		return "preview:" + previewSession + ":" + id;
	}

	QString previewUrl(const OverlayDocument &document) const
	{
		const QString layout = document.height > document.width ? "portrait" : "landscape";
		return QString("http://127.0.0.1:%1/preview/%2/%3?layout=%4&revision=%5#token=%2")
			.arg(port).arg(previewSession, document.id, layout)
			.arg(QDateTime::currentMSecsSinceEpoch());
	}

	QString liveUrl(const QString &id, const QString &layout = {}) const
	{
		QString result = QString("http://127.0.0.1:%1/overlay/%2").arg(port).arg(id);
		if (!layout.isEmpty())
			result += "?layout=" + layout;
		if (!liveCapability.isEmpty())
			result += "#token=" + liveCapability;
		return result;
	}

	void migrateManagedSources()
	{
		QSet<QString> ids;
		for (const auto &document : publishedDocuments) ids.insert(document.id);
		if (PulseOverlay::migrateManagedSources(directory, port, liveCapability, ids, status) > 0)
			obs_frontend_save();
	}
	void refreshPreview()
	{
		if (previewBrowser) {
			if (OverlayDocument *document = currentDocument())
				previewBrowser->setURL(previewUrl(*document).toStdString());
		}
	}

	QString pageHtml(const OverlayDocument &document, const QString &layout = "landscape",
			 const QString &eventPath = {}) const
	{
		return PulseOverlay::renderPage(documentJson(document), layout, eventPath);
	}
	void startServer()
	{
		if (!server.listen(QHostAddress::LocalHost, port)) {
			port = 0;
			if (status)
				status("Overlay server could not start: " + server.errorString());
			return;
		}
		QObject::connect(&server, &QTcpServer::newConnection, owner, [this] {
			while (QTcpSocket *socket = server.nextPendingConnection()) {
				QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
				QObject::connect(socket, &QTcpSocket::readyRead, owner, [this, socket] { handleRequest(socket); });
				QTimer::singleShot(5000, socket, [socket] {
					if (socket->state() == QAbstractSocket::ConnectedState && socket->property("pwRequestPending").toBool())
						socket->disconnectFromHost();
				});
				socket->setProperty("pwRequestPending", true);
			}
		});
	}

	int subscriberCount() const
	{
		int count = 0;
		for (const auto &clients : subscribers)
			count += clients.size();
		return count;
	}

	bool hasConnectedSubscriber(const QString &key) const
	{
		for (const QPointer<QTcpSocket> &socket : subscribers.value(key))
			if (socket && socket->state() == QAbstractSocket::ConnectedState)
				return true;
		return false;
	}

	void sendActiveSnapshot(const QString &id, QTcpSocket *socket)
	{
		const AlertQueue &queue = alertQueues[id];
		if (!socket || !queue.lane.isActive() || queue.activePayload.isEmpty())
			return;
		QJsonObject payload = queue.activePayload;
		QJsonObject alert = payload.value("__pwAlert").toObject();
		alert.insert("muted", true);
		const int elapsed = int(QDateTime::currentMSecsSinceEpoch() - queue.activeStartedMs);
		alert.insert("remainingMs", std::max(250, queue.activeDurationMs - elapsed));
		payload.insert("__pwAlert", alert);
		socket->write("data: " + jsonLine(QJsonObject{{"event", queue.activeEventKey}, {"data", payload}}) + "\n\n");
	}

	static QHash<QByteArray, QByteArray> requestHeaders(const QByteArray &request)
	{
		QHash<QByteArray, QByteArray> headers;
		const QList<QByteArray> lines = request.split('\n');
		for (qsizetype index = 1; index < lines.size(); ++index) {
			const QByteArray line = lines[index].trimmed();
			const qsizetype colon = line.indexOf(':');
			if (colon > 0)
				headers.insert(line.left(colon).trimmed().toLower(), line.mid(colon + 1).trimmed());
		}
		return headers;
	}

	bool validHostAndOrigin(const QHash<QByteArray, QByteArray> &headers) const
	{
		const QByteArray authority = "127.0.0.1:" + QByteArray::number(port);
		if (headers.value("host") != authority)
			return false;
		const QByteArray origin = headers.value("origin");
		return origin.isEmpty() || origin == "http://" + authority;
	}

	static bool authorized(const QHash<QByteArray, QByteArray> &headers, const QString &capability)
	{
		return !capability.isEmpty() && headers.value("authorization") == "Bearer " + capability.toUtf8();
	}

	static QString bootstrapHtml()
	{
		return PulseOverlay::renderBootstrap();
	}
	void handleRequest(QTcpSocket *socket)
	{
		QByteArray request = socket->property("pwRequestBuffer").toByteArray();
		request += socket->readAll();
		if (request.size() > 16384) {
			respond(socket, 400, "text/plain", "Request headers too large");
			return;
		}
		if (!request.contains("\r\n\r\n")) {
			socket->setProperty("pwRequestBuffer", request);
			return;
		}
		socket->setProperty("pwRequestBuffer", {});
		socket->setProperty("pwRequestPending", false);
		const QList<QByteArray> first = request.left(request.indexOf("\r\n")).split(' ');
		const QHash<QByteArray, QByteArray> headers = requestHeaders(request);
		if (first.size() != 3 || first[0] != "GET") {
			respond(socket, 400, "text/plain", "Invalid request");
			return;
		}
		if (!validHostAndOrigin(headers)) {
			respond(socket, 403, "text/plain", "Forbidden");
			return;
		}
		const QUrl requestUrl = QUrl::fromEncoded(first[1]);
		const QString path = requestUrl.path();
		const QStringList parts = path.split('/', Qt::SkipEmptyParts);
		if (parts.size() == 3 && parts[0] == "overlay" && parts[2] == "events") {
			const OverlayDocument *document = findPublishedDocument(parts[1]);
			if (!document) {
				respond(socket, 404, "text/plain", "Overlay not found");
				return;
			}
			if (!authorized(headers, liveCapability)) {
				respond(socket, 401, "text/plain", "Reconnect this managed overlay source");
				return;
			}
			if (subscriberCount() >= 32) {
				respond(socket, 503, "text/plain", "Too many renderer connections");
				return;
			}
			socket->write("HTTP/1.1 200 OK\r\nContent-Type: text/event-stream\r\nCache-Control: no-store\r\nReferrer-Policy: no-referrer\r\nX-Content-Type-Options: nosniff\r\nConnection: keep-alive\r\n\r\n: connected\n\n");
			subscribers[document->id].append(socket);
			subscriberLayouts.insert(socket, QUrlQuery(requestUrl).queryItemValue("layout").toLower() == "portrait" ? "portrait" : "landscape");
			QObject::connect(socket, &QTcpSocket::disconnected, owner, [this, id = document->id, socket] {
				subscribers[id].removeAll(socket);
				subscriberLayouts.remove(socket);
				socket->deleteLater();
			});
			if (document->alertBox) {
				if (alertQueues[document->id].lane.isActive())
					sendActiveSnapshot(document->id, socket);
				else
					QTimer::singleShot(0, owner, [this, id = document->id] { startNext(id); });
			}
			return;
		}
		if (parts.size() == 4 && parts[0] == "preview" && parts[1] == previewSession && parts[3] == "events") {
			const OverlayDocument *document = findDocument(parts[2]);
			if (!document || !dialog) {
				respond(socket, 404, "text/plain", "Preview not found");
				return;
			}
			if (!authorized(headers, previewSession)) {
				respond(socket, 401, "text/plain", "Preview capability expired");
				return;
			}
			if (subscriberCount() >= 32) {
				respond(socket, 503, "text/plain", "Too many renderer connections");
				return;
			}
			const QString key = previewSubscriberKey(document->id);
			socket->write("HTTP/1.1 200 OK\r\nContent-Type: text/event-stream\r\nCache-Control: no-cache\r\nConnection: keep-alive\r\n\r\n: connected\n\n");
			subscribers[key].append(socket);
			subscriberLayouts.insert(socket, QUrlQuery(requestUrl).queryItemValue("layout").toLower() == "portrait" ? "portrait" : "landscape");
			QObject::connect(socket, &QTcpSocket::disconnected, owner, [this, key, socket] {
				subscribers[key].removeAll(socket);
				subscriberLayouts.remove(socket);
				socket->deleteLater();
			});
			return;
		}
		if (parts.size() == 3 && parts[0] == "overlay" && parts[2] == "render") {
			const OverlayDocument *document = findPublishedDocument(parts[1]);
			if (!document) {
				respond(socket, 404, "text/plain", "Overlay not found");
				return;
			}
			if (!authorized(headers, liveCapability)) {
				respond(socket, 401, "text/plain", "Reconnect this managed overlay source");
				return;
			}
			const QString layout = QUrlQuery(requestUrl).queryItemValue("layout").toLower() == "portrait" ? "portrait" : "landscape";
			respond(socket, 200, "text/html; charset=utf-8", pageHtml(*document, layout).toUtf8());
			return;
		}
		if (parts.size() == 2 && parts[0] == "overlay") {
			if (!findPublishedDocument(parts[1])) {
				respond(socket, 404, "text/plain", "Overlay not found");
				return;
			}
			respond(socket, 200, "text/html; charset=utf-8", bootstrapHtml().toUtf8());
			return;
		}
		if (parts.size() == 3 && parts[0] == "preview" && parts[1] == previewSession) {
			const OverlayDocument *document = findDocument(parts[2]);
			if (!document || !dialog) {
				respond(socket, 404, "text/plain", "Preview not found");
				return;
			}
			const QString layout = QUrlQuery(requestUrl).queryItemValue("layout").toLower() == "portrait" ? "portrait" : "landscape";
			const QString eventPath = QString("/preview/%1/%2/events").arg(previewSession, document->id);
			respond(socket, 200, "text/html; charset=utf-8", pageHtml(*document, layout, eventPath).toUtf8());
			return;
		}
		respond(socket, 404, "text/plain", "Not found");
	}

	static void respond(QTcpSocket *socket, int code, const QByteArray &contentType, const QByteArray &body)
	{
		const QByteArray reason = code == 200 ? "OK" : code == 401 ? "Unauthorized" : code == 403 ? "Forbidden" : code == 404 ? "Not Found" : code == 503 ? "Service Unavailable" : "Bad Request";
		socket->write("HTTP/1.1 " + QByteArray::number(code) + " " + reason + "\r\nContent-Type: " + contentType + "\r\nCache-Control: no-store\r\nReferrer-Policy: no-referrer\r\nX-Content-Type-Options: nosniff\r\nConnection: close\r\nContent-Length: " + QByteArray::number(body.size()) + "\r\n\r\n" + body);
		socket->disconnectFromHost();
	}

	void emitToKey(const QString &key, const QString &eventKey, const QJsonObject &data)
	{
		auto &clients = subscribers[key];
		QTcpSocket *audioOwner = nullptr;
		if (data.contains("__pwAlert") && !data.value("__pwAlert").toObject().value("muted").toBool()) {
			for (auto it = clients.crbegin(); it != clients.crend(); ++it) {
				if (*it && (*it)->state() == QAbstractSocket::ConnectedState && subscriberLayouts.value(*it) == "landscape") {
					audioOwner = *it;
					break;
				}
			}
			if (!audioOwner) {
				for (auto it = clients.crbegin(); it != clients.crend(); ++it)
					if (*it && (*it)->state() == QAbstractSocket::ConnectedState) {
						audioOwner = *it;
						break;
					}
			}
		}
		for (auto it = clients.begin(); it != clients.end();) {
			if (!*it || (*it)->state() != QAbstractSocket::ConnectedState) {
				if (*it)
					subscriberLayouts.remove(*it);
				it = clients.erase(it);
			} else if ((*it)->bytesToWrite() > 1024 * 1024) {
				(*it)->disconnectFromHost();
				it = clients.erase(it);
			} else {
				QJsonObject clientData = data;
				if (clientData.contains("__pwAlert") && *it != audioOwner) {
					QJsonObject alert = clientData.value("__pwAlert").toObject();
					alert.insert("muted", true);
					clientData.insert("__pwAlert", alert);
				}
				const QByteArray message = "data: " + jsonLine(QJsonObject{{"event", eventKey}, {"data", clientData}}) + "\n\n";
				(*it)->write(message);
				++it;
			}
		}
	}

	void emitTo(const OverlayDocument &document, const QString &eventKey, const QJsonObject &data)
	{
		emitToKey(document.id, eventKey, data);
	}

	QJsonObject matchingVariation(const OverlayDocument &document, const QString &eventKey,
				      const QJsonObject &data, bool *matched = nullptr) const
	{
		return PulseOverlay::matchAlertVariation(document.alertVariations, eventKey, data, matched);
	}

	QJsonObject alertPayload(const OverlayDocument &document, const QString &eventKey, QJsonObject data) const
	{
		bool matched = false;
		const QJsonObject variation = matchingVariation(document, eventKey, data, &matched);
		if (!matched)
			return {};
		data.insert("__pwAlert", QJsonObject{
			{"variationId", variation.value("id")},
			{"headline", PulseOverlay::expandAlertTemplate(variation.value("headline").toString(), data)},
			{"message", PulseOverlay::expandAlertTemplate(variation.value("message").toString(), data)},
			{"media", variation.value("media")}, {"sound", variation.value("sound")},
			{"color", variation.value("color").toString("#22D3EE")},
			{"muted", alertsMuted}, {"tts", document.textToSpeech},
		});
		return data;
	}

	bool acceptsEvent(const OverlayDocument &document, const QString &eventKey, const QJsonObject &data) const
	{
		if (!document.alertBox)
			return document.triggerEvent == eventKey || eventKey == "pulseweaver.overlay.trigger";
		bool matched = false;
		matchingVariation(document, eventKey, data, &matched);
		return matched;
	}

	void startNext(const QString &id)
	{
		AlertQueue &queue = alertQueues[id];
		const OverlayDocument *document = findPublishedDocument(id);
		if (!document) {
			queue.lane.clearPending();
			return;
		}
		if (!hasConnectedSubscriber(id))
			return;
		const std::optional<PulseOverlay::AlertEvent> next = queue.lane.beginNext();
		if (!next)
			return;
		const QJsonObject payload = alertPayload(*document, next->eventKey, next->data);
		if (payload.isEmpty()) {
			queue.lane.completeCurrent();
			QTimer::singleShot(0, owner, [this, id] { startNext(id); });
			return;
		}
		queue.activeEventKey = next->eventKey;
		queue.activePayload = payload;
		queue.activeStartedMs = QDateTime::currentMSecsSinceEpoch();
		queue.activeDurationMs = document->durationMs;
		emitTo(*document, next->eventKey, payload);
		if (!queue.timer) {
			queue.timer = new QTimer(owner);
			queue.timer->setSingleShot(true);
			QObject::connect(queue.timer, &QTimer::timeout, owner, [this, id] {
				AlertQueue &current = alertQueues[id];
				if (const OverlayDocument *published = findPublishedDocument(id))
					emitTo(*published, "pulseweaver.overlay.complete", QJsonObject{{"__pwControl", "complete"}});
				current.lane.completeCurrent();
				current.activeEventKey.clear();
				current.activePayload = {};
				if (current.lane.takeReloadPending()) {
					if (const OverlayDocument *published = findPublishedDocument(id))
						emitTo(*published, "pulseweaver.overlay.reload", QJsonObject{{"__pwControl", "reload"}});
					QTimer::singleShot(400, owner, [this, id] { startNext(id); });
				} else
					startNext(id);
			});
		}
		queue.timer->start(std::clamp(document->durationMs + 700, 950, 120700));
	}

	bool enqueue(const OverlayDocument &document, const QString &eventKey, const QJsonObject &data)
	{
		if (QJsonDocument(data).toJson(QJsonDocument::Compact).size() > 256 * 1024) {
			if (status)
				status("Oversized overlay event was rejected.");
			return false;
		}
		if (!acceptsEvent(document, eventKey, data))
			return false;
		if (!document.alertBox) {
			emitTo(document, eventKey, data);
			return true;
		}
		AlertQueue &queue = alertQueues[document.id];
		if (!queue.lane.enqueue({eventKey, data})) {
			if (status)
				status("Alert queue is full for " + document.name + "; newest alert was rejected.");
			return false;
		}
		startNext(document.id);
		return true;
	}

	bool skip(const QString &id)
	{
		AlertQueue &queue = alertQueues[id];
		if (!queue.lane.isActive())
			return false;
		if (queue.timer)
			queue.timer->stop();
		queue.lane.skipCurrent();
		queue.activeEventKey.clear();
		queue.activePayload = {};
		const OverlayDocument *document = findPublishedDocument(id);
		if (document)
			emitTo(*document, "pulseweaver.overlay.skip", QJsonObject{{"__pwControl", "skip"}});
		if (queue.lane.takeReloadPending()) {
			if (document)
				emitTo(*document, "pulseweaver.overlay.reload", QJsonObject{{"__pwControl", "reload"}});
			QTimer::singleShot(400, owner, [this, id] { startNext(id); });
		} else
			startNext(id);
		return true;
	}

	void setPaused(const QString &id, bool paused)
	{
		AlertQueue &queue = alertQueues[id];
		queue.lane.setPaused(paused);
		if (!paused)
			startNext(id);
	}

	void stopAndClear(const QString &id)
	{
		AlertQueue &queue = alertQueues[id];
		queue.lane.clearPending();
		if (queue.lane.isActive())
			skip(id);
	}

	void setMuted(bool muted)
	{
		alertsMuted = muted;
		for (const OverlayDocument &document : publishedDocuments)
			if (document.alertBox)
				emitTo(document, "pulseweaver.overlay.mute", QJsonObject{{"__pwControl", "mute"}, {"muted", muted}});
	}

	void refreshDocumentList(int preferred = -1)
	{
		if (!documentList)
			return;
		loading = true;
		const int row = preferred >= 0 ? preferred : std::max(0, documentList->currentRow());
		documentList->clear();
		for (const OverlayDocument &document : documents) {
			const bool published = repository.isDraftPublished(document.id);
			auto *item = new QListWidgetItem(document.name + (published ? QString() : QStringLiteral("  • draft")), documentList);
			item->setData(Qt::UserRole, document.id);
		}
		if (!documents.empty())
			documentList->setCurrentRow(std::min(row, int(documents.size()) - 1));
		loading = false;
		loadDocument();
	}

	void loadDocument()
	{
		OverlayDocument *document = currentDocument();
		if (!document)
			return;
		loading = true;
		name->setText(document->name);
		trigger->setText(document->triggerEvent);
		externalUrl->setText(document->externalUrl);
		canvasWidth->setValue(document->width);
		canvasHeight->setValue(document->height);
		duration->setValue(document->durationMs);
		eventDriven->setChecked(document->eventDriven);
		htmlCode->setPlainText(document->customHtml);
		cssCode->setPlainText(document->customCss);
		jsCode->setPlainText(document->customJs);
		for (QSpinBox *field : {canvasWidth, canvasHeight, duration})
			field->setProperty("pwLoadedValue", field->value());
		loading = false;
		loadAlertFields();
		refreshCanvas();
		refreshPreview();
		if (editorStatus)
			editorStatus->setText(QString("%1  •  PUBLISHED URL  http://127.0.0.1:%2/overlay/%3")
						      .arg(repository.isDraftPublished(document->id) ? "DRAFT MATCHES PUBLISHED" : "UNPUBLISHED DRAFT")
						      .arg(port).arg(document->id));
	}

	void refreshCanvas(const QString &selectId = {})
	{
		OverlayDocument *document = currentDocument();
		if (!document || !scene)
			return;
		loading = true;
		scene->clear();
		scene->setSceneRect(0, 0, document->width, document->height);
		layers->clear();
		for (auto it = document->elements.rbegin(); it != document->elements.rend(); ++it) {
			auto *layer = new QListWidgetItem((it->visible ? "◉  " : "○  ") + it->name, layers);
			layer->setData(Qt::UserRole, it->id);
			if (it->locked)
				layer->setForeground(QColor("#778399"));
		}
		for (OverlayElement &element : document->elements) {
			auto *item = new CanvasItem(element, [this](const QString &id, QPointF position) {
				if (loading)
					return;
				if (OverlayDocument *document = currentDocument()) {
					for (OverlayElement &entry : document->elements) {
						if (entry.id == id) {
							const QPointF before(entry.x, entry.y);
							entry.x = position.x();
							entry.y = position.y();
							if (currentElement() && currentElement()->id == id) {
								elementX->setValue(entry.x);
								elementY->setValue(entry.y);
							}
							if (!save()) {
								entry.x = before.x();
								entry.y = before.y();
							} else
								refreshPreview();
							break;
						}
					}
				}
			});
			scene->addItem(item);
			if (element.id == selectId)
				item->setSelected(true);
		}
		if (view)
			view->fitInView(scene->sceneRect(), Qt::KeepAspectRatio);
		if (!selectId.isEmpty()) {
			for (int row = 0; row < layers->count(); ++row)
				if (layers->item(row)->data(Qt::UserRole).toString() == selectId)
					layers->setCurrentRow(row);
		}
		loading = false;
		loadElement();
	}

	void loadElement()
	{
		OverlayElement *element = currentElement();
		loading = true;
		const QList<QWidget *> elementFields{elementName, elementText, elementBinding, elementAsset,
						 elementColor, elementBackground, elementX, elementY, elementWidth, elementHeight,
						 elementRotation, elementOpacity, elementFont, visibleButton, lockedButton};
		for (QWidget *field : elementFields)
			field->setEnabled(element != nullptr);
		if (element) {
			elementName->setText(element->name);
			elementText->setText(element->text);
			elementBinding->setText(element->binding);
			elementAsset->setText(element->asset);
			elementColor->setText(element->color);
			elementBackground->setText(element->background);
			elementX->setValue(element->x);
			elementY->setValue(element->y);
			elementWidth->setValue(element->width);
			elementHeight->setValue(element->height);
			elementRotation->setValue(element->rotation);
			elementOpacity->setValue(element->opacity);
			elementFont->setValue(element->fontSize);
			for (QDoubleSpinBox *field : {elementX, elementY, elementWidth, elementHeight, elementRotation, elementOpacity})
				field->setProperty("pwLoadedValue", field->value());
			elementFont->setProperty("pwLoadedValue", elementFont->value());
			visibleButton->setText(element->visible ? "◉  VISIBLE" : "○  HIDDEN");
			lockedButton->setText(element->locked ? "🔒  LOCKED" : "🔓  EDITABLE");
		}
		loading = false;
	}

	void loadAlertFields()
	{
		if (!alertVariationSelect)
			return;
		OverlayDocument *document = currentDocument();
		const bool enabled = document && document->alertBox && !document->alertVariations.isEmpty();
		loading = true;
		const QString selected = alertVariationSelect->currentData().toString();
		alertVariationSelect->clear();
		if (document) {
			for (const QJsonValue &value : document->alertVariations) {
				const QJsonObject variation = value.toObject();
				alertVariationSelect->addItem(variation.value("name").toString() + "  ·  " + variation.value("event").toString(),
							      variation.value("id").toString());
			}
		}
		const int selectedIndex = alertVariationSelect->findData(selected);
		if (selectedIndex >= 0)
			alertVariationSelect->setCurrentIndex(selectedIndex);
		const QList<QWidget *> alertFields{alertVariationSelect, alertHeadline, alertMessage, alertMedia,
						 alertSound, alertColor, alertMinimumAmount, alertMinimumViewers,
						 alertEnter, alertExit, alertFont, alertTts};
		for (QWidget *widget : alertFields)
			widget->setEnabled(enabled);
		if (enabled) {
			const int index = std::max(0, alertVariationSelect->currentIndex());
			const QJsonObject variation = document->alertVariations.at(index).toObject();
			alertHeadline->setText(variation.value("headline").toString());
			alertMessage->setText(variation.value("message").toString());
			alertMedia->setText(variation.value("media").toString());
			alertSound->setText(variation.value("sound").toString());
			alertColor->setText(variation.value("color").toString());
			alertMinimumAmount->setValue(variation.value("minAmount").toDouble());
			alertMinimumViewers->setValue(variation.value("minViewers").toInt());
			alertEnter->setCurrentText(document->enterAnimation);
			alertExit->setCurrentText(document->exitAnimation);
			alertFont->setText(document->fontFamily);
			alertTts->setChecked(document->textToSpeech);
		}
		loading = false;
	}

	void refreshActionVariations()
	{
		if (!actionVariationSelect || !actionOverlaySelect)
			return;
		const QString selected = actionVariationSelect->currentData().toString();
		actionVariationSelect->clear();
		const OverlayDocument *document = findPublishedDocument(actionOverlaySelect->currentData().toString());
		if (document) {
			for (const QJsonValue &value : document->alertVariations) {
				const QJsonObject variation = value.toObject();
				actionVariationSelect->addItem(variation.value("name").toString(), variation.value("event").toString());
			}
		}
		const int selectedIndex = actionVariationSelect->findData(selected);
		if (selectedIndex >= 0)
			actionVariationSelect->setCurrentIndex(selectedIndex);
		if (actionPause) {
			const QSignalBlocker blocked(actionPause);
			actionPause->setChecked(document && alertQueues[document->id].lane.isPaused());
		}
	}

	void refreshActionControls()
	{
		if (!actionOverlaySelect)
			return;
		const QString selected = actionOverlaySelect->currentData().toString();
		actionOverlaySelect->clear();
		for (const OverlayDocument &document : publishedDocuments) {
			if (document.alertBox)
				actionOverlaySelect->addItem(document.name, document.id);
		}
		const int selectedIndex = actionOverlaySelect->findData(selected);
		if (selectedIndex >= 0)
			actionOverlaySelect->setCurrentIndex(selectedIndex);
		refreshActionVariations();
		const bool enabled = actionOverlaySelect->count() > 0;
		if (actionVariationSelect)
			actionVariationSelect->setEnabled(enabled);
		if (actionStatus)
			actionStatus->setText(enabled ? "Ready. Manual tests enter the same host-owned queue as live events."
						    : "Publish an Alert Box in Overlay Designer to enable live controls.");
	}

	void mountActionControls(QWidget *mainWindow)
	{
		if (actionAlertsPage || !mainWindow)
			return;
		auto *tabs = mainWindow->findChild<QTabWidget *>("PulseWeaverPlatformTabs");
		if (!tabs)
			return;
		actionAlertsPage = new QWidget(tabs);
		actionAlertsPage->setObjectName("PulseWeaverAlertActions");
		auto *layout = new QVBoxLayout(actionAlertsPage);
		auto *title = new QLabel("ALERT BOX · LIVE CONTROLS", actionAlertsPage);
		title->setObjectName("Kicker");
		layout->addWidget(title);
		auto *copy = new QLabel("Trigger, skip, and mute published alerts during a show. Alert design stays in Camera → Overlay Designer.", actionAlertsPage);
		copy->setWordWrap(true);
		copy->setObjectName("Muted");
		layout->addWidget(copy);

		auto *form = new QFormLayout;
		actionOverlaySelect = new QComboBox(actionAlertsPage);
		actionVariationSelect = new QComboBox(actionAlertsPage);
		actionUser = new QLineEdit("PulseTester", actionAlertsPage);
		actionAmount = new QDoubleSpinBox(actionAlertsPage);
		actionAmount->setRange(0, 1000000000.0);
		actionAmount->setDecimals(2);
		actionAmount->setValue(5);
		actionViewers = new QSpinBox(actionAlertsPage);
		actionViewers->setRange(0, 1000000000);
		actionViewers->setValue(42);
		form->addRow("Published Alert Box", actionOverlaySelect);
		form->addRow("Variation", actionVariationSelect);
		form->addRow("Viewer name", actionUser);
		form->addRow("Amount", actionAmount);
		form->addRow("Raid viewers", actionViewers);
		layout->addLayout(form);

		auto *buttons = new QHBoxLayout;
		auto *triggerButton = new QPushButton("TRIGGER ALERT", actionAlertsPage);
		auto *skipButton = new QPushButton("SKIP CURRENT", actionAlertsPage);
		auto *clearButton = new QPushButton("STOP + CLEAR", actionAlertsPage);
		buttons->addWidget(triggerButton);
		buttons->addWidget(skipButton);
		buttons->addWidget(clearButton);
		layout->addLayout(buttons);
		actionMute = new QCheckBox("MUTE ALERT AUDIO", actionAlertsPage);
		actionMute->setChecked(alertsMuted);
		layout->addWidget(actionMute);
		actionPause = new QCheckBox("PAUSE AFTER CURRENT ALERT", actionAlertsPage);
		layout->addWidget(actionPause);
		actionStatus = new QLabel(actionAlertsPage);
		actionStatus->setWordWrap(true);
		actionStatus->setObjectName("Muted");
		layout->addWidget(actionStatus);
		layout->addStretch();
		tabs->addTab(actionAlertsPage, "ALERTS");

		QObject::connect(actionOverlaySelect, &QComboBox::currentIndexChanged, owner,
				 [this] { refreshActionVariations(); });
		QObject::connect(triggerButton, &QPushButton::clicked, owner, [this] {
			const OverlayDocument *document = findPublishedDocument(actionOverlaySelect->currentData().toString());
			if (!document || actionVariationSelect->currentIndex() < 0)
				return;
			const QString event = actionVariationSelect->currentData().toString();
			const QJsonObject data{{"user", actionUser->text()}, {"user_name", actionUser->text()},
				{"amount", actionAmount->value()}, {"viewers", actionViewers->value()}, {"platform", "manual"}};
			const bool accepted = enqueue(*document, event, data);
			actionStatus->setText(accepted ? "Alert queued." : "Alert did not meet this variation's filters.");
		});
		QObject::connect(skipButton, &QPushButton::clicked, owner, [this] {
			const bool skipped = skip(actionOverlaySelect->currentData().toString());
			actionStatus->setText(skipped ? "Current alert skipped; queue advanced." : "No alert is currently active.");
		});
		QObject::connect(clearButton, &QPushButton::clicked, owner, [this] {
			const QString id = actionOverlaySelect->currentData().toString();
			stopAndClear(id);
			actionStatus->setText("Current alert stopped and pending alerts cleared.");
		});
		QObject::connect(actionMute, &QCheckBox::toggled, owner, [this](bool muted) {
			setMuted(muted);
			actionStatus->setText(muted ? "Alert audio muted. Visual alerts remain active." : "Alert audio enabled.");
		});
		QObject::connect(actionPause, &QCheckBox::toggled, owner, [this](bool paused) {
			setPaused(actionOverlaySelect->currentData().toString(), paused);
			actionStatus->setText(paused ? "Queue paused after the current alert." : "Queue resumed.");
		});
		refreshActionControls();
	}

	void loadSelectedAlertVariation()
	{
		if (loading)
			return;
		OverlayDocument *document = currentDocument();
		if (!document || !document->alertBox)
			return;
		const int index = alertVariationSelect->currentIndex();
		if (index < 0 || index >= document->alertVariations.size())
			return;
		loading = true;
		const QJsonObject variation = document->alertVariations.at(index).toObject();
		alertHeadline->setText(variation.value("headline").toString());
		alertMessage->setText(variation.value("message").toString());
		alertMedia->setText(variation.value("media").toString());
		alertSound->setText(variation.value("sound").toString());
		alertColor->setText(variation.value("color").toString());
		alertMinimumAmount->setValue(variation.value("minAmount").toDouble());
		alertMinimumViewers->setValue(variation.value("minViewers").toInt());
		loading = false;
	}

	void saveAlertFields()
	{
		if (loading)
			return;
		OverlayDocument *document = currentDocument();
		if (!document || !document->alertBox)
			return;
		const int index = alertVariationSelect->currentIndex();
		if (index < 0 || index >= document->alertVariations.size())
			return;
		const OverlayDocument before = *document;
		QJsonObject variation = document->alertVariations.at(index).toObject();
		variation.insert("headline", alertHeadline->text());
		variation.insert("message", alertMessage->text());
		variation.insert("media", alertMedia->text().trimmed());
		variation.insert("sound", alertSound->text().trimmed());
		variation.insert("color", alertColor->text().trimmed().isEmpty() ? "#22D3EE" : alertColor->text().trimmed());
		variation.insert("minAmount", alertMinimumAmount->value());
		variation.insert("minViewers", alertMinimumViewers->value());
		document->alertVariations.replace(index, variation);
		document->enterAnimation = alertEnter->currentText();
		document->exitAnimation = alertExit->currentText();
		document->fontFamily = alertFont->text().trimmed().isEmpty() ? "Segoe UI" : alertFont->text().trimmed();
		document->textToSpeech = alertTts->isChecked();
		if (!save())
			*document = before;
		refreshDocumentList(documentList->currentRow());
	}

	void saveDocumentFields(QWidget *field = nullptr)
	{
		if (loading)
			return;
		if (auto *spin = qobject_cast<QSpinBox *>(field))
			if (spin->property("pwLoadedValue").toInt() == spin->value()) return;
		OverlayDocument *document = currentDocument();
		if (!document)
			return;
		const OverlayDocument before = *document;
		if (field == name) document->name = name->text().trimmed().isEmpty() ? "Untitled Overlay" : name->text().trimmed();
		if (field == trigger) document->triggerEvent = trigger->text().trimmed();
		if (field == externalUrl) document->externalUrl = externalUrl->text().trimmed();
		if (field == canvasWidth) document->width = canvasWidth->value();
		if (field == canvasHeight) document->height = canvasHeight->value();
		if (field == duration) document->durationMs = duration->value();
		if (field == eventDriven) document->eventDriven = eventDriven->isChecked();
		if (!field) document->customHtml = htmlCode->toPlainText();
		if (!field) document->customCss = cssCode->toPlainText();
		if (!field) document->customJs = jsCode->toPlainText();
		if (!save())
			*document = before;
		refreshDocumentList(documentList->currentRow());
	}

	void saveElementFields(QWidget *field = nullptr)
	{
		if (loading)
			return;
		if (auto *spin = qobject_cast<QDoubleSpinBox *>(field))
			if (spin->property("pwLoadedValue").toDouble() == spin->value()) return;
		if (field == elementFont && elementFont->property("pwLoadedValue").toInt() == elementFont->value()) return;
		OverlayElement *element = currentElement();
		if (!element)
			return;
		const QString id = element->id;
		const OverlayElement before = *element;
		if (field == elementName) element->name = elementName->text().trimmed().isEmpty() ? element->type.toUpper() : elementName->text().trimmed();
		if (field == elementText) element->text = elementText->text();
		if (field == elementBinding) element->binding = elementBinding->text().trimmed();
		if (field == elementAsset) element->asset = elementAsset->text().trimmed();
		if (field == elementColor) element->color = elementColor->text().trimmed();
		if (field == elementBackground) element->background = elementBackground->text().trimmed();
		if (field == elementX) element->x = elementX->value();
		if (field == elementY) element->y = elementY->value();
		if (field == elementWidth) element->width = elementWidth->value();
		if (field == elementHeight) element->height = elementHeight->value();
		if (field == elementRotation) element->rotation = elementRotation->value();
		if (field == elementOpacity) element->opacity = elementOpacity->value();
		if (field == elementFont) element->fontSize = elementFont->value();
		if (!save())
			*element = before;
		refreshCanvas(id);
		refreshPreview();
	}

	void addElement(const QString &type)
	{
		OverlayDocument *document = currentDocument();
		if (!document)
			return;
		OverlayElement element;
		element.type = type;
		element.name = type.left(1).toUpper() + type.mid(1);
		if (type == "shape") {
			element.text.clear();
			element.width = 600;
			element.height = 250;
		} else if (type == "progress") {
			element.text.clear();
			element.binding = "progress";
			element.width = 700;
			element.height = 70;
		} else if (type == "image" || type == "video") {
			element.text = "Set an HTTPS or data URL in the inspector";
			element.width = 640;
			element.height = 360;
		}
		document->elements.push_back(element);
		if (!save())
			document->elements.pop_back();
		refreshCanvas(element.id);
	}

	void createEditor()
	{
		dialog = new QDialog(static_cast<QWidget *>(obs_frontend_get_main_window()));
		dialog->setAttribute(Qt::WA_DeleteOnClose);
		dialog->setWindowTitle("Pulse Weaver — Overlay Designer");
		dialog->resize(1560, 900);
		previewSession = QUuid::createUuid().toString(QUuid::WithoutBraces);
		// Overlay editing follows the active application theme.
		auto *root = new QVBoxLayout(dialog);
		auto *head = new QHBoxLayout;
		auto *title = new QLabel("OVERLAY DESIGNER  •  DRAFT, PREVIEW, PUBLISH");
		title->setObjectName("PulseWeaverHeading");
		head->addWidget(title);
		head->addStretch();
		auto *test = new QPushButton("TEST DRAFT PREVIEW");
		auto *publishButton = new QPushButton("PUBLISH DRAFT");
		auto *browser = new QPushButton("OPEN PUBLISHED URL");
		placementTarget = new QComboBox;
		placementTarget->addItem("Landscape", "landscape");
		placementTarget->addItem("Portrait", "portrait");
		placementTarget->addItem("Both", "both");
		auto *addScene = new QPushButton("ADD TO CAMERA SCENE");
		head->addWidget(test);
		head->addWidget(publishButton);
		head->addWidget(browser);
		head->addWidget(placementTarget);
		head->addWidget(addScene);
		root->addLayout(head);

		auto *splitter = new QSplitter;
		auto *left = new QWidget;
		auto *leftLayout = new QVBoxLayout(left);
		leftLayout->addWidget(new QLabel("OVERLAYS"));
		documentList = new QListWidget;
		leftLayout->addWidget(documentList, 1);
		auto *documentButtons = new QHBoxLayout;
		for (const QString &label : {QString("+"), QString("DUP"), QString("−")}) {
			auto *button = new QPushButton(label);
			documentButtons->addWidget(button);
			if (label == "+")
				QObject::connect(button, &QPushButton::clicked, dialog, [this] {
					OverlayDocument document;
					document.name = "New Overlay";
					document.elements.push_back(OverlayElement{});
					documents.push_back(document);
					if (!save())
						documents.pop_back();
					refreshDocumentList(int(documents.size()) - 1);
				});
			else if (label == "DUP")
				QObject::connect(button, &QPushButton::clicked, dialog, [this] {
					if (OverlayDocument *current = currentDocument()) {
						OverlayDocument copy = *current;
						copy.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
						copy.name += " Copy";
						for (OverlayElement &element : copy.elements)
							element.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
						documents.push_back(copy);
						if (!save())
							documents.pop_back();
						refreshDocumentList(int(documents.size()) - 1);
					}
				});
			else
				QObject::connect(button, &QPushButton::clicked, dialog, [this] {
					const int row = documentList->currentRow();
					if (row >= 0 && documents.size() > 1 && QMessageBox::question(dialog, "Delete overlay", "Delete this Pulse Weaver overlay?") == QMessageBox::Yes) {
						const OverlayDocument removed = documents[size_t(row)];
						documents.erase(documents.begin() + row);
						if (!save())
							documents.insert(documents.begin() + row, removed);
						refreshDocumentList(std::max(0, row - 1));
					}
				});
		}
		leftLayout->addLayout(documentButtons);
		auto *importWeb = new QPushButton("IMPORT STREAM ELEMENTS / LUMIA URL");
		importWeb->setToolTip("Import a hosted overlay URL as a direct top-level browser source; the provider keeps handling its own events");
		QObject::connect(importWeb, &QPushButton::clicked, dialog, [this] {
			bool ok = false;
			const QString urlText = QInputDialog::getText(dialog, "Import hosted overlay", "StreamElements, Lumia or compatible overlay URL", QLineEdit::Normal, {}, &ok).trimmed();
			const QUrl url(urlText);
			if (!ok || !url.isValid() || (url.scheme() != "https" && url.scheme() != "http")) {
				if (ok)
					QMessageBox::warning(dialog, "Import hosted overlay", "Enter a complete HTTPS or HTTP overlay URL.");
				return;
			}
			const QString suggested = url.host().isEmpty() ? "Imported Overlay" : url.host() + " Overlay";
			const QString overlayName = QInputDialog::getText(dialog, "Overlay name", "Name", QLineEdit::Normal, suggested, &ok).trimmed();
			if (!ok || overlayName.isEmpty())
				return;
			OverlayDocument document;
			document.name = overlayName;
			document.externalUrl = urlText;
			documents.push_back(document);
			if (!save())
				documents.pop_back();
			refreshDocumentList(int(documents.size()) - 1);
		});
		leftLayout->addWidget(importWeb);
		auto *newAlertBox = new QPushButton("NEW ALERT BOX");
		newAlertBox->setToolTip("Create a responsive Alert Box with follow, subscription, contribution, and raid variations");
		QObject::connect(newAlertBox, &QPushButton::clicked, dialog, [this] {
			OverlayDocument document = alertBoxDocument();
			documents.push_back(document);
			if (!save())
				documents.pop_back();
			refreshDocumentList(int(documents.size()) - 1);
		});
		leftLayout->addWidget(newAlertBox);
		leftLayout->addWidget(new QLabel("LAYERS"));
		layers = new QListWidget;
		leftLayout->addWidget(layers, 1);
		auto *elementButtons = new QHBoxLayout;
		for (const auto &pair : {qMakePair(QString("TEXT"), QString("text")), qMakePair(QString("SHAPE"), QString("shape")), qMakePair(QString("IMAGE"), QString("image")), qMakePair(QString("VIDEO"), QString("video")), qMakePair(QString("GOAL"), QString("progress"))}) {
			auto *button = new QPushButton(pair.first);
			button->setToolTip("Add " + pair.second + " layer");
			QObject::connect(button, &QPushButton::clicked, dialog, [this, type = pair.second] { addElement(type); });
			elementButtons->addWidget(button);
		}
		leftLayout->addLayout(elementButtons);
		auto *removeLayer = new QPushButton("REMOVE SELECTED LAYER");
		QObject::connect(removeLayer, &QPushButton::clicked, dialog, [this] {
			OverlayDocument *document = currentDocument();
			OverlayElement *element = currentElement();
			if (!document || !element)
				return;
			const QString id = element->id;
			const qsizetype index = std::distance(document->elements.data(), element);
			const OverlayElement removed = *element;
			document->elements.erase(std::remove_if(document->elements.begin(), document->elements.end(), [&id](const OverlayElement &entry) { return entry.id == id; }), document->elements.end());
			if (!save())
				document->elements.insert(document->elements.begin() + index, removed);
			refreshCanvas();
		});
		leftLayout->addWidget(removeLayer);

		auto *center = new QWidget;
		auto *centerLayout = new QVBoxLayout(center);
		scene = new QGraphicsScene(center);
		view = new QGraphicsView(scene);
		view->setBackgroundBrush(QColor("#04070D"));
		view->setRenderHint(QPainter::Antialiasing, true);
		view->setDragMode(QGraphicsView::RubberBandDrag);
		auto *canvasTabs = new QTabWidget(center);
		canvasTabs->addTab(view, "DESIGN CANVAS");
		auto *previewPage = new QWidget(canvasTabs);
		auto *previewLayout = new QVBoxLayout(previewPage);
		previewLayout->setContentsMargins(0, 0, 0, 0);
		previewCef = obs_browser_init_panel();
		if (previewCef) {
			previewBrowser = previewCef->create_widget(previewPage, "about:blank", nullptr);
			if (previewBrowser)
				previewLayout->addWidget(previewBrowser, 1);
		}
		if (!previewBrowser) {
			auto *unavailable = new QLabel("Live draft preview is unavailable because the bundled browser is not loaded.", previewPage);
			unavailable->setAlignment(Qt::AlignCenter);
			unavailable->setWordWrap(true);
			previewLayout->addWidget(unavailable, 1);
		}
		canvasTabs->addTab(previewPage, "LIVE DRAFT PREVIEW");
		centerLayout->addWidget(canvasTabs, 1);
		editorStatus = new QLabel;
		editorStatus->setTextInteractionFlags(Qt::TextSelectableByMouse);
		centerLayout->addWidget(editorStatus);

		auto *right = new QTabWidget;
		auto *properties = new QWidget;
		auto *propertiesLayout = new QVBoxLayout(properties);
		auto *documentForm = new QFormLayout;
		name = new QLineEdit;
		trigger = new QLineEdit;
		trigger->setPlaceholderText("e.g. twitch.channel.follow");
		externalUrl = new QLineEdit;
		externalUrl->setPlaceholderText("Optional hosted StreamElements / Lumia overlay URL");
		canvasWidth = new QSpinBox;
		canvasWidth->setRange(64, 7680);
		canvasHeight = new QSpinBox;
		canvasHeight->setRange(64, 7680);
		duration = new QSpinBox;
		duration->setRange(250, 120000);
		eventDriven = new QCheckBox("Hide until its trigger/test event");
		documentForm->addRow("Overlay name", name);
		documentForm->addRow("Trigger event", trigger);
		documentForm->addRow("Hosted overlay URL", externalUrl);
		documentForm->addRow("Canvas width", canvasWidth);
		documentForm->addRow("Canvas height", canvasHeight);
		documentForm->addRow("Duration ms", duration);
		documentForm->addRow("Behaviour", eventDriven);
		propertiesLayout->addLayout(documentForm);
		propertiesLayout->addWidget(new QLabel("SELECTED LAYER"));
		auto *elementForm = new QFormLayout;
		elementName = new QLineEdit;
		elementText = new QLineEdit;
		elementBinding = new QLineEdit;
		elementBinding->setPlaceholderText("user_name, viewers, amount…");
		elementAsset = new QLineEdit;
		elementAsset->setPlaceholderText("HTTPS or data URL");
		elementColor = new QLineEdit;
		elementBackground = new QLineEdit;
		auto makeDouble = [](double minimum, double maximum, int decimals = 1) {
			auto *field = new QDoubleSpinBox;
			field->setRange(minimum, maximum);
			field->setDecimals(decimals);
			return field;
		};
		elementX = makeDouble(-7680, 7680);
		elementY = makeDouble(-7680, 7680);
		elementWidth = makeDouble(10, 7680);
		elementHeight = makeDouble(10, 7680);
		elementRotation = makeDouble(-360, 360);
		elementOpacity = makeDouble(0, 1, 2);
		elementOpacity->setSingleStep(.05);
		elementFont = new QSpinBox;
		elementFont->setRange(6, 300);
		elementForm->addRow("Name", elementName);
		elementForm->addRow("Text", elementText);
		elementForm->addRow("Variable", elementBinding);
		elementForm->addRow("Asset URL", elementAsset);
		elementForm->addRow("Text colour", elementColor);
		elementForm->addRow("Background", elementBackground);
		elementForm->addRow("X", elementX);
		elementForm->addRow("Y", elementY);
		elementForm->addRow("Width", elementWidth);
		elementForm->addRow("Height", elementHeight);
		elementForm->addRow("Rotation", elementRotation);
		elementForm->addRow("Opacity", elementOpacity);
		elementForm->addRow("Font size", elementFont);
		propertiesLayout->addLayout(elementForm);
		auto *stateButtons = new QHBoxLayout;
		visibleButton = new QPushButton;
		lockedButton = new QPushButton;
		stateButtons->addWidget(visibleButton);
		stateButtons->addWidget(lockedButton);
		propertiesLayout->addLayout(stateButtons);
		propertiesLayout->addStretch();
		right->addTab(properties, "PROPERTIES");

		auto *alertPage = new QWidget;
		auto *alertLayout = new QVBoxLayout(alertPage);
		auto *alertCopy = new QLabel("One published Alert Box can render four event variations. Place it in landscape, portrait, or both; the host keeps one shared queue.");
		alertCopy->setWordWrap(true);
		alertLayout->addWidget(alertCopy);
		auto *alertForm = new QFormLayout;
		alertVariationSelect = new QComboBox;
		auto *variationRow = new QWidget(alertPage);
		auto *variationRowLayout = new QHBoxLayout(variationRow);
		variationRowLayout->setContentsMargins(0, 0, 0, 0);
		variationRowLayout->addWidget(alertVariationSelect, 1);
		auto *variationUp = new QPushButton("↑", variationRow);
		auto *variationDown = new QPushButton("↓", variationRow);
		variationUp->setToolTip("Move this variation earlier");
		variationDown->setToolTip("Move this variation later");
		variationRowLayout->addWidget(variationUp);
		variationRowLayout->addWidget(variationDown);
		alertHeadline = new QLineEdit;
		alertHeadline->setPlaceholderText("NEW FOLLOWER");
		alertMessage = new QLineEdit;
		alertMessage->setPlaceholderText("Welcome {user}!  Tokens: {user}, {amount}, {viewers}, {platform}");
		alertMedia = new QLineEdit;
		alertMedia->setPlaceholderText("Optional HTTPS image / GIF URL");
		alertSound = new QLineEdit;
		alertSound->setPlaceholderText("Optional HTTPS audio URL");
		alertColor = new QLineEdit;
		alertColor->setPlaceholderText("#22D3EE");
		alertMinimumAmount = new QDoubleSpinBox;
		alertMinimumAmount->setRange(0, 1000000000.0);
		alertMinimumAmount->setDecimals(2);
		alertMinimumViewers = new QSpinBox;
		alertMinimumViewers->setRange(0, 1000000000);
		alertEnter = new QComboBox;
		alertEnter->addItems({"fade", "slide-up", "slide-left", "zoom"});
		alertExit = new QComboBox;
		alertExit->addItems({"fade", "slide-up", "slide-left", "zoom"});
		alertFont = new QLineEdit;
		alertFont->setPlaceholderText("Segoe UI");
		alertTts = new QCheckBox("Read the alert message aloud");
		alertForm->addRow("Variation", variationRow);
		alertForm->addRow("Headline", alertHeadline);
		alertForm->addRow("Message", alertMessage);
		alertForm->addRow("Media URL", alertMedia);
		alertForm->addRow("Sound URL", alertSound);
		alertForm->addRow("Accent colour", alertColor);
		alertForm->addRow("Minimum amount", alertMinimumAmount);
		alertForm->addRow("Minimum raid viewers", alertMinimumViewers);
		alertForm->addRow("Enter animation", alertEnter);
		alertForm->addRow("Exit animation", alertExit);
		alertForm->addRow("Font family", alertFont);
		alertForm->addRow("Text to speech", alertTts);
		alertLayout->addLayout(alertForm);
		auto *saveAlert = new QPushButton("SAVE ALERT SETTINGS");
		alertLayout->addWidget(saveAlert);
		alertLayout->addStretch();
		right->addTab(alertPage, "ALERT BOX");

		auto *code = new QWidget;
		auto *codeLayout = new QVBoxLayout(code);
		auto *codeScope = new QLabel("Custom code shares the visual layers and receives overlay events. External scripts and direct network requests are blocked; use HTTPS image, video and sound URLs for media.");
		codeScope->setWordWrap(true);
		codeLayout->addWidget(codeScope);
		codeLayout->addWidget(new QLabel("Additional HTML — preserved alongside visual layers"));
		htmlCode = new QTextEdit;
		codeLayout->addWidget(htmlCode, 1);
		codeLayout->addWidget(new QLabel("Additional CSS"));
		cssCode = new QTextEdit;
		codeLayout->addWidget(cssCode, 1);
		codeLayout->addWidget(new QLabel("Additional JavaScript"));
		jsCode = new QTextEdit;
		codeLayout->addWidget(jsCode, 1);
		auto *saveCode = new QPushButton("SAVE DRAFT CODE");
		codeLayout->addWidget(saveCode);
		right->addTab(code, "ADVANCED CODE");

		splitter->addWidget(left);
		splitter->addWidget(center);
		splitter->addWidget(right);
		splitter->setSizes({300, 880, 380});
		root->addWidget(splitter, 1);

		QObject::connect(documentList, &QListWidget::currentRowChanged, dialog, [this] { if (!loading) loadDocument(); });
		QObject::connect(layers, &QListWidget::currentItemChanged, dialog, [this] { if (!loading) loadElement(); });
		QObject::connect(scene, &QGraphicsScene::selectionChanged, dialog, [this] {
			if (loading || scene->selectedItems().isEmpty())
				return;
			const QString id = scene->selectedItems().front()->data(0).toString();
			for (int row = 0; row < layers->count(); ++row)
				if (layers->item(row)->data(Qt::UserRole).toString() == id)
					layers->setCurrentRow(row);
		});
		const QList<QWidget *> documentFields{name, trigger, externalUrl, canvasWidth, canvasHeight, duration, eventDriven};
		for (QWidget *field : documentFields) {
			if (auto *line = qobject_cast<QLineEdit *>(field))
				QObject::connect(line, &QLineEdit::editingFinished, dialog, [this, field] { saveDocumentFields(field); });
			else if (auto *spin = qobject_cast<QSpinBox *>(field))
				QObject::connect(spin, &QSpinBox::editingFinished, dialog, [this, field] { saveDocumentFields(field); });
			else if (auto *check = qobject_cast<QCheckBox *>(field))
				QObject::connect(check, &QCheckBox::toggled, dialog, [this, field] { saveDocumentFields(field); });
		}
		for (QLineEdit *field : {elementName, elementText, elementBinding, elementAsset, elementColor, elementBackground})
			QObject::connect(field, &QLineEdit::editingFinished, dialog, [this, field] { saveElementFields(field); });
		for (QDoubleSpinBox *field : {elementX, elementY, elementWidth, elementHeight, elementRotation, elementOpacity})
			QObject::connect(field, &QDoubleSpinBox::editingFinished, dialog, [this, field] { saveElementFields(field); });
		QObject::connect(elementFont, &QSpinBox::editingFinished, dialog, [this] { saveElementFields(elementFont); });
		QObject::connect(visibleButton, &QPushButton::clicked, dialog, [this] {
			if (OverlayElement *element = currentElement()) {
				const QString id = element->id;
				element->visible = !element->visible;
				if (!save())
					element->visible = !element->visible;
				refreshCanvas(id);
			}
		});
		QObject::connect(lockedButton, &QPushButton::clicked, dialog, [this] {
			if (OverlayElement *element = currentElement()) {
				const QString id = element->id;
				element->locked = !element->locked;
				if (!save())
					element->locked = !element->locked;
				refreshCanvas(id);
			}
		});
		QObject::connect(saveCode, &QPushButton::clicked, dialog, [this] { saveDocumentFields(); });
		QObject::connect(alertVariationSelect, &QComboBox::currentIndexChanged, dialog,
				 [this] { loadSelectedAlertVariation(); });
		QObject::connect(saveAlert, &QPushButton::clicked, dialog, [this] { saveAlertFields(); });
		auto moveVariation = [this](int direction) {
			OverlayDocument *document = currentDocument();
			const int from = alertVariationSelect ? alertVariationSelect->currentIndex() : -1;
			const int to = from + direction;
			if (!document || !document->alertBox || from < 0 || to < 0 || to >= document->alertVariations.size())
				return;
			const QJsonArray before = document->alertVariations;
			QJsonArray reordered;
			for (int index = 0; index < before.size(); ++index) {
				if (index == from)
					reordered.append(before.at(to));
				else if (index == to)
					reordered.append(before.at(from));
				else
					reordered.append(before.at(index));
			}
			document->alertVariations = reordered;
			if (!save())
				document->alertVariations = before;
			loadAlertFields();
			alertVariationSelect->setCurrentIndex(to);
		};
		QObject::connect(variationUp, &QPushButton::clicked, dialog, [moveVariation] { moveVariation(-1); });
		QObject::connect(variationDown, &QPushButton::clicked, dialog, [moveVariation] { moveVariation(1); });
		QObject::connect(test, &QPushButton::clicked, dialog, [this] {
			if (OverlayDocument *document = currentDocument()) {
				const QJsonObject data{{"user", "PulseTester"}, {"user_name", "PulseTester"}, {"viewers", 42}, {"amount", 100}, {"progress", 64}};
				{
					const OverlayDocument *published = document;
					const QString variationId = alertVariationSelect ? alertVariationSelect->currentData().toString() : QString();
					QString event;
					for (const QJsonValue &value : published->alertVariations) {
						const QJsonObject variation = value.toObject();
						if (variation.value("id").toString() == variationId)
							event = variation.value("event").toString();
					}
					if (event.isEmpty())
						event = published->alertBox && !published->alertVariations.isEmpty()
						? published->alertVariations.first().toObject().value("event").toString()
										 : published->triggerEvent.isEmpty() ? "pulseweaver.test" : published->triggerEvent;
					QJsonObject payload = published->alertBox ? alertPayload(*published, event, data) : data;
					if (!payload.isEmpty()) {
						emitToKey(previewSubscriberKey(published->id), event, payload);
						editorStatus->setText("Draft test sent only to the private preview.");
					}
				}
			}
		});
		QObject::connect(publishButton, &QPushButton::clicked, dialog, [this] {
			OverlayDocument *document = currentDocument();
			if (!document)
				return;
			const QString id = document->id;
			if (!save()) {
				refreshDocumentList();
				return;
			}
			if (publish(id)) {
				refreshActionControls();
				refreshDocumentList();
				if (editorStatus)
					editorStatus->setText("Published revision saved. Existing browser sources use it on their next reload.");
			} else if (editorStatus) {
				editorStatus->setText("Publish failed. The previous published revision remains live.");
			}
		});
		QObject::connect(browser, &QPushButton::clicked, dialog, [this] { if (OverlayDocument *document = currentDocument()) QDesktopServices::openUrl(QUrl(liveUrl(document->id))); });
		QObject::connect(addScene, &QPushButton::clicked, dialog, [this] {
			if (OverlayDocument *document = currentDocument()) {
				QString error;
				if (owner->addToCameraScenes(document->id, placementTarget->currentData().toString(), &error))
					editorStatus->setText("Added published overlay to the selected Camera scene target.");
				else
					editorStatus->setText("Could not add overlay: " + error);
			}
		});
		QObject::connect(dialog, &QDialog::finished, dialog, [this] {
			const QString prefix = "preview:" + previewSession + ":";
			for (auto it = subscribers.begin(); it != subscribers.end();) {
				if (!it.key().startsWith(prefix)) {
					++it;
					continue;
				}
				for (const QPointer<QTcpSocket> &socket : it.value())
					if (socket)
						socket->disconnectFromHost();
				it = subscribers.erase(it);
			}
			if (previewBrowser) {
				previewBrowser->closeBrowser();
				delete previewBrowser;
				previewBrowser = nullptr;
			}
			delete previewCef;
			previewCef = nullptr;
			previewSession.clear();
		});
		QObject::connect(dialog, &QObject::destroyed, owner, [this] {
			dialog = nullptr;
			documentList = nullptr;
			layers = nullptr;
			scene = nullptr;
			view = nullptr;
			placementTarget = nullptr;
			alertVariationSelect = nullptr;
			alertHeadline = nullptr;
			alertMessage = nullptr;
			alertMedia = nullptr;
			alertSound = nullptr;
			alertColor = nullptr;
			alertMinimumAmount = nullptr;
			alertMinimumViewers = nullptr;
			alertEnter = nullptr;
			alertExit = nullptr;
			alertFont = nullptr;
			alertTts = nullptr;
		});
		refreshDocumentList();
	}
};

PulseOverlayRuntime::PulseOverlayRuntime(QObject *parent, StatusCallback status)
	: QObject(parent), impl(std::make_unique<Impl>(this, std::move(status)))
{
}

PulseOverlayRuntime::~PulseOverlayRuntime() = default;

void PulseOverlayRuntime::bindShell(QWidget *mainWindow)
{
	if (!mainWindow)
		return;
	if (auto *button = mainWindow->findChild<QPushButton *>("PulseWeaverOverlayButton"))
		connect(button, &QPushButton::clicked, this, [this] { openEditor(); });
	impl->mountActionControls(mainWindow);
}

void PulseOverlayRuntime::sceneCollectionLoaded()
{
	// Sources only exist after collection activation. Re-scanning is idempotent
	// and also permits retries after a failed journal write or collection switch.
	impl->migrateManagedSources();
}

void PulseOverlayRuntime::openEditor()
{
	if (!impl->dialog)
		impl->createEditor();
	impl->dialog->show();
	impl->dialog->raise();
	impl->dialog->activateWindow();
}

void PulseOverlayRuntime::publishEvent(const QString &eventKey, const QJsonObject &data)
{
	for (const OverlayDocument &document : impl->publishedDocuments) {
		impl->enqueue(document, eventKey, data);
	}
}

bool PulseOverlayRuntime::triggerOverlay(const QString &nameOrId, const QJsonObject &data)
{
	const OverlayDocument *document = impl->findPublishedDocument(nameOrId);
	if (!document)
		return false;
	QJsonObject payload = data;
	if (payload.isEmpty())
		payload = {{"user", "PulseTester"}, {"user_name", "PulseTester"}, {"viewers", 42}, {"amount", 100}, {"progress", 64}};
	return impl->enqueue(*document, "pulseweaver.overlay.trigger", payload);
}

bool PulseOverlayRuntime::skipAlert(const QString &nameOrId)
{
	const OverlayDocument *document = impl->findPublishedDocument(nameOrId);
	return document && impl->skip(document->id);
}

void PulseOverlayRuntime::setAlertsMuted(bool muted)
{
	impl->setMuted(muted);
	if (impl->actionMute && impl->actionMute->isChecked() != muted)
		impl->actionMute->setChecked(muted);
}

bool PulseOverlayRuntime::alertsMuted() const
{
	return impl->alertsMuted;
}

QStringList PulseOverlayRuntime::overlayNames() const
{
	QStringList names;
	for (const OverlayDocument &document : impl->publishedDocuments)
		names.append(document.name);
	return names;
}

QString PulseOverlayRuntime::overlayIdForName(const QString &name) const
{
	const OverlayDocument *document = impl->findPublishedDocument(name);
	return document ? document->id : QString();
}

QString PulseOverlayRuntime::urlFor(const QString &id) const
{
	return impl->liveUrl(id);
}

QJsonObject PulseOverlayRuntime::catalogueJson() const
{
	QJsonArray overlays;
	for (const OverlayDocument &document : impl->publishedDocuments)
		overlays.append(QJsonObject{{"id", document.id}, {"name", document.name}, {"width", document.width},
			{"height", document.height}, {"triggerEvent", document.triggerEvent}, {"url", urlFor(document.id)}});
	return {{"overlays", overlays}, {"port", int(impl->port)}};
}

bool PulseOverlayRuntime::createOverlay(const QString &name, int width, int height, const QString &text,
					const QString &triggerEvent, QString *createdId)
{
	const QString cleanName = name.trimmed();
	if (cleanName.isEmpty() || impl->findDocument(cleanName))
		return false;
	OverlayDocument document;
	document.name = cleanName;
	document.width = std::clamp(width, 64, 7680);
	document.height = std::clamp(height, 64, 7680);
	document.triggerEvent = triggerEvent.trimmed();
	document.eventDriven = !document.triggerEvent.isEmpty();
	OverlayElement backdrop;
	backdrop.type = "shape";
	backdrop.name = "Backdrop";
	backdrop.text.clear();
	backdrop.x = document.width * .1;
	backdrop.y = document.height * .68;
	backdrop.width = document.width * .8;
	backdrop.height = document.height * .2;
	backdrop.background = "#321457";
	OverlayElement headline;
	headline.name = "Headline";
	headline.text = text.isEmpty() ? cleanName : text;
	headline.x = document.width * .15;
	headline.y = document.height * .72;
	headline.width = document.width * .7;
	headline.height = document.height * .11;
	headline.fontSize = std::clamp(document.width / 24, 24, 96);
	document.elements = {backdrop, headline};
	impl->documents.push_back(document);
	if (!impl->save()) {
		impl->documents.pop_back();
		return false;
	}
	if (createdId)
		*createdId = document.id;
	if (impl->dialog)
		impl->refreshDocumentList(int(impl->documents.size()) - 1);
	return true;
}

bool PulseOverlayRuntime::removeOverlay(const QString &nameOrId)
{
	const std::vector<OverlayDocument> before = impl->documents;
	impl->documents.erase(std::remove_if(impl->documents.begin(), impl->documents.end(),
		[&nameOrId](const OverlayDocument &document) {
			return document.id == nameOrId || document.name.compare(nameOrId, Qt::CaseInsensitive) == 0;
		}), impl->documents.end());
	if (impl->documents.size() == before.size())
		return false;
	if (!impl->save()) {
		impl->documents = before;
		return false;
	}
	if (impl->dialog)
		impl->refreshDocumentList();
	return true;
}

bool PulseOverlayRuntime::addToCurrentScene(const QString &nameOrId, QString *error)
{
	return addToCameraScenes(nameOrId, "landscape", error);
}

bool PulseOverlayRuntime::addToCameraScenes(const QString &nameOrId, const QString &target, QString *error)
{
	const OverlayDocument *document = impl->findPublishedDocument(nameOrId);
	if (!document) {
		if (error)
			*error = "Published overlay not found.";
		return false;
	}
	const QString normalized = target.trimmed().toLower();
	if (normalized != "landscape" && normalized != "portrait" && normalized != "both") {
		if (error)
			*error = "Target must be Landscape, Portrait or Both.";
		return false;
	}

	struct TargetScene {
		obs_source_t *source = nullptr;
		QString role;
		int width = 0;
		int height = 0;
	};
	QList<TargetScene> targets;
	auto resolve = [&](const QString &role) -> bool {
		QWidget *mainWindow = static_cast<QWidget *>(obs_frontend_get_main_window());
		const char *property = role == "portrait" ? "pulseCameraPortraitSceneUuid" : "pulseCameraLandscapeSceneUuid";
		QString uuid = mainWindow ? mainWindow->property(property).toString() : QString();
		obs_source_t *source = uuid.isEmpty() ? nullptr : obs_get_source_by_uuid(uuid.toUtf8().constData());
		if (!source && role == "landscape")
			source = obs_frontend_get_current_preview_scene();
		obs_scene_t *scene = source ? obs_scene_from_source(source) : nullptr;
		obs_canvas_t *canvas = source ? obs_source_get_canvas(source) : nullptr;
		const bool portrait = canvas && QString::fromUtf8(obs_canvas_get_name(canvas)) == "Pulse Weaver Vertical";
		obs_video_info video{};
		const bool videoReady = canvas ? obs_canvas_get_video_info(canvas, &video) : obs_get_video_info(&video);
		obs_canvas_release(canvas);
		if (!scene || portrait != (role == "portrait") || !videoReady) {
			obs_source_release(source);
			return false;
		}
		targets.append({source, role, int(video.base_width), int(video.base_height)});
		return true;
	};
	if ((normalized == "landscape" || normalized == "both") && !resolve("landscape")) {
		if (error)
			*error = "Select a Landscape scene in Camera first.";
		return false;
	}
	if ((normalized == "portrait" || normalized == "both") && !resolve("portrait")) {
		for (const TargetScene &entry : std::as_const(targets))
			obs_source_release(entry.source);
		if (error)
			*error = "Select a Portrait scene in Camera first.";
		return false;
	}

	QJsonArray placements;
	bool success = true;
	for (const TargetScene &entry : std::as_const(targets)) {
		QString baseName = QString("PW Overlay — %1 (%2)").arg(document->name, entry.role == "portrait" ? "Portrait" : "Landscape");
		QString sourceName = baseName;
		for (int suffix = 2; obs_source_t *existing = obs_get_source_by_name(sourceName.toUtf8().constData()); ++suffix) {
			obs_source_release(existing);
			sourceName = baseName + " " + QString::number(suffix);
		}
		const QString url = impl->liveUrl(document->id, entry.role);
		obs_data_t *settings = obs_data_create();
		obs_data_set_string(settings, "url", url.toUtf8().constData());
		obs_data_set_int(settings, "width", entry.width);
		obs_data_set_int(settings, "height", entry.height);
		obs_data_set_bool(settings, "reroute_audio", true);
		obs_data_set_int(settings, "webpage_control_level", 0);
		obs_source_t *source = obs_source_create("browser_source", sourceName.toUtf8().constData(), settings, nullptr);
		obs_data_release(settings);
		obs_sceneitem_t *item = source ? obs_scene_add(obs_scene_from_source(entry.source), source) : nullptr;
		if (!source || !item) {
			obs_source_release(source);
			success = false;
			break;
		}
		placements.append(QJsonObject{{"sceneUuid", QString::fromUtf8(obs_source_get_uuid(entry.source))},
						      {"sourceName", sourceName}, {"url", url},
						      {"width", entry.width}, {"height", entry.height}});
		obs_source_release(source);
	}
	for (const TargetScene &entry : std::as_const(targets))
		obs_source_release(entry.source);
	if (!success) {
		const QByteArray rollback = QJsonDocument(QJsonObject{{"operation", "remove"}, {"placements", placements}}).toJson(QJsonDocument::Compact);
		applyOverlayPlacement(rollback.constData());
		if (error)
			*error = "The browser source could not be added; no Camera scenes were changed.";
		return false;
	}

	const QByteArray undo = QJsonDocument(QJsonObject{{"operation", "remove"}, {"placements", placements}}).toJson(QJsonDocument::Compact);
	const QByteArray redo = QJsonDocument(QJsonObject{{"operation", "add"}, {"placements", placements}}).toJson(QJsonDocument::Compact);
	obs_frontend_add_undo_redo_action("Add Pulse Weaver overlay", applyOverlayPlacement, applyOverlayPlacement,
					  undo.constData(), redo.constData(), false);
	obs_frontend_save();
	return success;
}
