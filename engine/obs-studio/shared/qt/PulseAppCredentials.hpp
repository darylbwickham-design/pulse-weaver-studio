#pragma once

#include <QCoreApplication>
#include <QDir>
#include <QSettings>
#include <QString>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <wincrypt.h>
#ifdef _MSC_VER
#pragma comment(lib, "crypt32.lib")
#endif
#endif

// Public distributions contain no developer registrations. These values belong
// only to the tester's isolated portable profile, never to a show/export.
namespace PulseAppCredentials {
inline QString path()
{
    const QString directory = QDir(QCoreApplication::applicationDirPath()).absoluteFilePath("../../config/pulseweaver");
    QDir().mkpath(directory);
    return directory + "/app-credentials.ini";
}
inline QString protect(const QString &value)
{
    if (value.isEmpty()) return {};
#ifdef _WIN32
    QByteArray bytes = value.toUtf8();
    DATA_BLOB input{DWORD(bytes.size()), reinterpret_cast<BYTE *>(bytes.data())}, output{};
    if (!CryptProtectData(&input, L"Pulse Weaver app credential", nullptr, nullptr, nullptr,
                          CRYPTPROTECT_UI_FORBIDDEN, &output)) return {};
    QByteArray encoded(reinterpret_cast<char *>(output.pbData), int(output.cbData));
    LocalFree(output.pbData);
    return "dpapi:" + QString::fromLatin1(encoded.toBase64());
#else
    return {};
#endif
}
inline QString reveal(const QString &value)
{
#ifdef _WIN32
    if (!value.startsWith("dpapi:")) return {};
    QByteArray bytes = QByteArray::fromBase64(value.mid(6).toLatin1());
    DATA_BLOB input{DWORD(bytes.size()), reinterpret_cast<BYTE *>(bytes.data())}, output{};
    if (!CryptUnprotectData(&input, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &output)) return {};
    const QString result = QString::fromUtf8(reinterpret_cast<char *>(output.pbData), int(output.cbData));
    LocalFree(output.pbData);
    return result;
#else
    return {};
#endif
}
inline QString get(const QString &provider, const QString &field)
{
    QSettings settings(path(), QSettings::IniFormat);
    const QString value = settings.value(provider + '/' + field).toString();
    return field == "client_secret" ? reveal(value) : value;
}
inline bool set(const QString &provider, const QString &field, const QString &value)
{
    const QString stored = field == "client_secret" ? protect(value) : value.trimmed();
    if (!value.isEmpty() && stored.isEmpty()) return false;
    QSettings settings(path(), QSettings::IniFormat);
    settings.setValue(provider + '/' + field, stored);
    settings.sync();
    return settings.status() == QSettings::NoError;
}
}
