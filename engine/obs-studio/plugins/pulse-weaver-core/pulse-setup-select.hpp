#pragma once

#include "pulse-setup-guided.hpp"
#include "pulse-setup-select-plan.hpp"
#include <QSlider>

// Two required pages. Optional editors keep their own local drafts, and nothing
// touches the source graph until the production wrapper commits Create.
class PulseSelectWizard final : public QWizard {
	PulseSelectPlan::Options options_;
	QJsonArray savedStages_;
	QVector<PulseGuidedPage *> pages_;
	QMap<QString, QWidget *> inputRows_;
	QMap<QString, QLabel *> inputLabels_;
	QMap<QString, QPushButton *> inputEdits_, inputClears_;
	QLabel *selection_ = nullptr, *previewTitle_ = nullptr, *summary_ = nullptr;
	PulseGuidedDiagram *diagram_ = nullptr;
	QString previewKey_;
	int nextDraft_ = 1, previewIndex_ = 0;
	bool advanced_ = false;

	QString inputTitle(const QString &role) const { return PulseShow::roleName(role); }
	QString sourceName(const QString &id) const {
		if (id.isEmpty()) return "Not selected";
		if (id.startsWith("new:")) return options_.newSources.value(id.mid(4)).toObject().value("name").toString("Missing input draft");
		OBSSourceAutoRelease source = obs_get_source_by_uuid(id.toUtf8().constData());
		return source ? QString::fromUtf8(obs_source_get_name(source)) : "Input no longer available";
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
			if (spec.isEmpty()) return "A selected input draft is missing.";
			return PulseSetupInput::draftProblem(spec, options_.newSources, id.mid(4), overlay);
		}
		OBSSourceAutoRelease source = obs_get_source_by_uuid(id.toUtf8().constData());
		if (!source || !(obs_source_get_output_flags(source) & OBS_SOURCE_VIDEO) || obs_scene_from_source(source) || obs_group_from_source(source)) return "Choose an available camera or picture input.";
		if (pulseIsNativeOverlaySource(source) || (!overlay && presentationOnly(source))) return "Choose a real input. Generated setup artwork is separate.";
		if (overlay && (!pulseWizardOverlaySourceAllowed(source) || !pulseBrowserMatchesCanvas(source, route == "vertical"))) return "Choose a browser overlay matching this format.";
		return {};
	}
	QString nameProblem() const {
		const auto name = options_.name.trimmed();
		for (const auto &section : {QString("Main"), QString("Starting"), QString("Break"), QString("Ending"), QString("Celebration")}) {
			const QString full = name + " · " + section;
			for (const auto &stage : savedStages_) if (stage.toObject().value("name") == full) return "This show name is already in use. Choose another name.";
			for (const auto &candidate : {full, full + " · Portrait"}) {
				OBSSourceAutoRelease source = obs_get_source_by_name(candidate.toUtf8().constData());
				OBSCanvasAutoRelease canvas = obs_get_canvas_by_name("Pulse Weaver Vertical");
				if (!source && canvas) source = obs_canvas_get_source_by_name(canvas, candidate.toUtf8().constData());
				if (source) return "A scene with this show name already exists. Choose another name.";
			}
		}
		return {};
	}
	QString allProblem() const {
		if (PulseHasActiveOutputs()) return "Stop streaming and recording before creating the show. Your choices stay here.";
		for (const auto &issue : {PulseSelectPlan::problem(options_), nameProblem()}) if (!issue.isEmpty()) return issue;
		for (const auto &id : PulseSelectPlan::references(options_)) {
			const auto issue = sourceProblem(id); if (!issue.isEmpty()) return sourceName(id) + ": " + issue;
		}
		for (const auto &layer : options_.alerts) if (layer.enabled) for (const auto &route : {QString("horizontal"), QString("vertical")}) {
			const auto issue = sourceProblem(route == "horizontal" ? layer.horizontal : layer.vertical, true, route); if (!issue.isEmpty()) return issue;
		}
		for (const auto &choice : PulseSelectPlan::choices()) if (options_.selected.contains(choice.key) && PulseSelectPlan::look(choice.key)->main == "graphic") {
			const auto artwork = options_.artwork.value(choice.key.section('/', 0, 0)).toObject();
			for (const auto &route : {QString("horizontal"), QString("vertical")}) {
				const auto issue = sourceProblem(artwork.value(route).toString(), true, route); if (!issue.isEmpty()) return issue;
			}
		}
		return {};
	}
	QVBoxLayout *addStep(const QString &title, const QString &detail) {
		auto *page = new PulseGuidedPage; auto *outer = new QVBoxLayout(page); outer->setContentsMargins(14, 10, 14, 4); outer->setSpacing(10);
		page->setObjectName(pages_.isEmpty() ? "PulseSelectLooksPage" : "PulseSelectBuildPage");
		PulseSetupInput::text(outer, page, pages_.isEmpty() ? "1  Choose your layouts     ·     2  Set up & build" : "1  Layouts selected     ·     2  Set up & build", "PulseSetupEyebrow");
		PulseSetupInput::text(outer, page, title, "PulseSetupTitle"); PulseSetupInput::text(outer, page, detail, "PulseSetupSubtitle");
		auto *scroll = new QScrollArea(page); scroll->setWidgetResizable(true); scroll->setFrameShape(QFrame::NoFrame); scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff); outer->addWidget(scroll, 1);
		auto *body = new QWidget(scroll); auto *layout = new QVBoxLayout(body); layout->setContentsMargins(0, 4, 8, 8); layout->setSpacing(12); layout->setAlignment(Qt::AlignTop); scroll->setWidget(body);
		page->notice = new QLabel(page); page->notice->setObjectName("PulseSetupNotice"); page->notice->setWordWrap(true); page->notice->setTextFormat(Qt::PlainText); outer->addWidget(page->notice);
		pages_.append(page); QWizard::addPage(page); return layout;
	}
	QString pickExisting(QWidget *owner, const QString &current, bool overlay, const QString &route = {}, const QString &role = {}) {
		auto *model = new QComboBox(owner); model->hide(); model->addItem("Choose…", "");
		struct Data { QComboBox *model; bool overlay; QString route; } data{model, overlay, route};
		obs_enum_sources([](void *opaque, obs_source_t *source) {
			auto &d = *static_cast<Data *>(opaque);
			if (!(obs_source_get_output_flags(source) & OBS_SOURCE_VIDEO) || obs_scene_from_source(source) || obs_group_from_source(source) || pulseIsNativeOverlaySource(source)) return true;
			if (!d.overlay && presentationOnly(source)) return true;
			if (d.overlay && (!pulseWizardOverlaySourceAllowed(source) || !pulseBrowserMatchesCanvas(source, d.route == "vertical"))) return true;
			d.model->addItem(QString::fromUtf8(obs_source_get_name(source)), QString::fromUtf8(obs_source_get_uuid(source))); return true;
		}, &data);
		for (auto it = options_.newSources.begin(); it != options_.newSources.end(); ++it) if (it.value().toObject().value("kind") != "setup_card" && (!overlay || it.value().toObject().value("kind") == "browser_source")) model->addItem(it.value().toObject().value("name").toString() + " · new", "new:" + it.key());
		model->setCurrentIndex(qMax(0, model->findData(current)));
		PulseGuidedPickerOutcome outcome(owner); qApp->installEventFilter(&outcome);
		PulseSetupChoices::choose(owner, model, overlay ? "Choose a " + route + " browser overlay" : "Choose " + inputTitle(role).toLower(), {}, "No matching inputs. Go back and choose Add new.");
		qApp->removeEventFilter(&outcome); const auto result = outcome.accepted ? model->currentData().toString() : QString(); model->deleteLater(); return result;
	}
	void addInput(const QString &role, bool edit = false) {
		const auto old = options_.sources.value(role), key = edit ? old.mid(4) : QString();
		const QString kind = role == "game" ? "game_capture" : role == "screen" ? "window_capture" : "dshow_input";
		const auto spec = PulseSetupInput::askDraft(this, inputTitle(role), kind, options_.newSources, edit ? options_.newSources.value(key).toObject() : QJsonObject{}, key);
		if (spec.isEmpty()) return;
		// The input editor rejects duplicate capture targets. Existing inputs can
		// be reused directly without opening another device or decoder.
		const QString id = edit ? key : "select-input-" + QString::number(nextDraft_++);
		options_.newSources.insert(id, spec); options_.sources.insert(role, "new:" + id); refresh();
	}
	void drawPreview() {
		QStringList keys; for (const auto &choice : PulseSelectPlan::choices()) if (options_.selected.contains(choice.key)) keys.append(choice.key);
		if (keys.isEmpty()) { diagram_->setView(nullptr, {}, {}, {}); previewTitle_->setText("Select a layout to see its arrangement"); return; }
		previewIndex_ = (previewIndex_ % keys.size() + keys.size()) % keys.size(); previewKey_ = keys[previewIndex_];
		const auto *look = PulseSelectPlan::look(previewKey_); QMap<QString, QString> names; QMap<QString, QSize> sizes;
		for (const auto &role : {look->main, look->support, look->third}) if (!role.isEmpty()) {
			names.insert(role, role == "graphic" ? options_.messages.value(previewKey_.section('/', 0, 0)) : options_.sources.value(role).isEmpty() ? inputTitle(role) : sourceName(options_.sources.value(role)));
			OBSSourceAutoRelease source = obs_get_source_by_uuid(options_.sources.value(role).toUtf8().constData());
			QSize size = source && obs_source_get_width(source) && obs_source_get_height(source) ? QSize(obs_source_get_width(source), obs_source_get_height(source)) : QSize(1920, 1080);
			const auto spec = options_.newSources.value(options_.sources.value(role).mid(4)).toObject();
			if (spec.value("kind") == "image_source") { const auto actual = QImageReader(spec.value("target").toString()).size(); if (actual.isValid()) size = actual; }
			sizes.insert("horizontal/" + role, size); sizes.insert("vertical/" + role, role == "graphic" ? QSize(1080, 1920) : size);
		}
		previewTitle_->setText(look->name + "  ·  " + QString::number(previewIndex_ + 1) + " / " + QString::number(keys.size()));
		diagram_->setView(look, options_.framing, names, sizes, !look->optionalPresenter || !options_.sources.value(look->support).isEmpty());
	}
	void refresh() {
		const auto roles = PulseSelectPlan::inputs(options_);
		for (auto it = inputRows_.begin(); it != inputRows_.end(); ++it) {
			it.value()->setVisible(roles.contains(it.key()));
			inputLabels_[it.key()]->setText(inputTitle(it.key()) + (roles.value(it.key()) ? "" : " · optional") + "\n" + sourceName(options_.sources.value(it.key())));
			inputEdits_[it.key()]->setVisible(options_.sources.value(it.key()).startsWith("new:")); inputClears_[it.key()]->setEnabled(!options_.sources.value(it.key()).isEmpty());
		}
		selection_->setText(QString::number(options_.selected.size()) + " selected · choose only the views and screens you will use");
		QStringList names; for (const auto &choice : PulseSelectPlan::choices()) if (options_.selected.contains(choice.key)) names.append(choice.title);
		summary_->setText(names.join("  ·  "));
		drawPreview(); for (auto *page : pages_) page->refresh();
	}
	void configureFraming() {
		QDialog dialog(this); dialog.setProperty("pulseSetupDialog", true); dialog.setObjectName("PulseSelectFraming"); dialog.setWindowTitle("Camera framing & colour"); PulseSetupInput::fitDialog(dialog, 720, 550);
		auto *body = new QVBoxLayout(&dialog); PulseSetupInput::text(body, &dialog, "Make the cameras fit your show", "PulseSetupTitle");
		PulseSetupInput::text(body, &dialog, "Game and screen captures keep the complete picture. Existing camera filters stay in place.");
		QMap<QString, QCheckBox *> crop, mono;
		const auto roles = PulseSelectPlan::inputs(options_);
		for (const auto &role : {QString("presenter"), QString("activity")}) if (roles.contains(role)) {
			auto *fill = new QCheckBox("Crop " + inputTitle(role).toLower() + " to fill its frame", &dialog); fill->setChecked(options_.framing.value(role + "Fill").toBool()); body->addWidget(fill); crop.insert(role, fill);
			auto *grey = new QCheckBox("Monochrome " + inputTitle(role).toLower(), &dialog); grey->setChecked(options_.framing.value(role + "Monochrome").toBool()); body->addWidget(grey); mono.insert(role, grey);
		}
		auto slider = [&](const QString &name, int min, int max, int initial) {
			auto *label = PulseSetupInput::text(body, &dialog, name + " · " + QString::number(initial) + "%"); auto *value = new QSlider(Qt::Horizontal, &dialog); value->setRange(min, max); value->setValue(initial); value->setAccessibleName(name); body->addWidget(value);
			QObject::connect(value, &QSlider::valueChanged, label, [label, name](int v) { label->setText(name + " · " + QString::number(v) + "%"); }); return value;
		};
		auto *size = slider("Landscape inset size", 18, 40, qRound(options_.framing.value("cameraSize").toDouble(.27) * 100));
		auto *portrait = slider("Portrait camera height", 22, 45, qRound(options_.framing.value("portraitCamera").toDouble(.32) * 100));
		PulseSetupInput::text(body, &dialog, "Inset controls apply to two-picture layouts. Three-picture layouts keep a clear side column / stacked arrangement.");
		auto *corners = new QHBoxLayout; QString corner = options_.framing.value("corner").toString("bottom-right");
		QVector<QPushButton *> cornerButtons;
		for (const auto &key : {QString("top-left"), QString("top-right"), QString("bottom-left"), QString("bottom-right")}) { auto *button = new QPushButton(key, &dialog); button->setCheckable(true); button->setChecked(corner == key); corners->addWidget(button); cornerButtons.append(button); QObject::connect(button, &QPushButton::clicked, &dialog, [&, key, button] { corner = key; for (auto *other : cornerButtons) other->setChecked(other == button); }); }
		body->addLayout(corners); body->addStretch();
		auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog); buttons->button(QDialogButtonBox::Ok)->setText("Use framing"); PulseSetupInput::primary(buttons->button(QDialogButtonBox::Ok)); body->addWidget(buttons);
		QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept); QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
		if (dialog.exec() == QDialog::Accepted) { for (auto it = crop.begin(); it != crop.end(); ++it) { options_.framing.insert(it.key() + "Fill", it.value()->isChecked()); options_.framing.insert(it.key() + "Monochrome", mono[it.key()]->isChecked()); } options_.framing.insert("cameraSize", size->value() / 100.); options_.framing.insert("portraitCamera", portrait->value() / 100.); options_.framing.insert("corner", corner); refresh(); }
	}
	// Shared-format overlay and transition editors are defined below.
	void configureTransitions();
	bool editOverlays(const QString &, QString &, QString &, QString * = nullptr);
	void configureScreens() {
		QDialog dialog(this); dialog.setProperty("pulseSetupDialog", true); dialog.setWindowTitle("Screen messages & artwork"); PulseSetupInput::fitDialog(dialog, 760, 620);
		auto *outer = new QVBoxLayout(&dialog); PulseSetupInput::text(outer, &dialog, "Your screen messages & artwork", "PulseSetupTitle");
		PulseSetupInput::text(outer, &dialog, "Built-in screens are static images. Use your provider's browser artwork for countdowns or animated scenes.");
		auto *scroll = new QScrollArea(&dialog); scroll->setWidgetResizable(true); outer->addWidget(scroll, 1); auto *body = new QWidget(scroll); auto *layout = new QVBoxLayout(body); layout->setAlignment(Qt::AlignTop); scroll->setWidget(body);
		const auto before = options_; QMap<QString, QLineEdit *> fields;
		for (const auto &choice : PulseSelectPlan::choices()) if (options_.selected.contains(choice.key) && PulseSelectPlan::look(choice.key)->main == "graphic") {
			const auto key = choice.key.section('/', 0, 0); PulseSetupInput::text(layout, body, choice.title, "PulseSetupSectionTitle");
			auto *field = new QLineEdit(options_.messages.value(key), body); field->setMaxLength(160); field->setAccessibleName(choice.title + " message"); layout->addWidget(field); fields.insert(key, field);
			auto *custom = new QPushButton("Choose browser artwork…", body), *reset = new QPushButton("Use built-in image", body); layout->addWidget(custom); layout->addWidget(reset);
			QObject::connect(custom, &QPushButton::clicked, &dialog, [&, key, custom, title = choice.title] { auto art = options_.artwork.value(key).toObject(); QString h = art.value("horizontal").toString(), v = art.value("vertical").toString(); if (editOverlays(title, h, v)) { options_.artwork.insert(key, QJsonObject{{"horizontal", h}, {"vertical", v}}); custom->setText("Browser artwork selected · Change…"); } });
			QObject::connect(reset, &QPushButton::clicked, &dialog, [this, key, custom] { options_.artwork.remove(key); custom->setText("Choose browser artwork…"); });
		}
		if (fields.isEmpty()) PulseSetupInput::text(layout, body, "Select Starting, Break, Ending or Celebration on the first page to customise those screens.");
		auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog); outer->addWidget(buttons); QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept); QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
		if (dialog.exec() == QDialog::Accepted) for (auto it = fields.begin(); it != fields.end(); ++it) options_.messages[it.key()] = it.value()->text(); else options_ = before;
		refresh();
	}
	void configureAlerts() {
		QDialog dialog(this); dialog.setProperty("pulseSetupDialog", true); dialog.setWindowTitle("Alerts · up to 3 layers"); PulseSetupInput::fitDialog(dialog, 720, 490);
		auto *body = new QVBoxLayout(&dialog); PulseSetupInput::text(body, &dialog, "Alerts & overlays", "PulseSetupTitle"); PulseSetupInput::text(body, &dialog, "Use up to three independent browser layers. Layer 3 sits above layers 1 and 2.");
		const auto before = options_; QVector<QLabel *> labels;
		for (int i = 0; i < 3; ++i) {
			auto *label = PulseSetupInput::text(body, &dialog, "", "PulseSetupSectionTitle"); labels.append(label); label->setText("Layer " + QString::number(i + 1) + (options_.alerts[i].enabled ? " · added" : " · empty"));
			auto *row = new QHBoxLayout; auto *edit = new QPushButton("Choose overlay…", &dialog), *remove = new QPushButton("Remove", &dialog); edit->setAccessibleName("Edit alert layer " + QString::number(i + 1)); row->addWidget(edit); row->addWidget(remove); body->addLayout(row);
			QObject::connect(edit, &QPushButton::clicked, &dialog, [&, i, label] { auto layer = options_.alerts[i]; if (!layer.enabled) layer.scope = "all"; if (editOverlays("Alert layer " + QString::number(i + 1), layer.horizontal, layer.vertical, &layer.scope)) { layer.enabled = true; options_.alerts[i] = layer; label->setText("Layer " + QString::number(i + 1) + " · added"); } });
			QObject::connect(remove, &QPushButton::clicked, &dialog, [&, i, label] { options_.alerts[i].enabled = false; label->setText("Layer " + QString::number(i + 1) + " · empty"); });
		}
		body->addStretch(); auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog); body->addWidget(buttons); QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept); QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
		if (dialog.exec() != QDialog::Accepted) options_ = before; refresh();
	}
public:
	explicit PulseSelectWizard(QWidget *parent, const QJsonArray &saved) : QWizard(parent), savedStages_(saved) {
		for (int i = 2; !nameProblem().isEmpty(); ++i) options_.name = "My Show " + QString::number(i);
		setObjectName("PulseWeaverSetupWizard"); setProperty("pulseSelectionFirst", true); setWindowTitle("Build my show · Pulse Weaver"); setWizardStyle(QWizard::ModernStyle); setOption(QWizard::NoBackButtonOnStartPage);
		setButtonText(QWizard::NextButton, "Set up selected layouts →"); setButtonText(QWizard::FinishButton, "Create my show"); setButtonText(QWizard::CancelButton, "Cancel"); setButtonText(QWizard::BackButton, "← Layouts");
		setMinimumSize(qMin(740, screen()->availableGeometry().width() - 40), qMin(560, screen()->availableGeometry().height() - 64)); PulseSetupInput::fitDialog(*this, 1100, 800);
		for (auto role : {QWizard::NextButton, QWizard::FinishButton}) PulseSetupInput::primary(button(role));
		auto *choose = addStep("What do you need in your show?", "Select the layouts you will switch between. Next, supply only the pictures those layouts need.");
		selection_ = PulseSetupInput::text(choose, this, "", "PulseSetupNotice");
		for (int section = 0; section < 2; ++section) {
			PulseSetupInput::text(choose, this, section == 0 ? "Live layouts" : "Starting, breaks & announcements", "PulseSetupSectionTitle");
			auto *grid = new PulseSetupChoices::ChoiceGrid(this, 3); choose->addWidget(grid);
			for (const auto &choice : PulseSelectPlan::choices()) if (choice.key.startsWith("select-main/") == (section == 0)) {
				auto *card = new PulseSetupChoices::ChoiceCard(grid, choice.title, choice.detail); card->setProperty("pulseSelectLook", choice.key); card->setAccessibleName("Include " + choice.title); card->setIncludeIndicator(true); grid->addCard(card);
				QObject::connect(card, &QPushButton::toggled, this, [this, key = choice.key](bool checked) { if (checked) options_.selected.insert(key); else options_.selected.remove(key); refresh(); });
			}
			grid->reflow(true);
		}
		auto *advanced = new QPushButton("Use a saved show layout / detailed setup…", this); choose->addWidget(advanced); QObject::connect(advanced, &QPushButton::clicked, this, [this] { advanced_ = true; reject(); });
		pages_[0]->problem = [this] { return options_.selected.isEmpty() ? QString("Select one or more layouts or screens.") : QString(); };
		auto *build = addStep("Set up your pictures, then build", "Choose each input once. It is shared across your selected layouts and both formats.");
		auto *name = new QLineEdit(options_.name, this); name->setMaxLength(60); name->setAccessibleName("Show name"); auto *form = new QFormLayout; form->addRow("Show name", name); build->addLayout(form); QObject::connect(name, &QLineEdit::textChanged, this, [this](const QString &value) { options_.name = value; refresh(); });
		summary_ = PulseSetupInput::text(build, this, "", "PulseSetupMuted");
		for (const auto &role : {QString("presenter"), QString("game"), QString("screen"), QString("activity")}) {
			auto *panel = new QWidget(this); panel->setObjectName("PulseSetupSurface"); build->addWidget(panel); inputRows_.insert(role, panel); panel->setProperty("pulseSelectInput", role);
			auto *body = new QVBoxLayout(panel); body->setContentsMargins(14, 10, 14, 10); inputLabels_.insert(role, PulseSetupInput::text(body, panel, "", "PulseSetupSectionTitle"));
			auto *row = new QHBoxLayout; body->addLayout(row); auto *existing = new QPushButton("Choose existing…", panel), *add = new QPushButton("Add new…", panel), *edit = new QPushButton("Edit draft…", panel), *clear = new QPushButton("Clear", panel); row->addWidget(existing); row->addWidget(add); row->addWidget(edit); row->addStretch(); row->addWidget(clear); inputEdits_.insert(role, edit); inputClears_.insert(role, clear);
			existing->setAccessibleName("Choose existing " + role); add->setAccessibleName("Add new " + role); edit->setAccessibleName("Edit new " + role); clear->setAccessibleName("Clear " + role);
			QObject::connect(existing, &QPushButton::clicked, this, [this, role] { const auto id = pickExisting(this, options_.sources.value(role), false, {}, role); if (!id.isEmpty()) options_.sources[role] = id; refresh(); });
			QObject::connect(add, &QPushButton::clicked, this, [this, role] { addInput(role); }); QObject::connect(edit, &QPushButton::clicked, this, [this, role] { addInput(role, true); }); QObject::connect(clear, &QPushButton::clicked, this, [this, role] { options_.sources.remove(role); refresh(); });
		}
		auto *preview = new QHBoxLayout; auto *previous = new QPushButton("←", this), *next = new QPushButton("→", this); previous->setAccessibleName("Previous selected layout"); next->setAccessibleName("Next selected layout"); previewTitle_ = new QLabel(this); previewTitle_->setWordWrap(true); preview->addWidget(previous); preview->addWidget(previewTitle_, 1); preview->addWidget(next); build->addLayout(preview);
		diagram_ = new PulseGuidedDiagram(this); diagram_->setMinimumHeight(190); build->addWidget(diagram_);
		QObject::connect(previous, &QPushButton::clicked, this, [this] { --previewIndex_; drawPreview(); }); QObject::connect(next, &QPushButton::clicked, this, [this] { ++previewIndex_; drawPreview(); });
		PulseSetupInput::text(build, this, "Arrangement illustration · inactive / new captures use an estimated 16:9 size. Check actual framing after creation.");
		PulseSetupInput::text(build, this, "Customise · optional", "PulseSetupSectionTitle"); auto *custom = new PulseSetupChoices::ChoiceGrid(this, 2); build->addWidget(custom);
		const QStringList titles{"Camera framing & colour", "Stingers & transitions", "Alerts · up to 3 layers", "Screen messages & artwork"};
		for (int i = 0; i < titles.size(); ++i) { auto *button = PulseSetupChoices::card(custom, titles[i], i == 0 ? "Fit or crop, inset size, corner and monochrome" : i == 1 ? "Fade, Cut or your own landscape / portrait stinger" : i == 2 ? "Reuse overlays or paste your provider's browser links" : "Edit the built-in messages or use your browser artwork"); button->setCheckable(false); custom->addCard(button); QObject::connect(button, &QPushButton::clicked, this, [this, i] { if (i == 0) configureFraming(); else if (i == 1) configureTransitions(); else if (i == 2) configureAlerts(); else configureScreens(); }); }
		custom->reflow(true); PulseSetupInput::text(build, this, "Create adds editable Stages and Looks. Existing audio, monitoring and source filters are retained. No broadcast starts here.", "PulseSetupNotice");
		pages_[1]->problem = [this] { return allProblem(); }; QObject::connect(this, &QWizard::currentIdChanged, this, [this](int) { refresh(); }); refresh();
	}
	const PulseSelectPlan::Options &options() const { return options_; }
	QString finalProblem() const { return allProblem(); }
	bool advancedRequested() const { return advanced_; }
};

inline void PulseSelectWizard::configureTransitions() {
		QDialog dialog(this); dialog.setObjectName("PulseGuidedTransitions"); dialog.setProperty("pulseSetupDialog", true); dialog.setWindowTitle("Screen transitions"); PulseSetupInput::fitDialog(dialog, 760, 500);
		auto *layout = new QVBoxLayout(&dialog); PulseSetupInput::text(layout, &dialog, "Between your show screens", "PulseSetupTitle");
		PulseSetupInput::text(layout, &dialog, "Choose transitions between your screens. Movement between Main layouts stays native and separate.");
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

inline bool PulseSelectWizard::editOverlays(const QString &title, QString &horizontal, QString &vertical, QString *scope) {
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
