#pragma once
#include <QJsonObject>
#include <QString>

namespace PulseYouTubeReusableStream {
inline bool compatible(const QJsonObject &stream, const QString &id, const QString &channel)
{
    const auto cdn = stream.value("cdn").toObject();
    const auto state = stream.value("status").toObject().value("streamStatus").toString();
    return !id.isEmpty() && !channel.isEmpty() && stream.value("id").toString() == id &&
        stream.value("snippet").toObject().value("channelId").toString() == channel &&
        stream.value("contentDetails").toObject().value("isReusable").toBool() &&
        (state == "created" || state == "ready" || state == "inactive") &&
        cdn.value("ingestionType").toString() == "rtmp" &&
        cdn.value("resolution").toString() == "variable" &&
        cdn.value("frameRate").toString() == "variable" &&
        !cdn.value("ingestionInfo").toObject().value("streamName").toString().isEmpty();
}
}
