#include "PulseVerticalEditor.hpp"

#include "OBSBasic.hpp"
#include "OBSQTDisplay.hpp"

#include <graphics/matrix4.h>
#include <graphics/vec3.h>
#include <graphics/vec4.h>

#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSplitter>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <vector>

namespace {

constexpr uint32_t VERTICAL_WIDTH = 1080;
constexpr uint32_t VERTICAL_HEIGHT = 1920;

QSize verticalCanvasSize()
{
	QSize result{int(VERTICAL_WIDTH), int(VERTICAL_HEIGHT)};
	obs_canvas_t *canvas = PulseWeaverGetVerticalCanvas();
	obs_video_info info = {};
	if (canvas && obs_canvas_get_video_info(canvas, &info) && info.base_width && info.base_height)
		result = QSize(int(info.base_width), int(info.base_height));
	obs_canvas_release(canvas);
	return result;
}

obs_source_t *verticalSceneSource()
{
	obs_canvas_t *canvas = PulseWeaverGetVerticalCanvas();
	if (!canvas)
		return nullptr;
	obs_source_t *source = obs_canvas_get_channel(canvas, 0);
	obs_canvas_release(canvas);
	return source;
}

obs_sceneitem_t *verticalItemRef(int64_t id)
{
	if (id < 0)
		return nullptr;
	obs_source_t *source = verticalSceneSource();
	obs_scene_t *scene = source ? obs_scene_from_source(source) : nullptr;
	obs_sceneitem_t *item = scene ? obs_scene_find_sceneitem_by_id(scene, id) : nullptr;
	if (item)
		obs_sceneitem_addref(item);
	obs_source_release(source);
	return item;
}

obs_source_t *verticalItemSourceRef(int64_t id)
{
	obs_sceneitem_t *item = verticalItemRef(id);
	obs_source_t *source = item ? obs_source_get_ref(obs_sceneitem_get_source(item)) : nullptr;
	obs_sceneitem_release(item);
	return source;
}

QPolygonF itemPolygon(obs_sceneitem_t *item, const std::function<QPointF(const QPointF &)> &map)
{
	QPolygonF result;
	if (!item)
		return result;
	matrix4 transform;
	obs_sceneitem_get_box_transform(item, &transform);
	for (const QPointF &corner : {QPointF(0, 0), QPointF(1, 0), QPointF(1, 1), QPointF(0, 1)}) {
		vec3 value;
		vec3_set(&value, float(corner.x()), float(corner.y()), 0.0f);
		vec3_transform(&value, &value, &transform);
		result << map(QPointF(value.x, value.y));
	}
	return result;
}

bool itemContains(obs_sceneitem_t *item, const QPointF &canvasPoint)
{
	if (!item || !obs_sceneitem_visible(item))
		return false;
	obs_source_t *source = obs_sceneitem_get_source(item);
	if (!(obs_source_get_output_flags(source) & OBS_SOURCE_VIDEO))
		return false;
	matrix4 transform;
	obs_sceneitem_get_box_transform(item, &transform);
	if (!matrix4_inv(&transform, &transform))
		return false;
	vec3 point;
	vec3_set(&point, float(canvasPoint.x()), float(canvasPoint.y()), 0.0f);
	vec3_transform(&point, &point, &transform);
	return point.x >= 0.0f && point.x <= 1.0f && point.y >= 0.0f && point.y <= 1.0f;
}

void drawLineLoop(const QPolygonF &points, uint32_t color)
{
	if (points.size() < 2)
		return;
	gs_render_start(true);
	for (qsizetype index = 0; index < points.size(); ++index) {
		const QPointF &from = points.at(index);
		const QPointF &to = points.at((index + 1) % points.size());
		gs_vertex2f(float(from.x()), float(from.y()));
		gs_vertex2f(float(to.x()), float(to.y()));
	}
	gs_vertbuffer_t *lines = gs_render_save();
	gs_effect_t *effect = obs_get_base_effect(OBS_EFFECT_SOLID);
	gs_eparam_t *colorParam = gs_effect_get_param_by_name(effect, "color");
	gs_effect_set_color(colorParam, color);
	gs_load_vertexbuffer(lines);
	while (gs_effect_loop(effect, "Solid"))
		gs_draw(GS_LINES, 0, 0);
	gs_load_vertexbuffer(nullptr);
	gs_vertexbuffer_destroy(lines);
}

void drawSolidRect(float left, float top, float right, float bottom, uint32_t color)
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
	while (gs_effect_loop(effect, "Solid"))
		gs_draw(GS_TRISTRIP, 0, 0);
	gs_load_vertexbuffer(nullptr);
	gs_vertexbuffer_destroy(quad);
}

void renderCanvasFit(obs_canvas_t *canvas, obs_sceneitem_t *selectedItem, uint32_t canvasWidth,
		     uint32_t canvasHeight, uint32_t displayWidth, uint32_t displayHeight)
{
	vec4 clearColor;
	/* A contrasting theatre-black matte makes the actual portrait program
	 * surface obvious even when the scene is transparent or a disconnected
	 * camera renders an almost-black placeholder. */
	vec4_set(&clearColor, 0.012f, 0.018f, 0.035f, 1.0f);
	gs_clear(GS_CLEAR_COLOR, &clearColor, 0.0f, 0);
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
	drawSolidRect(0.0f, 0.0f, float(canvasWidth), float(canvasHeight), 0xFF000000);
	/* Render the canvas view itself. Imported browser/capture sources are
	 * activated through the canvas view; rendering only its scene source can
	 * produce a valid layer list over a completely black editor surface. */
	if (canvas)
		obs_canvas_render(canvas);
	const qreal edge = std::max(1.0f, 2.0f / scale);
	for (int pass = 0; pass < 3; ++pass) {
		const qreal inset = edge * pass;
		drawLineLoop({QPointF(inset, inset), QPointF(canvasWidth - inset, inset),
			      QPointF(canvasWidth - inset, canvasHeight - inset),
			      QPointF(inset, canvasHeight - inset)},
			     pass == 1 ? 0xFFEED322 : 0xFFEF46D9);
	}
	if (selectedItem) {
		const QPolygonF polygon = itemPolygon(selectedItem, [](const QPointF &point) { return point; });
		drawLineLoop(polygon, 0xFF5C3BFF);
		if (polygon.size() == 4) {
			const qreal handle = std::max(5.0f, 7.0f / scale);
			const QPointF corner = polygon.at(2);
			drawLineLoop({corner + QPointF(-handle, -handle), corner + QPointF(handle, -handle),
				      corner + QPointF(handle, handle), corner + QPointF(-handle, handle)},
				     0xFFFFFFFF);
		}
	}
	gs_reset_viewport();
	gs_projection_pop();
	gs_viewport_pop();
}

class VerticalOverlay final : public OBSQTDisplay {
public:
	std::function<int64_t()> selected;
	std::function<void(int64_t)> select;
	std::function<void()> changed;
	std::function<void()> committed;

	explicit VerticalOverlay(QWidget *parent = nullptr) : OBSQTDisplay(parent)
	{
		setMouseTracking(true);
		setFocusPolicy(Qt::StrongFocus);
		SetDisplayBackgroundColor(QColor("#020307"));
	}

private:
	bool dragging = false;
	bool resizing = false;
	QPointF startCanvas;
	vec2 startPosition = {};
	vec2 startScale = {};
	QPointF lastClick;
	int cycleIndex = 0;

	QRectF canvasRect() const
	{
		const QSize canvas = verticalCanvasSize();
		/* The OBS display and this interaction surface are siblings in a
		 * StackAll layout and therefore have identical logical geometry.  Do
		 * not map the native OBS child HWND back through QWidget coordinates:
		 * on Windows with display scaling that can return device/global
		 * coordinates and separates hit testing from the rendered canvas. */
		const QRectF available(contentsRect());
		if (canvas.isEmpty() || available.isEmpty())
			return {};
		const qreal scale = std::min(available.width() / qreal(canvas.width()),
					     available.height() / qreal(canvas.height()));
		const QSizeF size(canvas.width() * scale, canvas.height() * scale);
		return QRectF(QPointF(available.left() + (available.width() - size.width()) / 2.0,
				      available.top() + (available.height() - size.height()) / 2.0),
			      size);
	}

	QPointF toCanvas(const QPointF &point) const
	{
		const QRectF rect = canvasRect();
		const QSize canvas = verticalCanvasSize();
		if (rect.isEmpty())
			return {};
		return QPointF((point.x() - rect.left()) * canvas.width() / rect.width(),
			       (point.y() - rect.top()) * canvas.height() / rect.height());
	}

	QPointF toWidget(const QPointF &point) const
	{
		const QRectF rect = canvasRect();
		const QSize canvas = verticalCanvasSize();
		if (rect.isEmpty())
			return {};
		return QPointF(rect.left() + point.x() * rect.width() / canvas.width(),
			       rect.top() + point.y() * rect.height() / canvas.height());
	}

	std::vector<int64_t> hits(const QPointF &point) const
	{
		struct HitData {
			QPointF point;
			std::vector<int64_t> ids;
		} data{point, {}};
		obs_source_t *source = verticalSceneSource();
		obs_scene_t *scene = source ? obs_scene_from_source(source) : nullptr;
		if (scene) {
			obs_scene_enum_items(
				scene,
				[](obs_scene_t *, obs_sceneitem_t *item, void *opaque) {
					auto *hit = static_cast<HitData *>(opaque);
					if (itemContains(item, hit->point))
						hit->ids.push_back(obs_sceneitem_get_id(item));
					return true;
				},
				&data);
		}
		obs_source_release(source);
		std::reverse(data.ids.begin(), data.ids.end());
		return data.ids;
	}

	bool nearResizeHandle(const QPointF &point, obs_sceneitem_t *item) const
	{
		const QPolygonF polygon = itemPolygon(item, [this](const QPointF &p) { return toWidget(p); });
		return polygon.size() == 4 && QLineF(point, polygon.at(2)).length() <= 12.0;
	}

	void mousePressEvent(QMouseEvent *event) override
	{
		if (event->button() != Qt::LeftButton || !canvasRect().contains(event->position()))
			return;
		event->accept();
		setFocus();
		const QPointF canvasPoint = toCanvas(event->position());
		obs_sceneitem_t *current = verticalItemRef(selected ? selected() : -1);
		if (current && !obs_sceneitem_locked(current) && nearResizeHandle(event->position(), current)) {
			resizing = true;
			grabMouse();
			startCanvas = canvasPoint;
			obs_sceneitem_get_scale(current, &startScale);
			obs_sceneitem_get_pos(current, &startPosition);
			obs_sceneitem_release(current);
			return;
		}
		/* A layer chosen in the list stays the editing target even when another
		 * source overlaps it. Alt-click still cycles through the visual stack. */
		if (current && !obs_sceneitem_locked(current) && obs_sceneitem_visible(current) &&
		    !(event->modifiers() & Qt::AltModifier) && itemContains(current, canvasPoint)) {
			dragging = true;
			grabMouse();
			startCanvas = canvasPoint;
			obs_sceneitem_get_pos(current, &startPosition);
			obs_sceneitem_get_scale(current, &startScale);
			obs_sceneitem_release(current);
			return;
		}
		obs_sceneitem_release(current);

		const std::vector<int64_t> underMouse = hits(canvasPoint);
		if (underMouse.empty()) {
			if (select)
				select(-1);
			update();
			return;
		}
		const bool cycle = (QLineF(lastClick, event->position()).length() < 5.0) ||
				   (event->modifiers() & Qt::AltModifier);
		cycleIndex = cycle ? (cycleIndex + 1) % int(underMouse.size()) : 0;
		lastClick = event->position();
		const int64_t id = underMouse[size_t(cycleIndex)];
		if (select)
			select(id);
		current = verticalItemRef(id);
		if (current && !obs_sceneitem_locked(current)) {
			dragging = true;
			grabMouse();
			startCanvas = canvasPoint;
			obs_sceneitem_get_pos(current, &startPosition);
			obs_sceneitem_get_scale(current, &startScale);
		}
		obs_sceneitem_release(current);
		update();
	}

	void mouseMoveEvent(QMouseEvent *event) override
	{
		obs_sceneitem_t *item = verticalItemRef(selected ? selected() : -1);
		if (!item)
			return;
		if (!dragging && !resizing) {
			setCursor(nearResizeHandle(event->position(), item) ? Qt::SizeFDiagCursor : Qt::ArrowCursor);
			obs_sceneitem_release(item);
			return;
		}
		event->accept();
		const QPointF delta = toCanvas(event->position()) - startCanvas;
		if (dragging) {
			vec2 position{startPosition.x + float(delta.x()), startPosition.y + float(delta.y())};
			obs_sceneitem_set_pos(item, &position);
		} else {
			obs_source_t *source = obs_sceneitem_get_source(item);
			const float sourceWidth = std::max(1u, obs_source_get_width(source));
			const float sourceHeight = std::max(1u, obs_source_get_height(source));
			vec2 scale{startScale.x + float(delta.x()) / sourceWidth,
				   startScale.y + float(delta.y()) / sourceHeight};
			if (event->modifiers() & Qt::ShiftModifier)
				scale.y = scale.x * (startScale.y < 0.0f ? -1.0f : 1.0f);
			if (std::abs(scale.x) >= 0.01f && std::abs(scale.y) >= 0.01f)
				obs_sceneitem_set_scale(item, &scale);
		}
		obs_sceneitem_release(item);
		if (changed)
			changed();
		update();
	}

	void mouseReleaseEvent(QMouseEvent *event) override
	{
		const bool edited = dragging || resizing;
		dragging = false;
		resizing = false;
		if (edited && mouseGrabber() == this)
			releaseMouse();
		event->accept();
		unsetCursor();
		if (changed)
			changed();
		if (edited && committed)
			committed();
	}
};

} // namespace

obs_canvas_t *PulseWeaverGetVerticalCanvas()
{
	return obs_get_canvas_by_name("Pulse Weaver Vertical");
}

namespace {

struct CopySceneItems {
	obs_scene_t *target = nullptr;
	float layoutScale = 1.0f;
	float offsetX = 0.0f;
	float offsetY = 0.0f;
	bool fitLayout = false;
};

bool copySceneItem(obs_scene_t *, obs_sceneitem_t *item, void *opaque)
{
	auto *copy = static_cast<CopySceneItems *>(opaque);
	obs_source_t *source = obs_sceneitem_get_source(item);
	obs_sceneitem_t *created = source ? obs_scene_add(copy->target, source) : nullptr;
	if (!created)
		return true;

	obs_transform_info transform = {};
	obs_sceneitem_get_info2(item, &transform);
	if (copy->fitLayout) {
		transform.pos.x = transform.pos.x * copy->layoutScale + copy->offsetX;
		transform.pos.y = transform.pos.y * copy->layoutScale + copy->offsetY;
		transform.scale.x *= copy->layoutScale;
		transform.scale.y *= copy->layoutScale;
		transform.bounds.x *= copy->layoutScale;
		transform.bounds.y *= copy->layoutScale;
	}
	obs_sceneitem_set_info2(created, &transform);
	obs_sceneitem_crop crop = {};
	obs_sceneitem_get_crop(item, &crop);
	obs_sceneitem_set_crop(created, &crop);
	obs_sceneitem_set_scale_filter(created, obs_sceneitem_get_scale_filter(item));
	obs_sceneitem_set_blending_method(created, obs_sceneitem_get_blending_method(item));
	obs_sceneitem_set_blending_mode(created, obs_sceneitem_get_blending_mode(item));
	obs_sceneitem_set_visible(created, obs_sceneitem_visible(item));
	obs_sceneitem_set_locked(created, obs_sceneitem_locked(item));
	obs_data_t *fromPrivate = obs_sceneitem_get_private_settings(item);
	obs_data_t *toPrivate = obs_sceneitem_get_private_settings(created);
	if (fromPrivate && toPrivate)
		obs_data_apply(toPrivate, fromPrivate);
	obs_data_release(fromPrivate);
	obs_data_release(toPrivate);
	return true;
}

QString uniqueVerticalSceneName(obs_canvas_t *canvas, const QString &suggested)
{
	QString result = suggested;
	for (int suffix = 2;; ++suffix) {
		obs_source_t *existing = obs_canvas_get_source_by_name(canvas, result.toUtf8().constData());
		if (!existing)
			return result;
		obs_source_release(existing);
		result = suggested + " " + QString::number(suffix);
	}
}

} // namespace

obs_scene_t *PulseWeaverDuplicateToVertical(obs_source_t *source, const char *name, bool fitHorizontalLayout,
					 bool linkToHorizontal)
{
	obs_scene_t *from = source ? obs_scene_from_source(source) : nullptr;
	obs_canvas_t *canvas = PulseWeaverGetVerticalCanvas();
	if (!from || !canvas) {
		obs_canvas_release(canvas);
		return nullptr;
	}

	const QString requested = QString::fromUtf8(name && *name ? name : obs_source_get_name(source));
	const QString unique = uniqueVerticalSceneName(canvas, requested);
	obs_scene_t *target = obs_canvas_scene_create(canvas, unique.toUtf8().constData());
	if (!target) {
		obs_canvas_release(canvas);
		return nullptr;
	}

	obs_video_info horizontal = {};
	obs_video_info vertical = {};
	obs_get_video_info(&horizontal);
	obs_canvas_get_video_info(canvas, &vertical);
	CopySceneItems copy{target, 1.0f, 0.0f, 0.0f, fitHorizontalLayout};
	if (fitHorizontalLayout && horizontal.base_width && horizontal.base_height && vertical.base_width &&
	    vertical.base_height) {
		copy.layoutScale = std::min(float(vertical.base_width) / float(horizontal.base_width),
					    float(vertical.base_height) / float(horizontal.base_height));
		copy.offsetX = (float(vertical.base_width) - float(horizontal.base_width) * copy.layoutScale) / 2.0f;
		copy.offsetY = (float(vertical.base_height) - float(horizontal.base_height) * copy.layoutScale) / 2.0f;
	}
	obs_scene_enum_items(from, copySceneItem, &copy);
	obs_source_copy_filters(obs_scene_get_source(target), source);

	obs_data_t *privateSettings = obs_source_get_private_settings(obs_scene_get_source(target));
	if (privateSettings) {
		obs_data_set_string(privateSettings, "pulseweaver.horizontal_uuid", obs_source_get_uuid(source));
		obs_data_set_bool(privateSettings, "pulseweaver.follow_horizontal", linkToHorizontal);
		obs_data_set_bool(privateSettings, "pulseweaver.native_vertical", true);
		obs_data_release(privateSettings);
	}
	obs_canvas_release(canvas);
	return target;
}

obs_source_t *PulseWeaverFindLinkedVerticalScene(const char *horizontalUuid)
{
	if (!horizontalUuid || !*horizontalUuid)
		return nullptr;
	obs_canvas_t *canvas = PulseWeaverGetVerticalCanvas();
	if (!canvas)
		return nullptr;
	struct Search {
		const char *uuid;
		obs_source_t *result = nullptr;
	} search{horizontalUuid};
	obs_canvas_enum_scenes(
		canvas,
		[](void *opaque, obs_source_t *scene) {
			auto *search = static_cast<Search *>(opaque);
			obs_data_t *settings = obs_source_get_private_settings(scene);
			const bool follows = settings && obs_data_get_bool(settings, "pulseweaver.follow_horizontal");
			const char *mapped = settings ? obs_data_get_string(settings, "pulseweaver.horizontal_uuid") : "";
			if (follows && mapped && strcmp(mapped, search->uuid) == 0)
				search->result = obs_source_get_ref(scene);
			obs_data_release(settings);
			return search->result == nullptr;
		},
		&search);
	obs_canvas_release(canvas);
	return search.result;
}

struct PulseVerticalEditor::Impl {
	PulseVerticalEditor *owner;
	OBSBasic *main;
	OBSQTDisplay *display = nullptr;
	VerticalOverlay *overlay = nullptr;
	QListWidget *scenes = nullptr;
	QTreeWidget *sources = nullptr;
	QLabel *canvasLabel = nullptr;
	QLabel *selectionLabel = nullptr;
	QDoubleSpinBox *x = nullptr;
	QDoubleSpinBox *y = nullptr;
	QDoubleSpinBox *scaleX = nullptr;
	QDoubleSpinBox *scaleY = nullptr;
	QDoubleSpinBox *rotation = nullptr;
	QPushButton *linkScene = nullptr;
	int64_t selectedId = -1;
	bool updating = false;
	bool displayEnabled = false;

	Impl(PulseVerticalEditor *owner_, OBSBasic *main_) : owner(owner_), main(main_) {}

	static void render(void *opaque, uint32_t width, uint32_t height)
	{
		auto *self = static_cast<Impl *>(opaque);
		obs_canvas_t *canvas = PulseWeaverGetVerticalCanvas();
		if (!canvas)
			return;
		obs_video_info info = {};
		obs_sceneitem_t *selectedItem = verticalItemRef(self->selectedId);
		if (obs_canvas_get_video_info(canvas, &info))
			renderCanvasFit(canvas, selectedItem, info.base_width, info.base_height, width, height);
		obs_sceneitem_release(selectedItem);
		obs_canvas_release(canvas);
	}

	void setSelected(int64_t id)
	{
		selectedId = id;
		obs_source_t *sceneSource = verticalSceneSource();
		obs_scene_t *scene = sceneSource ? obs_scene_from_source(sceneSource) : nullptr;
		if (scene) {
			obs_scene_enum_items(
				scene,
				[](obs_scene_t *, obs_sceneitem_t *item, void *opaque) {
					const int64_t wanted = *static_cast<int64_t *>(opaque);
					obs_sceneitem_select(item, obs_sceneitem_get_id(item) == wanted);
					return true;
				},
				&selectedId);
		}
		obs_source_release(sceneSource);
		refreshSources();
		refreshInspector();
		if (overlay)
			overlay->update();
	}

	void refreshScenes()
	{
		obs_canvas_t *canvas = PulseWeaverGetVerticalCanvas();
		QSignalBlocker blocker(scenes);
		const QString previous = scenes->currentItem() ? scenes->currentItem()->data(Qt::UserRole).toString() : QString();
		scenes->clear();
		if (!canvas) {
			canvasLabel->setText("NO VERTICAL CANVAS — create one from Show Control");
			return;
		}
		const QSize size = verticalCanvasSize();
		canvasLabel->setText(QString("NATIVE CANVAS  •  %1  •  %2 × %3")
					     .arg(QString::fromUtf8(obs_canvas_get_name(canvas)))
					     .arg(size.width())
					     .arg(size.height()));
		obs_source_t *current = obs_canvas_get_channel(canvas, 0);
		const QString currentName = current ? QString::fromUtf8(obs_source_get_name(current)) : QString();
		obs_source_release(current);
		struct Data {
			QListWidget *list;
			QString current;
			QString previous;
			QListWidgetItem *currentItem = nullptr;
			QListWidgetItem *previousItem = nullptr;
		} data{scenes, currentName, previous};
		obs_canvas_enum_scenes(
			canvas,
			[](void *opaque, obs_source_t *source) {
				auto *data = static_cast<Data *>(opaque);
				const QString name = QString::fromUtf8(obs_source_get_name(source));
				auto *item = new QListWidgetItem(name, data->list);
				item->setData(Qt::UserRole, name);
				if (name == data->current)
					data->currentItem = item;
				if (!data->previous.isEmpty() && name == data->previous)
					data->previousItem = item;
				return true;
			},
			&data);
		/* The canvas channel is authoritative. Previously the old visual
		 * selection could override it later in enumeration, making the list
		 * show a populated scene while an empty scene was actually rendered. */
		QListWidgetItem *chosen = data.currentItem ? data.currentItem : data.previousItem;
		if (!chosen && scenes->count() > 0)
			chosen = scenes->item(0);
		if (chosen)
			scenes->setCurrentItem(chosen);
		if (!current && scenes->count() > 0) {
			QListWidgetItem *initial = chosen;
			scenes->setCurrentItem(initial);
			const QByteArray name = initial->data(Qt::UserRole).toString().toUtf8();
			obs_source_t *source = obs_canvas_get_source_by_name(canvas, name.constData());
			if (source)
				obs_canvas_set_channel(canvas, 0, source);
			obs_source_release(source);
		}
		obs_canvas_release(canvas);
	}

	void refreshSources()
	{
		QSignalBlocker blocker(sources);
		sources->clear();
		obs_source_t *sceneSource = verticalSceneSource();
		obs_scene_t *scene = sceneSource ? obs_scene_from_source(sceneSource) : nullptr;
		struct Row {
			int64_t id;
			QString name;
			QString type;
			bool visible;
			bool locked;
		};
		std::vector<Row> rows;
		if (scene) {
			obs_scene_enum_items(
				scene,
				[](obs_scene_t *, obs_sceneitem_t *item, void *opaque) {
					auto *rows = static_cast<std::vector<Row> *>(opaque);
					obs_source_t *source = obs_sceneitem_get_source(item);
					const char *type = obs_source_get_unversioned_id(source);
					rows->push_back({obs_sceneitem_get_id(item), QString::fromUtf8(obs_source_get_name(source)),
							 QString::fromUtf8(obs_source_get_display_name(type)),
							 obs_sceneitem_visible(item), obs_sceneitem_locked(item)});
					return true;
				},
				&rows);
		}
		obs_source_release(sceneSource);
		std::reverse(rows.begin(), rows.end());
		for (const Row &row : rows) {
			auto *item = new QTreeWidgetItem(sources, {row.visible ? "●" : "○", row.locked ? "🔒" : "🔓", row.name, row.type});
			item->setData(2, Qt::UserRole, QVariant::fromValue<qlonglong>(row.id));
			item->setForeground(0, row.visible ? QColor("#f8fafc") : QColor("#667085"));
			item->setForeground(1, row.locked ? QColor("#f8fafc") : QColor("#667085"));
			QFont font = item->font(2);
			font.setStrikeOut(!row.visible);
			item->setFont(2, font);
			if (row.id == selectedId)
				sources->setCurrentItem(item);
		}
	}

	void refreshInspector()
	{
		obs_sceneitem_t *item = verticalItemRef(selectedId);
		updating = true;
		QSignalBlocker bx(x), by(y), bsx(scaleX), bsy(scaleY), br(rotation);
		if (!item) {
			selectionLabel->setText("Select a source on the canvas or in the layer list.");
			for (QDoubleSpinBox *field : {x, y, scaleX, scaleY, rotation})
				field->setEnabled(false);
		} else {
			obs_source_t *source = obs_sceneitem_get_source(item);
			selectionLabel->setText(QString::fromUtf8(obs_source_get_name(source)));
			vec2 position, scale;
			obs_sceneitem_get_pos(item, &position);
			obs_sceneitem_get_scale(item, &scale);
			x->setValue(position.x);
			y->setValue(position.y);
			scaleX->setValue(scale.x);
			scaleY->setValue(scale.y);
			rotation->setValue(obs_sceneitem_get_rot(item));
			for (QDoubleSpinBox *field : {x, y, scaleX, scaleY, rotation})
				field->setEnabled(!obs_sceneitem_locked(item));
		}
		updating = false;
		obs_sceneitem_release(item);
	}

	void applyInspector()
	{
		if (updating)
			return;
		obs_sceneitem_t *item = verticalItemRef(selectedId);
		if (!item || obs_sceneitem_locked(item)) {
			obs_sceneitem_release(item);
			return;
		}
		vec2 position{float(x->value()), float(y->value())};
		vec2 scale{float(scaleX->value()), float(scaleY->value())};
		obs_sceneitem_set_pos(item, &position);
		obs_sceneitem_set_scale(item, &scale);
		obs_sceneitem_set_rot(item, float(rotation->value()));
		obs_sceneitem_release(item);
		if (overlay)
			overlay->update();
	}

	void switchScene(QListWidgetItem *item)
	{
		if (updating || !item)
			return;
		const QByteArray name = item->data(Qt::UserRole).toString().toUtf8();
		obs_canvas_t *canvas = PulseWeaverGetVerticalCanvas();
		obs_source_t *source = canvas ? obs_canvas_get_source_by_name(canvas, name.constData()) : nullptr;
		if (canvas && source)
			obs_canvas_set_channel(canvas, 0, source);
		obs_source_release(source);
		obs_canvas_release(canvas);
		selectedId = -1;
		refreshLinkState();
		refreshSources();
		refreshInspector();
	}

	void refreshLinkState()
	{
		if (!linkScene)
			return;
		obs_source_t *source = verticalSceneSource();
		obs_data_t *settings = source ? obs_source_get_private_settings(source) : nullptr;
		QSignalBlocker blocker(linkScene);
		linkScene->setChecked(settings && obs_data_get_bool(settings, "pulseweaver.follow_horizontal"));
		linkScene->setText(linkScene->isChecked() ? "🔗 LINKED TO 16:9" : "⛓ LINK TO 16:9 SCENE");
		obs_data_release(settings);
		obs_source_release(source);
	}

	void setLinkState(bool linked)
	{
		obs_source_t *vertical = verticalSceneSource();
		obs_source_t *horizontal = obs_frontend_get_current_scene();
		obs_data_t *settings = vertical ? obs_source_get_private_settings(vertical) : nullptr;
		if (settings && horizontal) {
			obs_data_set_string(settings, "pulseweaver.horizontal_uuid", obs_source_get_uuid(horizontal));
			obs_data_set_bool(settings, "pulseweaver.follow_horizontal", linked);
			obs_data_set_bool(settings, "pulseweaver.native_vertical", true);
			obs_frontend_save();
		}
		obs_data_release(settings);
		obs_source_release(horizontal);
		obs_source_release(vertical);
		refreshLinkState();
	}

	void importHorizontal()
	{
		obs_source_t *horizontal = obs_frontend_get_current_scene();
		if (!horizontal)
			return;
		const QString suggested = QString::fromUtf8(obs_source_get_name(horizontal)) + " — Vertical";
		bool ok = false;
		const QString name = QInputDialog::getText(owner, "Duplicate horizontal scene", "Vertical scene name",
							 QLineEdit::Normal, suggested, &ok).trimmed();
		if (ok && !name.isEmpty()) {
			obs_scene_t *created = PulseWeaverDuplicateToVertical(horizontal, name.toUtf8().constData(), true, true);
			obs_canvas_t *canvas = PulseWeaverGetVerticalCanvas();
			if (created && canvas)
				obs_canvas_set_channel(canvas, 0, obs_scene_get_source(created));
			obs_canvas_release(canvas);
			obs_scene_release(created);
			obs_frontend_save();
		}
		obs_source_release(horizontal);
		selectedId = -1;
		refreshScenes();
		refreshLinkState();
		refreshSources();
		refreshInspector();
	}

	void createScene(bool duplicate)
	{
		obs_canvas_t *canvas = PulseWeaverGetVerticalCanvas();
		if (!canvas)
			return;
		const QString suggested = duplicate && scenes->currentItem() ? scenes->currentItem()->text() + " Copy" : "New Vertical Scene";
		bool ok = false;
		const QString name = QInputDialog::getText(owner, "Vertical scene", "Scene name", QLineEdit::Normal, suggested, &ok).trimmed();
		if (!ok || name.isEmpty()) {
			obs_canvas_release(canvas);
			return;
		}
		obs_scene_t *created = nullptr;
		if (duplicate) {
			obs_source_t *source = obs_canvas_get_channel(canvas, 0);
			obs_scene_t *scene = source ? obs_scene_from_source(source) : nullptr;
			if (scene)
				created = obs_scene_duplicate(scene, name.toUtf8().constData(), OBS_SCENE_DUP_REFS);
			obs_source_release(source);
		} else {
			created = obs_canvas_scene_create(canvas, name.toUtf8().constData());
		}
		if (created) {
			obs_canvas_set_channel(canvas, 0, obs_scene_get_source(created));
			obs_scene_release(created);
		}
		obs_canvas_release(canvas);
		selectedId = -1;
		refreshScenes();
		refreshSources();
		refreshInspector();
	}

	void deleteScene()
	{
		obs_canvas_t *canvas = PulseWeaverGetVerticalCanvas();
		if (!canvas || scenes->count() <= 1) {
			obs_canvas_release(canvas);
			return;
		}
		if (QMessageBox::question(owner, "Delete vertical scene", "Delete the selected vertical scene?") != QMessageBox::Yes) {
			obs_canvas_release(canvas);
			return;
		}
		obs_source_t *current = obs_canvas_get_channel(canvas, 0);
		obs_scene_t *scene = current ? obs_scene_from_source(current) : nullptr;
		QListWidgetItem *replacement = scenes->item(scenes->currentRow() == 0 ? 1 : 0);
		if (replacement) {
			const QByteArray name = replacement->data(Qt::UserRole).toString().toUtf8();
			obs_source_t *next = obs_canvas_get_source_by_name(canvas, name.constData());
			if (next)
				obs_canvas_set_channel(canvas, 0, next);
			obs_source_release(next);
		}
		if (scene)
			obs_canvas_scene_remove(scene);
		obs_source_release(current);
		obs_canvas_release(canvas);
		selectedId = -1;
		refreshScenes();
		refreshSources();
		refreshInspector();
	}

	void addExisting(const QString &uuid)
	{
		obs_source_t *sceneSource = verticalSceneSource();
		obs_scene_t *scene = sceneSource ? obs_scene_from_source(sceneSource) : nullptr;
		obs_source_t *source = obs_get_source_by_uuid(uuid.toUtf8().constData());
		obs_sceneitem_t *item = scene && source ? obs_scene_add(scene, source) : nullptr;
		if (item)
			selectedId = obs_sceneitem_get_id(item);
		obs_source_release(source);
		obs_source_release(sceneSource);
		refreshSources();
		refreshInspector();
	}

	void createSource(const QString &unversionedId, const QString &label)
	{
		const char *latest = obs_get_latest_input_type_id(unversionedId.toUtf8().constData());
		const QByteArray id = latest ? QByteArray(latest) : unversionedId.toUtf8();
		QString base = label;
		QString name = base;
		for (int suffix = 2;; ++suffix) {
			obs_source_t *existing = obs_get_source_by_name(name.toUtf8().constData());
			if (!existing)
				break;
			obs_source_release(existing);
			name = base + " " + QString::number(suffix);
		}
		bool ok = false;
		name = QInputDialog::getText(owner, "Add vertical source", "Source name", QLineEdit::Normal, name, &ok).trimmed();
		if (!ok || name.isEmpty())
			return;
		obs_data_t *settings = obs_data_create();
		if (unversionedId == "browser_source") {
			const QSize canvas = verticalCanvasSize();
			obs_data_set_int(settings, "width", canvas.width());
			obs_data_set_int(settings, "height", canvas.height());
		}
		obs_source_t *source = obs_source_create(id.constData(), name.toUtf8().constData(), settings, nullptr);
		obs_data_release(settings);
		obs_source_t *sceneSource = verticalSceneSource();
		obs_scene_t *scene = sceneSource ? obs_scene_from_source(sceneSource) : nullptr;
		obs_sceneitem_t *item = scene && source ? obs_scene_add(scene, source) : nullptr;
		if (item)
			selectedId = obs_sceneitem_get_id(item);
		obs_source_release(sceneSource);
		refreshSources();
		refreshInspector();
		if (source)
			owner->OpenProperties(source);
		obs_source_release(source);
	}

	void showAddMenu(QPushButton *button)
	{
		QMenu menu(button);
		QMenu *create = menu.addMenu("NEW SOURCE");
		for (const auto &definition : std::vector<std::pair<QString, QString>>{{"dshow_input", "Camera"},
			 {"game_capture", "Game Capture"}, {"window_capture", "Window Capture"},
			 {"monitor_capture", "Display Capture"}, {"browser_source", "Browser / Overlay"},
			 {"image_source", "Image"}, {"ffmpeg_source", "Media"}, {"text_gdiplus", "Text"}}) {
			QAction *action = create->addAction(definition.second);
			QObject::connect(action, &QAction::triggered, owner,
					 [this, definition] { createSource(definition.first, definition.second); });
		}
		QMenu *existing = menu.addMenu("ADD EXISTING SOURCE");
		struct Existing {
			QString name;
			QString uuid;
		};
		std::vector<Existing> entries;
		obs_source_t *currentScene = verticalSceneSource();
		const QString currentUuid = currentScene ? QString::fromUtf8(obs_source_get_uuid(currentScene)) : QString();
		obs_source_release(currentScene);
		obs_enum_sources(
			[](void *opaque, obs_source_t *source) {
				auto *entries = static_cast<std::vector<Existing> *>(opaque);
				entries->push_back({QString::fromUtf8(obs_source_get_name(source)), QString::fromUtf8(obs_source_get_uuid(source))});
				return true;
			},
			&entries);
		std::sort(entries.begin(), entries.end(), [](const Existing &a, const Existing &b) {
			return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
		});
		for (const Existing &entry : entries) {
			if (entry.uuid == currentUuid)
				continue;
			QAction *action = existing->addAction(entry.name);
			QObject::connect(action, &QAction::triggered, owner, [this, entry] { addExisting(entry.uuid); });
		}
		menu.exec(button->mapToGlobal(QPoint(0, button->height())));
	}

	void removeSource()
	{
		obs_sceneitem_t *item = verticalItemRef(selectedId);
		if (item)
			obs_sceneitem_remove(item);
		obs_sceneitem_release(item);
		selectedId = -1;
		refreshSources();
		refreshInspector();
	}

	void moveSource(obs_order_movement movement)
	{
		obs_sceneitem_t *item = verticalItemRef(selectedId);
		if (item)
			obs_sceneitem_set_order(item, movement);
		obs_sceneitem_release(item);
		refreshSources();
	}

	void fitSelected(bool fill)
	{
		obs_sceneitem_t *item = verticalItemRef(selectedId);
		if (!item || obs_sceneitem_locked(item)) {
			obs_sceneitem_release(item);
			return;
		}
		obs_source_t *source = obs_sceneitem_get_source(item);
		const float width = std::max(1u, obs_source_get_width(source));
		const float height = std::max(1u, obs_source_get_height(source));
		const QSize canvas = verticalCanvasSize();
		const float factor = fill ? std::max(canvas.width() / width, canvas.height() / height)
					  : std::min(canvas.width() / width, canvas.height() / height);
		vec2 scale{factor, factor};
		vec2 position{(canvas.width() - width * factor) / 2.0f, (canvas.height() - height * factor) / 2.0f};
		obs_sceneitem_set_bounds_type(item, OBS_BOUNDS_NONE);
		obs_sceneitem_set_alignment(item, OBS_ALIGN_LEFT | OBS_ALIGN_TOP);
		obs_sceneitem_set_scale(item, &scale);
		obs_sceneitem_set_pos(item, &position);
		obs_sceneitem_release(item);
		refreshInspector();
	}

	void centerSelected()
	{
		obs_sceneitem_t *item = verticalItemRef(selectedId);
		if (!item || obs_sceneitem_locked(item)) {
			obs_sceneitem_release(item);
			return;
		}
		obs_source_t *source = obs_sceneitem_get_source(item);
		vec2 scale;
		obs_sceneitem_get_scale(item, &scale);
		const QSize canvas = verticalCanvasSize();
		vec2 position{(canvas.width() - obs_source_get_width(source) * scale.x) / 2.0f,
			      (canvas.height() - obs_source_get_height(source) * scale.y) / 2.0f};
		obs_sceneitem_set_alignment(item, OBS_ALIGN_LEFT | OBS_ALIGN_TOP);
		obs_sceneitem_set_pos(item, &position);
		obs_sceneitem_release(item);
		refreshInspector();
	}

	void build()
	{
		auto *root = new QVBoxLayout(owner);
		root->setContentsMargins(8, 8, 8, 8);
		root->setSpacing(7);
		canvasLabel = new QLabel("VERTICAL CANVAS");
		canvasLabel->setObjectName("PulseWeaverKicker");
		root->addWidget(canvasLabel);
		auto *splitter = new QSplitter;
		root->addWidget(splitter, 1);

		auto *left = new QFrame;
		left->setObjectName("PulseWeaverCard");
		left->setMinimumWidth(280);
		left->setMaximumWidth(390);
		auto *leftLayout = new QVBoxLayout(left);
		auto *sceneHeader = new QHBoxLayout;
		sceneHeader->addWidget(new QLabel("VERTICAL SCENES"));
		for (const QString &label : {"+", "⧉", "−"}) {
			auto *button = new QPushButton(label);
			button->setMaximumWidth(36);
			sceneHeader->addWidget(button);
			if (label == "+")
				QObject::connect(button, &QPushButton::clicked, owner, [this] { createScene(false); });
			else if (label == "⧉")
				QObject::connect(button, &QPushButton::clicked, owner, [this] { createScene(true); });
			else
				QObject::connect(button, &QPushButton::clicked, owner, [this] { deleteScene(); });
		}
		leftLayout->addLayout(sceneHeader);
		auto *fromHorizontal = new QPushButton("⧉  DUPLICATE CURRENT 16:9 SCENE");
		fromHorizontal->setToolTip("Create an editable 9:16 scene from the active horizontal scene and link scene switching");
		leftLayout->addWidget(fromHorizontal);
		QObject::connect(fromHorizontal, &QPushButton::clicked, owner, [this] { importHorizontal(); });
		scenes = new QListWidget;
		scenes->setMinimumHeight(140);
		leftLayout->addWidget(scenes, 1);
		linkScene = new QPushButton("⛓ LINK TO 16:9 SCENE");
		linkScene->setCheckable(true);
		linkScene->setToolTip("When linked, selecting its horizontal scene also selects this vertical scene");
		leftLayout->addWidget(linkScene);
		QObject::connect(linkScene, &QPushButton::toggled, owner, [this](bool checked) { setLinkState(checked); });
		QObject::connect(scenes, &QListWidget::currentItemChanged, owner,
				 [this](QListWidgetItem *current) { switchScene(current); });

		auto *sourceHeader = new QHBoxLayout;
		sourceHeader->addWidget(new QLabel("SOURCES / LAYERS"));
		auto *add = new QPushButton("+");
		auto *remove = new QPushButton("−");
		auto *up = new QPushButton("↑");
		auto *down = new QPushButton("↓");
		for (QPushButton *button : {add, remove, up, down}) {
			button->setMaximumWidth(36);
			sourceHeader->addWidget(button);
		}
		leftLayout->addLayout(sourceHeader);
		QObject::connect(add, &QPushButton::clicked, owner, [this, add] { showAddMenu(add); });
		QObject::connect(remove, &QPushButton::clicked, owner, [this] { removeSource(); });
		QObject::connect(up, &QPushButton::clicked, owner, [this] { moveSource(OBS_ORDER_MOVE_UP); });
		QObject::connect(down, &QPushButton::clicked, owner, [this] { moveSource(OBS_ORDER_MOVE_DOWN); });
		sources = new QTreeWidget;
		sources->setHeaderLabels({"", "", "SOURCE", "TYPE"});
		sources->setColumnWidth(0, 32);
		sources->setColumnWidth(1, 36);
		sources->header()->setSectionResizeMode(2, QHeaderView::Stretch);
		sources->header()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
		leftLayout->addWidget(sources, 2);
		QObject::connect(sources, &QTreeWidget::currentItemChanged, owner,
				 [this](QTreeWidgetItem *entry) {
					 if (entry)
						 setSelected(entry->data(2, Qt::UserRole).toLongLong());
				 });
		QObject::connect(sources, &QTreeWidget::itemClicked, owner, [this](QTreeWidgetItem *entry, int column) {
			const int64_t id = entry->data(2, Qt::UserRole).toLongLong();
			obs_sceneitem_t *item = verticalItemRef(id);
			if (!item)
				return;
			if (column == 0)
				obs_sceneitem_set_visible(item, !obs_sceneitem_visible(item));
			else if (column == 1)
				obs_sceneitem_set_locked(item, !obs_sceneitem_locked(item));
			obs_sceneitem_release(item);
			setSelected(id);
		});
		auto *sourceActions = new QGridLayout;
		auto *properties = new QPushButton("PROPERTIES");
		auto *filters = new QPushButton("FILTERS / AUDIO FX");
		sourceActions->addWidget(properties, 0, 0);
		sourceActions->addWidget(filters, 0, 1);
		leftLayout->addLayout(sourceActions);
		QObject::connect(properties, &QPushButton::clicked, owner, [this] {
			obs_source_t *source = verticalItemSourceRef(selectedId);
			if (source)
				owner->OpenProperties(source);
			obs_source_release(source);
		});
		QObject::connect(filters, &QPushButton::clicked, owner, [this] {
			obs_source_t *source = verticalItemSourceRef(selectedId);
			if (source)
				owner->OpenFilters(source);
			obs_source_release(source);
		});
		splitter->addWidget(left);

		auto *stage = new QFrame;
		stage->setObjectName("PulseWeaverStage");
		auto *stageLayout = new QVBoxLayout(stage);
		stageLayout->setContentsMargins(0, 0, 0, 0);
		stageLayout->setSpacing(0);
		overlay = new VerticalOverlay(stage);
		display = overlay;
		overlay->setAccessibleName("Vertical canvas interaction surface");
		overlay->selected = [this] { return selectedId; };
		overlay->select = [this](int64_t id) { setSelected(id); };
		overlay->changed = [this] { refreshInspector(); };
		overlay->committed = [] { obs_frontend_save(); };
		stageLayout->addWidget(overlay);
		QObject::connect(display, &OBSQTDisplay::DisplayCreated, owner, [this](OBSQTDisplay *created) {
			obs_display_add_draw_callback(created->GetDisplay(), render, this);
			obs_display_set_enabled(created->GetDisplay(), displayEnabled);
		});
		splitter->addWidget(stage);

		auto *inspector = new QFrame;
		inspector->setObjectName("PulseWeaverCard");
		inspector->setMinimumWidth(250);
		inspector->setMaximumWidth(340);
		auto *inspectorLayout = new QVBoxLayout(inspector);
		inspectorLayout->addWidget(new QLabel("TRANSFORM / INSPECTOR"));
		selectionLabel = new QLabel("Select a source on the canvas or in the layer list.");
		selectionLabel->setObjectName("PulseWeaverMuted");
		selectionLabel->setWordWrap(true);
		inspectorLayout->addWidget(selectionLabel);
		auto *form = new QFormLayout;
		auto makeField = [form](const QString &name, double minimum, double maximum, int decimals) {
			auto *field = new QDoubleSpinBox;
			field->setRange(minimum, maximum);
			field->setDecimals(decimals);
			field->setSingleStep(decimals ? 0.05 : 1.0);
			form->addRow(name, field);
			return field;
		};
		x = makeField("X", -10000, 10000, 0);
		y = makeField("Y", -10000, 10000, 0);
		scaleX = makeField("Scale X", -100, 100, 3);
		scaleY = makeField("Scale Y", -100, 100, 3);
		rotation = makeField("Rotation", -3600, 3600, 1);
		inspectorLayout->addLayout(form);
		for (QDoubleSpinBox *field : {x, y, scaleX, scaleY, rotation})
			QObject::connect(field, &QDoubleSpinBox::valueChanged, owner, [this] { applyInspector(); });
		auto *fit = new QPushButton("FIT TO VERTICAL CANVAS");
		auto *fill = new QPushButton("FILL VERTICAL CANVAS");
		auto *center = new QPushButton("CENTER");
		inspectorLayout->addWidget(fit);
		inspectorLayout->addWidget(fill);
		inspectorLayout->addWidget(center);
		QObject::connect(fit, &QPushButton::clicked, owner, [this] { fitSelected(false); });
		QObject::connect(fill, &QPushButton::clicked, owner, [this] { fitSelected(true); });
		QObject::connect(center, &QPushButton::clicked, owner, [this] { centerSelected(); });
		auto *hint = new QLabel("Drag sources directly. Use the corner handle to resize; hold Shift for linked scaling. Alt-click or repeatedly click to cycle overlapping layers.");
		hint->setObjectName("PulseWeaverMuted");
		hint->setWordWrap(true);
		inspectorLayout->addWidget(hint);
		inspectorLayout->addStretch();
		splitter->addWidget(inspector);
		splitter->setStretchFactor(1, 1);
		refreshScenes();
		refreshLinkState();
		refreshSources();
		refreshInspector();
	}
};

PulseVerticalEditor::PulseVerticalEditor(OBSBasic *main, QWidget *parent)
	: QWidget(parent), impl(std::make_unique<Impl>(this, main))
{
	impl->build();
}

PulseVerticalEditor::~PulseVerticalEditor()
{
	Shutdown();
}

void PulseVerticalEditor::Refresh()
{
	if (!impl)
		return;
	impl->refreshScenes();
	impl->refreshSources();
	impl->refreshInspector();
}

void PulseVerticalEditor::SetDisplayEnabled(bool enabled)
{
	if (!impl)
		return;
	impl->displayEnabled = enabled;
	if (impl->display && impl->display->GetDisplay())
		obs_display_set_enabled(impl->display->GetDisplay(), enabled);
}

void PulseVerticalEditor::Shutdown()
{
	if (impl && impl->display)
		impl->display->DestroyDisplay();
}

void PulseVerticalEditor::OpenProperties(obs_source_t *source)
{
	if (impl && impl->main && source)
		impl->main->CreatePropertiesWindow(source);
}

void PulseVerticalEditor::OpenFilters(obs_source_t *source)
{
	if (impl && impl->main && source)
		impl->main->OpenFilters(OBSSource(source));
}
