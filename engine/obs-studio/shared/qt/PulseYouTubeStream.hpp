#pragma once
#include <QCoreApplication>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QProcess>
#include <QSet>
#include <functional>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace PulseYouTubeStream {
struct Result {
    QString reason;
    bool received = false;
};
inline bool fallbackEligible(const QString &reason)
{
    return reason == "streamUnsupported" || reason == "streamUnavailable";
}
inline bool terminal(const QString &reason)
{
    return reason == "unauthenticated" || reason == "invalidArgument";
}
inline QString eventType(QString type)
{
    if (!type.contains('_')) return type;
    const auto words = type.toLower().split('_');
    QString result = words.value(0);
    for (int i = 1; i < words.size(); ++i) {
        auto word = words[i]; if (!word.isEmpty()) word[0] = word[0].toUpper(); result += word;
    }
    return result;
}
inline Result run(const QString &executable, const QJsonObject &input,
                  const std::function<bool()> &cancelled,
                  const std::function<void(const QJsonObject &)> &batch)
{
    if (cancelled()) return {"cancelled"};
    if (!QFileInfo::exists(executable)) return {"streamUnsupported"};
    QProcess child;
#ifdef _WIN32
    child.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *args) {
        args->flags |= CREATE_NO_WINDOW;
    });
#endif
    child.setStandardErrorFile(QProcess::nullDevice());
    child.start(executable, QStringList{});
    if (!child.waitForStarted(5000)) return {"streamUnsupported"};
    child.write(QJsonDocument(input).toJson(QJsonDocument::Compact) + '\n');
    Result result;
    QByteArray pending;
    const QSet<QString> reasons{"quotaExceeded", "rateLimitExceeded", "unauthenticated", "forbidden",
        "liveChatEnded", "liveChatNotFound", "streamUnsupported", "invalidArgument", "streamUnavailable"};
    while (true) {
        if (cancelled()) { result.reason = "cancelled"; break; }
        child.waitForReadyRead(250);
        pending += child.readAllStandardOutput();
        if (pending.size() > 8 * 1024 * 1024) { result.reason = "streamUnavailable"; break; }
        int end;
        while ((end = pending.indexOf('\n')) >= 0) {
            const auto line = pending.left(end); pending.remove(0, end + 1);
            QJsonParseError error;
            const auto document = QJsonDocument::fromJson(line, &error);
            if (error.error != QJsonParseError::NoError || !document.isObject()) {
                result.reason = "streamUnavailable"; break;
            }
            const auto object = document.object();
            if (object.contains("error")) {
                const auto reason = object.value("error").toString();
                result.reason = reasons.contains(reason) ? reason : "streamUnavailable";
                break;
            }
            result.received = true;
            batch(object);
            bool ended = !object.value("offlineAt").toString().isEmpty();
            for (const auto &item : object.value("items").toArray())
                ended = ended || eventType(item.toObject().value("snippet").toObject().value("type").toString()) == "chatEndedEvent";
            if (ended) {
                result.reason = "liveChatEnded"; break;
            }
        }
        if (!result.reason.isEmpty()) break;
        if (child.state() == QProcess::NotRunning) {
            if (!pending.trimmed().isEmpty() || child.exitCode() != 0 || child.exitStatus() != QProcess::NormalExit)
                result.reason = "streamUnavailable";
            break;
        }
    }
    child.closeWriteChannel();
    if (!child.waitForFinished(1000)) { child.kill(); child.waitForFinished(1000); }
    if (!result.received && result.reason.isEmpty()) result.reason = "streamUnavailable";
    return result;
}
}
