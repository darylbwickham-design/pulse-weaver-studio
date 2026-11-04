#include "OBSAbout.hpp"

#include "../../shared/qt/PulseManual.hpp"
#include "../../shared/qt/PulseUpdateRelease.hpp"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QFile>
#include <QIcon>
#include <QJsonDocument>
#include <QPixmap>
#include <QPushButton>
#include <QUrl>

#include "moc_OBSAbout.cpp"

OBSAbout::OBSAbout(QWidget *parent) : QDialog(parent), ui(new Ui::OBSAbout)
{
	setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
	ui->setupUi(this);
	setWindowTitle("About Pulse Weaver");
	setWindowIcon(QIcon(":/res/images/pulseweaver.png"));

	PulseUpdates::Identity release;
	QFile identity(QCoreApplication::applicationDirPath() + "/pulseweaver-update.json");
	if (identity.open(QIODevice::ReadOnly) && identity.size() <= 16384)
		release = PulseUpdates::identityFromJson(QJsonDocument::fromJson(identity.readAll()).object());
	const QString version = release.tag.isEmpty() ? "Development build" : release.tag.mid(1);

	ui->icon->setPixmap(QPixmap(":/res/images/pulseweaver.png"));
	ui->icon->setFixedSize(144, 144);
	ui->name->setText("Pulse Weaver Studio");
	ui->version->setText("Version " + version);
	ui->info->setText("Stream production with Stages, Looks, scenes, lighting and connected show actions.");
	ui->name->setProperty("class", "text-heading");
	ui->version->setProperty("class", "text-large");
	ui->contribute->hide();
	ui->donate->hide();
	ui->getInvolved->hide();
	ui->about->hide();
	ui->authors->hide();
	ui->license->hide();

	ui->textBrowser->setHtml(
		"<h2>Pulse Weaver Studio</h2>"
		"<p>Use the Nerds' Introduction for the complete guide to Stages, Looks, scenes, "
		"stream workflows, the Lumia companion and release history. The bundled manual "
		"opens in your browser, where it can be read or printed.</p>"
		"<p>Pulse Weaver uses the OBS engine. Open source notices and licenses are "
		"included with the installation.</p>");

	auto *manual = new QPushButton("Open and print manual", this);
	manual->setEnabled(PulseManual::exists());
	manual->setToolTip(manual->isEnabled() ? "Open the bundled offline manual in your browser" :
					    "The bundled manual was not found in this installation");
	ui->horizontalLayout->addWidget(manual);
	connect(manual, &QPushButton::clicked, this, [this] { PulseManual::open(this); });

	auto *releaseNotes = new QPushButton("Release notes", this);
	ui->horizontalLayout->addWidget(releaseNotes);
	connect(releaseNotes, &QPushButton::clicked, this, [release] {
		const QString tag = release.tag.isEmpty() ? "" : "/tag/" + release.tag;
		QDesktopServices::openUrl(QUrl(PulseUpdates::repository + "/releases" + tag));
	});

	auto *closeButton = new QPushButton("Close", this);
	ui->horizontalLayout->addWidget(closeButton);
	connect(closeButton, &QPushButton::clicked, this, &QDialog::accept);
}

void OBSAbout::ShowAbout() {}
void OBSAbout::ShowAuthors() {}
void OBSAbout::ShowLicense() {}
