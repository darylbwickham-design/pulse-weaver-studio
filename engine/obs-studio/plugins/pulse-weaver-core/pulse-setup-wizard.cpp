#include "pulse-motion-engine.hpp"

#include <QAbstractButton>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QFrame>
#include <QGuiApplication>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QStyle>
#include <QVBoxLayout>
#include <QWizard>
#include <QWizardPage>
#include <functional>
#include "pulse-setup-flexible.hpp"
#include "pulse-setup-guided.hpp"
#include "../../shared/qt/PulseInstructions.hpp"
#include "../../shared/qt/PulseOutputActivity.hpp"

namespace {
QString wizardNameProblem(const QString &prefix, const QSet<QString> &groups, const QJsonArray &stages)
{
	if (prefix.isEmpty() || prefix.size() > 90) return "Enter a show name of 1–90 characters.";
	if (groups.isEmpty()) return "Choose at least one section for your show.";
	OBSCanvasAutoRelease vertical = obs_get_canvas_by_name("Pulse Weaver Vertical");
	for (const QString &group : groups) {
		const QString name = prefix + " · " + group;
		for (const auto &entry : stages) if (entry.toObject().value("name").toString() == name)
			return "A Stage named “" + name + "” already exists. Choose another show name.";
		for (const QString &candidate : {name, name + " · Portrait"}) {
			OBSSourceAutoRelease source = obs_get_source_by_name(candidate.toUtf8().constData());
			if (!source && vertical) source = obs_canvas_get_source_by_name(vertical, candidate.toUtf8().constData());
			if (source) return "A scene named “" + candidate + "” already exists. Choose another show name.";
		}
	}
	return {};
}

QLabel *setupText(QVBoxLayout *layout, QWidget *parent, const QString &message, const char *role = "PulseSetupMuted")
{
	auto *label = new QLabel(message, parent);
	label->setTextFormat(Qt::PlainText); label->setWordWrap(true); label->setObjectName(role);
	layout->addWidget(label); return label;
}

QFrame *setupSurface(QWidget *parent, const char *name = "PulseSetupSurface")
{
	auto *surface = new QFrame(parent); surface->setObjectName(name);
	surface->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Maximum);
	return surface;
}

void sizeSetupDialog(QDialog &dialog, int width, int height)
{
	const auto *screen = dialog.parentWidget() ? dialog.parentWidget()->screen() : QGuiApplication::primaryScreen();
	if (screen) {
		const auto available = screen->availableGeometry().size();
		width = qMin(width, available.width() - 40); height = qMin(height, available.height() - 60);
	}
	dialog.resize(width, height);
}

class SetupPage final : public QWizardPage {
public:
	std::function<QString()> problem;
	QLabel *error = nullptr;
	bool showProblem = true;
	bool isComplete() const override { return !problem || problem().isEmpty(); }
	bool validatePage() override {
		const QString message = problem ? problem() : QString();
		if (error) { error->setText(message); error->setVisible(showProblem && !message.isEmpty()); }
		return message.isEmpty();
	}
	void changed() {
		if (error) {
			const QString message = problem ? problem() : QString();
			error->setText(message); error->setVisible(showProblem && !message.isEmpty());
		}
		emit completeChanged();
	}
	// Back preserves the single in-memory draft; Cancel still discards it.
	void cleanupPage() override {}
};

// Static illustration: no cameras, media decoders or browser instances.
class SetupCanvasIllustration final : public QWidget {
public:
	explicit SetupCanvasIllustration(QWidget *parent) : QWidget(parent) {
		setAccessibleName("Illustration: one show arranged for landscape and portrait");
		setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred); setMinimumHeight(144);
	}
	QSize sizeHint() const override { return {800, 184}; }
protected:
	void paintEvent(QPaintEvent *) override {
		QPainter painter(this); painter.setRenderHint(QPainter::Antialiasing);
		const QRectF bounds = QRectF(rect()).adjusted(1, 1, -1, -1);
		QLinearGradient backdrop(bounds.topLeft(), bounds.bottomRight());
		backdrop.setColorAt(0, QColor("#203247")); backdrop.setColorAt(1, QColor("#0d1726"));
		painter.setPen(QColor("#425970")); painter.setBrush(backdrop); painter.drawRoundedRect(bounds, 14, 14);
		const qreal scale = qMin((width() - 32.) / 800., (height() - 20.) / 172.);
		painter.translate((width() - 800 * scale) / 2, (height() - 172 * scale) / 2); painter.scale(scale, scale);
		auto panel = [&](const QRectF &canvas, bool portrait) {
			painter.setBrush(QColor("#080f1a")); painter.setPen(QPen(QColor("#647c94"), 1.5)); painter.drawRoundedRect(canvas, 8, 8);
			const QRectF content = canvas.adjusted(7, 7, -7, -7);
			QLinearGradient sky(content.topLeft(), content.bottomRight()); sky.setColorAt(0, QColor("#316176")); sky.setColorAt(1, QColor("#1b2b43"));
			painter.setBrush(sky); painter.setPen(Qt::NoPen); painter.drawRoundedRect(content, 4, 4);
			painter.save(); painter.setClipRect(content);
			QPainterPath mountain; mountain.moveTo(content.left(), content.bottom());
			mountain.lineTo(content.left() + content.width() * .3, content.top() + content.height() * .32);
			mountain.lineTo(content.left() + content.width() * .55, content.top() + content.height() * .65);
			mountain.lineTo(content.right(), content.top() + content.height() * .2); mountain.lineTo(content.right(), content.bottom()); mountain.closeSubpath();
			painter.fillPath(mountain, QColor("#39546c"));
			const QRectF inset = portrait ? QRectF(content.left() + 6, content.bottom() - 45, content.width() - 12, 39) :
				QRectF(content.left() + 9, content.bottom() - 58, 73, 49);
			painter.setPen(QPen(QColor("#d1b6f6"), 1.5)); painter.setBrush(QColor("#594378")); painter.drawRoundedRect(inset, 5, 5);
			painter.setPen(Qt::NoPen); painter.setBrush(QColor("#bda6d4"));
			painter.drawEllipse(QPointF(inset.center().x(), inset.top() + inset.height() * .35), inset.height() * .14, inset.height() * .14);
			painter.drawRoundedRect(QRectF(inset.center().x() - 11, inset.top() + inset.height() * .57, 22, 19), 9, 9);
			painter.restore();
		};
		panel({319, 18, 245, 138}, false); panel({592, 9, 88, 156}, true);
		QFont heading = font(); heading.setPixelSize(23); heading.setBold(true); painter.setFont(heading); painter.setPen(QColor("#eef5fc"));
		painter.drawText(QRectF(25, 32, 270, 63), Qt::AlignLeft | Qt::AlignVCenter | Qt::TextWordWrap, "One show.\nBoth canvases.");
		QFont caption = font(); caption.setPixelSize(13); painter.setFont(caption); painter.setPen(QColor("#b2c1d3"));
		painter.drawText(QRectF(25, 109, 265, 35), Qt::TextWordWrap, "Keep the same sources. Give each format its own arrangement.");
		caption.setPixelSize(12); caption.setBold(true); painter.setFont(caption);
		painter.drawText(QRectF(699, 35, 80, 30), Qt::AlignLeft | Qt::AlignVCenter, "16:9");
		painter.drawText(QRectF(699, 99, 80, 30), Qt::AlignLeft | Qt::AlignVCenter, "9:16");
		painter.setPen(QPen(QColor("#81cad8"), 2)); painter.drawLine(QPointF(692, 71), QPointF(763, 71));
	}
};

void openSetupLumiaHelp(QWidget *parent)
{
	QDialog dialog(parent); dialog.setObjectName("PulseWeaverSetupDialog"); dialog.setProperty("pulseSetupDialog", true);
	dialog.setWindowTitle("Lumia artwork and controls"); sizeSetupDialog(dialog, 680, 560);
	auto *outer = new QVBoxLayout(&dialog); outer->setContentsMargins(24, 20, 24, 20); outer->setSpacing(14);
	setupText(outer, &dialog, "LUMIA STREAM", "PulseSetupEyebrow");
	setupText(outer, &dialog, "Connect when you need it", "PulseSetupTitle");
	setupText(outer, &dialog, "A feed-only show can be planned without Lumia. The supplied countdowns, artwork and widgets use Lumia browser sources.", "PulseSetupSubtitle");
	auto *scroll = new QScrollArea(&dialog); scroll->setFrameShape(QFrame::NoFrame); scroll->setWidgetResizable(true); outer->addWidget(scroll, 1);
	auto *body = new QWidget(scroll); auto *layout = new QVBoxLayout(body); layout->setContentsMargins(0, 0, 0, 0); layout->setSpacing(12); scroll->setWidget(body);
	const std::pair<QString, QString> steps[] = {
		{"1 · Install and connect", "Install Lumia Stream, import the Pulse Weaver companion plugin, and follow the plugin's connection instructions for this installation."},
		{"2 · Prepare matching artwork", "Import and customize landscape (H) and portrait (V) templates in Lumia. Use its chain/copy-link button to get the browser links. Template import links cannot be displayed as sources."},
		{"3 · Finish the commands", "After creating your show, choose its new Stages and Looks in Lumia's countdown and scene-switch actions. Import accompanying alert commands once for both canvases."}};
	for (const auto &step : steps) {
		auto *surface = setupSurface(body); auto *content = new QVBoxLayout(surface); content->setContentsMargins(16, 12, 16, 12);
		setupText(content, surface, step.first, "PulseSetupSectionTitle"); setupText(content, surface, step.second); layout->addWidget(surface);
	}
	auto *downloads = new QHBoxLayout; layout->addLayout(downloads);
	auto *lumia = new QPushButton("Get Lumia Stream", body), *plugin = new QPushButton("Get companion plugin", body);
	downloads->addWidget(lumia); downloads->addWidget(plugin);
	QObject::connect(lumia, &QPushButton::clicked, &dialog, [] { QDesktopServices::openUrl(QUrl("https://www.lumiastream.com/downloads")); });
	QObject::connect(plugin, &QPushButton::clicked, &dialog, [] { QDesktopServices::openUrl(QUrl("https://github.com/darylbwickham-design/pulse-weaver-studio/releases/latest/download/PulseWeaver-Lumia-1.4.4.lumiaplugin")); });
	auto *guide = new QPushButton("Open artwork and command guide", body); layout->addWidget(guide);
	QObject::connect(guide, &QPushButton::clicked, &dialog, [&dialog] { PulseInstructions::openDialog(&dialog); });
	layout->addStretch();
	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog); outer->addWidget(buttons);
	QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject); dialog.exec();
}

void showSetupCompletion(QWidget *parent, const QString &message)
{
	QDialog dialog(parent); dialog.setObjectName("PulseWeaverSetupDialog"); dialog.setProperty("pulseSetupDialog", true);
	dialog.setWindowTitle("Your show is created"); sizeSetupDialog(dialog, 720, 580);
	auto *outer = new QVBoxLayout(&dialog); outer->setContentsMargins(24, 20, 24, 20); outer->setSpacing(12);
	setupText(outer, &dialog, "SHOW CREATED", "PulseSetupEyebrow");
	setupText(outer, &dialog, "Make it yours, then check it", "PulseSetupTitle");
	setupText(outer, &dialog, message, "PulseSetupSubtitle");
	auto *scroll = new QScrollArea(&dialog); scroll->setWidgetResizable(true); scroll->setFrameShape(QFrame::NoFrame); outer->addWidget(scroll, 1);
	auto *body = new QWidget(scroll); auto *layout = new QVBoxLayout(body); layout->setContentsMargins(0, 0, 0, 0); layout->setSpacing(10); scroll->setWidget(body);
	struct CheckTopic { QString title, summary, detail; };
	const CheckTopic checks[] = {
		{"Picture and movement", "Check both canvases, Looks and Stage transitions.", "Open Control. Check both canvases, each Look and the transitions between Stages. The planning diagrams do not test your real feeds or stinger timing."},
		{"Sound and monitoring", "Confirm the intended overlay sound and monitoring route.", "New browser sources start with output audio off; existing audio settings are preserved. Choose the intended audio owner and monitoring route, then check headphones and stream audio through a full transition."},
		{"Lumia commands", "Point countdowns and alerts at your new Stages and Looks.", "Choose your new Stages and Looks in Lumia. Check a countdown and an alert, including its sound. The Lumia bulb on Show Control opens the guide again."},
		{"Destinations and rehearsal", "Review outputs and encoders, then rehearse privately.", "Review your destinations and encoders, then run a private rehearsal when you are ready. Creating the show has not tested live delivery or performance while gaming."}};
	auto *topics = new QGridLayout; topics->setSpacing(12); layout->addLayout(topics);
	int topicIndex = 0;
	for (const auto &check : checks) {
		auto *card = PulseSetupChoices::card(body, check.title, check.summary); card->setCheckable(false);
		topics->addWidget(card, topicIndex / 2, topicIndex % 2); ++topicIndex;
		QObject::connect(card, &QPushButton::clicked, &dialog, [&dialog, title = check.title, detail = check.detail] {
			QDialog topic(&dialog); topic.setObjectName("PulseWeaverSetupDialog"); topic.setProperty("pulseSetupDialog", true);
			topic.setWindowTitle(title); sizeSetupDialog(topic, 560, 360);
			auto *content = new QVBoxLayout(&topic); content->setContentsMargins(22, 18, 22, 18); content->setSpacing(14);
			setupText(content, &topic, title, "PulseSetupTitle");
			auto *scroll = new QScrollArea(&topic); scroll->setFrameShape(QFrame::NoFrame); scroll->setWidgetResizable(true); content->addWidget(scroll, 1);
			auto *body = new QWidget(scroll); auto *text = new QVBoxLayout(body); scroll->setWidget(body);
			setupText(text, body, detail, "PulseSetupSubtitle"); text->addStretch();
			auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, &topic); buttons->button(QDialogButtonBox::Close)->setText("Back"); content->addWidget(buttons);
			QObject::connect(buttons, &QDialogButtonBox::rejected, &topic, &QDialog::reject); topic.exec();
		});
	}
	layout->addStretch();
	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog); outer->addWidget(buttons);
	auto *done = buttons->button(QDialogButtonBox::Close);
	done->setText("Done"); done->setProperty("setupPrimary", true);
	done->style()->unpolish(done); done->style()->polish(done); done->update();
	auto *guide = buttons->addButton("Lumia setup guide", QDialogButtonBox::ActionRole);
	QObject::connect(guide, &QPushButton::clicked, &dialog, [&dialog] { PulseInstructions::openDialog(&dialog); });
	QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::accept); dialog.exec();
}
}

void PulseMotionEngine::openShowWizard()
{
	if (!mayLeaveDraft()) return;
	QFile stagesFile(QDir(QFileInfo(storagePath).absolutePath()).filePath("../../pulseweaver-stages.json"));
	QJsonArray savedStages;
	if (stagesFile.open(QIODevice::ReadOnly)) savedStages = QJsonDocument::fromJson(stagesFile.readAll()).array();
	PulseGuidedWizard wizard(editor, savedStages);
	while (wizard.exec() == QDialog::Accepted) {
		const QString issue = wizard.finalProblem();
		if (!issue.isEmpty()) { QMessageBox::warning(&wizard, "Your show needs attention", issue + "\nYour choices are preserved."); continue; }
		const auto request = PulseSetupPlan::draft(wizard.options());
		if (request.isEmpty()) { QMessageBox::warning(&wizard, "Your show needs attention", "Check your inputs and included views. Your choices are preserved."); continue; }
		const auto result = createGuidedShow(request);
		if (!result.value("ok").toBool()) { QMessageBox::warning(&wizard, "Show not created", result.value("message").toString() + "\nYour choices are preserved."); continue; }
		selectEditorStage(wizard.options().name.trimmed() + " · Main");
		setStatus(result.value("message").toString());
		showSetupCompletion(editor, "Created “" + wizard.options().name.trimmed() + "”. Main contains your chosen views. In Control, screens are called Stages and views are called Looks. Check the actual pictures and one intended alert-audio copy before streaming.");
		return;
	}
	if (wizard.advancedRequested()) openDetailedShowWizard();
}

void PulseMotionEngine::openDetailedShowWizard()
{
	if (!mayLeaveDraft()) return;
	QWizard wizard(editor); wizard.setObjectName("PulseWeaverSetupWizard");
	wizard.setWindowTitle("Build my show · Pulse Weaver"); wizard.setWizardStyle(QWizard::ModernStyle);
	wizard.setOption(QWizard::NoBackButtonOnStartPage); wizard.setOption(QWizard::HaveCustomButton1);
	wizard.setButtonText(QWizard::FinishButton, "Create my show"); wizard.setButtonText(QWizard::CancelButton, "Cancel");
	wizard.setButtonText(QWizard::CustomButton1, "Review"); wizard.resize(1100, 800);
	wizard.button(QWizard::CustomButton1)->setAccessibleName("Return to review");
	wizard.button(QWizard::CustomButton1)->setToolTip("Return to the final review, stopping at any step that needs an answer.");
	if (const auto *screen = editor ? editor->screen() : QGuiApplication::primaryScreen()) {
		const auto available = screen->availableGeometry().size();
		wizard.setMinimumSize(qMin(740, available.width() - 40), qMin(560, available.height() - 60));
		wizard.resize(qMin(1100, available.width() - 40), qMin(800, available.height() - 60));
	}
	for (auto role : {QWizard::NextButton, QWizard::FinishButton}) {
		auto *button = wizard.button(role);
		button->setProperty("setupPrimary", true);
		// QWizard creates these before the scoped setup style is assigned.
		button->style()->unpolish(button); button->style()->polish(button); button->update();
	}
	QVector<SetupPage *> pages;
	const QStringList phases{"Start", "Sections", "Looks", "Sources", "Review"};
	auto page = [&](const QString &title, const QString &subtitle) {
		auto *value = new SetupPage; value->setObjectName("PulseSetupPage");
		value->setAccessibleName(title); value->setAccessibleDescription(subtitle);
		const int index = pages.size(); pages.append(value);
		auto *outer = new QVBoxLayout(value); outer->setContentsMargins(14, 10, 14, 4); outer->setSpacing(12);
		auto *phaseRow = new QHBoxLayout; phaseRow->setSpacing(8); outer->addLayout(phaseRow);
		for (int step = 0; step < phases.size(); ++step) {
			auto *label = new QLabel(QString::number(step + 1) + "  " + phases[step], value);
			label->setWordWrap(true);
			label->setProperty("setupPhaseState", step == index ? "current" : step < index ? "complete" : "upcoming");
			label->setAccessibleName(phases[step] + (step == index ? ", current step" : step < index ? ", previous step" : ", upcoming step"));
			phaseRow->addWidget(label, 1);
		}
		setupText(outer, value, title, "PulseSetupTitle"); setupText(outer, value, subtitle, "PulseSetupSubtitle");
		auto *scroll = new QScrollArea(value); scroll->setObjectName("PulseSetupPageScroll"); scroll->setWidgetResizable(true); scroll->setFrameShape(QFrame::NoFrame);
		scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff); outer->addWidget(scroll, 1);
		auto *body = new QWidget(scroll); auto *layout = new QVBoxLayout(body); layout->setContentsMargins(0, 4, 10, 8); layout->setSpacing(14);
		layout->setAlignment(Qt::AlignTop); scroll->setWidget(body);
		value->error = new QLabel(value); value->error->setTextFormat(Qt::PlainText); value->error->setWordWrap(true); value->error->setObjectName("PulseSetupError");
		value->error->setAccessibleName("Setup issue"); value->error->hide(); outer->addWidget(value->error);
		wizard.addPage(value); return std::pair{value, layout};
	};

	auto [startPage, startLayout] = page("Start with your kind of show", "Pick a starting point. You can change every section and Look as you go.");
	startLayout->addWidget(new SetupCanvasIllustration(startPage));
	auto *startingPoint = new QComboBox(startPage); startingPoint->setAccessibleName("Show starting point");
	startingPoint->addItem("Full show", "full"); startingPoint->addItem("Game and content", "game"); startingPoint->addItem("Conversation", "chat"); startingPoint->hide();
	auto *startingCards = new QHBoxLayout; startingCards->setSpacing(12); startLayout->addLayout(startingCards);
	const QStringList startingDetails{
		"Starting, Chat, Game, Celebration and Intermission. A complete starting arrangement to customize.",
		"Start with a Game section and an optional supporting feed. Add other sections when you need them.",
		"Start with one main feed for Chat. A simple route for a camera, content or conversation."};
	QVector<QPushButton *> startingButtons;
	for (int i = 0; i < startingPoint->count(); ++i) {
		auto *card = PulseSetupChoices::card(startPage, startingPoint->itemText(i), startingDetails[i]);
		card->setAccessibleName(startingPoint->itemText(i) + " starting point"); card->setChecked(i == 0); startingCards->addWidget(card, 1);
		startingButtons.append(card);
		connect(card, &QPushButton::clicked, &wizard, [startingPoint, card, i] { startingPoint->setCurrentIndex(i); card->setChecked(true); });
		connect(startingPoint, &QComboBox::currentIndexChanged, card, [card, i](int selected) { card->setChecked(selected == i); });
	}
	auto *startSummary = setupText(startLayout, startPage, "", "PulseSetupSectionTitle");
	auto *modelSurface = setupSurface(startPage); auto *modelLayout = new QVBoxLayout(modelSurface); modelLayout->setContentsMargins(16, 12, 16, 12); modelLayout->setSpacing(5);
	setupText(modelLayout, modelSurface, "Stage → Look → Sources", "PulseSetupSectionTitle");
	setupText(modelLayout, modelSurface, "A Stage is a section of your show. Its Looks are arrangements you switch between. Sources supply the cameras, captures, media and artwork; a source can be reused.");
	startLayout->addWidget(modelSurface);
	auto *lumiaRow = new QHBoxLayout; startLayout->addLayout(lumiaRow);
	auto *lumiaHint = new QLabel("Using Lumia countdowns or artwork? You can prepare their browser links while planning.", startPage);
	lumiaHint->setObjectName("PulseSetupMuted"); lumiaHint->setWordWrap(true); lumiaRow->addWidget(lumiaHint, 1);
	auto *lumiaHelp = new QPushButton("Lumia setup…", startPage); lumiaRow->addWidget(lumiaHelp);
	connect(lumiaHelp, &QPushButton::clicked, &wizard, [&wizard] { openSetupLumiaHelp(&wizard); });
	startLayout->addStretch();

	auto [stagePage, stageLayout] = page("Choose the sections you use", "Each section becomes a Stage. Next, choose the Looks inside it.");
	auto *nameSurface = setupSurface(stagePage); auto *nameLayout = new QHBoxLayout(nameSurface); nameLayout->setContentsMargins(16, 12, 16, 12); nameLayout->setSpacing(16);
	auto *nameLabel = new QLabel("Show name", nameSurface); nameLabel->setObjectName("PulseSetupSectionTitle"); nameLayout->addWidget(nameLabel);
	auto *name = new QLineEdit("My Show", nameSurface); name->setMaxLength(90); name->setAccessibleName("Show name"); nameLabel->setBuddy(name); nameLayout->addWidget(name, 1); stageLayout->addWidget(nameSurface);
	const QMap<QString, QString> descriptions{
		{"Starting", "Welcome viewers, run a countdown or get ready to begin."},
		{"Chat", "Conversation, reactions and showing content."},
		{"Game", "Main gameplay, commentary and alternate views."},
		{"Projects", "Work areas, activities, demonstrations and detail views."},
		{"Celebration", "Reactions, announcements and special moments."},
		{"Intermission", "Be right back, keep an activity visible or end your show."}};
	QMap<QString, QCheckBox *> groups;
	auto *stageCards = new QGridLayout; stageCards->setSpacing(12); stageLayout->addLayout(stageCards); int stageCardIndex = 0;
	for (const auto &group : PulseShow::setupCatalogue()) {
		auto *check = new QCheckBox(group.name, stagePage); check->setChecked(group.name != "Projects");
		check->setAccessibleDescription(descriptions.value(group.name)); groups.insert(group.name, check);
		stageCards->addWidget(PulseSetupChoices::checkCard(check, stagePage, descriptions.value(group.name)), stageCardIndex / 2, stageCardIndex % 2); ++stageCardIndex;
	}
	auto *sectionSummary = setupText(stageLayout, stagePage, ""); stageLayout->addStretch();
	auto [lookPage, lookLayout] = page("Shape your Looks", "Preview a Look, choose whether to include it, then configure only what you want to change.");
	auto [sourcePage, sourceLayout] = page("Give your show its sources", "Assign the required feeds and artwork. Reuse an existing source wherever it should share the same content.");
	sourcePage->showProblem = false;
	auto [reviewPage, reviewLayout] = page("Review your show", "Check the actual assignments and what will be added before creating anything.");

	QFile manifestFile(QDir(QFileInfo(storagePath).absolutePath()).filePath("setup-reference.json")); QJsonObject manifest;
	if (manifestFile.open(QIODevice::ReadOnly)) manifest = QJsonDocument::fromJson(manifestFile.readAll()).object(); manifestFile.close();
	QFile stagesFile(QDir(QFileInfo(storagePath).absolutePath()).filePath("../../pulseweaver-stages.json")); QJsonArray savedStages;
	if (stagesFile.open(QIODevice::ReadOnly)) savedStages = QJsonDocument::fromJson(stagesFile.readAll()).array(); stagesFile.close();
	QSet<QString> allGroups; for (const auto &group : PulseShow::setupCatalogue()) allGroups.insert(group.name);
	QString suggested = "My Show";
	for (int suffix = 2; !wizardNameProblem(suggested, allGroups, savedStages).isEmpty(); ++suffix) suggested = "My Show " + QString::number(suffix);
	name->setText(suggested);
	auto selectedGroups = [&] { QSet<QString> selected; for (auto it = groups.cbegin(); it != groups.cend(); ++it) if (it.value()->isChecked()) selected.insert(it.key()); return selected; };
	stagePage->problem = [&] { return wizardNameProblem(name->text().trimmed(), selectedGroups(), savedStages); };
	FlexibleSetup setup(wizard, lookPage, lookLayout, sourcePage, sourceLayout, manifest, savedStages, actions);
	auto *reviewName = setupText(reviewLayout, reviewPage, "Show · " + name->text(), "PulseSetupSectionTitle");
	auto *reviewEdits = new PulseSetupChoices::ChoiceGrid(reviewPage, 4); reviewLayout->addWidget(reviewEdits);
	auto *editSections = new QPushButton("Edit sections", reviewPage), *editLooks = new QPushButton("Edit Looks", reviewPage), *fixSources = new QPushButton("Edit sources", reviewPage);
	reviewEdits->addCard(editSections); reviewEdits->addCard(editLooks); reviewEdits->addCard(fixSources);
	auto *refreshChecks = new QPushButton("Check again", reviewPage); refreshChecks->setAccessibleName("Refresh setup checks");
	refreshChecks->setToolTip("Recheck assignments and whether outputs have stopped. This does not play or connection-test your sources."); reviewEdits->addCard(refreshChecks); reviewEdits->reflow(true);
	connect(refreshChecks, &QPushButton::clicked, &wizard, [&] { reviewPage->changed(); setup.refreshReview(); });
	setup.buildReview(reviewPage, reviewLayout, [&wizard] { wizard.back(); });
	connect(editSections, &QPushButton::clicked, &wizard, [&] { while (wizard.currentId() > 1) wizard.back(); });
	connect(editLooks, &QPushButton::clicked, &wizard, [&] { while (wizard.currentId() > 2) wizard.back(); });
	connect(fixSources, &QPushButton::clicked, &wizard, [&] { setup.focusFirstProblem(); wizard.back(); });
	setupText(reviewLayout, reviewPage, "Creating adds the selected Stages, paired Looks and new source drafts. Existing source settings are retained. Output destinations are configured separately; this action does not start a broadcast or recording.", "PulseSetupNotice");
	lookPage->problem = [&] { return setup.problem(false); };
	sourcePage->problem = [&] { return setup.problem(true); };
	reviewPage->problem = [&] {
		if (PulseHasActiveOutputs()) return QString("Stop streaming, recording and other outputs before creating your show. Your choices stay here.");
		QString issue = stagePage->problem(); if (issue.isEmpty()) issue = setup.problem(true); return issue;
	};
	bool applyingPreset = false;
	auto update = [&] {
		const auto selected = selectedGroups(); setup.update(selected);
		stagePage->changed(); lookPage->changed(); sourcePage->changed(); reviewPage->changed();
		if (wizard.currentPage() == reviewPage) setup.refreshReview();
		sectionSummary->setText(QString("%1 sections selected. Each selected section needs at least one Look.").arg(selected.size()));
		const QSet<QString> full{"Starting", "Chat", "Game", "Celebration", "Intermission"};
		const int preset = selected == full ? 0 : selected == QSet<QString>{"Game"} ? 1 : selected == QSet<QString>{"Chat"} ? 2 : -1;
		if (!applyingPreset) { const QSignalBlocker blocker(startingPoint); startingPoint->setCurrentIndex(preset); }
		for (int index = 0; index < startingButtons.size(); ++index) startingButtons[index]->setChecked(index == preset);
		startSummary->setText(preset < 0 ? QString("Custom starting point · %1 sections selected").arg(selected.size()) : startingPoint->itemText(preset) + " · customize on the next page");
	};
	setup.changed = [&] { lookPage->changed(); sourcePage->changed(); reviewPage->changed(); if (wizard.currentPage() == reviewPage) setup.refreshReview(); };
	for (auto *check : groups) connect(check, &QCheckBox::toggled, &wizard, [&](bool) { if (!applyingPreset) update(); });
	connect(startingPoint, &QComboBox::currentIndexChanged, &wizard, [&](int index) {
		if (index < 0) return; applyingPreset = true;
		for (auto it = groups.cbegin(); it != groups.cend(); ++it) {
			it.value()->setChecked(index == 0 ? it.key() != "Projects" : it.key() == (index == 1 ? "Game" : "Chat"));
		}
		update(); applyingPreset = false;
	});
	connect(name, &QLineEdit::textChanged, &wizard, [&](const QString &) { stagePage->changed(); reviewPage->changed(); reviewName->setText("Show · " + name->text()); });
	bool visitedReview = false;
	connect(&wizard, &QWizard::customButtonClicked, &wizard, [&](int button) {
		if (button != QWizard::CustomButton1) return;
		while (wizard.currentId() >= 0 && wizard.currentId() < 4) {
			const int previous = wizard.currentId();
			if (!wizard.currentPage()->isComplete()) {
				if (wizard.currentId() == 3) setup.focusFirstProblem();
				if (auto *current = dynamic_cast<SetupPage *>(wizard.currentPage())) current->changed();
				break;
			}
			wizard.next(); if (wizard.currentId() == previous) break;
		}
	});
	const QStringList nextLabels{"Choose sections", "Choose Looks", "Assign sources", "Review show"};
	connect(&wizard, &QWizard::currentIdChanged, &wizard, [&](int id) {
		if (id < 0) return; update();
		if (id == 3) setup.showSourcePreview();
		if (id == 4) { visitedReview = true; setup.refreshReview(); }
		if (id < nextLabels.size()) wizard.setButtonText(QWizard::NextButton, nextLabels[id]);
		wizard.setOption(QWizard::HaveCustomButton1, visitedReview && id > 0 && id < 3);
	});
	update(); wizard.setOption(QWizard::HaveCustomButton1, false); wizard.setButtonText(QWizard::NextButton, nextLabels[0]);
	while (wizard.exec() == QDialog::Accepted) {
		const QString issue = reviewPage->problem();
		if (!issue.isEmpty()) { QMessageBox::warning(&wizard, "Your show needs attention", issue + "\nYour answers are preserved."); continue; }
		const QJsonObject result = createGuidedShow(setup.draft(name->text().trimmed()));
		if (result.value("ok").toBool()) { setStatus(result.value("message").toString()); showSetupCompletion(editor, result.value("message").toString()); return; }
		QMessageBox::warning(&wizard, "Show not created", result.value("message").toString() + "\nYour answers are preserved.");
	}
}
