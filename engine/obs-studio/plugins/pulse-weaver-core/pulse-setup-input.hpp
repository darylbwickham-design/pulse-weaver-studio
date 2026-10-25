#pragma once

#include "pulse-setup-choices.hpp"
#include "pulse-source-canvas.hpp"
#include <obs.hpp>
#include <QCheckBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QImageReader>
#include <QJsonObject>
#include <QLineEdit>
#include <QSignalBlocker>
#include <QStyle>
#include <tuple>

namespace PulseSetupInput {
inline void primary(QAbstractButton *button)
{
	button->setProperty("setupPrimary", true);
	button->style()->unpolish(button); button->style()->polish(button); button->update();
}
inline void fitDialog(QDialog &dialog, int width, int height)
{
	const auto *screen = dialog.parentWidget() ? dialog.parentWidget()->screen() : QGuiApplication::primaryScreen();
	const QSize available = screen ? screen->availableGeometry().size() - QSize(40, 64) : QSize(width, height);
	dialog.resize(qMin(width, available.width()), qMin(height, available.height()));
}
inline QLabel *text(QVBoxLayout *layout, QWidget *parent, const QString &value, const char *role = "PulseSetupMuted")
{
	auto *label = new QLabel(value, parent); label->setTextFormat(Qt::PlainText); label->setWordWrap(true);
	label->setObjectName(role); layout->addWidget(label); return label;
}
inline QStringList reservedInputNames(const QJsonObject &spec)
{
	const QString name = spec.value("name").toString().trimmed();
	QStringList names{name};
	if (spec.value("kind") == "browser_source") names << name + " · horizontal" << name + " · vertical";
	return names;
}
inline QString uniqueName(const QString &base, const QJsonObject &drafts, const QString &kind = {})
{
	auto used = [&](const QString &name) {
		for (const QString &candidate : reservedInputNames({{"name", name}, {"kind", kind}})) {
			OBSSourceAutoRelease source = obs_get_source_by_name(candidate.toUtf8().constData());
			if (source) return true;
			for (const auto &value : drafts) if (reservedInputNames(value.toObject()).contains(candidate)) return true;
		}
		return false;
	};
	QString name = base;
	for (int suffix = 2; used(name); ++suffix) name = base + " " + QString::number(suffix);
	return name;
}
inline QString draftProblem(const QJsonObject &spec, const QJsonObject &drafts = {}, const QString &editing = {}, bool overlay = false)
{
	const QString name = spec.value("name").toString().trimmed(), kind = spec.value("kind").toString();
	if (name.isEmpty() || name.size() > 90) return "Give this input a name of 1–90 characters.";
	const QStringList names = reservedInputNames(spec);
	for (const auto &candidate : names) {
		OBSSourceAutoRelease source = obs_get_source_by_name(candidate.toUtf8().constData());
		if (source) return "An input named “" + candidate + "” already exists. Reuse it or choose another name.";
		for (auto it = drafts.begin(); it != drafts.end(); ++it)
			if (it.key() != editing && reservedInputNames(it.value().toObject()).contains(candidate))
				return "Another new input already uses this name.";
	}
	const QSet<QString> types{"dshow_input", "window_capture", "monitor_capture", "game_capture", "ffmpeg_source", "image_source", "browser_source"};
	if (!types.contains(kind)) return "Choose an available input type.";
	if (overlay && kind != "browser_source") return "Alert layers use browser overlays.";
	const QString target = spec.value("target").toString();
	if (kind == "browser_source") {
		const QString url = spec.value("url").toString();
		const QUrl parsed(url.trimmed());
		if (!parsed.isValid() || parsed.host().isEmpty() || (parsed.scheme() != "http" && parsed.scheme() != "https"))
			return "Enter the complete HTTP or HTTPS browser link from your provider.";
		if (parsed.host().compare("api.lumiastream.com", Qt::CaseInsensitive) == 0 && parsed.path().startsWith("/shared/download/"))
			return "Import this template in Lumia, then use its browser / copy-link URL here.";
		if (pulseIsNativeOverlayUrl(url)) return "Choose an external browser link. Old generated Pulse Weaver overlays cannot be used here.";
		if (overlay && !pulseWizardOverlayUrlAllowed(url)) return "Use an alert browser link from Lumia, StreamElements, Streamlabs or another provider.";
	} else if (kind == "image_source" || kind == "ffmpeg_source") {
		const QFileInfo file(target);
		if (!file.isFile() || !file.isReadable()) return "Choose a readable file on this computer.";
		if (kind == "image_source" && !QImageReader(target).canRead()) return "This file cannot be read as an image. Choose a supported image.";
	} else if (kind != "game_capture" && target.isEmpty()) return "Choose the device, window or display you want to show.";
	return {};
}

// This dialog collects settings only. It never opens a camera, decoder or browser.
inline QJsonObject askDraft(QWidget *parent, const QString &title, const QString &preferred,
	const QJsonObject &drafts, const QJsonObject &prior = {}, const QString &editing = {})
{
	QDialog dialog(parent); dialog.setObjectName("PulseGuidedSourceDialog"); dialog.setProperty("pulseSetupDialog", true);
	dialog.setWindowTitle(title); fitDialog(dialog, 720, 560);
	auto *outer = new QVBoxLayout(&dialog); outer->setContentsMargins(22, 18, 22, 18); outer->setSpacing(12);
	text(outer, &dialog, title, "PulseSetupTitle");
	text(outer, &dialog, "Choose what supplies this picture. Nothing new is opened until you create the show.");
	auto *scroll = new QScrollArea(&dialog); scroll->setWidgetResizable(true); scroll->setFrameShape(QFrame::NoFrame); outer->addWidget(scroll, 1);
	auto *body = new QWidget(scroll); auto *layout = new QVBoxLayout(body); layout->setAlignment(Qt::AlignTop); scroll->setWidget(body);
	auto *form = new QFormLayout; form->setRowWrapPolicy(QFormLayout::WrapLongRows); layout->addLayout(form);
	auto *name = new QLineEdit(prior.value("name").toString(uniqueName(title, drafts, prior.value("kind").toString(preferred))), body);
	name->setMaxLength(90); name->setAccessibleName("Input name"); form->addRow("Name", name);
	auto *kind = new QComboBox(body); kind->setAccessibleName("Input type"); kind->hide();
	const std::tuple<const char *, const char *, const char *> kinds[] = {
		{"Camera", "dshow_input", "A connected webcam, capture card or camera device."},
		{"Window or call", "window_capture", "One application, presentation or guest-call window."},
		{"Display", "monitor_capture", "Everything on one display. Check for private windows before streaming."},
		{"Game", "game_capture", "A fullscreen game or a specific game window."},
		{"Image", "image_source", "A picture or test image stored on this computer."},
		{"Video file", "ffmpeg_source", "A local video. Playback begins when the created source is used."},
		{"Browser link", "browser_source", "A guest feed or web picture supplied by an HTTP/HTTPS URL."}};
	QSet<QString> available;
	for (size_t index = 0;; ++index) { const char *id = nullptr, *unversioned = nullptr;
		if (!obs_enum_input_types2(index, &id, &unversioned)) break;
		available.insert(QString::fromUtf8(unversioned ? unversioned : id)); }
	for (const auto &[label, id, detail] : kinds) if (available.contains(id)) {
		const int index = kind->count(); kind->addItem(label, id); kind->setItemData(index, detail, Qt::ToolTipRole);
	}
	const QString wanted = prior.value("kind").toString(preferred);
	kind->setCurrentIndex(qMax(0, kind->findData(wanted)));
	auto *changeType = new QPushButton(body); changeType->setAccessibleName("Change input type"); form->addRow("Picture from", changeType);
	auto *target = new QComboBox(body); target->setAccessibleName("Input device or window"); target->hide();
	auto *chooseTarget = new QPushButton(body); chooseTarget->setAccessibleName("Choose input device or window"); form->addRow("Device or window", chooseTarget);
	auto *refresh = new QPushButton("Refresh devices and windows", body); refresh->setAccessibleName("Refresh input choices"); layout->addWidget(refresh);
	auto *pathRow = new QWidget(body); auto *pathLayout = new QHBoxLayout(pathRow); pathLayout->setContentsMargins(0, 0, 0, 0);
	const QString priorKind = prior.value("kind").toString();
	auto *path = new QLineEdit(priorKind == "image_source" || priorKind == "ffmpeg_source" ? prior.value("target").toString() : QString(), pathRow); path->setAccessibleName("Input file"); pathLayout->addWidget(path);
	auto *browse = new QPushButton("Browse…", pathRow); pathLayout->addWidget(browse); form->addRow("File", pathRow);
	auto *url = new QLineEdit(prior.value("url").toString(), body); url->setAccessibleName("Input browser link"); url->setPlaceholderText("https://…"); form->addRow("Browser link", url);
	auto *loop = new QCheckBox("Loop video", body); loop->setChecked(prior.value("loop").toBool()); form->addRow(loop);
	auto *hint = text(layout, body, "");
	auto *browserAudio = text(layout, body, "New browser copies start silent. Choose one audible copy after creation.", "PulseSetupNotice");
	auto *error = text(outer, &dialog, "", "PulseSetupError"); error->hide();
	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog); outer->addWidget(buttons);
	buttons->button(QDialogButtonBox::Ok)->setText("Use this input"); primary(buttons->button(QDialogButtonBox::Ok));
	QString loadedKind, missingTarget;
	auto snapshot = [&] {
		const QString type = kind->currentData().toString();
		return QJsonObject{{"name", name->text().trimmed()}, {"kind", type}, {"target", type == "image_source" || type == "ffmpeg_source" ? path->text().trimmed() : target->currentData().toString()},
			{"url", url->text().trimmed()}, {"loop", loop->isChecked()}};
	};
	auto validate = [&] {
		QString issue = draftProblem(snapshot(), drafts, editing);
		if (!missingTarget.isEmpty() && target->currentData().toString() == missingTarget)
			issue = "The previous device or window is unavailable. Choose it again, or choose another target.";
		error->setText(issue); error->setVisible(!issue.isEmpty()); buttons->button(QDialogButtonBox::Ok)->setEnabled(issue.isEmpty());
	};
	auto loadTargets = [&](bool refreshCurrent) {
		const QString type = kind->currentData().toString();
		const bool sameKind = type == loadedKind;
		const QString selected = refreshCurrent && sameKind ? target->currentData().toString() :
			loadedKind.isEmpty() && type == priorKind ? prior.value("target").toString() : QString();
		if (!loadedKind.isEmpty() && !sameKind) {
			const QSignalBlocker pathBlocker(path), urlBlocker(url), loopBlocker(loop);
			path->clear(); url->clear(); loop->setChecked(false);
		}
		const QSignalBlocker targetBlocker(target);
		target->clear(); missingTarget.clear();
		const bool file = type == "image_source" || type == "ffmpeg_source", browser = type == "browser_source";
		if (type == "game_capture") target->addItem("Any fullscreen game", ""); else target->addItem("Choose…", "");
		QString property = type == "dshow_input" ? "video_device_id" : type == "monitor_capture" ? "monitor_id" : "window";
		obs_properties_t *properties = obs_get_source_properties(type.toUtf8().constData());
		obs_property_t *values = properties ? obs_properties_get(properties, property.toUtf8().constData()) : nullptr;
		if (!values && type == "monitor_capture" && properties) values = obs_properties_get(properties, "monitor");
		if (values && obs_property_get_type(values) == OBS_PROPERTY_LIST) for (size_t i = 0; i < obs_property_list_item_count(values); ++i) {
			if (obs_property_list_item_disabled(values, i)) continue;
			const QString id = obs_property_list_format(values) == OBS_COMBO_FORMAT_INT ? QString::number(obs_property_list_item_int(values, i)) : QString::fromUtf8(obs_property_list_item_string(values, i));
			target->addItem(QString::fromUtf8(obs_property_list_item_name(values, i)), id);
		}
		if (properties) obs_properties_destroy(properties);
		const int index = target->findData(selected);
		if (index >= 0) target->setCurrentIndex(index);
		else if (!selected.isEmpty() && !file && !browser) {
			missingTarget = selected;
			target->addItem("Previous device or window unavailable · Choose another…", selected);
			target->setCurrentIndex(target->count() - 1);
		}
		loadedKind = type;
		chooseTarget->setText(target->currentText()); changeType->setText(kind->currentText() + "  ·  Change…");
		chooseTarget->setVisible(!file && !browser); form->labelForField(chooseTarget)->setVisible(!file && !browser); refresh->setVisible(!file && !browser);
		pathRow->setVisible(file); form->labelForField(pathRow)->setVisible(file);
		url->setVisible(browser); form->labelForField(url)->setVisible(browser); loop->setVisible(type == "ffmpeg_source");
		browserAudio->setVisible(browser);
		hint->setText(kind->currentData(Qt::ToolTipRole).toString()); validate();
	};
	QObject::connect(changeType, &QPushButton::clicked, &dialog, [&] {
		PulseSetupChoices::choose(&dialog, kind, "What supplies this picture?", {}, "No supported input types are available.");
	});
	QObject::connect(kind, &QComboBox::currentIndexChanged, &dialog, [&](int) { loadTargets(false); });
	QObject::connect(chooseTarget, &QPushButton::clicked, &dialog, [&] {
		PulseSetupChoices::choose(&dialog, target, "Choose a device or window", {}, "No devices or windows were found. Go back and refresh after opening or connecting it.");
	});
	QObject::connect(target, &QComboBox::currentIndexChanged, &dialog, [&](int) { chooseTarget->setText(target->currentText()); validate(); });
	QObject::connect(refresh, &QPushButton::clicked, &dialog, [&] { loadTargets(true); });
	QObject::connect(browse, &QPushButton::clicked, &dialog, [&] {
		const QString file = QFileDialog::getOpenFileName(&dialog, "Choose the local file", path->text()); if (!file.isEmpty()) path->setText(file);
	});
	for (auto *field : {name, path, url}) QObject::connect(field, &QLineEdit::textChanged, &dialog, [&](const QString &) { validate(); });
	QObject::connect(loop, &QCheckBox::toggled, &dialog, [&](bool) { validate(); });
	QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] { validate(); if (error->text().isEmpty()) dialog.accept(); });
	QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	loadTargets(false); if (missingTarget.isEmpty()) error->hide();
	if (dialog.exec() != QDialog::Accepted) return {};
	return snapshot();
}
} // namespace PulseSetupInput
