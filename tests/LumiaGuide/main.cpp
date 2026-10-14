#include "../../engine/obs-studio/shared/qt/PulseInstructions.hpp"
#include <QApplication>
#include <QDir>
#include <QEventLoop>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPushButton>
#include <QScrollBar>
#include <QTabBar>
#include <QTextCursor>
#include <QTimer>
#include <QWizard>
#include <iostream>

static int failures = 0, checks = 0;
namespace Ui {
static void qWait(int milliseconds) {
	QEventLoop loop;
	QTimer::singleShot(milliseconds, &loop, &QEventLoop::quit);
	loop.exec();
}
static void keyClick(QWidget *widget, Qt::Key key) {
	QKeyEvent down(QEvent::KeyPress, key, Qt::NoModifier), up(QEvent::KeyRelease, key, Qt::NoModifier);
	QApplication::sendEvent(widget, &down); QApplication::sendEvent(widget, &up);
}
static void mouseClick(QWidget *widget, Qt::MouseButton button, Qt::KeyboardModifiers modifiers = Qt::NoModifier,
                       QPoint point = QPoint(-1, -1)) {
	if (point == QPoint(-1, -1)) point = widget->rect().center();
	QMouseEvent down(QEvent::MouseButtonPress, QPointF(point), QPointF(widget->mapToGlobal(point)), button, button, modifiers);
	QMouseEvent up(QEvent::MouseButtonRelease, QPointF(point), QPointF(widget->mapToGlobal(point)), button, Qt::NoButton, modifiers);
	QApplication::sendEvent(widget, &down); QApplication::sendEvent(widget, &up);
}
}
static void check(bool ok, const char *message)
{
	++checks;
	if (!ok) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
}

int main(int argc, char **argv)
{
	QApplication app(argc, argv);
	const QString output = argc > 1 ? QString::fromLocal8Bit(argv[1]) : "screenshots";
	QDir().mkpath(output);
	QStringList firstContents;
	// Exercise the same modal helper with both caller types without an OBS/Lumia session.
	QWidget showControl;
	QWizard wizard;
	for (QWidget *parent : {&showControl, static_cast<QWidget *>(&wizard)}) {
		QTimer::singleShot(0, [&] {
			auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
			check(dialog != nullptr, "shared guide opens modally for each caller");
			if (!dialog) return;
			auto *topics = dialog->findChild<QTabWidget *>("PulseWeaverInstructionTopics");
			check(topics && topics->count() == 5, "all five guide topics remain available");
			if (!topics) { dialog->reject(); return; }
			QStringList contents;
			for (int index = 0; index < topics->count(); ++index) {
				auto *browser = qobject_cast<QTextBrowser *>(topics->widget(index));
				check(browser && browser->openExternalLinks(), "topic retains external link support");
				if (browser) contents << browser->toPlainText();
			}
			if (firstContents.isEmpty()) firstContents = contents;
			else check(contents == firstContents, "wizard and Show Control parents show identical guide content");
			if (parent == &wizard) { dialog->reject(); return; }
			check(topics->tabText(3) == "Game challenges", "challenge topic is no longer Fortnite-only");
			for (const QSize size : {QSize(900, 700), QSize(760, 560), QSize(640, 480)}) {
				dialog->resize(size);
				Ui::qWait(40);
				for (int index = 0; index < topics->count(); ++index) {
					topics->setCurrentIndex(index);
					auto *browser = qobject_cast<QTextBrowser *>(topics->widget(index));
					browser->verticalScrollBar()->setValue(0);
					Ui::qWait(30);
					check(browser->viewport()->width() > 400 && browser->viewport()->height() > 200,
					      "topic has usable viewport at supported dialog sizes");
					if (index == 3) {
						check(browser->horizontalScrollBar()->maximum() == 0, "game instructions wrap without horizontal scrolling");
						browser->setFocus();
						Ui::keyClick(browser, Qt::Key_PageDown);
						check(browser->verticalScrollBar()->value() > 0, "keyboard scroll reaches later challenge instructions");
						browser->verticalScrollBar()->setValue(browser->verticalScrollBar()->maximum());
						Ui::qWait(20);
						check(browser->verticalScrollBar()->value() == browser->verticalScrollBar()->maximum(), "legacy migration note is reachable");
						check(dialog->grab().save(output + QString("/game-%1x%2-bottom.png").arg(size.width()).arg(size.height())), "save migration-note screenshot");
						browser->verticalScrollBar()->setValue(0);
					}
					check(dialog->grab().save(output + QString("/topic-%1-%2x%3.png").arg(index).arg(size.width()).arg(size.height())), "save topic screenshot");
				}
			}
			// Click a real document anchor, intercepting it so the check never opens a network page.
			topics->setCurrentIndex(0);
			auto *browser = qobject_cast<QTextBrowser *>(topics->widget(0));
			browser->setOpenExternalLinks(false);
			browser->setOpenLinks(false);
			QUrl clicked;
			QObject::connect(browser, &QTextBrowser::anchorClicked, browser, [&](const QUrl &url) { clicked = url; });
			QTextCursor cursor = browser->document()->find("Install Lumia Stream");
			check(!cursor.isNull(), "original Lumia download anchor is present");
			cursor.setPosition(cursor.selectionStart() + 2);
			browser->setTextCursor(cursor);
			browser->ensureCursorVisible();
			Ui::qWait(20);
			Ui::mouseClick(browser->viewport(), Qt::LeftButton, Qt::NoModifier, browser->cursorRect(cursor).center());
			check(clicked == QUrl("https://www.lumiastream.com/downloads"), "existing guide link is clickable and keeps its URL");
			browser->setOpenExternalLinks(true);
			auto *buttons = dialog->findChild<QDialogButtonBox *>();
			check(buttons != nullptr, "modal close control remains available");
			if (buttons) Ui::mouseClick(buttons->button(QDialogButtonBox::Close), Qt::LeftButton);
			else dialog->reject();
		});
		PulseInstructions::openDialog(parent);
	}
	std::cout << checks << " guide checks; " << failures << " failures\n";
	return failures ? 1 : 0;
}
