#include "../../engine/obs-studio/shared/qt/PulseInteractionFeedback.hpp"
#include <QEventLoop>
#include <QTimer>
#include <iostream>
#include <stdexcept>

static void check(bool ok, const char *message) { if (!ok) throw std::runtime_error(message); }
static void wait(int ms) { QEventLoop loop; QTimer::singleShot(ms, &loop, &QEventLoop::quit); loop.exec(); }
static void click(QWidget *widget) {
    const QPointF local(15, 15), global(widget->mapToGlobal(QPoint(15, 15)));
    QMouseEvent down(QEvent::MouseButtonPress, local, global, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QMouseEvent up(QEvent::MouseButtonRelease, local, global, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(widget, &down); QApplication::sendEvent(widget, &up);
}
static void space(QWidget *widget) {
    QKeyEvent down(QEvent::KeyPress, Qt::Key_Space, Qt::NoModifier);
    QKeyEvent up(QEvent::KeyRelease, Qt::Key_Space, Qt::NoModifier);
    QApplication::sendEvent(widget, &down); QApplication::sendEvent(widget, &up);
}
int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    app.setProperty("pulseWeaverInteractionFeedback", true);
    PulseVisual::InteractionFeedback feedback(&app);
    QWidget host;
    host.setAttribute(Qt::WA_DontShowOnScreen);
    host.resize(400, 200);
    QPushButton button("Test", &host);
    button.setGeometry(10, 10, 140, 40);
    host.show();
    app.processEvents();
    int clicks = 0;
    QObject::connect(&button, &QPushButton::clicked, [&] { ++clicks; });
    auto overlays = [&] { return host.findChildren<QWidget *>("PulseWeaverInteractionSweep").size(); };
    click(&button);
    check(clicks == 1 && overlays() == 1, "Feedback must preserve exactly one click");
    for (int i = 0; i < 12; ++i) click(&button);
    check(clicks == 13 && overlays() == 1, "Rapid input accumulated sweeps or lost clicks");
    wait(300);
    check(overlays() == 0, "Animation retained idle work after completion");
    space(&button);
    check(clicks == 14 && overlays() == 1, "Keyboard activation lost native behaviour");
    button.hide();
    wait(20);
    check(overlays() == 0, "Hidden control retained its animation");
    button.show();
    app.setProperty("pulseWeaverInteractionFeedback", false);
    click(&button);
    check(clicks == 15 && overlays() == 0, "Feedback off changed clicks or animated");
    app.setProperty("pulseWeaverInteractionFeedback", true);
    button.setEnabled(false);
    click(&button);
    check(clicks == 15 && overlays() == 0, "Disabled control reacted");
    std::cout << "PASS: mouse/keyboard, rapid clicks, cleanup, hidden, disabled and feedback-off\n";
}
