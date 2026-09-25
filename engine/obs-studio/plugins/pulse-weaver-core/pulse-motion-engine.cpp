#include "pulse-motion-engine.hpp"

#include "pulse-scene-item-ref.hpp"

#include <obs.h>

#include <graphics/matrix4.h>
#include <graphics/vec3.h>
#include <graphics/vec4.h>

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QCompleter>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontMetrics>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QInputDialog>
#include <QDialog>
#include <QDialogButtonBox>
#include <QPaintEngine>
#include <QPushButton>
#include <QSaveFile>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QSplitter>
#include <QSlider>
#include <QTabWidget>
#include <QToolButton>
#include <QTreeWidget>
#include <QUuid>
#include <QVBoxLayout>
#include <QWindow>

#include <algorithm>
#include <cmath>
#include <limits>
#include <mutex>
#include <utility>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

namespace {

QString motionCollection();

obs_source_t *motionSource(const QString &name)
{
	obs_source_t *source = obs_get_source_by_name(name.toUtf8().constData());
	if (!source) {
		OBSCanvasAutoRelease canvas = obs_get_canvas_by_name("Pulse Weaver Vertical");
		if (canvas) source = obs_canvas_get_source_by_name(canvas, name.toUtf8().constData());
	}
	return source;
}

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

bool motionGraphicSwitch(const QString &sourceName)
{
	const QString name = sourceName.toLower();
	if (name.endsWith("backdrop")) return true;
	static const QSet<QString> graphics{
		"brb scene", "stream ended", "lumia startiing", "lumia overlay",
		"chatty", "vert chatty", "whodatpalalerts", "whodatpallb",
		"whodatpal", "caption", "cheer1", "emotes", "follower",
		"shouty", "joyfull", "spiner"};
	return graphics.contains(name);
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

namespace {

QPolygonF motionItemPolygon(obs_sceneitem_t *item)
{
	QPolygonF result;
	if (!item) return result;
	matrix4 transform;
	obs_sceneitem_get_box_transform(item, &transform);
	for (const QPointF &corner : {QPointF(0, 0), QPointF(1, 0), QPointF(1, 1), QPointF(0, 1)}) {
		vec3 value;
		vec3_set(&value, float(corner.x()), float(corner.y()), 0.0f);
		vec3_transform(&value, &value, &transform);
		result << QPointF(value.x, value.y);
	}
	return result;
}

bool motionItemContains(obs_sceneitem_t *item, const QPointF &canvasPoint)
{
	if (!item || !obs_sceneitem_visible(item)) return false;
	obs_source_t *source = obs_sceneitem_get_source(item);
	if (!source || !(obs_source_get_output_flags(source) & OBS_SOURCE_VIDEO)) return false;
	matrix4 transform;
	obs_sceneitem_get_box_transform(item, &transform);
	if (!matrix4_inv(&transform, &transform)) return false;
	vec3 point;
	vec3_set(&point, float(canvasPoint.x()), float(canvasPoint.y()), 0.0f);
	vec3_transform(&point, &point, &transform);
	return point.x >= 0.0f && point.x <= 1.0f && point.y >= 0.0f && point.y <= 1.0f;
}

QPointF motionItemPoint(obs_sceneitem_t *item, const QPointF &canvasPoint)
{
	if (!item) return {0.5, 0.5};
	matrix4 transform;
	obs_sceneitem_get_box_transform(item, &transform);
	if (!matrix4_inv(&transform, &transform)) return {0.5, 0.5};
	vec3 point;
	vec3_set(&point, float(canvasPoint.x()), float(canvasPoint.y()), 0.0f);
	vec3_transform(&point, &point, &transform);
	return {std::clamp(double(point.x), 0.0, 1.0), std::clamp(double(point.y), 0.0, 1.0)};
}

QPointF motionCanvasPoint(obs_sceneitem_t *item, const QPointF &itemPoint)
{
	if (!item) return {};
	matrix4 transform;
	obs_sceneitem_get_box_transform(item, &transform);
	vec3 point;
	vec3_set(&point, float(itemPoint.x()), float(itemPoint.y()), 0.0f);
	vec3_transform(&point, &point, &transform);
	return {point.x, point.y};
}

void motionDrawLines(const QPolygonF &points, uint32_t color, bool loop = true)
{
	if (points.size() < 2) return;
	gs_render_start(true);
	const qsizetype edges = loop ? points.size() : points.size() / 2;
	for (qsizetype index = 0; index < edges; ++index) {
		const QPointF &from = loop ? points.at(index) : points.at(index * 2);
		const QPointF &to = loop ? points.at((index + 1) % points.size()) : points.at(index * 2 + 1);
		gs_vertex2f(float(from.x()), float(from.y()));
		gs_vertex2f(float(to.x()), float(to.y()));
	}
	gs_vertbuffer_t *lines = gs_render_save();
	gs_effect_t *effect = obs_get_base_effect(OBS_EFFECT_SOLID);
	gs_eparam_t *colorParam = gs_effect_get_param_by_name(effect, "color");
	gs_effect_set_color(colorParam, color);
	gs_load_vertexbuffer(lines);
	while (gs_effect_loop(effect, "Solid")) gs_draw(GS_LINES, 0, 0);
	gs_load_vertexbuffer(nullptr);
	gs_vertexbuffer_destroy(lines);
}

void motionDrawSolid(float left, float top, float right, float bottom, uint32_t color)
{
	gs_render_start(true);
	gs_vertex2f(left, top);
	gs_vertex2f(left, bottom);
	gs_vertex2f(right, top);
	gs_vertex2f(right, bottom);
	gs_vertbuffer_t *quad = gs_render_save();
	gs_effect_t *effect = obs_get_base_effect(OBS_EFFECT_SOLID);
	gs_eparam_t *colorParam = gs_effect_get_param_by_name(effect, "color");
	gs_effect_set_color(colorParam, color);
	gs_load_vertexbuffer(quad);
	while (gs_effect_loop(effect, "Solid")) gs_draw(GS_TRISTRIP, 0, 0);
	gs_load_vertexbuffer(nullptr);
	gs_vertexbuffer_destroy(quad);
}

} // namespace

class PulseMotionCanvas final : public QWidget {
public:
	using Interaction = std::function<void(qint64, const QPointF &, bool)>;

	explicit PulseMotionCanvas(QWidget *parent = nullptr) : QWidget(parent)
	{
		setAttribute(Qt::WA_PaintOnScreen);
		setAttribute(Qt::WA_StaticContents);
		setAttribute(Qt::WA_NoSystemBackground);
		setAttribute(Qt::WA_OpaquePaintEvent);
		setAttribute(Qt::WA_DontCreateNativeAncestors);
		setAttribute(Qt::WA_NativeWindow);
		setMinimumSize(240, 180);
		setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
		setCursor(Qt::CrossCursor);
		setMouseTracking(true);
		connect(windowHandle(), &QWindow::visibleChanged, this, [this](bool visible) {
			if (visible) QTimer::singleShot(0, this, [this] { createDisplay(); });
		});
	}

	~PulseMotionCanvas() override {
		display = nullptr;
		if (showing && state.source) obs_source_dec_showing(state.source);
	}

	void setMotionState(QString container, qint64 selected, QSet<qint64> controlled, bool punch,
		const QPointF &focus, double zoom, obs_source_t *draft = nullptr)
	{
		std::lock_guard lock(stateMutex);
		state.container = container.toUtf8();
		OBSSourceAutoRelease live = draft ? nullptr : obs_get_source_by_name(state.container.constData());
		if (!live && !draft && !state.container.isEmpty()) {
			OBSCanvasAutoRelease portrait = obs_get_canvas_by_name("Pulse Weaver Vertical");
			if (portrait) live = obs_canvas_get_source_by_name(portrait, state.container.constData());
		}
		OBSSource next = draft ? OBSSource(draft) : OBSSource(live);
		if (next != state.source) {
			if (showing && state.source) obs_source_dec_showing(state.source);
			state.source = next;
			showing = isVisible() && bool(state.source);
			if (showing) obs_source_inc_showing(state.source);
		}
		state.selected = selected;
		state.controlled = std::move(controlled);
		state.punch = punch;
		state.focus = focus;
		state.zoom = std::clamp(zoom, 1.0, 4.0);
	}

	Interaction interacted;
	std::function<void()> activated;
	std::function<void()> beginEdit;
	std::function<void(qint64, QPointF, double, double, int, bool)> transformed;

protected:
	void showEvent(QShowEvent *event) override
	{
		QWidget::showEvent(event);
		std::lock_guard lock(stateMutex);
		if (!showing && state.source) { obs_source_inc_showing(state.source); showing = true; }
	}
	void hideEvent(QHideEvent *event) override
	{
		QWidget::hideEvent(event);
		std::lock_guard lock(stateMutex);
		if (showing && state.source) obs_source_dec_showing(state.source);
		showing = false;
	}
	QPaintEngine *paintEngine() const override { return nullptr; }

	void paintEvent(QPaintEvent *event) override
	{
		createDisplay();
		QWidget::paintEvent(event);
	}

	void resizeEvent(QResizeEvent *event) override
	{
		QWidget::resizeEvent(event);
		createDisplay();
		if (display) {
			const QSize pixels = pixelSize();
			obs_display_resize(display, uint32_t(pixels.width()), uint32_t(pixels.height()));
		}
	}

	void mousePressEvent(QMouseEvent *event) override
	{
		if (event->button() != Qt::LeftButton) return;
		if (activated) activated();
		const State snapshot = snapshotState();
		const QSize canvas = sourceSize(snapshot.source);
		const QRectF surface = canvasRect(canvas);
		if (!surface.contains(event->position())) return;
		const QPointF point((event->position().x() - surface.left()) * canvas.width() / surface.width(),
			(event->position().y() - surface.top()) * canvas.height() / surface.height());
		auto *scene = sceneFor(snapshot.source);
		auto *selected = scene && snapshot.selected >= 0 ? obs_scene_find_sceneitem_by_id(scene, snapshot.selected) : nullptr;
		const int handle = !snapshot.punch && selected && obs_sceneitem_visible(selected) && !obs_sceneitem_locked(selected) ?
			cornerAt(selected, point, 12.0 * canvas.width() / surface.width()) : -1;
		if (handle >= 0) {
			const QPolygonF corners = motionItemPolygon(selected);
			dragItem = snapshot.selected;
			resizeCorner = handle;
			resizeCornerStart = corners.at(handle);
			resizePressPoint = point;
			resizeAnchor = corners.at((handle + 2) % 4);
			resizePolygon = corners;
			resizeAppliedX = resizeAppliedY = 1.0;
			resizeCrop = event->modifiers().testFlag(Qt::AltModifier);
			resizeFree = event->modifiers().testFlag(Qt::ShiftModifier) || resizeCrop;
			draggingResize = true;
			dragEditStarted = false;
			grabMouse();
			event->accept();
			return;
		}
		const qint64 itemId = topItemAt(snapshot.source, point);
		if (itemId < 0) return;
		const QPointF focus = itemPoint(snapshot.source, itemId, point);
		draggingFocus = snapshot.punch;
		draggingLayout = !snapshot.punch;
		dragEditStarted = false;
		lastDrag = point;
		dragItem = itemId;
		grabMouse();
		if (interacted) interacted(itemId, focus, !snapshot.punch);
	}

	void mouseMoveEvent(QMouseEvent *event) override
	{
		const State snapshot = snapshotState();
		const QSize canvas = sourceSize(snapshot.source);
		const QRectF surface = canvasRect(canvas);
		if (surface.isEmpty()) return;
		const QPointF point((event->position().x() - surface.left()) * canvas.width() / surface.width(),
			(event->position().y() - surface.top()) * canvas.height() / surface.height());
		if (!draggingResize && !draggingFocus && !draggingLayout) {
			auto *scene = sceneFor(snapshot.source);
			auto *selected = scene && snapshot.selected >= 0 ? obs_scene_find_sceneitem_by_id(scene, snapshot.selected) : nullptr;
			const int handle = !snapshot.punch && selected && obs_sceneitem_visible(selected) && !obs_sceneitem_locked(selected) ?
				cornerAt(selected, point, 12.0 * canvas.width() / surface.width()) : -1;
			setCursor(handle >= 0 ? (handle % 2 == 0 ? Qt::SizeFDiagCursor : Qt::SizeBDiagCursor) :
				selected && motionItemContains(selected, point) ? Qt::SizeAllCursor : Qt::CrossCursor);
			return;
		}
		if (dragItem < 0) return;
		if (draggingResize) {
			const double dragThreshold = 5.0 * canvas.width() / surface.width();
			if (!dragEditStarted && (point - resizePressPoint).manhattanLength() < dragThreshold) return;
			const QPointF adjustedPoint = point + resizeCornerStart - resizePressPoint;
			const QPointF diagonal = resizeCornerStart - resizeAnchor;
			const double lengthSquared = QPointF::dotProduct(diagonal, diagonal);
			if (lengthSquared < 1.0) return;
			double desiredX = std::clamp(QPointF::dotProduct(adjustedPoint - resizeAnchor, diagonal) / lengthSquared, 0.05, 20.0);
			double desiredY = desiredX;
			if (resizeFree && resizePolygon.size() == 4) {
				const QPointF horizontal = resizePolygon.at(1) - resizePolygon.at(0);
				const QPointF vertical = resizePolygon.at(3) - resizePolygon.at(0);
				const double determinant = horizontal.x() * vertical.y() - horizontal.y() * vertical.x();
				if (std::abs(determinant) > 1.0) {
					const QPointF offset = adjustedPoint - resizeAnchor;
					desiredX = std::clamp(std::abs((offset.x() * vertical.y() - offset.y() * vertical.x()) / determinant), 0.05, 20.0);
					desiredY = std::clamp(std::abs((horizontal.x() * offset.y() - horizontal.y() * offset.x()) / determinant), 0.05, 20.0);
				}
			}
			if (std::abs(desiredX - resizeAppliedX) < 0.0001 && std::abs(desiredY - resizeAppliedY) < 0.0001) return;
			if (!dragEditStarted && beginEdit) { beginEdit(); dragEditStarted = true; }
			if (transformed) transformed(dragItem, {}, desiredX / resizeAppliedX, desiredY / resizeAppliedY,
				(resizeCorner + 2) % 4, resizeCrop);
			resizeAppliedX = desiredX; resizeAppliedY = desiredY;
			return;
		}
		if (draggingLayout) {
			const double dragThreshold = dragEditStarted ? 0.5 : 5.0 * canvas.width() / surface.width();
			if ((point - lastDrag).manhattanLength() < dragThreshold) return;
			if (!dragEditStarted && beginEdit) { beginEdit(); dragEditStarted = true; }
			if (transformed) transformed(dragItem, point - lastDrag, 1.0, 1.0, -1, false);
			lastDrag = point;
		} else if (interacted) interacted(dragItem, itemPoint(snapshot.source, dragItem, point), false);
	}

	void mouseReleaseEvent(QMouseEvent *event) override
	{
		if (event->button() != Qt::LeftButton) return;
		draggingFocus = false;
		draggingLayout = false;
		draggingResize = false;
		resizeCorner = -1;
		dragItem = -1;
		if (mouseGrabber() == this) releaseMouse();
	}

	void wheelEvent(QWheelEvent *event) override
	{
		const State snapshot = snapshotState();
		if (snapshot.punch || snapshot.selected < 0) return;
		if (beginEdit) beginEdit();
		const double factor = event->angleDelta().y() > 0 ? 1.05 : 1.0 / 1.05;
		if (transformed) transformed(snapshot.selected, {}, factor, factor, -1, false);
		event->accept();
	}

private:
	struct State {
		QByteArray container;
		OBSSource source;
		qint64 selected = -1;
		QSet<qint64> controlled;
		bool punch = true;
		QPointF focus{0.5, 0.42};
		double zoom = 1.5;
	};
	mutable std::mutex stateMutex;
	State state;
	OBSDisplay display;
	bool showing = false;
	bool draggingFocus = false;
	bool draggingLayout = false;
	bool draggingResize = false;
	bool dragEditStarted = false;
	QPointF lastDrag;
	qint64 dragItem = -1;
	int resizeCorner = -1;
	QPointF resizeCornerStart;
	QPointF resizePressPoint;
	QPointF resizeAnchor;
	QPolygonF resizePolygon;
	double resizeAppliedX = 1.0;
	double resizeAppliedY = 1.0;
	bool resizeCrop = false;
	bool resizeFree = false;

	static int cornerAt(obs_sceneitem_t *item, const QPointF &point, double radius)
	{
		const QPolygonF corners = motionItemPolygon(item);
		for (int index = 0; index < corners.size(); ++index) {
			const QPointF distance = point - corners.at(index);
			if (QPointF::dotProduct(distance, distance) <= radius * radius) return index;
		}
		return -1;
	}

	State snapshotState() const
	{
		std::lock_guard lock(stateMutex);
		return state;
	}

	QSize pixelSize() const
	{
		const qreal scale = devicePixelRatioF();
		return QSize(std::max(1, qRound(width() * scale)), std::max(1, qRound(height() * scale)));
	}

	static QSize sourceSize(obs_source_t *source)
	{
		int width = source ? int(obs_source_get_width(source)) : 0;
		int height = source ? int(obs_source_get_height(source)) : 0;
		if (width <= 0 || height <= 0) {
			obs_video_info info{};
			if (obs_get_video_info(&info)) {
				width = int(info.base_width);
				height = int(info.base_height);
			}
		}
		return {std::max(1, width), std::max(1, height)};
	}

	QRectF canvasRect(const QSize &canvas) const
	{
		if (canvas.isEmpty()) return {};
		const QRectF available(contentsRect());
		const qreal scale = std::min(available.width() / canvas.width(), available.height() / canvas.height());
		const QSizeF fitted(canvas.width() * scale, canvas.height() * scale);
		return QRectF(QPointF((available.width() - fitted.width()) / 2.0,
			(available.height() - fitted.height()) / 2.0), fitted);
	}

	static obs_scene_t *sceneFor(obs_source_t *source)
	{
		obs_scene_t *scene = source ? obs_scene_from_source(source) : nullptr;
		return !scene && source ? obs_group_from_source(source) : scene;
	}

	static qint64 topItemAt(obs_source_t *source, const QPointF &point)
	{
		obs_scene_t *scene = sceneFor(source);
		if (!scene) return -1;
		struct Hits { QPointF point; qint64 id = -1; double area = std::numeric_limits<double>::infinity(); } hits{point};
		obs_scene_enum_items(scene, [](obs_scene_t *, obs_sceneitem_t *item, void *opaque) {
			auto &hits = *static_cast<Hits *>(opaque);
			if (obs_sceneitem_locked(item) || !motionItemContains(item, hits.point)) return true;
			const QRectF bounds = motionItemPolygon(item).boundingRect();
			const double area = std::max(1.0, bounds.width() * bounds.height());
			// Full-canvas chat and event overlays often have transparent pixels.
			// Prefer the smaller visible item underneath so inset cameras can be selected by clicking them.
			if (area <= hits.area) { hits.id = obs_sceneitem_get_id(item); hits.area = area; }
			return true;
		}, &hits);
		return hits.id;
	}

	static QPointF itemPoint(obs_source_t *source, qint64 itemId, const QPointF &point)
	{
		obs_scene_t *scene = sceneFor(source);
		obs_sceneitem_t *item = scene ? obs_scene_find_sceneitem_by_id(scene, itemId) : nullptr;
		return motionItemPoint(item, point);
	}

	void createDisplay()
	{
		if (display || !windowHandle() || !windowHandle()->isExposed()) return;
		const QSize pixels = pixelSize();
		gs_init_data info{};
		info.cx = uint32_t(pixels.width());
		info.cy = uint32_t(pixels.height());
		info.format = GS_BGRA;
		info.zsformat = GS_ZS_NONE;
#ifdef _WIN32
		info.window.hwnd = reinterpret_cast<HWND>(windowHandle()->winId());
#else
		return;
#endif
		display = obs_display_create(&info, 0xFF0B0712);
		if (display) obs_display_add_draw_callback(display, &PulseMotionCanvas::render, this);
	}

	static void render(void *opaque, uint32_t displayWidth, uint32_t displayHeight)
	{
		auto *canvas = static_cast<PulseMotionCanvas *>(opaque);
		const State snapshot = canvas->snapshotState();
		OBSSource source = snapshot.source;
		vec4 clearColor;
		vec4_set(&clearColor, 0.015f, 0.01f, 0.03f, 1.0f);
		gs_clear(GS_CLEAR_COLOR, &clearColor, 0.0f, 0);
		if (!source || !displayWidth || !displayHeight) return;
		const QSize sourcePixels = sourceSize(snapshot.source);
		const uint32_t canvasWidth = uint32_t(sourcePixels.width());
		const uint32_t canvasHeight = uint32_t(sourcePixels.height());
		const float scale = std::min(float(displayWidth) / canvasWidth, float(displayHeight) / canvasHeight);
		const int fittedWidth = int(canvasWidth * scale);
		const int fittedHeight = int(canvasHeight * scale);
		const int left = (int(displayWidth) - fittedWidth) / 2;
		const int top = (int(displayHeight) - fittedHeight) / 2;
		gs_viewport_push();
		gs_projection_push();
		gs_ortho(0.0f, float(canvasWidth), 0.0f, float(canvasHeight), -100.0f, 100.0f);
		gs_set_viewport(left, top, fittedWidth, fittedHeight);
		motionDrawSolid(0, 0, float(canvasWidth), float(canvasHeight), 0xFF000000);
		obs_source_video_render(source);
		obs_scene_t *scene = sceneFor(source);
		if (scene) {
			obs_sceneitem_t *selected = snapshot.selected >= 0 ?
				obs_scene_find_sceneitem_by_id(scene, snapshot.selected) : nullptr;
			if (selected) {
				const QPolygonF corners = motionItemPolygon(selected);
				motionDrawLines(corners, 0xFFFF4BD8);
				if (!snapshot.punch && obs_sceneitem_visible(selected) && !obs_sceneitem_locked(selected)) {
					const float outer = 7.0f / scale;
					const float inner = 4.0f / scale;
					for (const QPointF &corner : corners) {
						motionDrawSolid(float(corner.x()) - outer, float(corner.y()) - outer,
							float(corner.x()) + outer, float(corner.y()) + outer, 0xFF101827);
						motionDrawSolid(float(corner.x()) - inner, float(corner.y()) - inner,
							float(corner.x()) + inner, float(corner.y()) + inner, 0xFF5CFFFF);
					}
				}
				if (snapshot.punch) {
					const double regionWidth = 1.0 / snapshot.zoom;
					const double regionHeight = 1.0 / snapshot.zoom;
					const double regionLeft = std::clamp(snapshot.focus.x() - regionWidth / 2.0, 0.0, 1.0 - regionWidth);
					const double regionTop = std::clamp(snapshot.focus.y() - regionHeight / 2.0, 0.0, 1.0 - regionHeight);
					QPolygonF region;
					for (const QPointF &point : {QPointF(regionLeft, regionTop),
						QPointF(regionLeft + regionWidth, regionTop),
						QPointF(regionLeft + regionWidth, regionTop + regionHeight),
						QPointF(regionLeft, regionTop + regionHeight)})
						region << motionCanvasPoint(selected, point);
					motionDrawLines(region, 0xFF5CFFFF);
					const QPointF focus = motionCanvasPoint(selected, snapshot.focus);
					const float radius = std::max(8.0f, 12.0f / scale);
					motionDrawLines({focus + QPointF(-radius, 0), focus + QPointF(radius, 0),
						focus + QPointF(0, -radius), focus + QPointF(0, radius)}, 0xFFFFFFFF, false);
				}
			}
		}
		gs_reset_viewport();
		gs_projection_pop();
		gs_viewport_pop();
	}
};

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
	previewTimer.stop();
	delete editor;
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
	if (!file.open(QIODevice::ReadOnly)) { if (file.exists()) originalStoreValid = false; return; }
	QJsonParseError error;
	const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
	if (error.error != QJsonParseError::NoError || !document.isObject()) { originalStoreValid = false; return; }
	const QJsonObject root = document.object();
	originals = root.value("originals").toArray();
	originalScenes = root.value("originalScenes").toObject();
	if (root.value("version").toInt() == 1 && root.value("actions").isArray())
		actions = root.value("actions").toArray();
}

bool PulseMotionEngine::save()
{
	if (!originalStoreValid) { setStatus("Motion storage could not be read. Recover the saved file before changing output.", true); return false; }
	QDir().mkpath(QFileInfo(storagePath).absolutePath());
	QSaveFile file(storagePath);
	if (!file.open(QIODevice::WriteOnly)) {
		setStatus("Could not save motion actions: " + file.errorString(), true);
		return false;
	}
	const QByteArray data = QJsonDocument(QJsonObject{{"version", 1}, {"actions", actions}, {"originals", originals}, {"originalScenes", originalScenes}}).toJson(QJsonDocument::Indented);
	if (file.write(data) != data.size()) { file.cancelWriting(); setStatus("Motion storage write failed.", true); return false; }
	if (!file.commit()) {
		setStatus("Could not finish saving motion actions.", true);
		return false;
	}
	syncHotkeys();
	return true;
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
	root->setContentsMargins(0, 0, 0, 0);
	root->setSpacing(4);

	auto *intro = new QLabel("Pick a stage → choose a look → arrange either canvas. Save once, trigger from Lumia.");
	intro->setWordWrap(true);
	intro->setObjectName("Kicker");
	root->addWidget(intro);
	intro->hide();

	auto *splitter = new QSplitter(Qt::Vertical);
	splitter->setChildrenCollapsible(false);
	splitter->setHandleWidth(0);
	root->addWidget(splitter, 1);
	auto *libraryPage = new QFrame;
	libraryPage->setMaximumHeight(52);
	auto *libraryLayout = new QHBoxLayout(libraryPage);
	libraryLayout->setContentsMargins(4, 3, 4, 3);
	libraryLayout->setSpacing(8);
	actionList = new QListWidget;
	actionList->hide();
	libraryLayout->addWidget(actionList);
	stagePicker = new QComboBox(libraryPage);
	stagePicker->setMinimumWidth(190);
	stagePicker->setToolTip("Choose the stage to design");
	lookPicker = new QComboBox(libraryPage);
	lookPicker->setMinimumWidth(180);
	lookPicker->setToolTip("Choose a look within this stage");
	libraryLayout->addWidget(new QLabel("Stage"));
	libraryLayout->addWidget(stagePicker, 1);
	libraryLayout->addWidget(new QLabel("Look"));
	libraryLayout->addWidget(lookPicker, 1);
	auto *commandRow = new QHBoxLayout;
	commandRow->setSpacing(6);
	libraryLayout->addLayout(commandRow);
	auto *showBuilder = new QPushButton("BUILD MY SHOW STAGES");
	showBuilder->setObjectName("Primary");
	showBuilder->setProperty("motionShowBuilder", true);
	showBuilder->setToolTip("Build Starting, Intermission, Hangout, Gameplay and Celebration from this imported setup.");
	commandRow->addWidget(showBuilder);
	connect(showBuilder, &QPushButton::clicked, this, [this] {
		const QJsonObject result = createShowStages();
		setStatus(result.value("message").toString(), !result.value("ok").toBool());
	});
	auto *newPunch = new QPushButton("+ CLOSE-UP");
	auto *newLayout = new QPushButton("+ LOOK");
	commandRow->addWidget(newPunch);
	commandRow->addWidget(newLayout);
	newPunch->hide(); newLayout->hide();
	auto *originalButton = new QPushButton("RESTORE ORIGINAL SCENES");
	originalButton->setToolTip("Restore protected original framing, visibility and layers, even after restarting.");
	commandRow->addWidget(originalButton);
	originalButton->hide();
	auto *moreButton = new QToolButton(libraryPage);
	moreButton->setText("•••");
	moreButton->setToolTip("Stage and look tools");
	moreButton->setPopupMode(QToolButton::InstantPopup);
	auto *moreMenu = new QMenu(moreButton);
	connect(moreMenu->addAction("New look…"), &QAction::triggered, newLayout, &QPushButton::click);
	connect(moreMenu->addAction("New camera close-up"), &QAction::triggered, newPunch, &QPushButton::click);
	moreMenu->addSeparator();
	connect(moreMenu->addAction("Restore original scenes"), &QAction::triggered, originalButton, &QPushButton::click);
	auto *quickStartAction = moreMenu->addAction("Quick start · four looks");
	auto *importAction = moreMenu->addAction("Import Move / Lumia JSON…");
	connect(quickStartAction, &QAction::triggered, this, [this] { createStarterStage(); });
	connect(importAction, &QAction::triggered, this, [this] { importFromFile(); });
	moreButton->setMenu(moreMenu);
	commandRow->addWidget(moreButton);
	connect(originalButton, &QPushButton::clicked, this, [this] {
		const QJsonObject result = restoreOriginals();
		setStatus(result.value("message").toString(), !result.value("ok").toBool());
	});

	splitter->addWidget(libraryPage);

	auto *details = new QWidget;
	auto *detailsLayout = new QVBoxLayout(details);
	detailsLayout->setContentsMargins(0, 0, 0, 0);
	auto *workspace = new QSplitter;
	workspace->setChildrenCollapsible(false);
	detailsLayout->addWidget(workspace, 1);
	auto *inspector = new QScrollArea;
	advancedPanel = inspector;
	inspector->setWidgetResizable(true);
	inspector->setFrameShape(QFrame::NoFrame);
	inspector->setMinimumWidth(290);
	inspector->setMaximumWidth(390);
	auto *inspectorBody = new QWidget;
	auto *inspectorLayout = new QVBoxLayout(inspectorBody);
	inspectorLayout->setContentsMargins(4, 4, 4, 4);
	inspectorLayout->setSpacing(8);
	inspector->setWidget(inspectorBody);
	auto inspectorPage = [inspectorLayout](const QString &title) {
		auto *page = new QGroupBox(title);
		auto *form = new QFormLayout(page);
		form->setRowWrapPolicy(QFormLayout::WrapAllRows);
		form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
		form->setContentsMargins(8, 12, 8, 8);
		form->setSpacing(7);
		inspectorLayout->addWidget(page);
		return form;
	};
	auto *form = inspectorPage("Look details");
	auto *timingForm = inspectorPage("Movement");
	auto *stageForm = inspectorPage("Trigger behaviour");
	inspectorLayout->addStretch();
	nameField = new QLineEdit;
	nameField->setPlaceholderText("For example: Camera close-up");
	kindField = new QComboBox;
	kindField->addItem("Zoom inside a camera frame", "punch");
	kindField->addItem("Animated stage look", "layout");
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
	policyField->setVisible(false);
	auto *policyCards = new QWidget;
	auto *policyLayout = new QVBoxLayout(policyCards);
	policyLayout->setContentsMargins(0, 0, 0, 0);
	policyLayout->setSpacing(6);
	auto *policyGroup = new QButtonGroup(policyCards);
	policyGroup->setExclusive(true);
	for (const auto &[label, value] : std::initializer_list<std::pair<QString, QString>>{
			{"GO TO STAGE\nthen move", "switch"}, {"STAGE ONLY\notherwise stop", "only"},
			{"CURRENT STAGE\nmove here", "current"}}) {
		auto *choice = new QToolButton;
		choice->setText(label);
		choice->setCheckable(true);
		choice->setProperty("motionPolicy", value);
		choice->setToolButtonStyle(Qt::ToolButtonTextOnly);
		choice->setMinimumHeight(54);
		choice->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
		choice->setChecked(value == "switch");
		choice->setStyleSheet("QToolButton{border:1px solid #334155;border-radius:8px;padding:7px;background:#101827;color:#cbd5e1;}"
			"QToolButton:checked{border:2px solid #22d3ee;background:#172033;color:white;}");
		policyGroup->addButton(choice);
		policyLayout->addWidget(choice);
		connect(choice, &QToolButton::clicked, this, [this, value] {
			const int row = policyField ? policyField->findData(value) : -1;
			if (row >= 0) policyField->setCurrentIndex(row);
		});
	}
	connect(policyField, &QComboBox::currentIndexChanged, policyCards, [this, policyGroup] {
		for (QAbstractButton *button : policyGroup->buttons())
			button->setChecked(button->property("motionPolicy").toString() == policyField->currentData().toString());
	});

	auto visualRange = [](QSpinBox *spin) {
		auto *host = new QWidget;
		auto *layout = new QHBoxLayout(host);
		layout->setContentsMargins(0, 0, 0, 0);
		auto *slider = new QSlider(Qt::Horizontal);
		slider->setRange(spin->minimum(), spin->maximum());
		slider->setValue(spin->value());
		layout->addWidget(slider, 1);
		layout->addWidget(spin);
		QObject::connect(slider, &QSlider::valueChanged, spin, &QSpinBox::setValue);
		QObject::connect(spin, &QSpinBox::valueChanged, slider, &QSlider::setValue);
		return host;
	};
	form->addRow("Look name", nameField);
	form->addRow("What should happen?", kindField);
	stageForm->addRow("When triggered", policyCards);
	stageForm->addRow("Assigned Stage", stageField);
	form->addRow("Scene or group", sceneField);
	form->addRow("Camera / source", sourceField);
	form->addRow("How close?", visualRange(zoomField));
	auto *presets = new QWidget;
	auto *presetRow = new QHBoxLayout(presets);
	presetRow->setContentsMargins(0, 0, 0, 0);
	for (int zoom : {125, 150, 200}) {
		auto *preset = new QPushButton(QString::number(zoom) + "%");
		presetRow->addWidget(preset);
		connect(preset, &QPushButton::clicked, zoomField, [this, zoom] { zoomField->setValue(zoom); });
	}
	form->addRow(presets);
	auto *centerFocus = new QPushButton("Centre focus");
	form->addRow(centerFocus);
	connect(centerFocus, &QPushButton::clicked, this, [this] {
		editorFocus = QPointF(0.5, 0.5);
		syncVisualCanvas();
	});
	timingForm->addRow("Move into position", visualRange(durationField));
	timingForm->addRow("Hold the close-up", visualRange(holdField));
	timingForm->addRow(restoreField);
	stageForm->addRow(returnStageField);

	auto *canvasCard = new QFrame;
	canvasCard->setObjectName("PulseWeaverCard");
	auto *canvasLayout = new QVBoxLayout(canvasCard);
	canvasLayout->setContentsMargins(12, 10, 12, 10);
	canvasLayout->setSpacing(8);
	auto *canvasHeading = new QLabel("DESIGN CANVAS  ·  EDIT WITHOUT CHANGING OUTPUT");
	canvasHeading->setObjectName("PulseWeaverCardTitle");
	canvasLayout->addWidget(canvasHeading);
	canvasHeading->hide();
	auto *canvasHelp = new QLabel("Choose a source, drag inside to move, or drag a corner handle to resize.");
	canvasHelp->setWordWrap(true);
	canvasHelp->setObjectName("Muted");
	canvasLayout->addWidget(canvasHelp);
	canvasHelp->hide();
	visualCanvas = new PulseMotionCanvas;
	visualCanvas->setMinimumSize(500, 280);
	visualCanvas->activated = [this] { activateDraft(mainContainer); };
	visualCanvas->interacted = [this](qint64 itemId, const QPointF &focus, bool toggle) {
		activateDraft(mainContainer);
		visualCanvasInteraction(itemId, focus, toggle);
	};
	auto *previewSurfaces = new QHBoxLayout;
	previewSurfaces->setSpacing(12);
	auto *portraitPanel = new QWidget(canvasCard);
	auto *portraitLayout = new QVBoxLayout(portraitPanel);
	portraitLayout->setContentsMargins(6, 0, 6, 6);
	portraitLayout->setSpacing(8);
	auto *portraitLabel = new QLabel("9:16 · Portrait · click to edit", portraitPanel);
	portraitLabel->setObjectName("MotionPortraitLabel");
	portraitLayout->addWidget(portraitLabel);
	portraitCanvas = new PulseMotionCanvas(portraitPanel);
	portraitCanvas->activated = [this] { activateDraft(pairedContainer); };
	portraitCanvas->setMinimumSize(170, 280);
	portraitCanvas->setToolTip("Drag to move · corner to resize · Shift + corner to stretch · Alt + corner to crop");
	portraitCanvas->setMaximumWidth(240);
	portraitCanvas->interacted = [this](qint64 itemId, const QPointF &focus, bool toggle) {
		activateDraft(pairedContainer);
		visualCanvasInteraction(itemId, focus, toggle);
	};
	portraitLayout->addWidget(portraitCanvas, 1);
	previewSurfaces->addWidget(portraitPanel, 1);
	auto *landscapePanel = new QWidget(canvasCard);
	auto *landscapeLayout = new QVBoxLayout(landscapePanel);
	landscapeLayout->setContentsMargins(6, 0, 6, 6);
	landscapeLayout->setSpacing(8);
	auto *landscapeLabel = new QLabel("16:9 · Landscape · click to edit", landscapePanel);
	landscapeLabel->setObjectName("MotionLandscapeLabel");
	landscapeLayout->addWidget(landscapeLabel);
	landscapeLayout->addWidget(visualCanvas, 1);
	visualCanvas->setToolTip("Drag to move · corner to resize · Shift + corner to stretch · Alt + corner to crop");
	previewSurfaces->addWidget(landscapePanel, 4);
	canvasLayout->addLayout(previewSurfaces, 1);
	auto *gestureHelp = new QLabel("Drag to move    ◇ Resize    Shift + ◇ Stretch    Alt + ◇ Crop", canvasCard);
	gestureHelp->setObjectName("Muted");
	canvasLayout->addWidget(gestureHelp);
	auto *layerRail = new QFrame;
	layerRail->setObjectName("PulseWeaverCard");
	layerRail->setMinimumWidth(218);
	layerRail->setMaximumWidth(262);
	auto *railLayout = new QVBoxLayout(layerRail);
	railLayout->setContentsMargins(8, 8, 8, 8);
	railLayout->setSpacing(7);
	auto *layersHeading = new QLabel("Layers", layerRail);
	layersHeading->setObjectName("PulseWeaverCardTitle");
	railLayout->addWidget(layersHeading);
	auto *layersHint = new QLabel("Choose a layer to edit. Tap its circle to show or hide it.", layerRail);
	layersHint->setWordWrap(true);
	layersHint->setObjectName("Muted");
	railLayout->addWidget(layersHint);
	auto *sourceScroll = new QScrollArea;
	sourceScroll->setWidgetResizable(true);
	sourceScroll->setFrameShape(QFrame::NoFrame);
	sourceScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	sourceChips = new QWidget;
	sourceChips->setLayout(new QVBoxLayout);
	sourceChips->layout()->setContentsMargins(0, 0, 0, 0);
	sourceScroll->setWidget(sourceChips);
	railLayout->addWidget(sourceScroll, 1);
	lookTools = new QWidget;
	auto *toolRows = new QVBoxLayout(lookTools);
	toolRows->setContentsMargins(0, 0, 0, 0);
	toolRows->setSpacing(5);
	auto *addSource = new QPushButton("+ Source");
	addSource->setToolTip("Add an existing source to this look");
	railLayout->addWidget(addSource);
	connect(addSource, &QPushButton::clicked, this, [this] { addDraftSource(); });
	selectedSourceLabel = new QLabel("", lookTools);
	selectedSourceLabel->setObjectName("PulseWeaverCardTitle");
	selectedSourceLabel->setWordWrap(true);
	toolRows->addWidget(selectedSourceLabel);
	auto *placementRow = new QHBoxLayout;
	auto *arrange = new QToolButton;
	arrange->setText("Position ▾");
	arrange->setPopupMode(QToolButton::InstantPopup);
	auto *arrangeMenu = new QMenu(arrange);
	for (const auto &[label, operation] : std::vector<std::pair<QString, QString>>{
		{"Fit on canvas", "fill"}, {"Fill canvas (crop to fit)", "cover"},
		{"Left half", "left"}, {"Right half", "right"}, {"Top-right inset", "corner"},
		{"Full-canvas overlay · lock framing", "overlay"}}) {
		connect(arrangeMenu->addAction(label), &QAction::triggered, this, [this, operation] { editDraft(operation); });
	}
	arrange->setMenu(arrangeMenu);
	arrangeMenu->addSeparator();
	connect(arrangeMenu->addAction("Adapt landscape to portrait"), &QAction::triggered, this, [this] { adaptPortrait(); });
	connect(arrangeMenu->addAction("Keep this overlay full canvas in every look"), &QAction::triggered, this, [this] { pinOverlayAcrossLooks(); });
	placementRow->addWidget(arrange);
	auto *layerMenuButton = new QToolButton;
	layerMenuButton->setText("Layer ▾");
	layerMenuButton->setPopupMode(QToolButton::InstantPopup);
	auto *layerMenu = new QMenu(layerMenuButton);
	for (const auto &[label, operation] : std::vector<std::pair<QString, QString>>{
		{"Bring to front", "front"}, {"Send to back", "back"}, {"Lock or unlock", "lock"}})
		connect(layerMenu->addAction(label), &QAction::triggered, this, [this, operation] { editDraft(operation); });
	layerMenuButton->setMenu(layerMenu);
	placementRow->addWidget(layerMenuButton);
	toolRows->addLayout(placementRow);
	auto *historyRow = new QHBoxLayout;
	for (const auto &[label, operation] : std::vector<std::pair<QString, QString>>{{"↶ Undo", "undo"}, {"↷ Redo", "redo"}}) {
		auto *button = new QToolButton;
		button->setText(label);
		historyRow->addWidget(button);
		connect(button, &QToolButton::clicked, this, [this, operation] { editDraft(operation); });
	}
	toolRows->addLayout(historyRow);
	railLayout->addWidget(lookTools);
	auto *previewRow = new QHBoxLayout;
	auto *previewButton = new QPushButton("▶ Preview movement");
	previewProgress = new QSlider(Qt::Horizontal);
	previewProgress->setRange(0, 100); previewProgress->setValue(100);
	previewProgress->setToolTip("Scrub from current output framing to this look. Only the design canvas changes.");
	previewRow->addWidget(previewButton); previewRow->addWidget(previewProgress, 1);
	canvasLayout->addLayout(previewRow);
	connect(previewProgress, &QSlider::valueChanged, this, [this](int value) { previewDraft(value); });
	connect(previewButton, &QPushButton::clicked, this, [this] {
		previewClock.restart(); previewProgress->setValue(0); previewDraft(0); previewTimer.start(16);
	});
	connect(&previewTimer, &QTimer::timeout, this, [this] {
		const int progress = int(std::min<qint64>(100, previewClock.elapsed() * 100 / std::max(1, durationField->value())));
		previewProgress->setValue(progress);
		if (progress >= 100) previewTimer.stop();
	});
	visualCanvas->beginEdit = [this] { rememberDraft(); };
	visualCanvas->transformed = [this](qint64 draftId, QPointF delta, double factorX, double factorY,
		int anchorCorner, bool crop) {
		const qint64 original = draftIds.key(draftId, -1);
		OBSSceneItem item = draftItem(original);
		if (!item || obs_sceneitem_locked(item)) return;
		includeDraftItem(original);
		const QPolygonF before = anchorCorner >= 0 ? motionItemPolygon(item) : QPolygonF{};
		Transform value = capture(item);
		value.pos.x += float(delta.x()); value.pos.y += float(delta.y());
		if (crop && anchorCorner >= 0) {
			obs_source_t *source = obs_sceneitem_get_source(item);
			const int width = source ? int(obs_source_get_width(source)) : 0;
			const int height = source ? int(obs_source_get_height(source)) : 0;
			if (width > 1 && height > 1) {
				const int visibleWidth = width - value.crop.left - value.crop.right;
				const int visibleHeight = height - value.crop.top - value.crop.bottom;
				const int xChange = int(std::lround(visibleWidth * (1.0 - factorX)));
				const int yChange = int(std::lround(visibleHeight * (1.0 - factorY)));
				const int draggedCorner = (anchorCorner + 2) % 4;
				if (draggedCorner == 0 || draggedCorner == 3)
					value.crop.left = std::clamp(value.crop.left + xChange, 0, width - value.crop.right - 1);
				else value.crop.right = std::clamp(value.crop.right + xChange, 0, width - value.crop.left - 1);
				if (draggedCorner == 0 || draggedCorner == 1)
					value.crop.top = std::clamp(value.crop.top + yChange, 0, height - value.crop.bottom - 1);
				else value.crop.bottom = std::clamp(value.crop.bottom + yChange, 0, height - value.crop.top - 1);
				if (value.boundsType != OBS_BOUNDS_NONE) {
					value.bounds.x *= float(factorX); value.bounds.y *= float(factorY);
				}
			}
		} else if (value.boundsType == OBS_BOUNDS_NONE) {
			value.scale.x *= float(factorX); value.scale.y *= float(factorY);
		} else {
			if (std::abs(factorX - factorY) > 0.0001)
				value.boundsType = OBS_BOUNDS_STRETCH;
			value.bounds.x *= float(factorX); value.bounds.y *= float(factorY);
		}
		apply(item, value, true);
		if (anchorCorner >= 0 && anchorCorner < before.size()) {
			const QPolygonF after = motionItemPolygon(item);
			if (anchorCorner < after.size()) {
				const QPointF correction = before.at(anchorCorner) - after.at(anchorCorner);
				value.pos.x += float(correction.x()); value.pos.y += float(correction.y());
				apply(item, value, true);
			}
		}
		refreshSummary();
	};
	portraitCanvas->beginEdit = visualCanvas->beginEdit;
	portraitCanvas->transformed = visualCanvas->transformed;

	workspace->addWidget(canvasCard);
	workspace->addWidget(layerRail);
	workspace->addWidget(inspector);
	workspace->setStretchFactor(0, 1);
	workspace->setSizes({950, 230, 310});
	inspector->hide();
	auto *advancedButton = new QToolButton;
	advancedButton->setText("Settings ▸");
	advancedButton->setCheckable(true);
	advancedButton->setToolTip("Timing, source details, stage behaviour and close-up options.");
	commandRow->addWidget(advancedButton);
	connect(advancedButton, &QToolButton::toggled, inspector, [this, inspector, layerRail, advancedButton](bool open) {
		inspector->setVisible(open);
		layerRail->setVisible(!open && kindField->currentData().toString() == "layout");
		advancedButton->setText(open ? "Layers" : "Settings");
	});

	itemTree = new QTreeWidget;
	itemTree->setHeaderLabels({"CONTROL", "SOURCE", "SHOWN", "LAYER"});
	itemTree->setColumnWidth(2, 60);
	itemTree->setColumnWidth(3, 48);
	itemTree->header()->setSectionResizeMode(1, QHeaderView::Stretch);
	itemTree->setMinimumHeight(130);
	itemTree->setColumnWidth(0, 70);
	form->addRow(itemTree);
	auto *selectionTools = new QWidget;
	auto *selectionRow = new QHBoxLayout(selectionTools);
	selectionRow->setContentsMargins(0, 0, 0, 0);
	for (const bool include : {true, false}) {
		auto *button = new QPushButton(include ? "Include all" : "Clear selection");
		selectionRow->addWidget(button);
		connect(button, &QPushButton::clicked, this, [this, include] {
			{
				const QSignalBlocker blocker(itemTree);
				for (int row = 0; row < itemTree->topLevelItemCount(); ++row)
					itemTree->topLevelItem(row)->setCheckState(0, include ? Qt::Checked : Qt::Unchecked);
			}
			refreshSummary();
		});
	}
	form->addRow(selectionTools);
	auto updateMode = [this, form, timingForm, presets, centerFocus, canvasHelp, selectionTools,
		layerRail, addSource, advancedButton] {
		const bool punch = kindField->currentData().toString() == "punch";
		layerRail->setVisible(!punch && !advancedButton->isChecked());
		addSource->setVisible(!punch);
		lookTools->setVisible(!punch && visualSelectedItem >= 0);
		form->setRowVisible(sourceField, punch);
		form->setRowVisible(zoomField->parentWidget(), punch);
		form->setRowVisible(presets, punch);
		form->setRowVisible(centerFocus, punch);
		form->setRowVisible(itemTree, !punch);
		form->setRowVisible(selectionTools, !punch);
		timingForm->setRowVisible(holdField->parentWidget(), punch);
		timingForm->setRowVisible(restoreField, punch);
		canvasHelp->setText(punch ? "3  FRAME CAMERA   ·   Drag the crosshair; cyan shows the crop."
			: "3  ARRANGE SOURCES   ·   Choose below, drag to move, scroll to resize.");
	};
	connect(kindField, &QComboBox::currentIndexChanged, editor, updateMode);
	updateMode();
	summaryLabel = new QLabel;
	summaryLabel->setWordWrap(true);
	summaryLabel->setObjectName("Muted");
	detailsLayout->addWidget(summaryLabel);
	summaryLabel->hide();
	auto *buttons = new QHBoxLayout;
	saveButton = new QPushButton("Save look");
	saveButton->setObjectName("Primary");
	auto *run = new QPushButton("Apply saved look");
	run->setToolTip("Runs the saved action on the real output. Save your edits first.");
	auto *stop = new QPushButton("STOP + RESTORE");
	auto *remove = new QPushButton("DELETE");
	buttons->addWidget(saveButton);
	buttons->addWidget(run);
	buttons->addWidget(stop);
	stop->hide();
	connect(moreMenu->addAction("Stop movement and restore"), &QAction::triggered, stop, &QPushButton::click);
	buttons->addStretch();
	buttons->addWidget(remove);
	remove->hide();
	connect(moreMenu->addAction("Delete this look"), &QAction::triggered, remove, &QPushButton::click);
	detailsLayout->addLayout(buttons);
	statusLabel = new QLabel("Choose an action or create one. Running changes the real output; editing does not.");
	statusLabel->setWordWrap(true);
	statusLabel->setObjectName("Muted");
	detailsLayout->addWidget(statusLabel);
	splitter->addWidget(details);
	splitter->setStretchFactor(1, 1);
	splitter->setSizes({46, 900});

	connect(newPunch, &QPushButton::clicked, this, [this] {
		if (!mayLeaveDraft()) return;
		editingId.clear();
		loadActionIntoEditor(QJsonObject{{"kind", "punch"}, {"name", "Camera close-up"}, {"policy", "switch"},
			{"zoomPercent", 150}, {"durationMs", 250}, {"holdMs", 5000}, {"restore", true}});
	});

	connect(newLayout, &QPushButton::clicked, this, [this] {
		if (!mayLeaveDraft()) return;
		bool ok = false;
		const QString name = QInputDialog::getText(editor, "Create a stage look", "Name this look (Game, Chatting, BRB, Printer…)", QLineEdit::Normal, "Chatting", &ok);
		if (ok && !name.trimmed().isEmpty()) createLook(name.trimmed(), false);
	});
	auto *duplicateLook = new QPushButton("DUPLICATE LOOK");
	commandRow->insertWidget(3, duplicateLook);
	duplicateLook->hide();
	connect(moreMenu->addAction("Duplicate this look…"), &QAction::triggered, duplicateLook, &QPushButton::click);
	connect(duplicateLook, &QPushButton::clicked, this, [this] {
		bool ok = false;
		const QString name = QInputDialog::getText(editor, "Duplicate look", "Name the new look", QLineEdit::Normal, nameField->text() + " copy", &ok);
		if (ok && !name.trimmed().isEmpty()) createLook(name.trimmed(), true);
	});
	connect(actionList, &QListWidget::currentItemChanged, this, [this](QListWidgetItem *item) {
		if (!item) return;
		loadActionIntoEditor(actionByIdentity(item->data(Qt::UserRole).toString()));
		QTimer::singleShot(0, this, [this] { refreshNavigation(); });
	});
	connect(stagePicker, &QComboBox::currentIndexChanged, this, [this] {
		if (!mayLeaveDraft()) { refreshNavigation(); return; }
		const QString group = stagePicker->currentData().toString();
		for (int row = 0; row < actionList->count(); ++row) {
			const QJsonObject action = actionByIdentity(actionList->item(row)->data(Qt::UserRole).toString());
			const QString actionGroup = action.value("stage").toString().isEmpty() ?
				(action.value("container").toString().startsWith("PW ") ? action.value("container").toString() : QString("Other moves")) :
				action.value("stage").toString();
			if (actionGroup == group) { actionList->setCurrentRow(row); break; }
		}
		refreshNavigation();
	});
	connect(lookPicker, &QComboBox::currentIndexChanged, this, [this] {
		const QString id = lookPicker->currentData().toString();
		if (id.isEmpty() || id == editingId) return;
		if (!mayLeaveDraft()) { refreshNavigation(); return; }
		for (int row = 0; row < actionList->count(); ++row)
			if (actionList->item(row)->data(Qt::UserRole).toString() == id) {
				actionList->setCurrentRow(row); break;
			}
	});
	connect(kindField, &QComboBox::currentIndexChanged, this, [this] { if (!loadingDraft) resetDraft(); populateItems(); refreshDraftRows(); refreshSummary(); });
	connect(policyField, &QComboBox::currentIndexChanged, this, [this] { refreshSummary(); });
	connect(stageField, &QComboBox::currentIndexChanged, this, [this] { refreshSummary(); });
	connect(sceneField, &QComboBox::currentIndexChanged, this, [this] {
		visualSelectedItem = -1;
		if (!loadingDraft) {
			parkedDrafts.clear(); loadedTargets = {}; pairedContainer.clear();
			mainContainer = sceneField->currentData().toString();
			resetDraft();
		}
		if (itemTree) itemTree->clear();
		populateSources();
		populateItems();
		refreshDraftRows();
		refreshSummary();
		syncVisualCanvas();
	});
	connect(sourceField, &QComboBox::currentIndexChanged, this, [this] { refreshSummary(); syncVisualCanvas(); });
	connect(zoomField, &QSpinBox::valueChanged, this, [this] { refreshSummary(); syncVisualCanvas(); });
	connect(durationField, &QSpinBox::valueChanged, this, [this] { refreshSummary(); });
	connect(holdField, &QSpinBox::valueChanged, this, [this] { refreshSummary(); });
	connect(restoreField, &QCheckBox::toggled, this, [this] { refreshSummary(); });
	connect(returnStageField, &QCheckBox::toggled, this, [this] { refreshSummary(); });
	connect(itemTree, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem *entry, int column) {
		if (column == 2 && !loadingDraft) {
			OBSSceneItem item = draftItem(entry->data(0, Qt::UserRole).toString().toLongLong());
			if (item) {
				rememberDraft();
				includeDraftItem(entry->data(0, Qt::UserRole).toString().toLongLong());
				obs_sceneitem_set_visible(item, entry->checkState(2) == Qt::Checked);
			}
		}
		refreshSummary();
	});
	connect(itemTree, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem *entry) {
		if (entry) visualSelectedItem = entry->data(0, Qt::UserRole).toString().toLongLong();
		refreshSourceChips();
		syncVisualCanvas();
	});
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
	auto *restoreLastButton = new QPushButton("RESTORE LAST");
	restoreLastButton->setToolTip("Restore sources from the last completed layout action.");
	buttons->insertWidget(3, restoreLastButton);
	restoreLastButton->hide();
	connect(moreMenu->addAction("Restore last layout"), &QAction::triggered, restoreLastButton, &QPushButton::click);
	connect(restoreLastButton, &QPushButton::clicked, this, [this] {
		const QJsonObject result = restoreLast();
		setStatus(result.value("message").toString(), !result.value("ok").toBool());
	});

	refreshEditor();
	const QString firstActionId = editingId;
	if (!firstActionId.isEmpty()) {
		auto *initialLoad = new QTimer(editor);
		initialLoad->setInterval(500);
		connect(initialLoad, &QTimer::timeout, this, [this, initialLoad, firstActionId, attempts = 0]() mutable {
			if (!editor || editingId != firstActionId || ++attempts > 40) {
				initialLoad->stop();
				initialLoad->deleteLater();
				return;
			}
			const QJsonObject action = actionByIdentity(firstActionId);
			const QString container = action.value("container").toString();
			if (sceneField->currentData().toString() != container || !itemTree->topLevelItemCount() || !draftScene) {
				populateScenes();
				loadActionIntoEditor(action);
			}
			if (sceneField->currentData().toString() == container && itemTree->topLevelItemCount() && draftScene) {
				initialLoad->stop();
				initialLoad->deleteLater();
			}
		});
		initialLoad->start();
	}
	if (actions.isEmpty())
		newPunch->click();
	return editor;
}

void PulseMotionEngine::refreshEditor()
{
	if (editor) {
		const bool built = std::any_of(actions.begin(), actions.end(), [](const QJsonValue &value) {
			return value.toObject().value("stage").toString() == "PW Starting";
		});
		for (QPushButton *button : editor->findChildren<QPushButton *>())
			if (button->property("motionShowBuilder").toBool()) {
				button->setVisible(!built);
			}
	}
	populateStages();
	populateScenes();
	if (actionList) {
		const QString selected = editingId;
		actionList->clear();
		for (const QJsonValue &value : actions) {
			const QJsonObject action = value.toObject();
			auto *item = new QListWidgetItem(action.value("name").toString("Unnamed action"), actionList);
			item->setText(item->text() + "\n" + (action.value("kind") == "punch" ? "Close-up" : "Layout")
				+ "  ·  " + QString::number(action.value("durationMs").toInt()) + " ms");
			item->setData(Qt::UserRole, action.value("id").toString());
			item->setToolTip(action.value("summary").toString());
			if (action.value("draft").toBool()) item->setText(item->text() + "  ·  REVIEW");
			if (action.value("id").toString() == selected) actionList->setCurrentItem(item);
		}
		if (!actionList->currentItem() && actionList->count()) {
			int preferred = 0;
			for (int row = 0; row < actionList->count(); ++row) {
				const QJsonObject candidate = actionByIdentity(actionList->item(row)->data(Qt::UserRole).toString());
				if (candidate.value("stage").toString().startsWith("PW ")) { preferred = row; break; }
			}
			actionList->setCurrentRow(preferred);
		}
	}
	refreshNavigation();
	populateSources();
	populateItems();
	refreshDraftRows();
	if (actionList && actionList->currentItem())
		loadActionIntoEditor(actionByIdentity(actionList->currentItem()->data(Qt::UserRole).toString()));
	refreshSummary();
	syncVisualCanvas();
}

void PulseMotionEngine::refreshNavigation()
{
	if (!stagePicker || !lookPicker || !actionList) return;
	QStringList groups;
	QHash<QString, int> counts;
	for (const QJsonValue &value : actions) {
		const QJsonObject action = value.toObject();
		if (action.value("collection").toString() != motionCollection()) continue;
		const QString group = action.value("stage").toString().isEmpty() ?
			(action.value("container").toString().startsWith("PW ") ? action.value("container").toString() : QString("Other moves")) :
			action.value("stage").toString();
		if (!counts.contains(group)) groups.append(group);
		counts[group]++;
	}
	const QJsonObject current = actionByIdentity(editingId);
	const QString currentGroup = current.value("stage").toString().isEmpty() ?
		(current.value("container").toString().startsWith("PW ") ? current.value("container").toString() : QString("Other moves")) :
		current.value("stage").toString();
	if (!current.isEmpty()) selectedStageGroup = currentGroup;
	std::stable_sort(groups.begin(), groups.end(), [](const QString &a, const QString &b) {
		return a.startsWith("PW ") && !b.startsWith("PW ");
	});
	if (!groups.contains(selectedStageGroup)) selectedStageGroup = groups.value(0);
	const QSignalBlocker stageBlocker(stagePicker);
	stagePicker->clear();
	for (const QString &group : groups)
		stagePicker->addItem(group.startsWith("PW ") ? group.mid(3) : group, group);
	stagePicker->setCurrentIndex(std::max(0, stagePicker->findData(selectedStageGroup)));
	const QSignalBlocker lookBlocker(lookPicker);
	lookPicker->clear();
	for (const QJsonValue &value : actions) {
		const QJsonObject action = value.toObject();
		if (action.value("collection").toString() != motionCollection()) continue;
		const QString group = action.value("stage").toString().isEmpty() ?
			(action.value("container").toString().startsWith("PW ") ? action.value("container").toString() : QString("Other moves")) :
			action.value("stage").toString();
		if (group != selectedStageGroup) continue;
		const QString id = action.value("id").toString();
		const QString label = action.value("name").toString().section(" · ", -1);
		lookPicker->addItem(label, id);
	}
	lookPicker->setCurrentIndex(std::max(0, lookPicker->findData(editingId)));
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
		if (!obs_scene_from_source(source)) return true;
		const QString name = QString::fromUtf8(obs_source_get_name(source));
		if (!name.isEmpty() && !pair->second->contains(name)) {
			pair->second->insert(name);
			pair->first->append(QJsonObject{{"name", name}, {"kind", "scene"}});
		}
		return true;
	}, &context);
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
	obs_source_t *source = motionSource(container);
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
	const QSignalBlocker blocker(itemTree);
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
			"", ""});
		item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
		item->setCheckState(0, selected.contains(key) ? Qt::Checked : Qt::Unchecked);
		item->setData(0, Qt::UserRole, key);
		item->setData(1, Qt::UserRole, row.value("source").toString());
		item->setCheckState(2, row.value("visible").toBool() ? Qt::Checked : Qt::Unchecked);
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
	loadingDraft = true;
	previewTimer.stop();
	parkedDrafts.clear();
	loadedTargets = action.value("items").toArray();
	mainContainer = action.value("container").toString(action.value("scene").toString());
	pairedContainer.clear();
	if (QComboBox *selector = stageSelector()) {
		const int stage = findStage(action.value("stage").toString());
		if (stage >= 0) {
			const QJsonObject config = QJsonDocument::fromJson(selector->itemData(stage).toString().toUtf8()).object();
			if (config.value("horizontal").toString() == mainContainer)
				pairedContainer = config.value("vertical").toString();
		}
	}
	editingId = action.value("id").toString();
	nameField->setText(action.value("name").toString());
	kindField->setCurrentIndex(std::max(0, kindField->findData(action.value("kind").toString("punch"))));
	policyField->setCurrentIndex(std::max(0, policyField->findData(action.value("policy").toString("switch"))));
	const int stageRow = stageField->findData(action.value("stage").toString());
	if (stageRow >= 0) stageField->setCurrentIndex(stageRow);
	const int sceneRow = sceneField->findData(action.value("container").toString(action.value("scene").toString()));
	if (sceneRow >= 0) sceneField->setCurrentIndex(sceneRow);
	if (mainContainer.isEmpty()) mainContainer = sceneField->currentData().toString();
	populateSources();
	const QString target = action.value("itemId").toString() + "|" + action.value("source").toString();
	const int sourceRow = sourceField->findData(target);
	if (sourceRow >= 0) sourceField->setCurrentIndex(sourceRow);
	zoomField->setValue(action.value("zoomPercent").toInt(150));
	durationField->setValue(action.value("durationMs").toInt(action.value("kind") == "punch" ? 250 : 750));
	holdField->setValue(action.value("holdMs").toInt(5000));
	restoreField->setChecked(action.value("restore").toBool(action.value("kind") == "punch"));
	returnStageField->setChecked(action.value("returnStage").toBool());
	editorFocus = QPointF(std::clamp(action.value("focusX").toDouble(0.5), 0.0, 1.0),
		std::clamp(action.value("focusY").toDouble(0.42), 0.0, 1.0));
	visualSelectedItem = action.value("itemId").toString().toLongLong();
	populateItems();
	const QJsonArray items = action.value("items").toArray();
	const QSignalBlocker blocker(itemTree);
	for (int row = 0; row < itemTree->topLevelItemCount(); ++row) {
		QTreeWidgetItem *treeItem = itemTree->topLevelItem(row);
		const QString id = treeItem->data(0, Qt::UserRole).toString();
		const bool checked = std::any_of(items.begin(), items.end(), [this, &id](const QJsonValue &value) {
			return value.toObject().value("itemId").toString() == id &&
				value.toObject().value("container").toString(mainContainer) == mainContainer;
		});
		treeItem->setCheckState(0, checked ? Qt::Checked : Qt::Unchecked);
	}
	resetDraft(action.value("items").toArray());
	loadingDraft = false;
	if (!pairedContainer.isEmpty()) {
		activateDraft(pairedContainer);
		activateDraft(mainContainer);
	}
	refreshSummary();
	syncVisualCanvas();
	setStatus(action.value("draft").toBool() ? "Imported draft: review the targets and press Save Action before running it." : "Editing does not change the live output.");
	draftDirty = false;
	if (saveButton) saveButton->setText("Save look");
}

bool PulseMotionEngine::mayLeaveDraft()
{
	if (!draftDirty) return true;
	const auto choice = QMessageBox::question(editor, "Save this look?",
		"You have changes to “" + nameField->text() + "”. Save them before choosing another look?",
		QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
	if (choice == QMessageBox::Cancel) return false;
	if (choice == QMessageBox::Save) { saveEditorAction(); return !draftDirty; }
	return true;
}

QJsonObject PulseMotionEngine::editorAction() const
{
	QJsonObject action{{"version", 1},
		{"id", editingId.isEmpty() ? QUuid::createUuid().toString(QUuid::WithoutBraces) : editingId},
		{"name", cleanName(nameField ? nameField->text() : QString(), "Unnamed action")},
		{"kind", kindField ? kindField->currentData().toString() : QString("punch")},
		{"activateScene", kindField && kindField->currentData().toString() == "layout"},
		{"policy", policyField ? policyField->currentData().toString() : QString("switch")},
		{"stage", stageField ? stageField->currentData().toString() : QString()},
		{"container", mainContainer.isEmpty() && sceneField ? sceneField->currentData().toString() : mainContainer},
		{"zoomPercent", zoomField ? zoomField->value() : 150},
		{"focusX", editorFocus.x()},
		{"focusY", editorFocus.y()},
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
		const QString container = draftContainer;
		for (int row = 0; row < itemTree->topLevelItemCount(); ++row) {
			QTreeWidgetItem *treeItem = itemTree->topLevelItem(row);
			if (treeItem->checkState(0) != Qt::Checked) continue;
			const qint64 id = treeItem->data(0, Qt::UserRole).toString().toLongLong();
			const QString source = treeItem->data(1, Qt::UserRole).toString();
			OBSSceneItem item = draftItem(id);
			if (!item) item = resolveItem(container, id, source, false);
			if (!item) continue;
			items.append(QJsonObject{{"container", container}, {"itemId", QString::number(id)}, {"source", source},
				{"transform", serialize(capture(item))}});
		}
		for (auto it = parkedDrafts.cbegin(); it != parkedDrafts.cend(); ++it) {
			if (it.key() == draftContainer) continue;
			for (qint64 id : it->controlled) {
				auto *item = obs_scene_find_sceneitem_by_id(it->scene, it->ids.value(id, -1));
				if (!item) continue;
				items.append(QJsonObject{{"container", it.key()}, {"itemId", QString::number(id)},
					{"source", QString::fromUtf8(obs_source_get_name(obs_sceneitem_get_source(item)))},
					{"transform", serialize(capture(item))}});
			}
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
	char *collection = obs_frontend_get_current_scene_collection();
	action.insert("collection", QString::fromUtf8(collection ? collection : ""));
	bfree(collection);
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
	const QJsonArray before = actions;
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
	if (!save()) { actions = before; return; }
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
	syncVisualCanvas();
}

void PulseMotionEngine::syncVisualCanvas()
{
	if (!visualCanvas || !sceneField || !kindField) return;
	const bool punch = kindField->currentData().toString() == "punch";
	QSet<qint64> controlled;
	qint64 selected = visualSelectedItem;
	if (punch && sourceField) {
		selected = sourceField->currentData().toString().section('|', 0, 0).toLongLong();
		visualSelectedItem = selected;
	} else if (itemTree) {
		for (int row = 0; row < itemTree->topLevelItemCount(); ++row) {
			QTreeWidgetItem *item = itemTree->topLevelItem(row);
			if (item->checkState(0) == Qt::Checked) controlled.insert(item->data(0, Qt::UserRole).toString().toLongLong());
		}
	}
	obs_source_t *draftSource = nullptr;
	if (!punch && draftScene) {
		selected = draftIds.value(selected, -1);
		QSet<qint64> draftControlled;
		for (qint64 id : controlled) if (draftIds.contains(id)) draftControlled.insert(draftIds.value(id));
		controlled = draftControlled;
		if (previewScene) {
			selected = previewIds.value(selected, -1);
			controlled.clear();
		}
		draftSource = obs_scene_get_source(previewScene ? previewScene.Get() : draftScene.Get());
	}
	auto updateCanvas = [&](PulseMotionCanvas *canvas, const QString &container) {
		if (!canvas) return;
		canvas->setVisible(!container.isEmpty());
		if (container == draftContainer) {
			canvas->setMotionState(container, selected, controlled, punch, editorFocus,
				zoomField ? zoomField->value() / 100.0 : 1.5, draftSource);
		} else {
			const auto parked = parkedDrafts.value(container);
			const auto preview = pairedPreviews.value(container);
			canvas->setMotionState(container, -1, {}, false, {}, 1.0,
				previewScene && preview.scene ? obs_scene_get_source(preview.scene) :
				parked.scene ? obs_scene_get_source(parked.scene) : nullptr);
		}
	};
	updateCanvas(visualCanvas, mainContainer);
	updateCanvas(portraitCanvas, pairedContainer);
	if (editor) {
		for (const auto &[name, container] : std::vector<std::pair<QString, QString>>{
			{"MotionPortraitLabel", pairedContainer}, {"MotionLandscapeLabel", mainContainer}}) {
			if (auto *label = editor->findChild<QLabel *>(name))
				label->setStyleSheet(container == draftContainer ? "color:#76caff;font-weight:600;" : "color:#aebdd0;");
		}
	}
}

void PulseMotionEngine::visualCanvasInteraction(qint64 itemId, const QPointF &focus, bool toggleLayoutItem)
{
	if (!kindField || !sourceField || !itemTree) return;
	if (kindField->currentData().toString() == "layout") itemId = draftIds.key(itemId, -1);
	visualSelectedItem = itemId;
	if (kindField->currentData().toString() == "punch") {
		for (int row = 0; row < sourceField->count(); ++row) {
			if (sourceField->itemData(row).toString().section('|', 0, 0).toLongLong() == itemId) {
				sourceField->setCurrentIndex(row);
				break;
			}
		}
		editorFocus = QPointF(std::clamp(focus.x(), 0.0, 1.0), std::clamp(focus.y(), 0.0, 1.0));
	} else if (toggleLayoutItem) {
		for (int row = 0; row < itemTree->topLevelItemCount(); ++row) {
			QTreeWidgetItem *item = itemTree->topLevelItem(row);
			if (item->data(0, Qt::UserRole).toString().toLongLong() != itemId) continue;
			itemTree->setCurrentItem(item);
			break;
		}
	}
	refreshSummary();
	syncVisualCanvas();
	refreshSourceChips();
}


namespace {
std::vector<obs_sceneitem_t *> motionItems(obs_scene_t *scene)
{
	std::vector<obs_sceneitem_t *> result;
	if (scene) obs_scene_enum_items(scene, [](obs_scene_t *, obs_sceneitem_t *item, void *data) {
		static_cast<std::vector<obs_sceneitem_t *> *>(data)->push_back(item);
		return true;
	}, &result);
	return result;
}

obs_sceneitem_t *motionAddSource(obs_scene_t *scene, obs_source_t *source)
{
	// Imported scenes can have a counter lower than manually assigned item IDs.
	qint64 nextId = 1;
	for (auto *existing : motionItems(scene))
		nextId = std::max(nextId, obs_sceneitem_get_id(existing) + 1);
	auto *item = obs_scene_add(scene, source);
	if (item && obs_sceneitem_get_id(item) < nextId)
		obs_sceneitem_set_id(item, nextId);
	return item;
}
}

void PulseMotionEngine::resetDraft(const QJsonArray &saved)
{
	previewTimer.stop();
	previewScene = nullptr;
	draftIds.clear();
	draftScene = nullptr;
	undoStates.clear(); redoStates.clear();
	draftContainer = sceneField ? sceneField->currentData().toString() : QString();
	OBSSourceAutoRelease source = motionSource(draftContainer);
	obs_scene_t *scene = source ? obs_scene_from_source(source) : nullptr;
	if (!scene && source) scene = obs_group_from_source(source);
	if (!scene) return;
	OBSSceneAutoRelease copy = obs_scene_duplicate(scene, "Motion editor draft", OBS_SCENE_DUP_PRIVATE_REFS);
	draftScene = copy;
	const auto originals = motionItems(scene), copies = motionItems(draftScene);
	for (size_t i = 0; i < std::min(originals.size(), copies.size()); ++i)
		draftIds.insert(obs_sceneitem_get_id(originals[i]), obs_sceneitem_get_id(copies[i]));
	applyDraftSnapshot(saved);
	if (previewProgress) { const QSignalBlocker blocker(previewProgress); previewProgress->setValue(100); }
}

void PulseMotionEngine::activateDraft(const QString &container)
{
	if (container.isEmpty() || container == draftContainer || !sceneField) return;
	ParkedDraft parked;
	parked.scene = draftScene;
	parked.ids = draftIds;
	parked.undo = undoStates; parked.redo = redoStates;
	for (int row = 0; itemTree && row < itemTree->topLevelItemCount(); ++row) {
		auto *item = itemTree->topLevelItem(row);
		if (item->checkState(0) == Qt::Checked)
			parked.controlled.insert(item->data(0, Qt::UserRole).toString().toLongLong());
	}
	if (!draftContainer.isEmpty()) parkedDrafts.insert(draftContainer, parked);
	previewTimer.stop(); previewScene = nullptr;
	const QSignalBlocker sceneBlocker(sceneField);
	int index = sceneField->findData(container);
	if (index < 0) { sceneField->addItem(container, container); index = sceneField->count() - 1; }
	sceneField->setCurrentIndex(index);
	const QSignalBlocker treeBlocker(itemTree);
	itemTree->clear();
	populateSources(); populateItems();
	if (parkedDrafts.contains(container)) {
		const auto existing = parkedDrafts.value(container);
		draftContainer = container; draftScene = existing.scene; draftIds = existing.ids;
		undoStates = existing.undo; redoStates = existing.redo;
		for (int row = 0; row < itemTree->topLevelItemCount(); ++row) {
			auto *item = itemTree->topLevelItem(row);
			item->setCheckState(0, existing.controlled.contains(item->data(0, Qt::UserRole).toString().toLongLong()) ? Qt::Checked : Qt::Unchecked);
		}
	} else {
		resetDraft(loadedTargets);
		for (int row = 0; row < itemTree->topLevelItemCount(); ++row) {
			auto *item = itemTree->topLevelItem(row);
			// A paired canvas is a complete look. Capture hidden sources too so
			// switching back from another look cannot leave its layers visible.
			item->setCheckState(0, Qt::Checked);
			for (const auto &value : loadedTargets) {
				const auto target = value.toObject();
				if (target.value("container").toString() == container && target.value("itemId").toString() == item->data(0, Qt::UserRole).toString())
					item->setCheckState(0, Qt::Checked);
			}
		}
	}
	visualSelectedItem = -1;
	refreshDraftRows(); syncVisualCanvas();
}

void PulseMotionEngine::includeDraftItem(qint64 originalId)
{
	const QSignalBlocker blocker(itemTree);
	for (int row = 0; itemTree && row < itemTree->topLevelItemCount(); ++row) {
		auto *item = itemTree->topLevelItem(row);
		if (item->data(0, Qt::UserRole).toString().toLongLong() == originalId)
			item->setCheckState(0, Qt::Checked);
	}
}

void PulseMotionEngine::addDraftSource(QString selected)
{
	if (!draftScene) return;
	QStringList names;
	obs_enum_sources([](void *opaque, obs_source_t *source) {
		if (!obs_scene_from_source(source) && !obs_group_from_source(source) &&
			(obs_source_get_output_flags(source) & OBS_SOURCE_VIDEO))
			static_cast<QStringList *>(opaque)->append(QString::fromUtf8(obs_source_get_name(source)));
		return true;
	}, &names);
	names.removeDuplicates(); names.sort(Qt::CaseInsensitive);
	if (selected.isEmpty()) {
	QInputDialog picker(editor);
	picker.setWindowTitle("Add an existing source");
	picker.setLabelText("Find a source for " + draftContainer + ". It starts hidden until you show it in a saved look.");
	picker.setComboBoxItems(names);
	picker.setComboBoxEditable(true);
	picker.setOkButtonText("Add source");
	picker.resize(560, 160);
	if (auto *combo = picker.findChild<QComboBox *>()) {
		combo->setInsertPolicy(QComboBox::NoInsert);
		combo->setCurrentIndex(-1);
		combo->lineEdit()->setPlaceholderText("Search your existing sources…");
		combo->completer()->setFilterMode(Qt::MatchContains);
		combo->completer()->setCaseSensitivity(Qt::CaseInsensitive);
	}
	if (picker.exec() != QDialog::Accepted) return;
	selected = picker.textValue().trimmed();
	}
	if (!names.contains(selected)) { setStatus("Choose an existing source from the search results.", true); return; }
	OBSSourceAutoRelease owner = motionSource(draftContainer);
	OBSSourceAutoRelease source = motionSource(selected);
	obs_scene_t *scene = owner ? obs_scene_from_source(owner) : nullptr;
	if (!scene || !source) { setStatus("The scene or source is no longer available.", true); return; }
	for (auto *item : motionItems(scene)) {
		if (obs_sceneitem_get_source(item) == source.Get()) {
			setStatus("That source is already in this canvas. Select it below to arrange it."); return;
		}
	}
	Execution protection;
	const auto existing = motionItems(scene);
	if (!existing.empty()) {
		Track track; track.item = OBSSceneItem(existing.front()); protection.tracks.push_back(track);
		QString error;
		if (!preserveOriginals(protection, error)) { setStatus(error, true); return; }
	}
	QJsonObject action = editorAction();
	OBSSceneItem item = OBSSceneItem(motionAddSource(scene, source));
	if (!item) { setStatus("Could not add the source.", true); return; }
	obs_sceneitem_set_visible(item, false);
	QJsonArray targets = action.value("items").toArray();
	const QJsonObject hiddenTarget{{"container", draftContainer}, {"itemId", QString::number(obs_sceneitem_get_id(item))},
		{"source", selected}, {"transform", serialize(capture(item))}};
	targets.append(hiddenTarget);
	action.insert("items", targets);
	// Existing looks must explicitly hide newly introduced layers when replayed.
	const QJsonArray before = actions;
	for (int index = 0; index < actions.size(); ++index) {
		auto saved = actions[index].toObject();
		if (saved.value("collection").toString() != motionCollection() || saved.value("kind") != "layout") continue;
		QStringList containers{saved.value("container").toString()};
		if (auto *selector = stageSelector()) {
			const int row = findStage(saved.value("stage").toString());
			if (row >= 0) containers.append(QJsonDocument::fromJson(selector->itemData(row).toString().toUtf8()).object().value("vertical").toString());
		}
		if (!containers.contains(draftContainer)) continue;
		auto savedTargets = saved.value("items").toArray();
		savedTargets.append(hiddenTarget);
		saved.insert("items", savedTargets);
		actions[index] = saved;
	}
	if (!save()) { actions = before; obs_sceneitem_remove(item); return; }
	const QString canvas = draftContainer;
	const qint64 id = obs_sceneitem_get_id(item);
	obs_frontend_save();
	loadActionIntoEditor(action);
	activateDraft(canvas);
	visualSelectedItem = id;
	refreshSourceChips(); syncVisualCanvas();
	draftDirty = true;
	if (saveButton) saveButton->setText("Save look •");
	setStatus("Added “" + selected + "” hidden. Arrange it, show it, then save this look.");
}

void PulseMotionEngine::adaptPortrait()
{
	if (pairedContainer.isEmpty() || mainContainer.isEmpty()) {
		setStatus("Choose a stage with both landscape and portrait scenes first.", true); return;
	}
	const QJsonObject original = editorAction();
	const QString portrait = pairedContainer;
	OBSSourceAutoRelease mainSource = motionSource(mainContainer);
	OBSSourceAutoRelease portraitSource = motionSource(portrait);
	obs_scene_t *scene = portraitSource ? obs_scene_from_source(portraitSource) : nullptr;
	if (!scene || !mainSource) return;
	const float mainWidth = float(obs_source_get_width(mainSource));
	const float mainHeight = float(obs_source_get_height(mainSource));
	const float width = float(obs_source_get_width(portraitSource));
	const float height = float(obs_source_get_height(portraitSource));
	if (mainWidth <= 0 || mainHeight <= 0 || width <= 0 || height <= 0) return;
	QJsonArray landscape;
	for (const auto &value : original.value("items").toArray())
		if (value.toObject().value("container").toString(mainContainer) == mainContainer) landscape.append(value);
	if (landscape.isEmpty()) return;
	bool captureLayout = false;
	for (const auto &value : landscape) {
		const auto target = value.toObject();
		OBSSourceAutoRelease source = motionSource(target.value("source").toString());
		const QString type = source ? QString::fromUtf8(obs_source_get_id(source)) : QString();
		if (deserialize(target.value("transform").toObject()).visible && (type == "game_capture" || type == "monitor_capture")) captureLayout = true;
	}
	activateDraft(portrait);
	const QJsonArray previous = draftSnapshot();
	for (const auto &value : landscape) {
		QString name = value.toObject().value("source").toString();
		if (name == "Chatty") name = "vert chatty";
		if (!obs_scene_find_source(scene, name.toUtf8().constData())) {
			addDraftSource(name);
			if (!obs_scene_find_source(scene, name.toUtf8().constData())) return;
		}
	}
	QJsonArray targets = landscape;
	std::vector<std::pair<obs_sceneitem_t *, Transform>> portraitTargets;
	for (auto *item : motionItems(scene)) {
		obs_source_t *source = obs_sceneitem_get_source(item);
		if (!(obs_source_get_output_flags(source) & OBS_SOURCE_VIDEO)) continue;
		const QString name = QString::fromUtf8(obs_source_get_name(source));
		Transform adapted = capture(item);
		adapted.visible = false;
		for (const auto &value : landscape) {
			const auto target = value.toObject();
			QString mapped = target.value("source").toString();
			if (mapped == "Chatty") mapped = "vert chatty";
			if (mapped != name) continue;
			adapted = deserialize(target.value("transform").toObject());
			const float sourceWidth = float(obs_source_get_width(source));
			const float sourceHeight = float(obs_source_get_height(source));
			const float boxWidth = adapted.boundsType == OBS_BOUNDS_NONE ? sourceWidth * adapted.scale.x : adapted.bounds.x;
			const float boxHeight = adapted.boundsType == OBS_BOUNDS_NONE ? sourceHeight * adapted.scale.y : adapted.bounds.y;
			adapted.pos = {adapted.pos.x * width / mainWidth, adapted.pos.y * height / mainHeight};
			adapted.bounds = {boxWidth * width / mainWidth, boxHeight * height / mainHeight};
			adapted.boundsType = OBS_BOUNDS_SCALE_INNER;
			adapted.boundsAlignment = 0;
			adapted.alignment = OBS_ALIGN_TOP | OBS_ALIGN_LEFT;
			adapted.scale = {1, 1};
			const bool camera = QString::fromUtf8(obs_source_get_id(source)) == "dshow_input" ||
				name.contains("facecam", Qt::CaseInsensitive) || name.contains("printer", Qt::CaseInsensitive);
			if (camera && boxWidth > mainWidth * .8f) adapted.boundsType = OBS_BOUNDS_SCALE_OUTER;
			else if (camera && sourceWidth > 0) adapted.bounds.y = adapted.bounds.x * sourceHeight / sourceWidth;
			const QString type = QString::fromUtf8(obs_source_get_id(source));
			if (captureLayout && name.contains("facecam", Qt::CaseInsensitive) && adapted.visible) {
				adapted.pos = {0, 0}; adapted.bounds = {width, height * .32f}; adapted.boundsType = OBS_BOUNDS_SCALE_INNER;
			}
			if (captureLayout && (type == "game_capture" || type == "monitor_capture") && adapted.visible) {
				adapted.pos = {0, height * .32f}; adapted.bounds = {width, height * .68f};
				adapted.boundsType = type == "game_capture" ? OBS_BOUNDS_SCALE_OUTER : OBS_BOUNDS_SCALE_INNER;
			}
			break;
		}
		if (name == "vert chatty") {
			adapted.pos = {0, 0}; adapted.scale = {1, 1}; adapted.crop = {}; adapted.rotation = 0;
			adapted.boundsType = OBS_BOUNDS_NONE; adapted.locked = true; adapted.visible = true;
		}
		portraitTargets.emplace_back(item, adapted);
	}
	std::stable_sort(portraitTargets.begin(), portraitTargets.end(), [](const auto &a, const auto &b) { return a.second.order < b.second.order; });
	int order = 0;
	for (auto &[item, transform] : portraitTargets) {
		transform.order = order++;
		targets.append(QJsonObject{{"container", portrait}, {"itemId", QString::number(obs_sceneitem_get_id(item))},
			{"source", QString::fromUtf8(obs_source_get_name(obs_sceneitem_get_source(item)))}, {"transform", serialize(transform)}});
	}
	QJsonObject action = original;
	action.insert("items", targets);
	loadActionIntoEditor(action);
	activateDraft(portrait);
	undoStates.push_back(previous);
	draftDirty = true;
	if (saveButton) saveButton->setText("Save look •");
	setStatus("Portrait adapted. Review the framing, then save. Chatty remains full canvas; Undo restores the previous draft.");
}

void PulseMotionEngine::pinOverlayAcrossLooks()
{
	OBSSceneItem selected = draftItem(visualSelectedItem);
	if (!selected) { setStatus("Select the overlay first.", true); return; }
	const QString sourceName = QString::fromUtf8(obs_source_get_name(obs_sceneitem_get_source(selected)));
	editDraft("overlay");
	const QJsonObject edited = editorAction();
	const QJsonArray before = actions;
	int changed = 0;
	for (int index = 0; index < actions.size(); ++index) {
		QJsonObject action = actions[index].toObject();
		if (action.value("collection").toString() != motionCollection() || action.value("kind") != "layout") continue;
		if (action.value("id").toString() == editingId) action = edited;
		QStringList containers{action.value("container").toString()};
		if (auto *selector = stageSelector()) {
			const int row = findStage(action.value("stage").toString());
			if (row >= 0) containers.append(QJsonDocument::fromJson(selector->itemData(row).toString().toUtf8()).object().value("vertical").toString());
		}
		QJsonArray targets = action.value("items").toArray();
		bool updated = false;
		for (const QString &container : containers) {
			if (container.isEmpty()) continue;
			OBSSceneItem item = resolveItem(container, 0, sourceName, false);
			if (!item) continue;
			Transform t = capture(item);
			t.pos = {0, 0}; t.scale = {1, 1}; t.crop = {}; t.rotation = 0;
			t.alignment = OBS_ALIGN_TOP | OBS_ALIGN_LEFT; t.boundsType = OBS_BOUNDS_NONE; t.locked = true; t.visible = true;
			QJsonObject target{{"container", container}, {"source", sourceName},
				{"itemId", QString::number(obs_sceneitem_get_id(item))}, {"transform", serialize(t)}};
			bool replaced = false;
			for (int i = 0; i < targets.size(); ++i) {
				const auto old = targets[i].toObject();
				if (old.value("container").toString(action.value("container").toString()) == container && old.value("source").toString() == sourceName) {
					targets[i] = target; replaced = true; break;
				}
			}
			if (!replaced) targets.append(target);
			updated = true;
		}
		if (updated) {
			action.insert("collection", motionCollection()); action.insert("items", targets);
			actions[index] = action; ++changed;
		}
	}
	if (!save()) { actions = before; return; }
	refreshEditor();
	setStatus("Saved full-canvas “" + sourceName + "” in " + QString::number(changed) + " looks. Its internal layout stays under external control.");
	emitEvent("motion_catalogue_changed");
}

OBSSceneItem PulseMotionEngine::draftItem(qint64 originalId) const
{
	return draftScene && draftIds.contains(originalId) ? OBSSceneItem(obs_scene_find_sceneitem_by_id(draftScene, draftIds.value(originalId))) : OBSSceneItem{};
}

QJsonArray PulseMotionEngine::draftSnapshot() const
{
	QJsonArray result;
	for (auto it = draftIds.cbegin(); it != draftIds.cend(); ++it) {
		OBSSceneItem item = draftItem(it.key());
		if (!item) continue;
		result.append(QJsonObject{{"itemId", QString::number(it.key())}, {"container", draftContainer},
			{"source", QString::fromUtf8(obs_source_get_name(obs_sceneitem_get_source(item)))},
			{"transform", serialize(capture(item))}});
	}
	return result;
}

void PulseMotionEngine::applyDraftSnapshot(const QJsonArray &snapshot)
{
	std::vector<std::pair<OBSSceneItem, Transform>> states;
	for (const auto &value : snapshot) {
		const auto row = value.toObject();
		if (row.value("container").toString(mainContainer) != draftContainer) continue;
		OBSSceneItem item = draftItem(row.value("itemId").toString().toLongLong());
		if (item && QString::fromUtf8(obs_source_get_name(obs_sceneitem_get_source(item))) == row.value("source").toString())
			states.emplace_back(item, deserialize(row.value("transform").toObject()));
	}
	std::sort(states.begin(), states.end(), [](const auto &a, const auto &b) { return a.second.order < b.second.order; });
	for (auto &[item, transform] : states) apply(item, transform, true);
	refreshDraftRows();
}

void PulseMotionEngine::rememberDraft()
{
	if (loadingDraft || !draftScene) return;
	draftDirty = true;
	if (saveButton) saveButton->setText("Save look •");
	previewTimer.stop(); previewScene = nullptr;
	if (previewProgress) { const QSignalBlocker blocker(previewProgress); previewProgress->setValue(100); }
	undoStates.push_back(draftSnapshot());
	if (undoStates.size() > 50) undoStates.erase(undoStates.begin());
	redoStates.clear();
}

void PulseMotionEngine::refreshDraftRows()
{
	if (!itemTree) return;
	const QSignalBlocker blocker(itemTree);
	for (int row = 0; row < itemTree->topLevelItemCount(); ++row) {
		auto *entry = itemTree->topLevelItem(row);
		OBSSceneItem item = draftItem(entry->data(0, Qt::UserRole).toString().toLongLong());
		if (!item) continue;
		entry->setCheckState(2, obs_sceneitem_visible(item) ? Qt::Checked : Qt::Unchecked);
		entry->setText(3, QString::number(obs_sceneitem_get_order_position(item)));
	}
	refreshSourceChips();
}

void PulseMotionEngine::refreshSourceChips()
{
	if (!sourceChips || !itemTree) return;
	QLayout *layout = sourceChips->layout();
	while (QLayoutItem *child = layout->takeAt(0)) {
		if (child->widget()) child->widget()->deleteLater();
		delete child;
	}
	std::vector<QTreeWidgetItem *> layers;
	for (int row = 0; row < itemTree->topLevelItemCount(); ++row)
		layers.push_back(itemTree->topLevelItem(row));
	std::stable_sort(layers.begin(), layers.end(), [](const auto *left, const auto *right) {
		return left->text(3).toInt() > right->text(3).toInt();
	});
	QString selectedName;
	for (QTreeWidgetItem *entry : layers)
		if (entry->data(0, Qt::UserRole).toString().toLongLong() == visualSelectedItem)
			selectedName = entry->text(1);
	if (selectedSourceLabel) selectedSourceLabel->setText(selectedName);
	if (lookTools && kindField)
		lookTools->setVisible(kindField->currentData().toString() == "layout" && !selectedName.isEmpty());
	for (QTreeWidgetItem *entry : layers) {
		const qint64 itemId = entry->data(0, Qt::UserRole).toString().toLongLong();
		const bool visible = entry->checkState(2) == Qt::Checked;
		auto *chip = new QWidget(sourceChips);
		auto *chipRow = new QHBoxLayout(chip);
		chipRow->setContentsMargins(0, 0, 0, 0);
		chipRow->setSpacing(4);
		auto *select = new QToolButton(chip);
		const QString sourceName = entry->text(1);
		select->setText(QFontMetrics(select->font()).elidedText(sourceName, Qt::ElideRight, 154));
		select->setToolTip("Select and arrange " + entry->text(1));
		select->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
		select->setStyleSheet(itemId == visualSelectedItem ?
			"QToolButton{background:#17344a;border:0;border-left:3px solid #46c5f1;border-radius:3px;color:white;padding:7px;text-align:left;}" :
			"QToolButton{background:transparent;border:0;border-left:3px solid transparent;border-radius:3px;color:#c8d7e6;padding:7px;text-align:left;}QToolButton:hover{background:#1a2838;}");
		chipRow->addWidget(select, 1);
		auto *eye = new QToolButton(chip);
		eye->setText(visible ? "◉" : "○");
		eye->setToolTip(visible ? "Hide this source in this look" : "Show this source in this look");
		eye->setStyleSheet("QToolButton{background:transparent;border:0;color:#67cffa;padding:7px;}");
		chipRow->addWidget(eye);
		layout->addWidget(chip);
		connect(select, &QToolButton::clicked, this, [this, itemId] {
			for (int index = 0; index < itemTree->topLevelItemCount(); ++index)
				if (itemTree->topLevelItem(index)->data(0, Qt::UserRole).toString().toLongLong() == itemId) {
					itemTree->setCurrentItem(itemTree->topLevelItem(index)); break;
				}
			refreshSourceChips();
		});
		connect(eye, &QToolButton::clicked, this, [this, itemId] {
			for (int index = 0; index < itemTree->topLevelItemCount(); ++index) {
				QTreeWidgetItem *row = itemTree->topLevelItem(index);
				if (row->data(0, Qt::UserRole).toString().toLongLong() != itemId) continue;
				row->setCheckState(2, row->checkState(2) == Qt::Checked ? Qt::Unchecked : Qt::Checked);
				break;
			}
			refreshSourceChips();
		});
	}
	layout->addItem(new QSpacerItem(0, 0, QSizePolicy::Minimum, QSizePolicy::Expanding));
}

void PulseMotionEngine::editDraft(const QString &operation)
{
	if (!draftScene) return;
	if (operation == "undo" || operation == "redo") {
		auto &from = operation == "undo" ? undoStates : redoStates;
		auto &to = operation == "undo" ? redoStates : undoStates;
		if (from.empty()) return;
		previewTimer.stop(); previewScene = nullptr;
		if (previewProgress) { const QSignalBlocker blocker(previewProgress); previewProgress->setValue(100); }
		to.push_back(draftSnapshot());
		const QJsonArray value = from.back(); from.pop_back();
		applyDraftSnapshot(value);
	} else {
		OBSSceneItem item = draftItem(visualSelectedItem);
		if (!item) { setStatus("Select a source in the canvas or source list first.", true); return; }
		rememberDraft();
		includeDraftItem(visualSelectedItem);
		Transform value = capture(item);
		if (operation == "visibility") value.visible = !value.visible;
		else if (operation == "lock") value.locked = !value.locked;
		else if (operation == "front") value.order = int(draftIds.size()) - 1;
		else if (operation == "back") value.order = 0;
		else if (operation == "larger" || operation == "smaller") {
			const float factor = operation == "larger" ? 1.1f : 1.0f / 1.1f;
			if (value.boundsType == OBS_BOUNDS_NONE) { value.scale.x *= factor; value.scale.y *= factor; }
			else { value.bounds.x *= factor; value.bounds.y *= factor; }
		} else {
			const float width = float(obs_source_get_width(obs_scene_get_source(draftScene)));
			const float height = float(obs_source_get_height(obs_scene_get_source(draftScene)));
			value.rotation = 0; value.alignment = OBS_ALIGN_LEFT | OBS_ALIGN_TOP;
			value.boundsType = OBS_BOUNDS_SCALE_INNER; value.boundsAlignment = 0;
			value.visible = true;
			if (operation == "fill" || operation == "cover") {
				value.pos = {0, 0}; value.bounds = {width, height};
				if (operation == "cover") value.boundsType = OBS_BOUNDS_SCALE_OUTER;
			}
			if (operation == "left") { value.pos = {0, 0}; value.bounds = {width / 2, height}; }
			if (operation == "right") { value.pos = {width / 2, 0}; value.bounds = {width / 2, height}; }
			if (operation == "corner") { value.pos = {width * .68f, height * .03f}; value.bounds = {width * .29f, height * .29f}; }
			if (operation == "overlay") {
				value.pos = {0, 0}; value.scale = {1, 1}; value.crop = {};
				value.boundsType = OBS_BOUNDS_NONE; value.locked = true;
			}
		}
		apply(item, value, true);
	}
	refreshDraftRows(); refreshSummary();
}

void PulseMotionEngine::previewDraft(int progress)
{
	if (!draftScene || !kindField || kindField->currentData().toString() != "layout") return;
	if (progress >= 100) { previewScene = nullptr; pairedPreviews.clear(); syncVisualCanvas(); return; }
	if (!previewScene) {
		pairedPreviews.clear();
		OBSSceneAutoRelease copy = obs_scene_duplicate(draftScene, "Motion transition preview", OBS_SCENE_DUP_PRIVATE_REFS);
		previewScene = copy;
		previewIds.clear();
		const auto draft = motionItems(draftScene), preview = motionItems(previewScene);
		for (size_t i = 0; i < std::min(draft.size(), preview.size()); ++i)
			previewIds.insert(obs_sceneitem_get_id(draft[i]), obs_sceneitem_get_id(preview[i]));
		for (auto it = parkedDrafts.cbegin(); it != parkedDrafts.cend(); ++it) {
			if (it.key() == draftContainer || !it->scene) continue;
			ParkedDraft paired;
			OBSSceneAutoRelease copy = obs_scene_duplicate(it->scene, "Motion paired transition preview", OBS_SCENE_DUP_PRIVATE_REFS);
			paired.scene = copy;
			if (!paired.scene) continue;
			const auto originals = motionItems(it->scene), copies = motionItems(paired.scene);
			for (size_t i = 0; i < std::min(originals.size(), copies.size()); ++i)
				paired.ids.insert(it->ids.key(obs_sceneitem_get_id(originals[i]), -1), obs_sceneitem_get_id(copies[i]));
			pairedPreviews.insert(it.key(), paired);
		}
	}
	for (auto it = draftIds.cbegin(); it != draftIds.cend(); ++it) {
		OBSSceneItem target = draftItem(it.key());
		if (!target) continue;
		OBSSceneItem live = resolveItem(draftContainer, it.key(), QString::fromUtf8(obs_source_get_name(obs_sceneitem_get_source(target))), false);
		auto *preview = obs_scene_find_sceneitem_by_id(previewScene, previewIds.value(it.value(), -1));
		if (!live || !preview) continue;
		const Transform from = capture(live), to = capture(target);
		const QString sourceName = QString::fromUtf8(obs_source_get_name(obs_sceneitem_get_source(target)));
		if (motionGraphicSwitch(sourceName) && from.visible != to.visible) {
			apply(preview, progress < 50 ? from : to, true);
			continue;
		}
		Transform frame = transitionFrame(from, to, progress / 100.0, float(obs_source_get_width(obs_scene_get_source(draftScene))));
		frame.visible = from.visible || to.visible;
		frame.order = from.visible && (progress < 60 || !to.visible) ? from.order : to.order;
		apply(preview, frame, true);
	}
	for (auto it = pairedPreviews.cbegin(); it != pairedPreviews.cend(); ++it) {
		const auto targetDraft = parkedDrafts.value(it.key());
		for (auto id = it->ids.cbegin(); id != it->ids.cend(); ++id) {
			auto *target = obs_scene_find_sceneitem_by_id(targetDraft.scene, targetDraft.ids.value(id.key(), -1));
			auto *preview = obs_scene_find_sceneitem_by_id(it->scene, id.value());
			if (!target || !preview) continue;
			OBSSceneItem live = resolveItem(it.key(), id.key(), QString::fromUtf8(obs_source_get_name(obs_sceneitem_get_source(target))), false);
			if (!live) continue;
			const Transform from = capture(live), to = capture(target);
			const QString sourceName = QString::fromUtf8(obs_source_get_name(obs_sceneitem_get_source(target)));
			if (motionGraphicSwitch(sourceName) && from.visible != to.visible) {
				apply(preview, progress < 50 ? from : to, true);
				continue;
			}
			Transform frame = transitionFrame(from, to, progress / 100.0, float(obs_source_get_width(obs_scene_get_source(targetDraft.scene))));
			frame.visible = from.visible || to.visible;
			frame.order = from.visible && (progress < 60 || !to.visible) ? from.order : to.order;
			apply(preview, frame, true);
		}
	}
	syncVisualCanvas();
}


void PulseMotionEngine::createStarterStage()
{
	QDialog dialog(editor);
	dialog.setWindowTitle("Create an animated stage");
	dialog.setMinimumWidth(480);
	auto *form = new QFormLayout(&dialog);
	auto *help = new QLabel("Choose existing sources for four reusable looks. A new scene will reference them; your existing scenes and source settings stay intact.");
	help->setWordWrap(true); form->addRow(help);
	auto *name = new QLineEdit("Animated Studio"); form->addRow("Stage scene name", name);
	QStringList sources;
	obs_enum_sources([](void *data, obs_source_t *source) {
		if ((obs_source_get_output_flags(source) & OBS_SOURCE_VIDEO) && !obs_scene_from_source(source) && !obs_group_from_source(source))
			static_cast<QStringList *>(data)->append(QString::fromUtf8(obs_source_get_name(source)));
		return true;
	}, &sources);
	sources.sort(Qt::CaseInsensitive);
	std::vector<QComboBox *> roles;
	for (const auto &[label, hint] : std::vector<std::pair<QString, QString>>{
		{"Game / screen", "Game Capture"}, {"Face camera", "second facecam"},
		{"Printer / focus camera", "FullScreen Printer"}, {"BRB graphic / video", "brb scene"}}) {
		auto *choice = new QComboBox;
		choice->addItem("— Not used —", "");
		for (const QString &source : sources) choice->addItem(source, source);
		const int match = choice->findData(hint);
		if (match >= 0) choice->setCurrentIndex(match);
		form->addRow(label, choice); roles.push_back(choice);
	}
	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
	buttons->button(QDialogButtonBox::Ok)->setText("Create four looks");
	form->addRow(buttons);
	connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	if (dialog.exec() != QDialog::Accepted) return;
	const QString sceneName = name->text().trimmed();
	OBSSourceAutoRelease existing = obs_get_source_by_name(sceneName.toUtf8().constData());
	if (sceneName.isEmpty() || existing) { setStatus("Choose an unused scene name; existing scenes were not changed.", true); return; }
	std::vector<OBSSource> chosen;
	for (auto *role : roles) {
		const QString sourceName = role->currentData().toString();
		OBSSourceAutoRelease source = sourceName.isEmpty() ? nullptr : obs_get_source_by_name(sourceName.toUtf8().constData());
		if (!sourceName.isEmpty() && !source) { setStatus("A selected source is no longer available.", true); return; }
		chosen.emplace_back(source);
	}
	if (std::none_of(chosen.begin(), chosen.end(), [](const auto &source) { return bool(source); })) {
		setStatus("Choose at least one source for the stage.", true); return;
	}
	OBSSceneAutoRelease scene = obs_scene_create(sceneName.toUtf8().constData());
	if (!scene) { setStatus("Could not create the stage scene.", true); return; }
	const float width = float(obs_source_get_width(obs_scene_get_source(scene)));
	const float height = float(obs_source_get_height(obs_scene_get_source(scene)));
	std::vector<OBSSceneItem> items;
	for (int i = 0; i < 4; ++i) {
		OBSSceneItem item = chosen[i] ? OBSSceneItem(obs_scene_add(scene, chosen[i])) : OBSSceneItem{};
		items.push_back(item);
		if (!item) continue;
		Transform t = capture(item);
		t.alignment = OBS_ALIGN_LEFT | OBS_ALIGN_TOP; t.boundsType = OBS_BOUNDS_SCALE_INNER;
		t.bounds = {width, height}; t.pos = {0, 0}; t.visible = i == 0 || i == 1;
		if (i == 1) { t.pos = {width * .68f, height * .65f}; t.bounds = {width * .3f, height * .3f}; }
		apply(item, t, true);
	}
	if (items[1]) obs_sceneitem_set_order(items[1], OBS_ORDER_MOVE_TOP);
	char *collection = obs_frontend_get_current_scene_collection();
	const QString collectionName = QString::fromUtf8(collection ? collection : ""); bfree(collection);
	QString firstId;
	for (int look = 0; look < 4; ++look) {
		const QString label = QStringList{"Game", "Chatting", "Printer", "BRB"}[look];
		QJsonArray targets;
		for (int role = 0; role < 4; ++role) {
			if (!items[role]) continue;
			Transform t = capture(items[role]);
			t.pos = {0, 0}; t.bounds = {width, height};
			t.visible = (look == 0 && (role == 0 || role == 1)) || (look == 1 && role == 1) ||
				(look == 2 && (role == 2 || role == 1)) || (look == 3 && role == 3);
			if ((look == 0 || look == 2) && role == 1) { t.pos = {width * .68f, height * .65f}; t.bounds = {width * .3f, height * .3f}; }
			targets.append(QJsonObject{{"container", sceneName}, {"itemId", QString::number(obs_sceneitem_get_id(items[role]))},
				{"source", QString::fromUtf8(obs_source_get_name(chosen[role]))}, {"transform", serialize(t)}});
		}
		const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
		if (firstId.isEmpty()) firstId = id;
		actions.append(QJsonObject{{"version", 1}, {"id", id}, {"name", sceneName + " · " + label}, {"kind", "layout"},
			{"collection", collectionName}, {"container", sceneName}, {"policy", "current"}, {"activateScene", true},
			{"durationMs", 750}, {"restore", false}, {"items", targets}, {"summary", "Animate " + sceneName + " into its " + label + " look."}});
	}
	obs_frontend_save();
	if (!save()) return;
	editingId = firstId;
	refreshEditor();
	loadActionIntoEditor(actionByIdentity(firstId));
	emitEvent("motion_catalogue_changed");
	setStatus("Created Game, Chatting, Printer and BRB looks. Preview here, or run them from Lumia. Existing scenes are unchanged.");
}

QJsonObject PulseMotionEngine::createShowStages()
{
	if (!originalStoreValid) return {{"ok", false}, {"message", "The protected motion store is unreadable."}};
	struct StagePlan { QString name; QString vertical; QStringList sources; };
	const std::vector<StagePlan> plans{
		{"PW Starting", "STarting", {"lumia startiing", "second facecam", "Chatty", "whodatpalalerts", "follower", "Cheer1", "lumia overlay", "caption", "FullScreen Printer"}},
		{"PW Intermission", "VERT BRB", {"brb scene", "Stream Ended", "second facecam", "FullScreen Printer", "Chatty", "whodatpalalerts", "emotes", "lumia overlay", "caption"}},
		{"PW Hangout", "Vertical Scene", {"second facecam", "FullScreen Printer", "main display", "Chatty", "whodatpalalerts", "lumia overlay", "caption"}},
		{"PW Gameplay", "Vertical Scene", {"Game Capture", "main screen", "second facecam", "FullScreen Printer", "Chatty", "whodatpalalerts", "lumia overlay", "caption"}},
		{"PW Celebration", "Vertical Scene", {"Shouty", "joyfull", "spiner", "second facecam", "FullScreen Printer", "Chatty", "whodatpalalerts", "Cheer1", "emotes", "lumia overlay"}},
	};
	const QString collection = motionCollection();
	if (collection != "og scenes (OBS Import)")
		return {{"ok", false}, {"message", "This show setup is only for the imported og scenes collection in this task instance."}};
	for (const StagePlan &plan : plans) {
		OBSSourceAutoRelease existing = obs_get_source_by_name(plan.name.toUtf8().constData());
		if (existing) return {{"ok", false}, {"message", "A show stage already exists: " + plan.name + ". The existing scene was left intact."}};
	}
	for (const QString &required : {QString("second facecam"), QString("FullScreen Printer"), QString("main display"), QString("Game Capture")}) {
		OBSSourceAutoRelease source = obs_get_source_by_name(required.toUtf8().constData());
		if (!source) return {{"ok", false}, {"message", "Required imported source is missing: " + required}};
	}
	OBSCanvasAutoRelease portraitCanvas = obs_get_canvas_by_name("Pulse Weaver Vertical");
	if (!portraitCanvas) return {{"ok", false}, {"message", "The Pulse Weaver Vertical canvas is missing."}};
	for (const StagePlan &plan : plans) {
		OBSSceneAutoRelease original = obs_canvas_get_scene_by_name(portraitCanvas, plan.vertical.toUtf8().constData());
		if (!original) return {{"ok", false}, {"message", "Required portrait scene is missing: " + plan.vertical}};
		const QString copyName = "PW Vert " + plan.name.mid(3);
		OBSSceneAutoRelease copy = obs_canvas_get_scene_by_name(portraitCanvas, copyName.toUtf8().constData());
		if (copy) return {{"ok", false}, {"message", "A portrait show scene already exists: " + copyName}};
	}
	const QString stagePath = QDir::cleanPath(QDir(QFileInfo(storagePath).absolutePath()).absoluteFilePath("../../pulseweaver-stages.json"));
	QFile stageFile(stagePath);
	if (!stageFile.open(QIODevice::ReadOnly)) return {{"ok", false}, {"message", "Cannot read the Stage catalogue. No scenes were created."}};
	const QJsonDocument stageDocument = QJsonDocument::fromJson(stageFile.readAll());
	stageFile.close();
	if (!stageDocument.isArray()) return {{"ok", false}, {"message", "The Stage catalogue is invalid. No scenes were created."}};
	QJsonArray stages = stageDocument.array();
	for (const QJsonValue &value : stages)
		for (const StagePlan &plan : plans)
			if (value.toObject().value("name").toString() == plan.name)
				return {{"ok", false}, {"message", "A Stage named " + plan.name + " already exists."}};

	struct Box { int source; float x; float y; float w; float h; };
	struct Look { int stage; QString name; int duration; std::vector<Box> boxes; };
	const std::vector<Look> looks{
		{0, "Starting", 1100, {{0,0,0,1,1},{3,0,0,1,1},{4,0,0,1,1},{5,0,0,1,1},{6,0,0,1,1},{7,.15f,.89f,.7f,.1f}}},
		{0, "Going live", 850, {{0,0,0,1,1},{3,0,0,1,1},{4,0,0,1,1},{5,0,0,1,1},{6,0,0,1,1},{7,.15f,.89f,.7f,.1f}}},
		{1, "BRB", 900, {{0,0,0,1,1},{5,0,0,1,1},{6,0,0,1,1},{7,0,0,1,1},{8,.15f,.89f,.7f,.1f}}},
		{1, "Ending", 1200, {{1,0,0,1,1},{4,.75f,.16f,.23f,.67f},{5,0,0,1,1},{6,0,0,1,1},{7,0,0,1,1},{8,.15f,.89f,.7f,.1f}}},
		{1, "Printer break", 850, {{3,0,0,1,1},{2,.04f,.63f,.29f,.32f},{4,.76f,.13f,.22f,.7f},{5,0,0,1,1},{7,0,0,1,1},{8,.15f,.89f,.7f,.1f}}},
		{2, "Facecam + printer", 750, {{0,0,0,1,1},{1,.70f,.05f,.28f,.28f},{3,.76f,.38f,.22f,.49f},{4,0,0,1,1},{5,0,0,1,1},{6,.15f,.89f,.7f,.1f}}},
		{2, "Printer + facecam", 750, {{1,0,0,1,1},{0,.69f,.05f,.29f,.29f},{3,.76f,.40f,.22f,.47f},{4,0,0,1,1},{5,0,0,1,1},{6,.15f,.89f,.7f,.1f}}},
		{2, "Screen share", 850, {{2,0,0,1,1},{0,.04f,.68f,.29f,.28f},{1,.76f,.04f,.22f,.23f},{3,.76f,.33f,.22f,.53f},{4,0,0,1,1},{5,0,0,1,1},{6,.15f,.89f,.7f,.1f}}},
		{3, "Gameplay", 700, {{0,0,0,1,1},{2,.03f,.69f,.28f,.27f},{4,.76f,.13f,.22f,.73f},{5,0,0,1,1},{6,0,0,1,1},{7,.15f,.89f,.7f,.1f}}},
		{3, "Screen activity", 750, {{1,0,0,1,1},{2,.03f,.68f,.28f,.28f},{4,.76f,.13f,.22f,.73f},{5,0,0,1,1},{6,0,0,1,1},{7,.15f,.89f,.7f,.1f}}},
		{3, "Printer activity", 850, {{3,0,0,1,1},{2,.03f,.68f,.28f,.28f},{4,.76f,.13f,.22f,.73f},{5,0,0,1,1},{6,0,0,1,1},{7,.15f,.89f,.7f,.1f}}},
		{4, "Raid welcome", 600, {{0,0,0,1,1},{3,.27f,.17f,.46f,.58f},{5,.76f,.14f,.22f,.7f},{6,0,0,1,1},{7,0,0,1,1},{8,0,0,1,1},{9,0,0,1,1}}},
		{4, "Shoutout", 650, {{1,0,0,1,1},{3,.06f,.49f,.4f,.45f},{5,.76f,.14f,.22f,.7f},{6,0,0,1,1},{7,0,0,1,1},{8,0,0,1,1},{9,0,0,1,1}}},
		{4, "Thank you", 800, {{2,0,0,1,1},{3,.28f,.13f,.44f,.66f},{4,.76f,.04f,.22f,.23f},{5,.76f,.31f,.22f,.55f},{6,0,0,1,1},{7,0,0,1,1},{8,0,0,1,1},{9,0,0,1,1}}},
	};
	QJsonArray newActions;
	for (int stageIndex = 0; stageIndex < int(plans.size()); ++stageIndex) {
		const StagePlan &plan = plans[stageIndex];
		OBSSceneAutoRelease scene = obs_scene_create(plan.name.toUtf8().constData());
		if (!scene) return {{"ok", false}, {"message", "Could not create " + plan.name + "."}};
		const QString portraitName = "PW Vert " + plan.name.mid(3);
		OBSSceneAutoRelease portraitOriginal = obs_canvas_get_scene_by_name(portraitCanvas, plan.vertical.toUtf8().constData());
		OBSSceneAutoRelease portrait = obs_scene_duplicate(portraitOriginal, portraitName.toUtf8().constData(), OBS_SCENE_DUP_REFS);
		if (!portrait) return {{"ok", false}, {"message", "Could not create " + portraitName + "."}};
		for (const QString &oldChat : {QString("chat 3"), QString("Chatty")}) {
			if (OBSSceneItem oldItem = obs_scene_find_source(portrait, oldChat.toUtf8().constData()))
				obs_sceneitem_remove(oldItem);
		}
		OBSSourceAutoRelease portraitChat = obs_get_source_by_name("vert chatty");
		if (!portraitChat) portraitChat = obs_get_source_by_name("Chatty");
		if (portraitChat) {
			OBSSceneItem chatItem = obs_scene_find_source(portrait, obs_source_get_name(portraitChat));
			if (!chatItem) chatItem = motionAddSource(portrait, portraitChat);
			if (chatItem) {
				obs_sceneitem_set_alignment(chatItem, OBS_ALIGN_LEFT | OBS_ALIGN_TOP);
				obs_sceneitem_set_bounds_type(chatItem, OBS_BOUNDS_NONE);
				const bool portraitSource = QString::fromUtf8(obs_source_get_name(portraitChat)) == "vert chatty";
				const vec2 position{0.0f, portraitSource ? 0.0f : 625.0f};
				const vec2 scale{portraitSource ? 1.0f : 0.5625f, portraitSource ? 1.0f : 0.5625f};
				obs_sceneitem_set_pos(chatItem, &position);
				obs_sceneitem_set_scale(chatItem, &scale);
				obs_sceneitem_set_visible(chatItem, true);
			}
		}
		OBSDataAutoRelease portraitSettings = obs_source_get_private_settings(obs_scene_get_source(portrait));
		obs_data_set_string(portraitSettings, "pulseweaver.horizontal_uuid", obs_source_get_uuid(obs_scene_get_source(scene)));
		obs_data_set_bool(portraitSettings, "pulseweaver.follow_horizontal", false);
		obs_data_set_bool(portraitSettings, "pulseweaver.native_vertical", true);
		const float width = float(obs_source_get_width(obs_scene_get_source(scene)));
		const float height = float(obs_source_get_height(obs_scene_get_source(scene)));
		std::vector<OBSSceneItem> items;
		for (const QString &sourceName : plan.sources) {
			OBSSourceAutoRelease source = obs_get_source_by_name(sourceName.toUtf8().constData());
			OBSSceneItem item = source ? OBSSceneItem(obs_scene_add(scene, source)) : OBSSceneItem{};
			items.push_back(item);
			if (item) {
				if (sourceName == "Chatty") {
					Transform chat = capture(item);
					chat.pos = {0, 0}; chat.scale = {1, 1}; chat.bounds = {0, 0};
					chat.boundsType = OBS_BOUNDS_NONE; chat.visible = true;
					apply(item, chat, true);
				} else obs_sceneitem_set_visible(item, false);
			}
		}
		for (const Look &look : looks) {
			if (look.stage != stageIndex) continue;
			QJsonArray targets;
			for (int role = 0; role < int(items.size()); ++role) {
				if (!items[role] || plan.sources[role] == "Chatty") continue;
				Transform transform = capture(items[role]);
				transform.alignment = OBS_ALIGN_LEFT | OBS_ALIGN_TOP;
				transform.boundsType = OBS_BOUNDS_SCALE_INNER;
				transform.pos = {0, 0}; transform.bounds = {width, height};
				transform.visible = false;
				for (const Box &box : look.boxes) if (box.source == role) {
					transform.pos = {width * box.x, height * box.y};
					transform.bounds = {width * box.w, height * box.h};
					transform.visible = true;
					break;
				}
				targets.append(QJsonObject{{"container", plan.name}, {"itemId", QString::number(obs_sceneitem_get_id(items[role]))},
					{"source", plan.sources[role]}, {"transform", serialize(transform)}});
			}
			newActions.append(QJsonObject{{"version", 1}, {"id", QUuid::createUuid().toString(QUuid::WithoutBraces)},
				{"name", plan.name + " · " + look.name}, {"kind", "layout"}, {"collection", collection},
				{"container", plan.name}, {"policy", "switch"}, {"stage", plan.name}, {"activateScene", true},
				{"durationMs", look.duration}, {"restore", false}, {"items", targets},
				{"summary", "Animate " + plan.name + " into " + look.name + "."}});
		}
		if (newActions.isEmpty()) continue;
		for (const QJsonValue &value : newActions) {
			const QJsonObject candidate = value.toObject();
			if (candidate.value("container") == plan.name) { for (const QJsonValue &entry : candidate.value("items").toArray()) {
				const QJsonObject target = entry.toObject();
				OBSSceneItem item = obs_scene_find_sceneitem_by_id(scene, target.value("itemId").toString().toLongLong());
				if (item) apply(item, deserialize(target.value("transform").toObject()), true);
			} break; }
		}
		QJsonObject assignments;
		for (const QString &provider : {QString("twitch"), QString("youtube"), QString("kick"), QString("recording")}) {
			const QJsonArray excluded = (provider == "youtube" || provider == "kick") ?
				QJsonArray{QString("spotifysound"), QString("spotify")} : QJsonArray{};
			assignments.insert(provider + "_horizontal", QJsonObject{{"canvas", "horizontal"}, {"scene", plan.name}, {"excluded", excluded}});
			assignments.insert(provider + "_vertical", QJsonObject{{"canvas", "vertical"},
				{"scene", portraitName}, {"excluded", excluded}});
		}
		stages.append(QJsonObject{{"name", plan.name}, {"horizontal", plan.name}, {"vertical", portraitName},
			{"horizontalTransition", "fade"}, {"horizontalDurationMs", 500},
			{"verticalTransition", "fade"}, {"verticalDurationMs", 500}, {"assignments", assignments}});
	}
	QSaveFile out(stagePath);
	if (!out.open(QIODevice::WriteOnly)) return {{"ok", false}, {"message", "Could not save the Stage catalogue."}};
	out.write(QJsonDocument(stages).toJson(QJsonDocument::Indented));
	if (!out.commit()) return {{"ok", false}, {"message", "Could not finish saving the Stage catalogue."}};
	for (const QJsonValue &value : newActions) actions.append(value);
	if (!save()) return {{"ok", false}, {"message", "The Stage scenes were created, but motion actions could not be saved."}};
	obs_frontend_save();
	refreshEditor();
	emitEvent("motion_catalogue_changed");
	return {{"ok", true}, {"message", "Created five show stages and 14 animated looks. YouTube and Kick exclude Spotify; Twitch routing is unchanged. Restart this preview to refresh Stage choices."},
		{"stages", int(plans.size())}, {"looks", newActions.size()}};
}

void PulseMotionEngine::createLook(const QString &name, bool duplicate)
{
	QJsonObject action = editorAction();
	if (!duplicate) action.remove("items");
	action.remove("id"); action.insert("kind", "layout"); action.insert("name", name);
	action.insert("restore", false);
	action.insert("durationMs", durationField ? durationField->value() : 750);
	if (!mainContainer.isEmpty()) action.insert("container", mainContainer);
	loadActionIntoEditor(action);
	if (!duplicate && itemTree) {
		const QSignalBlocker blocker(itemTree);
		for (int row = 0; row < itemTree->topLevelItemCount(); ++row) itemTree->topLevelItem(row)->setCheckState(0, Qt::Checked);
	}
	refreshSummary();
	setStatus("Arrange this look in the canvas, then Save. Your live output is unchanged.");
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

PulseMotionEngine::Transform PulseMotionEngine::transitionFrame(const Transform &from, const Transform &to, double progress, float canvasWidth)
{
	Transform entrance = from, exit = to;
	if (from.visible != to.visible && canvasWidth > 0) {
		if (to.visible) {
			entrance = to;
			entrance.pos.x = to.pos.x - canvasWidth;
		} else {
			exit = from;
			exit.pos.x = from.pos.x + canvasWidth;
		}
	}
	return interpolate(entrance, exit, progress);
}

void PulseMotionEngine::apply(obs_sceneitem_t *item, const Transform &value, bool finalFrame)
{
	if (!item) return;
	obs_sceneitem_defer_update_begin(item);
	// Outgoing layers must be hidden before their parked final transform is set.
	if (finalFrame && !value.visible) obs_sceneitem_set_visible(item, false);
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
		if (value.visible) obs_sceneitem_set_visible(item, true);
		obs_sceneitem_set_locked(item, value.locked);
	}
	obs_sceneitem_defer_update_end(item);
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
		owner = motionSource(resolvedContainer);
	}
	obs_scene_t *scene = owner ? obs_scene_from_source(owner) : nullptr;
	if (!scene && owner) scene = obs_group_from_source(owner);
	if (!scene) return {};
	OBSSceneItem item = itemId > 0 ? PulseRuntimeSafety::findSceneItemById(scene, itemId) : OBSSceneItem{};
	if (item && !sourceName.isEmpty() && QString::fromUtf8(obs_source_get_name(obs_sceneitem_get_source(item))) != sourceName) item = nullptr;
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
	char *collection = obs_frontend_get_current_scene_collection();
	const QString currentCollection = QString::fromUtf8(collection ? collection : "");
	bfree(collection);
	if (action.contains("collection") && action.value("collection").toString() != currentCollection) {
		error = "This look belongs to another scene collection. Switch to its collection first.";
		return false;
	}

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
		std::stable_sort(execution.tracks.begin(), execution.tracks.end(), [](const auto &a, const auto &b) { return a.target.order < b.target.order; });
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


namespace {
QString motionCollection()
{
	char *name = obs_frontend_get_current_scene_collection();
	const QString result = QString::fromUtf8(name ? name : "");
	bfree(name);
	return result;
}
}

bool PulseMotionEngine::preserveOriginals(const Execution &execution, QString &error)
{
	if (!originalStoreValid) { error = "Original scene storage is unreadable. Output was not changed."; return false; }
	const QString collection = motionCollection();
	const QJsonArray before = originals;
	const QJsonObject beforeScene = originalScenes;
	if (!originalScenes.contains(collection)) {
		OBSSourceAutoRelease current = obs_frontend_get_current_scene();
		OBSCanvasAutoRelease canvas = obs_get_canvas_by_name("Pulse Weaver Vertical");
		OBSSourceAutoRelease portrait = canvas ? obs_canvas_get_channel(canvas, 0) : nullptr;
		if (portrait && obs_source_get_type(portrait) == OBS_SOURCE_TYPE_TRANSITION)
			portrait = obs_transition_get_active_source(portrait);
		if (current) originalScenes.insert(collection, QJsonObject{
			{"scene", QString::fromUtf8(obs_source_get_uuid(current))}, {"stage", activeStageName()},
			{"portrait", portrait ? QString::fromUtf8(obs_source_get_uuid(portrait)) : QString()}});
	}
	for (const Track &track : execution.tracks) {
		obs_scene_t *scene = obs_sceneitem_get_scene(track.item);
		obs_source_t *owner = scene ? obs_scene_get_source(scene) : nullptr;
		if (!owner) { error = "Cannot protect the original scene. Output was not changed."; return false; }
		const QString uuid = QString::fromUtf8(obs_source_get_uuid(owner));
		bool known = false;
		for (const auto &value : originals) {
			const auto saved = value.toObject();
			if (saved.value("collection").toString() == collection && saved.value("uuid").toString() == uuid) { known = true; break; }
		}
		if (known) continue;
		QJsonArray items;
		for (auto *item : motionItems(scene))
			items.append(QJsonObject{{"itemId", QString::number(obs_sceneitem_get_id(item))},
				{"sourceUuid", QString::fromUtf8(obs_source_get_uuid(obs_sceneitem_get_source(item)))},
				{"source", QString::fromUtf8(obs_source_get_name(obs_sceneitem_get_source(item)))},
				{"transform", serialize(capture(item))}});
		originals.append(QJsonObject{{"collection", collection}, {"uuid", uuid},
			{"container", QString::fromUtf8(obs_source_get_name(owner))}, {"items", items},
			{"capturedAt", QDateTime::currentDateTimeUtc().toString(Qt::ISODate)}});
	}
	if (!save()) { originals = before; originalScenes = beforeScene; error = "Could not persist the original scene. Output was not changed."; return false; }
	return true;
}

QJsonObject PulseMotionEngine::restoreOriginals()
{
	struct Restore { OBSSceneItem item; Transform transform; };
	std::vector<Restore> restore;
	std::vector<OBSSceneItem> additions;
	for (const auto &value : originals) {
		const auto saved = value.toObject();
		if (saved.value("collection").toString() != motionCollection()) continue;
		OBSSourceAutoRelease source = obs_get_source_by_uuid(saved.value("uuid").toString().toUtf8().constData());
		obs_scene_t *scene = source ? obs_scene_from_source(source) : nullptr;
		if (!scene && source) scene = obs_group_from_source(source);
		if (!scene) return {{"ok", false}, {"message", "An original scene is missing. Recover it from the scene collection backup before restoring."}};
		QSet<qint64> originalIds;
		for (const auto &entry : saved.value("items").toArray()) {
			const auto row = entry.toObject();
			originalIds.insert(row.value("itemId").toString().toLongLong());
			auto *item = obs_scene_find_sceneitem_by_id(scene, row.value("itemId").toString().toLongLong());
			if (!item || QString::fromUtf8(obs_source_get_uuid(obs_sceneitem_get_source(item))) != row.value("sourceUuid").toString())
				return {{"ok", false}, {"message", "An original source is missing or replaced. Nothing was restored; recover the scene collection backup first."}};
			restore.push_back({OBSSceneItem(item), deserialize(row.value("transform").toObject())});
		}
		for (auto *item : motionItems(scene))
			if (!originalIds.contains(obs_sceneitem_get_id(item))) additions.emplace_back(item);
	}
	if (restore.empty()) return {{"ok", false}, {"message", "No original snapshots for this collection yet. Originals are protected before the first live motion."}};
	OBSSourceAutoRelease originalScene;
	const QJsonValue savedProgram = originalScenes.value(motionCollection());
	const QJsonObject program = savedProgram.toObject();
	const QString sceneUuid = savedProgram.isString() ? savedProgram.toString() : program.value("scene").toString();
	if (!sceneUuid.isEmpty()) {
		originalScene = obs_get_source_by_uuid(sceneUuid.toUtf8().constData());
		if (!originalScene) return {{"ok", false}, {"message", "The original on-air scene is missing. Nothing was restored; recover the collection backup first."}};
	}
	OBSSourceAutoRelease originalPortrait;
	OBSCanvasAutoRelease portraitCanvas = obs_get_canvas_by_name("Pulse Weaver Vertical");
	const QString portraitUuid = program.value("portrait").toString();
	if (!portraitUuid.isEmpty()) {
		originalPortrait = obs_get_source_by_uuid(portraitUuid.toUtf8().constData());
		if (!originalPortrait || !portraitCanvas) return {{"ok", false}, {"message", "The original portrait scene is missing. Nothing was restored."}};
	}
	const QString originalStage = program.value("stage").toString();
	const int originalStageIndex = originalStage.isEmpty() ? -1 : findStage(originalStage);
	if (!originalStage.isEmpty() && originalStageIndex < 0)
		return {{"ok", false}, {"message", "The original Stage is missing. Nothing was restored."}};
	animationTimer.stop(); stageTimer.stop();
	if (active) complete(false, "Stopped to restore original scenes.");
	// Retain added source references so saved looks can be played again after restore.
	for (auto &item : additions) obs_sceneitem_set_visible(item, false);
	std::stable_sort(restore.begin(), restore.end(), [](const auto &a, const auto &b) { return a.transform.order < b.transform.order; });
	for (auto &entry : restore) apply(entry.item, entry.transform, true);
	QJsonObject stage;
	if (originalStageIndex >= 0) {
		stage = QJsonDocument::fromJson(stageSelector()->itemData(originalStageIndex).toString().toUtf8()).object();
		switchStage(originalStageIndex);
	}
	if (originalScene && stage.value("horizontal").toString() != QString::fromUtf8(obs_source_get_name(originalScene)))
		obs_frontend_set_current_scene(originalScene);
	if (originalPortrait && stage.value("vertical").toString() != QString::fromUtf8(obs_source_get_name(originalPortrait)))
		obs_canvas_set_channel(portraitCanvas, 0, originalPortrait);
	else if (program.contains("portrait") && portraitUuid.isEmpty() && portraitCanvas && stage.value("vertical").toString().isEmpty())
		obs_canvas_set_channel(portraitCanvas, 0, nullptr);
	lastRestore.clear();
	obs_frontend_save();
	emitEvent("motion_state", {{"state", "finished"}, {"name", "Original scenes"}, {"message", "Original scenes restored."}});
	return {{"ok", true}, {"message", "Original scenes restored. Protected snapshots remain available for future restores."}};
}

void PulseMotionEngine::prepareCoveragePairs(Execution &execution)
{
	execution.coveragePairs.clear();
	std::vector<bool> paired(execution.tracks.size(), false);
	for (std::size_t i = 0; i < execution.tracks.size(); ++i) {
		if (paired[i]) continue;
		const Track &a = execution.tracks[i];
		if (!a.from.visible || !a.target.visible) continue;
		obs_scene_t *scene = obs_sceneitem_get_scene(a.item);
		obs_source_t *owner = scene ? obs_scene_get_source(scene) : nullptr;
		const float width = owner ? float(obs_source_get_width(owner)) : 0.0f;
		const float height = owner ? float(obs_source_get_height(owner)) : 0.0f;
		if (width <= 0.0f || height <= 0.0f) continue;
		const auto full = [width, height](const Transform &t) {
			return t.boundsType != OBS_BOUNDS_NONE && t.pos.x < width * 0.08f &&
				t.pos.y < height * 0.08f && t.bounds.x >= width * 0.9f &&
				t.bounds.y >= height * 0.9f;
		};
		const auto inset = [width, height](const Transform &t) {
			return t.boundsType != OBS_BOUNDS_NONE && t.pos.x >= width * 0.45f &&
				t.pos.y < height * 0.2f && t.bounds.x <= width * 0.45f &&
				t.bounds.y <= height * 0.45f;
		};
		const auto top = [width, height](const Transform &t) {
			return t.boundsType != OBS_BOUNDS_NONE && t.pos.y < height * 0.08f &&
				t.bounds.x >= width * 0.9f && t.bounds.y >= height * 0.2f &&
				t.bounds.y < height * 0.5f;
		};
		const auto bottom = [width, height](const Transform &t) {
			return t.boundsType != OBS_BOUNDS_NONE && t.pos.y >= height * 0.2f &&
				t.bounds.x >= width * 0.9f && t.bounds.y > height * 0.5f &&
				t.pos.y + t.bounds.y >= height * 0.9f;
		};
		for (std::size_t j = i + 1; j < execution.tracks.size(); ++j) {
			if (paired[j]) continue;
			const Track &b = execution.tracks[j];
			if (a.container != b.container || !b.from.visible || !b.target.visible) continue;
			if (width > height * 1.2f && full(a.from) && inset(a.target) &&
			    inset(b.from) && full(b.target)) {
				execution.coveragePairs.push_back({Execution::CoveragePair::Kind::FullInset, j, i, width, height});
			} else if (width > height * 1.2f && inset(a.from) && full(a.target) &&
			           full(b.from) && inset(b.target)) {
				execution.coveragePairs.push_back({Execution::CoveragePair::Kind::FullInset, i, j, width, height});
			} else if (height > width * 1.2f && top(a.from) && bottom(a.target) &&
			           bottom(b.from) && top(b.target)) {
				execution.coveragePairs.push_back({Execution::CoveragePair::Kind::PortraitSplit, i, j, width, height});
			} else if (height > width * 1.2f && bottom(a.from) && top(a.target) &&
			           top(b.from) && bottom(b.target)) {
				execution.coveragePairs.push_back({Execution::CoveragePair::Kind::PortraitSplit, j, i, width, height});
			} else {
				continue;
			}
			paired[i] = paired[j] = true;
			break;
		}
	}
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
		if (action.value("kind") != "layout" || active->action.value("kind") != "layout")
			return {{"ok", false}, {"message", "Another motion action is running. Stop or restore it before starting this one."}, {"executionId", active->id}};
	}
	auto execution = std::make_unique<Execution>();
	execution->id = QUuid::createUuid().toString(QUuid::WithoutBraces);
	execution->requestId = requestId;
	execution->action = action;
	execution->generation = ++generation;
	QString error;
	if (!prepareExecution(*execution, error) || !preserveOriginals(*execution, error))
		return {{"ok", false}, {"message", error}};
	if (active) {
		complete(false, "Transition replaced by the next stage look.");
		for (Track &track : execution->tracks)
			track.baseline = track.from = capture(track.item);
	}
	active = std::move(execution);
	const QString policy = action.value("policy").toString("switch");
	const int targetStage = policy == "switch" ? findStage(action.value("stage").toString()) : -1;
	if (targetStage >= 0 && stageSelector() && targetStage != stageSelector()->currentIndex()) {
		active->phase = "changing_stage";
		active->clock.start();
		// The destination scenes are still off air. Load the requested look into
		// them now so the stinger reveals the finished composition at its cut.
		for (Track &track : active->tracks) apply(track.item, track.target, true);
		active->preloadedStage = true;
		switchStage(targetStage);
		stageTimer.start(stageReadinessDelay(action));
		setStatus("Changing to Stage “" + action.value("stage").toString() + "”…");
		emitEvent("motion_state", {{"state", "changing_stage"}, {"executionId", active->id},
			{"action", action.value("id")}, {"name", action.value("name")}});
	} else {
		if (action.value("activateScene").toBool() && action.value("stage").toString().isEmpty()) {
			OBSSourceAutoRelease source = obs_get_source_by_name(action.value("container").toString().toUtf8().constData());
			OBSSourceAutoRelease current = obs_frontend_get_current_scene();
			if (source && source.Get() != current.Get() && obs_scene_from_source(source)) obs_frontend_set_current_scene(source);
		}
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
	if (active->phase == "changing_stage") {
		QWidget *window = static_cast<QWidget *>(obs_frontend_get_main_window());
		OBSSourceAutoRelease transition = obs_frontend_get_current_transition();
		OBSCanvasAutoRelease canvas = obs_get_canvas_by_name("Pulse Weaver Vertical");
		OBSSourceAutoRelease portrait = canvas ? obs_canvas_get_channel(canvas, 0) : nullptr;
		const bool transitioning = (window && window->property("pulseWeaverStageTransitionPending").toBool()) ||
			(transition && obs_transition_is_active(transition)) ||
			(portrait && obs_transition_is_active(portrait));
		if (transitioning) {
			if (active->clock.elapsed() > 30000) { complete(false, "Stage transition did not finish. Check the stinger media."); return; }
			stageTimer.start(50); return;
		}
	}
	if (active->preloadedStage) {
		finishMove();
		return;
	}
	active->phase = "moving";
	active->clock.restart();
	active->orderCommitted = false;
	active->graphicCommitted = false;
	for (Track &track : active->tracks) {
		track.baseline = track.from = capture(track.item);
		if (active->action.value("kind") == "punch")
			track.target = punchTarget(track.item, track.baseline,
				active->action.value("zoomPercent").toDouble(150.0) / 100.0,
				active->action.value("focusX").toDouble(0.5), active->action.value("focusY").toDouble(0.42));
		obs_scene_t *scene = obs_sceneitem_get_scene(track.item);
		const float width = scene ? float(obs_source_get_width(obs_scene_get_source(scene))) : 0.0f;
		if (active->action.value("kind") == "layout" && motionGraphicSwitch(track.sourceName) &&
		    track.from.visible != track.target.visible)
			continue;
		// Place entering layers outside the canvas before revealing them.
		apply(track.item, transitionFrame(track.from, track.target, 0.0, width));
		if (active->action.value("kind") == "layout" && !track.from.visible && track.target.visible)
			obs_sceneitem_set_order_position(track.item, track.target.order);
		obs_sceneitem_set_visible(track.item, track.from.visible || track.target.visible);
	}
	if (active->action.value("kind") == "layout") prepareCoveragePairs(*active);
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
	std::vector<bool> covered(active->tracks.size(), false);
	if (!active->restoring)
		for (const Execution::CoveragePair &pair : active->coveragePairs)
			covered[pair.incoming] = covered[pair.outgoing] = true;
	if (!active->restoring && !active->graphicCommitted && progress >= 0.5) {
		// Hide old artwork before revealing the new full-canvas graphics.
		for (Track &track : active->tracks)
			if (motionGraphicSwitch(track.sourceName) && track.from.visible && !track.target.visible)
				apply(track.item, track.target, true);
		for (Track &track : active->tracks)
			if (motionGraphicSwitch(track.sourceName) && !track.from.visible && track.target.visible)
				apply(track.item, track.target, true);
		active->graphicCommitted = true;
	}
	if (!active->restoring && !active->orderCommitted && progress >= 0.6) {
		// Swap the stack after continuous sources have travelled most of the way.
		// Incoming sources were prepared off canvas before their first visible frame.
		for (Track &track : active->tracks)
			if (!covered[&track - active->tracks.data()] && track.from.visible && track.target.visible)
				obs_sceneitem_set_order_position(track.item, track.target.order);
		active->orderCommitted = true;
	}
	if (!active->restoring) {
		for (Execution::CoveragePair &pair : active->coveragePairs) {
			Track &incoming = active->tracks[pair.incoming];
			Track &outgoing = active->tracks[pair.outgoing];
			Transform enterFrame, leaveFrame;
			if (pair.kind == Execution::CoveragePair::Kind::FullInset) {
				if (progress < 0.5) {
					enterFrame = interpolate(incoming.from, incoming.target, progress * 2.0);
					enterFrame.boundsType = OBS_BOUNDS_SCALE_OUTER;
					enterFrame.boundsCrop = true;
					leaveFrame = outgoing.from;
				} else {
					enterFrame = incoming.target;
					Transform insetStart = outgoing.target;
					insetStart.pos.x += insetStart.bounds.x * 0.5f;
					insetStart.pos.y += insetStart.bounds.y * 0.5f;
					insetStart.bounds = {1.0f, 1.0f};
					leaveFrame = interpolate(insetStart, outgoing.target, (progress - 0.5) * 2.0);
					if (!pair.orderCommitted) {
						obs_sceneitem_set_order_position(incoming.item, incoming.target.order);
						obs_sceneitem_set_order_position(outgoing.item, outgoing.target.order);
						pair.orderCommitted = true;
					}
				}
			} else {
				auto fullFrame = [&pair](const Transform &value) {
					Transform full = value;
					full.pos = {0.0f, 0.0f};
					full.bounds = {pair.width, pair.height};
					full.boundsType = OBS_BOUNDS_SCALE_OUTER;
					full.boundsCrop = true;
					full.boundsAlignment = 0;
					full.alignment = 5;
					return full;
				};
				const Transform enterFull = fullFrame(incoming.from);
				const Transform leaveFull = fullFrame(outgoing.from);
				if (progress < 0.4) {
					enterFrame = interpolate(incoming.from, enterFull, progress / 0.4);
					leaveFrame = outgoing.from;
				} else if (progress < 0.7) {
					enterFrame = enterFull;
					leaveFrame = interpolate(outgoing.from, leaveFull, (progress - 0.4) / 0.3);
				} else {
					enterFrame = interpolate(enterFull, incoming.target, (progress - 0.7) / 0.3);
					leaveFrame = interpolate(leaveFull, outgoing.target, (progress - 0.7) / 0.3);
				}
				if (progress > 0.0 && progress < 1.0) {
					enterFrame.boundsType = leaveFrame.boundsType = OBS_BOUNDS_SCALE_OUTER;
					enterFrame.boundsCrop = leaveFrame.boundsCrop = true;
				}
			}
			apply(incoming.item, enterFrame, progress >= 1.0);
			apply(outgoing.item, leaveFrame, progress >= 1.0);
		}
	}
	for (Track &track : active->tracks) {
		if (covered[&track - active->tracks.data()]) continue;
		if (!active->restoring && motionGraphicSwitch(track.sourceName) &&
		    track.from.visible != track.target.visible)
			continue;
		obs_scene_t *scene = obs_sceneitem_get_scene(track.item);
		const float width = scene ? float(obs_source_get_width(obs_scene_get_source(scene))) : 0.0f;
		const Transform frame = active->restoring ? interpolate(track.from, track.target, progress) : transitionFrame(track.from, track.target, progress, width);
		apply(track.item, frame, progress >= 1.0);
	}
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
	std::stable_sort(active->tracks.begin(), active->tracks.end(), [](const auto &a, const auto &b) { return a.target.order < b.target.order; });
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
	if (!ok && active->preloadedStage && active->phase == "changing_stage")
		for (Track &track : active->tracks) apply(track.item, track.baseline, true);
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
			if (active->preloadedStage)
				for (Track &track : active->tracks) apply(track.item, track.baseline, true);
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
	if (active) return { {"ok", false}, {"message", "Stop the current motion before restoring the last layout."} };
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
		if (active->preloadedStage)
			for (Track &track : active->tracks) apply(track.item, track.baseline, true);
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
	if (event == OBS_FRONTEND_EVENT_FINISHED_LOADING || event == OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGED) {
		QTimer::singleShot(0, this, [this] { refreshEditor(); });
		return;
	}
	if (event == OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGED || event == OBS_FRONTEND_EVENT_SCENE_LIST_CHANGED) {
		populateScenes();
		populateSources();
		populateItems();
		refreshSummary();
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
