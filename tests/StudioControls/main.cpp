#include "../../engine/obs-studio/shared/qt/PulseStudioControls.hpp"
#include <QApplication>
#include <QDir>
#include <QKeyEvent>
#include <QPalette>
#include <cstdio>
#include <stdexcept>

static int checks = 0;
static void check(bool ok, const char *description) { ++checks; if (!ok) { std::fprintf(stderr, "FAIL %d: %s\n", checks, description); std::fflush(stderr); std::exit(1); } }
static QToolButton *choice(QWidget *panel, const QString &mode) {
	for (auto *button : panel->findChildren<QToolButton *>()) if (button->property("routeMode") == mode) return button;
	return nullptr;
}
int main(int argc, char **argv)
{
	QApplication app(argc, argv);
	const QString output = argc > 1 ? QString::fromLocal8Bit(argv[1]) : "screenshots";
	QDir().mkpath(output);
	QPalette dark;
	dark.setColor(QPalette::Window, QColor("#101820")); dark.setColor(QPalette::WindowText, QColor("#e2eaf3"));
	dark.setColor(QPalette::Base, QColor("#172330")); dark.setColor(QPalette::AlternateBase, QColor("#253b50"));
	dark.setColor(QPalette::Text, QColor("#e2eaf3")); dark.setColor(QPalette::Mid, QColor("#34506a"));
	dark.setColor(QPalette::Highlight, QColor("#087ab0")); dark.setColor(QPalette::HighlightedText, Qt::white);
	app.setPalette(dark);
	QWidget host; host.setAttribute(Qt::WA_DontShowOnScreen);
	auto *layout = new QVBoxLayout(&host); layout->setContentsMargins(0, 0, 0, 0);
	auto *previews = new PulseStudioControls::PreviewStrip(true, 15); layout->addWidget(previews);
	auto *routes = new QWidget; auto *row = new QHBoxLayout(routes); row->setContentsMargins(12, 6, 12, 6);
	QComboBox twitch, youtube, kick;
	for (auto *controller : {&twitch, &youtube, &kick}) { controller->addItem("Off", "off"); controller->addItem("Landscape", "horizontal"); }
	twitch.addItem("Both", "dual"); youtube.addItem("Portrait", "vertical"); youtube.addItem("Both", "dual");
	twitch.setCurrentIndex(1); youtube.setCurrentIndex(3);
	bool live = false; int accepted = twitch.currentIndex(), changes = 0;
	QObject::connect(&twitch, &QComboBox::currentIndexChanged, &host, [&](int index) {
		if(live && index != 0) {QSignalBlocker blocker(&twitch); twitch.setCurrentIndex(accepted); return;}
		accepted = index; ++changes;
	});
	auto *tw = new PulseStudioControls::RouteChoices("Twitch", &twitch);
	auto *yt = new PulseStudioControls::RouteChoices("YouTube", &youtube);
	auto *ki = new PulseStudioControls::RouteChoices("Kick", &kick);
	row->addWidget(tw, 1); row->addWidget(yt, 1); row->addWidget(ki, 1); layout->addWidget(routes);
	check(changes == 0 && choice(tw, "horizontal")->isChecked(), "construction preserves saved routes without triggering a change");
	choice(tw, "dual")->click(); check(twitch.currentData() == "dual" && changes == 1, "one click selects exactly one route");
	choice(tw, "dual")->click(); check(changes == 1, "clicking the selected route does not reapply an output");
	live = true; choice(tw, "horizontal")->click();
	check(twitch.currentData() == "dual" && choice(tw, "dual")->isChecked() && !choice(tw, "horizontal")->isChecked(), "rejected live change restores the accepted selection");
	choice(tw, "off")->click(); check(twitch.currentData() == "off" && choice(tw, "off")->isChecked(), "live Off retains existing controller behavior");
	live = false; twitch.setCurrentIndex(1); check(choice(tw, "horizontal")->isChecked(), "external mode changes update the visible buttons");
	check(choice(ki, "dual") == nullptr && choice(ki, "vertical") == nullptr, "Kick offers only supported formats");
	check(previews->live->isChecked() && previews->value->text() == "15 FPS", "preview settings load independently");
	for(int position = 0; position < 3; ++position) {
		previews->rate->setValue(position);
		check(previews->value->text() == QString::number(PulseStudioControls::PreviewStrip::fps(position)) + " FPS", "slider displays the actual discrete refresh rate");
	}
	previews->rate->setValue(0);
	QKeyEvent right(QEvent::KeyPress, Qt::Key_Right, Qt::NoModifier); QApplication::sendEvent(previews->rate, &right);
	check(previews->value->text() == "30 FPS", "keyboard arrows select the next real refresh rate");
	previews->live->click(); check(!previews->live->isChecked() && previews->rate->value() == 1, "preview enable and refresh controls remain independent");
	previews->live->setChecked(true); previews->rate->setValue(0);
	QComboBox recording;
	for(const QString &mode : {QString("standard"), QString("off"), QString("horizontal"), QString("vertical"), QString("dual")}) recording.addItem(mode, mode);
	recording.setCurrentIndex(0);
	PulseStudioControls::RouteChoices recordingModes("Recording", &recording, nullptr, false);
	recordingModes.setAttribute(Qt::WA_DontShowOnScreen); recordingModes.setFixedSize(240, 32); recordingModes.show();
	check(choice(&recordingModes, "standard")->isChecked(), "compact recording controls preserve Alpha's saved Standard mode");
	check(choice(&recordingModes, "standard")->text() == "Default", "Standard recording is labelled Default in the joined control");
	check(choice(&recordingModes, "standard")->toolTip().contains("recording settings"), "Default explains that it uses the recording settings");
	check(choice(&recordingModes, "standard") != choice(&recordingModes, "horizontal"), "Default and 16:9 retain separate recording paths");
	const QSize recordingFootprint = recordingModes.size();
	recording.setEnabled(false);
	check(!recordingModes.isEnabled() && !choice(&recordingModes, "horizontal")->isEnabled(), "recording lock disables the entire visible control");
	choice(&recordingModes, "horizontal")->click(); check(recording.currentData() == "standard", "disabled recording controls cannot change Default recording format");
	recording.setEnabled(true); choice(&recordingModes, "horizontal")->click();
	check(recording.currentData() == "horizontal", "recording format becomes selectable again when stopped");
	for(const QString &mode : {QString("standard"), QString("off"), QString("horizontal"), QString("vertical"), QString("dual")}) {
		choice(&recordingModes, mode)->click(); app.processEvents();
		check(recording.currentData() == mode && recordingModes.size() == recordingFootprint, "recording mode changes preserve the fixed footprint");
	}
	recording.setCurrentIndex(recording.findData("standard"));
	check(choice(&recordingModes, "standard")->isChecked() && !choice(&recordingModes, "horizontal")->isChecked(), "external Standard selection restores only Default");
	check(recordingModes.grab().save(output + "/recording-240x32.png"), "save compact recording control rendering");
	for (int font : {10, 12, 14}) {
		app.setFont(QFont("Segoe UI", font));
		host.setFont(QFont("Segoe UI", font));
		for(auto *widget : host.findChildren<QWidget *>()) widget->setFont(QFont("Segoe UI", font));
		recordingModes.setFont(QFont("Segoe UI", font));
		for(auto *widget : recordingModes.findChildren<QWidget *>()) widget->setFont(QFont("Segoe UI", font));
		app.processEvents();
		check(recordingModes.size() == QSize(240,32), "larger text does not enlarge the recording slot");
		for(auto *button : recordingModes.findChildren<QToolButton *>()) {
			check(recordingModes.rect().contains(QRect(button->mapTo(&recordingModes, QPoint()),button->size())), "recording segments stay within the constrained slot");
			check(button->width() >= button->fontMetrics().horizontalAdvance(button->text())+10 && button->height() >= button->fontMetrics().height()+2, "recording labels fit without clipping at larger font sizes");
		}
		check(recordingModes.grab().save(output + QString("/recording-240x32-%1pt.png").arg(font)), "save larger recording label rendering");
		for(int width : {640, 760, 1000}) {
			host.resize(width, 180); host.show(); app.processEvents();
			check(host.width() == width, "controls fit narrow panes without forcing horizontal growth");
			const auto children = previews->findChildren<QWidget *>(QString(), Qt::FindDirectChildrenOnly);
			for(auto *child : children) check(previews->rect().contains(child->geometry()), "preview controls stay inside their own strip");
			for(auto *panel : {tw, yt, ki}) for(auto *button : panel->findChildren<QToolButton *>()) {
				check(panel->rect().contains(QRect(button->mapTo(panel, QPoint()), button->size())), "output buttons stay inside their provider group");
				check(button->width() >= button->fontMetrics().horizontalAdvance(button->text()) + 10, "output labels have room at larger font sizes");
			}
			check(host.grab().save(output + QString("/controls-%1px-%2pt.png").arg(width).arg(font)), "save actual production widget rendering");
		}
	}
	std::printf("PASS: %d control checks; saved routes, live-change rejection, keyboard refresh and nine layout sizes.\n", checks);
}
