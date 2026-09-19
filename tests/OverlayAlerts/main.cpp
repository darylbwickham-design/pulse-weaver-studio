#include "pulse-overlay-alerts.hpp"
#include "pulse-overlay-renderer.hpp"

#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>

#include <iostream>

namespace {

int failures = 0;

void expect(bool condition, const char *message)
{
	if (condition)
		return;
	std::cerr << "FAIL: " << message << '\n';
	++failures;
}

QJsonObject variation(const char *id, const char *event, double minimumAmount = 0, int minimumViewers = 0)
{
	return {{"id", id}, {"event", event}, {"minAmount", minimumAmount}, {"minViewers", minimumViewers}};
}

void variationMatchingHonoursEventAndThresholds()
{
	const QJsonArray variations{variation("follow", "audience.followed"),
		variation("tip", "support.contribution", 10), variation("raid", "audience.raid", 0, 25)};
	bool matched = false;
	expect(PulseOverlay::matchAlertVariation(variations, "support.contribution", {{"amount", 9}}, &matched).isEmpty() && !matched,
	       "contribution below threshold is rejected");
	expect(PulseOverlay::matchAlertVariation(variations, "support.contribution", {{"amount", 10}}, &matched).value("id") == "tip" && matched,
	       "contribution at threshold matches");
	expect(PulseOverlay::matchAlertVariation(variations, "audience.raid", {{"viewers", 24}}, &matched).isEmpty(),
	       "raid below viewer threshold is rejected");
	expect(PulseOverlay::matchAlertVariation(variations, "pulseweaver.overlay.trigger",
		{{"event", "audience.raid"}, {"viewers", 25}}, &matched).value("id") == "raid",
	       "manual trigger selects an explicit variation event");
	expect(PulseOverlay::matchAlertVariation(variations, "audience.followed", {}, &matched).value("id") == "follow",
	       "follow canonical event matches exactly");
}

void templatesUseStableFallbacks()
{
	expect(PulseOverlay::expandAlertTemplate("{user}:{amount}:{viewers}:{platform}",
		{{"user", QJsonObject{{"displayName", "Nested User"}}}, {"amount", 4.5}, {"viewers", 27}, {"platform", "twitch"}})
		== "Nested User:4.5:27:twitch", "template expands nested user and numeric values");
	expect(PulseOverlay::expandAlertTemplate("Hello {user}", {}) == "Hello Someone",
	       "missing user has a deterministic fallback");
}

void providerPayloadsNormalizeForAlertTemplates()
{
	const QJsonObject twitchRaid = PulseOverlay::normalizeAlertEvent(
		{{"from_broadcaster_user_name", "Raider"}, {"viewer_count", 73}}, "audience.raid", "Twitch");
	expect(twitchRaid.value("user_name") == "Raider" && twitchRaid.value("viewers").toInt() == 73,
	       "Twitch raid normalizes user and viewer count");
	const QJsonObject twitchCheer = PulseOverlay::normalizeAlertEvent(
		{{"user_name", "Cheerer"}, {"bits", 250}}, "support.contribution", "Twitch");
	expect(twitchCheer.value("amount").toDouble() == 250 && twitchCheer.value("platform") == "twitch",
	       "Twitch cheer normalizes amount and platform");
	const QJsonObject youtube = PulseOverlay::normalizeAlertEvent(
		{{"authorDetails", QJsonObject{{"displayName", "Viewer"}}},
		 {"monetaryDetails", QJsonObject{{"amountMicros", 3500000.0}}}},
		"support.contribution", "YouTube");
	expect(youtube.value("user_name") == "Viewer" && youtube.value("amount").toDouble() == 3.5,
	       "YouTube contribution normalizes nested user and micros");
}

void laneOwnsOrderingPauseSkipAndReload()
{
	PulseOverlay::AlertLane lane;
	expect(lane.enqueue({"one", {{"sequence", 1}}}, 2), "first event queues");
	expect(lane.enqueue({"two", {{"sequence", 2}}}, 2), "second event queues");
	expect(!lane.enqueue({"three", {}}, 2), "pending capacity rejects newest event");
	lane.setPaused(true);
	expect(!lane.beginNext().has_value(), "paused lane starts nothing");
	lane.setPaused(false);
	auto first = lane.beginNext();
	expect(first && first->eventKey == "one" && lane.isActive(), "first queued event starts in order");
	expect(!lane.beginNext().has_value(), "active lane cannot start a duplicate renderer item");
	lane.markReloadPending();
	expect(lane.skipCurrent() && !lane.isActive(), "skip completes only current item");
	expect(lane.takeReloadPending() && !lane.takeReloadPending(), "deferred reload is consumed once");
	auto second = lane.beginNext();
	expect(second && second->eventKey == "two", "queue advances after skip");
	expect(lane.completeCurrent(), "normal completion releases the lane");
	lane.enqueue({"four", {}}, 2);
	lane.clearPending();
	expect(lane.pendingCount() == 0 && !lane.beginNext(), "stop-and-clear removes pending work");
}

} // namespace

int main(int argc, char **argv)
{
	QCoreApplication application(argc, argv);
	// Emit production HTML for isolated browser integration tests; no copied JS.
	if (argc == 6 && QString::fromUtf8(argv[1]) == "--render") {
		QFile input(QString::fromUtf8(argv[2])), output(QString::fromUtf8(argv[5]));
		if (!input.open(QIODevice::ReadOnly) || !output.open(QIODevice::WriteOnly)) return 2;
		const auto document = QJsonDocument::fromJson(input.readAll()).object();
		const QByteArray html = PulseOverlay::renderPage(document, QString::fromUtf8(argv[3]), QString::fromUtf8(argv[4])).toUtf8();
		return output.write(html) == html.size() ? 0 : 3;
	}
	if (argc == 3 && QString::fromUtf8(argv[1]) == "--bootstrap") {
		QFile output(QString::fromUtf8(argv[2]));
		const QByteArray html = PulseOverlay::renderBootstrap().toUtf8();
		return output.open(QIODevice::WriteOnly) && output.write(html) == html.size() ? 0 : 3;
	}
	variationMatchingHonoursEventAndThresholds();
	templatesUseStableFallbacks();
	providerPayloadsNormalizeForAlertTemplates();
	laneOwnsOrderingPauseSkipAndReload();
	if (!failures)
		std::cout << "Pulse overlay alert tests passed\n";
	return failures ? 1 : 0;
}
