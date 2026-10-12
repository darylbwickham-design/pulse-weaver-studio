#include "pulse-motion-engine.hpp"

#include <QCheckBox>
#include <QBuffer>
#include <QComboBox>
#include <QDesktopServices>
#include <QFormLayout>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QJsonDocument>
#include <QImageReader>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QScreen>
#include <QGuiApplication>
#include <QStackedWidget>
#include <QTabWidget>
#include <QUrl>
#include <QVBoxLayout>
#include <QWizard>
#include <QWizardPage>
#include <functional>
#include "pulse-setup-flexible.hpp"
#include "pulse-lumia-brand.hpp"
#include "../../shared/qt/PulseInstructions.hpp"
#include "../../shared/qt/PulseOutputActivity.hpp"

namespace {
class SetupPage final : public QWizardPage {
public:
	std::function<QString()> problem;
	QLabel *error = nullptr;
	bool isComplete() const override { return !problem || problem().isEmpty(); }
	bool validatePage() override {
		const QString message = problem ? problem() : QString();
		if (error) error->setText(message);
		return message.isEmpty();
	}
	void changed() { if (error) error->setText(problem ? problem() : QString()); emit completeChanged(); }
};
}

void PulseMotionEngine::openShowWizard()
{
	if (!mayLeaveDraft()) return;
	QWizard wizard(editor);
	wizard.setWindowTitle("Set up my show · Pulse Weaver");
	wizard.setWizardStyle(QWizard::ModernStyle);
	wizard.setOption(QWizard::NoBackButtonOnStartPage);
	wizard.setButtonText(QWizard::FinishButton, "Create my show");
	wizard.resize(1040, 820);
	if (const auto *screen = QGuiApplication::primaryScreen()) {
		const auto available = screen->availableGeometry().size();
		wizard.resize(qMin(1040, available.width() - 40), qMin(820, available.height() - 60));
	}
	auto page = [&](const QString &title, const QString &subtitle) {
		auto *value = new SetupPage;
		value->setTitle(title); value->setSubTitle(subtitle);
		auto *outer = new QVBoxLayout(value);
		auto *scroll = new QScrollArea(value); scroll->setWidgetResizable(true);
		auto *body = new QWidget(scroll); auto *layout = new QVBoxLayout(body);
		scroll->setWidget(body); outer->addWidget(scroll);
		value->error = new QLabel(value); value->error->setWordWrap(true);
		value->error->setStyleSheet("color:#ffb8a8"); outer->addWidget(value->error);
		wizard.addPage(value);
		return std::pair{value, layout};
	};
	auto text = [](QVBoxLayout *layout, QWidget *parent, const QString &message) {
		auto *label = new QLabel(message, parent); label->setWordWrap(true); layout->addWidget(label); return label;
	};
	auto [connectPage, connectLayout] = page("1  Prepare your show", "Set up Lumia and prepare your artwork.");
	text(connectLayout, connectPage, "A Stage is a part of your stream, such as Starting or Game. A Look is a layout you switch to within that Stage. Sources supply cameras, captures, media and overlays. Each Look has a landscape canvas and a portrait canvas.");
	auto *lumiaBrand = new QLabel(connectPage);
	QBuffer lumiaSvg;
	lumiaSvg.setData(QByteArray(pulseLumiaLogo)); lumiaSvg.open(QIODevice::ReadOnly);
	QImageReader logoReader(&lumiaSvg, "svg");
	const qreal logoRatio = lumiaBrand->devicePixelRatioF();
	// Rasterize the vector at display resolution instead of enlarging its 121×40 intrinsic bitmap.
	logoReader.setScaledSize(logoReader.size().scaled(QSize(qRound(242 * logoRatio), qRound(80 * logoRatio)), Qt::KeepAspectRatio));
	QPixmap lumiaLogo = QPixmap::fromImage(logoReader.read());
	lumiaLogo.setDevicePixelRatio(logoRatio);
	if (!lumiaLogo.isNull()) lumiaBrand->setPixmap(lumiaLogo);
	else lumiaBrand->setText("Lumia Stream");
	lumiaBrand->setAccessibleName("Lumia Stream"); connectLayout->addWidget(lumiaBrand);
	text(connectLayout, connectPage, "Lumia Stream provides the Lumia overlay templates for Pulse Weaver, including the countdown and widgets, and is required to use them. Install Lumia, connect its Pulse Weaver plugin, import the templates you need and copy their browser links for page 4. You can also reuse the Lumia browser sources already in your setup. Lumia is the recommended way to control animations, Stage / Look changes and show actions; Pulse Weaver arranges your cameras and content and moves them between Looks.");
	auto *download = new QPushButton("Download Lumia Stream", connectPage);
	connect(download, &QPushButton::clicked, &wizard, [] { QDesktopServices::openUrl(QUrl("https://www.lumiastream.com/downloads")); });
	connectLayout->addWidget(download);
	text(connectLayout, connectPage, "In Lumia, add the Pulse Weaver plugin and follow its connection instructions. In Overlays, import each template your chosen Looks use, including its landscape (H) and portrait (V) versions. Customize your logo, colours and text there.");
	auto *marketplace = new QPushButton("Get Lumia overlays", connectPage);
	connect(marketplace, &QPushButton::clicked, &wizard, [&wizard] { PulseInstructions::openDialog(&wizard); });
	connectLayout->addWidget(marketplace);
	text(connectLayout, connectPage, "A template share link imports an overlay into your Lumia library. After importing, use the overlay's chain / copy-link button to obtain your own browser link, then assign it on page 4. Match H to landscape and V to portrait. Add chat, alerts, frames and Newsroom only when you use them. Existing sources can be kept.");
	text(connectLayout, connectPage, "The Lumia bulb beside the readiness status on Show Control keeps the setup instructions available after this wizard: overlay imports, starting the countdown, running your chosen command at zero, Chat Physics and camera clearances, Stage / Look changes, Fortnite commands and stingers. You can also read them here while setting up your show.");
	auto *instructions = new QPushButton("Read Lumia setup instructions", connectPage);
	connect(instructions, &QPushButton::clicked, &wizard, [&wizard] { PulseInstructions::openDialog(&wizard); });
	connectLayout->addWidget(instructions);
	for (const QString &label : {QString("Lumia Stream is installed"), QString("The Pulse Weaver plugin is connected"),
		QString("My Lumia templates are imported, or their browser sources are already available")}) connectLayout->addWidget(new QCheckBox(label, connectPage));
	text(connectLayout, connectPage, "You can prepare your stage and source choices while completing the Lumia setup. These checkboxes are readiness reminders; they do not verify the connection or run overlay commands. Configure the countdown's completion command in Lumia using the guide so it can select your next Stage / Look when the timer reaches zero.");
	text(connectLayout, connectPage, "Choose existing sources or add new ones while assigning your feeds on page 4. For optional stage transitions, page 3 lets you reuse a stinger or add a video and its cut point. Choose landscape and portrait versions where needed; new sources and stingers are added only when you create your show.");
	connectLayout->addStretch();

	auto [stagePage, stageLayout] = page("2  Choose stage groups", "Which parts of your stream should this show include?");
	auto *name = new QLineEdit("My Show", stagePage); name->setMaxLength(90); name->setAccessibleName("Show name");
	stageLayout->addWidget(new QLabel("Show name", stagePage)); stageLayout->addWidget(name);
	text(stageLayout, stagePage, "Choose the groups you use. On the next page, choose their Looks and layout options for landscape and portrait.");
	const QMap<QString, QString> descriptions{
		{"Starting", "Countdowns, welcomes and getting ready to begin"},
		{"Chat", "Conversation, reactions and showcasing content"},
		{"Game", "Main gameplay, commentary and secondary views"},
		{"Projects", "Activities, work areas, demonstrations and detail views"},
		{"Celebration", "Reactions, announcements and moments worth celebrating"},
		{"Intermission", "BRB, continued activity and ending"}};
	QMap<QString, QCheckBox *> groups;
	for (const auto &group : PulseShow::setupCatalogue()) {
		auto *check = new QCheckBox(group.name, stagePage); check->setChecked(group.name != "Projects");
		check->setAccessibleDescription(descriptions.value(group.name)); groups.insert(group.name, check);
		stageLayout->addWidget(check); text(stageLayout, stagePage, descriptions.value(group.name));
	}
	stageLayout->addStretch();
	stagePage->problem = [&] {
		if (name->text().trimmed().isEmpty()) return QString("Enter a show name.");
		for (auto *check : groups) if (check->isChecked()) return QString();
		return QString("Choose at least one stage group.");
	};
	auto [lookPage, lookLayout] = page("3  Choose Looks and options", "Select your operating presentations and their layout options.");
	text(lookLayout, lookPage, "Choose an on-air focus swap only if you will use that alternate presentation. Landscape, portrait and layout options belong to each Look.");
	auto [sourcePage, sourceLayout] = page("4  Assign sources and review", "Assign feeds and each Look's artwork, then review both canvas diagrams and create your show.");
	QFile manifestFile(QDir(QFileInfo(storagePath).absolutePath()).filePath("setup-reference.json"));
	QJsonObject manifest;
	if (manifestFile.open(QIODevice::ReadOnly)) manifest = QJsonDocument::fromJson(manifestFile.readAll()).object();
	manifestFile.close();
	QFile stagesFile(QDir(QFileInfo(storagePath).absolutePath()).filePath("../../pulseweaver-stages.json"));
	QJsonArray savedStages;
	if (stagesFile.open(QIODevice::ReadOnly)) savedStages = QJsonDocument::fromJson(stagesFile.readAll()).array();
	stagesFile.close();
	FlexibleSetup setup(wizard, lookPage, lookLayout, sourcePage, sourceLayout, manifest, savedStages, actions);
	text(sourceLayout, sourcePage, "After creating your show, select the Lumia logo on Show Control to finish its Lumia commands. Select your new Stages / Looks in the countdown and scene-switch actions, and add the accompanying alert commands once for both canvases.");
	lookPage->problem = [&] { return setup.problem(false); };
	sourcePage->problem = [&] {
		if (PulseHasActiveOutputs()) return QString("Stop streaming, recording and other outputs before creating your show. Your choices will stay here.");
		QString error = stagePage->problem(); if (error.isEmpty()) error = setup.problem(true); return error;
	};
	auto update = [&] {
		QSet<QString> selected;
		for (auto it = groups.begin(); it != groups.end(); ++it) if (it.value()->isChecked()) selected.insert(it.key());
		setup.update(selected); stagePage->changed(); lookPage->changed(); sourcePage->changed();
	};
	setup.changed = [&] { lookPage->changed(); sourcePage->changed(); };
	for (auto *check : groups) connect(check, &QCheckBox::toggled, &wizard, [&](bool) { update(); });
	connect(name, &QLineEdit::textChanged, &wizard, [&](const QString &) { stagePage->changed(); sourcePage->changed(); });
	connect(&wizard, &QWizard::currentIdChanged, &wizard, [&](int) { update(); });
	update();
	while (wizard.exec() == QDialog::Accepted) {
		const QJsonObject result = createGuidedShow(setup.draft(name->text().trimmed()));
		if (result.value("ok").toBool()) { setStatus(result.value("message").toString()); return; }
		QMessageBox::warning(&wizard, "Show not created", result.value("message").toString() + "\nYour answers are preserved.");
	}
}
