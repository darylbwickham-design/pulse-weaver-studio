#include "../../engine/obs-studio/shared/qt/PulseYouTubeStream.hpp"
#include "../../engine/obs-studio/shared/qt/PulseYouTubeChatSessions.hpp"
#include <QElapsedTimer>
#include <QThread>
#include <iostream>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    if (argc == 1) {
        // The test subprocess only consumes test input. No network or real credentials.
        std::string line; std::getline(std::cin, line);
        const auto input = QJsonDocument::fromJson(QByteArray::fromStdString(line)).object();
        const auto mode = input.value("chatId").toString();
        if (mode == "hang") { QThread::sleep(30); return 0; }
        if (mode == "malformed") { std::cout << "not-json\n" << std::flush; return 0; }
        if (mode == "unknown-error") { std::cout << "{\"error\":\"secret\"}\n" << std::flush; return 0; }
        if (mode == "quota") { std::cout << "{\"error\":\"quotaExceeded\"}\n" << std::flush; return 0; }
        std::cout << "{\"nextPageToken\":\"" << input.value("pageToken").toString().toStdString() << "\"}\n" << std::flush;
        std::cout << "{\"nextPageToken\":\"second\",\"offlineAt\":\"2026-09-28\"}\n" << std::flush;
        return 0;
    }
    int checks = 0, failures = 0;
    auto check = [&](bool ok, const char *name) { ++checks; if (!ok) { ++failures; std::cerr << name << '\n'; } };
    using namespace PulseYouTubeStream;
    const auto executable = QCoreApplication::applicationFilePath();
    QStringList cursors;
    auto collect = [&](const QJsonObject &object) { cursors.append(object.value("nextPageToken").toString()); };
    const auto stream = run(executable, {{"chatId", "frames"}, {"pageToken", "resume"}}, [] { return false; }, collect);
    check(stream.received && stream.reason == "liveChatEnded", "offline response terminates stream");
    check(cursors == QStringList{"resume", "second"}, "cursor resume and multiple messages");
    check(run(executable, {{"chatId", "quota"}}, [] { return false; }, collect).reason == "quotaExceeded", "quota propagated");
    check(run(executable, {{"chatId", "malformed"}}, [] { return false; }, collect).reason == "streamUnavailable", "malformed frame rejected");
    check(run(executable, {{"chatId", "unknown-error"}}, [] { return false; }, collect).reason == "streamUnavailable", "untrusted error sanitized");
    QElapsedTimer clock; clock.start();
    check(run(executable, {{"chatId", "hang"}}, [&] { return clock.elapsed() > 300; }, collect).reason == "cancelled", "idle stream cancellation");
    check(clock.elapsed() < 4000, "cancellation bounded");
    check(!fallbackEligible("quotaExceeded") && !fallbackEligible("unauthenticated") && !fallbackEligible("rateLimitExceeded"), "no fallback around quota or auth");
    check(fallbackEligible("streamUnsupported") && fallbackEligible("streamUnavailable"), "transport fallback allowed");
    check(eventType("TEXT_MESSAGE_EVENT") == "textMessageEvent" && eventType("SUPER_CHAT_EVENT") == "superChatEvent", "protobuf enum normalization");
    check(eventType("textMessageEvent") == "textMessageEvent", "REST event normalization");
    PulseYouTubeChat::Sessions sessions{{"vertical", {"v", "shared"}}, {"horizontal", {"h", "shared"}}};
    check(PulseYouTubeChat::owner(sessions, "shared") == "horizontal", "one deterministic shared reader");
    sessions["vertical"].requestPending = true;
    check(PulseYouTubeChat::owner(sessions, "shared") == "vertical", "retain existing reader after route changes");
    check(terminal("unauthenticated") && !terminal("streamUnavailable"), "authentication suspension");
    std::cout << checks << " checks, " << failures << " failures\n";
    return failures ? 1 : 0;
}
