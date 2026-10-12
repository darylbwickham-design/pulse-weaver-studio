#pragma once

#include "pulse-show-templates.hpp"
#include "pulse-source-canvas.hpp"
#include "pulse-setup-stingers.hpp"
#include <obs-frontend-api.h>
#include <obs.hpp>
#include <QCheckBox>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFontMetrics>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QMap>
#include <QPainter>
#include <QPushButton>
#include <QSet>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTabWidget>
#include <QUrl>
#include <QUuid>
#include <QVBoxLayout>
#include <QVector>
#include <QWizard>
#include <functional>

class FlexibleLayoutDiagram final : public QWidget {
	QPixmap diagram;
public:
	explicit FlexibleLayoutDiagram(QWidget *parent = nullptr) : QWidget(parent) {
		setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
		setAccessibleName("Landscape and portrait layout diagram");
	}
	QSize sizeHint() const override { return {560, 300}; }
	QSize minimumSizeHint() const override { return {240, 130}; }
	void setDiagram(const QPixmap &value) { diagram = value; update(); }
protected:
	void paintEvent(QPaintEvent *) override {
		if (diagram.isNull()) return;
		const QSize fitted = diagram.size().scaled(size(), Qt::KeepAspectRatio);
		QPainter painter(this); painter.setRenderHint(QPainter::SmoothPixmapTransform);
		painter.drawPixmap(QRect(QPoint((width() - fitted.width()) / 2, (height() - fitted.height()) / 2), fitted), diagram);
	}
};

// UI state only. Creation uses the same validated, transactional native builder
// as the previous wizard, without editing any existing source configuration.
class FlexibleSetup {
	struct LookFields {
		QCheckBox *selected, *mainFill, *supportFill;
		QComboBox *composition, *corner;
		QSpinBox *portraitShare;
	};
	struct Question {
		QString group, role, canvas, lookId;
		QString requiredKind;
		QWidget *panel;
		QComboBox *choice, *scope = nullptr, *placement = nullptr;
		QLineEdit *url = nullptr;
	};
	QMap<QString, LookFields> looks;
	QMap<QString, QJsonObject> draftedSources;
	int nextSource = 1;
	struct SavedFields { QComboBox *preset; QJsonObject stage; QJsonArray actions; QMap<QString, QCheckBox *> selected; };
	QMap<QString, SavedFields> saved;
	QJsonArray optionalSources;
	std::vector<Question> inventory;
	QSet<QString> activeGroups;
	QVector<int> activeQuestions;
	int sourceIndex = 0;
	QLabel *title, *summary;
	FlexibleLayoutDiagram *picture, *lookPicture;
	QStackedWidget *questions;
	QPushButton *previous, *next;
	QComboBox *review, *lookReview, *sourceReview;
	struct StageTransitions { QComboBox *horizontal, *vertical; QSpinBox *horizontalDuration, *verticalDuration; };
	QMap<QString, StageTransitions> stageTransitions;
	QJsonObject stingerDrafts;
	QSet<QString> existingTransitionNames;
	QSpinBox *duration;
	bool usesSaved(const QString &group) const { return saved.contains(group) && saved[group].preset->currentIndex() == 1; }
	QJsonArray savedIds(const QString &group) const {
		QJsonArray ids; const auto &s = saved[group];
		for (const auto &entry : s.actions) if (s.selected.value(entry.toObject().value("id").toString())->isChecked()) ids.append(entry.toObject().value("id"));
		return ids;
	}

	QStringList selectedRoles(const PulseShow::Theme &group) const {
		QJsonArray ids;
		for (const auto &look : group.looks) if (looks.value(group.id + "/" + look.id).selected->isChecked()) ids.append(look.id);
		return PulseShow::roles(group, ids);
	}
	const Question *question(const QString &group, const QString &role, const QString &canvas = {}, const QString &lookId = {}) const {
		const QString route = role == "graphic" && canvas.isEmpty() ? "horizontal" : canvas;
		for (const auto &q : inventory) if (q.group == group && q.role == role && q.canvas == route &&
			(role != "graphic" || q.lookId == lookId)) return &q;
		return nullptr;
	}
	QJsonObject framing(const PulseShow::Theme &group, const PulseShow::Look &look) const {
		const auto &f = looks[group.id + "/" + look.id];
		return {{"composition", f.composition->currentData().toString()}, {"corner", f.corner->currentData().toString()},
			{"divider", f.portraitShare->value() / 100.}, {"durationMs", duration->value()},
			{look.main + "Fill", f.mainFill->isChecked()}, {look.support + "Fill", f.supportFill->isChecked()}};
	}
	bool supporting(const PulseShow::Theme &group, const PulseShow::Look &look) const {
		const auto *q = question(group.name, look.support, {}, look.id);
		return q && !q->choice->currentData().toString().isEmpty();
	}
	void showQuestion() {
		questions->setVisible(!activeQuestions.isEmpty());
		sourceReview->setVisible(!activeQuestions.isEmpty());
		if (!activeQuestions.isEmpty()) {
			sourceIndex = qBound(0, sourceIndex, int(activeQuestions.size()) - 1);
			questions->setCurrentWidget(inventory[activeQuestions[sourceIndex]].panel);
			title->setText(QString("Source %1 of %2 · %3").arg(sourceIndex + 1).arg(activeQuestions.size())
				.arg(inventory[activeQuestions[sourceIndex]].choice->accessibleName()));
			sourceReview->blockSignals(true); sourceReview->setCurrentIndex(sourceIndex); sourceReview->blockSignals(false);
		}
		else title->setText("No sources to assign. Choose at least one Look on page 3.");
		previous->setEnabled(sourceIndex > 0);
		next->setEnabled(sourceIndex + 1 < activeQuestions.size());
	}
	bool fullCanvasArtwork(const Question *q) const {
		return q && q->placement &&
			!q->choice->currentData().toString().isEmpty() && q->placement->currentData().toBool();
	}
	QSet<QString> overlayTargets(const QString &scope) const {
		QSet<QString> targets;
		for (const auto &group : PulseShow::setupCatalogue()) if (activeGroups.contains(group.name)) {
			QStringList keys;
			if (usesSaved(group.name)) for (const auto &id : savedIds(group.name)) keys.append(group.id + "/saved:" + id.toString());
			else for (const auto &look : group.looks) if (looks.value(group.id + "/" + look.id).selected->isChecked()) keys.append(group.id + "/" + look.id);
			for (const auto &key : keys) if (scope.isEmpty() || scope == group.name || scope == key) targets.insert(key);
		}
		return targets;
	}
	void createSource(QWizard &owner, QComboBox *selection, const QString &label, const QString &requiredKind, bool edit) {
		const QString selected = selection->currentData().toString();
		const QString key = edit ? selected.mid(4) : "wizard-source-" + QString::number(nextSource);
		const QJsonObject prior = edit ? draftedSources.value(key) : QJsonObject();
		QDialog dialog(&owner); dialog.setWindowTitle(edit ? "Edit new source" : "Create new source");
		dialog.resize(640, 480);
		if (const auto *screen = owner.screen()) {
			const auto available = screen->availableGeometry().size();
			dialog.resize(qMin(640, available.width() - 40), qMin(480, available.height() - 60));
		}
		auto *outer = new QVBoxLayout(&dialog);
		auto *scroll = new QScrollArea(&dialog); scroll->setWidgetResizable(true); outer->addWidget(scroll, 1);
		auto *fields = new QWidget(scroll); auto *layout = new QVBoxLayout(fields); layout->setAlignment(Qt::AlignTop); scroll->setWidget(fields);
		auto *form = new QFormLayout; form->setRowWrapPolicy(QFormLayout::WrapLongRows); layout->addLayout(form);
		auto *name = new QLineEdit(prior.value("name").toString(label), &dialog);
		name->setMaxLength(120); name->setAccessibleName("New source name"); form->addRow("Source name", name);
		auto *kind = new QComboBox(&dialog); kind->setAccessibleName("New source type");
		for (const auto &entry : {std::pair{"Camera", "dshow_input"}, {"Display capture", "monitor_capture"},
			{"Window capture", "window_capture"}, {"Game capture", "game_capture"}, {"Video / media file", "ffmpeg_source"},
			{"Image file", "image_source"}, {"Browser link", "browser_source"}})
			if (requiredKind.isEmpty() || requiredKind == entry.second) kind->addItem(entry.first, entry.second);
		form->addRow("Source type", kind);
		auto *target = new QComboBox(&dialog); target->setAccessibleName("New source device or window");
		target->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon); target->setMinimumContentsLength(18);
		form->addRow("Device / window", target);
		auto *fileRow = new QWidget(&dialog); auto *fileLayout = new QHBoxLayout(fileRow); fileLayout->setContentsMargins(0, 0, 0, 0);
		auto *file = new QLineEdit(prior.value("target").toString(), fileRow); file->setAccessibleName("New source file");
		auto *browse = new QPushButton("Browse…", fileRow); fileLayout->addWidget(file, 1); fileLayout->addWidget(browse);
		form->addRow("File", fileRow);
		auto *loop = new QCheckBox("Loop video", &dialog); loop->setChecked(prior.value("loop").toBool(true)); form->addRow(loop);
		auto *url = new QLineEdit(prior.value("url").toString(), &dialog); url->setPlaceholderText("https://…");
		url->setAccessibleName("New source browser URL"); form->addRow("Browser URL", url);
		auto *hint = new QLabel("This source is created only when you create your show. You can reuse it in other stage groups. Reuse an existing camera input when it already uses the same device.", &dialog);
		hint->setWordWrap(true); layout->addWidget(hint);
		auto refresh = [&] {
			const QString type = kind->currentData().toString();
			const bool localFile = type == "ffmpeg_source" || type == "image_source", browser = type == "browser_source";
			target->setVisible(!localFile && !browser); form->labelForField(target)->setVisible(!localFile && !browser);
			fileRow->setVisible(localFile); form->labelForField(fileRow)->setVisible(localFile);
			loop->setVisible(type == "ffmpeg_source"); url->setVisible(browser); form->labelForField(url)->setVisible(browser);
			target->clear(); if (localFile || browser) return;
			if (type == "game_capture") target->addItem("Any fullscreen game", "");
			obs_properties_t *properties = obs_get_source_properties(type.toUtf8().constData());
			if (!properties) return;
			const char *field = type == "dshow_input" ? "video_device_id" : type == "monitor_capture" ? "monitor_id" : "window";
			obs_property_t *list = obs_properties_get(properties, field);
			if (!list && type == "monitor_capture") list = obs_properties_get(properties, "monitor");
			if (list) for (size_t i = 0; i < obs_property_list_item_count(list); ++i) {
				if (obs_property_list_item_disabled(list, i)) continue;
				const QString id = obs_property_list_format(list) == OBS_COMBO_FORMAT_INT ?
					QString::number(obs_property_list_item_int(list, i)) : QString::fromUtf8(obs_property_list_item_string(list, i));
				if (!id.isEmpty()) target->addItem(obs_property_list_item_name(list, i), id);
			}
			obs_properties_destroy(properties);
		};
		QObject::connect(kind, &QComboBox::currentIndexChanged, &dialog, [&](int) { refresh(); });
		kind->setCurrentIndex(qMax(0, kind->findData(prior.value("kind").toString()))); refresh();
		kind->setEnabled(!edit && requiredKind.isEmpty());
		if (!prior.isEmpty() && target->findData(prior.value("target")) >= 0) target->setCurrentIndex(target->findData(prior.value("target")));
		QObject::connect(browse, &QPushButton::clicked, &dialog, [&] {
			const QString selectedFile = QFileDialog::getOpenFileName(&dialog, "Choose source file", file->text(),
				kind->currentData() == "image_source" ? "Images (*.png *.jpg *.jpeg *.webp *.gif *.bmp);;All files (*)" :
				"Media (*.mp4 *.webm *.mov *.mkv *.avi *.m4v);;All files (*)");
			if (!selectedFile.isEmpty()) file->setText(selectedFile);
		});
		auto *error = new QLabel(&dialog); error->setWordWrap(true); layout->addWidget(error);
		auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog); outer->addWidget(buttons);
		QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
		QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
			const QString n = name->text().trimmed(), type = kind->currentData().toString();
			OBSSourceAutoRelease existing = obs_get_source_by_name(n.toUtf8().constData());
			if (n.isEmpty() || n.size() > 120 || existing) { error->setText("Choose an unused source name (1–120 characters)."); return; }
			for (auto it = draftedSources.cbegin(); it != draftedSources.cend(); ++it)
				if (it.key() != key && it.value().value("name").toString() == n) { error->setText("Another new source uses that name."); return; }
			QString chosenTarget = target->currentData().toString();
			if (type == "ffmpeg_source" || type == "image_source") {
				const QFileInfo chosen(file->text().trimmed());
				if (!chosen.isFile() || !chosen.isReadable()) { error->setText("Choose a readable local file."); return; }
				chosenTarget = chosen.absoluteFilePath();
			} else if (type == "browser_source") {
				const QString problem = PulseShow::browserLinkProblem(url->text());
				if (!problem.isEmpty()) { error->setText(problem); return; }
				if (!pulseWizardOverlayUrlAllowed(url->text())) { error->setText("Use a Lumia overlay or plugin browser link for your show."); return; }
			} else if (type != "game_capture" && chosenTarget.isEmpty()) { error->setText("Choose an available device or window."); return; }
			draftedSources.insert(key, {{"name", n}, {"kind", type}, {"target", chosenTarget}, {"url", url->text().trimmed()}, {"loop", loop->isChecked()}});
			if (!edit) ++nextSource;
			for (const auto &q : inventory) if (q.requiredKind.isEmpty() || q.requiredKind == type) {
				const int index = q.choice->findData("new:" + key);
				if (index >= 0) q.choice->setItemText(index, "New: " + n); else q.choice->addItem("New: " + n, "new:" + key);
			}
			selection->setCurrentIndex(selection->findData("new:" + key));
			dialog.accept(); draw(); if (changed) changed();
		});
		dialog.exec();
	}
	void sourceActions(QWizard &owner, Question &q, QVBoxLayout *layout, const QString &label) {
		const QSet<QString> creatable{"dshow_input", "monitor_capture", "window_capture", "game_capture",
			"ffmpeg_source", "image_source", "browser_source"};
		if (!q.requiredKind.isEmpty() && !creatable.contains(q.requiredKind)) return;
		auto *row = new QHBoxLayout;
		auto *create = new QPushButton("Create new…", q.panel), *edit = new QPushButton("Edit new…", q.panel);
		create->setAccessibleName(label + " create new source"); edit->setAccessibleName(label + " edit new source");
		row->addWidget(create); row->addWidget(edit); row->addStretch(); layout->addLayout(row);
		edit->hide();
		QObject::connect(create, &QPushButton::clicked, &owner, [this, &owner, choice = q.choice, label, requiredKind = q.requiredKind] {
			createSource(owner, choice, label, requiredKind, false);
		});
		QObject::connect(edit, &QPushButton::clicked, &owner, [this, &owner, choice = q.choice, label, requiredKind = q.requiredKind] {
			createSource(owner, choice, label, requiredKind, true);
		});
		QObject::connect(q.choice, &QComboBox::currentIndexChanged, &owner, [this, edit, choice = q.choice](int) {
			edit->setVisible(draftedSources.contains(choice->currentData().toString().mid(4)));
		});
	}
public:
	QTabWidget *tabs;
	QWidget *sourcePanel;
	std::function<void()> changed;
	FlexibleSetup(QWizard &owner, QWidget *lookPage, QVBoxLayout *lookLayout, QWidget *sourcePage, QVBoxLayout *sourceLayout,
		const QJsonObject &manifest, const QJsonArray &stages, const QJsonArray &actions)
		{
		optionalSources = manifest.value("optionalSources").toArray();
		QMap<QString, QString> stingers;
		obs_frontend_source_list transitionSources{};
		obs_frontend_get_transitions(&transitionSources);
		for (size_t index = 0; index < transitionSources.sources.num; ++index) {
			auto *source = transitionSources.sources.array[index];
			if (source) existingTransitionNames.insert(QString::fromUtf8(obs_source_get_name(source)));
			if (source && QString::fromUtf8(obs_source_get_id(source)) == "obs_stinger_transition")
				stingers.insert(QString::fromUtf8(obs_source_get_name(source)), "stinger:" + QString::fromUtf8(obs_source_get_name(source)));
		}
		obs_frontend_source_list_free(&transitionSources);
		auto text = [](QVBoxLayout *layout, QWidget *parent, const QString &message) {
			auto *label = new QLabel(message, parent); label->setWordWrap(true); layout->addWidget(label); return label;
		};
		lookReview = new QComboBox(lookPage); lookReview->setAccessibleName("Look layout to preview"); lookLayout->addWidget(lookReview);
		lookPicture = new FlexibleLayoutDiagram(lookPage); lookLayout->addWidget(lookPicture);
		text(lookLayout, lookPage, "Preview placement here. Assign real feeds and artwork on page 4; these diagrams do not play your sources.");
		tabs = new QTabWidget(lookPage); lookLayout->addWidget(tabs);
		for (const auto &group : PulseShow::setupCatalogue()) {
			auto *tab = new QWidget(tabs); auto *layout = new QVBoxLayout(tab); layout->setAlignment(Qt::AlignTop); tabs->addTab(tab, group.name);
			SavedFields s{}; s.preset = new QComboBox(tab); s.preset->setAccessibleName(group.name + " layout preset");
			s.preset->addItem("Choose layout options", ""); layout->addWidget(s.preset);
			const auto config = manifest.value("groups").toObject().value(group.name).toObject();
			for (const auto &entry : stages) if (entry.toObject().value("name") == config.value("reference")) s.stage = entry.toObject();
			for (const auto &entry : actions) { const auto action = entry.toObject();
				if (!s.stage.isEmpty() && action.value("kind").toString() == "layout" && !action.value("draft").toBool() &&
					action.value("container") == s.stage.value("horizontal") &&
					(!config.contains("looks") || config.value("looks").toArray().contains(action.value("id")))) s.actions.append(action);
			}
			auto *savedPanel = new QWidget(tab); savedPanel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
			auto *savedLayout = new QVBoxLayout(savedPanel); savedLayout->setAlignment(Qt::AlignTop); layout->addWidget(savedPanel); savedPanel->hide();
			if (!s.actions.isEmpty()) {
				s.preset->addItem("Saved layout: " + s.stage.value("name").toString(), "saved");
				text(savedLayout, savedPanel, "Keep this layout's camera treatments, artwork, framing and saved movement in both formats. Choose its operating Looks below, then assign feeds on the next page.");
				for (const auto &entry : s.actions) { const auto action = entry.toObject(); const QString id = action.value("id").toString();
					QString label = action.value("name").toString(); label.remove(0, s.stage.value("name").toString().size() + 3);
					auto *check = new QCheckBox(label, savedPanel); check->setChecked(true); savedLayout->addWidget(check); s.selected.insert(id, check);
					QObject::connect(check, &QCheckBox::toggled, &owner, [this](bool) { update(activeGroups); if (changed) changed(); });
				}
			}
			saved.insert(group.name, s);
			auto *transitionCard = new QGroupBox("When entering this stage", tab);
			auto *transitionLayout = new QFormLayout(transitionCard); layout->addWidget(transitionCard);
			StageTransitions policy{};
			auto selector = [&](const QString &route, QSpinBox *&time) {
				auto *combo = new QComboBox(transitionCard); combo->setAccessibleName(group.name + " " + route + " stage transition");
				combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon); combo->setMinimumContentsLength(18);
				combo->addItem("Keep saved transition / use Fade for a new layout", "");
				combo->addItem("Fade", "fade"); combo->addItem("Cut", "cut");
				for (auto it = stingers.cbegin(); it != stingers.cend(); ++it) combo->addItem("Stinger · " + it.key(), it.value());
				time = new QSpinBox(transitionCard); time->setRange(0, 3000); time->setValue(500); time->setSuffix(" ms");
				time->setAccessibleName(group.name + " " + route + " fade duration"); time->setEnabled(false);
				auto *row = new QWidget(transitionCard); auto *rowLayout = new QHBoxLayout(row); rowLayout->setContentsMargins(0, 0, 0, 0);
				rowLayout->addWidget(combo, 1);
				auto *add = new QPushButton("Add stinger…", row); add->setAccessibleName(group.name + " " + route + " add stinger"); rowLayout->addWidget(add);
				auto *edit = new QPushButton("Edit…", row); edit->setAccessibleName(group.name + " " + route + " edit new stinger"); edit->setEnabled(false); rowLayout->addWidget(edit);
				transitionLayout->addRow(route == "horizontal" ? "Landscape transition" : "Portrait transition", row);
				transitionLayout->addRow("Fade duration", time);
				QObject::connect(combo, &QComboBox::currentIndexChanged, &owner, [this, combo, time, edit](int) {
					time->setEnabled(combo->currentData().toString() == "fade");
					edit->setEnabled(combo->currentData().toString().startsWith("new-stinger:"));
					if (changed) changed();
				});
				auto configure = [this, combo, route, &owner](bool editing) {
					const QString id = editing ? combo->currentData().toString().mid(12) : QUuid::createUuid().toString(QUuid::WithoutBraces);
					const auto prior = editing ? stingerDrafts.value(id).toObject() : QJsonObject();
					QSet<QString> names = existingTransitionNames;
					for (auto it = stingerDrafts.constBegin(); it != stingerDrafts.constEnd(); ++it) if (it.key() != id) names.insert(it.value().toObject().value("name").toString());
					const auto spec = PulseSetupStingers::ask(&owner, route == "vertical", names, prior);
					if (spec.isEmpty()) return;
					const QString choice = "new-stinger:" + id;
					stingerDrafts.insert(id, spec);
					for (const auto &policy : stageTransitions) for (auto *selector : {policy.horizontal, policy.vertical}) {
						const QString label = "New stinger · " + spec.value("name").toString();
						const int index = selector->findData(choice);
						if (index >= 0) selector->setItemText(index, label); else selector->addItem(label, choice);
					}
					combo->setCurrentIndex(combo->findData(choice));
					if (changed) changed();
				};
				QObject::connect(add, &QPushButton::clicked, &owner, [configure] { configure(false); });
				QObject::connect(edit, &QPushButton::clicked, &owner, [configure] { configure(true); });
				return combo;
			};
			policy.horizontal = selector("horizontal", policy.horizontalDuration);
			policy.vertical = selector("vertical", policy.verticalDuration); stageTransitions.insert(group.name, policy);
			auto *transitionHint = new QLabel("Use an existing transition or add a stinger video for each format. New stingers are created with your show and can be reused by other stages. A stinger uses its own cut point; movement between Looks is separate.", transitionCard);
			transitionHint->setWordWrap(true); transitionLayout->addRow(transitionHint);
			auto *optionsPanel = new QWidget(tab); auto *optionsLayout = new QVBoxLayout(optionsPanel); layout->addWidget(optionsPanel);
			QObject::connect(s.preset, &QComboBox::currentIndexChanged, &owner, [this, savedPanel, optionsPanel](int index) {
				savedPanel->setVisible(index == 1); optionsPanel->setVisible(index == 0); update(activeGroups); if (changed) changed(); });
			layout = optionsLayout;
			text(layout, tab, "Choose operating Looks, then set their layout options. Feed A, B and C are slots for this stage group; each can be a camera, capture or media source.");
			for (const auto &look : group.looks) {
				auto *card = new QGroupBox(tab); auto *body = new QVBoxLayout(card); layout->addWidget(card);
				LookFields f{};
				f.selected = new QCheckBox(look.name, card);
				f.selected->setChecked(look.id == group.looks.front().id || (group.name == "Intermission" && look.id == "ending"));
				body->addWidget(f.selected);
				text(body, card, look.description);
				QString requirements = "Required: " + PulseShow::roleName(look.main);
				if (!look.support.isEmpty()) requirements += QString(look.optionalPresenter ? ". Optional: " : " and ") + PulseShow::roleName(look.support);
				text(body, card, requirements + ".");
				auto *options = new QWidget(card); auto *form = new QFormLayout(options); body->addWidget(options);
				options->setEnabled(f.selected->isChecked());
				f.composition = new QComboBox(options); f.composition->addItem("Recommended layout", "");
				f.composition->addItem("Main view with corner inset", "corner"); f.composition->addItem("Supporting side column", "rail");
				f.composition->addItem("Side by side", "split");
				f.corner = new QComboBox(options);
				for (const auto &entry : {std::pair{"Top left", "top-left"}, {"Top right", "top-right"}, {"Bottom left", "bottom-left"}, {"Bottom right", "bottom-right"}})
					f.corner->addItem(entry.first, entry.second);
				f.portraitShare = new QSpinBox(options); f.portraitShare->setRange(15, 65); f.portraitShare->setValue(30); f.portraitShare->setSuffix("%");
				f.mainFill = new QCheckBox("Crop main feed to fill (off: fit whole image)", options);
				f.supportFill = new QCheckBox("Crop supporting feed to fill (off: fit whole image)", options);
				f.composition->setAccessibleName(group.name + " · " + look.name + " landscape composition");
				f.corner->setAccessibleName(group.name + " · " + look.name + " inset corner");
				f.portraitShare->setAccessibleName(group.name + " · " + look.name + " portrait top panel size");
				if (!look.support.isEmpty()) { form->addRow("Landscape composition", f.composition); form->addRow("Inset corner", f.corner);
					form->addRow("Portrait top panel size", f.portraitShare); }
				else { f.composition->hide(); f.corner->hide(); f.portraitShare->hide(); }
				if (!PulseShow::graphic(look.main)) form->addRow(f.mainFill);
				else f.mainFill->hide();
				if (!look.support.isEmpty() && !PulseShow::graphic(look.support)) form->addRow(f.supportFill);
				else f.supportFill->hide();
				auto optionState = [f, recommended = look.layout] {
					const QString composition = f.composition->currentData().toString().isEmpty() ? recommended : f.composition->currentData().toString();
					f.corner->setEnabled(composition == "corner");
					f.portraitShare->setEnabled(composition != "footer" && composition != "label");
				};
				optionState();
				looks.insert(group.id + "/" + look.id, f);
				QObject::connect(f.selected, &QCheckBox::toggled, &owner, [this, options](bool checked) { options->setEnabled(checked); update(activeGroups); if (changed) changed(); });
				auto previewLook = [this, key = group.id + "/" + look.id] {
					const int index = review->findData(key); if (index >= 0) review->setCurrentIndex(index); draw();
				};
				for (auto *combo : {f.composition, f.corner}) QObject::connect(combo, &QComboBox::currentIndexChanged, &owner, [previewLook](int) { previewLook(); });
				QObject::connect(f.composition, &QComboBox::currentIndexChanged, &owner, [optionState](int) { optionState(); });
				QObject::connect(f.portraitShare, &QSpinBox::valueChanged, &owner, [previewLook](int) { previewLook(); });
				for (auto *check : {f.mainFill, f.supportFill}) QObject::connect(check, &QCheckBox::toggled, &owner, [previewLook](bool) { previewLook(); });
			}
			layout->addStretch();
		}
		sourcePanel = new QWidget(sourcePage); auto *layout = new QVBoxLayout(sourcePanel); sourceLayout->addWidget(sourcePanel);
		title = text(layout, sourcePanel, "Assign your feeds");
		sourceReview = new QComboBox(sourcePanel); sourceReview->setAccessibleName("Source to assign"); layout->addWidget(sourceReview);
		questions = new QStackedWidget(sourcePanel); layout->addWidget(questions);
		QObject::connect(sourceReview, &QComboBox::currentIndexChanged, &owner, [this](int index) {
			if (index >= 0) { sourceIndex = index; showQuestion(); }
		});
		auto *navigation = new QHBoxLayout; previous = new QPushButton("Previous source", sourcePanel); next = new QPushButton("Next source", sourcePanel);
		navigation->addWidget(previous); navigation->addWidget(next); navigation->addStretch(); layout->addLayout(navigation);
		QObject::connect(previous, &QPushButton::clicked, &owner, [this] { --sourceIndex; showQuestion(); });
		QObject::connect(next, &QPushButton::clicked, &owner, [this] { ++sourceIndex; showQuestion(); });
		auto addQuestion = [&](const QString &group, const QString &role, const QString &canvas = QString(),
			const QString &lookId = QString(), const QString &lookName = QString()) {
			Question q{}; q.group = group; q.role = role; q.canvas = canvas; q.lookId = lookId; q.panel = new QWidget(questions); auto *body = new QVBoxLayout(q.panel);
			const QString label = group == "Overlay" ? (role.startsWith("horizontal") ? "Landscape overlay " : "Portrait overlay ") + role.section('/', 1) :
				group + " · " + (role == "graphic" ? lookName + " · " + (canvas == "vertical" ? "Portrait artwork" : "Landscape artwork") : PulseShow::roleName(role));
			text(body, q.panel, label);
			q.choice = new QComboBox(q.panel); q.choice->setAccessibleName(label);
			const bool overlay = group == "Overlay";
			const bool artwork = overlay || role == "graphic" || role == "chat";
			q.choice->addItem(role == "graphic" ? "Choose your Lumia overlay…" : "None / skip when optional", "");
			if (artwork) q.requiredKind = "browser_source";
			const int orientation = overlay ? (role.startsWith("horizontal") ? 0 : 1) :
				canvas.isEmpty() ? -1 : canvas == "vertical" ? 1 : 0;
			struct Scan { QComboBox *choice; int orientation; bool artwork; } scan{q.choice, orientation, artwork};
			obs_enum_sources([](void *opaque, obs_source_t *source) {
				auto &scan = *static_cast<Scan *>(opaque);
				if (!(obs_source_get_output_flags(source) & OBS_SOURCE_VIDEO) || obs_scene_from_source(source) || obs_group_from_source(source)) return true;
				if (pulseIsNativeOverlaySource(source)) return true;
				if (scan.artwork && !pulseWizardOverlaySourceAllowed(source)) return true;
				if (scan.orientation >= 0 && !pulseBrowserMatchesCanvas(source, scan.orientation == 1)) return true;
				scan.choice->addItem(obs_source_get_name(source), obs_source_get_uuid(source));
				return true;
			}, &scan);
			body->addWidget(q.choice);
			sourceActions(owner, q, body, label);
			if (!role.startsWith("feed")) {
				q.choice->addItem("Add a Lumia browser link", "url"); q.url = new QLineEdit(q.panel);
				q.url->setPlaceholderText("https://…"); q.url->setAccessibleName(label + " browser URL"); body->addWidget(q.url);
			}
			if (role == "graphic") {
				if (lookId == "countdown") text(body, q.panel, "Set the countdown duration and its command at zero in Lumia. The Lumia bulb on Show Control explains how to start it and change Look when it finishes.");
				q.placement = new QComboBox(q.panel); q.placement->setAccessibleName(label + " placement");
				q.placement->addItem("Full canvas overlay", true); q.placement->addItem("Use layout panel", false);
				auto *placementHint = text(body, q.panel, "Imported artwork normally covers the whole canvas. Use the layout panel for artwork designed to sit beside a feed.");
				body->addWidget(q.placement);
				auto placementState = [choice = q.choice, placement = q.placement, placementHint] {
					const QString id = choice->currentData().toString();
					const bool external = !id.isEmpty();
					placement->setVisible(external); placementHint->setVisible(external);
				};
				placementState(); QObject::connect(q.choice, &QComboBox::currentIndexChanged, &owner, [placementState](int) { placementState(); });
				QObject::connect(q.placement, &QComboBox::currentIndexChanged, &owner, [this](int) { draw(); });
				text(body, q.panel, "This artwork belongs only to " + lookName + ". Import its matching Lumia template, then choose its browser source or paste its link. Other Looks have their own artwork questions."); }
			if (overlay) {
				q.scope = new QComboBox(q.panel); q.scope->setAccessibleName(label + " visibility");
				q.scope->addItem("Every Look in this show", "");
				text(body, q.panel, "Three shared alert / overlay layers are available in each format. Layer 1 is lowest and layer 3 is on top. Choose an existing source or add a new one; skip any layer you do not need. Each source is reused across the Looks you select.");
				if (role.section('/', 1) == "2") {
					text(body, q.panel, "Add Chat Box Physics here? Install Chat Box Physics from Lumia's plugin marketplace and enable it, then select its browser source or paste its link. Use the matching layer in both formats. Installation and movement instructions are behind the Lumia bulb on Show Control.");
					auto *physicsPlugin = new QPushButton("Find Chat Box Physics in Lumia", q.panel); body->addWidget(physicsPlugin);
					QObject::connect(physicsPlugin, &QPushButton::clicked, &owner, [] { QDesktopServices::openUrl(QUrl("https://lumiastream.com/plugins#chat_physics_local")); });
					const bool portrait = role.startsWith("vertical");
					text(body, q.panel, portrait ? "Default portrait link: http://127.0.0.1:39270/portrait · browser size 1080 × 1920." : "Default landscape link: http://127.0.0.1:39270/ · browser size 1920 × 1080.");
					auto *usePhysics = new QPushButton("Use default Chat Box Physics link", q.panel); body->addWidget(usePhysics);
					usePhysics->setAccessibleName(label + " use Chat Box Physics link");
					QObject::connect(usePhysics, &QPushButton::clicked, &owner, [choice = q.choice, url = q.url, portrait] {
						url->setText(portrait ? "http://127.0.0.1:39270/portrait" : "http://127.0.0.1:39270/");
						choice->setCurrentIndex(choice->findData("url"));
					});
					text(body, q.panel, "Use the plugin's configured port if changed. Keep its API token in Lumia plugin settings. Skip this slot if you do not want Chat Box Physics, or use it for another Lumia layer.");
				}
				text(body, q.panel, "Show this full-canvas layer in"); body->addWidget(q.scope);
				text(body, q.panel, "Use matching landscape and portrait links. New browser layers start with audio off; enable sound on one copy later in Camera if needed. Commands are imported once in Lumia.");
				QObject::connect(q.scope, &QComboBox::currentIndexChanged, &owner, [this](int) { draw(); if (changed) changed(); });
			} else if (!artwork) text(body, q.panel, "Choose an existing source or create a new camera, capture, media, image or browser input. New sources can be reused in other stage groups. This group's feeds stay independent of the other groups; optional supporting feeds can be skipped.");
			questions->addWidget(q.panel); inventory.push_back(q);
			if (q.url) q.url->hide();
			QObject::connect(q.choice, &QComboBox::currentIndexChanged, &owner, [this, url = q.url, choice = q.choice](int) {
				if (url) url->setVisible(choice->currentData().toString() == "url");
				draw(); if (changed) changed();
			});
			if (q.url) QObject::connect(q.url, &QLineEdit::textChanged, &owner, [this](const QString &) { if (changed) changed(); });
		};
		for (const auto &g : PulseShow::setupCatalogue()) {
			QJsonArray ids; for (const auto &look : g.looks) ids.append(look.id);
			for (const auto &role : PulseShow::roles(g, ids)) {
				if (role == "graphic") {
					for (const auto &look : g.looks) if (look.main == "graphic" || look.support == "graphic") {
						addQuestion(g.name, role, "horizontal", look.id, look.name);
						addQuestion(g.name, role, "vertical", look.id, look.name);
					}
				} else addQuestion(g.name, role);
			}
			QSet<QString> seen; QStringList sources;
			std::function<void(obs_source_t *)> scan;
			scan = [&](obs_source_t *source) {
				if (!source) return; const QString uuid = obs_source_get_uuid(source); if (seen.contains(uuid)) return; seen.insert(uuid);
				auto *scene = obs_group_from_source(source); if (!scene) scene = obs_scene_from_source(source);
				if (scene) { obs_scene_enum_items(scene, [](obs_scene_t *, obs_sceneitem_t *item, void *opaque) {
					(*static_cast<std::function<void(obs_source_t *)> *>(opaque))(obs_sceneitem_get_source(item)); return true;
				}, &scan); return; }
				const QString type = obs_source_get_unversioned_id(source);
				if (type == "dshow_input" || type == "game_capture" || type == "window_capture" || type == "monitor_capture" ||
					type == "ffmpeg_source" || type.contains("ndi", Qt::CaseInsensitive) || optionalSources.contains(uuid) || pulseIsNativeOverlaySource(source)) sources.append(uuid);
			};
			for (const QString &route : {QString("horizontal"), QString("vertical")}) {
				const auto name = saved[g.name].stage.value(route).toString(); OBSSourceAutoRelease source = obs_get_source_by_name(name.toUtf8().constData());
				OBSCanvasAutoRelease canvas = obs_get_canvas_by_name("Pulse Weaver Vertical");
				OBSSceneAutoRelease portrait = canvas ? obs_canvas_get_scene_by_name(canvas, name.toUtf8().constData()) : nullptr;
				scan(source ? source.Get() : portrait ? obs_scene_get_source(portrait) : nullptr);
			}
			for (const QString &uuid : sources) {
				OBSSourceAutoRelease source = obs_get_source_by_uuid(uuid.toUtf8().constData()); if (!source) continue;
				Question q{}; q.group = g.name; q.role = "saved:" + uuid; q.panel = new QWidget(questions); auto *body = new QVBoxLayout(q.panel);
				text(body, q.panel, g.name + " · " + QString::fromUtf8(obs_source_get_name(source)));
				const bool replaceNative = pulseIsNativeOverlaySource(source);
				if (replaceNative) {
					OBSDataAutoRelease settings = obs_source_get_settings(source);
					const int width = obs_data_get_int(settings, "width"), height = obs_data_get_int(settings, "height");
					if (width > 0 && height > 0 && width != height) q.canvas = height > width ? "vertical" : "horizontal";
				}
				text(body, q.panel, replaceNative ? "This saved layout uses older built-in artwork. Choose a Lumia browser source or create a browser link to replace it in your new show." : "Keep the current source to reproduce this saved layout, choose another source of the same type, or create a replacement below. Camera effects and framing are retained.");
				if (!q.canvas.isEmpty()) text(body, q.panel, q.canvas == "vertical" ? "Choose the portrait version of your Lumia overlay." : "Choose the landscape version of your Lumia overlay.");
				q.choice = new QComboBox(q.panel); body->addWidget(q.choice);
				q.choice->setAccessibleName(g.name + " · " + QString::fromUtf8(obs_source_get_name(source)));
				q.requiredKind = obs_source_get_unversioned_id(source);
				sourceActions(owner, q, body, q.choice->accessibleName());
				if (replaceNative) q.choice->addItem("Choose your Lumia overlay…", "");
				else if (optionalSources.contains(uuid)) q.choice->addItem("None / skip this optional widget", "");
				struct Scan { QComboBox *choice; QString type; bool replaceNative; QString canvas; } context{q.choice, obs_source_get_unversioned_id(source), replaceNative, q.canvas};
				obs_enum_sources([](void *opaque, obs_source_t *candidate) { auto &s = *static_cast<Scan *>(opaque);
					if (pulseIsNativeOverlaySource(candidate) || (s.replaceNative && !pulseWizardOverlaySourceAllowed(candidate))) return true;
					if (!s.canvas.isEmpty() && !pulseBrowserMatchesCanvas(candidate, s.canvas == "vertical")) return true;
					if ((obs_source_get_output_flags(candidate) & OBS_SOURCE_VIDEO) && !obs_scene_from_source(candidate) && !obs_group_from_source(candidate) &&
						s.type == obs_source_get_unversioned_id(candidate)) s.choice->addItem(obs_source_get_name(candidate), obs_source_get_uuid(candidate)); return true;
				}, &context);
				q.choice->setCurrentIndex(qMax(0, q.choice->findData(uuid))); questions->addWidget(q.panel); inventory.push_back(q);
				QObject::connect(q.choice, &QComboBox::currentIndexChanged, &owner, [this](int) { if (changed) changed(); });
			}
		}
		for (const QString &route : {QString("horizontal"), QString("vertical")}) for (int i = 1; i <= 3; ++i) addQuestion("Overlay", route + "/" + QString::number(i));
		auto *form = new QFormLayout; duration = new QSpinBox(sourcePanel); duration->setRange(0, 3000); duration->setValue(850); duration->setSuffix(" ms");
		form->addRow("Movement between Looks", duration); layout->addLayout(form);
		auto *stingerLink = new QPushButton("Create a stinger with Profounder", sourcePanel); layout->addWidget(stingerLink);
		QObject::connect(stingerLink, &QPushButton::clicked, &owner, [] { QDesktopServices::openUrl(QUrl("https://profounder.com/transitions/")); });
		text(layout, sourcePanel, "Profounder offers free and paid transition designs. Download your chosen video, then use Add stinger on page 3 to choose the file and its cut point. Assign matching landscape and portrait versions to the stages where you want them. After creating your show, preview and adjust new stingers in Camera's Scene Transitions panel. If hidden, show it through Studio → Docks → Scene Transitions.");
		text(layout, sourcePanel, "For any new Lumia browser link: first import the shared template into your own Lumia Overlays, configure it, then copy its browser link. A marketplace or template share link is not a browser source. Use the matching H/V version and skip widgets you do not need.");
		review = new QComboBox(sourcePanel); review->setAccessibleName("Flexible Look to review"); layout->addWidget(review);
		picture = new FlexibleLayoutDiagram(sourcePanel); layout->addWidget(picture); summary = text(layout, sourcePanel, "");
		text(layout, sourcePanel, "The diagrams show placement in both formats. Saved layouts keep their artwork, camera treatments and movement. Shared alert layers apply to both new and copied layouts. Stage transitions stay saved unless you choose replacements on page 3. Check your actual feeds privately in Control after creation, before starting a broadcast.");
		QObject::connect(review, &QComboBox::currentIndexChanged, &owner, [this](int) { draw(); });
		QObject::connect(lookReview, &QComboBox::currentIndexChanged, &owner, [this](int index) {
			if (index >= 0) review->setCurrentIndex(review->findData(lookReview->itemData(index)));
		});
	}
	void update(const QSet<QString> &groups) {
		const int previousQuestion = activeQuestions.value(sourceIndex, -1);
		activeGroups = groups; activeQuestions.clear(); const QString selected = review->currentData().toString(); review->blockSignals(true); review->clear();
		int tab = 0;
		for (const auto &g : PulseShow::setupCatalogue()) {
			tabs->setTabVisible(tab++, groups.contains(g.name)); if (!groups.contains(g.name)) continue;
			if (usesSaved(g.name)) {
				for (const auto &entry : saved[g.name].actions) { const auto a = entry.toObject(); if (savedIds(g.name).contains(a.value("id")))
					review->addItem(a.value("name").toString(), g.id + "/saved:" + a.value("id").toString()); }
				for (int i = 0; i < int(inventory.size()); ++i) if (inventory[i].group == g.name && inventory[i].role.startsWith("saved:")) activeQuestions.append(i);
				continue;
			}
			for (const auto &look : g.looks) if (looks[g.id + "/" + look.id].selected->isChecked()) review->addItem(g.name + " · " + look.name, g.id + "/" + look.id);
			const auto roles = selectedRoles(g);
			for (int i = 0; i < int(inventory.size()); ++i) if (inventory[i].group == g.name && roles.contains(inventory[i].role) &&
				(inventory[i].lookId.isEmpty() || looks[g.id + "/" + inventory[i].lookId].selected->isChecked())) activeQuestions.append(i);
		}
		bool custom = false; for (const auto &group : groups) if (!usesSaved(group)) custom = true;
		duration->setEnabled(custom);
		for (int i = 0; i < int(inventory.size()); ++i) if (!groups.isEmpty() && inventory[i].group == "Overlay") activeQuestions.append(i);
		for (auto &q : inventory) if (q.scope) {
			const QString scope = q.scope->currentData().toString(), label = q.scope->currentText();
			q.scope->blockSignals(true); q.scope->clear(); q.scope->addItem("Every Look in this show", "");
			for (const auto &g : PulseShow::setupCatalogue()) if (groups.contains(g.name)) {
				q.scope->addItem(g.name + " · all selected Looks", g.name);
				if (usesSaved(g.name)) {
					for (const auto &entry : saved[g.name].actions) { const auto action = entry.toObject();
						if (savedIds(g.name).contains(action.value("id"))) q.scope->addItem(action.value("name").toString(), g.id + "/saved:" + action.value("id").toString());
					}
				} else for (const auto &look : g.looks) if (looks[g.id + "/" + look.id].selected->isChecked())
					q.scope->addItem(g.name + " · " + look.name, g.id + "/" + look.id);
			}
			if (q.scope->findData(scope) < 0) q.scope->addItem(label, scope);
			q.scope->setCurrentIndex(q.scope->findData(scope)); q.scope->blockSignals(false);
		}
		sourceReview->blockSignals(true); sourceReview->clear();
		for (int index : activeQuestions) sourceReview->addItem(inventory[index].choice->accessibleName(), index);
		sourceIndex = qMax(0, activeQuestions.indexOf(previousQuestion)); sourceReview->blockSignals(false);
		lookReview->blockSignals(true); lookReview->clear();
		for (int index = 0; index < review->count(); ++index) lookReview->addItem(review->itemText(index), review->itemData(index));
		lookReview->blockSignals(false);
		review->setCurrentIndex(qMax(0, review->findData(selected))); review->blockSignals(false); showQuestion(); draw();
	}
	QString problem(bool sources) const {
		for (const QString &group : activeGroups) {
			const auto &policy = stageTransitions[group];
			for (auto *selector : {policy.horizontal, policy.vertical}) {
				const QString choice = selector->currentData().toString();
				if (!choice.startsWith("new-stinger:")) continue;
				const auto spec = stingerDrafts.value(choice.mid(12)).toObject();
				const QFileInfo file(spec.value("path").toString());
				if (!file.isAbsolute() || !file.isFile() || !file.isReadable())
					return "Choose a readable video file for " + group + "'s new stinger, or select another transition.";
			}
		}
		for (const auto &g : PulseShow::setupCatalogue()) {
			if (!activeGroups.contains(g.name)) continue; bool any = false;
			if (usesSaved(g.name)) { if (savedIds(g.name).isEmpty()) return "Choose at least one saved Look for " + g.name + "."; continue; }
			for (const auto &look : g.looks) {
				if (!looks.value(g.id + "/" + look.id).selected->isChecked()) continue; any = true;
				if (sources) for (const QString &role : {look.main, look.optionalPresenter ? QString() : look.support}) {
					if (role.isEmpty()) continue;
					const QStringList routes = role == "graphic" ? QStringList{"horizontal", "vertical"} : QStringList{QString()};
					for (const QString &route : routes) {
						const auto *q = question(g.name, role, route, look.id);
						if (!q || q->choice->currentData().toString().isEmpty()) return "Choose " +
							(route.isEmpty() ? PulseShow::roleName(role) : route == "vertical" ? "portrait artwork" : "landscape artwork") +
							" for " + g.name + " · " + look.name + ".";
					}
				}
			}
			if (!any) return "Choose at least one Look for " + g.name + ".";
		}
		if (sources) for (int i : activeQuestions) { const auto &q = inventory[i];
			if (q.role.startsWith("saved:") && q.choice->currentData().toString().isEmpty()) {
				OBSSourceAutoRelease original = obs_get_source_by_uuid(q.role.mid(6).toUtf8().constData());
				if (!optionalSources.contains(q.role.mid(6)) || pulseIsNativeOverlaySource(original))
					return "Choose a replacement source for " + q.choice->accessibleName() + ".";
			}
			if (q.choice->currentData().toString() == "url") {
				const QString error = PulseShow::browserLinkProblem(q.url->text());
				if (!error.isEmpty()) return q.choice->accessibleName() + ": " + error;
				if (!pulseWizardOverlayUrlAllowed(q.url->text())) return q.choice->accessibleName() + ": use a Lumia overlay or plugin browser link.";
			}
			if (q.scope && !q.choice->currentData().toString().isEmpty()) {
				const QString scope = q.scope->currentData().toString(); bool matches = scope.isEmpty();
				for (const auto &g : PulseShow::setupCatalogue()) if (activeGroups.contains(g.name)) {
					if (usesSaved(g.name)) {
						for (const auto &id : savedIds(g.name)) if (scope == g.name || scope == g.id + "/saved:" + id.toString()) matches = true;
					} else for (const auto &look : g.looks) if (looks.value(g.id + "/" + look.id).selected->isChecked() &&
						(scope == g.name || scope == g.id + "/" + look.id)) matches = true;
				}
				if (!matches) return "Choose a selected Look or stage group for " + q.choice->accessibleName() + ", or skip this overlay.";
				const auto targets = overlayTargets(scope);
				for (int previous = 0; previous < i; ++previous) {
					const auto &other = inventory[previous];
					if (!other.scope || other.role.section('/', 0, 0) != q.role.section('/', 0, 0) ||
						other.choice->currentData() != q.choice->currentData()) continue;
					// Separate pasted-link questions have separate draft identities.
					if (q.choice->currentData().toString() == "url") continue;
					for (const auto &target : overlayTargets(other.scope->currentData().toString())) if (targets.contains(target))
						return q.choice->currentText() + " is already used in another shared layer on this canvas for the same Look. Choose a different source or different Looks.";
				}
				for (const auto &g : PulseShow::setupCatalogue()) if (activeGroups.contains(g.name) && usesSaved(g.name)) {
					bool targeted = scope.isEmpty() || scope == g.name;
					for (const auto &id : savedIds(g.name)) if (scope == g.id + "/saved:" + id.toString()) targeted = true;
					if (!targeted) continue;
					QJsonObject bindings; QJsonArray disabled;
					for (const auto &input : inventory) if (input.group == g.name && input.role.startsWith("saved:")) {
						const QString uuid = input.role.mid(6), assignment = input.choice->currentData().toString();
						if (assignment.isEmpty()) disabled.append(uuid); else if (assignment != uuid) bindings.insert(uuid, assignment);
					}
					const QString route = q.role.section('/', 0, 0), name = saved[g.name].stage.value(route).toString();
					OBSSourceAutoRelease scene = obs_get_source_by_name(name.toUtf8().constData());
					if (!scene && route == "vertical") {
						OBSCanvasAutoRelease canvas = obs_get_canvas_by_name("Pulse Weaver Vertical");
						if (canvas) scene = obs_canvas_get_source_by_name(canvas, name.toUtf8().constData());
					}
					if (pulseSceneUsesSource(scene, q.choice->currentData().toString(), bindings, disabled))
						return q.choice->currentText() + " already exists in saved layout “" + saved[g.name].stage.value("name").toString() +
							"”. Leave this shared slot empty, or choose another scope or source.";
				}
			}
		}
		return {};
	}
	QJsonObject draft(const QString &name) const {
		QJsonArray stages, overlays; QJsonObject definitions, selectedStingers;
		auto sourceId = [&](const Question &q) {
			const QString choice = q.choice->currentData().toString(); const QString key = q.group + "/" + q.role +
				(q.lookId.isEmpty() ? QString() : "/" + q.lookId) +
				(q.canvas.isEmpty() ? QString() : "/" + q.canvas);
			if (choice.startsWith("new:") && draftedSources.contains(choice.mid(4))) definitions.insert(choice.mid(4), draftedSources.value(choice.mid(4)));
			if (choice != "url") return choice;
			QJsonObject spec{{"name", name.left(qMax(0, 117 - int(key.size()))) + " · " + key}, {"kind", "browser_source"}, {"url", q.url->text().trimmed()}};
			definitions.insert(key, spec); return "new:" + key;
		};
		QJsonArray selectedKeys;
		for (const auto &g : PulseShow::setupCatalogue()) {
			if (!activeGroups.contains(g.name)) continue; QJsonArray ids; QJsonObject roles, frames, artwork;
			QJsonObject transitionChoices;
			const auto &policy = stageTransitions[g.name];
			for (const QString &route : {QString("horizontal"), QString("vertical")}) {
				auto *combo = route == "horizontal" ? policy.horizontal : policy.vertical;
				auto *time = route == "horizontal" ? policy.horizontalDuration : policy.verticalDuration;
				const QString choice = combo->currentData().toString(); if (choice.isEmpty()) continue;
				transitionChoices.insert(route + "Transition", choice);
				if (choice.startsWith("new-stinger:")) selectedStingers.insert(choice.mid(12), stingerDrafts.value(choice.mid(12)));
				if (choice == "fade") transitionChoices.insert(route + "DurationMs", time->value());
			}
			if (usesSaved(g.name)) {
				QJsonObject bindings; QJsonArray disabled;
				for (const auto &id : savedIds(g.name)) selectedKeys.append(g.id + "/saved:" + id.toString());
				for (const auto &q : inventory) if (q.group == g.name && q.role.startsWith("saved:")) {
					const QString uuid = q.role.mid(6), assigned = sourceId(q);
					if (assigned.isEmpty()) disabled.append(uuid); else if (assigned != uuid) bindings.insert(uuid, assigned);
				}
				stages.append(QJsonObject{{"theme", g.id}, {"referenceStage", saved[g.name].stage.value("name")},
					{"referenceLooks", savedIds(g.name)}, {"sourceBindings", bindings}, {"disabledSources", disabled},
					{"transitions", transitionChoices}}); continue;
			}
			for (const auto &look : g.looks) if (looks.value(g.id + "/" + look.id).selected->isChecked()) {
				ids.append(look.id); selectedKeys.append(g.id + "/" + look.id);
				for (const QString &route : {QString("horizontal"), QString("vertical")}) frames.insert(look.id + "/" + route, framing(g, look));
				if (look.main == "graphic" || look.support == "graphic") artwork.insert(look.id, QJsonObject{
					{"graphic", sourceId(*question(g.name, "graphic", "horizontal", look.id))},
					{"graphicPortrait", sourceId(*question(g.name, "graphic", "vertical", look.id))},
					{"graphicFullCanvas", fullCanvasArtwork(question(g.name, "graphic", "horizontal", look.id))},
					{"graphicPortraitFullCanvas", fullCanvasArtwork(question(g.name, "graphic", "vertical", look.id))}});
			}
			for (const auto &role : PulseShow::roles(g, ids)) {
				if (role != "graphic") roles.insert(role, sourceId(*question(g.name, role)));
			}
			stages.append(QJsonObject{{"theme", g.id}, {"looks", ids}, {"roles", roles}, {"lookArtwork", artwork}, {"framing", frames}, {"transitions", transitionChoices}});
		}
		for (const auto &q : inventory) if (q.group == "Overlay" && !q.choice->currentData().toString().isEmpty()) {
			QJsonArray visible; for (const auto &key : selectedKeys) { const auto *g = PulseShow::theme(key.toString().section('/', 0, 0));
				if (q.scope->currentData().toString().isEmpty() || q.scope->currentData().toString() == g->name || q.scope->currentData() == key.toString()) visible.append(key); }
			if (!visible.isEmpty()) overlays.append(QJsonObject{{"canvas", q.role.section('/', 0, 0)}, {"order", q.role.section('/', 1).toInt()}, {"source", sourceId(q)}, {"looks", visible}});
		}
		return {{"schema", 2}, {"overlayProvider", "lumia"}, {"name", name}, {"stages", stages}, {"newSources", definitions}, {"newStingers", selectedStingers}, {"overlays", overlays}};
	}
	void draw() {
		const QString key = review->currentData().toString(); const auto *g = PulseShow::theme(key.section('/', 0, 0));
		lookReview->blockSignals(true); lookReview->setCurrentIndex(lookReview->findData(key)); lookReview->blockSignals(false);
		const auto *look = g ? PulseShow::look(*g, key.section('/', 1)) : nullptr;
		QJsonObject savedLook;
		if (g && usesSaved(g->name)) for (const auto &entry : saved[g->name].actions)
			if (entry.toObject().value("id").toString() == key.section('/', 1).mid(6)) savedLook = entry.toObject();
		auto diagram = [&](bool beforeAssignment) {
		QPixmap image(760, 408); image.fill(QColor("#0b1220")); QPainter painter(&image);
		QFont diagramFont = painter.font(); diagramFont.setPixelSize(18); painter.setFont(diagramFont);
		painter.setRenderHint(QPainter::TextAntialiasing);
		for (bool vertical : {false, true}) {
			const QRectF canvas = vertical ? QRectF(590, 26, 158, 281) : QRectF(20, 26, 500, 281.25);
			painter.setPen(Qt::white); painter.drawText(QPointF(canvas.x(), 18), vertical ? "Portrait 9:16" : "Landscape 16:9"); painter.fillRect(canvas, QColor("#172235"));
			if (!savedLook.isEmpty()) {
				painter.save(); painter.setClipRect(canvas);
				for (const auto &entry : savedLook.value("items").toArray()) { const auto target = entry.toObject(); const auto t = target.value("transform").toObject();
					if (!t.value("visible").toBool() || (target.value("container") != savedLook.value("container")) != vertical) continue;
					const double w = vertical ? 1080. : 1920., h = vertical ? 1920. : 1080.;
					double width = t.value("boundsWidth").toDouble(), height = t.value("boundsHeight").toDouble();
					if (!t.value("boundsType").toInt()) { width = (target.value("sourceWidth").toDouble() - t.value("cropLeft").toDouble() - t.value("cropRight").toDouble()) * t.value("scaleX").toDouble(1);
						height = (target.value("sourceHeight").toDouble() - t.value("cropTop").toDouble() - t.value("cropBottom").toDouble()) * t.value("scaleY").toDouble(1); }
					QRectF rect(canvas.x() + t.value("positionX").toDouble() / w * canvas.width(), canvas.y() + t.value("positionY").toDouble() / h * canvas.height(), width / w * canvas.width(), height / h * canvas.height());
					painter.setPen(QColor("#52daef")); painter.drawRect(rect);
				}
				painter.restore();
			}
			if (look) for (auto panel : PulseShow::layout(*look, vertical, beforeAssignment || supporting(*g, *look), framing(*g, *look))) {
				const auto *q = question(g->name, panel.role, panel.role == "graphic" ? (vertical ? "vertical" : "horizontal") : QString(), look->id);
				const bool fullGraphic = panel.role == "graphic" && fullCanvasArtwork(q);
				if (fullGraphic) panel.rect = {0, 0, 1, 1};
				QRectF rect(canvas.x() + panel.rect.x() * canvas.width(), canvas.y() + panel.rect.y() * canvas.height(), panel.rect.width() * canvas.width(), panel.rect.height() * canvas.height());
				painter.fillRect(rect, fullGraphic ? QColor(23, 102, 128, 80) : panel.role == "feedA" ? QColor("#66479c") : QColor("#176680"));
				painter.setPen(QColor("#52daef")); painter.drawRect(rect);
				QString label = panel.role == "graphic" ? "Artwork" : PulseShow::roleName(panel.role);
				if (!beforeAssignment && q) label += "\n" + painter.fontMetrics().elidedText(q->choice->currentData().toString().isEmpty() ? "Not assigned" : q->choice->currentText(), Qt::ElideRight, qMax(0, int(rect.width()) - 10));
				painter.setPen(Qt::white); painter.drawText(rect.adjusted(5, 1, -5, -1), Qt::AlignCenter | Qt::TextWordWrap, label);
			}
			QStringList layers;
			if (g) for (const auto &q : inventory) if (q.group == "Overlay" &&
				q.role.startsWith(vertical ? "vertical/" : "horizontal/") && !q.choice->currentData().toString().isEmpty() &&
				(q.scope->currentData().toString().isEmpty() || q.scope->currentData().toString() == g->name || q.scope->currentData().toString() == key))
				layers.append(q.role.section('/', 1) + ": " + q.choice->currentText());
			if (!layers.isEmpty()) {
				painter.setPen(QPen(QColor("#ffcb66"), 3, Qt::DashLine)); painter.drawRect(canvas.adjusted(3, 3, -3, -3));
				painter.setPen(QColor("#ffcb66"));
				painter.drawText(QPointF(canvas.x(), 328), vertical ? "Layers (1 to 3)" : "Shared layers · 1 lowest, 3 highest");
				for (int index = 0; index < layers.size(); ++index)
					painter.drawText(QPointF(canvas.x(), 350 + index * 22), painter.fontMetrics().elidedText(layers[index], Qt::ElideRight, int(canvas.width())));
			}
			painter.setPen(Qt::white); painter.drawRect(canvas);
		}
		painter.end(); return image;
		};
		picture->setDiagram(diagram(false)); lookPicture->setDiagram(diagram(true));
		summary->setText(QString("%1 stage groups · %2 selected Looks · landscape and portrait. Review each Look and its assigned source names above.").arg(activeGroups.size()).arg(review->count()));
	}
};
