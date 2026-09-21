#pragma once

#include "PulseAppCredentials.hpp"
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>

// Google desktop clients are public clients. This registration identifies the
// application; it must never contain a user's access or refresh tokens.
namespace PulseYouTubeRegistration {
struct Registration {
    QString clientId;
    QString clientSecret;
};

inline Registration bundled(const QString &fileName)
{
    QFile file(fileName);
    if (!file.open(QIODevice::ReadOnly) || file.size() > 16384) return {};
    const auto document = QJsonDocument::fromJson(file.readAll());
    const auto root = document.object();
    if (root.size() != 1 || !root.value("installed").isObject()) return {};
    const auto client = root.value("installed").toObject();
    if (client.size() != 2 || !client.value("client_id").isString() ||
        !client.value("client_secret").isString()) return {};
    Registration result{client.value("client_id").toString().trimmed(),
                        client.value("client_secret").toString().trimmed()};
    if (!result.clientId.endsWith(".apps.googleusercontent.com") || result.clientSecret.isEmpty()) return {};
    return result;
}

inline Registration resolve(const Registration &local, const Registration &defaults)
{
    if (local.clientId.isEmpty()) return defaults;
    // Never mix a custom/legacy client ID with another registration's secret.
    if (local.clientSecret.isEmpty() && local.clientId == defaults.clientId)
        return defaults;
    return local;
}

inline Registration current()
{
    const auto fileName = QDir(QCoreApplication::applicationDirPath())
        .absoluteFilePath("../../data/pulse-weaver/youtube-desktop-client.json");
    return resolve({PulseAppCredentials::get("youtube", "client_id"),
                    PulseAppCredentials::get("youtube", "client_secret")}, bundled(fileName));
}
}
