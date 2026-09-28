#pragma once
#include <QDateTime>
#include <QSettings>
#include <QTimeZone>
#include <algorithm>
#include <mutex>

namespace PulseYouTubeQuota {
inline bool exhausted(const QString &reason)
{
	return reason == "quotaExceeded" || reason == "dailyLimitExceeded" || reason == "dailyLimitExceededUnreg";
}
inline bool throttled(const QString &reason, long status = 0)
{
	return status == 429 || reason == "rateLimitExceeded" || reason == "userRateLimitExceeded";
}
inline bool terminalChatError(const QString &reason)
{
	return reason == "liveChatEnded" || reason == "liveChatDisabled" || reason == "liveChatNotFound" ||
		reason == "forbidden" || reason == "insufficientPermissions";
}
inline qint64 nextReset(qint64 now)
{
	const QTimeZone pacific("America/Los_Angeles");
	if (!pacific.isValid()) return now + 24LL * 60 * 60 * 1000;
	const auto local = QDateTime::fromMSecsSinceEpoch(now, pacific);
	return QDateTime(local.date().addDays(1), QTime(0, 0), pacific).toMSecsSinceEpoch() + 60000;
}
inline qint64 backoff(int failures)
{
	return std::min<qint64>(300000, 2000LL << std::clamp(failures - 1, 0, 8));
}
inline qint64 pollDelay(qint64 requested) { return std::max<qint64>(5000, requested); }
struct Pause {
	qint64 until = 0;
	QString reason;
};
inline std::mutex storageMutex;
inline Pause read(const QString &file, const QString &key, qint64 now)
{
	std::lock_guard<std::mutex> lock(storageMutex);
	QSettings settings(file, QSettings::IniFormat);
	Pause pause{settings.value(key + "/until").toLongLong(), settings.value(key + "/reason").toString()};
	return pause.until > now ? pause : Pause{};
}
inline Pause record(const QString &file, const QString &key, const QString &reason, long status, qint64 now)
{
	Pause pause;
	if (exhausted(reason)) pause = {nextReset(now), reason};
	else if (throttled(reason, status)) pause = {now + 60000, "rateLimitExceeded"};
	else return pause;
	std::lock_guard<std::mutex> lock(storageMutex);
	QSettings settings(file, QSettings::IniFormat);
	const qint64 existing = settings.value(key + "/until").toLongLong();
	if (existing > pause.until)
		return {existing, settings.value(key + "/reason").toString()};
	settings.setValue(key + "/until", pause.until);
	settings.setValue(key + "/reason", pause.reason);
	settings.sync();
	return pause;
}
inline QString message(const Pause &pause)
{
	if (!pause.until) return {};
	const auto until = QDateTime::fromMSecsSinceEpoch(pause.until).toLocalTime().toString("ddd d MMM HH:mm t");
	return exhausted(pause.reason) ?
		"YouTube API quota exhausted. Requests paused until " + until +
		" (daily reset). Reconnecting will not restore quota. Twitch and Kick are unaffected." :
		"YouTube is limiting requests. Retrying after " + until + ".";
}
}
