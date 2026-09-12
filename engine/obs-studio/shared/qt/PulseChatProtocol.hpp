#pragma once
#include <QJsonObject>

namespace PulseChat {
inline QJsonObject twitchBanBody(const QString &user, int seconds)
{
    QJsonObject data{{"user_id", user}};
    if (seconds > 0) data.insert("duration", seconds);
    return {{"data", data}};
}
inline QJsonObject kickBanBody(qint64 broadcaster, qint64 user, int minutes)
{
    QJsonObject data{{"broadcaster_user_id", broadcaster}, {"user_id", user}};
    if (minutes > 0) data.insert("duration", minutes);
    return data;
}
inline QJsonObject youtubeBanBody(const QString &chat, const QString &user, int seconds)
{
    QJsonObject snippet{{"liveChatId", chat}, {"type", seconds > 0 ? "temporary" : "permanent"},
        {"bannedUserDetails", QJsonObject{{"channelId", user}}}};
    if (seconds > 0) snippet.insert("banDurationSeconds", seconds);
    return {{"snippet", snippet}};
}
}
