#include "PulseYouTubeStream.hpp"
#include "PulseYouTubeChatSessions.hpp"
#include <iostream>
#include <stdexcept>
int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    int checks = 0;
    auto check = [&](bool ok) { ++checks; if (!ok) throw std::runtime_error("check " + std::to_string(checks)); };
    try {
        check(argc == 2);
        QStringList cursors;
        auto run = [&](QString mode, int cancelAfter = -1) {
            QElapsedTimer timer; timer.start();
            return PulseYouTubeStream::run(QString::fromLocal8Bit(argv[1]),
                {{"mode", mode}, {"pageToken", "resume"}},
                [&] { return cancelAfter >= 0 && timer.elapsed() >= cancelAfter; },
                [&](const QJsonObject &batch) { if (batch.contains("nextPageToken")) cursors << batch.value("nextPageToken").toString(); });
        };
        auto result = run("complete");
        check(result.reason.isEmpty() && result.received && result.batches == 2 && result.grpcStatus == 0);
        check(cursors == QStringList({"one", "two"}));
        check(result.durationMs >= 100);
        result = run("empty");
        check(result.reason.isEmpty() && !result.received);
        result = run("error");
        check(result.reason == "deadlineExceeded" && result.grpcStatus == 4);
        check(run("invalid").reason == "streamUnavailable");
        check(run("offline").reason == "liveChatEnded");
        result = run("quiet", 1000);
        check(result.reason == "cancelled" && result.batches == 2 && result.durationMs < 5000);
        PulseYouTubeChat::Session session;
        session.pageToken = "saved";
        session.failures = 2;
        PulseYouTubeChat::completed(session, true, 1000);
        check(session.failures == 0 && session.nextRequestMs == 2000 && session.pageToken == "saved");
        for (int i = 0; i < 20; ++i) PulseYouTubeChat::completed(session, false, 1000);
        check(session.nextRequestMs == 301000 && session.failures == 0);
        PulseYouTubeChat::completed(session, true, 1000);
        check(session.emptyCompletions == 0 && session.nextRequestMs == 2000);
        PulseYouTubeChat::failed(session, "streamUnavailable", "", 1000, 0);
        check(!session.suspended && session.failures == 1);
        PulseYouTubeChat::failed(session, "liveChatEnded", "", 1000, 0);
        check(session.suspended);
        auto sessions = PulseYouTubeChat::create("dual", "a", "b");
        sessions["horizontal"].liveChatId = "shared";
        sessions["vertical"].liveChatId = "shared";
        check(PulseYouTubeChat::targets(sessions).size() == 1);
        sessions["vertical"].requestPending = true;
        check(PulseYouTubeChat::owner(sessions, "shared") == "vertical");
        std::cout << "PASS: " << checks << " lifecycle checks\n";
    } catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
}
