#include "pulse-overlay-runtime.hpp"

#include <obs-module.h>
#include <obs-frontend-api.h>
#include <util/bmem.h>
#include <util/platform.h>

#include <QCheckBox>
#include <QColor>
#include <QComboBox>
#include <QDateTime>
#include <QDesktopServices>
#include <QDialog>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileInfo>
#include <QFormLayout>
#include <QGraphicsItem>
#include <QGraphicsRectItem>
#include <QGraphicsScene>
#include <QGraphicsTextItem>
#include <QGraphicsView>
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
#include <QSaveFile>
#include <QSpinBox>
#include <QSplitter>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTabWidget>
#include <QTextEdit>
#include <QTimer>
#include <QUrl>
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
	std::vector<OverlayElement> elements;
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

QJsonObject documentJson(const OverlayDocument &document)
{
	QJsonArray elements;
	for (const OverlayElement &element : document.elements)
		elements.append(elementJson(element));
	return {{"id", document.id}, {"name", document.name}, {"triggerEvent", document.triggerEvent},
		{"width", document.width}, {"height", document.height}, {"durationMs", document.durationMs},
		{"eventDriven", document.eventDriven}, {"externalUrl", document.externalUrl}, {"customHtml", document.customHtml},
		{"customCss", document.customCss}, {"customJs", document.customJs}, {"elements", elements}};
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
	for (const QJsonValue &value : json.value("elements").toArray())
		document.elements.push_back(elementFromJson(value.toObject()));
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
	PulseOverlayRuntime *owner;
	StatusCallback status;
	QString directory = overlayConfigDirectory();
	QString storePath = QDir(directory).filePath("overlays.json");
	std::vector<OverlayDocument> documents;
	QTcpServer server;
	quint16 port = 18754;
	QHash<QString, QList<QPointer<QTcpSocket>>> subscribers;
	QPointer<QDialog> dialog;
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
	QTextEdit *htmlCode = nullptr;
	QTextEdit *cssCode = nullptr;
	QTextEdit *jsCode = nullptr;
	QLabel *editorStatus = nullptr;
	bool loading = false;

	explicit Impl(PulseOverlayRuntime *owner_, StatusCallback status_) : owner(owner_), status(std::move(status_))
	{
		load();
		startServer();
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

	void load()
	{
		QFile file(storePath);
		if (file.open(QIODevice::ReadOnly)) {
			const QJsonDocument json = QJsonDocument::fromJson(file.readAll());
			for (const QJsonValue &value : json.object().value("overlays").toArray())
				documents.push_back(documentFromJson(value.toObject()));
		}
		if (documents.empty()) {
			seedDefaults();
			save();
		}
	}

	void save()
	{
		QJsonArray overlays;
		for (const OverlayDocument &document : documents)
			overlays.append(documentJson(document));
		if (QFile::exists(storePath)) {
			QFile::remove(storePath + ".bak");
			QFile::copy(storePath, storePath + ".bak");
		}
		QSaveFile file(storePath);
		if (!file.open(QIODevice::WriteOnly)) {
			if (status)
				status("Overlay save failed: " + file.errorString());
			return;
		}
		file.write(QJsonDocument(QJsonObject{{"schema", 1}, {"overlays", overlays}}).toJson(QJsonDocument::Indented));
		if (!file.commit() && status)
			status("Overlay save failed: " + file.errorString());
	}

	QString pageHtml(const OverlayDocument &document) const
	{
		const QUrl imported(document.externalUrl);
		if (imported.isValid() && (imported.scheme() == "https" || imported.scheme() == "http")) {
			QByteArray encoded = QJsonDocument(QJsonArray{document.externalUrl}).toJson(QJsonDocument::Compact);
			encoded = encoded.mid(1, encoded.size() - 2);
			return "<!doctype html><html><head><meta charset='utf-8'></head><body style='margin:0;background:transparent'>"
			       "<script>location.replace(" + QString::fromUtf8(encoded) + ");</script></body></html>";
		}
		QString body;
		for (const OverlayElement &element : document.elements) {
			if (!element.visible)
				continue;
			const QString style = QString("left:%1px;top:%2px;width:%3px;height:%4px;transform:rotate(%5deg);opacity:%6;color:%7;background:%8;font-size:%9px;")
				.arg(element.x).arg(element.y).arg(element.width).arg(element.height).arg(element.rotation)
				.arg(element.opacity).arg(cssEscaped(element.color)).arg(element.type == "text" ? "transparent" : cssEscaped(element.background)).arg(element.fontSize);
			const QString bind = element.binding.toHtmlEscaped();
			if (element.type == "image")
				body += QString("<img class='pw e' data-bind='%1' style='%2' src='%3'>").arg(bind, style, element.asset.toHtmlEscaped());
			else if (element.type == "video")
				body += QString("<video class='pw e' data-bind='%1' style='%2' src='%3' autoplay loop muted></video>").arg(bind, style, element.asset.toHtmlEscaped());
			else if (element.type == "progress")
				body += QString("<div class='pw e progress' data-bind='%1' style='%2'><div class='fill'></div></div>").arg(bind, style);
			else
				body += QString("<div class='pw e %1' data-bind='%2' style='%3'>%4</div>").arg(element.type.toHtmlEscaped(), bind, style, element.text.toHtmlEscaped());
		}
		const QString hidden = document.eventDriven ? "opacity:0" : "opacity:1";
		return QString(R"HTML(<!doctype html><html><head><meta charset="utf-8"><style>
html,body{margin:0;width:100%;height:100%;overflow:hidden;background:transparent}.stage{position:relative;width:%1px;height:%2px;%3}.e{position:absolute;box-sizing:border-box;display:flex;align-items:center;justify-content:center;text-align:center;font-family:Segoe UI,Arial,sans-serif}.shape{border:2px solid rgba(255,255,255,.22);border-radius:24px}.progress{padding:8px;border-radius:999px}.fill{height:100%;width:0;background:#22d3ee;border-radius:999px}.stage.active{animation:pw-in .35s ease-out both}.stage.out{animation:pw-out .35s ease-in both}@keyframes pw-in{from{opacity:0;transform:translateY(24px) scale(.96)}to{opacity:1;transform:none}}@keyframes pw-out{to{opacity:0;transform:translateY(-16px) scale(.98)}}%4
</style></head><body><div id="stage" class="stage">%5%6</div><script>
const stage=document.getElementById('stage');let hideTimer;const get=(o,p)=>p.split('.').reduce((v,k)=>v&&v[k],o);function apply(d){document.querySelectorAll('[data-bind]').forEach(el=>{const key=el.dataset.bind;if(!key)return;const value=get(d,key)??get(d,'event.'+key)??d[key];if(value===undefined)return;if(el.classList.contains('progress'))el.querySelector('.fill').style.width=Math.max(0,Math.min(100,Number(value)))+'%';else if(el.tagName==='IMG'||el.tagName==='VIDEO')el.src=value;else el.textContent=value;});stage.classList.remove('out');void stage.offsetWidth;stage.classList.add('active');stage.style.opacity=1;clearTimeout(hideTimer);%7}
const es=new EventSource('/overlay/%8/events');es.onmessage=e=>{try{apply(JSON.parse(e.data).data||{})}catch(x){console.error(x)}};%9
</script></body></html>)HTML")
			.arg(document.width).arg(document.height).arg(hidden, document.customCss, body, document.customHtml)
			.arg(document.eventDriven ? QString("hideTimer=setTimeout(()=>{stage.classList.add('out')},%1);").arg(document.durationMs) : QString())
			.arg(document.id, document.customJs);
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
				QObject::connect(socket, &QTcpSocket::readyRead, owner, [this, socket] { handleRequest(socket); });
			}
		});
	}

	void handleRequest(QTcpSocket *socket)
	{
		const QByteArray request = socket->readAll();
		const QList<QByteArray> first = request.left(request.indexOf("\r\n")).split(' ');
		if (first.size() < 2) {
			respond(socket, 400, "text/plain", "Invalid request");
			return;
		}
		const QString path = QUrl::fromEncoded(first[1]).path();
		const QStringList parts = path.split('/', Qt::SkipEmptyParts);
		if (parts.size() == 3 && parts[0] == "overlay" && parts[2] == "events") {
			const OverlayDocument *document = findDocument(parts[1]);
			if (!document) {
				respond(socket, 404, "text/plain", "Overlay not found");
				return;
			}
			socket->write("HTTP/1.1 200 OK\r\nContent-Type: text/event-stream\r\nCache-Control: no-cache\r\nConnection: keep-alive\r\nAccess-Control-Allow-Origin: *\r\n\r\n: connected\n\n");
			subscribers[document->id].append(socket);
			QObject::connect(socket, &QTcpSocket::disconnected, owner, [this, id = document->id, socket] {
				subscribers[id].removeAll(socket);
				socket->deleteLater();
			});
			return;
		}
		if (parts.size() == 2 && parts[0] == "overlay") {
			const OverlayDocument *document = findDocument(parts[1]);
			if (!document) {
				respond(socket, 404, "text/plain", "Overlay not found");
				return;
			}
			respond(socket, 200, "text/html; charset=utf-8", pageHtml(*document).toUtf8());
			return;
		}
		respond(socket, 404, "text/plain", "Not found");
	}

	static void respond(QTcpSocket *socket, int code, const QByteArray &contentType, const QByteArray &body)
	{
		const QByteArray reason = code == 200 ? "OK" : code == 404 ? "Not Found" : "Bad Request";
		socket->write("HTTP/1.1 " + QByteArray::number(code) + " " + reason + "\r\nContent-Type: " + contentType + "\r\nAccess-Control-Allow-Origin: *\r\nConnection: close\r\nContent-Length: " + QByteArray::number(body.size()) + "\r\n\r\n" + body);
		socket->disconnectFromHost();
	}

	void emitTo(const OverlayDocument &document, const QString &eventKey, const QJsonObject &data)
	{
		const QByteArray message = "data: " + jsonLine(QJsonObject{{"event", eventKey}, {"data", data}}) + "\n\n";
		auto &clients = subscribers[document.id];
		for (auto it = clients.begin(); it != clients.end();) {
			if (!*it || (*it)->state() != QAbstractSocket::ConnectedState)
				it = clients.erase(it);
			else {
				(*it)->write(message);
				++it;
			}
		}
	}

	void refreshDocumentList(int preferred = -1)
	{
		if (!documentList)
			return;
		loading = true;
		const int row = preferred >= 0 ? preferred : std::max(0, documentList->currentRow());
		documentList->clear();
		for (const OverlayDocument &document : documents) {
			auto *item = new QListWidgetItem(document.name, documentList);
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
		loading = false;
		refreshCanvas();
		if (editorStatus)
			editorStatus->setText(QString("LIVE URL  http://127.0.0.1:%1/overlay/%2").arg(port).arg(document->id));
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
							entry.x = position.x();
							entry.y = position.y();
							if (currentElement() && currentElement()->id == id) {
								elementX->setValue(entry.x);
								elementY->setValue(entry.y);
							}
							save();
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
			visibleButton->setText(element->visible ? "◉  VISIBLE" : "○  HIDDEN");
			lockedButton->setText(element->locked ? "🔒  LOCKED" : "🔓  EDITABLE");
		}
		loading = false;
	}

	void saveDocumentFields()
	{
		if (loading)
			return;
		OverlayDocument *document = currentDocument();
		if (!document)
			return;
		document->name = name->text().trimmed().isEmpty() ? "Untitled Overlay" : name->text().trimmed();
		document->triggerEvent = trigger->text().trimmed();
		document->externalUrl = externalUrl->text().trimmed();
		document->width = canvasWidth->value();
		document->height = canvasHeight->value();
		document->durationMs = duration->value();
		document->eventDriven = eventDriven->isChecked();
		document->customHtml = htmlCode->toPlainText();
		document->customCss = cssCode->toPlainText();
		document->customJs = jsCode->toPlainText();
		save();
		refreshDocumentList(documentList->currentRow());
	}

	void saveElementFields()
	{
		if (loading)
			return;
		OverlayElement *element = currentElement();
		if (!element)
			return;
		const QString id = element->id;
		element->name = elementName->text().trimmed().isEmpty() ? element->type.toUpper() : elementName->text().trimmed();
		element->text = elementText->text();
		element->binding = elementBinding->text().trimmed();
		element->asset = elementAsset->text().trimmed();
		element->color = elementColor->text().trimmed();
		element->background = elementBackground->text().trimmed();
		element->x = elementX->value();
		element->y = elementY->value();
		element->width = elementWidth->value();
		element->height = elementHeight->value();
		element->rotation = elementRotation->value();
		element->opacity = elementOpacity->value();
		element->fontSize = elementFont->value();
		save();
		refreshCanvas(id);
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
		save();
		refreshCanvas(element.id);
	}

	void createEditor()
	{
		dialog = new QDialog(static_cast<QWidget *>(obs_frontend_get_main_window()));
		dialog->setAttribute(Qt::WA_DeleteOnClose);
		dialog->setWindowTitle("Pulse Weaver — Overlay Designer");
		dialog->resize(1560, 900);
		// Overlay editing follows the active application theme.
		auto *root = new QVBoxLayout(dialog);
		auto *head = new QHBoxLayout;
		auto *title = new QLabel("OVERLAY DESIGNER  •  VISUAL, CODE, LIVE");
		title->setObjectName("PulseWeaverHeading");
		head->addWidget(title);
		head->addStretch();
		auto *test = new QPushButton("TEST OVERLAY");
		auto *browser = new QPushButton("OPEN LIVE URL");
		auto *addScene = new QPushButton("ADD TO CURRENT SCENE");
		head->addWidget(test);
		head->addWidget(browser);
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
					save();
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
						save();
						refreshDocumentList(int(documents.size()) - 1);
					}
				});
			else
				QObject::connect(button, &QPushButton::clicked, dialog, [this] {
					const int row = documentList->currentRow();
					if (row >= 0 && documents.size() > 1 && QMessageBox::question(dialog, "Delete overlay", "Delete this Pulse Weaver overlay?") == QMessageBox::Yes) {
						documents.erase(documents.begin() + row);
						save();
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
			save();
			refreshDocumentList(int(documents.size()) - 1);
		});
		leftLayout->addWidget(importWeb);
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
			document->elements.erase(std::remove_if(document->elements.begin(), document->elements.end(), [&id](const OverlayElement &entry) { return entry.id == id; }), document->elements.end());
			save();
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
		centerLayout->addWidget(view, 1);
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

		auto *code = new QWidget;
		auto *codeLayout = new QVBoxLayout(code);
		codeLayout->addWidget(new QLabel("Additional HTML — preserved alongside visual layers"));
		htmlCode = new QTextEdit;
		codeLayout->addWidget(htmlCode, 1);
		codeLayout->addWidget(new QLabel("Additional CSS"));
		cssCode = new QTextEdit;
		codeLayout->addWidget(cssCode, 1);
		codeLayout->addWidget(new QLabel("Additional JavaScript"));
		jsCode = new QTextEdit;
		codeLayout->addWidget(jsCode, 1);
		auto *saveCode = new QPushButton("SAVE CODE + RELOAD LIVE OVERLAY");
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
				QObject::connect(line, &QLineEdit::editingFinished, dialog, [this] { saveDocumentFields(); });
			else if (auto *spin = qobject_cast<QSpinBox *>(field))
				QObject::connect(spin, &QSpinBox::editingFinished, dialog, [this] { saveDocumentFields(); });
			else if (auto *check = qobject_cast<QCheckBox *>(field))
				QObject::connect(check, &QCheckBox::toggled, dialog, [this] { saveDocumentFields(); });
		}
		for (QLineEdit *field : {elementName, elementText, elementBinding, elementAsset, elementColor, elementBackground})
			QObject::connect(field, &QLineEdit::editingFinished, dialog, [this] { saveElementFields(); });
		for (QDoubleSpinBox *field : {elementX, elementY, elementWidth, elementHeight, elementRotation, elementOpacity})
			QObject::connect(field, &QDoubleSpinBox::editingFinished, dialog, [this] { saveElementFields(); });
		QObject::connect(elementFont, &QSpinBox::editingFinished, dialog, [this] { saveElementFields(); });
		QObject::connect(visibleButton, &QPushButton::clicked, dialog, [this] { if (OverlayElement *element = currentElement()) { element->visible = !element->visible; save(); refreshCanvas(element->id); } });
		QObject::connect(lockedButton, &QPushButton::clicked, dialog, [this] { if (OverlayElement *element = currentElement()) { element->locked = !element->locked; save(); refreshCanvas(element->id); } });
		QObject::connect(saveCode, &QPushButton::clicked, dialog, [this] { saveDocumentFields(); });
		QObject::connect(test, &QPushButton::clicked, dialog, [this] {
			if (OverlayDocument *document = currentDocument()) {
				const QJsonObject data{{"user", "PulseTester"}, {"user_name", "PulseTester"}, {"viewers", 42}, {"amount", 100}, {"progress", 64}};
				emitTo(*document, document->triggerEvent.isEmpty() ? "pulseweaver.test" : document->triggerEvent, data);
			}
		});
		QObject::connect(browser, &QPushButton::clicked, dialog, [this] { if (OverlayDocument *document = currentDocument()) QDesktopServices::openUrl(QUrl(QString("http://127.0.0.1:%1/overlay/%2").arg(port).arg(document->id))); });
		QObject::connect(addScene, &QPushButton::clicked, dialog, [this] {
			if (OverlayDocument *document = currentDocument()) {
				QString error;
				if (owner->addToCurrentScene(document->id, &error))
					editorStatus->setText("Added to the active OBS scene as a managed browser source.");
				else
					editorStatus->setText("Could not add overlay: " + error);
			}
		});
		QObject::connect(dialog, &QObject::destroyed, owner, [this] {
			dialog = nullptr;
			documentList = nullptr;
			layers = nullptr;
			scene = nullptr;
			view = nullptr;
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
	for (const OverlayDocument &document : impl->documents) {
		if (document.triggerEvent == eventKey)
			impl->emitTo(document, eventKey, data);
	}
}

bool PulseOverlayRuntime::triggerOverlay(const QString &nameOrId, const QJsonObject &data)
{
	const OverlayDocument *document = impl->findDocument(nameOrId);
	if (!document)
		return false;
	QJsonObject payload = data;
	if (payload.isEmpty())
		payload = {{"user", "PulseTester"}, {"user_name", "PulseTester"}, {"viewers", 42}, {"amount", 100}, {"progress", 64}};
	impl->emitTo(*document, "pulseweaver.overlay.trigger", payload);
	return true;
}

QStringList PulseOverlayRuntime::overlayNames() const
{
	QStringList names;
	for (const OverlayDocument &document : impl->documents)
		names.append(document.name);
	return names;
}

QString PulseOverlayRuntime::overlayIdForName(const QString &name) const
{
	const OverlayDocument *document = impl->findDocument(name);
	return document ? document->id : QString();
}

QString PulseOverlayRuntime::urlFor(const QString &id) const
{
	return QString("http://127.0.0.1:%1/overlay/%2").arg(impl->port).arg(id);
}

QJsonObject PulseOverlayRuntime::catalogueJson() const
{
	QJsonArray overlays;
	for (const OverlayDocument &document : impl->documents)
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
	impl->save();
	if (createdId)
		*createdId = document.id;
	if (impl->dialog)
		impl->refreshDocumentList(int(impl->documents.size()) - 1);
	return true;
}

bool PulseOverlayRuntime::removeOverlay(const QString &nameOrId)
{
	const auto before = impl->documents.size();
	impl->documents.erase(std::remove_if(impl->documents.begin(), impl->documents.end(),
		[&nameOrId](const OverlayDocument &document) {
			return document.id == nameOrId || document.name.compare(nameOrId, Qt::CaseInsensitive) == 0;
		}), impl->documents.end());
	if (impl->documents.size() == before)
		return false;
	impl->save();
	if (impl->dialog)
		impl->refreshDocumentList();
	return true;
}

bool PulseOverlayRuntime::addToCurrentScene(const QString &nameOrId, QString *error)
{
	const OverlayDocument *document = impl->findDocument(nameOrId);
	if (!document) {
		if (error)
			*error = "Overlay not found.";
		return false;
	}
	obs_source_t *sceneSource = obs_frontend_get_current_scene();
	obs_scene_t *scene = sceneSource ? obs_scene_from_source(sceneSource) : nullptr;
	if (!scene) {
		if (sceneSource)
			obs_source_release(sceneSource);
		if (error)
			*error = "There is no active horizontal scene.";
		return false;
	}
	QString sourceName = "PW Overlay — " + document->name;
	for (int suffix = 2; obs_source_t *existing = obs_get_source_by_name(sourceName.toUtf8().constData()); ++suffix) {
		obs_source_release(existing);
		sourceName = "PW Overlay — " + document->name + " " + QString::number(suffix);
	}
	obs_data_t *settings = obs_data_create();
	obs_data_set_string(settings, "url", urlFor(document->id).toUtf8().constData());
	obs_data_set_int(settings, "width", document->width);
	obs_data_set_int(settings, "height", document->height);
	obs_data_set_bool(settings, "reroute_audio", true);
	obs_source_t *source = obs_source_create("browser_source", sourceName.toUtf8().constData(), settings, nullptr);
	obs_data_release(settings);
	bool success = source != nullptr;
	if (source) {
		success = obs_scene_add(scene, source) != nullptr;
		obs_source_release(source);
	}
	obs_source_release(sceneSource);
	if (!success && error)
		*error = "The OBS browser source plugin is unavailable.";
	if (success)
		obs_frontend_save();
	return success;
}
