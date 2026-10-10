#pragma once

#include "pulse-show-templates.hpp"
#include <QSpinBox>

// UI state only. Creation uses the same validated, transactional native builder
// as the previous wizard, without editing any existing source configuration.
class FlexibleSetup {
	struct LookFields {
		QCheckBox *selected, *mainFill, *supportFill;
		QComboBox *composition, *corner;
		QSpinBox *portraitShare;
		QLineEdit *message;
	};
	struct Question {
		QString group, role;
		QWidget *panel;
		QComboBox *choice, *scope = nullptr;
		QLineEdit *url = nullptr;
		QSpinBox *minutes = nullptr;
	};
	QMap<QString, LookFields> looks;
	struct SavedFields { QComboBox *preset; QJsonObject stage; QJsonArray actions; QMap<QString, QCheckBox *> selected; };
	QMap<QString, SavedFields> saved;
	QJsonArray optionalSources;
	std::vector<Question> inventory;
	QSet<QString> activeGroups;
	QVector<int> activeQuestions;
	int sourceIndex = 0;
	QLabel *title, *picture, *summary;
	QStackedWidget *questions;
	QPushButton *previous, *next;
	QComboBox *review;
	struct StageTransitions { QComboBox *horizontal, *vertical; QSpinBox *horizontalDuration, *verticalDuration; };
	QMap<QString, StageTransitions> stageTransitions;
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
	const Question *question(const QString &group, const QString &role) const {
		for (const auto &q : inventory) if (q.group == group && q.role == role) return &q;
		return nullptr;
	}
	QJsonObject framing(const PulseShow::Theme &group, const PulseShow::Look &look) const {
		const auto &f = looks[group.id + "/" + look.id];
		return {{"composition", f.composition->currentData().toString()}, {"corner", f.corner->currentData().toString()},
			{"divider", f.portraitShare->value() / 100.}, {"durationMs", duration->value()},
			{look.main + "Fill", f.mainFill->isChecked()}, {look.support + "Fill", f.supportFill->isChecked()}};
	}
	bool supporting(const PulseShow::Theme &group, const PulseShow::Look &look) const {
		const auto *q = question(group.name, look.support);
		return q && !q->choice->currentData().toString().isEmpty();
	}
	void showQuestion() {
		questions->setVisible(!activeQuestions.isEmpty());
		if (!activeQuestions.isEmpty()) {
			sourceIndex = qBound(0, sourceIndex, int(activeQuestions.size()) - 1);
			questions->setCurrentWidget(inventory[activeQuestions[sourceIndex]].panel);
			title->setText(QString("Source %1 of %2").arg(sourceIndex + 1).arg(activeQuestions.size()));
		}
		previous->setEnabled(sourceIndex > 0);
		next->setEnabled(sourceIndex + 1 < activeQuestions.size());
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
			if (source && QString::fromUtf8(obs_source_get_id(source)) == "obs_stinger_transition")
				stingers.insert(QString::fromUtf8(obs_source_get_name(source)), "stinger:" + QString::fromUtf8(obs_source_get_name(source)));
		}
		obs_frontend_source_list_free(&transitionSources);
		auto text = [](QVBoxLayout *layout, QWidget *parent, const QString &message) {
			auto *label = new QLabel(message, parent); label->setWordWrap(true); layout->addWidget(label); return label;
		};
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
				combo->addItem("Keep saved transition / use Fade for a new layout", "");
				combo->addItem("Fade", "fade"); combo->addItem("Cut", "cut");
				for (auto it = stingers.cbegin(); it != stingers.cend(); ++it) combo->addItem("Stinger · " + it.key(), it.value());
				time = new QSpinBox(transitionCard); time->setRange(0, 3000); time->setValue(500); time->setSuffix(" ms");
				time->setAccessibleName(group.name + " " + route + " fade duration"); time->setEnabled(false);
				transitionLayout->addRow(route == "horizontal" ? "Landscape transition" : "Portrait transition", combo);
				transitionLayout->addRow("Fade duration", time);
				QObject::connect(combo, &QComboBox::currentIndexChanged, &owner, [combo, time](int) {
					time->setEnabled(combo->currentData().toString() == "fade");
				});
				return combo;
			};
			policy.horizontal = selector("horizontal", policy.horizontalDuration);
			policy.vertical = selector("vertical", policy.verticalDuration); stageTransitions.insert(group.name, policy);
			auto *transitionHint = new QLabel("Choose a stinger for each format, or keep the saved choices. A stinger uses its own video timing and cut point. Movement between Looks is configured separately.", transitionCard);
			transitionHint->setWordWrap(true); transitionLayout->addRow(transitionHint);
			auto *optionsPanel = new QWidget(tab); auto *optionsLayout = new QVBoxLayout(optionsPanel); layout->addWidget(optionsPanel);
			QObject::connect(s.preset, &QComboBox::currentIndexChanged, &owner, [this, savedPanel, optionsPanel](int index) {
				savedPanel->setVisible(index == 1); optionsPanel->setVisible(index == 0); update(activeGroups); if (changed) changed(); });
			layout = optionsLayout;
			text(layout, tab, "Choose operating Looks, then set their layout options. Feed A, B and C are slots for this stage group; each can be a camera, capture or media source.");
			for (const auto &look : group.looks) {
				auto *card = new QGroupBox(tab); auto *body = new QVBoxLayout(card); layout->addWidget(card);
				LookFields f{};
				f.selected = new QCheckBox(look.name, card); f.selected->setChecked(true); body->addWidget(f.selected);
				text(body, card, look.description);
				auto *options = new QWidget(card); auto *form = new QFormLayout(options); body->addWidget(options);
				f.composition = new QComboBox(options); f.composition->addItem("Recommended layout", "");
				f.composition->addItem("Main view with corner inset", "corner"); f.composition->addItem("Supporting side column", "rail");
				f.composition->addItem("Side by side", "split");
				f.corner = new QComboBox(options);
				for (const auto &entry : {std::pair{"Top left", "top-left"}, {"Top right", "top-right"}, {"Bottom left", "bottom-left"}, {"Bottom right", "bottom-right"}})
					f.corner->addItem(entry.first, entry.second);
				f.portraitShare = new QSpinBox(options); f.portraitShare->setRange(15, 65); f.portraitShare->setValue(30); f.portraitShare->setSuffix("%");
				f.mainFill = new QCheckBox("Crop main feed to fill (off: fit whole image)", options);
				f.supportFill = new QCheckBox("Crop supporting feed to fill (off: fit whole image)", options);
				f.message = new QLineEdit(look.name, options); f.message->setMaxLength(180);
				if (!look.support.isEmpty()) { form->addRow("Landscape composition", f.composition); form->addRow("Inset corner", f.corner);
					form->addRow("Portrait top panel size", f.portraitShare); }
				else { f.composition->hide(); f.corner->hide(); f.portraitShare->hide(); }
				if (!PulseShow::graphic(look.main)) form->addRow(f.mainFill);
				else f.mainFill->hide();
				if (!look.support.isEmpty() && !PulseShow::graphic(look.support)) form->addRow(f.supportFill);
				else f.supportFill->hide();
				if (look.main == "graphic" || look.support == "graphic") form->addRow("Built-in message", f.message);
				else f.message->hide();
				auto optionState = [f, recommended = look.layout] {
					const QString composition = f.composition->currentData().toString().isEmpty() ? recommended : f.composition->currentData().toString();
					f.corner->setEnabled(composition == "corner");
					f.portraitShare->setEnabled(composition != "footer" && composition != "label");
				};
				optionState();
				looks.insert(group.id + "/" + look.id, f);
				QObject::connect(f.selected, &QCheckBox::toggled, &owner, [this, options](bool checked) { options->setEnabled(checked); update(activeGroups); if (changed) changed(); });
				for (auto *combo : {f.composition, f.corner}) QObject::connect(combo, &QComboBox::currentIndexChanged, &owner, [this](int) { draw(); });
				QObject::connect(f.composition, &QComboBox::currentIndexChanged, &owner, [optionState](int) { optionState(); });
				QObject::connect(f.portraitShare, &QSpinBox::valueChanged, &owner, [this](int) { draw(); });
				for (auto *check : {f.mainFill, f.supportFill}) QObject::connect(check, &QCheckBox::toggled, &owner, [this](bool) { draw(); });
			}
			layout->addStretch();
		}
		sourcePanel = new QWidget(sourcePage); auto *layout = new QVBoxLayout(sourcePanel); sourceLayout->addWidget(sourcePanel);
		title = text(layout, sourcePanel, "Assign your feeds"); questions = new QStackedWidget(sourcePanel); layout->addWidget(questions);
		auto *navigation = new QHBoxLayout; previous = new QPushButton("Previous source", sourcePanel); next = new QPushButton("Next source", sourcePanel);
		navigation->addWidget(previous); navigation->addWidget(next); navigation->addStretch(); layout->addLayout(navigation);
		QObject::connect(previous, &QPushButton::clicked, &owner, [this] { --sourceIndex; showQuestion(); });
		QObject::connect(next, &QPushButton::clicked, &owner, [this] { ++sourceIndex; showQuestion(); });
		auto addQuestion = [&](const QString &group, const QString &role) {
			Question q{}; q.group = group; q.role = role; q.panel = new QWidget(questions); auto *body = new QVBoxLayout(q.panel);
			const QString label = group == "Overlay" ? (role.startsWith("horizontal") ? "Landscape overlay " : "Portrait overlay ") + role.section('/', 1) : group + " · " + PulseShow::roleName(role);
			text(body, q.panel, label);
			q.choice = new QComboBox(q.panel); q.choice->setAccessibleName(label);
			q.choice->addItem("None / skip when optional", "");
			if (role == "graphic") { q.choice->addItem("Built-in title / countdown", "builtin"); q.choice->setCurrentIndex(1); }
			const bool overlay = group == "Overlay";
			struct Scan { QComboBox *choice; bool feed; } scan{q.choice, role.startsWith("feed")};
			obs_enum_sources([](void *opaque, obs_source_t *source) {
				auto &scan = *static_cast<Scan *>(opaque); const QString type = obs_source_get_unversioned_id(source);
				if (!(obs_source_get_output_flags(source) & OBS_SOURCE_VIDEO) || obs_scene_from_source(source) || obs_group_from_source(source)) return true;
				if (!scan.feed || type == "dshow_input" || type == "game_capture" || type == "window_capture" || type == "monitor_capture" ||
				    type == "ffmpeg_source" || type == "vlc_source" || type.contains("ndi", Qt::CaseInsensitive))
					scan.choice->addItem(obs_source_get_name(source), obs_source_get_uuid(source));
				return true;
			}, &scan);
			if (!role.startsWith("feed")) {
				q.choice->addItem("New browser / Lumia overlay link", "url"); q.url = new QLineEdit(q.panel);
				q.url->setPlaceholderText("https://…"); q.url->setAccessibleName(group + " " + role + " browser URL"); body->addWidget(q.url);
			}
			body->addWidget(q.choice);
			if (role == "graphic") { q.minutes = new QSpinBox(q.panel); q.minutes->setRange(1, 120); q.minutes->setValue(5); q.minutes->setSuffix(" minutes");
				text(body, q.panel, "Countdown duration (built-in Countdown Look)"); body->addWidget(q.minutes); }
			if (overlay) {
				q.scope = new QComboBox(q.panel); q.scope->addItem("All selected stage groups", "");
				for (const auto &g : PulseShow::setupCatalogue()) q.scope->addItem(g.name, g.name);
				text(body, q.panel, "Show this full-canvas overlay in"); body->addWidget(q.scope);
			} else text(body, q.panel, "Choose a source already in Pulse Weaver. A feed can serve different roles in different stage groups. Required slots must be assigned; optional supporting feeds can be skipped.");
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
			for (const auto &role : PulseShow::roles(g, ids)) addQuestion(g.name, role);
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
					type == "ffmpeg_source" || type.contains("ndi", Qt::CaseInsensitive) || optionalSources.contains(uuid)) sources.append(uuid);
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
				text(body, q.panel, "Keep the current source to reproduce this saved layout, or choose another source of the same type. Camera effects and framing are retained.");
				q.choice = new QComboBox(q.panel); body->addWidget(q.choice);
				if (optionalSources.contains(uuid)) q.choice->addItem("None / skip this optional widget", "");
				struct Scan { QComboBox *choice; QString type; } context{q.choice, obs_source_get_unversioned_id(source)};
				obs_enum_sources([](void *opaque, obs_source_t *candidate) { auto &s = *static_cast<Scan *>(opaque);
					if ((obs_source_get_output_flags(candidate) & OBS_SOURCE_VIDEO) && !obs_scene_from_source(candidate) && !obs_group_from_source(candidate) &&
						s.type == obs_source_get_unversioned_id(candidate)) s.choice->addItem(obs_source_get_name(candidate), obs_source_get_uuid(candidate)); return true;
				}, &context);
				q.choice->setCurrentIndex(q.choice->findData(uuid)); questions->addWidget(q.panel); inventory.push_back(q);
				QObject::connect(q.choice, &QComboBox::currentIndexChanged, &owner, [this](int) { if (changed) changed(); });
			}
		}
		for (const QString &route : {QString("horizontal"), QString("vertical")}) for (int i = 1; i <= 3; ++i) addQuestion("Overlay", route + "/" + QString::number(i));
		auto *form = new QFormLayout; duration = new QSpinBox(sourcePanel); duration->setRange(0, 3000); duration->setValue(850); duration->setSuffix(" ms");
		form->addRow("Movement between Looks", duration); layout->addLayout(form);
		auto *stingerLink = new QPushButton("Create a stinger with Profounder", sourcePanel); layout->addWidget(stingerLink);
		QObject::connect(stingerLink, &QPushButton::clicked, &owner, [] { QDesktopServices::openUrl(QUrl("https://profounder.com/transitions/")); });
		text(layout, sourcePanel, "Profounder offers free and paid transition designs. Download your chosen video, add it as a Stinger in Studio's Scene Transitions, and configure its cut point. Add landscape and portrait versions when needed. The stage options on page 3 list your existing stingers; you can also assign a newly added one later in Show → Manage stages and routing.");
		text(layout, sourcePanel, "For any new Lumia browser link: first import the shared template into your own Lumia Overlays, configure it, then copy its browser link. A marketplace or template share link is not a browser source. Use the matching H/V version and skip widgets you do not need.");
		review = new QComboBox(sourcePanel); review->setAccessibleName("Flexible Look to review"); layout->addWidget(review);
		picture = new QLabel(sourcePanel); layout->addWidget(picture); summary = text(layout, sourcePanel, "");
		text(layout, sourcePanel, "The diagrams show placement in both formats. Saved layouts keep their artwork, camera treatments and movement. Stage transitions stay saved unless you choose replacements on page 3. Extra overlay slots and movement controls apply to layouts made with the layout options. Check actual feeds privately in Control after creation.");
		QObject::connect(review, &QComboBox::currentIndexChanged, &owner, [this](int) { draw(); });
	}
	void update(const QSet<QString> &groups) {
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
			for (int i = 0; i < int(inventory.size()); ++i) if (inventory[i].group == g.name && roles.contains(inventory[i].role)) activeQuestions.append(i);
		}
		bool custom = false; for (const auto &group : groups) if (!usesSaved(group)) custom = true;
		duration->setEnabled(custom);
		for (int i = 0; i < int(inventory.size()); ++i) if (custom && inventory[i].group == "Overlay") activeQuestions.append(i);
		review->setCurrentIndex(qMax(0, review->findData(selected))); review->blockSignals(false); showQuestion(); draw();
	}
	QString problem(bool sources) const {
		for (const auto &g : PulseShow::setupCatalogue()) {
			if (!activeGroups.contains(g.name)) continue; bool any = false;
			if (usesSaved(g.name)) { if (savedIds(g.name).isEmpty()) return "Choose at least one saved Look for " + g.name + "."; continue; }
			for (const auto &look : g.looks) {
				if (!looks.value(g.id + "/" + look.id).selected->isChecked()) continue; any = true;
				if (sources) for (const QString &role : {look.main, look.optionalPresenter ? QString() : look.support}) {
					if (role.isEmpty()) continue; const auto *q = question(g.name, role);
					if (!q || q->choice->currentData().toString().isEmpty()) return "Choose " + PulseShow::roleName(role) + " for " + g.name + " · " + look.name + ".";
				}
			}
			if (!any) return "Choose at least one Look for " + g.name + ".";
		}
		if (sources) for (int i : activeQuestions) { const auto &q = inventory[i]; if (q.choice->currentData().toString() != "url") continue;
			const QUrl url(q.url->text().trimmed()); if (!url.isValid() || url.host().isEmpty() || (url.scheme() != "https" && url.scheme() != "http"))
				return "Enter a complete browser URL for " + q.group + " · " + q.role + "."; }
		return {};
	}
	QJsonObject draft(const QString &name) const {
		QJsonArray stages, overlays; QJsonObject definitions;
		auto sourceId = [&](const Question &q) {
			const QString choice = q.choice->currentData().toString(); const QString key = q.group + "/" + q.role;
			if (choice != "builtin" && choice != "url") return choice;
			QJsonObject spec{{"name", name + " · " + key}, {"kind", choice == "builtin" ? "builtin_graphic" : "browser_source"}};
			if (choice == "url") spec.insert("url", q.url->text().trimmed());
			else { QJsonObject messages; for (const auto &g : PulseShow::setupCatalogue()) if (g.name == q.group)
				for (const auto &look : g.looks) messages.insert(look.id, looks.value(g.id + "/" + look.id).message->text());
				spec.insert("textByLook", messages); spec.insert("minutes", q.minutes->value()); }
			definitions.insert(key, spec); return "new:" + key;
		};
		QJsonArray selectedKeys;
		for (const auto &g : PulseShow::setupCatalogue()) {
			if (!activeGroups.contains(g.name)) continue; QJsonArray ids; QJsonObject roles, frames;
			QJsonObject transitionChoices;
			const auto &policy = stageTransitions[g.name];
			for (const QString &route : {QString("horizontal"), QString("vertical")}) {
				auto *combo = route == "horizontal" ? policy.horizontal : policy.vertical;
				auto *time = route == "horizontal" ? policy.horizontalDuration : policy.verticalDuration;
				const QString choice = combo->currentData().toString(); if (choice.isEmpty()) continue;
				transitionChoices.insert(route + "Transition", choice);
				if (choice == "fade") transitionChoices.insert(route + "DurationMs", time->value());
			}
			if (usesSaved(g.name)) {
				QJsonObject bindings; QJsonArray disabled;
				for (const auto &q : inventory) if (q.group == g.name && q.role.startsWith("saved:")) {
					const QString uuid = q.role.mid(6), assigned = q.choice->currentData().toString();
					if (assigned.isEmpty()) disabled.append(uuid); else if (assigned != uuid) bindings.insert(uuid, assigned);
				}
				stages.append(QJsonObject{{"theme", g.id}, {"referenceStage", saved[g.name].stage.value("name")},
					{"referenceLooks", savedIds(g.name)}, {"sourceBindings", bindings}, {"disabledSources", disabled},
					{"transitions", transitionChoices}}); continue;
			}
			for (const auto &look : g.looks) if (looks.value(g.id + "/" + look.id).selected->isChecked()) {
				ids.append(look.id); selectedKeys.append(g.id + "/" + look.id);
				for (const QString &route : {QString("horizontal"), QString("vertical")}) frames.insert(look.id + "/" + route, framing(g, look));
			}
			for (const auto &role : PulseShow::roles(g, ids)) roles.insert(role, sourceId(*question(g.name, role)));
			stages.append(QJsonObject{{"theme", g.id}, {"looks", ids}, {"roles", roles}, {"framing", frames}, {"transitions", transitionChoices}});
		}
		for (const auto &q : inventory) if (q.group == "Overlay" && !q.choice->currentData().toString().isEmpty()) {
			QJsonArray visible; for (const auto &key : selectedKeys) { const auto *g = PulseShow::theme(key.toString().section('/', 0, 0));
				if (q.scope->currentData().toString().isEmpty() || q.scope->currentData().toString() == g->name) visible.append(key); }
			if (!visible.isEmpty()) overlays.append(QJsonObject{{"canvas", q.role.section('/', 0, 0)}, {"order", q.role.section('/', 1).toInt()}, {"source", sourceId(q)}, {"looks", visible}});
		}
		return {{"schema", 2}, {"name", name}, {"stages", stages}, {"newSources", definitions}, {"overlays", overlays}};
	}
	void draw() {
		const QString key = review->currentData().toString(); const auto *g = PulseShow::theme(key.section('/', 0, 0));
		const auto *look = g ? PulseShow::look(*g, key.section('/', 1)) : nullptr;
		QJsonObject savedLook;
		if (g && usesSaved(g->name)) for (const auto &entry : saved[g->name].actions)
			if (entry.toObject().value("id").toString() == key.section('/', 1).mid(6)) savedLook = entry.toObject();
		QPixmap image(760, 330); image.fill(QColor("#0b1220")); QPainter painter(&image);
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
			if (look) for (const auto &panel : PulseShow::layout(*look, vertical, supporting(*g, *look), framing(*g, *look))) {
				QRectF rect(canvas.x() + panel.rect.x() * canvas.width(), canvas.y() + panel.rect.y() * canvas.height(), panel.rect.width() * canvas.width(), panel.rect.height() * canvas.height());
				painter.fillRect(rect, panel.role == "feedA" ? QColor("#66479c") : QColor("#176680")); painter.setPen(QColor("#52daef")); painter.drawRect(rect);
				painter.setPen(Qt::white); painter.drawText(rect, Qt::AlignCenter, PulseShow::roleName(panel.role));
			}
			painter.setPen(Qt::white); painter.drawRect(canvas);
		}
		painter.end(); picture->setPixmap(image); summary->setText(QString("%1 stage groups · %2 selected Looks · independent landscape and portrait layouts").arg(activeGroups.size()).arg(review->count()));
	}
};
