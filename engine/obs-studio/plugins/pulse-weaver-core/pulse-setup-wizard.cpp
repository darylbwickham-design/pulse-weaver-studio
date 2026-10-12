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
QString wizardNameProblem(const QString &prefix, const QSet<QString> &groups, const QJsonArray &stages)
{
	if (prefix.isEmpty() || prefix.size() > 90) return "Enter a show name of 1–90 characters.";
	if (groups.isEmpty()) return "Choose at least one stage group.";
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
	text(connectLayout, connectPage, "Lumia Stream is required for the supplied overlays, countdown and widgets. It also lets you control animations and Stage / Look changes. Pulse Weaver arranges your feeds and moves them between Looks.");
	auto task = [&](const QString &title) {
		auto *box = new QGroupBox(title, connectPage); auto *body = new QVBoxLayout(box);
		body->setAlignment(Qt::AlignTop); connectLayout->addWidget(box); return body;
	};
	auto *install = task("1  Install Lumia Stream");
	auto *download = new QPushButton("Download Lumia Stream", connectPage);
	connect(download, &QPushButton::clicked, &wizard, [] { QDesktopServices::openUrl(QUrl("https://www.lumiastream.com/downloads")); });
	install->addWidget(download); install->addWidget(new QCheckBox("Lumia Stream is installed", connectPage));
	auto *plugin = task("2  Connect the Pulse Weaver plugin");
	text(plugin, connectPage, "Download the companion plugin below. Import its .lumiaplugin file in Lumia, then follow its connection instructions for this Pulse Weaver installation.");
	auto *pluginDownload = new QPushButton("Download the Pulse Weaver Lumia plugin", connectPage);
	connect(pluginDownload, &QPushButton::clicked, &wizard, [] { QDesktopServices::openUrl(QUrl("https://github.com/darylbwickham-design/pulse-weaver-studio/releases/latest/download/PulseWeaver-Lumia-1.4.4.lumiaplugin")); });
	plugin->addWidget(pluginDownload); plugin->addWidget(new QCheckBox("The Pulse Weaver plugin is connected", connectPage));
	auto *artwork = task("3  Import the overlays you need");
	text(artwork, connectPage, "Import matching landscape (H) and portrait (V) templates in Lumia. Customize their logo, colours and text, then use the chain / copy-link button to get each browser link for page 4. You can reuse existing Lumia browser sources.");
	auto *instructions = new QPushButton("Get Lumia overlays and setup instructions", connectPage);
	connect(instructions, &QPushButton::clicked, &wizard, [&wizard] { PulseInstructions::openDialog(&wizard); });
	artwork->addWidget(instructions); artwork->addWidget(new QCheckBox("My Lumia templates or browser sources are ready", connectPage));
	text(connectLayout, connectPage, "These checkboxes are reminders. You can continue planning while finishing Lumia setup. The Lumia bulb on Show Control keeps the countdown, Chat Box Physics, Newsroom, alert and stinger instructions available after this wizard.");
	text(connectLayout, connectPage, "Next, choose your stage groups and Looks. Page 3 offers optional stingers; page 4 lets you reuse or create sources. Nothing is added until you choose Create my show.");
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
	auto *stageCards = new QGridLayout; stageLayout->addLayout(stageCards); int stageCardIndex = 0;
	for (const auto &group : PulseShow::setupCatalogue()) {
		auto *check = new QCheckBox(group.name, stagePage); check->setChecked(group.name != "Projects");
		check->setAccessibleDescription(descriptions.value(group.name)); groups.insert(group.name, check);
		stageCards->addWidget(PulseSetupChoices::checkCard(check, stagePage, descriptions.value(group.name)),
			stageCardIndex / 2, stageCardIndex % 2); ++stageCardIndex;
	}
	stageLayout->addStretch();
	stagePage->problem = [&] {
		if (name->text().trimmed().isEmpty()) return QString("Enter a show name.");
		for (auto *check : groups) if (check->isChecked()) return QString();
		return QString("Choose at least one stage group.");
	};
	auto [lookPage, lookLayout] = page("3  Choose Looks and options", "Select your operating presentations and their layout options.");
	text(lookLayout, lookPage, "Click the Look cards you need, then move through your stages. Recommended layouts are ready to use; open Layout options to adjust one. Stage transitions contains Fade, Cut, stingers and the Profounder link.");
	auto [sourcePage, sourceLayout] = page("4  Assign sources and review", "Assign feeds and each Look's artwork, then review both canvas diagrams and create your show.");
	QFile manifestFile(QDir(QFileInfo(storagePath).absolutePath()).filePath("setup-reference.json"));
	QJsonObject manifest;
	if (manifestFile.open(QIODevice::ReadOnly)) manifest = QJsonDocument::fromJson(manifestFile.readAll()).object();
	manifestFile.close();
	QFile stagesFile(QDir(QFileInfo(storagePath).absolutePath()).filePath("../../pulseweaver-stages.json"));
	QJsonArray savedStages;
	if (stagesFile.open(QIODevice::ReadOnly)) savedStages = QJsonDocument::fromJson(stagesFile.readAll()).array();
	stagesFile.close();
	QSet<QString> allGroups; for (const auto &group : PulseShow::setupCatalogue()) allGroups.insert(group.name);
	QString suggested = "My Show";
	for (int suffix = 2; !wizardNameProblem(suggested, allGroups, savedStages).isEmpty(); ++suffix)
		suggested = "My Show " + QString::number(suffix);
	name->setText(suggested);
	stagePage->problem = [&] {
		QSet<QString> selected; for (auto it = groups.begin(); it != groups.end(); ++it) if (it.value()->isChecked()) selected.insert(it.key());
		return wizardNameProblem(name->text().trimmed(), selected, savedStages);
	};
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
	connect(&wizard, &QWizard::currentIdChanged, &wizard, [&](int) { update(); if (wizard.currentPage() == sourcePage) setup.showSourcePreview(); });
	update();
	while (wizard.exec() == QDialog::Accepted) {
		const QJsonObject result = createGuidedShow(setup.draft(name->text().trimmed()));
		if (result.value("ok").toBool()) { setStatus(result.value("message").toString()); return; }
		QMessageBox::warning(&wizard, "Show not created", result.value("message").toString() + "\nYour answers are preserved.");
	}
}
