#pragma once
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFontInfo>
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
#include <QSettings>
#include <QSlider>
#include <QSplitter>
#include <QStackedWidget>
#include <QStyle>
#include <QTabWidget>
#include <QTimer>
#include <QWidget>
#include <QWizard>
#include <memory>
#include <algorithm>
#include <obs.h>
#include <obs-source.h>
#if __has_include("PulseLightsUiAudit.hpp")
#include "PulseLightsUiAudit.hpp"
#define PULSEWEAVER_LIGHTS_UI_AUDIT_AVAILABLE 1
#endif

// Explicit, one-shot development audit. The caller only enables this for a
// separate inert portable fixture; normal launches allocate no audit timer.
namespace PulseUiLayoutAudit {
struct MixerAudioFixture {
	QList<obs_source_t *> sources;
	MixerAudioFixture()
	{
		// This source produces no buffers and has no device, media, tick or
		// playback callback. It exists only in an explicitly inert UI fixture.
		static bool registered = false;
		if(!registered) {
			obs_source_info info{};
			info.id = "pulseweaver_audit_inert_audio_v1";
			info.type = OBS_SOURCE_TYPE_INPUT; info.output_flags = OBS_SOURCE_AUDIO;
			info.get_name = [](void *) {return "Pulse Weaver inert mixer audit input";};
			info.create = [](obs_data_t *, obs_source_t *source) -> void * {return source;};
			info.destroy = [](void *) {};
			obs_register_source(&info); registered = true;
		}
		for(int index = 0; index < 8; ++index) {
			const auto name = QString("Fixture silent audio channel %1").arg(index + 1, 2, 10, QLatin1Char('0')).toUtf8();
			auto *source = obs_source_create("pulseweaver_audit_inert_audio_v1", name.constData(), nullptr, nullptr);
			if(!source) continue;
			obs_source_set_muted(source, true); obs_source_set_volume(source, 0.0f);
			obs_source_set_audio_mixers(source, 0);
			obs_source_set_monitoring_type(source, OBS_MONITORING_TYPE_NONE);
			obs_source_inc_active(source); obs_source_set_audio_active(source, true);
			sources.append(source);
		}
	}
	~MixerAudioFixture()
	{
		for(auto *source : sources) {obs_source_dec_active(source); obs_source_remove(source); obs_source_release(source);}
	}
	bool safe() const
	{
		if(sources.size() != 8) return false;
		for(auto *source : sources)
			if(!obs_source_muted(source) || obs_source_get_volume(source) != 0.0f || obs_source_get_audio_mixers(source) != 0 ||
			   obs_source_get_monitoring_type(source) != OBS_MONITORING_TYPE_NONE || !obs_source_active(source) || !obs_source_audio_active(source)) return false;
		return true;
	}
};
inline void settleUi(int milliseconds = 70)
{
	QApplication::processEvents();
	QEventLoop loop; QTimer::singleShot(milliseconds, &loop, &QEventLoop::quit); loop.exec();
}
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
inline QRect visibleRect(QWidget *widget, QWidget *window)
{
	if(!widget || !widget->isVisibleTo(window)) return {};
	QRect visible(widget->mapTo(window, QPoint()), widget->size());
	for(auto *parent = widget->parentWidget(); parent; parent = parent->parentWidget()) {
		visible = visible.intersected(parent->contentsRect().translated(parent->mapTo(window, QPoint())));
		if(parent == window) break;
	}
	return visible;
}
inline bool fullyVisible(QWidget *widget, QWidget *window)
{
	return widget && widget->isVisibleTo(window) && !widget->size().isEmpty() &&
		visibleRect(widget, window) == QRect(widget->mapTo(window, QPoint()), widget->size());
}
inline QList<QPushButton *> workspaceButtons(QWidget *window)
{
	QList<QPushButton *> result{nullptr, nullptr, nullptr, nullptr, nullptr};
	auto *header = window->findChild<QWidget *>("PulseWeaverHeader");
	if(!header) return result;
	for(auto *button : header->findChildren<QPushButton *>()) {
		const auto value = button->property("pulseWorkspaceIndex");
		if(!value.isValid()) continue;
		const int index = value.toInt();
		if(index >= 0 && index < result.size()) result[index] = button;
	}
	return result;
}
inline QJsonObject navigationGeometry(QWidget *window)
{
	QJsonArray entries;
	QList<QRect> occupied;
	bool obvious = true;
	const auto buttons = workspaceButtons(window);
	auto *header = window->findChild<QWidget *>("PulseWeaverHeader");
	for(int index = 0; index < 4; ++index) {
		auto *button = buttons[index];
		if(!button) {obvious = false; continue;}
		button->ensurePolished();
		const int pixels = QFontInfo(button->font()).pixelSize();
		const QSize icon = button->icon().actualSize(button->iconSize());
		QString title = button->text(); title.remove('&');
		const int textWidth = button->fontMetrics().horizontalAdvance(title);
		const bool textFits = button->contentsRect().width() >= textWidth + icon.width() + 8;
		const QRect bounds(button->mapTo(window, QPoint()), button->size());
		bool overlap = false;
		for(const QRect &previous : occupied) overlap = overlap || previous.intersects(bounds);
		occupied.append(bounds);
		const bool adequate = !overlap && fullyVisible(button, window) && button->width() >= 120 && button->height() >= 42 &&
			pixels >= 14 && int(button->font().weight()) >= int(QFont::DemiBold) &&
			!button->icon().isNull() && icon.width() >= 22 && icon.height() >= 22 && textFits &&
			(index != 1 || !title.contains("soon", Qt::CaseInsensitive));
		auto entry = widgetGeometry(button, window);
		entry.insert("workspaceIndex", index); entry.insert("label", title);
		entry.insert("fontPixels", pixels); entry.insert("fontWeight", int(button->font().weight()));
		entry.insert("iconSize", QJsonArray{icon.width(), icon.height()});
		entry.insert("ancestorVisibleRect", rectValue(visibleRect(button, window)));
		entry.insert("minimumHeight", button->minimumHeight()); entry.insert("maximumHeight", button->maximumHeight());
		entry.insert("layoutUsesWidgetRect", button->testAttribute(Qt::WA_LayoutUsesWidgetRect));
		if(header && header->layout()) {
			const int item = header->layout()->indexOf(button);
			if(item >= 0) entry.insert("headerLayoutItemGeometry", rectValue(header->layout()->itemAt(item)->geometry()));
		}
		QJsonArray ancestors;
		for(auto *parent = button->parentWidget(); parent; parent = parent->parentWidget()) {
			ancestors.append(widgetGeometry(parent, window));
			if(parent == window) break;
		}
		entry.insert("ancestors", ancestors);
		entry.insert("textFits", textFits); entry.insert("obvious", adequate);
		entries.append(entry); obvious = obvious && adequate;
	}
	return {{"workspaces", entries}, {"header", widgetGeometry(header, window)},
		{"primaryNavigationObvious", obvious && entries.size() == 4}};
}
inline QJsonObject choiceCardGeometry(QWidget *owner, QWidget *window, int expectedHeight)
{
	QJsonArray cards;
	bool contained = true;
	for(auto *card : owner->findChildren<QPushButton *>("PulseWeaverChoiceCard")) {
		if(!card->isVisibleTo(window)) continue;
		const bool adequate = card->height() == expectedHeight && fullyVisible(card, window);
		auto entry = widgetGeometry(card, window);
		entry.insert("label", card->text()); entry.insert("expectedPaintedHeight", expectedHeight);
		entry.insert("ancestorVisibleRect", rectValue(visibleRect(card, window)));
		entry.insert("paintedContentContained", adequate); cards.append(entry);
		contained = contained && adequate;
	}
	return {{"cards", cards}, {"paintedContentContained", contained && !cards.isEmpty()}};
}
inline QJsonObject operatingGeometry(QWidget *window, QWidget *landscape, QWidget *portrait, QWidget *chat)
{
	const QRect horizontal = visibleRect(landscape, window), vertical = visibleRect(portrait, window);
	const bool compact = fullyVisible(landscape, window) && fullyVisible(portrait, window) &&
		horizontal.top() <= 160 && vertical.top() <= 160;
	const bool programmes = compact && horizontal.height() >= 280 && vertical.height() >= 280;
	auto *health = window->findChild<QWidget *>("PulseWeaverStreamStats");
	auto *transport = window->findChild<QWidget *>("PulseWeaverTransport");
	auto *recording = window->findChild<QWidget *>("PulseWeaverRouteChoicesRecording");
	QJsonArray tracks;
	bool constrained = true;
	for(auto *track : window->findChildren<QWidget *>("PulseWeaverRouteTrack")) {
		if(!track->isVisibleTo(window)) continue;
		tracks.append(widgetGeometry(track, window));
		constrained = constrained && fullyVisible(track, window) && track->height() == 32;
	}
	constrained = constrained && tracks.size() == 4 && recording && fullyVisible(recording, window) &&
		recording->width() >= 150 && recording->width() <= 240 && recording->height() == 32;
	return {{"landscapeVisible", rectValue(horizontal)}, {"portraitVisible", rectValue(vertical)},
		{"mainChromeCompact", compact}, {"smallProgrammeSpaceEfficient", programmes},
		{"smallChatWidthPreserved", fullyVisible(chat, window) && chat->width() >= 280},
		{"smallHealthVisible", fullyVisible(health, window)}, {"smallTransportVisible", fullyVisible(transport, window)},
		{"routeTracks", tracks}, {"routeTracksConstrained", constrained}};
}
inline QJsonObject expandedMixerGeometry(QWidget *window, QWidget *landscape, QWidget *portrait)
{
	auto *scroll = window->findChild<QScrollArea *>("PulseWeaverCompactMixer");
	auto *card = scroll ? scroll->parentWidget() : nullptr;
	auto *body = scroll ? scroll->widget() : nullptr;
	QJsonObject report{{"available", scroll && card && body}};
	if(!scroll || !card || !body) return report;
	auto strips = body->findChildren<QWidget *>("PulseWeaverNativeMixerStrip");
	std::sort(strips.begin(), strips.end(), [body](QWidget *left, QWidget *right) {return left->mapTo(body, QPoint()).x() < right->mapTo(body, QPoint()).x();});
	QJsonArray entries;
	bool native = strips.size() == 8, contained = native;
	for(auto *strip : strips) {
		const QRect bounds(strip->mapTo(window, QPoint()), strip->size());
		int top = bounds.top(), bottom = bounds.bottom();
		for(auto *parent = strip->parentWidget(); parent; parent = parent->parentWidget()) {
			const auto available = parent->contentsRect().translated(parent->mapTo(window, QPoint()));
			top = qMax(top, available.top()); bottom = qMin(bottom, available.bottom());
			if(parent == window) break;
		}
		const bool genuine = QByteArray(strip->metaObject()->className()) == "VolumeControl";
		const bool vertical = strip->isVisibleTo(window) && strip->height() == 180 && bottom - top + 1 == 180;
		auto entry = widgetGeometry(strip, window); entry.insert("nativeVolumeControl", genuine); entry.insert("verticallyContained", vertical);
		entries.append(entry); native = native && genuine; contained = contained && vertical;
	}
	auto *horizontal = scroll->horizontalScrollBar();
	const int previous = horizontal->value();
	bool reachable = !strips.isEmpty();
	if(!strips.isEmpty()) {
		horizontal->setValue(horizontal->minimum()); QApplication::processEvents();
		reachable = reachable && fullyVisible(strips.front(), window);
		horizontal->setValue(horizontal->maximum()); QApplication::processEvents();
		reachable = reachable && fullyVisible(strips.back(), window);
		horizontal->setValue(previous); QApplication::processEvents();
	}
	const int extent = scroll->style()->pixelMetric(QStyle::PM_ScrollBarExtent);
	const int viewport = scroll->viewport()->height();
	const int top = scroll->mapTo(card, QPoint()).y();
	const int bottom = card->layout() ? card->layout()->contentsMargins().bottom() : 0;
	const int naturalLimit = top + 180 + extent + 2 * scroll->frameWidth() + bottom + 12;
	const bool bounded = !scroll->isHidden() && fullyVisible(card, window) && body->height() == 180 &&
		viewport >= 180 && viewport <= 180 + extent + 8 && top <= 70 &&
		card->height() <= naturalLimit && card->height() <= 270 && scroll->verticalScrollBar()->maximum() == 0;
	const bool programme = fullyVisible(landscape, window) && fullyVisible(portrait, window) && landscape->height() >= 160 && portrait->height() >= 160;
	report.insert("card", widgetGeometry(card, window)); report.insert("scroll", widgetGeometry(scroll, window));
	report.insert("body", widgetGeometry(body, window)); report.insert("viewportHeight", viewport);
	report.insert("nativeScrollbarExtent", extent); report.insert("naturalHeightLimit", naturalLimit);
	report.insert("strips", entries); report.insert("nativeEightChannels", native); report.insert("controlsContained", contained);
	report.insert("horizontalControlsReachable", reachable); report.insert("boundedHeight", bounded);
	report.insert("programmeUsable", programme);
	report.insert("healthVisible", fullyVisible(window->findChild<QWidget *>("PulseWeaverStreamStats"), window));
	report.insert("transportVisible", fullyVisible(window->findChild<QWidget *>("PulseWeaverTransport"), window));
	return report;
}
inline QJsonObject mixerGeometryAudit(QWidget *window, QWidget *landscape, QWidget *portrait, QWidget *chat,
				      const QString &directory, const QJsonObject &startup)
{
	auto *scroll = window->findChild<QScrollArea *>("PulseWeaverCompactMixer");
	auto *split = window->findChild<QSplitter *>("PulseWeaverPreviewMixerSplit");
	auto *card = scroll ? scroll->parentWidget() : nullptr;
	QPushButton *toggle = nullptr;
	if(card) for(auto *button : card->findChildren<QPushButton *>()) if(button->text().endsWith("audio")) {toggle = button; break;}
	QJsonObject result{{"mixerPopulatedFixture", false}};
	if(!scroll || !split || !card || !toggle) return result;
	if(!scroll->isHidden()) {toggle->click(); settleUi();}
	QJsonArray profiles;
	bool populated = true, bounded = true, controls = true, reachable = true, previews = true, health = true, transport = true, restored = true, chatStable = true, exactSizes = true;
	int smallest = 10000, largest = 0;
	for(const auto &size : {QSize(1180, 720), QSize(1240, 930), QSize(1280, 1000)}) {
		window->resize(size); settleUi();
		const QRect baselineLandscape(landscape->mapTo(window, QPoint()), landscape->size());
		const QRect baselinePortrait(portrait->mapTo(window, QPoint()), portrait->size());
		const QRect baselineChat(chat->mapTo(window, QPoint()), chat->size());
		for(int cycle = 0; cycle < 2; ++cycle) {
			toggle->click(); settleUi();
			auto state = expandedMixerGeometry(window, landscape, portrait);
			state.insert("windowSize", QJsonArray{size.width(), size.height()}); state.insert("cycle", cycle + 1);
			state.insert("actualWindowSize", QJsonArray{window->width(), window->height()}); exactSizes = exactSizes && window->size() == size;
			profiles.append(state);
			populated = populated && state.value("nativeEightChannels").toBool();
			bounded = bounded && state.value("boundedHeight").toBool(); controls = controls && state.value("controlsContained").toBool();
			reachable = reachable && state.value("horizontalControlsReachable").toBool(); previews = previews && state.value("programmeUsable").toBool();
			health = health && state.value("healthVisible").toBool(); transport = transport && state.value("transportVisible").toBool();
			chatStable = chatStable && QRect(chat->mapTo(window, QPoint()), chat->size()) == baselineChat;
			smallest = qMin(smallest, card->height()); largest = qMax(largest, card->height());
			const QString stem = QString("main-%1x%2-audio-expanded-%3.png").arg(size.width()).arg(size.height()).arg(cycle + 1);
			window->grab().save(directory + "/" + stem);
			if(size == QSize(1180, 720) && cycle == 0) window->grab().save(directory + "/main-1180x720-audio-expanded.png");
			toggle->click(); settleUi();
			restored = restored && QRect(landscape->mapTo(window, QPoint()), landscape->size()) == baselineLandscape &&
				QRect(portrait->mapTo(window, QPoint()), portrait->size()) == baselinePortrait && QRect(chat->mapTo(window, QPoint()), chat->size()) == baselineChat;
		}
	}
	// Obtain a real legacy splitter allocation for the next fresh startup.
	// Only this explicit fixture temporarily lifts the card's allocation cap;
	// all behavioural assertions above and below retain their natural limits.
	toggle->click(); settleUi();
	const int minimum = card->minimumHeight(), maximum = card->maximumHeight();
	const auto policy = card->sizePolicy(); card->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
	card->setMinimumHeight(0); card->setMaximumHeight(QWIDGETSIZE_MAX);
	split->setSizes({1, 2000}); settleUi();
	const int legacyHeight = card->height();
	const QByteArray legacy = split->saveState();
	QSettings stateFile(directory + "/mixer-oversized-state.ini", QSettings::IniFormat);
	stateFile.setValue("showControl2/previewMixerSplit", legacy); stateFile.sync();
	card->setMinimumHeight(minimum); card->setMaximumHeight(maximum); card->setSizePolicy(policy);
	toggle->click(); settleUi(); toggle->click(); settleUi();
	const auto recovered = expandedMixerGeometry(window, landscape, portrait);
	const bool legacyRecovered = legacyHeight > largest + 80 && recovered.value("boundedHeight").toBool() && recovered.value("programmeUsable").toBool();
	window->grab().save(directory + "/main-1280x1000-audio-oversize-recovered.png");
	toggle->click(); settleUi(); window->resize(1180, 720); settleUi();
	window->grab().save(directory + "/main-1180x720-audio-restored.png");
	const QString mode = qEnvironmentVariable("PULSEWEAVER_MIXER_FIXTURE_AUDIT");
	const bool startupPass = mode == "saved-oversize" ? startup.value("boundedHeight").toBool() && startup.value("nativeEightChannels").toBool() && startup.value("controlsContained").toBool() : startup.value("initiallyCollapsed").toBool();
	result.insert("mixerProfiles", profiles); result.insert("mixerStartup", startup);
	result.insert("mixerPopulatedFixture", populated); result.insert("mixerExpandedHeightBounded", bounded && largest - smallest <= 2);
	result.insert("mixerNativeControlsContained", controls); result.insert("mixerHorizontalControlsReachable", reachable);
	result.insert("mixerExpandedProgrammeUsable", previews); result.insert("mixerExpandedTransportVisible", transport);
	result.insert("expandedAudioHealthVisible", health); result.insert("audioCollapseRestoresGeometry", restored);
	result.insert("mixerChatGeometryStable", chatStable); result.insert("mixerLegacyOversizeRecovered", legacyRecovered);
	result.insert("mixerRequestedSizesHonoured", exactSizes);
	result.insert("mixerStartupStatePassed", startupPass); result.insert("mixerLegacyHeight", legacyHeight);
	result.insert("mixerSavedOversizeStateBase64", QString::fromLatin1(legacy.toBase64()));
	return result;
}
inline QJsonObject controlGeometry(QWidget *window, const QString &directory)
{
	auto *control = window->findChild<QPushButton *>("PulseWeaverControlMode");
	auto *show = window->findChild<QPushButton *>("PulseWeaverShowMode");
	auto *stack = window->findChild<QStackedWidget *>("PulseWeaverShowContentStack");
	auto *mount = window->findChild<QWidget *>("PulseWeaverMotionPluginMount");
	QJsonObject result{{"controlModeAvailable", control && show && stack && mount}};
	if(!control || !show || !stack || !mount) return result;
	// Navigation only: never select, save, run, delete or preview a Look.
	control->click(); QApplication::processEvents();
	QJsonArray toggles;
	for(auto *button : {show, control}) {
		auto entry = widgetGeometry(button, window);
		entry.insert("minimumHeight", button->minimumHeight()); entry.insert("maximumHeight", button->maximumHeight());
		entry.insert("ancestorVisibleRect", rectValue(visibleRect(button, window)));
		QJsonArray ancestors;
		for(auto *parent = button->parentWidget(); parent; parent = parent->parentWidget()) {
			ancestors.append(widgetGeometry(parent, window));
			if(parent == window) break;
		}
		entry.insert("ancestors", ancestors); toggles.append(entry);
	}
	result.insert("controlModeToggles", toggles);
	result.insert("controlMount", widgetGeometry(mount, window));
	const bool mounted = stack->currentIndex() == 1 && fullyVisible(control, window) &&
		fullyVisible(show, window) && mount->isVisibleTo(window) && mount->layout() && !mount->findChildren<QPushButton *>().isEmpty();
	QJsonArray strips;
	bool contained = true;
	int choices = 0;
	for(auto *strip : mount->findChildren<QScrollArea *>()) {
		if(strip->minimumHeight() != 44 || strip->maximumHeight() != 44) continue;
		auto entry = widgetGeometry(strip, window);
		bool stripContained = fullyVisible(strip, window) && strip->height() == 44;
		QJsonArray buttons;
		for(auto *choice : strip->findChildren<QPushButton *>("PulseWeaverMotionStageChoice")) {
			const bool choiceContained = choice->height() == 36 && fullyVisible(choice, window);
			auto button = widgetGeometry(choice, window);
			button.insert("label", choice->text()); button.insert("ancestorVisibleRect", rectValue(visibleRect(choice, window)));
			button.insert("fullyContained", choiceContained); buttons.append(button);
			stripContained = stripContained && choiceContained; ++choices;
		}
		entry.insert("choices", buttons); entry.insert("fullyContained", stripContained); strips.append(entry);
		contained = contained && stripContained;
	}
	result.insert("controlModeAvailable", mounted);
	result.insert("motionNavigation", strips);
	result.insert("motionNavigationChoiceCount", choices);
	result.insert("motionNavigationContained", mounted && strips.size() == 2 && choices > 0 && contained);
	window->grab().save(directory + "/workspace-control.png");
	QPushButton *builder = nullptr;
	for(auto *button : mount->findChildren<QPushButton *>())
		if(button->property("motionShowBuilder").toBool()) {builder = button; break;}
	if(builder) {
		auto options = std::make_shared<QJsonObject>();
		QTimer::singleShot(150, window, [window, directory, options] {
			auto *wizard = window->findChild<QWizard *>();
			options->insert("setupOptionGridAvailable", wizard && wizard->isVisible());
			if(!wizard || !wizard->isVisible()) return;
			// The second wizard page only edits its local draft. Never reach
			// source previews, external links or the final Create action.
			wizard->next(); QApplication::processEvents();
			QJsonArray cards;
			QList<QRect> occupied;
			bool contained = true;
			for(auto *card : wizard->findChildren<QPushButton *>("PulseWeaverSetupChoiceCard")) {
				if(!card->isVisibleTo(wizard)) continue;
				const QRect bounds(card->mapTo(wizard, QPoint()), card->size());
				bool overlap = false;
				for(const QRect &previous : occupied) overlap = overlap || previous.intersects(bounds);
				occupied.append(bounds);
				const bool adequate = !overlap && fullyVisible(card, wizard) && card->height() >= 64 && card->height() >= card->minimumHeight();
				auto entry = widgetGeometry(card, wizard);
				entry.insert("label", card->text()); entry.insert("minimumHeight", card->minimumHeight());
				entry.insert("maximumHeight", card->maximumHeight()); entry.insert("overlap", overlap);
				entry.insert("ancestorVisibleRect", rectValue(visibleRect(card, wizard)));
				entry.insert("fullyContained", adequate); cards.append(entry);
				contained = contained && adequate;
			}
			options->insert("setupOptionCards", cards);
			options->insert("setupOptionCardsContained", cards.size() == 6 && contained);
			wizard->grab().save(directory + "/setup-option-cards.png");
			wizard->reject();
			options->insert("setupOptionGridCancelled", !wizard->isVisible());
		});
		builder->click();
		for(auto it = options->begin(); it != options->end(); ++it) result.insert(it.key(), it.value());
	} else {
		result.insert("setupOptionGridAvailable", false);
	}
	show->click(); QApplication::processEvents();
	result.insert("controlReturnsToShow", stack->currentIndex() == 0 && show->isChecked());
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
		const auto cardGeometry = choiceCardGeometry(dialog, dialog, 94);
		result->insert("commandsCardGeometry", cardGeometry);
		result->insert("commandsCardsSized", cardGeometry.value("paintedContentContained"));
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
	const auto navigation = workspaceButtons(window);
	auto *showButton = navigation[0];
	if(showButton) {showButton->click(); QApplication::processEvents();}
	const auto motion = controlGeometry(window, directory);
	for(auto it = motion.begin(); it != motion.end(); ++it) result->insert(it.key(), it.value());
	if(navigation[3]) {navigation[3]->click(); QApplication::processEvents();}
	auto *tabs = window->findChild<QTabWidget *>("PulseWeaverPlatformTabs");
	if(tabs && tabs->count()) {tabs->setCurrentIndex(0); QApplication::processEvents();}
	window->grab().save(directory + "/workspace-action.png");
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
	if(navigation[4]) {navigation[4]->click(); QApplication::processEvents();}
	if(auto *sections = window->findChild<QStackedWidget *>("PulseWeaverSetupSections")) {
		sections->setCurrentIndex(0); QApplication::processEvents();
		window->grab().save(directory + "/setup-categories.png");
		window->grab().save(directory + "/workspace-settings.png");
		const auto cardGeometry = choiceCardGeometry(sections, window, 110);
		result->insert("setupCardGeometry", cardGeometry);
		result->insert("setupCardsSized", cardGeometry.value("paintedContentContained"));
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
	if(navigation[2]) {
		navigation[2]->click(); QApplication::processEvents();
		window->grab().save(directory + "/workspace-camera.png");
		result->insert("cameraWorkspaceVisible", navigation[2]->property("pulseWorkspaceActive").toBool());
	}
	if(navigation[1]) {
		navigation[1]->click(); QApplication::processEvents();
		#ifdef PULSEWEAVER_LIGHTS_UI_AUDIT_AVAILABLE
		const auto lights = PulseLightsUiAudit::inspect(window, directory);
		#else
		const QJsonObject lights{{"lightsMounted", false}, {"auditPending", true}};
		#endif
		result->insert("lightsAudit", lights);
		result->insert("lightsMounted", lights.value("lightsMounted").toBool());
		window->grab().save(directory + "/workspace-lights.png");
	}
	if(showButton) showButton->click();
	return *result;
}
inline void run(QWidget *window, QWidget *landscape, QWidget *portrait, QWidget *chat,
                QCheckBox *toggle, QSlider *rate, QLabel *status, const QString &directory)
{
	if(!landscape || !portrait || !chat || !toggle || !rate) return;
	QDir().mkpath(directory);
	const auto arguments = QCoreApplication::arguments();
	const QString mixerMode = qEnvironmentVariable("PULSEWEAVER_MIXER_FIXTURE_AUDIT");
	bool inertProfile = true;
	if(!mixerMode.isEmpty()) obs_enum_sources([](void *data, obs_source_t *source) {
		if(obs_source_get_type(source) == OBS_SOURCE_TYPE_INPUT) *static_cast<bool *>(data) = false;
		return true;
	}, &inertProfile);
	std::shared_ptr<MixerAudioFixture> audioFixture;
	if((mixerMode == "collapsed" || mixerMode == "saved-oversize") && inertProfile &&
	   arguments.contains("--portable") && arguments.contains("--multi") && arguments.contains("--disable-updater") &&
	   qEnvironmentVariable("PULSEWEAVER_UI_LAYOUT_AUDIT_DIR") == directory)
		audioFixture = std::make_shared<MixerAudioFixture>();
	auto *initialMixer = window->findChild<QScrollArea *>("PulseWeaverCompactMixer");
	const bool mixerInitiallyCollapsed = initialMixer && initialMixer->isHidden();
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
	QTimer::singleShot(audioFixture ? 1300 : 500, window, [=] {
		QJsonObject mixerStartup;
		if(audioFixture) {
			if(initialMixer && !initialMixer->isHidden()) {
				mixerStartup = expandedMixerGeometry(window, landscape, portrait);
				window->grab().save(directory + "/mixer-startup-saved-oversize.png");
				for(auto *button : initialMixer->parentWidget()->findChildren<QPushButton *>())
					if(button->text() == "Collapse audio") {button->click(); settleUi(); break;}
			}
			mixerStartup.insert("initiallyCollapsed", mixerInitiallyCollapsed);
			mixerStartup.insert("mode", mixerMode); mixerStartup.insert("sourcesInert", audioFixture->safe());
		}
		const auto before = rectangles();
		const auto largeNavigation = navigationGeometry(window);
		window->grab().save(directory + "/main-1280x1000.png");
		const bool wasChecked = toggle->isChecked(); const int previousRate = rate->value();
		const QString previousStatus = status ? status->text() : QString();
		toggle->setChecked(!wasChecked); rate->setValue(previousRate == 2 ? 0 : 2);
		if(status) status->setText("Test connection failed: a long diagnostic must not take space from the program previews or chat.");
		QTimer::singleShot(250, window, [=] {
			const auto after = rectangles();
			QJsonObject report{{"largeBefore", before}, {"largeAfter", after}, {"largeStable", before == after}};
			report.insert("mixerFixtureRequested", !mixerMode.isEmpty());
			report.insert("largeNavigation", largeNavigation);
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
					const auto smallNavigation = navigationGeometry(window);
					report.insert("smallNavigation", smallNavigation);
					report.insert("primaryNavigationObvious", largeNavigation.value("primaryNavigationObvious").toBool() &&
						smallNavigation.value("primaryNavigationObvious").toBool());
					const auto operating = operatingGeometry(window, landscape, portrait, chat);
					report.insert("smallOperatingGeometry", operating);
					for(const char *key : {"mainChromeCompact", "smallProgrammeSpaceEfficient", "smallChatWidthPreserved",
							"smallHealthVisible", "smallTransportVisible", "routeTracksConstrained"})
						report.insert(key, operating.value(key));
					toggle->setChecked(wasChecked); rate->setValue(previousRate);
					if(audioFixture) {
						const auto mixer = mixerGeometryAudit(window, landscape, portrait, chat, directory, mixerStartup);
						for(auto it = mixer.begin(); it != mixer.end(); ++it) report.insert(it.key(), it.value());
						report.insert("mixerSourcesInert", audioFixture->safe());
					} else for(auto *audio : window->findChildren<QPushButton *>()) {
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
