#include "../../engine/obs-studio/shared/qt/PulseChat.hpp"
#include <QJsonDocument>
#include <iostream>
#include <stdexcept>

static void check(bool condition, const char *message) { if (!condition) throw std::runtime_error(message); }
int main(int argc, char **argv)
{
    std::cerr << "Starting Qt chat tests\n";
    QApplication app(argc, argv);
    std::cerr << "Qt ready\n";
    try {
        const auto badges = PulseChat::parseBadges(QJsonDocument::fromJson(R"({"data":[
            {"set_id":"subscriber","versions":[{"id":"0","image_url_1x":"https://example.invalid/sub.png"},
            {"id":"12","image_url_2x":"https://example.invalid/year.png"}]},
            {"set_id":"moderator","versions":[{"id":"1","image_url_1x":"https://example.invalid/mod.png"}]}]})").object());
        check(badges.size() == 3 && badges.value("subscriber/12").path() == "/year.png", "Twitch nested badge versions");
        check(PulseChat::kickFragments("hello [emote:37226:KEKW] world").size() == 3, "Kick emote parsing");
        check(PulseChat::twitchBanBody("42", 600).value("data").toObject().value("duration").toInt() == 600, "Twitch timeout units");
        check(PulseChat::kickBanBody(10, 42, 10).value("duration").toInt() == 10, "Kick timeout units");
        check(PulseChat::kickBanBody(10, 42, 0).value("user_id").isDouble() && !PulseChat::kickBanBody(10, 42, 0).contains("duration"), "Kick numeric ID and permanent ban");
        check(PulseChat::youtubeBanBody("chat", "42", 600).value("snippet").toObject().value("banDurationSeconds").toInt() == 600, "YouTube timeout duration missing");
        check(!PulseChat::youtubeBanBody("chat", "42", 0).value("snippet").toObject().contains("banDurationSeconds"), "YouTube permanent ban has a duration");
        PulseChat::Feed feed;
        feed.setAttribute(Qt::WA_DontShowOnScreen);
        feed.setStyleSheet("QListWidget{background:#18181b;border:0;color:#efeff1;}");
        std::cerr << "Feed ready\n";
        feed.resize(280, 420); feed.show(); app.processEvents();
        feed.setProperty("pulseWeaverChatFilter", "all");
        feed.setProperty("pulseWeaverChatAutoScroll", true);
        const QString longText = "This message must wrap naturally " + QString(180, 'W') + " <b>plain text</b> 日本語 😀";
        for (const QString platform : {"twitch", "youtube", "kick"})
            PulseChat::append(&feed, platform, "LongStreamerUsername", longText, "#59d6c7", {"moderator", "subscriber"}, {}, "42", platform + "-message");
        app.processEvents();
        std::cerr << "Wrapping fixture ready\n";
        const int narrowHeight = feed.visualItemRect(feed.item(0)).height();
        check(narrowHeight > 50, "Long chat did not wrap");
        check(feed.horizontalScrollBar()->maximum() == 0, "Chat has horizontal overflow");
        feed.resize(500, 420); app.processEvents();
        check(feed.visualItemRect(feed.item(0)).height() < narrowHeight, "Resize did not reflow messages");
        check(feed.itemWidget(feed.item(0)) == nullptr, "Per-message widgets reintroduced");
        PulseChat::append(&feed, "twitch", "duplicate", "duplicate", {}, {}, {}, "42", "twitch-message");
        check(feed.count() == 3, "Duplicate EventSub message not suppressed");
        PulseChat::markDeleted(&feed, "twitch", "twitch-message");
        check(feed.item(0)->data(PulseChat::Deleted).toBool() && !feed.item(1)->data(PulseChat::Deleted).toBool(), "Deletion affected another platform");
        for (int i = 0; i < 80; ++i) PulseChat::append(&feed, "youtube", "Viewer", "Message " + QString::number(i));
        app.processEvents();
        check(PulseChat::atBottom(&feed), "New messages did not follow bottom");
        feed.setProperty("pulseWeaverChatAutoScroll", false);
        feed.verticalScrollBar()->setValue(150);
        auto *anchor = feed.itemAt(QPoint(2, 2));
        const int anchorOffset = feed.visualItemRect(anchor).top();
        PulseChat::append(&feed, "kick", "AnotherViewer", "New message while reading history");
        app.processEvents();
        check(feed.itemAt(QPoint(2, 2)) == anchor && feed.visualItemRect(anchor).top() == anchorOffset, "Paused scrollback jumped");
        check(feed.property("pulseWeaverUnreadCount").toInt() == 1, "Unread count missing");
        for (int i = 0; i < 510; ++i) PulseChat::append(&feed, "twitch", "Viewer", "Bounded history " + QString::number(i));
        std::cerr << "History fixture ready\n";
        check(feed.count() == 500, "History limit exceeded");
        feed.clear(); feed.resize(300, 470); feed.setProperty("pulseWeaverChatAutoScroll", true);
        PulseChat::append(&feed, "twitch", "RiverRuns", "Welcome in! Great to see everyone here.", "#bf94ff");
        PulseChat::append(&feed, "kick", "TimeToDoTheTango", "The stream looks great — audio is clear too.", "#53fc18", {"moderator"});
        PulseChat::append(&feed, "youtube", "Luna", "That was a close one 😂", "#69bbff", {"member"});
        PulseChat::append(&feed, "twitch", "LongUsernameThatStillFits", "A longer message wraps into the next line without hiding the name or forcing a sideways scrollbar.", "#ffb86c");
        PulseChat::append(&feed, "youtube", "Nova", "Ready for the next round?", "#f991d0", {"verified"});
        app.processEvents();
        if (argc > 1) feed.grab().save(QString::fromLocal8Bit(argv[1]));
        std::cout << "PASS: nested badges, Kick emotes, wrapping, resize, deduplication, deletion, scrollback, unread count, bounded history\n";
        return 0;
    } catch (const std::exception &error) { std::cerr << "FAIL: " << error.what() << '\n'; return 1; }
}
