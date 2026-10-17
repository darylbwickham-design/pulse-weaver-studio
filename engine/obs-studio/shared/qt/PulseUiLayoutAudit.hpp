#pragma once
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLayout>
#include <QKeyEvent>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSlider>
#include <QStackedWidget>
#include <QTabWidget>
#include <QTimer>
#include <QWidget>
#include <memory>

// Explicit, one-shot development audit. The caller only enables this for a
// separate inert portable fixture; normal launches allocate no audit timer.
namespace PulseUiLayoutAudit {
inline QJsonArray rectValue(const QRect &rect)
{
	return {rect.x(), rect.y(), rect.width(), rect.height()};
}
inline QJsonArray marginValue(const QMargins &margins)
{
	return {margins.left(), margins.top(), margins.right(), margins.bottom()};
}
inline QJsonObject widgetGeometry(QWidget *widget, QWidget *window)
{
	if(!widget) return {};
	const auto hint = widget->sizeHint();
	const auto minimum = widget->minimumSizeHint();
	QJsonObject result{{"object", widget->objectName()}, {"class", widget->metaObject()->className()},
		{"rect", rectValue(widget->rect())}, {"contentsRect", rectValue(widget->contentsRect())},
		{"inWindow", rectValue(QRect(widget->mapTo(window, QPoint()), widget->size()))},
		{"widgetMargins", marginValue(widget->contentsMargins())},
		{"sizeHint", QJsonArray{hint.width(), hint.height()}},
		{"minimumSizeHint", QJsonArray{minimum.width(), minimum.height()}},
		{"visible", widget->isVisibleTo(window)}};
	if(auto *layout = widget->layout()) {
		result.insert("layoutMargins", marginValue(layout->contentsMargins()));
		result.insert("layoutGeometry", rectValue(layout->geometry()));
		result.insert("layoutSizeConstraint", int(layout->sizeConstraint()));
	}
	return result;
}
inline QJsonObject automationGeometry(QWidget *window, QWidget *page)
{
	auto *navigator = page->findChild<QStackedWidget *>("PulseWeaverCommandNavigator");
	QJsonObject report{{"navigator", widgetGeometry(navigator, window)}};
	if(navigator) report.insert("currentPage", widgetGeometry(navigator->currentWidget(), window));
	QJsonArray categories;
	bool allContained = true;
	const auto cards = page->findChildren<QPushButton *>("PulseWeaverCommandCategory");
	for(auto *card : cards) {
		QJsonObject entry = widgetGeometry(card, window);
		entry.insert("category", card->text().section('\n', 0, 0));
		const QRect bounds(card->mapTo(window, QPoint()), card->size());
		QRect intersection = bounds;
		bool contentsContain = true;
		QJsonArray ancestors;
		for(auto *ancestor = card->parentWidget(); ancestor; ancestor = ancestor->parentWidget()) {
			const QPoint origin = ancestor->mapTo(window, QPoint());
			const QRect ancestorRect(origin, ancestor->size());
			const QRect ancestorContents = ancestor->contentsRect().translated(origin);
			intersection = intersection.intersected(ancestorRect);
			contentsContain = contentsContain && ancestorContents.contains(bounds);
			auto details = widgetGeometry(ancestor, window);
			details.insert("containsCard", ancestorRect.contains(bounds));
			details.insert("contentsContainCard", ancestorContents.contains(bounds));
			ancestors.append(details);
			if(ancestor == window) break;
		}
		entry.insert("ancestorIntersection", rectValue(intersection));
		entry.insert("ancestors", ancestors);
		entry.insert("fullyContained", intersection == bounds && contentsContain && card->isVisibleTo(window));
		allContained = allContained && intersection == bounds && contentsContain && card->isVisibleTo(window);
		categories.append(entry);
	}
	report.insert("categories", categories);
	report.insert("automationCategoryCount", cards.size());
	report.insert("automationCardsContained", cards.size() == 4 && allContained);
	return report;
}
inline QJsonObject commands(QWidget *window, const QString &directory)
{
	auto *button = window->findChild<QPushButton *>("PulseWeaverCommandButton");
	if(!button) return {{"paletteAvailable", false}};
	auto result = std::make_shared<QJsonObject>();
	QTimer::singleShot(150, window, [window, directory, result] {
		auto *dialog = window->findChild<QDialog *>("PulseWeaverCommandPalette");
		result->insert("paletteAvailable", dialog != nullptr);
		if(!dialog) return;
		dialog->grab().save(directory + "/commands-palette.png");
		if(auto *viewport = dialog->findChild<QScrollArea *>()) {
			bool reachable = true;
			for(auto *card : dialog->findChildren<QPushButton *>("PulseWeaverChoiceCard"))
				reachable = reachable && card->width() <= viewport->viewport()->width();
			reachable = reachable && (viewport->widget()->height() <= viewport->viewport()->height() || viewport->verticalScrollBar()->maximum() > 0);
			result->insert("commandsCardsReachable", reachable);
		}
		for(auto *card : dialog->findChildren<QPushButton *>("PulseWeaverChoiceCard")) {
			if(card->text() == "Workspaces") {card->click(); break;}
		}
		// Adding cards to a visible layout queues their first show/layout pass.
		// Capture the completed native state, as an operator sees it next frame.
		QApplication::processEvents();
		const auto categoryCards = dialog->findChildren<QPushButton *>("PulseWeaverChoiceCard");
		result->insert("commandsCategoryCards", !categoryCards.isEmpty() && categoryCards.size() <= 6);
		dialog->grab().save(directory + "/command-workspace-cards.png");
		for(auto *back : dialog->findChildren<QPushButton *>()) {
			if(back->text().contains("All categories")) {back->click(); break;}
		}
		QApplication::processEvents();
		result->insert("commandsBackToCategories", dialog->findChildren<QPushButton *>("PulseWeaverChoiceCard").size() == 5);
		if(auto *search = dialog->findChild<QLineEdit *>("PulseWeaverCommandSearch")) {
			search->setText("Go live");
			QApplication::processEvents();
			QMetaObject::invokeMethod(search, "returnPressed", Qt::DirectConnection);
			result->insert("paletteEnterGuarded", dialog->isVisible());
			dialog->grab().save(directory + "/commands-guarded-transport.png");
		}
		QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
		QApplication::sendEvent(dialog, &escape);
		result->insert("paletteEscapeClosed", !dialog->isVisible());
	});
	button->click();
	if(auto *stage = window->findChild<QPushButton *>("PulseWeaverStageChoice")) {
		QTimer::singleShot(150, window, [window, directory, result] {
			auto *picker = window->findChild<QDialog *>("PulseWeaverStagePicker");
			result->insert("stageCardsAvailable", picker != nullptr);
			if(picker) {picker->grab().save(directory + "/stage-cards.png"); picker->reject();}
		});
		stage->click();
	}
	QPushButton *showButton = nullptr;
	for(auto *nav : window->findChildren<QPushButton *>()) {
		if(nav->property("pulseWorkspaceIndex").isValid() && nav->property("pulseWorkspaceIndex").toInt() == 0) showButton = nav;
		if(nav->property("pulseWorkspaceIndex").isValid() && nav->property("pulseWorkspaceIndex").toInt() == 3) nav->click();
	}
	auto *tabs = window->findChild<QTabWidget *>("PulseWeaverPlatformTabs");
	auto *page = window->findChild<QWidget *>("PulseWeaverBasicCommandsPage");
	result->insert("automationCommandsAvailable", tabs && page);
	if(tabs && page) {
		tabs->setCurrentWidget(page); QApplication::processEvents();
		if(page->layout()) page->layout()->activate();
		if(auto *navigator = page->findChild<QStackedWidget *>("PulseWeaverCommandNavigator")) {
			if(navigator->layout()) navigator->layout()->activate();
			if(navigator->currentWidget() && navigator->currentWidget()->layout()) navigator->currentWidget()->layout()->activate();
		}
		QApplication::processEvents();
		const auto geometry = automationGeometry(window, page);
		result->insert("automationGeometry", geometry);
		result->insert("automationCardsContained", geometry.value("automationCardsContained"));
		window->grab().save(directory + "/commands-integration.png");
		for(auto *category : page->findChildren<QPushButton *>("PulseWeaverCommandCategory")) {
			if(category->text().startsWith("Audio")) {category->click(); break;}
		}
		for(auto *choice : page->findChildren<QPushButton *>("PulseWeaverCommandChoice")) {
			if(choice->property("pulseCommand").toString() == "audio.volume") {choice->click(); break;}
		}
		QApplication::processEvents(); window->grab().save(directory + "/commands-example.png");
	}
	for(auto *nav : window->findChildren<QPushButton *>()) {
		if(nav->property("pulseWorkspaceIndex").isValid() && nav->property("pulseWorkspaceIndex").toInt() == 4) {nav->click(); break;}
	}
	if(auto *sections = window->findChild<QStackedWidget *>("PulseWeaverSetupSections")) {
		sections->setCurrentIndex(0); QApplication::processEvents();
		window->grab().save(directory + "/setup-categories.png");
		int categoryCount = 0;
		QPushButton *sound = nullptr;
		for(auto *card : sections->findChildren<QPushButton *>("PulseWeaverChoiceCard")) {
			if(!card->property("pulseSetupCategory").isValid()) continue;
			++categoryCount;
			if(card->property("pulseSetupCategory").toString() == "Sound and recording") sound = card;
		}
		result->insert("setupCategoryGrid", categoryCount == 6);
		if(sound) {sound->click(); QApplication::processEvents(); window->grab().save(directory + "/setup-sound-detail.png");}
		if(auto *back = window->findChild<QPushButton *>("PulseWeaverSetupBack")) back->click();
		result->insert("setupBackToCategories", sections->currentIndex() == 0);
	}
	if(showButton) showButton->click();
	return *result;
}
inline void run(QWidget *window, QWidget *landscape, QWidget *portrait, QWidget *chat,
                QCheckBox *toggle, QSlider *rate, QLabel *status, const QString &directory)
{
	if(!landscape || !portrait || !chat || !toggle || !rate) return;
	QDir().mkpath(directory);
	// The resolved application QSS is static style data. This opt-in fixture
	// artifact lets focused Qt checks reproduce the actual native theme.
	QFile theme(directory + "/active-theme.qss");
	if(theme.open(QIODevice::WriteOnly)) theme.write(qApp->styleSheet().toUtf8());
	auto clipped = [window](QWidget *widget) {
		if(!widget) return QRect();
		QRect visible(widget->mapTo(window, QPoint()), widget->size());
		for(auto *parent = widget->parentWidget(); parent; parent = parent->parentWidget()) {
			visible = visible.intersected(QRect(parent->mapTo(window, QPoint()), parent->size()));
			if(parent == window) break;
		}
		return visible;
	};
	auto healthVisible = [window, clipped] {
		auto *health = window->findChild<QWidget *>("PulseWeaverStreamStats");
		return health && health->isVisibleTo(window) && clipped(health) == QRect(health->mapTo(window, QPoint()), health->size());
	};
	auto rectangles = [=] {
		QJsonObject result;
		for(const auto &entry : {qMakePair("landscape", landscape), qMakePair("portrait", portrait), qMakePair("chat", chat)}) {
			const QPoint origin = entry.second->mapTo(window, QPoint());
			result.insert(entry.first, QJsonArray{origin.x(), origin.y(), entry.second->width(), entry.second->height()});
		}
		for(const char *name : {"PulseWeaverProductionControls", "PulseWeaverStageChoice", "PulseWeaverGameWindow", "PulseWeaverStreamStats", "PulseWeaverPreviewMixerSplit", "PulseWeaverTransport", "PulseWeaverRouteChoicesRecording", "PulseWeaverRecord", "PulseWeaverLive"}) {
			if(auto *widget = window->findChild<QWidget *>(name)) {
				const QPoint origin = widget->mapTo(window, QPoint());
				result.insert(name, QJsonArray{origin.x(), origin.y(), widget->width(), widget->height()});
			}
		}
		return result;
	};
	window->setAttribute(Qt::WA_DontShowOnScreen); window->showNormal(); window->resize(1280, 1000);
	QTimer::singleShot(500, window, [=] {
		const auto before = rectangles();
		window->grab().save(directory + "/main-1280x1000.png");
		const bool wasChecked = toggle->isChecked(); const int previousRate = rate->value();
		const QString previousStatus = status ? status->text() : QString();
		toggle->setChecked(!wasChecked); rate->setValue(previousRate == 2 ? 0 : 2);
		if(status) status->setText("Test connection failed: a long diagnostic must not take space from the program previews or chat.");
		QTimer::singleShot(250, window, [=] {
			const auto after = rectangles();
			QJsonObject report{{"largeBefore", before}, {"largeAfter", after}, {"largeStable", before == after}};
			toggle->setChecked(wasChecked); rate->setValue(previousRate); if(status) status->setText(previousStatus);
			window->resize(1180, 720);
			QTimer::singleShot(500, window, [=]() mutable {
				const auto smallBefore = rectangles(); window->grab().save(directory + "/main-1180x720.png");
				toggle->setChecked(!wasChecked); rate->setValue(previousRate == 2 ? 0 : 2);
				QTimer::singleShot(250, window, [=]() mutable {
					const auto smallAfter = rectangles();
					report.insert("smallBefore", smallBefore); report.insert("smallAfter", smallAfter);
					report.insert("smallStable", smallBefore == smallAfter);
					report.insert("smallProgrammeUsable", clipped(landscape).height() >= 160 && clipped(portrait).height() >= 160);
					report.insert("smallHealthVisible", healthVisible());
					toggle->setChecked(wasChecked); rate->setValue(previousRate);
					for(auto *audio : window->findChildren<QPushButton *>()) {
						if(audio->text() != "Expand audio") continue;
						audio->click(); QApplication::processEvents();
						report.insert("expandedAudioHealthVisible", healthVisible());
						window->grab().save(directory + "/main-1180x720-audio-expanded.png");
						audio->click(); QApplication::processEvents();
						report.insert("audioCollapseRestoresGeometry", rectangles() == smallBefore);
						window->grab().save(directory + "/main-1180x720-audio-restored.png");
						break;
					}
					const auto palette = commands(window, directory);
					for(auto it = palette.begin(); it != palette.end(); ++it) report.insert(it.key(), it.value());
					QFile file(directory + "/layout-audit.json");
					if(file.open(QIODevice::WriteOnly)) file.write(QJsonDocument(report).toJson());
				});
			});
		});
	});
}
}
