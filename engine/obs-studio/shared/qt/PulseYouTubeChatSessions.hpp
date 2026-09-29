#pragma once

#include <QHash>
#include <QSet>
#include <QString>
#include <QStringList>
#include "PulseYouTubeQuota.hpp"

namespace PulseYouTubeChat {

struct Session {
	QString broadcastId;
	QString liveChatId;
	QString pageToken;
	qint64 nextRequestMs = 0;
	bool requestPending = false;
	int failures = 0;
	QString lastError;
	bool suspended = false;
	qint64 pollIntervalMs = 5000;
	int emptyCompletions = 0;
};

inline void failed(Session &session, const QString &reason, const QString &error, qint64 now, qint64 blockedUntil)
{
	session.failures = std::min(session.failures + 1, 30);
	session.lastError = error;
	session.suspended = PulseYouTubeQuota::terminalChatError(reason);
	session.nextRequestMs = std::max(blockedUntil,
		now + std::max(session.pollIntervalMs, PulseYouTubeQuota::backoff(session.failures)));
}

// Successful completion remains successful. Guard against rapid reconnects
// separately from RPC errors, preserving streaming and the resume cursor.
inline void completed(Session &session, bool receivedBatch, qint64 now)
{
    session.failures = 0;
    session.lastError.clear();
    session.emptyCompletions = receivedBatch ? 0 : std::min(7, session.emptyCompletions + 1);
    const qint64 delay = session.emptyCompletions == 0 ? 1000 :
        std::min<qint64>(300000, 5000LL << (session.emptyCompletions - 1));
    session.nextRequestMs = now + delay;
}

using Sessions = QHash<QString, Session>;

inline Sessions create(const QString &mode, const QString &primaryBroadcastId,
			       const QString &secondaryBroadcastId = {})
{
	Sessions sessions;
	if (!primaryBroadcastId.isEmpty())
		sessions.insert(mode == "vertical" ? "vertical" : "horizontal",
				{primaryBroadcastId});
	if (mode == "dual" && !secondaryBroadcastId.isEmpty())
		sessions.insert("vertical", {secondaryBroadcastId});
	return sessions;
}

inline QString owner(const Sessions &sessions, const QString &chatId)
{
	for (auto it = sessions.cbegin(); it != sessions.cend(); ++it)
		if (it->liveChatId == chatId && it->requestPending) return it.key();
	QString route;
	for (auto it = sessions.cbegin(); it != sessions.cend(); ++it)
		if (it->liveChatId == chatId && (route.isEmpty() || it.key() < route)) route = it.key();
	return route;
}

inline bool ready(const Sessions &sessions)
{
	if (sessions.isEmpty())
		return false;
	for (const Session &session : sessions) {
		if (session.suspended || session.liveChatId.isEmpty())
			return false;
	}
	return true;
}

inline bool available(const Sessions &sessions)
{
	for (const Session &session : sessions) {
		if (!session.suspended && !session.liveChatId.isEmpty())
			return true;
	}
	return false;
}

inline QStringList targets(const Sessions &sessions)
{
	QStringList result;
	for (const Session &session : sessions) {
		if (!session.suspended && !session.liveChatId.isEmpty() && !result.contains(session.liveChatId))
			result.push_back(session.liveChatId);
	}
	return result;
}

inline QHash<QString, QString> pendingTargets(const Sessions &sessions, const QSet<QString> &deliveredRoutes)
{
	QHash<QString, QString> result;
	for (auto session = sessions.cbegin(); session != sessions.cend(); ++session) {
		if (!session->suspended && !session->liveChatId.isEmpty() && !deliveredRoutes.contains(session.key()))
			result.insert(session.key(), session->liveChatId);
	}
	return result;
}

inline QSet<QString> owedRoutes(const Sessions &sessions, const QSet<QString> &deliveredRoutes)
{
	QSet<QString> result;
	for (auto session = sessions.cbegin(); session != sessions.cend(); ++session) {
		if (!deliveredRoutes.contains(session.key()))
			result.insert(session.key());
	}
	return result;
}

inline QHash<QString, QString> pendingTargets(const Sessions &sessions, const QSet<QString> &deliveredRoutes,
					       const QSet<QString> &blockedRoutes)
{
	QHash<QString, QString> result;
	for (auto session = sessions.cbegin(); session != sessions.cend(); ++session) {
		if (!session->suspended && !session->liveChatId.isEmpty() && !deliveredRoutes.contains(session.key()) &&
		    !blockedRoutes.contains(session.key()))
			result.insert(session.key(), session->liveChatId);
	}
	return result;
}

} // namespace PulseYouTubeChat
