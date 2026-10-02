#include "pulse-motion-engine.hpp"
#include "pulse-show-templates.hpp"
#include "pulse-scene-item-ref.hpp"
#include <obs-module.h>
#include <graphics/vec4.h>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QImage>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QSaveFile>
#include <QSignalBlocker>
#include <QScrollArea>
#include <QSlider>
#include <QSpinBox>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrl>
#include <QUuid>
#include <QVBoxLayout>
#include <QWindow>
#include <QWizard>
#include <QWizardPage>
#include <algorithm>
#include <cmath>
#include <mutex>
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
QString freshId()
{
	return QUuid::createUuid().toString(QUuid::WithoutBraces);
}
bool outputsBusy()
{
	bool busy = obs_frontend_streaming_active() || obs_frontend_recording_active();
	obs_enum_outputs(
		[](void *opaque, obs_output_t *output) {
			if (obs_output_active(output)) {
				*static_cast<bool *>(opaque) = true;
			}
			return true;
		},
		&busy);
	return busy;
}
void sizeScene(obs_scene_t *scene, int width, int height)
{
	OBSDataAutoRelease settings = obs_source_get_settings(obs_scene_get_source(scene));
	obs_data_set_bool(settings, "custom_size", true);
	obs_data_set_int(settings, "cx", width);
	obs_data_set_int(settings, "cy", height);
	obs_source_update(obs_scene_get_source(scene), settings);
	obs_source_load(obs_scene_get_source(scene));
}
bool compatible(obs_source_t *source, const QString &role)
{
	if (!source || obs_scene_from_source(source) || obs_group_from_source(source) ||
	    !(obs_source_get_output_flags(source) & OBS_SOURCE_VIDEO)) {
		return false;
	}
	const QString kind = obs_source_get_unversioned_id(source);
	if (role == "screen") {
		return kind == "monitor_capture" || kind == "display_capture" || kind == "window_capture";
	}
	if (role == "game") {
		return kind == "game_capture" || kind == "window_capture";
	}
	if (role == "presenter" || role == "activity" || role == "detail") {
		return kind == "dshow_input" || kind == "ffmpeg_source" || kind == "window_capture" ||
		       kind == "ndi_source";
	}
	return true;
}
void position(obs_sceneitem_t *item, const PulseShow::Panel &panel, int width, int height,
	      const QJsonObject &options = {})
{
	vec2 pos{float(panel.rect.x() * width), float(panel.rect.y() * height)};
	vec2 bounds{float(panel.rect.width() * width), float(panel.rect.height() * height)};
	obs_sceneitem_set_alignment(item, OBS_ALIGN_LEFT | OBS_ALIGN_TOP);
	obs_sceneitem_set_pos(item, &pos);
	obs_sceneitem_set_bounds(item, &bounds);
	obs_sceneitem_set_bounds_type(item, panel.fill ? OBS_BOUNDS_SCALE_OUTER : OBS_BOUNDS_SCALE_INNER);
	obs_sceneitem_set_bounds_alignment(item, OBS_ALIGN_CENTER);
	obs_sceneitem_set_bounds_crop(item, panel.fill);
	obs_sceneitem_crop crop{};
	if (panel.fill) {
		obs_source_t *source = obs_sceneitem_get_source(item);
		const double sw = obs_source_get_width(source), sh = obs_source_get_height(source);
		if (sw > 0 && sh > 0 && bounds.x > 0 && bounds.y > 0) {
			const double ratio = bounds.x / bounds.y;
			const double fx = qBound(0., options.value(panel.role + "X").toDouble(.5), 1.);
			const double fy = qBound(0., options.value(panel.role + "Y").toDouble(.5), 1.);
			if (sw / sh > ratio) {
				const int excess = int(sw - sh * ratio);
				crop.left = int(excess * fx);
				crop.right = excess - crop.left;
			} else {
				const int excess = int(sh - sw / ratio);
				crop.top = int(excess * fy);
				crop.bottom = excess - crop.top;
			}
		}
	}
	obs_sceneitem_set_crop(item, &crop);
}
// Filters belong to a wrapper scene, never to a reused capture source.
obs_scene_t *treatedCamera(obs_source_t *camera, const QString &name, bool temporary, int width, int height,
			   const QString &maskPath, const QJsonObject &options)
{
	OBSSceneAutoRelease scene = temporary ? obs_scene_create_private(name.toUtf8().constData())
					      : obs_scene_create(name.toUtf8().constData());
	if (!scene) {
		return nullptr;
	}
	sizeScene(scene, width, height);
	obs_sceneitem_t *item = obs_scene_add(scene, camera);
	if (!item) {
		if (!temporary) {
			obs_source_remove(obs_scene_get_source(scene));
		}
		return nullptr;
	}
	position(item, {"presenter", {0, 0, 1, 1}, true, false}, width, height, options);
	OBSDataAutoRelease settings = obs_data_create();
	obs_data_set_double(settings, "saturation", -1.0);
	obs_data_set_double(settings, "brightness", -.12);
	OBSSourceAutoRelease colour = obs_source_create_private("color_filter", "Starting grey camera", settings);
	OBSDataAutoRelease maskSettings = obs_data_create();
	obs_data_set_string(maskSettings, "type", "mask_alpha_filter.effect");
	obs_data_set_string(maskSettings, "image_path", maskPath.toUtf8().constData());
	obs_data_set_bool(maskSettings, "stretch", true);
	OBSSourceAutoRelease mask = obs_source_create_private("mask_filter", "Clipped corner", maskSettings);
	if (!colour || !mask) {
		if (!temporary) {
			obs_source_remove(obs_scene_get_source(scene));
		}
		return nullptr;
	}
	obs_source_filter_add(obs_scene_get_source(scene), colour);
	obs_source_filter_add(obs_scene_get_source(scene), mask);
	return obs_scene_get_ref(scene);
}
bool writeMask(const QString &path)
{
	QImage image(512, 512, QImage::Format_ARGB32);
	image.fill(Qt::transparent);
	QPainter painter(&image);
	painter.setRenderHint(QPainter::Antialiasing);
	painter.setPen(Qt::NoPen);
	painter.setBrush(Qt::white);
	painter.drawPolygon(QPolygonF{{0, 0}, {440, 0}, {512, 72}, {512, 512}, {0, 512}});
	painter.end();
	return image.save(path, "PNG");
}
struct CheckedPage : QWizardPage {
	std::function<QString()> problem;
	QLabel *error = nullptr;
	bool isComplete() const override { return !problem || problem().isEmpty(); }
	bool validatePage() override
	{
		const QString text = problem ? problem() : QString();
		if (error) {
			error->setText(text);
		}
		return text.isEmpty();
	}
	void changed()
	{
		if (error) {
			error->setText(problem ? problem() : QString());
		}
		emit completeChanged();
	}
};
// Starting an inactive capture is asynchronous. Keep a balanced showing lease
// while reviewing it so its dimensions can become available before reframing.
struct PreviewCaptures {
	QMap<QString, OBSSource> sources;
	~PreviewCaptures() { clear(); }
	void clear()
	{
		for (const auto &source : sources) {
			obs_source_dec_showing(source);
		}
		sources.clear();
	}
	void keepOnly(const QSet<QString> &ids)
	{
		for (auto it = sources.begin(); it != sources.end();) {
			if (!ids.contains(it.key())) {
				obs_source_dec_showing(it.value());
				it = sources.erase(it);
			} else {
				++it;
			}
		}
	}
	void add(obs_source_t *source)
	{
		const QString id = obs_source_get_uuid(source);
		if (!sources.contains(id)) {
			sources.insert(id, OBSSource(source));
			obs_source_inc_showing(source);
		}
	}
};
// Native preview uses private scenes. Browser overlays are deliberately replaced
// by placeholders so previewing cannot consume alerts or play widget audio.
class BuilderPreview : public QWidget {
	OBSDisplay display;
	OBSSource source;
	std::mutex mutex;
	bool showing = false;
	void createDisplay()
	{
		if (display || !windowHandle() || !windowHandle()->isExposed()) {
			return;
		}
		gs_init_data info{};
		info.cx = qMax(1, int(width() * devicePixelRatioF()));
		info.cy = qMax(1, int(height() * devicePixelRatioF()));
		info.format = GS_BGRA;
		info.zsformat = GS_ZS_NONE;
#ifdef _WIN32
		info.window.hwnd = reinterpret_cast<HWND>(winId());
#else
		return;
#endif
		display = obs_display_create(&info, 0xFF111827);
		if (display) {
			obs_display_add_draw_callback(display, draw, this);
		}
	}
	static void draw(void *opaque, uint32_t width, uint32_t height)
	{
		auto *self = static_cast<BuilderPreview *>(opaque);
		std::lock_guard lock(self->mutex);
		vec4 clear;
		vec4_set(&clear, .04f, .06f, .09f, 1);
		gs_clear(GS_CLEAR_COLOR, &clear, 0, 0);
		if (!self->source) {
			return;
		}
		const uint32_t sw = obs_source_get_width(self->source), sh = obs_source_get_height(self->source);
		if (!sw || !sh) {
			return;
		}
		const double scale = qMin(double(width) / sw, double(height) / sh);
		const int w = int(sw * scale), h = int(sh * scale);
		gs_viewport_push();
		gs_projection_push();
		gs_set_viewport((int(width) - w) / 2, (int(height) - h) / 2, w, h);
		gs_ortho(0, float(sw), 0, float(sh), -100, 100);
		obs_source_video_render(self->source);
		gs_projection_pop();
		gs_viewport_pop();
	}

protected:
	QPaintEngine *paintEngine() const override { return nullptr; }
	void paintEvent(QPaintEvent *) override { createDisplay(); }
	void showEvent(QShowEvent *event) override
	{
		QWidget::showEvent(event);
		createDisplay();
		std::lock_guard lock(mutex);
		if (source && !showing) {
			obs_source_inc_showing(source);
			showing = true;
		}
	}
	void hideEvent(QHideEvent *event) override
	{
		std::lock_guard lock(mutex);
		if (source && showing) {
			obs_source_dec_showing(source);
		}
		showing = false;
		QWidget::hideEvent(event);
	}
	void resizeEvent(QResizeEvent *event) override
	{
		QWidget::resizeEvent(event);
		if (display) {
			obs_display_resize(display, qMax(1, int(width() * devicePixelRatioF())),
					   qMax(1, int(height() * devicePixelRatioF())));
		}
	}

public:
	explicit BuilderPreview(QWidget *parent) : QWidget(parent)
	{
		setAttribute(Qt::WA_PaintOnScreen);
		setAttribute(Qt::WA_NoSystemBackground);
		setAttribute(Qt::WA_NativeWindow);
		setAttribute(Qt::WA_DontCreateNativeAncestors);
		setMinimumSize(200, 240);
		connect(windowHandle(), &QWindow::visibleChanged, this, [this](bool visible) {
			if (visible) {
				QTimer::singleShot(0, this, [this] { createDisplay(); });
			}
		});
	}
	~BuilderPreview() override
	{
		display = nullptr;
		if (source && showing) {
			obs_source_dec_showing(source);
		}
	}
	void setScene(obs_scene_t *scene)
	{
		std::lock_guard lock(mutex);
		if (source && showing) {
			obs_source_dec_showing(source);
		}
		source = scene ? obs_scene_get_source(scene) : nullptr;
		showing = source && isVisible();
		if (showing) {
			obs_source_inc_showing(source);
		}
	}
};
} // namespace

void PulseMotionEngine::openShowWizard()
{
	if (!mayLeaveDraft()) {
		return;
	}
	QWizard wizard(editor);
	wizard.setWindowTitle("Build my show · Pulse Weaver");
	wizard.setWizardStyle(QWizard::ModernStyle);
	wizard.setOption(QWizard::NoBackButtonOnStartPage);
	wizard.resize(1040, 800);
	QMap<QString, QCheckBox *> themes, looks;
	QMap<QString, QComboBox *> sources;
	QMap<QString, QWidget *> sourceRows, themeGroups, lookGroups;
	QMap<QString, QJsonObject> newSources, options;
	QMap<QString, QSet<QString>> overlayVisibility;
	QTemporaryDir previewFiles;
	const QString maskPath = previewFiles.filePath("corner.png");
	writeMask(maskPath);
	auto page = [&](const QString &title, const QString &subtitle) {
		auto *value = new CheckedPage;
		value->setTitle(title);
		value->setSubTitle(subtitle);
		auto *outer = new QVBoxLayout(value);
		auto *scroll = new QScrollArea(value);
		scroll->setWidgetResizable(true);
		auto *body = new QWidget(scroll);
		body->setLayout(new QVBoxLayout);
		scroll->setWidget(body);
		outer->addWidget(scroll);
		value->error = new QLabel(value);
		value->error->setWordWrap(true);
		value->error->setStyleSheet("color:#ffb8a8");
		outer->addWidget(value->error);
		wizard.addPage(value);
		return std::pair{value, static_cast<QVBoxLayout *>(body->layout())};
	};
	auto [choosePage, chooseLayout] =
		page("Choose your stages", "Each selected theme becomes one Stage. Next, choose the Looks inside it.");
	auto *name = new QLineEdit("My Show", choosePage);
	name->setMaxLength(90);
	chooseLayout->addWidget(new QLabel("Show name"));
	chooseLayout->addWidget(name);
	auto *reference = new QPushButton("Copy an existing show's compositions…", choosePage);
	chooseLayout->addWidget(reference);
	bool copyReference = false;
	connect(reference, &QPushButton::clicked, &wizard, [&] {
		copyReference = true;
		wizard.reject();
	});
	for (const auto &theme : PulseShow::catalogue()) {
		auto *check = new QCheckBox(theme.name, choosePage);
		check->setChecked(theme.id == "viewer");
		themes.insert(theme.id, check);
		chooseLayout->addWidget(check);
	}
	chooseLayout->addStretch();
	choosePage->problem = [&] {
		if (name->text().trimmed().isEmpty()) {
			return QString("Enter a show name.");
		}
		for (auto *check : themes) {
			if (check->isChecked()) {
				return QString();
			}
		}
		return QString("Choose at least one Stage theme.");
	};
	connect(name, &QLineEdit::textChanged, choosePage, [choosePage] { choosePage->changed(); });
	auto [lookPage, lookLayout] = page(
		"Choose Looks for each Stage",
		"Three different views are selected for each Stage. Keep them to animate between them, or untick views you do not need.");
	auto *lookTabs = new QTabWidget(lookPage);
	lookLayout->addWidget(lookTabs);
	for (const auto &theme : PulseShow::catalogue()) {
		auto *group = new QGroupBox(lookTabs);
		auto *layout = new QHBoxLayout(group);
		lookGroups.insert(theme.id, group);
		lookTabs->addTab(group, theme.name);
		for (const auto &look : theme.looks) {
			const QString key = theme.id + "/" + look.id;
			auto *card = new QGroupBox(group);
			auto *cardLayout = new QVBoxLayout(card);
			layout->addWidget(card, 1);
			auto *check = new QCheckBox(look.name, card);
			check->setChecked(true);
			looks.insert(key, check);
			cardLayout->addWidget(check);
			auto *description = new QLabel(look.description, card);
			description->setWordWrap(true);
			cardLayout->addWidget(description);
			auto *pictures = new QLabel(card);
			QPixmap image(250, 160);
			image.fill(QColor("#0b1220"));
			QPainter painter(&image);
			for (bool vertical : {false, true}) {
				const QRectF canvas = vertical ? QRectF(204, 24, 38, 68) : QRectF(8, 24, 180, 101);
				for (const auto &panel : PulseShow::layout(look, vertical, !look.support.isEmpty())) {
					QRectF r(canvas.x() + panel.rect.x() * canvas.width(),
						 canvas.y() + panel.rect.y() * canvas.height(),
						 panel.rect.width() * canvas.width(),
						 panel.rect.height() * canvas.height());
					painter.fillRect(r, panel.role == "presenter" ? QColor("#66479c")
										      : QColor("#176680"));
					painter.setPen(QColor("#52daef"));
					painter.drawRect(r);
					if (!vertical && r.width() > 40) {
						painter.setPen(Qt::white);
						painter.drawText(r, Qt::AlignCenter,
								 panel.role == "presenter" ? "Camera" : panel.role);
					}
				}
				painter.setPen(Qt::white);
				painter.drawText(QPointF(canvas.x(), 16), vertical ? "9:16" : "16:9");
			}
			painter.end();
			pictures->setPixmap(image);
			cardLayout->addWidget(pictures);
			cardLayout->addStretch();
		}
	}
	lookLayout->addWidget(new QLabel(
		"Camera = purple · content = blue. Switching Looks moves shared sources; switching Stages uses its transition.",
		lookPage));
	lookLayout->addStretch();
	lookPage->problem = [&] {
		for (const auto &theme : PulseShow::catalogue()) {
			if (themes.value(theme.id)->isChecked()) {
				bool any = false;
				for (const auto &look : theme.looks) {
					any |= looks.value(theme.id + "/" + look.id)->isChecked();
				}
				if (!any) {
					return "Choose at least one Look for " + theme.name + ".";
				}
			}
		}
		return QString();
	};
	auto [sourcePage, sourceLayout] = page(
		"Assign sources to each Stage",
		"Select an existing source or create a new one. The Stage's Looks share these assignments; other Stages can use different sources.");
	auto *reuseSources = new QCheckBox("Reuse choices for empty assignments in other Stages", sourcePage);
	reuseSources->setChecked(true);
	sourceLayout->addWidget(reuseSources);
	auto *sourceTabs = new QTabWidget(sourcePage);
	sourceLayout->addWidget(sourceTabs);
	auto sourceControl = [&](QFormLayout *form, QWidget *parent, const QString &key, const QString &role,
				 const QString &label) {
		auto *row = new QWidget(parent);
		auto *layout = new QHBoxLayout(row);
		layout->setContentsMargins(0, 0, 0, 0);
		auto *combo = new QComboBox(row);
		combo->setProperty("builderRole", role);
		combo->setAccessibleName(label);
		combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
		combo->setMinimumContentsLength(18);
		combo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
		combo->addItem("Choose source / none", "");
		struct Scan {
			QComboBox *combo;
			QString role;
		} scan{combo, role};
		obs_enum_sources(
			[](void *opaque, obs_source_t *source) {
				auto &scan = *static_cast<Scan *>(opaque);
				if (compatible(source, scan.role)) {
					scan.combo->addItem(obs_source_get_name(source), obs_source_get_uuid(source));
				}
				return true;
			},
			&scan);
		// Also offer drafted compatible sources created earlier in this wizard.
		for (auto it = newSources.cbegin(); it != newSources.cend(); ++it) {
			if (it.value().value("role").toString() == role) {
				combo->addItem("Reuse new: " + it.value().value("name").toString(), "new:" + it.key());
			}
		}
		auto *create = new QPushButton("Create new…", row);
		layout->addWidget(combo, 1);
		layout->addWidget(create);
		form->addRow(label, row);
		sources.insert(key, combo);
		sourceRows.insert(key, row);
		if (role == "graphic") {
			const QString theme = key.section('/', 0, 0);
			const QString title = theme == "starting"       ? "Starting soon"
					      : theme == "intermission" ? "Back soon"
									: "Thanks for watching";
			const QString sourceName = name->text().trimmed() + " · " + theme + " · Title";
			newSources.insert(key, {{"role", role},
						{"name", sourceName},
						{"kind", "builtin_graphic"},
						{"text", title},
						{"minutes", 5},
						{"autoName", true}});
			combo->addItem("New: " + sourceName, "new:" + key);
			combo->setCurrentIndex(combo->count() - 1);
		}
		connect(create, &QPushButton::clicked, &wizard, [&, key, role, label, combo] {
			QDialog dialog(&wizard);
			dialog.setWindowTitle("Create " + label);
			auto *outer = new QVBoxLayout(&dialog);
			auto *form = new QFormLayout;
			outer->addLayout(form);
			auto *sourceName = new QLineEdit(
				name->text().trimmed() + " · " + key.section('/', 0, 0) + " · " + label, &dialog);
			form->addRow("Source name", sourceName);
			auto *kind = new QComboBox(&dialog);
			if (role == "screen") {
				kind->addItem("Display capture", "monitor_capture");
				kind->addItem("Window capture", "window_capture");
			} else if (role == "game") {
				kind->addItem("Game capture", "game_capture");
				kind->addItem("Window capture", "window_capture");
			} else if (role == "presenter" || role == "activity" || role == "detail") {
				kind->addItem("Camera", "dshow_input");
				kind->addItem("Window capture", "window_capture");
			} else {
				if (role == "graphic") {
					kind->addItem("Built-in title / countdown", "builtin_graphic");
				}
				kind->addItem("Browser overlay URL", "browser_source");
			}
			form->addRow("Source type", kind);
			auto *target = new QComboBox(&dialog);
			form->addRow("Device / window", target);
			auto *url = new QLineEdit(&dialog);
			url->setPlaceholderText("https://…");
			form->addRow("URL", url);
			auto *text = new QLineEdit(key.startsWith("starting")       ? "Starting soon"
						   : key.startsWith("intermission") ? "Be right back"
										    : "Thanks for watching",
						   &dialog);
			form->addRow("Title", text);
			auto *minutes = new QSpinBox(&dialog);
			minutes->setRange(1, 120);
			minutes->setValue(5);
			form->addRow("Countdown minutes", minutes);
			auto refresh = [&] {
				const QString type = kind->currentData().toString();
				target->clear();
				const bool capture = type != "browser_source" && type != "builtin_graphic";
				target->setVisible(capture);
				form->labelForField(target)->setVisible(capture);
				url->setVisible(type == "browser_source");
				form->labelForField(url)->setVisible(type == "browser_source");
				text->setVisible(type == "builtin_graphic");
				form->labelForField(text)->setVisible(type == "builtin_graphic");
				minutes->setVisible(type == "builtin_graphic");
				form->labelForField(minutes)->setVisible(type == "builtin_graphic");
				if (!capture) {
					return;
				}
				if (type == "game_capture") {
					target->addItem("Any fullscreen game", "");
				}
				obs_properties_t *properties = obs_get_source_properties(type.toUtf8().constData());
				if (!properties) {
					return;
				}
				const char *field = type == "dshow_input"       ? "video_device_id"
						    : type == "monitor_capture" ? "monitor_id"
										: "window";
				obs_property_t *list = obs_properties_get(properties, field);
				const bool oldMonitor = type == "monitor_capture" && !list;
				if (oldMonitor) {
					list = obs_properties_get(properties, "monitor");
				}
				if (list) {
					for (size_t i = 0; i < obs_property_list_item_count(list); ++i) {
						if (!obs_property_list_item_disabled(list, i)) {
							const QString id =
								oldMonitor
									? QString::number(
										  obs_property_list_item_int(list, i))
									: QString::fromUtf8(
										  obs_property_list_item_string(list,
														i));
							if (!id.isEmpty()) {
								target->addItem(obs_property_list_item_name(list, i),
										id);
							}
						}
					}
				}
				obs_properties_destroy(properties);
			};
			connect(kind, QOverload<int>::of(&QComboBox::currentIndexChanged), &dialog,
				[&](int) { refresh(); });
			refresh();
			auto *error = new QLabel(&dialog);
			error->setWordWrap(true);
			outer->addWidget(error);
			auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
			outer->addWidget(buttons);
			connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
			connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
				const QString n = sourceName->text().trimmed(), type = kind->currentData().toString();
				OBSSourceAutoRelease existing = obs_get_source_by_name(n.toUtf8().constData());
				if (n.isEmpty() || n.size() > 120 || existing) {
					error->setText("Choose a unique source name (1–120 characters).");
					return;
				}
				for (auto it = newSources.cbegin(); it != newSources.cend(); ++it) {
					if (it.key() != key && it.value().value("name").toString() == n) {
						error->setText("Another drafted source uses this name.");
						return;
					}
				}
				const QUrl u(url->text().trimmed());
				if (type == "browser_source" && (!u.isValid() || u.host().isEmpty() ||
								 (u.scheme() != "https" && u.scheme() != "http"))) {
					error->setText("Enter a complete https:// or http:// URL.");
					return;
				}
				if (type != "browser_source" && type != "builtin_graphic" && type != "game_capture" &&
				    target->currentData().toString().isEmpty()) {
					error->setText("Choose an available device or window.");
					return;
				}
				newSources.insert(key, {{"role", role},
							{"name", n},
							{"kind", type},
							{"target", target->currentData().toString()},
							{"url", u.toString()},
							{"text", text->text()},
							{"minutes", minutes->value()}});
				for (auto it = sources.begin(); it != sources.end(); ++it) {
					if (it.value()->property("builderRole").toString() == role ||
					    (role.startsWith("overlay") && it.key().startsWith("overlay/"))) {
						const int old = it.value()->findData("new:" + key);
						if (old >= 0) {
							it.value()->setItemText(old, "New: " + n);
						} else {
							it.value()->addItem("New: " + n, "new:" + key);
						}
					}
				}
				combo->setCurrentIndex(combo->findData("new:" + key));
				dialog.accept();
				sourcePage->changed();
			});
			dialog.exec();
		});
		connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged), sourcePage,
			[sourcePage](int) { sourcePage->changed(); });
		return combo;
	};
	for (const auto &theme : PulseShow::catalogue()) {
		auto *group = new QGroupBox(sourceTabs);
		auto *form = new QFormLayout(group);
		themeGroups.insert(theme.id, group);
		sourceTabs->addTab(group, theme.name);
		QStringList roles;
		for (const auto &look : theme.looks) {
			for (const auto &role : {look.main, look.support}) {
				if (!role.isEmpty() && !roles.contains(role)) {
					roles << role;
				}
			}
		}
		for (const auto &role : roles) {
			sourceControl(form, group, theme.id + "/" + role, role, PulseShow::roleName(role));
		}
		if (roles.contains("chat")) {
			sourceControl(form, group, theme.id + "/chatPortrait", "chat",
				      "Portrait chat (optional override)");
		}
	}
	connect(name, &QLineEdit::textChanged, &wizard, [&](const QString &value) {
		for (auto it = newSources.begin(); it != newSources.end(); ++it) {
			if (!it.value().value("autoName").toBool()) {
				continue;
			}
			const QString sourceName = value.trimmed() + " · " + it.key().section('/', 0, 0) + " · Title";
			it.value().insert("name", sourceName);
			for (auto *combo : sources) {
				const int index = combo->findData("new:" + it.key());
				if (index >= 0) {
					combo->setItemText(index, "New: " + sourceName);
				}
			}
		}
	});
	bool reusingSources = false;
	for (auto it = sources.begin(); it != sources.end(); ++it) {
		auto *choice = it.value();
		connect(choice, QOverload<int>::of(&QComboBox::currentIndexChanged), sourcePage, [&, choice](int) {
			if (reusingSources || !reuseSources->isChecked() ||
			    choice->currentData().toString().isEmpty() ||
			    PulseShow::graphic(choice->property("builderRole").toString())) {
				return;
			}
			reusingSources = true;
			for (auto *other : sources) {
				if (other == choice || !other->currentData().toString().isEmpty() ||
				    other->property("builderRole") != choice->property("builderRole")) {
					continue;
				}
				const int index = other->findData(choice->currentData());
				if (index >= 0) {
					other->setCurrentIndex(index);
				}
			}
			reusingSources = false;
		});
	}
	auto *sourceNote = new QLabel(
		"Existing source settings are shared. Framing is independent. Audio devices and routing stay as configured.",
		sourcePage);
	sourceNote->setWordWrap(true);
	sourceLayout->addWidget(sourceNote);
	auto selectedLookKeys = [&] {
		QStringList result;
		for (const auto &theme : PulseShow::catalogue()) {
			if (themes.value(theme.id)->isChecked()) {
				for (const auto &look : theme.looks) {
					if (looks.value(theme.id + "/" + look.id)->isChecked()) {
						result << theme.id + "/" + look.id;
					}
				}
			}
		}
		return result;
	};
	sourcePage->problem = [&] {
		for (const auto &theme : PulseShow::catalogue()) {
			if (themes.value(theme.id)->isChecked()) {
				for (const auto &look : theme.looks) {
					if (looks.value(theme.id + "/" + look.id)->isChecked()) {
						for (const auto &role :
						     {look.main, look.optionalPresenter ? QString() : look.support}) {
							if (!role.isEmpty()) {
								const QString id = sources.value(theme.id + "/" + role)
											   ->currentData()
											   .toString();
								if (id.isEmpty()) {
									return "Choose " + PulseShow::roleName(role) +
									       " for " + theme.name + " → " +
									       look.name + ".";
								}
								if (!id.startsWith("new:")) {
									OBSSourceAutoRelease source =
										obs_get_source_by_uuid(
											id.toUtf8().constData());
									if (!compatible(source, role)) {
										return "The selected " +
										       PulseShow::roleName(role) +
										       " is unavailable. Choose it again.";
									}
								}
							}
						}
					}
				}
			}
		}
		return QString();
	};
	auto [overlayPage, overlayLayout] = page(
		"Add overlays",
		"Up to three full-canvas layers for each canvas, above the cameras and content. Empty slots are fine.");
	for (const QString &route : {QString("horizontal"), QString("vertical")}) {
		auto *group = new QGroupBox(route == "horizontal" ? "Landscape" : "Portrait", overlayPage);
		auto *form = new QFormLayout(group);
		overlayLayout->addWidget(group);
		for (int i = 1; i <= 3; ++i) {
			const QString key = "overlay/" + route + "/" + QString::number(i);
			auto *combo = sourceControl(form, group, key, "overlay" + QString::number(i),
						    "Overlay " + QString::number(i));
			auto *visibility = new QPushButton("Choose Looks…", group);
			form->addRow("Visible in", visibility);
			connect(visibility, &QPushButton::clicked, &wizard, [&, key] {
				QDialog dialog(&wizard);
				dialog.setWindowTitle("Overlay visibility · " + key);
				auto *layout = new QVBoxLayout(&dialog);
				QMap<QString, QCheckBox *> checks;
				for (const QString &id : selectedLookKeys()) {
					auto *check = new QCheckBox(looks.value(id)->text(), &dialog);
					check->setChecked(!overlayVisibility.contains(key) ||
							  overlayVisibility.value(key).contains(id));
					checks.insert(id, check);
					layout->addWidget(check);
				}
				auto *buttons =
					new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
				layout->addWidget(buttons);
				connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
				connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
				if (dialog.exec() == QDialog::Accepted) {
					QSet<QString> ids;
					for (auto it = checks.begin(); it != checks.end(); ++it) {
						if (it.value()->isChecked()) {
							ids.insert(it.key());
						}
					}
					overlayVisibility.insert(key, ids);
				}
			});
			Q_UNUSED(combo);
		}
	}
	auto *overlayNote = new QLabel(
		"Layer 1 is lowest; layer 3 is highest. Use Lumia's Chatty / Chatty vert in layer 3: full canvas, on top, visible in every Look. Existing overlay dimensions and audio are preserved. New browser overlays use the selected canvas size and start with audio excluded.",
		overlayPage);
	overlayNote->setWordWrap(true);
	overlayLayout->addWidget(overlayNote);
	// Optional local defaults are stored outside the shipped catalogue.
	QFile localDefaults(QDir(QFileInfo(storagePath).absolutePath()).filePath("show-builder-defaults.json"));
	QJsonObject defaults;
	if (localDefaults.open(QIODevice::ReadOnly)) {
		defaults = QJsonDocument::fromJson(localDefaults.readAll()).object();
	}
	localDefaults.close();
	const QJsonObject overlayDefaults = defaults.value("overlays").toObject();
	for (auto it = overlayDefaults.begin(); it != overlayDefaults.end(); ++it) {
		if (sources.contains(it.key())) {
			const QJsonObject spec = it.value().toObject();
			if (spec.contains("uuid")) {
				const int index = sources.value(it.key())->findData(spec.value("uuid").toString());
				if (index >= 0) {
					sources.value(it.key())->setCurrentIndex(index);
				}
			} else if (spec.contains("url")) {
				OBSSourceAutoRelease existing =
					obs_get_source_by_name(spec.value("name").toString().toUtf8().constData());
				OBSDataAutoRelease settings = existing ? obs_source_get_settings(existing) : nullptr;
				const QString existingId =
					existing &&
							QString(obs_source_get_unversioned_id(existing)) ==
								"browser_source" &&
							QString(obs_data_get_string(settings, "url")) ==
								spec.value("url").toString()
						? QString(obs_source_get_uuid(existing))
						: QString();
				if (!existingId.isEmpty()) {
					const int index = sources.value(it.key())->findData(existingId);
					if (index >= 0) {
						sources.value(it.key())->setCurrentIndex(index);
					}
				} else {
					QJsonObject draft = spec;
					draft.insert("kind", "browser_source");
					draft.insert("role", "overlay" + it.key().section('/', -1));
					newSources.insert(it.key(), draft);
					sources.value(it.key())->addItem("New: " + spec.value("name").toString(),
									 "new:" + it.key());
					sources.value(it.key())->setCurrentIndex(sources.value(it.key())->count() - 1);
				}
			}
			if (spec.contains("excludeLooks")) {
				QSet<QString> visible;
				for (const auto &theme : PulseShow::catalogue()) {
					for (const auto &look : theme.looks) {
						const QString id = theme.id + "/" + look.id;
						if (!spec.value("excludeLooks").toArray().contains(id)) {
							visible.insert(id);
						}
					}
				}
				overlayVisibility.insert(it.key(), visible);
			}
		}
	}
	const QJsonObject sourceDefaults = defaults.value("sources").toObject();
	for (auto it = sourceDefaults.begin(); it != sourceDefaults.end(); ++it) {
		if (sources.contains(it.key())) {
			const int index = sources.value(it.key())->findData(it.value().toString());
			if (index >= 0) {
				sources.value(it.key())->setCurrentIndex(index);
			}
		}
	}
	auto [reviewPage, reviewLayout] = page(
		"Review both canvases",
		"Choose each Look to check its framing. Browser overlays and uncreated sources are shown as placeholders; they are not activated here.");
	auto *reviewLook = new QComboBox(reviewPage);
	// Native OBS displays must not live inside QScrollArea: their child windows
	// can paint beyond the viewport during scroll. Only the controls scroll.
	auto *fixedReview = new QWidget(reviewPage);
	auto *fixedLayout = new QVBoxLayout(fixedReview);
	fixedLayout->setContentsMargins(0, 0, 0, 0);
	static_cast<QVBoxLayout *>(reviewPage->layout())->insertWidget(0, fixedReview);
	fixedLayout->addWidget(reviewLook);
	auto *summary = new QLabel(reviewPage);
	summary->setWordWrap(true);
	fixedLayout->addWidget(summary);
	auto *previews = new QWidget(reviewPage);
	auto *previewLayout = new QHBoxLayout(previews);
	auto *landscape = new BuilderPreview(previews);
	auto *portrait = new BuilderPreview(previews);
	previewLayout->addWidget(landscape, 3);
	previewLayout->addWidget(portrait, 1);
	fixedLayout->addWidget(previews);
	auto *demoRow = new QHBoxLayout;
	demoRow->addWidget(new QLabel("Move from", fixedReview));
	auto *motionFrom = new QComboBox(fixedReview);
	demoRow->addWidget(motionFrom, 1);
	auto *playMovement = new QPushButton("▶ Play movement", fixedReview);
	demoRow->addWidget(playMovement);
	fixedLayout->addLayout(demoRow);
	auto *motionProgress = new QSlider(Qt::Horizontal, fixedReview);
	motionProgress->setRange(0, 1000);
	motionProgress->setValue(1000);
	motionProgress->setToolTip("Scrub source movement between Looks. These are private preview scenes.");
	fixedLayout->addWidget(motionProgress);
	auto *movementHelp =
		new QLabel("Choose a different starting Look, then Play. Live output is unchanged.", fixedReview);
	movementHelp->setWordWrap(true);
	fixedLayout->addWidget(movementHelp);
	auto *route = new QComboBox(reviewPage);
	route->addItem("Adjust landscape", "horizontal");
	route->addItem("Adjust portrait", "vertical");
	reviewLayout->addWidget(route);
	auto *form = new QFormLayout;
	reviewLayout->addLayout(form);
	auto *divider = new QSpinBox(reviewPage);
	divider->setRange(15, 65);
	divider->setSuffix("% presenter strip");
	form->addRow("Portrait divider", divider);
	auto *corner = new QComboBox(reviewPage);
	for (const QString &c :
	     {QString("top-left"), QString("top-right"), QString("bottom-left"), QString("bottom-right")}) {
		corner->addItem(c, c);
	}
	form->addRow("Landscape inset corner", corner);
	auto *framingRole = new QComboBox(reviewPage);
	form->addRow("Frame source", framingRole);
	auto *panelRow = new QWidget(reviewPage);
	auto *panelLayout = new QHBoxLayout(panelRow);
	panelLayout->setContentsMargins(0, 0, 0, 0);
	QList<QSpinBox *> panelFields;
	for (const QString &label : {QString("Left"), QString("Top"), QString("Width"), QString("Height")}) {
		panelLayout->addWidget(new QLabel(label, panelRow));
		auto *field = new QSpinBox(panelRow);
		field->setRange(label == "Left" || label == "Top" ? 0 : 1, 100);
		field->setSuffix("%");
		field->setAccessibleName("Panel " + label);
		panelLayout->addWidget(field);
		panelFields.append(field);
	}
	auto *resetPanel = new QPushButton("Reset", panelRow);
	panelLayout->addWidget(resetPanel);
	form->addRow("Panel placement", panelRow);
	auto *fill = new QCheckBox("Crop to fill panel (off: fit full image)", reviewPage);
	form->addRow(fill);
	auto *focusX = new QSpinBox(reviewPage);
	focusX->setRange(0, 100);
	auto *focusY = new QSpinBox(reviewPage);
	focusY->setRange(0, 100);
	form->addRow("Crop position X (%)", focusX);
	form->addRow("Crop position Y (%)", focusY);
	auto *movement = new QSpinBox(reviewPage);
	movement->setRange(0, 3000);
	movement->setSingleStep(50);
	movement->setSuffix(" ms");
	form->addRow("Movement on both canvases", movement);
	auto *transition = new QComboBox(reviewPage);
	transition->addItem("Fade", "fade");
	transition->addItem("Cut", "cut");
	form->addRow("Between Stages", transition);
	auto draft = [&] {
		QJsonArray stagePlans;
		for (const auto &theme : PulseShow::catalogue()) {
			if (themes.value(theme.id)->isChecked()) {
				QJsonArray selected;
				QJsonObject roleIds, framing;
				for (const auto &look : theme.looks) {
					if (looks.value(theme.id + "/" + look.id)->isChecked()) {
						selected.append(look.id);
						for (const QString &r : {QString("horizontal"), QString("vertical")}) {
							framing.insert(
								look.id + "/" + r,
								options.value(theme.id + "/" + look.id + "/" + r));
						}
					}
				}
				for (const auto &role : PulseShow::roles(theme, selected)) {
					roleIds.insert(role,
						       sources.value(theme.id + "/" + role)->currentData().toString());
				}
				if (roleIds.contains("chat") && sources.contains(theme.id + "/chatPortrait")) {
					roleIds.insert(
						"chatPortrait",
						sources.value(theme.id + "/chatPortrait")->currentData().toString());
				}
				stagePlans.append(QJsonObject{{"theme", theme.id},
							      {"looks", selected},
							      {"roles", roleIds},
							      {"framing", framing}});
			}
		}
		QJsonArray overlays;
		for (auto it = sources.begin(); it != sources.end(); ++it) {
			if (it.key().startsWith("overlay/") && !it.value()->currentData().toString().isEmpty()) {
				QJsonArray visible;
				for (const QString &id : selectedLookKeys()) {
					if (!overlayVisibility.contains(it.key()) ||
					    overlayVisibility.value(it.key()).contains(id)) {
						visible.append(id);
					}
				}
				overlays.append(QJsonObject{{"key", it.key()},
							    {"canvas", it.key().section('/', 1, 1)},
							    {"order", it.key().section('/', 2, 2).toInt()},
							    {"source", it.value()->currentData().toString()},
							    {"looks", visible}});
			}
		}
		QJsonObject definitions;
		for (auto it = newSources.begin(); it != newSources.end(); ++it) {
			definitions.insert(it.key(), it.value());
		}
		return QJsonObject{{"schema", 2},          {"name", name->text().trimmed()},
				   {"stages", stagePlans}, {"newSources", definitions},
				   {"overlays", overlays}, {"transition", transition->currentData().toString()}};
	};
	bool loading = false;
	PreviewCaptures previewCaptures;
	QSet<QString> pendingCaptures;
	Execution rehearsal;
	QTimer rehearsalTimer;
	QElapsedTimer rehearsalClock;
	int previousProgress = 1000;
	auto resetRehearsal = [&] {
		rehearsal.orderCommitted = rehearsal.graphicCommitted = false;
		for (auto &pair : rehearsal.coveragePairs) {
			pair.orderCommitted = false;
		}
		for (const auto &track : rehearsal.tracks) {
			apply(track.item, track.from, true);
		}
	};
	auto showProgress = [&](int value) {
		if (value < previousProgress) {
			resetRehearsal();
		}
		applyMovementFrame(rehearsal, value / 1000.);
		previousProgress = value;
	};
	auto render = [&] {
		rehearsalTimer.stop();
		rehearsal.tracks.clear();
		rehearsal.coveragePairs.clear();
		rehearsal.orderCommitted = rehearsal.graphicCommitted = false;
		const QString key = reviewLook->currentData().toString();
		const auto *theme = PulseShow::theme(key.section('/', 0, 0));
		if (!theme) {
			return;
		}
		const auto *look = PulseShow::look(*theme, key.section('/', 1, 1));
		const QString fromKey = motionFrom->currentData().toString();
		const auto *fromLook = PulseShow::look(*theme, fromKey.section('/', 1, 1));
		if (!look) {
			return;
		}
		if (!fromLook) {
			fromLook = look;
		}
		QStringList warnings;
		QSet<QString> usedCaptures;
		pendingCaptures.clear();
		obs_video_info horizontalInfo{}, verticalInfo{};
		obs_get_video_info(&horizontalInfo);
		OBSCanvasAutoRelease verticalCanvas = obs_get_canvas_by_name("Pulse Weaver Vertical");
		if (verticalCanvas) {
			obs_canvas_get_video_info(verticalCanvas, &verticalInfo);
		}
		for (bool vertical : {false, true}) {
			const QString r = vertical ? "vertical" : "horizontal";
			const QJsonObject settings = options.value(key + "/" + r),
					  fromSettings = options.value(fromKey + "/" + r);
			const auto &info = vertical ? verticalInfo : horizontalInfo;
			const int width = info.base_width ? int(info.base_width) : (vertical ? 1080 : 1920);
			const int height = info.base_height ? int(info.base_height) : (vertical ? 1920 : 1080);
			OBSSceneAutoRelease scene = obs_scene_create_private("Builder preview");
			sizeScene(scene, width, height);
			auto panelsFor = [&](const PulseShow::Look &value, const QJsonObject &framing) {
				const bool supporting = !value.support.isEmpty() &&
							!sources.value(theme->id + "/" + value.support)
								 ->currentData()
								 .toString()
								 .isEmpty();
				return PulseShow::layout(value, vertical, supporting, framing);
			};
			const auto targetPanels = panelsFor(*look, settings),
				   fromPanels = panelsFor(*fromLook, fromSettings);
			QMap<QString, PulseShow::Panel> targetByRole, fromByRole;
			QMap<QString, int> targetOrder, fromOrder;
			QStringList roles;
			int order = 0;
			for (const auto &panel : targetPanels) {
				targetByRole.insert(panel.role, panel);
				targetOrder.insert(panel.role, order++);
				roles << panel.role;
			}
			order = 0;
			for (const auto &panel : fromPanels) {
				fromByRole.insert(panel.role, panel);
				fromOrder.insert(panel.role, order++);
				if (!roles.contains(panel.role)) {
					roles << panel.role;
				}
			}
			for (const QString &role : roles) {
				const PulseShow::Panel panel = targetByRole.contains(role) ? targetByRole.value(role)
											   : fromByRole.value(role);
				QString id = sources.value(theme->id + "/" + role)->currentData().toString();
				if (vertical && role == "chat" &&
				    !sources.value(theme->id + "/chatPortrait")->currentData().toString().isEmpty()) {
					id = sources.value(theme->id + "/chatPortrait")->currentData().toString();
				}
				OBSSourceAutoRelease live = id.startsWith("new:")
								    ? nullptr
								    : obs_get_source_by_uuid(id.toUtf8().constData());
				const bool placeholder = !live || PulseShow::graphic(role) ||
							 QString(obs_source_get_unversioned_id(live)) ==
								 "browser_source";
				OBSSourceAutoRelease source;
				if (placeholder) {
					OBSDataAutoRelease config = obs_data_create();
					obs_data_set_int(config, "width", width);
					obs_data_set_int(config, "height", height);
					obs_data_set_int(config, "color",
							 role == "presenter" ? 0xff78565c
							 : role == "chat"    ? 0xff785330
									     : 0xff524122);
					source = obs_source_create_private("color_source_v3", role.toUtf8().constData(),
									   config);
					warnings << PulseShow::roleName(role) + " uses a placeholder";
				} else {
					usedCaptures.insert(id);
					previewCaptures.add(live);
					if (!obs_source_get_width(live) || !obs_source_get_height(live)) {
						pendingCaptures.insert(id);
						warnings << PulseShow::roleName(role) +
								    " is starting; check that its device is available";
					}
					source = obs_source_get_ref(live);
				}
				if (!source) {
					continue;
				}
				OBSSceneAutoRelease treatment;
				if (panel.treated) {
					treatment = treatedCamera(source, "Preview camera treatment", true,
								  qMax(1, int(panel.rect.width() * width)),
								  qMax(1, int(panel.rect.height() * height)), maskPath,
								  settings);
					if (treatment) {
						source = obs_source_get_ref(obs_scene_get_source(treatment));
					}
				}
				if (auto *item = obs_scene_add(scene, source)) {
					Track track;
					track.container = r;
					track.item = item;
					track.graphic = PulseShow::graphic(role);
					position(item, targetByRole.contains(role) ? targetByRole.value(role) : panel,
						 width, height, settings);
					track.target = capture(item);
					track.target.visible = targetByRole.contains(role);
					track.target.order = targetOrder.value(role);
					position(item, fromByRole.contains(role) ? fromByRole.value(role) : panel,
						 width, height, fromSettings);
					track.from = track.baseline = capture(item);
					track.from.visible = fromByRole.contains(role);
					track.from.order = fromOrder.value(role);
					rehearsal.tracks.push_back(track);
				}
			}
			(vertical ? portrait : landscape)->setScene(scene);
		}
		prepareCoveragePairs(rehearsal);
		showProgress(1000);
		{
			const QSignalBlocker blocker(motionProgress);
			motionProgress->setValue(1000);
		}
		previewCaptures.keepOnly(usedCaptures);
		warnings.removeDuplicates();
		summary->setText(name->text().trimmed() + " · " +
				 QString::number(draft().value("stages").toArray().size()) + " Stages · " +
				 QString::number(selectedLookKeys().size()) + " Looks\n" + warnings.join("; "));
	};
	auto loadPanelFields = [&] {
		const QString key = reviewLook->currentData().toString(), role = framingRole->currentData().toString();
		const auto *theme = PulseShow::theme(key.section('/', 0, 0));
		const auto *look = theme ? PulseShow::look(*theme, key.section('/', 1, 1)) : nullptr;
		if (!look) {
			return;
		}
		const auto panels = PulseShow::layout(*look, route->currentData().toString() == "vertical", true,
						      options.value(key + "/" + route->currentData().toString()));
		for (const auto &panel : panels) {
			if (panel.role == role) {
				const double values[]{panel.rect.x(), panel.rect.y(), panel.rect.width(),
						      panel.rect.height()};
				for (int i = 0; i < 4; ++i) {
					const QSignalBlocker blocker(panelFields[i]);
					panelFields[i]->setValue(int(std::round(values[i] * 100)));
				}
			}
		}
	};
	auto loadControls = [&] {
		loading = true;
		const QString key = reviewLook->currentData().toString();
		const auto *theme = PulseShow::theme(key.section('/', 0, 0));
		const auto *look = theme ? PulseShow::look(*theme, key.section('/', 1, 1)) : nullptr;
		const QJsonObject settings = options.value(key + "/" + route->currentData().toString());
		divider->setValue(int(settings.value("divider").toDouble(look ? look->divider : .30) * 100));
		divider->setEnabled(route->currentData().toString() == "vertical" && look && !look->support.isEmpty() &&
				    look->layout != "footer" && look->layout != "label");
		corner->setCurrentIndex(
			qMax(0, corner->findData(settings.value("corner").toString(look ? look->corner : "top-left"))));
		corner->setEnabled(route->currentData().toString() == "horizontal" && look &&
				   (look->layout == "corner" || look->layout == "reaction"));
		movement->setValue(options.value(key + "/horizontal").value("durationMs").toInt(850));
		const QString previousFrom = motionFrom->currentData().toString();
		motionFrom->clear();
		for (const QString &selected : selectedLookKeys()) {
			if (selected == key || selected.section('/', 0, 0) != theme->id) {
				continue;
			}
			motionFrom->addItem(PulseShow::look(*theme, selected.section('/', 1, 1))->name, selected);
		}
		motionFrom->setCurrentIndex(qMax(0, motionFrom->findData(previousFrom)));
		playMovement->setEnabled(motionFrom->count() > 0);
		framingRole->clear();
		if (look) {
			for (const QString &role : {look->main, look->support}) {
				if (!role.isEmpty()) {
					framingRole->addItem(PulseShow::roleName(role), role);
				}
			}
		}
		const QString role = framingRole->currentData().toString();
		fill->setChecked(settings.value(role + "Fill")
					 .toBool(role == "presenter" || role == "activity" || role == "detail"));
		focusX->setValue(int(settings.value(role + "X").toDouble(.5) * 100));
		focusY->setValue(int(settings.value(role + "Y").toDouble(.5) * 100));
		loadPanelFields();
		loading = false;
		render();
	};
	auto storeControls = [&] {
		if (loading) {
			return;
		}
		const QString key = reviewLook->currentData().toString() + "/" + route->currentData().toString();
		QJsonObject settings = options.value(key);
		const QString role = framingRole->currentData().toString();
		settings.insert("divider", divider->value() / 100.);
		settings.insert("corner", corner->currentData().toString());
		settings.insert("durationMs", movement->value());
		settings.insert(role + "Fill", fill->isChecked());
		settings.insert(role + "X", focusX->value() / 100.);
		settings.insert(role + "Y", focusY->value() / 100.);
		options.insert(key, settings);
		for (const QString &canvas : {QString("horizontal"), QString("vertical")}) {
			const QString durationKey = reviewLook->currentData().toString() + "/" + canvas;
			QJsonObject shared = options.value(durationKey);
			shared.insert("durationMs", movement->value());
			options.insert(durationKey, shared);
		}
		loadPanelFields();
		render();
	};
	connect(reviewLook, QOverload<int>::of(&QComboBox::currentIndexChanged), &wizard, [&](int) { loadControls(); });
	connect(route, QOverload<int>::of(&QComboBox::currentIndexChanged), &wizard, [&](int) { loadControls(); });
	connect(framingRole, QOverload<int>::of(&QComboBox::currentIndexChanged), &wizard, [&](int) {
		if (loading) {
			return;
		}
		loading = true;
		const QJsonObject settings =
			options.value(reviewLook->currentData().toString() + "/" + route->currentData().toString());
		const QString role = framingRole->currentData().toString();
		fill->setChecked(settings.value(role + "Fill")
					 .toBool(role == "presenter" || role == "activity" || role == "detail"));
		focusX->setValue(int(settings.value(role + "X").toDouble(.5) * 100));
		focusY->setValue(int(settings.value(role + "Y").toDouble(.5) * 100));
		loadPanelFields();
		loading = false;
	});
	for (auto *spin : {divider, focusX, focusY, movement}) {
		connect(spin, QOverload<int>::of(&QSpinBox::valueChanged), &wizard, [&](int) { storeControls(); });
	}
	connect(corner, QOverload<int>::of(&QComboBox::currentIndexChanged), &wizard, [&](int) { storeControls(); });
	connect(fill, &QCheckBox::toggled, &wizard, [&](bool) { storeControls(); });
	for (auto *field : panelFields) {
		connect(field, QOverload<int>::of(&QSpinBox::valueChanged), &wizard, [&](int) {
			if (loading) {
				return;
			}
			const QString key =
				reviewLook->currentData().toString() + "/" + route->currentData().toString();
			QJsonArray rect;
			for (auto *value : panelFields) {
				rect.append(value->value() / 100.);
			}
			QJsonObject settings = options.value(key);
			settings.insert(framingRole->currentData().toString() + "Rect", rect);
			options.insert(key, settings);
			loadPanelFields();
			render();
		});
	}
	connect(resetPanel, &QPushButton::clicked, &wizard, [&] {
		const QString key = reviewLook->currentData().toString() + "/" + route->currentData().toString();
		QJsonObject settings = options.value(key);
		settings.remove(framingRole->currentData().toString() + "Rect");
		options.insert(key, settings);
		loadPanelFields();
		render();
	});
	connect(motionFrom, QOverload<int>::of(&QComboBox::currentIndexChanged), &wizard, [&](int) {
		if (!loading) {
			render();
		}
	});
	connect(motionProgress, &QSlider::valueChanged, &wizard, [&](int value) { showProgress(value); });
	rehearsalTimer.setTimerType(Qt::PreciseTimer);
	rehearsalTimer.setInterval(16);
	connect(playMovement, &QPushButton::clicked, &wizard, [&] {
		if (rehearsal.tracks.empty()) {
			return;
		}
		resetRehearsal();
		previousProgress = 0;
		motionProgress->setValue(0);
		showProgress(0);
		movementHelp->setText("Playing " + motionFrom->currentText() + " → " + reviewLook->currentText() +
				      ". Live output is unchanged.");
		rehearsalClock.restart();
		rehearsalTimer.start();
	});
	connect(&rehearsalTimer, &QTimer::timeout, &wizard, [&] {
		const int progress =
			int(qMin<qint64>(1000, rehearsalClock.elapsed() * 1000 / qMax(1, movement->value())));
		motionProgress->setValue(progress);
		if (progress == 1000) {
			rehearsalTimer.stop();
			movementHelp->setText(
				"Movement ended. Select another Look to compare, or Finish to create the show.");
		}
	});
	auto update = [&] {
		int tab = 0;
		for (const auto &theme : PulseShow::catalogue()) {
			const bool enabled = themes.value(theme.id)->isChecked();
			lookTabs->setTabVisible(tab, enabled);
			sourceTabs->setTabVisible(tab++, enabled);
			QJsonArray chosen;
			for (const auto &look : theme.looks) {
				if (looks.value(theme.id + "/" + look.id)->isChecked()) {
					chosen.append(look.id);
				}
			}
			QStringList required = PulseShow::roles(theme, chosen);
			if (required.contains("chat")) {
				required << "chatPortrait";
			}
			for (auto it = sourceRows.begin(); it != sourceRows.end(); ++it) {
				if (it.key().startsWith(theme.id + "/")) {
					it.value()->setVisible(required.contains(it.key().section('/', 1)));
					auto *form = qobject_cast<QFormLayout *>(themeGroups.value(theme.id)->layout());
					if (form && form->labelForField(it.value())) {
						form->labelForField(it.value())
							->setVisible(required.contains(it.key().section('/', 1)));
					}
				}
			}
		}
		choosePage->changed();
		lookPage->changed();
		sourcePage->changed();
	};
	for (auto *check : themes) {
		connect(check, &QCheckBox::toggled, &wizard, [&](bool) { update(); });
	}
	for (auto *check : looks) {
		connect(check, &QCheckBox::toggled, &wizard, [&](bool) { update(); });
	}
	reviewPage->problem = [&] {
		QString error = choosePage->problem();
		if (error.isEmpty()) {
			error = lookPage->problem();
		}
		if (error.isEmpty()) {
			error = sourcePage->problem();
		}
		if (error.isEmpty() && outputsBusy()) {
			error = "Stop streaming and recording before creating the show. Your choices are preserved.";
		}
		return error;
	};
	connect(&wizard, &QWizard::currentIdChanged, &wizard, [&](int) {
		update();
		if (wizard.currentPage() == reviewPage) {
			loading = true;
			const QString selected = reviewLook->currentData().toString();
			reviewLook->clear();
			for (const QString &key : selectedLookKeys()) {
				reviewLook->addItem(PulseShow::theme(key.section('/', 0, 0))->name + " · " +
							    PulseShow::look(*PulseShow::theme(key.section('/', 0, 0)),
									    key.section('/', 1, 1))
								    ->name,
						    key);
			}
			reviewLook->setCurrentIndex(qMax(0, reviewLook->findData(selected)));
			loading = false;
			loadControls();
			reviewPage->changed();
		} else {
			rehearsalTimer.stop();
			rehearsal.tracks.clear();
			rehearsal.coveragePairs.clear();
			landscape->setScene(nullptr);
			portrait->setScene(nullptr);
			previewCaptures.clear();
			pendingCaptures.clear();
		}
	});
	QTimer status;
	status.setInterval(1000);
	connect(&status, &QTimer::timeout, reviewPage, [&] {
		if (wizard.currentPage() == reviewPage) {
			reviewPage->changed();
			bool ready = false;
			for (const QString &id : pendingCaptures) {
				const auto source = previewCaptures.sources.value(id);
				if (source && obs_source_get_width(source) && obs_source_get_height(source)) {
					ready = true;
					break;
				}
			}
			if (ready) {
				render();
			}
		}
	});
	status.start();
	update();
	while (wizard.exec() == QDialog::Accepted) {
		rehearsalTimer.stop();
		rehearsal.tracks.clear();
		rehearsal.coveragePairs.clear();
		landscape->setScene(nullptr);
		portrait->setScene(nullptr);
		previewCaptures.clear();
		auto *finish = wizard.button(QWizard::FinishButton);
		if (finish) {
			finish->setEnabled(false);
		}
		const QJsonObject result = createGuidedShow(draft());
		if (result.value("ok").toBool()) {
			setStatus(result.value("message").toString());
			return;
		}
		QMessageBox::warning(&wizard, "Show not created",
				     result.value("message").toString() +
					     "\nYour choices are preserved. Correct the problem and try again.");
		reviewPage->changed();
		loadControls();
	}
	landscape->setScene(nullptr);
	portrait->setScene(nullptr);
	rehearsalTimer.stop();
	rehearsal.tracks.clear();
	if (copyReference) {
		openReferenceWizard();
	}
}

QJsonObject PulseMotionEngine::createThemedShow(const QJsonObject &choices)
{
	auto error = [](const QString &message) {
		return QJsonObject{{"ok", false}, {"message", message}};
	};
	if (outputsBusy()) {
		return error("Stop all streaming and recording outputs before creating a show.");
	}
	const QString prefix = choices.value("name").toString().trimmed();
	if (prefix.isEmpty() || prefix.size() > 90) {
		return error("Enter a show name of 1–90 characters.");
	}
	const QJsonArray requested = choices.value("stages").toArray(), overlays = choices.value("overlays").toArray();
	const QJsonObject definitions = choices.value("newSources").toObject();
	const QString transition = choices.value("transition").toString("fade");
	if (transition != "fade" && transition != "cut") {
		return error("Choose Fade or Cut between Stages.");
	}
	if (requested.isEmpty()) {
		return error("Choose at least one Stage.");
	}
	OBSCanvasAutoRelease verticalCanvas = obs_get_canvas_by_name("Pulse Weaver Vertical");
	obs_video_info horizontalInfo{}, verticalInfo{};
	if (!verticalCanvas || !obs_get_video_info(&horizontalInfo) ||
	    !obs_canvas_get_video_info(verticalCanvas, &verticalInfo) || !horizontalInfo.base_width ||
	    !horizontalInfo.base_height || !verticalInfo.base_width || !verticalInfo.base_height) {
		return error("Both video canvases must be ready. Open Show and wait for its portrait preview.");
	}
	const QString stagePath =
		QDir::cleanPath(QDir(QFileInfo(storagePath).absolutePath()).filePath("../../pulseweaver-stages.json"));
	QFile stageFile(stagePath);
	QJsonArray oldStages;
	if (stageFile.exists()) {
		if (!stageFile.open(QIODevice::ReadOnly)) {
			return error("Could not read the Stage catalogue.");
		}
		const auto document = QJsonDocument::fromJson(stageFile.readAll());
		stageFile.close();
		if (!document.isArray()) {
			return error("The Stage catalogue is invalid. Repair it before creating a show.");
		}
		oldStages = document.array();
	}
	QSet<QString> selectedThemes, selectedLooks, usedDefinitions, reservedNames;
	auto reserve = [&](const QString &name) {
		if (name.isEmpty() || reservedNames.contains(name)) {
			return false;
		}
		OBSSourceAutoRelease existing = obs_get_source_by_name(name.toUtf8().constData());
		OBSSceneAutoRelease portrait = obs_canvas_get_scene_by_name(verticalCanvas, name.toUtf8().constData());
		if (existing || portrait) {
			return false;
		}
		reservedNames.insert(name);
		return true;
	};
	QString validationError;
	auto validateSource = [&](const QString &id, const QString &role, bool required) {
		if (id.isEmpty()) {
			if (required) {
				validationError = "Choose " + PulseShow::roleName(role) + " for each selected Look.";
			}
			return !required;
		}
		if (id.startsWith("new:")) {
			const QString key = id.mid(4);
			const QJsonObject definition = definitions.value(key).toObject();
			const QString name = definition.value("name").toString().trimmed();
			const QString kind = definition.value("kind").toString();
			if (name.isEmpty() || name.size() > 120) {
				validationError = "Complete the new source name for " + role + ".";
				return false;
			}
			const QSet<QString> allowed{"dshow_input",  "monitor_capture", "window_capture",
						    "game_capture", "browser_source",  "builtin_graphic"};
			if (!allowed.contains(kind) ||
			    ((role == "presenter" || role == "activity" || role == "detail") && kind != "dshow_input" &&
			     kind != "window_capture") ||
			    (role == "screen" && kind != "monitor_capture" && kind != "window_capture") ||
			    (role == "game" && kind != "game_capture" && kind != "window_capture")) {
				validationError = "Incompatible new source type for " + PulseShow::roleName(role) + ".";
				return false;
			}
			if (kind == "browser_source") {
				const QUrl url(definition.value("url").toString());
				if (!url.isValid() || url.host().isEmpty() ||
				    (url.scheme() != "http" && url.scheme() != "https")) {
					validationError = "Enter a complete URL for " + name + ".";
					return false;
				}
			}
			if (kind != "browser_source" && kind != "builtin_graphic" && kind != "game_capture" &&
			    definition.value("target").toString().isEmpty()) {
				validationError = "Choose a device or window for " + name + ".";
				return false;
			}
			if (!usedDefinitions.contains(key) && !reserve(name)) {
				validationError = "The source name “" + name + "” is already in use.";
				return false;
			}
			usedDefinitions.insert(key);
			return true;
		}
		OBSSourceAutoRelease source = obs_get_source_by_uuid(id.toUtf8().constData());
		if (!compatible(source, role)) {
			validationError = "The selected " + PulseShow::roleName(role) +
					  " is missing or incompatible. Choose it again.";
			return false;
		}
		return true;
	};
	for (const auto &value : requested) {
		const QJsonObject stage = value.toObject();
		const QString id = stage.value("theme").toString();
		const auto *theme = PulseShow::theme(id);
		if (!theme || selectedThemes.contains(id)) {
			return error("Unknown or duplicate Stage theme: " + id);
		}
		selectedThemes.insert(id);
		const QString name = prefix + " · " + theme->name;
		for (const auto &existing : oldStages) {
			if (existing.toObject().value("name").toString() == name) {
				return error("A Stage named “" + name + "” already exists. Choose another show name.");
			}
		}
		if (!reserve(name) || !reserve(name + " · Portrait")) {
			return error("A scene named “" + name + "” already exists. Choose another show name.");
		}
		const QJsonArray chosen = stage.value("looks").toArray();
		if (chosen.isEmpty()) {
			return error("Choose at least one Look for " + theme->name + ".");
		}
		const QJsonObject roles = stage.value("roles").toObject();
		for (const auto &lookId : chosen) {
			const auto *look = PulseShow::look(*theme, lookId.toString());
			const QString key = id + "/" + lookId.toString();
			if (!look || selectedLooks.contains(key)) {
				return error("Unknown or duplicate Look: " + key);
			}
			selectedLooks.insert(key);
			if (!validateSource(roles.value(look->main).toString(), look->main, true) ||
			    (!look->support.isEmpty() && !validateSource(roles.value(look->support).toString(),
									 look->support, !look->optionalPresenter))) {
				return error(theme->name + " → " + look->name + ": " + validationError);
			}
		}
	}
	QSet<QString> overlaySlots;
	for (const auto &value : overlays) {
		const QJsonObject overlay = value.toObject();
		const QString route = overlay.value("canvas").toString();
		const int slot = overlay.value("order").toInt();
		const QString key = route + QString::number(slot);
		if ((route != "horizontal" && route != "vertical") || slot < 1 || slot > 3 ||
		    overlaySlots.contains(key)) {
			return error("Choose up to three distinct overlay slots per canvas.");
		}
		overlaySlots.insert(key);
		for (const auto &id : overlay.value("looks").toArray()) {
			if (!selectedLooks.contains(id.toString())) {
				return error("An overlay refers to an unselected Look.");
			}
		}
		if (!validateSource(overlay.value("source").toString(), "overlay", true)) {
			return error(validationError);
		}
	}
	for (const auto &value : requested) {
		const QJsonObject roleIds = value.toObject().value("roles").toObject();
		if (!validateSource(roleIds.value("chatPortrait").toString(), "chat", false)) {
			return error(validationError);
		}
	}
	// Recheck all reserved generated names after sources have been validated.
	for (const auto &value : requested) {
		const QJsonObject stage = value.toObject();
		const auto *theme = PulseShow::theme(stage.value("theme").toString());
		for (const auto &id : stage.value("looks").toArray()) {
			const auto *look = PulseShow::look(*theme, id.toString());
			if (look->layout == "starting") {
				for (const QString &route : {QString("horizontal"), QString("vertical")}) {
					if (!reserve(prefix + " · " + theme->name + " · " + look->name + " · " + route +
						     " camera")) {
						return error(
							"A Starting camera treatment name already exists. Choose another show name.");
					}
				}
			}
		}
	}
	const QJsonArray oldActions = actions;
	QJsonArray stages = oldStages, newActions;
	std::vector<OBSSource> created;
	QStringList createdFiles;
	auto fail = [&](const QString &message) {
		actions = oldActions;
		for (auto it = created.rbegin(); it != created.rend(); ++it) {
			if (*it) {
				obs_source_remove(*it);
			}
		}
		created.clear();
		for (const QString &file : createdFiles) {
			QFile::remove(file);
		}
		return error(message);
	};
	const QString assetDirectory = QDir(QFileInfo(storagePath).absolutePath()).filePath("show-graphics");
	if (!QDir().mkpath(assetDirectory)) {
		return error("Could not create the show graphics directory.");
	}
	const QString maskPath = QDir(assetDirectory).filePath(freshId() + ".png");
	if (!writeMask(maskPath)) {
		return error("Could not save the clipped-corner mask.");
	}
	createdFiles << maskPath;
	QMap<QString, QString> resolved;
	auto createSource = [&](const QString &key, const QString &route, const QString &lookId) -> OBSSource {
		if (!key.startsWith("new:")) {
			OBSSourceAutoRelease source = obs_get_source_by_uuid(key.toUtf8().constData());
			return OBSSource(source);
		}
		const QJsonObject spec = definitions.value(key.mid(4)).toObject();
		const QString kind = spec.value("kind").toString();
		const bool graphic = kind == "builtin_graphic";
		const QString cacheKey =
			key + (graphic ? "/" + PulseShow::graphicVariant(lookId) + "/" + route : QString());
		if (resolved.contains(cacheKey)) {
			OBSSourceAutoRelease source =
				obs_get_source_by_uuid(resolved.value(cacheKey).toUtf8().constData());
			return OBSSource(source);
		}
		OBSDataAutoRelease settings = obs_data_create();
		const QString target = spec.value("target").toString();
		QString actual = kind, name = spec.value("name").toString();
		const bool vertical = route == "vertical";
		const int width = vertical ? verticalInfo.base_width : horizontalInfo.base_width,
			  height = vertical ? verticalInfo.base_height : horizontalInfo.base_height;
		if (kind == "dshow_input") {
			obs_data_set_string(settings, "video_device_id", target.toUtf8().constData());
		} else if (kind == "window_capture") {
			obs_data_set_string(settings, "window", target.toUtf8().constData());
		} else if (kind == "monitor_capture") {
			obs_properties_t *properties = obs_get_source_properties("monitor_capture");
			const bool modern = properties && obs_properties_get(properties, "monitor_id");
			if (properties) {
				obs_properties_destroy(properties);
			}
			if (modern) {
				obs_data_set_string(settings, "monitor_id", target.toUtf8().constData());
			} else {
				obs_data_set_int(settings, "monitor", target.toInt());
			}
		} else if (kind == "game_capture") {
			obs_data_set_string(settings, "capture_mode", target.isEmpty() ? "any_fullscreen" : "window");
			if (!target.isEmpty()) {
				obs_data_set_string(settings, "window", target.toUtf8().constData());
			}
		} else {
			actual = "browser_source";
			obs_data_set_int(settings, "width", width);
			obs_data_set_int(settings, "height", height);
			obs_data_set_string(settings, "css",
					    "body { background-color: rgba(0,0,0,0); margin: 0; overflow: hidden; }");
			obs_data_set_bool(settings, "shutdown", true);
			obs_data_set_bool(settings, "reroute_audio", true);
			if (!graphic) {
				obs_data_set_string(settings, "url", spec.value("url").toString().toUtf8().constData());
			} else {
				name += " · " + PulseShow::graphicVariant(lookId).replace('/', " · ") + " · " + route;
				OBSSourceAutoRelease existing = obs_get_source_by_name(name.toUtf8().constData());
				if (existing) {
					return {};
				}
				const QString file = QDir(assetDirectory).filePath(freshId() + ".html");
				const bool countdown = lookId.startsWith("starting/") ||
						       lookId == "intermission/return";
				const int seconds = qBound(1, spec.value("minutes").toInt(5), 120) * 60;
				const QString html =
					QString("<!doctype html><meta charset='utf-8'><style>html,body{margin:0;width:100%;height:100%;overflow:hidden;background:transparent;color:white;font-family:Arial,sans-serif}body{display:grid;place-content:center;text-align:center}h1{font-size:clamp(30px,6vw,110px);margin:.2em}#timer{font-size:clamp(42px,9vw,150px)}</style><h1>%1</h1><div id='timer'></div><script>let end=Date.now()+%2*1000;function tick(){let n=Math.max(0,Math.ceil((end-Date.now())/1000));document.getElementById('timer').textContent=%3?String(Math.floor(n/60)).padStart(2,'0')+':'+String(n%60).padStart(2,'0'):'';}tick();setInterval(tick,1000);</script>")
						.arg(spec.value("text").toString("Starting soon").toHtmlEscaped())
						.arg(seconds)
						.arg(countdown ? "true" : "false");
				QSaveFile out(file);
				const QByteArray bytes = html.toUtf8();
				if (!out.open(QIODevice::WriteOnly) || out.write(bytes) != bytes.size() ||
				    !out.commit()) {
					return {};
				}
				createdFiles << file;
				obs_data_set_bool(settings, "is_local_file", true);
				obs_data_set_string(settings, "local_file",
						    QDir::toNativeSeparators(file).toUtf8().constData());
			}
		}
		OBSSourceAutoRelease source =
			obs_source_create(actual.toUtf8().constData(), name.toUtf8().constData(), settings, nullptr);
		if (!source) {
			return {};
		}
		if (actual == "browser_source") {
			obs_source_set_audio_mixers(source, 0);
		}
		created.emplace_back(source);
		resolved.insert(cacheKey, obs_source_get_uuid(source));
		return OBSSource(source);
	};
	const QJsonObject previousAssignments =
		oldStages.isEmpty() ? QJsonObject{} : oldStages.first().toObject().value("assignments").toObject();
	char *collectionName = obs_frontend_get_current_scene_collection();
	const QString collection = QString::fromUtf8(collectionName ? collectionName : "");
	bfree(collectionName);
	for (const auto &value : requested) {
		const QJsonObject stage = value.toObject();
		const auto *theme = PulseShow::theme(stage.value("theme").toString());
		const QString name = prefix + " · " + theme->name, portraitName = name + " · Portrait";
		OBSSceneAutoRelease horizontal = obs_scene_create(name.toUtf8().constData());
		if (!horizontal) {
			return fail("Could not create " + name + ".");
		}
		created.emplace_back(obs_scene_get_source(horizontal));
		OBSSceneAutoRelease portrait =
			obs_canvas_scene_create(verticalCanvas, portraitName.toUtf8().constData());
		if (!portrait) {
			return fail("Could not create " + portraitName + ".");
		}
		created.emplace_back(obs_scene_get_source(portrait));
		OBSDataAutoRelease metadata = obs_source_get_private_settings(obs_scene_get_source(portrait));
		obs_data_set_string(metadata, "pulseweaver.horizontal_uuid",
				    obs_source_get_uuid(obs_scene_get_source(horizontal)));
		obs_data_set_bool(metadata, "pulseweaver.native_vertical", true);
		obs_data_set_bool(metadata, "pulseweaver.follow_horizontal", false);
		const QJsonObject roleIds = stage.value("roles").toObject(),
				  framing = stage.value("framing").toObject();
		const int firstLook = newActions.size();
		QJsonObject savedRoles;
		struct Item {
			QString token;
			OBSSceneItem item;
		};
		std::vector<Item> horizontalItems, verticalItems;
		QMap<QString, OBSSource> treatedSources;
		for (const auto &id : stage.value("looks").toArray()) {
			const auto *look = PulseShow::look(*theme, id.toString());
			const QString lookKey = theme->id + "/" + look->id;
			QJsonArray targets;
			for (bool vertical : {false, true}) {
				const QString route = vertical ? "vertical" : "horizontal",
					      container = vertical ? portraitName : name;
				obs_scene_t *scene = vertical ? portrait.Get() : horizontal.Get();
				auto &items = vertical ? verticalItems : horizontalItems;
				const int width = vertical ? verticalInfo.base_width : horizontalInfo.base_width,
					  height = vertical ? verticalInfo.base_height : horizontalInfo.base_height;
				const QJsonObject settings = framing.value(look->id + "/" + route).toObject();
				QSet<qint64> visible;
				const bool supporting = !look->support.isEmpty() &&
							!roleIds.value(look->support).toString().isEmpty();
				int order = 0;
				auto add = [&](OBSSource source, const QString &token, const PulseShow::Panel &panel,
					       bool overlay) -> bool {
					if (!source) {
						return false;
					}
					OBSSceneItem item;
					for (const auto &entry : items) {
						if (entry.token == token) {
							item = entry.item;
							break;
						}
					}
					if (!item) {
						item = obs_scene_add(scene, source);
						if (!item) {
							return false;
						}
						items.push_back({token, item});
					}
					position(item, panel, width, height, settings);
					obs_sceneitem_set_visible(item, true);
					obs_sceneitem_set_order_position(item, order++);
					obs_sceneitem_set_locked(item, overlay);
					visible.insert(obs_sceneitem_get_id(item));
					return true;
				};
				for (const auto &panel : PulseShow::layout(*look, vertical, supporting, settings)) {
					QString sourceId = roleIds.value(panel.role).toString();
					if (vertical && panel.role == "chat" &&
					    !roleIds.value("chatPortrait").toString().isEmpty()) {
						sourceId = roleIds.value("chatPortrait").toString();
					}
					OBSSource source = createSource(sourceId, route, lookKey);
					if (!source) {
						return fail("Could not create or resolve " +
							    PulseShow::roleName(panel.role) + " for " + theme->name +
							    ".");
					}
					QString token = panel.role;
					if (definitions.value(sourceId.mid(4)).toObject().value("kind").toString() ==
					    "builtin_graphic") {
						token += "/" + PulseShow::graphicVariant(lookKey);
					}
					savedRoles.insert(panel.role + "/" + route, obs_source_get_uuid(source));
					if (panel.treated) {
						const QString treatmentKey = panel.role + "/" + route;
						if (!treatedSources.contains(treatmentKey)) {
							const QString treatmentName = name + " · " +
										      PulseShow::roleName(panel.role) +
										      " · " + route + " camera";
							OBSSceneAutoRelease treatment = treatedCamera(
								source, treatmentName, false,
								qMax(1, int(panel.rect.width() * width)),
								qMax(1, int(panel.rect.height() * height)), maskPath,
								settings);
							if (!treatment) {
								return fail(
									"Could not create the dimmed clipped-corner camera. Check the colour and mask filters.");
							}
							created.emplace_back(obs_scene_get_source(treatment));
							treatedSources.insert(treatmentKey,
									      obs_scene_get_source(treatment));
						}
						source = treatedSources.value(treatmentKey);
						token += "/treated";
					}
					if (!add(source, token, panel, PulseShow::graphic(panel.role))) {
						return fail("Could not add a layer to " + name + ".");
					}
				}
				std::vector<QJsonObject> sortedOverlays;
				for (const auto &ov : overlays) {
					if (ov.toObject().value("canvas").toString() == route &&
					    ov.toObject().value("looks").toArray().contains(lookKey)) {
						sortedOverlays.push_back(ov.toObject());
					}
				}
				std::sort(sortedOverlays.begin(), sortedOverlays.end(),
					  [](const auto &a, const auto &b) {
						  return a.value("order").toInt() < b.value("order").toInt();
					  });
				for (const auto &overlay : sortedOverlays) {
					OBSSource source =
						createSource(overlay.value("source").toString(), route, lookKey);
					if (!add(source, "overlay/" + QString::number(overlay.value("order").toInt()),
						 {"overlay", {0, 0, 1, 1}, false, false}, true)) {
						return fail("Could not add an overlay to " + name + ".");
					}
				}
				for (const auto &entry : items) {
					const bool shown = visible.contains(obs_sceneitem_get_id(entry.item));
					obs_sceneitem_set_visible(entry.item, shown);
					Transform transform = capture(entry.item);
					transform.visible = shown;
					obs_source_t *source = obs_sceneitem_get_source(entry.item);
					targets.append(QJsonObject{
						{"container", container},
						{"itemId", QString::number(obs_sceneitem_get_id(entry.item))},
						{"source", obs_source_get_name(source)},
						{"sourceUuid", obs_source_get_uuid(source)},
						{"graphic", entry.token.startsWith("overlay/") ||
								    entry.token.startsWith("graphic") ||
								    entry.token == "chat"},
						{"sourceWidth", int(obs_source_get_width(source))},
						{"sourceHeight", int(obs_source_get_height(source))},
						{"transform", serialize(transform)}});
				}
			}
			newActions.append(QJsonObject{
				{"version", 1},
				{"id", freshId()},
				{"name", name + " · " + look->name},
				{"kind", "layout"},
				{"collection", collection},
				{"container", name},
				{"policy", "switch"},
				{"stage", name},
				{"activateScene", true},
				{"durationMs",
				 qBound(0,
					framing.value(look->id + "/horizontal").toObject().value("durationMs").toInt(850),
					3000)},
				{"restore", false},
				{"items", targets},
				{"templateId", lookKey}});
		}
		// Every Look must explicitly hide layers introduced by a later Look.
		for (int i = firstLook; i < newActions.size(); ++i) {
			QJsonObject action = newActions[i].toObject();
			QJsonArray targets = action.value("items").toArray();
			for (bool vertical : {false, true}) {
				for (const auto &entry : vertical ? verticalItems : horizontalItems) {
					const QString container = vertical ? portraitName : name;
					bool included = false;
					for (const auto &target : targets) {
						if (target.toObject().value("container").toString() == container &&
						    target.toObject().value("itemId").toString() ==
							    QString::number(obs_sceneitem_get_id(entry.item))) {
							included = true;
						}
					}
					if (!included) {
						Transform transform = capture(entry.item);
						transform.visible = false;
						obs_source_t *source = obs_sceneitem_get_source(entry.item);
						targets.append(QJsonObject{
							{"container", container},
							{"itemId", QString::number(obs_sceneitem_get_id(entry.item))},
							{"source", obs_source_get_name(source)},
							{"sourceUuid", obs_source_get_uuid(source)},
							{"graphic", entry.token.startsWith("overlay/") ||
									    entry.token.startsWith("graphic")},
							{"transform", serialize(transform)}});
					}
				}
			}
			action.insert("items", targets);
			newActions[i] = action;
		}
		for (const auto &value : newActions[firstLook].toObject().value("items").toArray()) {
			const auto target = value.toObject();
			obs_scene_t *scene = target.value("container").toString() == name ? horizontal.Get()
											  : portrait.Get();
			OBSSceneItem item = PulseRuntimeSafety::findSceneItemById(
				scene, target.value("itemId").toString().toLongLong());
			if (item) {
				apply(item, deserialize(target.value("transform").toObject()), true);
			}
		}
		QJsonObject assignments;
		for (const QString &provider :
		     {QString("twitch"), QString("youtube"), QString("kick"), QString("recording")}) {
			for (const QString &route : {QString("horizontal"), QString("vertical")}) {
				const QString key = provider + "_" + route;
				assignments.insert(key,
						   QJsonObject{{"canvas", route},
							       {"scene", route == "horizontal" ? name : portraitName},
							       {"excluded", previousAssignments.value(key)
										    .toObject()
										    .value("excluded")
										    .toArray()}});
			}
		}
		stages.append(QJsonObject{{"name", name},
					  {"horizontal", name},
					  {"vertical", portraitName},
					  {"horizontalTransition", transition},
					  {"horizontalDurationMs", 500},
					  {"verticalTransition", transition},
					  {"verticalDurationMs", 500},
					  {"assignments", assignments},
					  {"sourceRoles", savedRoles},
					  {"createdBy", "show-builder-v2"},
					  {"themeId", theme->id}});
	}
	if (outputsBusy()) {
		return fail("An output started during creation. Stop it and try again.");
	}
	for (const auto &action : newActions) {
		actions.append(action);
	}
	if (!save()) {
		return fail("Could not save the new Looks. The new show was rolled back.");
	}
	QSaveFile out(stagePath);
	const QByteArray bytes = QJsonDocument(stages).toJson(QJsonDocument::Indented);
	if (!out.open(QIODevice::WriteOnly) || out.write(bytes) != bytes.size() || !out.commit()) {
		out.cancelWriting();
		actions = oldActions;
		const bool restored = save();
		return fail(
			restored
				? "Could not save the Stage catalogue. New scenes and Looks were rolled back."
				: "Could not save the Stage catalogue or restore the Look store. Stop and repair storage before retrying.");
	}
	obs_frontend_save();
	editingId = newActions.first().toObject().value("id").toString();
	refreshEditor();
	loadActionIntoEditor(actionByIdentity(editingId));
	emitEvent("motion_catalogue_changed");
	return QJsonObject{{"ok", true},
			   {"message",
			    QString("Created %1 Stages and %2 selected Looks. Edit their framing in Control.")
				    .arg(requested.size())
				    .arg(newActions.size())},
			   {"stages", requested.size()},
			   {"looks", newActions.size()}};
}
