#include "../../engine/obs-studio/shared/qt/PulseYouTubeQuota.hpp"
#include "../../engine/obs-studio/shared/qt/PulseYouTubeChatSessions.hpp"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <iostream>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    int failures = 0, checks = 0;
    auto check = [&](bool ok, const char *name) {
        ++checks;
        if (!ok) { ++failures; std::cerr << "FAIL: " << name << '\n'; }
    };
    using namespace PulseYouTubeQuota;
    auto ms = [](const char *date) { return QDateTime::fromString(date, Qt::ISODate).toMSecsSinceEpoch(); };
    const qint64 now = ms("2026-09-28T12:00:00Z");
    check(nextReset(now) == ms("2026-09-29T07:01:00Z"), "summer Pacific reset");
    check(nextReset(ms("2026-01-28T12:00:00Z")) == ms("2026-01-29T08:01:00Z"), "winter Pacific reset");
    check(nextReset(ms("2026-03-08T09:00:00Z")) == ms("2026-03-09T07:01:00Z"), "spring DST reset");
    check(nextReset(ms("2026-11-01T08:00:00Z")) == ms("2026-11-02T08:01:00Z"), "autumn DST reset");
    QTemporaryDir temporary;
    check(temporary.isValid(), "isolated storage");
    const QString file = temporary.filePath("quota.ini");
    const auto pause = record(file, "app", "quotaExceeded", 403, now);
    check(pause.until == nextReset(now), "daily quota rejection");
    check(read(file, "app", now + 1000).until == pause.until, "persisted across readers and routes");
    check(!read(file, "other-app", now).until, "registration isolation");
    int requests = 0;
    auto mockRequest = [&](qint64 time) { if (!read(file, "app", time).until) ++requests; };
    mockRequest(now); mockRequest(now + 60000); mockRequest(pause.until - 1);
    check(requests == 0, "requests suppressed during exhaustion");
    mockRequest(pause.until);
    check(requests == 1, "resume after reset");
    check(record(file, "app", "rateLimitExceeded", 429, now).until == pause.until, "throttle cannot shorten quota pause");
    check(record(file, "rate", "", 429, now).until == now + 60000, "non JSON 429 throttle");
    check(!record(file, "forbidden", "forbidden", 403, now).until, "unrelated 403 is not quota");
    check(exhausted("dailyLimitExceeded") && !exhausted("rateLimitExceeded"), "quota classification");
    check(pollDelay(60000) == 60000 && pollDelay(1) == 5000, "honor full server interval");
    check(backoff(1) == 2000 && backoff(6) == 64000 && backoff(30) == 300000, "bounded backoff");
    PulseYouTubeChat::Session session;
    session.liveChatId = "chat"; session.pageToken = "page"; session.pollIntervalMs = 60000;
    PulseYouTubeChat::failed(session, "backendError", "temporary", now, 0);
    check(session.nextRequestMs == now + 60000 && !session.suspended, "network failure preserves poll interval");
    for (int n = 0; n < 10; ++n) PulseYouTubeChat::failed(session, "backendError", "temporary", now, 0);
    check(session.liveChatId == "chat" && session.pageToken == "page", "retries preserve cursor without rediscovery");
    PulseYouTubeChat::failed(session, "quotaExceeded", "quota", now, pause.until);
    check(session.nextRequestMs == pause.until && !session.suspended, "quota resumes after reset");
    PulseYouTubeChat::failed(session, "liveChatEnded", "ended", now, 0);
    PulseYouTubeChat::Sessions sessions{{"horizontal", session}};
    check(session.suspended && !PulseYouTubeChat::ready(sessions) && !PulseYouTubeChat::available(sessions), "ended chat suspended");
    check(PulseYouTubeChat::targets(sessions).isEmpty() && PulseYouTubeChat::pendingTargets(sessions, {}).isEmpty(), "no sends to ended chat");
    check(message(pause).contains("Reconnecting will not restore quota"), "actionable quota message");
    std::cout << checks << " checks, " << failures << " failures\n";
    return failures ? 1 : 0;
}
