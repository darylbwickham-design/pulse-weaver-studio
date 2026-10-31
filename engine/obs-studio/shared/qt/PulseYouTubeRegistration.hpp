#pragma once

#include "PulseAppCredentials.hpp"
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

// Google desktop clients are public clients. This registration identifies the
// application; it must never contain a user's access or refresh tokens.
namespace PulseYouTubeRegistration {
struct Registration {
    QString clientId;
    QString clientSecret;
};

inline QString validationError(const Registration &value)
{
    if (value.clientId.isEmpty() && value.clientSecret.isEmpty()) return {};
    if (value.clientId.isEmpty() || value.clientSecret.isEmpty())
        return "Enter both the client ID and client secret, or leave both empty to use the shared app.";
    static const QRegularExpression id("^[A-Za-z0-9_-]+\\.apps\\.googleusercontent\\.com$");
    if (value.clientId.size() > 512 || !id.match(value.clientId).hasMatch())
        return "Enter a Google OAuth client ID ending in .apps.googleusercontent.com. An API key will not work.";
    if (value.clientSecret.size() > 4096 || value.clientSecret.contains(QRegularExpression("\\s")))
        return "The client secret contains spaces or is too long. Copy it again from your Desktop app client.";
    return {};
}

inline Registration local(const QString &fileName = PulseAppCredentials::path())
{
    QSettings settings(fileName, QSettings::IniFormat);
    if (settings.contains("youtube/desktop_registration")) {
        const auto json = QJsonDocument::fromJson(PulseAppCredentials::reveal(
            settings.value("youtube/desktop_registration").toString()).toUtf8()).object();
        return {json.value("client_id").toString(), json.value("client_secret").toString()};
    }
    return {settings.value("youtube/client_id").toString(),
            PulseAppCredentials::reveal(settings.value("youtube/client_secret").toString())};
}

// One protected value and one atomic QSettings sync: never persist half a pair.
// Tokens and other providers are left untouched; callers must disconnect first.
inline bool saveLocal(const Registration &value, const QString &fileName = PulseAppCredentials::path())
{
    if (!validationError(value).isEmpty()) return false;
    const QString protectedPair = value.clientId.isEmpty() ? QString() : PulseAppCredentials::protect(
        QString::fromUtf8(QJsonDocument(QJsonObject{{"client_id", value.clientId},
            {"client_secret", value.clientSecret}}).toJson(QJsonDocument::Compact)));
    if (!value.clientId.isEmpty() && protectedPair.isEmpty()) return false;
    QSettings settings(fileName, QSettings::IniFormat);
    if (protectedPair.isEmpty()) settings.remove("youtube/desktop_registration");
    else settings.setValue("youtube/desktop_registration", protectedPair);
    settings.remove("youtube/client_id");
    settings.remove("youtube/client_secret");
    settings.sync();
    return settings.status() == QSettings::NoError;
}

inline bool matchesAccount(const QString &savedClientId, const Registration &registration)
{
    // Unbound legacy accounts are bound when first loaded successfully.
    return savedClientId.isEmpty() || savedClientId == registration.clientId;
}

inline Registration bundled(const QString &fileName)
{
    QFile file(fileName);
    if (!file.open(QIODevice::ReadOnly) || file.size() > 16384) return {};
    const auto document = QJsonDocument::fromJson(file.readAll());
    const auto root = document.object();
    if (root.size() != 1 || !root.value("installed").isObject()) return {};
    const auto client = root.value("installed").toObject();
    if ((client.size() != 1 && client.size() != 2) || !client.value("client_id").isString() ||
        (client.size() == 2 && !client.value("client_secret").isString())) return {};
    Registration result{client.value("client_id").toString().trimmed(),
                        client.value("client_secret").toString().trimmed()};
    if (!result.clientId.endsWith(".apps.googleusercontent.com") ||
        (client.size() == 2 && result.clientSecret.isEmpty())) return {};
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

inline Registration defaults()
{
    const auto fileName = QDir(QCoreApplication::applicationDirPath())
        .absoluteFilePath("../../data/pulse-weaver/youtube-desktop-client.json");
    return bundled(fileName);
}

inline Registration forEditor(const Registration &saved, const Registration &defaults)
{
    // Older installations could store the publisher's registration locally.
    // It is still a bundled app, and must never be prefilled as user input.
    return !defaults.clientId.isEmpty() && saved.clientId == defaults.clientId ? Registration{} : saved;
}

inline Registration current()
{
    return resolve(local(), defaults());
}
}
