#include "OBSBasic.hpp"

#include <QDialog>
#include <QDialogButtonBox>
#include <QGridLayout>
#include <QGuiApplication>
#include <QLabel>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QScreen>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

namespace {
QScreen *savedScreen()
{
	auto *config = App()->GetUserConfig();
	const QString serial = QString::fromUtf8(config_get_string(config, "PulseHud", "ScreenSerial"));
	const QString name = QString::fromUtf8(config_get_string(config, "PulseHud", "ScreenName"));
	QScreen *found = nullptr;
	for (QScreen *screen : QGuiApplication::screens())
		if ((!serial.isEmpty() && screen->serialNumber() == serial) ||
		    (serial.isEmpty() && !name.isEmpty() && screen->name() == name)) {
			if (found) return nullptr;
			found = screen;
		}
	return found;
}
}

void OBSBasic::UpdatePulseHudControls()
{
	const bool running = pulseHudOutput && pulseHudOutput->Running();
	if (pulseHudToggle) {
		const qreal ratio = devicePixelRatioF();
		QPixmap icon(qRound(24 * ratio), qRound(24 * ratio));
		icon.setDevicePixelRatio(ratio);
		icon.fill(Qt::transparent);
		QPainter painter(&icon);
		painter.setRenderHint(QPainter::Antialiasing);
		const auto draw = [&painter] {
			painter.drawRoundedRect(QRectF(2.5, 3.5, 19, 13), 2, 2);
			painter.drawLine(QPointF(12, 16.5), QPointF(12, 20.5));
			painter.drawLine(QPointF(7.5, 20.5), QPointF(16.5, 20.5));
		};
		if (running) { painter.setPen(QPen(QColor(89, 230, 255, 45), 5)); draw(); }
		painter.setPen(QPen(running ? QColor("#59e6ff") : QColor("#8695a8"), 1.7));
		draw(); painter.end();
		pulseHudToggle->setIcon(QIcon(icon));
		pulseHudToggle->setIconSize(QSize(24, 24));
		QSignalBlocker blocker(pulseHudToggle);
		pulseHudToggle->setChecked(running);
		pulseHudToggle->setToolTip(running ? "HUD on · click to hide transparent effects" :
			"HUD off · click to show transparent effects on your chosen display");
		pulseHudToggle->setAccessibleName(running ? "HUD on: hide effects" : "HUD off: show effects");
	}
	if (pulseHudScreenChoice) {
		const auto screen = PulseHudOutput::Describe(pulseHudScreen);
		const QString model = pulseHudScreen ? (pulseHudScreen->model().isEmpty() ? pulseHudScreen->name() : pulseHudScreen->model()) : QString();
		pulseHudScreenChoice->setText(screen.valid() ? pulseHudScreenChoice->fontMetrics().elidedText("Screen: " + model, Qt::ElideRight, 140) : "Choose screen…");
		pulseHudScreenChoice->setToolTip(screen.valid() ? QString("HUD display · %1 × %2\nClick to choose another display")
			.arg(screen.pixels.width()).arg(screen.pixels.height()) : "Choose the display that sets the HUD canvas size");
	}
}

void OBSBasic::WatchPulseHudScreen(QScreen *screen)
{
	if (pulseHudScreen == screen) return;
	disconnect(pulseHudGeometryConnection);
	disconnect(pulseHudRemovedConnection);
	pulseHudScreen = screen;
	if (!screen) { UpdatePulseHudControls(); return; }
	pulseHudGeometryConnection = connect(screen, &QScreen::geometryChanged, this, [this, watched = QPointer<QScreen>(screen)] {
		QTimer::singleShot(150, this, [this, watched] {
			if (!watched || pulseHudScreen != watched) return;
			const bool active = pulseHudOutput && pulseHudOutput->Active();
			StopPulseHud();
			if (EnsurePulseHudCanvas() && active) TogglePulseHud();
			RefreshPulseCameraPreview();
			UpdatePulseHudControls();
		});
	});
	pulseHudRemovedConnection = connect(qGuiApp, &QGuiApplication::screenRemoved, this, [this, screen](QScreen *removed) {
		if (removed != screen) return;
		StopPulseHud();
		pulseHudScreen = nullptr;
		UpdatePulseHudControls();
	});
	UpdatePulseHudControls();
}

bool OBSBasic::ChoosePulseHudScreen()
{
	QDialog dialog(this);
	dialog.setWindowTitle("HUD display");
	auto *layout = new QVBoxLayout(&dialog);
	auto *heading = new QLabel("Choose where your HUD effects appear");
	heading->setObjectName("PulseWeaverCardTitle");
	layout->addWidget(heading);
	auto *help = new QLabel("The canvas matches this display. Your desktop stays visible and clicks pass through the effects.");
	help->setWordWrap(true);
	layout->addWidget(help);
	auto *tiles = new QGridLayout;
	layout->addLayout(tiles);
	QPointer<QScreen> chosen;
	int index = 0;
	for (QScreen *screen : QGuiApplication::screens()) {
		const auto description = PulseHudOutput::Describe(screen);
		if (!description.valid()) continue;
		const QString model = screen->model().isEmpty() ? screen->name() : screen->model();
		auto *tile = new QPushButton(QString("%1%2\n%3 × %4 · %5")
			.arg(model).arg(screen == QGuiApplication::primaryScreen() ? " · Main" : "").arg(description.pixels.width()).arg(description.pixels.height())
			.arg(description.pixels.width() >= description.pixels.height() ? "Landscape" : "Portrait"));
		tile->setObjectName("PulseWeaverHudScreenCard");
		/* Toolbar controls have a compact themed height. These two-line cards
		 * need their own metrics so theme polish cannot squash the labels. */
		tile->setStyleSheet("QPushButton#PulseWeaverHudScreenCard { height: -1px; min-height: 72px; max-height: 16777215px; padding: 10px 14px; }");
		tile->setMinimumSize(240, 96);
		tile->setAutoDefault(false);
		tile->setCursor(Qt::PointingHandCursor);
		tile->setToolTip(tile->text());
		connect(tile, &QPushButton::clicked, &dialog, [&dialog, &chosen, screen = QPointer<QScreen>(screen)] {
			if (!screen) return;
			chosen = screen; dialog.accept();
		});
		connect(screen, &QObject::destroyed, tile, [tile] { tile->setEnabled(false); tile->setText("Display disconnected"); });
		tiles->addWidget(tile, index / 3, index % 3);
		++index;
	}
	if (!index) layout->addWidget(new QLabel("No supported Windows displays are available."));
	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel);
	connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	layout->addWidget(buttons);
	if (dialog.exec() != QDialog::Accepted || !chosen) return false;
	const bool active = pulseHudOutput && pulseHudOutput->Active();
	StopPulseHud();
	WatchPulseHudScreen(chosen);
	auto *config = App()->GetUserConfig();
	config_set_string(config, "PulseHud", "ScreenName", chosen->name().toUtf8().constData());
	config_set_string(config, "PulseHud", "ScreenSerial", chosen->serialNumber().toUtf8().constData());
	config_save_safe(config, "tmp", nullptr);
	const bool ready = EnsurePulseHudCanvas();
	if (ready && active) TogglePulseHud();
	if (ready && pulseHudEditing) RefreshPulseCameraPreview();
	UpdatePulseHudControls();
	return ready;
}

bool OBSBasic::EnsurePulseHudCanvas()
{
	if (!pulseHudScreen) WatchPulseHudScreen(savedScreen());
	if (!pulseHudScreen) return false;
	const auto screen = PulseHudOutput::Describe(pulseHudScreen);
	if (!screen.valid()) return false;
	OBSCanvasAutoRelease canvas = obs_get_canvas_by_name("Pulse Weaver HUD");
	if (!canvas) {
		/* A HUD is an editor/source namespace, not another video output mix.
		 * Its custom-sized scenes can resize while broadcasting without resetting
		 * video, and off-state browsers gain no permanent programme/showing ref. */
		AddCanvas("Pulse Weaver HUD", nullptr, SCENE_REF);
		canvas = obs_get_canvas_by_name("Pulse Weaver HUD");
		if (!canvas) { QMessageBox::warning(this, "HUD", "The HUD canvas could not be created."); return false; }
	}
	/* One fixed effects composition, never a stage, Look or scene picker. */
	OBSSourceAutoRelease source = obs_canvas_get_source_by_name(canvas, "HUD");
	if (!source) {
		OBSSceneAutoRelease scene = obs_canvas_scene_create(canvas, "HUD");
		if (!scene) return false;
		source = obs_source_get_ref(obs_scene_get_source(scene));
		AddScene(source.Get());
	}
	pulseHudPreview.Select(obs_scene_from_source(source));
	obs_scene_set_custom_size(obs_scene_from_source(source), uint32_t(screen.pixels.width()), uint32_t(screen.pixels.height()));
	SaveProject();
	return true;
}

void OBSBasic::SetPulseHudEditing()
{
	if (!EnsurePulseHudCanvas() && !ChoosePulseHudScreen()) {
		for (auto *button : pulseCameraPage->findChildren<QPushButton *>()) {
			QSignalBlocker blocker(button);
			if (button->property("pulseCanvasHud").toBool()) button->setChecked(pulseHudEditing);
			if (button->property("pulseCanvasPortrait").isValid())
				button->setChecked(!pulseHudEditing && button->property("pulseCanvasPortrait").toBool() == pulsePortraitEditing);
		}
		return;
	}
	CloseDialogs();
	pulsePortraitPreview.SetShowing(false);
	pulsePortraitEditing = false;
	pulseHudEditing = true;
	for (auto *button : pulseCameraPage->findChildren<QPushButton *>()) {
		QSignalBlocker blocker(button);
		if (button->property("pulseCanvasHud").toBool()) button->setChecked(true);
		if (button->property("pulseCanvasPortrait").isValid()) button->setChecked(false);
	}
	if (pulseHudScreenChoice) pulseHudScreenChoice->show();
	RefreshPulseCameraScenes();
	SelectPulsePortraitScene(obs_scene_get_source(pulseHudPreview.Get()));
	setPreviewScalingWindow();
	if (pulsePages && pulsePages->currentIndex() == 2) SetPulseWeaverWorkspace(2);
	UpdatePulseHudControls();
}

void OBSBasic::StopPulseHud()
{
	if (pulseHudOutput) pulseHudOutput->Stop();
	UpdatePulseHudControls();
}

void OBSBasic::TogglePulseHud()
{
	if (pulseHudOutput && pulseHudOutput->Active()) { StopPulseHud(); return; }
	UpdatePulseHudControls(); // A click is a request; highlight follows the first presented frame.
	if (!EnsurePulseHudCanvas() && !ChoosePulseHudScreen()) return;
	if (!pulseHudOutput) {
		pulseHudOutput = new PulseHudOutput(this);
		pulseHudOutput->stateChanged = [this](bool, const QString &error) {
			UpdatePulseHudControls();
			if (!error.isEmpty()) QMessageBox::warning(this, "HUD", error);
		};
	}
	const QString error = pulseHudOutput->Start(obs_scene_get_source(pulseHudPreview.Get()), PulseHudOutput::Describe(pulseHudScreen));
	if (!error.isEmpty()) QMessageBox::warning(this, "HUD", error);
	UpdatePulseHudControls();
}
