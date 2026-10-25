#pragma once

#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSet>
#include <QScreen>
#include <QScrollArea>
#include <QSpinBox>
#include <QStyle>
#include <QStringList>
#include <QVBoxLayout>

namespace PulseSetupStingers {
inline QString fileProblem(const QString &path)
{
	const QFileInfo file(path.trimmed());
	if (!file.isAbsolute() || !file.isFile() || !file.isReadable()) return "Choose a readable video file on this computer.";
	const QStringList formats{"webm", "mov", "mp4", "mkv", "avi", "m4v", "flv", "mpg", "mpeg", "ts", "m2ts", "mxf", "ogv"};
	if (!formats.contains(file.suffix().toLower())) return "Choose a video file, such as MP4, WebM or MOV. An image or document cannot be used as a stinger.";
	return {};
}
// Collect a draft only. No native transition or source is created until Finish.
inline QJsonObject ask(QWidget *parent, bool portrait, const QSet<QString> &usedNames, const QJsonObject &prior = {})
{
	QDialog dialog(parent);
	dialog.setObjectName("PulseWeaverSetupDialog"); dialog.setProperty("pulseSetupDialog", true);
	dialog.setWindowTitle(prior.isEmpty() ? "Add a stinger" : "Edit new stinger");
	dialog.resize(560, 460);
	if (const auto *screen = parent ? parent->screen() : QGuiApplication::primaryScreen()) {
		const auto available = screen->availableGeometry().size();
		dialog.resize(qMin(560, available.width() - 40), qMin(460, available.height() - 60));
	}
	auto *outer = new QVBoxLayout(&dialog);
	auto *scroll = new QScrollArea(&dialog); scroll->setWidgetResizable(true); outer->addWidget(scroll, 1);
	auto *fields = new QWidget(scroll); auto *body = new QVBoxLayout(fields); body->setAlignment(Qt::AlignTop); scroll->setWidget(fields);
	auto *hint = new QLabel(portrait ? "Choose a portrait transition video for this stage." :
		"Choose a landscape transition video for this stage.", &dialog);
	hint->setWordWrap(true); body->addWidget(hint);
	auto *form = new QFormLayout; form->setRowWrapPolicy(QFormLayout::WrapLongRows); body->addLayout(form);
	const QString baseName = portrait ? "Portrait stinger" : "Landscape stinger";
	QString suggestedName = baseName;
	for (int suffix = 2; usedNames.contains(suggestedName); ++suffix) suggestedName = baseName + " " + QString::number(suffix);
	auto *name = new QLineEdit(prior.value("name").toString(suggestedName), &dialog);
	name->setMaxLength(90); name->setAccessibleName("New stinger name"); form->addRow("Name", name);
	auto *fileRow = new QWidget(&dialog); auto *fileLayout = new QHBoxLayout(fileRow); fileLayout->setContentsMargins(0, 0, 0, 0);
	auto *path = new QLineEdit(prior.value("path").toString(), fileRow); path->setAccessibleName("New stinger video file");
	path->setPlaceholderText("Choose a video on this computer"); fileLayout->addWidget(path);
	auto *browse = new QPushButton("Browse…", fileRow); fileLayout->addWidget(browse); form->addRow("Video file", fileRow);
	auto *point = new QSpinBox(&dialog); point->setRange(0, 120000); point->setValue(prior.value("transitionPointMs").toInt(1000)); point->setSuffix(" ms");
	point->setAccessibleName("New stinger cut point"); form->addRow("Cut point", point);
	auto *timing = new QLabel("Set the time when the video fully covers the screen and the next stage should appear. Use the timing supplied with your stinger. After creation, preview and adjust it in Camera's Scene Transitions panel. If hidden, show it through Studio → Docks → Scene Transitions.", &dialog);
	timing->setWordWrap(true); body->addWidget(timing);
	auto *ownership = new QLabel("The video stays in its current location. This stinger is added only when you choose Create my show. You can reuse it in other stage transition selectors.", &dialog);
	ownership->setWordWrap(true); body->addWidget(ownership);
	auto *error = new QLabel(&dialog); error->setWordWrap(true); error->setStyleSheet("color:#ffb8a8"); body->addWidget(error);
	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dialog); outer->addWidget(buttons);
	buttons->button(QDialogButtonBox::Save)->setText("Use this stinger");
	buttons->button(QDialogButtonBox::Save)->setProperty("setupPrimary", true);
	auto *save = buttons->button(QDialogButtonBox::Save);
	save->style()->unpolish(save); save->style()->polish(save); save->update();
	auto validate = [&] {
		QString problem;
		const QString value = name->text().trimmed();
		if (value.isEmpty()) problem = "Enter a name for this stinger.";
		else if (usedNames.contains(value)) problem = "That transition name is already used. Choose another name.";
		else problem = fileProblem(path->text());
		error->setText(problem); buttons->button(QDialogButtonBox::Save)->setEnabled(problem.isEmpty());
	};
	QObject::connect(name, &QLineEdit::textChanged, &dialog, [&](const QString &) { validate(); });
	QObject::connect(path, &QLineEdit::textChanged, &dialog, [&](const QString &) { validate(); });
	QObject::connect(browse, &QPushButton::clicked, &dialog, [&] {
		const QString file = QFileDialog::getOpenFileName(&dialog, "Choose a stinger video", path->text(),
			"Video files (*.webm *.mov *.mp4 *.mkv *.avi *.m4v *.flv *.mpg *.mpeg *.ts *.m2ts *.mxf *.ogv);;All files (*)");
		if (!file.isEmpty()) path->setText(file);
	});
	QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
	QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	validate(); error->clear();
	if (dialog.exec() != QDialog::Accepted) return {};
	return {{"name", name->text().trimmed()}, {"path", QFileInfo(path->text().trimmed()).absoluteFilePath()},
		{"transitionPointMs", point->value()}};
}
}
