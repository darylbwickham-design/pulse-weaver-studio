#pragma once

#include <QHash>
#include <QSet>
#include <QString>
#include <QStringList>

namespace PulseYouTubeChat {

struct Session {
	QString broadcastId;
	QString liveChatId;
	QString pageToken;
	qint64 nextRequestMs = 0;
	bool requestPending = false;
	int failures = 0;
	QString lastError;
};

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

inline bool ready(const Sessions &sessions)
{
	if (sessions.isEmpty())
		return false;
	for (const Session &session : sessions) {
		if (session.liveChatId.isEmpty())
			return false;
	}
	return true;
}

inline bool available(const Sessions &sessions)
{
	for (const Session &session : sessions) {
		if (!session.liveChatId.isEmpty())
			return true;
	}
	return false;
}

inline QStringList targets(const Sessions &sessions)
{
	QStringList result;
	for (const Session &session : sessions) {
		if (!session.liveChatId.isEmpty() && !result.contains(session.liveChatId))
			result.push_back(session.liveChatId);
	}
	return result;
}

inline QHash<QString, QString> pendingTargets(const Sessions &sessions, const QSet<QString> &deliveredRoutes)
{
	QHash<QString, QString> result;
	for (auto session = sessions.cbegin(); session != sessions.cend(); ++session) {
		if (!session->liveChatId.isEmpty() && !deliveredRoutes.contains(session.key()))
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
		if (!session->liveChatId.isEmpty() && !deliveredRoutes.contains(session.key()) &&
		    !blockedRoutes.contains(session.key()))
			result.insert(session.key(), session->liveChatId);
	}
	return result;
}

} // namespace PulseYouTubeChat
