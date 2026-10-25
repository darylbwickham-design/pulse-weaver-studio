#pragma once

#include "pulse-setup-plan.hpp"
#include "pulse-setup-input.hpp"
#include "pulse-setup-stingers.hpp"
#include "../../shared/qt/PulseOutputActivity.hpp"
#include <QApplication>
#include <QEvent>
#include <QFile>
#include <QGridLayout>
#include <QJsonArray>
#include <QPainter>
#include <QSpinBox>
#include <QTabWidget>
#include <QWizard>
#include <QWizardPage>
#include <functional>

class PulseGuidedPage final : public QWizardPage {
public:
	std::function<QString()> problem;
	QLabel *notice = nullptr;
	bool isComplete() const override { return !problem || problem().isEmpty(); }
	bool validatePage() override { refresh(); return isComplete(); }
	void cleanupPage() override {}
	void refresh() {
		const QString issue = problem ? problem() : QString();
		if (notice) { notice->setText(issue); notice->setVisible(!issue.isEmpty()); }
		emit completeChanged();
	}
};

// The shared click-through picker leaves its model unchanged on Back. Observe
// acceptance so an already selected item is not mistaken for a new choice.
class PulseGuidedPickerOutcome final : public QObject {
	QWidget *owner_;
public:
	bool accepted = false;
	explicit PulseGuidedPickerOutcome(QWidget *owner) : QObject(owner), owner_(owner) {}
	bool eventFilter(QObject *object, QEvent *event) override {
		if (event->type() == QEvent::Show) {
			auto *dialog = qobject_cast<QDialog *>(object);
			if (dialog && dialog->parentWidget() == owner_ && dialog->objectName() == "PulseWeaverSetupChoiceDialog")
				QObject::connect(dialog, &QDialog::accepted, this, [this] { accepted = true; });
		}
		return false;
	}
};

// A fit-aware illustration. It shows the space a source actually occupies rather
// than painting an entire bounds rectangle and hiding potential letterboxing.
class PulseGuidedDiagram final : public QWidget {
	const PulseShow::Look *look_ = nullptr;
	QJsonObject framing_;
	QMap<QString, QString> names_;
	QMap<QString, QSize> sizes_;
public:
	explicit PulseGuidedDiagram(QWidget *parent = nullptr) : QWidget(parent) {
		setMinimumHeight(220); setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
		setAccessibleName("Landscape and portrait fit illustration");
	}
	QSize sizeHint() const override { return {800, 310}; }
	void setView(const PulseShow::Look *look, const QJsonObject &framing, const QMap<QString, QString> &names, const QMap<QString, QSize> &sizes) {
		look_ = look; framing_ = framing; names_ = names; sizes_ = sizes; update();
	}
protected:
	void paintEvent(QPaintEvent *) override {
		QPainter p(this); p.setRenderHint(QPainter::Antialiasing); p.fillRect(rect(), QColor("#0D1724"));
		if (!look_) return;
		const qreal h = qMax(80., qMin(height() - 36., (width() - 42.) / (16. / 9. + 9. / 16.)));
		const qreal total = h * (16. / 9. + 9. / 16.) + 22;
		const qreal left = (width() - total) / 2;
		const QRectF canvases[]{QRectF(left, 26, h * 16. / 9., h), QRectF(left + h * 16. / 9. + 22, 26, h * 9. / 16., h)};
		for (int format = 0; format < 2; ++format) {
			const auto canvas = canvases[format];
			QLinearGradient bg(canvas.topLeft(), canvas.bottomRight()); bg.setColorAt(0, QColor("#243B51")); bg.setColorAt(1, QColor("#101C2C"));
			p.fillRect(canvas, bg); p.setPen(QColor("#B8D3E6"));
			p.drawText(QRectF(canvas.left(), 0, canvas.width(), 22), Qt::AlignCenter, format ? "Portrait · 9:16" : "Landscape · 16:9");
			for (const auto &panel : PulseShow::layout(*look_, format == 1, true, framing_)) {
				QRectF bounds(canvas.x() + panel.rect.x() * canvas.width(), canvas.y() + panel.rect.y() * canvas.height(), panel.rect.width() * canvas.width(), panel.rect.height() * canvas.height());
				const QString route = format ? "vertical/" : "horizontal/";
				const QSize size = sizes_.value(route + panel.role, QSize(1920, 1080));
				const qreal ratio = qreal(qMax(1, size.width())) / qMax(1, size.height());
				QRectF picture = bounds;
				if (!panel.fill) {
					const QSizeF fitted = QSizeF(ratio, 1).scaled(bounds.size(), Qt::KeepAspectRatio);
					picture = QRectF(QPointF(bounds.center().x() - fitted.width() / 2, bounds.center().y() - fitted.height() / 2), fitted);
				}
				p.save(); p.setClipRect(bounds);
				p.fillRect(picture, panel.role == "feedB" ? QColor("#28566D") : panel.role == "feedC" ? QColor("#66517B") : QColor("#446679"));
				p.setPen(QPen(QColor("#77D9E6"), 1.5)); p.drawRect(picture.adjusted(1, 1, -1, -1));
				QFont label = font(); label.setPixelSize(qBound(9, int(picture.width() / 16), 14)); p.setFont(label); p.setPen(Qt::white);
				p.drawText(picture.adjusted(5, 3, -5, -3), Qt::AlignCenter | Qt::TextWordWrap, names_.value(panel.role, panel.role));
				p.restore();
			}
			p.setPen(QColor("#7294AD")); p.drawRect(canvas);
		}
	}
};

class PulseGuidedWizard final : public QWizard {
	using Options = PulseSetupPlan::Options;
	Options options_;
	QJsonArray savedStages_;
	QVector<PulseGuidedPage *> pages_;
	QVector<QVBoxLayout *> bodies_;
	QMap<QString, QLabel *> inputLabels_;
	QMap<QString, QPushButton *> inputEdit_, inputClear_;
	QVector<QLabel *> alertLabels_;
	QVector<QPushButton *> alertClear_;
	QMap<QString, QLabel *> artworkLabels_;
	QMap<QString, QPushButton *> artworkClear_;
	PulseSetupChoices::ChoiceGrid *viewsGrid_ = nullptr;
	QLabel *viewHint_ = nullptr, *reviewSummary_ = nullptr, *reviewInputs_ = nullptr, *reviewScreens_ = nullptr, *reviewAlerts_ = nullptr, *reviewViewName_ = nullptr, *reviewNotice_ = nullptr;
	PulseGuidedDiagram *reviewDiagram_ = nullptr;
	QPushButton *reviewPrevious_ = nullptr, *reviewNext_ = nullptr;
	QStringList reviewViews_;
	int reviewIndex_ = 0, nextDraft_ = 1;
	bool advanced_ = false, viewsTouched_ = false, visitedReview_ = false, updating_ = false;

	QString &input(const QString &role) {
		return role == "camera" ? options_.sources.camera : role == "content" ? options_.sources.content : options_.sources.detail;
	}
	QString input(const QString &role) const {
		return role == "camera" ? options_.sources.camera : role == "content" ? options_.sources.content : options_.sources.detail;
	}
	QString inputTitle(const QString &role) const {
		if (role == "camera") return "Your camera";
		if (role == "detail") return "Second angle or guest";
		return options_.activity == "gaming" ? "Your game" : options_.activity == "creative" ? "Work, screen or demonstration" : "Shared content or call window";
	}
	QString sourceName(const QString &id) const {
		if (id.isEmpty()) return "Not added";
		if (id.startsWith("new:")) return options_.newSources.value(id.mid(4)).toObject().value("name").toString("Missing new input");
		OBSSourceAutoRelease source = obs_get_source_by_uuid(id.toUtf8().constData());
		return source ? QString::fromUtf8(obs_source_get_name(source)) : "Input is no longer available";
	}
	QSize sourceSize(const QString &id, bool portrait) const {
		if (id.startsWith("new:")) {
			const auto spec = options_.newSources.value(id.mid(4)).toObject();
			if (spec.value("kind") == "image_source") { const QSize size = QImageReader(spec.value("target").toString()).size(); if (size.isValid()) return size; }
			if (spec.value("kind") == "browser_source") {
				obs_video_info info{};
				OBSCanvasAutoRelease canvas = portrait ? obs_get_canvas_by_name("Pulse Weaver Vertical") : nullptr;
				const bool known = portrait ? canvas && obs_canvas_get_video_info(canvas, &info) : obs_get_video_info(&info);
				if (known && info.base_width && info.base_height) return {int(info.base_width), int(info.base_height)};
				return portrait ? QSize(1080, 1920) : QSize(1920, 1080);
			}
		} else if (!id.isEmpty()) {
			OBSSourceAutoRelease source = obs_get_source_by_uuid(id.toUtf8().constData());
			if (source && obs_source_get_width(source) && obs_source_get_height(source)) return {int(obs_source_get_width(source)), int(obs_source_get_height(source))};
		}
		return {1920, 1080};
	}
	static bool presentationOnly(obs_source_t *source) {
		if (pulseIsNativeOverlaySource(source)) return true;
		OBSDataAutoRelease metadata = obs_source_get_private_settings(source);
		return obs_data_get_bool(metadata, "pulseweaver.setup_card");
	}
	QString sourceProblem(const QString &id, bool overlay = false, const QString &route = {}) const {
		if (id.isEmpty()) return {};
		if (id.startsWith("new:")) {
			const auto spec = options_.newSources.value(id.mid(4)).toObject();
			if (spec.isEmpty()) return "A new input is missing. Choose it again.";
			return PulseSetupInput::draftProblem(spec, options_.newSources, id.mid(4), overlay);
		}
		OBSSourceAutoRelease source = obs_get_source_by_uuid(id.toUtf8().constData());
		if (!source || !(obs_source_get_output_flags(source) & OBS_SOURCE_VIDEO) || obs_scene_from_source(source) || obs_group_from_source(source))
			return "“" + sourceName(id) + "” is missing or cannot provide a picture.";
		if (pulseIsNativeOverlaySource(source) || (!overlay && presentationOnly(source)))
			return "Choose a real camera or content input. Generated setup screens and native overlays are separate artwork.";
		if (overlay && (!pulseWizardOverlaySourceAllowed(source) || (!route.isEmpty() && !pulseBrowserMatchesCanvas(source, route == "vertical"))))
			return "Choose a matching browser overlay for this format.";
		return {};
	}
	QString inputsProblem() const {
		const auto &s = options_.sources;
		if ((options_.activity == "gaming" || options_.activity == "creative") && s.content.isEmpty())
			return "Choose " + inputTitle("content").toLower() + " to continue. A camera is optional.";
		if (s.camera.isEmpty() && s.content.isEmpty() && s.detail.isEmpty()) return "Add at least one camera, picture or content input to continue.";
		QSet<QString> seen;
		for (const QString &role : {QString("camera"), QString("content"), QString("detail")}) {
			const QString id = input(role); if (id.isEmpty()) continue;
			if (seen.contains(id)) return "Use each input once here. Views will reuse it automatically; a second angle needs a different input.";
			seen.insert(id); const QString issue = sourceProblem(id); if (!issue.isEmpty()) return inputTitle(role) + ": " + issue;
		}
		return {};
	}
	QString viewsProblem() const {
		if (options_.views.isEmpty()) return "Include at least one useful view.";
		QSet<QString> available; bool showsContent = false;
		for (const auto &view : PulseSetupPlan::availableViews(options_.sources)) {
			available.insert(view.id);
			if (options_.views.contains(view.id) && view.roles.contains("feedB")) showsContent = true;
		}
		for (const auto &id : options_.views) if (!available.contains(id)) return "An included view needs an input that is no longer assigned. Choose its input or another view.";
		if ((options_.activity == "gaming" || options_.activity == "creative") && !showsContent)
			return "Include a view that shows " + inputTitle("content").toLower() + ". You can keep camera-only views too.";
		return {};
	}
	bool screenEnabled(const QString &key) const {
		return key == "guided-starting" ? options_.starting : key == "guided-break" ? options_.brb : options_.ending;
	}
	QString screenMessage(const QString &key) const {
		return key == "guided-starting" ? options_.startingText : key == "guided-break" ? options_.breakText : options_.endingText;
	}
	bool screenNeedsMessage(const QString &key) const {
		const auto art = options_.artwork.value(key).toObject();
		return screenEnabled(key) && (art.value("horizontal").toString().isEmpty() || art.value("vertical").toString().isEmpty());
	}
	QString artworkSummary(const QString &key) const {
		const auto art = options_.artwork.value(key).toObject(); QStringList formats;
		for (const QString &route : {QString("horizontal"), QString("vertical")}) {
			const QString id = art.value(route).toString();
			formats << (route == "horizontal" ? "Landscape: " : "Portrait: ") +
				(id.isEmpty() ? "Built-in image · “" + screenMessage(key) + "”" : sourceName(id));
		}
		return formats.join("\n");
	}
	bool usesNewBrowser() const {
		QSet<QString> used;
		for (const auto &view : PulseSetupPlan::availableViews(options_.sources)) if (options_.views.contains(view.id))
			for (const auto &role : view.roles) used.insert(role == "feedA" ? options_.sources.camera : role == "feedB" ? options_.sources.content : options_.sources.detail);
		for (const auto &alert : options_.alerts) if (alert.enabled) { used.insert(alert.horizontal); used.insert(alert.vertical); }
		for (auto it = options_.artwork.begin(); it != options_.artwork.end(); ++it) if (screenEnabled(it.key())) {
			used.insert(it.value().toObject().value("horizontal").toString()); used.insert(it.value().toObject().value("vertical").toString());
		}
		for (const auto &id : used) if (id.startsWith("new:") && options_.newSources.value(id.mid(4)).toObject().value("kind") == "browser_source") return true;
		return false;
	}
	QString nameProblem() const {
		const QString name = options_.name.trimmed();
		if (name.isEmpty() || name.size() > 60) return "Give your show a name of 1–60 characters.";
		for (const QString &section : {QString("Main"), QString("Starting"), QString("Break"), QString("Ending")}) {
			const QString full = name + " · " + section;
			for (const auto &stage : savedStages_) if (stage.toObject().value("name") == full) return "This show name is already in use. Choose another name.";
			OBSSourceAutoRelease source = obs_get_source_by_name(full.toUtf8().constData()); if (source) return "A scene with this show name already exists. Choose another name.";
		}
		return {};
	}
	QString alertsProblem() const {
		for (int i = 0; i < options_.alerts.size(); ++i) {
			const auto &alert = options_.alerts[i]; if (!alert.enabled) continue;
			if (alert.horizontal.isEmpty() && alert.vertical.isEmpty()) return "Add a browser link to alert layer " + QString::number(i + 1) + ", or remove that layer.";
			for (const QString &route : {QString("horizontal"), QString("vertical")}) {
				const QString id = route == "horizontal" ? alert.horizontal : alert.vertical;
				const QString issue = sourceProblem(id, true, route); if (!issue.isEmpty()) return "Alert layer " + QString::number(i + 1) + ": " + issue;
			}
		}
		return {};
	}
	QString allProblem() const {
		if (PulseHasActiveOutputs()) return "Stop streaming, recording and other outputs before creating the show. Your answers stay here.";
		for (const auto &issue : {inputsProblem(), nameProblem(), alertsProblem(), PulseSetupPlan::problem(options_)}) if (!issue.isEmpty()) return issue;
		for (auto it = options_.artwork.begin(); it != options_.artwork.end(); ++it) {
			if ((it.key() == "guided-starting" && !options_.starting) || (it.key() == "guided-break" && !options_.brb) || (it.key() == "guided-ending" && !options_.ending)) continue;
			for (const QString &route : {QString("horizontal"), QString("vertical")}) {
				const QString issue = sourceProblem(it.value().toObject().value(route).toString(), true, route); if (!issue.isEmpty()) return "Custom screen artwork: " + issue;
			}
		}
		return {};
	}
	QVBoxLayout *addPage(const QString &phase, const QString &title, const QString &detail) {
		auto *page = new PulseGuidedPage; page->setObjectName("PulseGuided" + phase + "Page"); page->setAccessibleName(title);
		auto *outer = new QVBoxLayout(page); outer->setContentsMargins(14, 10, 14, 4); outer->setSpacing(10);
		const QStringList phases{"Activity", "Inputs", "Views", "Screens", "Alerts", "Review"};
		auto *steps = new QHBoxLayout; steps->setSpacing(7); outer->addLayout(steps);
		const int current = pages_.size();
		for (int i = 0; i < phases.size(); ++i) { auto *label = new QLabel(QString::number(i + 1) + "  " + phases[i], page); label->setWordWrap(true);
			label->setProperty("setupPhaseState", i == current ? "current" : i < current ? "complete" : "upcoming"); steps->addWidget(label, 1); }
		PulseSetupInput::text(outer, page, title, "PulseSetupTitle");
		PulseSetupInput::text(outer, page, detail, "PulseSetupSubtitle");
		auto *scroll = new QScrollArea(page); scroll->setObjectName("PulseSetupPageScroll"); scroll->setWidgetResizable(true); scroll->setFrameShape(QFrame::NoFrame);
		scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff); outer->addWidget(scroll, 1);
		auto *body = new QWidget(scroll); auto *layout = new QVBoxLayout(body); layout->setContentsMargins(0, 4, 8, 8); layout->setSpacing(12); layout->setAlignment(Qt::AlignTop); scroll->setWidget(body);
		page->notice = new QLabel(page); page->notice->setObjectName("PulseSetupNotice"); page->notice->setTextFormat(Qt::PlainText); page->notice->setWordWrap(true); page->notice->hide(); outer->addWidget(page->notice);
		pages_.append(page); bodies_.append(layout); QWizard::addPage(page); return layout;
	}
	QWidget *surface(QVBoxLayout *layout) {
		auto *panel = new QWidget(this); panel->setObjectName("PulseSetupSurface"); layout->addWidget(panel); return panel;
	}
	QComboBox *existingSources(QWidget *parent, const QString &current, bool overlay = false, const QString &route = {}, const QString &role = {}) {
		auto *model = new QComboBox(parent); model->hide(); model->addItem("Choose…", "");
		struct Data { PulseGuidedWizard *wizard; QComboBox *model; QString role, route; bool overlay; } data{this, model, role, route, overlay};
		obs_enum_sources([](void *opaque, obs_source_t *source) {
			auto &d = *static_cast<Data *>(opaque);
			if (!(obs_source_get_output_flags(source) & OBS_SOURCE_VIDEO) || obs_scene_from_source(source) || obs_group_from_source(source)) return true;
			if (pulseIsNativeOverlaySource(source) || (!d.overlay && presentationOnly(source))) return true;
			const QString id = obs_source_get_uuid(source);
			if (d.overlay && (!pulseWizardOverlaySourceAllowed(source) || !pulseBrowserMatchesCanvas(source, d.route == "vertical"))) return true;
			if (!d.overlay) for (const QString &r : {QString("camera"), QString("content"), QString("detail")}) if (r != d.role && d.wizard->input(r) == id) return true;
			d.model->addItem(QString::fromUtf8(obs_source_get_name(source)), id); return true;
		}, &data);
		for (auto it = options_.newSources.begin(); it != options_.newSources.end(); ++it) {
			const auto spec = it.value().toObject(); const QString id = "new:" + it.key();
			if (spec.value("kind") == "setup_card" || (overlay && spec.value("kind") != "browser_source")) continue;
			bool duplicate = false; if (!overlay) for (const QString &r : {QString("camera"), QString("content"), QString("detail")}) if (r != role && input(r) == id) duplicate = true;
			if (!duplicate) model->addItem(spec.value("name").toString() + " · new input", id);
		}
		model->setCurrentIndex(qMax(0, model->findData(current))); return model;
	}
	QString pickExisting(QWidget *owner, const QString &current, bool overlay, const QString &route = {}, const QString &role = {}) {
		auto *model = existingSources(owner, current, overlay, route, role);
		model->setAccessibleName(overlay ? route + " browser overlay" : inputTitle(role));
		PulseGuidedPickerOutcome outcome(owner); qApp->installEventFilter(&outcome);
		PulseSetupChoices::choose(owner, model, overlay ? "Choose a " + (route == "vertical" ? QString("portrait") : QString("landscape")) + " browser overlay" : "Choose " + inputTitle(role).toLower(), {},
			overlay ? "No matching browser overlays are available. Go back and use a browser link." : "No reusable inputs are available. Go back and choose Add a new input.");
		qApp->removeEventFilter(&outcome);
		const QString selected = outcome.accepted ? model->currentData().toString() : QString(); model->deleteLater(); return selected;
	}
	void refreshViews() {
		const auto available = PulseSetupPlan::availableViews(options_.sources); QSet<QString> ids; for (const auto &view : available) ids.insert(view.id);
		if (viewsTouched_) {
			const bool hadSelection = !options_.views.isEmpty(); options_.views.intersect(ids);
			// Replace a selection invalidated by an input removal, but preserve an
			// intentional empty choice across Back and Review repairs.
			if (hadSelection && options_.views.isEmpty()) { options_.views = PulseSetupPlan::recommendedViews(options_.activity, options_.sources); viewsTouched_ = false; }
		} else options_.views = PulseSetupPlan::recommendedViews(options_.activity, options_.sources);
		viewsGrid_->clearCards();
		for (const auto &view : available) {
			auto *panel = new QWidget(viewsGrid_); auto *layout = new QVBoxLayout(panel); layout->setContentsMargins(0, 0, 0, 0); layout->setSpacing(4);
			auto *choice = PulseSetupChoices::card(panel, view.title, view.description); choice->setChecked(options_.views.contains(view.id));
			choice->setProperty("pulseGuidedView", view.id); choice->setAccessibleName("Include " + view.title); layout->addWidget(choice);
			auto *preview = new QPushButton("Preview this view", panel); preview->setAccessibleName("Preview " + view.title); layout->addWidget(preview);
			QObject::connect(choice, &QPushButton::toggled, this, [this, id = view.id](bool checked) { viewsTouched_ = true; if (checked) options_.views.insert(id); else options_.views.remove(id); refresh(); });
			QObject::connect(preview, &QPushButton::clicked, this, [this, id = view.id, name = view.title] { previewView(id, name); });
			viewsGrid_->addCard(panel);
		}
		viewsGrid_->reflow(true);
		viewHint_->setText(available.size() == 1 ? "One input gives one useful full-picture view. Add a camera or different content on Inputs for more views." : "Each view changes what viewers see. Camera size and cropping are framing choices, rather than extra near-duplicate views.");
	}
	void chooseInput(const QString &role) {
		QDialog dialog(this); dialog.setObjectName("PulseGuidedChooseInput"); dialog.setProperty("pulseSetupDialog", true); dialog.setWindowTitle(inputTitle(role)); PulseSetupInput::fitDialog(dialog, 700, 450);
		auto *layout = new QVBoxLayout(&dialog); layout->setContentsMargins(22, 18, 22, 18); layout->setSpacing(14);
		PulseSetupInput::text(layout, &dialog, inputTitle(role), "PulseSetupTitle");
		PulseSetupInput::text(layout, &dialog, "Reuse an input you already have, or set up one new picture. The show will share it between its views.");
		auto *reuse = PulseSetupChoices::card(&dialog, "Use an existing input", "Choose a camera, capture, image, video or browser source already in this installation."); reuse->setCheckable(false); layout->addWidget(reuse);
		auto *create = PulseSetupChoices::card(&dialog, "Add a new input", "Choose its device, window, file or browser link. It stays a draft until Create my show."); create->setCheckable(false); layout->addWidget(create);
		QString selected;
		QObject::connect(reuse, &QPushButton::clicked, &dialog, [&] {
			const QString picked = pickExisting(&dialog, input(role), false, {}, role);
			if (!picked.isEmpty()) { selected = picked; dialog.accept(); }
		});
		QObject::connect(create, &QPushButton::clicked, &dialog, [&] {
			const QString kind = role == "content" ? (options_.activity == "gaming" ? "game_capture" : "window_capture") : "dshow_input";
			const auto spec = PulseSetupInput::askDraft(&dialog, inputTitle(role), kind, options_.newSources); if (spec.isEmpty()) return;
			// A capture device is shared rather than opened twice under new names.
			for (auto it = options_.newSources.begin(); it != options_.newSources.end(); ++it) if (spec.value("kind") != "browser_source" && !spec.value("target").toString().isEmpty() &&
				it.value().toObject().value("kind") == spec.value("kind") && it.value().toObject().value("target") == spec.value("target")) { selected = "new:" + it.key(); dialog.accept(); return; }
			const QString key = "input-" + QString::number(nextDraft_++); options_.newSources.insert(key, spec); selected = "new:" + key; dialog.accept();
		});
		layout->addStretch(); auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, &dialog); layout->addWidget(buttons); QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
		if (dialog.exec() == QDialog::Accepted && !selected.isEmpty()) { input(role) = selected; refreshViews(); refresh(); }
	}
	void configureFraming() {
		QDialog dialog(this); dialog.setObjectName("PulseGuidedFraming"); dialog.setProperty("pulseSetupDialog", true); dialog.setWindowTitle("Framing"); PulseSetupInput::fitDialog(dialog, 660, 390);
		auto *layout = new QVBoxLayout(&dialog); PulseSetupInput::text(layout, &dialog, "Keep important content visible", "PulseSetupTitle");
		PulseSetupInput::text(layout, &dialog, "Game and shared content keep their whole picture. A wide picture cannot fill a tall screen without either space around it or cropping. Preview shows that space; it is not a live capture.");
		auto *camera = new QCheckBox("Crop camera to fill", &dialog); camera->setAccessibleName("Crop camera to fill its frame"); camera->setChecked(options_.cropCamera); camera->setEnabled(!options_.sources.camera.isEmpty()); layout->addWidget(camera);
		auto *detail = new QCheckBox("Crop second angle to fill", &dialog); detail->setAccessibleName("Crop second angle or guest to fill its frame"); detail->setChecked(options_.cropDetail); detail->setEnabled(!options_.sources.detail.isEmpty()); layout->addWidget(detail);
		PulseSetupInput::text(layout, &dialog, "Cropping fills the camera's allotted space by cutting off its outer edges. With cropping off, the complete picture fits inside that space.");
		PulseSetupInput::text(layout, &dialog, "Leave cropping off for a call window, presentation, document or a group whose edges must remain visible.", "PulseSetupNotice");
		layout->addStretch(); auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog); buttons->button(QDialogButtonBox::Ok)->setText("Use framing"); PulseSetupInput::primary(buttons->button(QDialogButtonBox::Ok)); layout->addWidget(buttons);
		QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept); QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
		if (dialog.exec() == QDialog::Accepted) { options_.cropCamera = camera->isChecked(); options_.cropDetail = detail->isChecked(); refresh(); }
	}
	QJsonObject framing() const { return {{"feedAFill", options_.cropCamera}, {"feedBFill", false}, {"feedCFill", options_.cropDetail}}; }
	void setDiagram(PulseGuidedDiagram *diagram, const QString &id) const {
		const auto *theme = PulseShow::theme("guided-main"); const auto *view = theme ? PulseShow::look(*theme, id) : nullptr;
		QMap<QString, QSize> sizes;
		for (bool portrait : {false, true}) {
			const QString route = portrait ? "vertical/" : "horizontal/";
			sizes.insert(route + "feedA", sourceSize(options_.sources.camera, portrait));
			sizes.insert(route + "feedB", sourceSize(options_.sources.content, portrait));
			sizes.insert(route + "feedC", sourceSize(options_.sources.detail, portrait));
		}
		diagram->setView(view, framing(), {{"feedA", sourceName(options_.sources.camera)}, {"feedB", sourceName(options_.sources.content)}, {"feedC", sourceName(options_.sources.detail)}},
			sizes);
	}
	void previewView(const QString &id, const QString &name) {
		QDialog dialog(this); dialog.setObjectName("PulseGuidedViewPreview"); dialog.setProperty("pulseSetupDialog", true); dialog.setWindowTitle(name); PulseSetupInput::fitDialog(dialog, 900, 520);
		auto *layout = new QVBoxLayout(&dialog); PulseSetupInput::text(layout, &dialog, name, "PulseSetupTitle");
		PulseSetupInput::text(layout, &dialog, "Placement illustration using assigned input sizes where known; otherwise a 16:9 example. No live inputs are opened. Preview does not change inclusion.");
		auto *diagram = new PulseGuidedDiagram(&dialog); layout->addWidget(diagram, 1); setDiagram(diagram, id);
		auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog); buttons->button(QDialogButtonBox::Close)->setText("Back to views"); layout->addWidget(buttons); QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject); dialog.exec();
	}
	void configureTransitions() {
		QDialog dialog(this); dialog.setObjectName("PulseGuidedTransitions"); dialog.setProperty("pulseSetupDialog", true); dialog.setWindowTitle("Screen transitions"); PulseSetupInput::fitDialog(dialog, 760, 500);
		auto *layout = new QVBoxLayout(&dialog); PulseSetupInput::text(layout, &dialog, "Between your show screens", "PulseSetupTitle");
		PulseSetupInput::text(layout, &dialog, "Choose how Main, Starting, Break and Ending switch. This is separate from movement between views inside Main.");
		auto *tabs = new QTabWidget(&dialog); layout->addWidget(tabs, 1);
		QMap<QString, QComboBox *> selectors; QMap<QString, QSpinBox *> times;
		QJsonObject stingers = options_.newStingers; QSet<QString> names;
		obs_frontend_source_list sources{}; obs_frontend_get_transitions(&sources);
		for (size_t i = 0; i < sources.sources.num; ++i) if (sources.sources.array[i]) names.insert(QString::fromUtf8(obs_source_get_name(sources.sources.array[i])));
		for (const auto &value : stingers) names.insert(value.toObject().value("name").toString());
		for (const QString &route : {QString("horizontal"), QString("vertical")}) {
			auto *page = new QWidget(tabs); auto *body = new QVBoxLayout(page); body->setAlignment(Qt::AlignTop); tabs->addTab(page, route == "horizontal" ? "Landscape · 16:9" : "Portrait · 9:16");
			auto *model = new QComboBox(page); model->hide(); model->setAccessibleName(route + " screen transition"); model->addItem("Fade", "fade"); model->addItem("Cut", "cut");
			for (size_t i = 0; i < sources.sources.num; ++i) if (sources.sources.array[i] && QString::fromUtf8(obs_source_get_id(sources.sources.array[i])) == "obs_stinger_transition")
				model->addItem(QString::fromUtf8(obs_source_get_name(sources.sources.array[i])), "stinger:" + QString::fromUtf8(obs_source_get_name(sources.sources.array[i])));
			for (auto it = stingers.begin(); it != stingers.end(); ++it) model->addItem(it.value().toObject().value("name").toString() + " · new", "new-stinger:" + it.key());
			model->setCurrentIndex(qMax(0, model->findData(options_.transitions.value(route + "Transition").toString("fade")))); selectors.insert(route, model);
			auto *choose = new QPushButton(model->currentText() + "  ·  Change…", page); body->addWidget(choose);
			QObject::connect(choose, &QPushButton::clicked, &dialog, [&, model, choose] { PulseSetupChoices::choose(&dialog, model, "Choose a transition"); choose->setText(model->currentText() + "  ·  Change…"); });
			auto *time = new QSpinBox(page); time->setRange(0, 3000); time->setSuffix(" ms fade"); time->setValue(options_.transitions.value(route + "DurationMs").toInt(500)); time->setAccessibleName(route + " fade duration"); body->addWidget(time); times.insert(route, time);
			auto *add = new QPushButton("Add a stinger video…", page); body->addWidget(add);
			QObject::connect(add, &QPushButton::clicked, &dialog, [&, route, model, choose] {
				const auto spec = PulseSetupStingers::ask(&dialog, route == "vertical", names); if (spec.isEmpty()) return;
				const QString key = "guided-stinger-" + QString::number(nextDraft_++); stingers.insert(key, spec); names.insert(spec.value("name").toString());
				for (auto *selector : selectors) selector->addItem(spec.value("name").toString() + " · new", "new-stinger:" + key);
				model->setCurrentIndex(model->findData("new-stinger:" + key)); choose->setText(model->currentText() + "  ·  Change…");
			});
			QObject::connect(model, &QComboBox::currentIndexChanged, time, [model, time](int) { time->setEnabled(model->currentData() == "fade"); }); time->setEnabled(model->currentData() == "fade");
		}
		obs_frontend_source_list_free(&sources);
		auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog); buttons->button(QDialogButtonBox::Ok)->setText("Use transitions"); PulseSetupInput::primary(buttons->button(QDialogButtonBox::Ok)); layout->addWidget(buttons);
		QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept); QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
		if (dialog.exec() == QDialog::Accepted) { options_.newStingers = stingers; for (const QString &route : {QString("horizontal"), QString("vertical")}) { options_.transitions.insert(route + "Transition", selectors[route]->currentData().toString()); options_.transitions.insert(route + "DurationMs", times[route]->value()); } refresh(); }
	}
	bool editOverlays(const QString &title, QString &horizontal, QString &vertical, QString *scope = nullptr) {
		QDialog dialog(this); dialog.setObjectName("PulseGuidedOverlayDialog"); dialog.setProperty("pulseSetupDialog", true); dialog.setWindowTitle(title); PulseSetupInput::fitDialog(dialog, 790, 610);
		auto *outer = new QVBoxLayout(&dialog); outer->setContentsMargins(22, 18, 22, 18); outer->setSpacing(12);
		PulseSetupInput::text(outer, &dialog, title, "PulseSetupTitle");
		PulseSetupInput::text(outer, &dialog, "Use the browser link from your provider, not a template import link. A new link creates only the formats you choose.");
		auto *scroll = new QScrollArea(&dialog); scroll->setWidgetResizable(true); scroll->setFrameShape(QFrame::NoFrame); outer->addWidget(scroll, 1);
		auto *body = new QWidget(scroll); auto *layout = new QVBoxLayout(body); layout->setAlignment(Qt::AlignTop); scroll->setWidget(body);
		QString h = horizontal, v = vertical;
		auto *hLabel = PulseSetupInput::text(layout, body, "", "PulseSetupSectionTitle");
		auto *hUrl = new QLineEdit(body); hUrl->setAccessibleName("Landscape browser link"); hUrl->setPlaceholderText("https://…"); layout->addWidget(hUrl);
		auto *hActions = new QVBoxLayout; auto *hExisting = new QPushButton("Use existing landscape overlay…", body); auto *hPaste = new QPushButton("Use a browser link", body); hActions->addWidget(hExisting); hActions->addWidget(hPaste); layout->addLayout(hActions);
		auto *same = new QCheckBox("Use the same browser link for portrait", body); same->setChecked((h.isEmpty() && v.isEmpty()) || (!h.isEmpty() && h == v)); layout->addWidget(same);
		auto *vLabel = PulseSetupInput::text(layout, body, "", "PulseSetupSectionTitle");
		auto *vUrl = new QLineEdit(body); vUrl->setAccessibleName("Portrait browser link"); vUrl->setPlaceholderText("Optional separate portrait link"); layout->addWidget(vUrl);
		auto *vActions = new QVBoxLayout; auto *vExisting = new QPushButton("Use existing portrait overlay…", body); auto *vPaste = new QPushButton("Use a browser link", body); vActions->addWidget(vExisting); vActions->addWidget(vPaste); layout->addLayout(vActions);
		auto *all = new QCheckBox("Show on every screen", body); all->setAccessibleName("Show on Starting, Break and Ending as well as Main"); all->setChecked(!scope || *scope == "all"); all->setVisible(scope); layout->addWidget(all);
		auto *scopeHint = PulseSetupInput::text(layout, body, "Every screen includes Main, Starting, Break and Ending. Turn this off to show this layer only in Main views."); scopeHint->setVisible(scope);
		PulseSetupInput::text(layout, body, scope ? "Layer 1 is below 2 and 3; all alert layers stay above your pictures. Existing audio and monitoring are retained. New browser copies start silent: choose one audible copy in the mixer after creation." :
			"Leave a format empty to keep its built-in screen. Existing source settings are retained. This assigns artwork; it does not test a countdown or alert.", "PulseSetupNotice");
		auto *error = PulseSetupInput::text(outer, &dialog, "", "PulseSetupError"); error->hide();
		auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog); buttons->button(QDialogButtonBox::Ok)->setText("Use these overlays"); PulseSetupInput::primary(buttons->button(QDialogButtonBox::Ok)); outer->addWidget(buttons);
		auto update = [&] {
			hLabel->setText(h.isEmpty() ? "Landscape · browser link" : "Landscape · " + sourceName(h)); hUrl->setEnabled(h.isEmpty());
			vLabel->setText(v.isEmpty() ? "Portrait · separate browser link" : "Portrait · " + sourceName(v)); vUrl->setEnabled(v.isEmpty());
			const bool separate = !same->isChecked(); vLabel->setVisible(separate); vUrl->setVisible(separate); vExisting->setVisible(separate); vPaste->setVisible(separate);
		};
		QObject::connect(hExisting, &QPushButton::clicked, &dialog, [&] { const QString picked = pickExisting(&dialog, h, true, "horizontal"); if (!picked.isEmpty()) h = picked; update(); });
		QObject::connect(vExisting, &QPushButton::clicked, &dialog, [&] { const QString picked = pickExisting(&dialog, v, true, "vertical"); if (!picked.isEmpty()) v = picked; update(); });
		auto editLink = [&](QString &id, QLineEdit *field) {
			if (id.startsWith("new:")) field->setText(options_.newSources.value(id.mid(4)).toObject().value("url").toString());
			else if (!id.isEmpty()) { OBSSourceAutoRelease source = obs_get_source_by_uuid(id.toUtf8().constData()); if (source) { OBSDataAutoRelease settings = obs_source_get_settings(source); field->setText(QString::fromUtf8(obs_data_get_string(settings, "url"))); } }
			id.clear(); update(); field->setFocus();
		};
		QObject::connect(hPaste, &QPushButton::clicked, &dialog, [&] { editLink(h, hUrl); }); QObject::connect(vPaste, &QPushButton::clicked, &dialog, [&] { editLink(v, vUrl); });
		QObject::connect(same, &QCheckBox::toggled, &dialog, [&](bool) { update(); });
		QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
			QJsonObject definitions = options_.newSources;
			auto link = [&](const QString &value, const QString &format) {
				const QString url = value.trimmed(); if (url.isEmpty()) return QString();
				const QString key = "overlay-" + QString::number(nextDraft_++);
				const QString name = PulseSetupInput::uniqueName(title + " · " + format, definitions, "browser_source");
				QJsonObject spec{{"name", name}, {"kind", "browser_source"}, {"url", url}};
				const QString issue = PulseSetupInput::draftProblem(spec, definitions, {}, true);
				if (!issue.isEmpty()) { error->setText(issue); return QString("invalid"); }
				definitions.insert(key, spec); return "new:" + key;
			};
			QString nextH = h.isEmpty() ? link(hUrl->text(), "landscape") : h;
			QString nextV = same->isChecked() ? QString() : v.isEmpty() ? link(vUrl->text(), "portrait") : v;
			if (nextH == "invalid" || (!same->isChecked() && nextV == "invalid")) { error->show(); return; }
			if (same->isChecked()) {
				nextV = nextH;
				if (!nextH.isEmpty() && !nextH.startsWith("new:")) {
					OBSSourceAutoRelease source = obs_get_source_by_uuid(nextH.toUtf8().constData());
					if (!pulseBrowserMatchesCanvas(source, true)) { OBSDataAutoRelease settings = obs_source_get_settings(source); nextV = link(QString::fromUtf8(obs_data_get_string(settings, "url")), "portrait"); }
				}
			}
			if (nextV == "invalid") { error->show(); return; }
			if (scope && nextH.isEmpty() && nextV.isEmpty()) { error->setText("Paste an alert link or choose an existing browser overlay."); error->show(); return; }
			for (const auto &pair : {qMakePair(nextH, QString("horizontal")), qMakePair(nextV, QString("vertical"))}) if (!pair.first.startsWith("new:")) {
				const QString issue = sourceProblem(pair.first, true, pair.second); if (!issue.isEmpty()) { error->setText(issue); error->show(); return; }
			}
			options_.newSources = definitions; horizontal = nextH; vertical = nextV; if (scope) *scope = all->isChecked() ? "all" : "main"; dialog.accept();
		});
		QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject); update();
		return dialog.exec() == QDialog::Accepted;
	}
	void reviewDiagram() {
		if (reviewViews_.isEmpty()) { reviewViewName_->setText("Choose a view on Views."); return; }
		reviewIndex_ = qBound(0, reviewIndex_, int(reviewViews_.size()) - 1); const QString id = reviewViews_[reviewIndex_];
		for (const auto &view : PulseSetupPlan::availableViews(options_.sources)) if (view.id == id) reviewViewName_->setText(view.title + " · " + QString::number(reviewIndex_ + 1) + " of " + QString::number(reviewViews_.size()));
		setDiagram(reviewDiagram_, id); reviewPrevious_->setEnabled(reviewIndex_ > 0); reviewNext_->setEnabled(reviewIndex_ + 1 < reviewViews_.size());
	}
	void refresh() {
		if (updating_) return; updating_ = true;
		for (auto it = inputLabels_.begin(); it != inputLabels_.end(); ++it) { const QString id = input(it.key());
			it.value()->setText(inputTitle(it.key()) + "\n" + (id.isEmpty() ? (it.key() == "camera" ? "Optional · no camera" : "Not added") : sourceName(id) + " · assigned, picture not tested"));
			inputEdit_[it.key()]->setVisible(id.startsWith("new:")); inputClear_[it.key()]->setEnabled(!id.isEmpty());
		}
		for (int i = 0; i < options_.alerts.size(); ++i) { const auto &alert = options_.alerts[i];
			alertLabels_[i]->setText(!alert.enabled ? "Not added · optional" : "Landscape: " + sourceName(alert.horizontal) + "\nPortrait: " + sourceName(alert.vertical) + "\n" + (alert.scope == "all" ? "Every screen" : "Main views only")); alertClear_[i]->setEnabled(alert.enabled); }
		for (auto it = artworkLabels_.begin(); it != artworkLabels_.end(); ++it) { const auto art = options_.artwork.value(it.key()).toObject(); const bool custom = !art.value("horizontal").toString().isEmpty() || !art.value("vertical").toString().isEmpty();
			it.value()->setText(custom ? artworkSummary(it.key()) : "Built-in images in both formats · ready without Lumia or another app"); artworkClear_[it.key()]->setEnabled(custom); }
		if (reviewSummary_) {
			reviewViews_.clear(); QStringList names;
			for (const auto &view : PulseSetupPlan::availableViews(options_.sources)) if (options_.views.contains(view.id)) { reviewViews_.append(view.id); names.append(view.title); }
			reviewSummary_->setText(options_.name + "\n" + names.join(" · "));
			QSet<QString> usedRoles; for (const auto &view : PulseSetupPlan::availableViews(options_.sources)) if (options_.views.contains(view.id)) for (const auto &role : view.roles) usedRoles.insert(role);
			QStringList inputs; for (const QString &role : {QString("camera"), QString("content"), QString("detail")}) if (!input(role).isEmpty()) {
				const QString key = role == "camera" ? "feedA" : role == "content" ? "feedB" : "feedC";
				inputs << inputTitle(role) + ": " + sourceName(input(role)) + (usedRoles.contains(key) ? QString() : " · unused by the selected views"); }
			reviewInputs_->setText(inputs.join("\n") + "\nOnly used inputs are added; each is shared across the views that show it.");
			QStringList screens;
			for (const QString &key : {QString("guided-starting"), QString("guided-break"), QString("guided-ending")}) if (screenEnabled(key))
				screens << (key == "guided-starting" ? QString("Starting") : key == "guided-break" ? QString("Break") : QString("Ending")) + "\n" + artworkSummary(key);
			reviewScreens_->setText(screens.isEmpty() ? "Only Main; no extra holding screens." : screens.join("\n\n"));
			QStringList alerts; for (int i = 0; i < options_.alerts.size(); ++i) if (options_.alerts[i].enabled) {
				const auto &alert = options_.alerts[i];
				alerts << "Layer " + QString::number(i + 1) + " · " + (alert.scope == "all" ? "Every screen" : "Main views") +
					"\nLandscape: " + sourceName(alert.horizontal) + "\nPortrait: " + sourceName(alert.vertical);
			}
			reviewAlerts_->setText(alerts.isEmpty() ? "No alert layers added." : alerts.join("\n\n"));
			reviewNotice_->setText("After creation: check the real pictures on both formats and try switching views. Existing source audio and monitoring are preserved." +
				(usesNewBrowser() ? QString(" New browser inputs start silent. Choose the intended audible copy in the mixer, then check its audio and monitoring before streaming.") : QString(" Check alert audio and monitoring before streaming.")));
			reviewDiagram();
		}
		for (auto *page : pages_) page->refresh(); updating_ = false;
	}

public:
	explicit PulseGuidedWizard(QWidget *parent, const QJsonArray &savedStages) : QWizard(parent), savedStages_(savedStages) {
		options_.activity.clear(); options_.alerts.resize(3);
		for (int suffix = 1; !nameProblem().isEmpty(); ++suffix) options_.name = "My Show " + QString::number(suffix + 1);
		setObjectName("PulseWeaverSetupWizard"); setProperty("pulseTaskFirst", true); setWindowTitle("Build my show · Pulse Weaver"); setWizardStyle(QWizard::ModernStyle);
		setOption(QWizard::NoBackButtonOnStartPage); setOption(QWizard::HaveCustomButton1, false);
		setButtonText(QWizard::FinishButton, "Create my show"); setButtonText(QWizard::CustomButton1, "Review");
		const auto *screen = parent ? parent->screen() : QGuiApplication::primaryScreen();
		const QSize available = screen ? screen->availableGeometry().size() - QSize(40, 64) : QSize(1100, 800);
		setMinimumSize(qMin(740, available.width()), qMin(560, available.height())); PulseSetupInput::fitDialog(*this, 1100, 800);
		for (auto role : {QWizard::NextButton, QWizard::FinishButton}) PulseSetupInput::primary(button(role));
		auto *activity = addPage("Activity", "What do you want to stream?", "Choose your main activity. You can add cameras, shared content and other views next.");
		auto *activities = new PulseSetupChoices::ChoiceGrid(this, 2); activity->addWidget(activities);
		struct Activity { const char *id, *name, *detail; };
		for (const auto &item : {Activity{"gaming", "Games", "A game, with or without a camera. Add useful content and camera views."}, Activity{"talk", "Chat and shared content", "Talk to viewers, react to a window or share a presentation."},
			Activity{"creative", "Making, music or teaching", "Show work, an instrument or a demonstration, with an optional face or detail camera."}, Activity{"guests", "Cameras and guests", "Use cameras or capture an existing call window. This does not host a guest call."}}) {
			auto *card = PulseSetupChoices::card(activities, item.name, item.detail); card->setCheckable(false); card->setProperty("pulseGuidedActivity", item.id); card->setAccessibleName(QString("Start ") + item.name); activities->addCard(card);
			QObject::connect(card, &QPushButton::clicked, this, [this, id = QString(item.id)] { options_.activity = id; refreshViews(); refresh(); next(); });
		}
		activities->reflow(true); PulseSetupInput::text(activity, this, "The result will include your chosen Main views and optional Starting, Break and Ending screens. No broadcast starts here.", "PulseSetupNotice");
		auto *advanced = new QPushButton("Use saved layouts / detailed setup…", this); advanced->setAccessibleName("Open detailed setup instead"); activity->addWidget(advanced); QObject::connect(advanced, &QPushButton::clicked, this, [this] { advanced_ = true; reject(); }); activity->addStretch();
		pages_[0]->problem = [this] { return options_.activity.isEmpty() ? QString("Choose an activity card to start.") : QString(); };

		auto *inputs = addPage("Inputs", "What will viewers see?", "Choose each real input once. The show will reuse it across its views; a camera is optional.");
		for (const QString &role : {QString("camera"), QString("content"), QString("detail")}) {
			auto *panel = surface(inputs); auto *row = new QVBoxLayout(panel); row->setContentsMargins(16, 12, 16, 12); row->setSpacing(8);
			inputLabels_.insert(role, PulseSetupInput::text(row, panel, "", "PulseSetupSectionTitle"));
			auto *actions = new QHBoxLayout; auto *choose = new QPushButton("Choose input…", panel); choose->setAccessibleName("Choose " + role + " input"); actions->addWidget(choose);
			auto *edit = new QPushButton("Edit new input…", panel); edit->setAccessibleName("Edit " + role + " input"); actions->addWidget(edit); inputEdit_.insert(role, edit);
			auto *clear = new QPushButton(role == "camera" ? "No camera" : "Remove", panel); clear->setAccessibleName("Remove " + role + " input"); actions->addWidget(clear); actions->addStretch(); row->addLayout(actions); inputClear_.insert(role, clear);
			QObject::connect(choose, &QPushButton::clicked, this, [this, role] { chooseInput(role); });
			QObject::connect(clear, &QPushButton::clicked, this, [this, role] { input(role).clear(); refreshViews(); refresh(); });
			QObject::connect(edit, &QPushButton::clicked, this, [this, role] { const QString key = input(role).mid(4); const auto spec = PulseSetupInput::askDraft(this, inputTitle(role), {}, options_.newSources, options_.newSources.value(key).toObject(), key); if (!spec.isEmpty()) { options_.newSources.insert(key, spec); refresh(); } });
		}
		PulseSetupInput::text(inputs, this, "Nothing new is opened while planning. Existing inputs keep their settings. Check the real pictures after creation."); inputs->addStretch(); pages_[1]->problem = [this] { return inputsProblem(); };

		auto *views = addPage("Views", "Which views will you switch between?", "Keep the views that have a job in your show. Preview opens an example without adding or removing it.");
		viewHint_ = PulseSetupInput::text(views, this, ""); viewsGrid_ = new PulseSetupChoices::ChoiceGrid(this, 2); views->addWidget(viewsGrid_);
		auto *viewActions = new QHBoxLayout; auto *framingButton = new QPushButton("Framing…", this), *transitions = new QPushButton("Screen transitions…", this); viewActions->addWidget(framingButton); viewActions->addWidget(transitions); viewActions->addStretch(); views->addLayout(viewActions); views->addStretch();
		QObject::connect(framingButton, &QPushButton::clicked, this, [this] { configureFraming(); }); QObject::connect(transitions, &QPushButton::clicked, this, [this] { configureTransitions(); });
		pages_[2]->problem = [this] { return viewsProblem(); };

		auto *screens = addPage("Screens", "Add useful screens around your Main views", "These built-in images work without Lumia or another app. Customize their messages, or use your own browser artwork.");
		auto *showName = new QLineEdit(options_.name, this); showName->setMaxLength(60); showName->setAccessibleName("Show name"); auto *nameForm = new QFormLayout; nameForm->addRow("Show name", showName); screens->addLayout(nameForm);
		QObject::connect(showName, &QLineEdit::textChanged, this, [this](const QString &name) { options_.name = name; refresh(); });
		for (const QString &key : {QString("guided-starting"), QString("guided-break"), QString("guided-ending")}) {
			const bool start = key == "guided-starting", away = key == "guided-break";
			const QString title = start ? "Starting screen" : away ? "Break screen" : "Ending screen";
			auto *panel = surface(screens); auto *body = new QVBoxLayout(panel); body->setContentsMargins(16, 12, 16, 12);
			auto *include = new QCheckBox(title, panel); include->setChecked(true); include->setAccessibleName("Include " + title.toLower()); body->addWidget(include);
			auto *message = new QLineEdit(start ? options_.startingText : away ? options_.breakText : options_.endingText, panel); message->setMaxLength(160); message->setAccessibleName(title + " message"); body->addWidget(message);
			artworkLabels_.insert(key, PulseSetupInput::text(body, panel, ""));
			auto *actions = new QVBoxLayout; auto *custom = new QPushButton("Use my own browser artwork…", panel), *builtIn = new QPushButton("Use built-in screen", panel); actions->addWidget(custom); actions->addWidget(builtIn); body->addLayout(actions); artworkClear_.insert(key, builtIn);
			QObject::connect(include, &QCheckBox::toggled, this, [this, start, away, message](bool value) { if (start) options_.starting = value; else if (away) options_.brb = value; else options_.ending = value; message->setEnabled(value); refresh(); });
			QObject::connect(message, &QLineEdit::textChanged, this, [this, start, away](const QString &value) { if (start) options_.startingText = value; else if (away) options_.breakText = value; else options_.endingText = value; refresh(); });
			QObject::connect(custom, &QPushButton::clicked, this, [this, key, title] { auto art = options_.artwork.value(key).toObject(); QString h = art.value("horizontal").toString(), v = art.value("vertical").toString(); if (editOverlays(title, h, v)) { options_.artwork.insert(key, QJsonObject{{"horizontal", h}, {"vertical", v}}); refresh(); } });
			QObject::connect(builtIn, &QPushButton::clicked, this, [this, key] { options_.artwork.remove(key); refresh(); });
		}
		PulseSetupInput::text(screens, this, "Starting is a static holding message. For a live countdown, choose your own countdown browser artwork and finish its provider commands after creation.", "PulseSetupNotice"); screens->addStretch();
		pages_[3]->problem = [this] {
			if (!nameProblem().isEmpty()) return nameProblem();
			for (const QString &key : {QString("guided-starting"), QString("guided-break"), QString("guided-ending")})
				if (screenNeedsMessage(key) && screenMessage(key).trimmed().isEmpty()) return QString("Add a message for each included screen that uses a built-in image in either format.");
			return QString();
		};

		auto *alerts = addPage("Alerts", "Alerts and overlays · up to 3 layers", "Optional. Add provider browser links or reuse existing overlays. You can skip this step and add them later.");
		for (int i = 0; i < 3; ++i) {
			auto *panel = surface(alerts); auto *body = new QVBoxLayout(panel); body->setContentsMargins(16, 12, 16, 12);
			PulseSetupInput::text(body, panel, "Alert layer " + QString::number(i + 1) + (i == 0 ? " · lowest overlay" : i == 2 ? " · highest overlay" : ""), "PulseSetupSectionTitle");
			alertLabels_.append(PulseSetupInput::text(body, panel, ""));
			auto *row = new QVBoxLayout; auto *edit = new QPushButton("Add or edit this layer…", panel), *remove = new QPushButton("Remove layer", panel); edit->setAccessibleName("Edit alert layer " + QString::number(i + 1)); remove->setAccessibleName("Remove alert layer " + QString::number(i + 1)); row->addWidget(edit); row->addWidget(remove); body->addLayout(row); alertClear_.append(remove);
			QObject::connect(edit, &QPushButton::clicked, this, [this, i] { auto layer = options_.alerts[i]; if (!layer.enabled && layer.horizontal.isEmpty() && layer.vertical.isEmpty()) layer.scope = "all"; if (editOverlays("Alert layer " + QString::number(i + 1), layer.horizontal, layer.vertical, &layer.scope)) { layer.enabled = true; options_.alerts[i] = layer; refresh(); } });
			QObject::connect(remove, &QPushButton::clicked, this, [this, i] { options_.alerts[i].enabled = false; refresh(); });
		}
		PulseSetupInput::text(alerts, this, "All three layers sit above your camera and content. Lumia, StreamElements, Streamlabs and other HTTP/HTTPS browser overlays are supported. No empty layers create sources.", "PulseSetupNotice"); alerts->addStretch(); pages_[4]->problem = [this] { return alertsProblem(); };

		auto *review = addPage("Review", "Here is the show you will get", "Inspect the views and assigned inputs. Create adds the editable show; it does not start a broadcast or certify the live pictures and sound.");
		reviewSummary_ = PulseSetupInput::text(review, this, "", "PulseSetupSectionTitle");
		auto *nav = new QHBoxLayout; reviewPrevious_ = new QPushButton("← Previous view", this); reviewNext_ = new QPushButton("Next view →", this); reviewViewName_ = new QLabel(this); reviewViewName_->setWordWrap(true); nav->addWidget(reviewPrevious_); nav->addWidget(reviewViewName_, 1); nav->addWidget(reviewNext_); review->addLayout(nav);
		reviewDiagram_ = new PulseGuidedDiagram(this); reviewDiagram_->setMinimumHeight(220); review->addWidget(reviewDiagram_);
		QObject::connect(reviewPrevious_, &QPushButton::clicked, this, [this] { --reviewIndex_; reviewDiagram(); }); QObject::connect(reviewNext_, &QPushButton::clicked, this, [this] { ++reviewIndex_; reviewDiagram(); });
		for (int section = 0; section < 3; ++section) {
			auto *panel = surface(review); auto *body = new QVBoxLayout(panel); body->setContentsMargins(14, 10, 14, 10);
			auto *label = PulseSetupInput::text(body, panel, ""); if (section == 0) reviewInputs_ = label; else if (section == 1) reviewScreens_ = label; else reviewAlerts_ = label;
			auto *edit = new QPushButton(section == 0 ? "Change inputs…" : section == 1 ? "Change screens…" : "Change alert layers…", panel); body->addWidget(edit);
			QObject::connect(edit, &QPushButton::clicked, this, [this, destination = section == 0 ? 1 : section == 1 ? 3 : 4] { while (currentId() > destination) back(); });
		}
		auto *editViews = new QPushButton("Change included views…", this); review->addWidget(editViews); QObject::connect(editViews, &QPushButton::clicked, this, [this] { while (currentId() > 2) back(); });
		auto *again = new QPushButton("Check assignments again", this); review->addWidget(again); QObject::connect(again, &QPushButton::clicked, this, [this] { refresh(); });
		reviewNotice_ = PulseSetupInput::text(review, this, "", "PulseSetupNotice");
		pages_[5]->problem = [this] { return allProblem(); };
		QObject::connect(this, &QWizard::currentIdChanged, this, [this](int id) {
			if (id < 0) return; if (id == 2) refreshViews(); if (id == 5) visitedReview_ = true;
			const QStringList labels{"Choose inputs", "Choose views", "Break screens", "Alert layers", "Review show"}; if (id < labels.size()) setButtonText(QWizard::NextButton, labels[id]);
			setOption(QWizard::HaveCustomButton1, visitedReview_ && id > 0 && id < 5); refresh();
		});
		QObject::connect(this, &QWizard::customButtonClicked, this, [this](int role) { if (role == QWizard::CustomButton1) while (currentId() >= 0 && currentId() < 5 && currentPage()->isComplete()) { const int before = currentId(); next(); if (before == currentId()) break; } });
		setButtonText(QWizard::NextButton, "Choose inputs"); refreshViews(); refresh();
	}
	const Options &options() const { return options_; }
	bool advancedRequested() const { return advanced_; }
	QString finalProblem() const { return allProblem(); }
};
