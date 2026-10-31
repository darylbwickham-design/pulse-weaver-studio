#include "../../engine/obs-studio/shared/qt/PulseYouTubeRegistration.hpp"
#include <QTemporaryDir>
#include <cstdio>
#include <cstdlib>

static void check(bool ok, const char *message)
{
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    using namespace PulseYouTubeRegistration;
    if (argc == 2 && QString::fromLocal8Bit(argv[1]) == "--isolated-runtime") {
        QDir root(QCoreApplication::applicationDirPath()); root.cdUp(); root.cdUp();
        check(root.dirName().startsWith("PulseWeaver-youtube-proof-"), "Only operate on disposable runtime");
        check(!QFile::exists(PulseAppCredentials::path()), "Fresh runtime has no saved registration or tokens");
        const auto defaults = bundled(root.filePath("data/pulse-weaver/youtube-desktop-client.json"));
        check(!defaults.clientId.isEmpty(), "Real packaged registration is present");
        const auto fresh = current();
        check(fresh.clientId == defaults.clientId && fresh.clientSecret == defaults.clientSecret,
              "Production lookup resolves registration on a clean installation");
        check(!QFile::exists(PulseAppCredentials::path()), "Reading fallback does not create account settings");
        check(PulseAppCredentials::set("youtube", "client_id", "legacy.apps.googleusercontent.com") &&
              PulseAppCredentials::set("youtube", "client_secret", "synthetic-secret") &&
              PulseAppCredentials::set("youtube", "refresh_token", "synthetic-refresh"), "Create synthetic existing profile");
        auto bytes = [] { QFile file(PulseAppCredentials::path()); check(file.open(QIODevice::ReadOnly), "Read fixture"); return file.readAll(); };
        const auto before = bytes();
        const auto existing = current();
        check(existing.clientId == "legacy.apps.googleusercontent.com" && existing.clientSecret == "synthetic-secret",
              "Production lookup preserves existing encrypted registration");
        check(bytes() == before && PulseAppCredentials::get("youtube", "refresh_token") == "synthetic-refresh",
              "Existing profile and tokens remain unchanged");
        std::puts("PASS: production lookup on clean packaged layout and existing encrypted profile; no real user account used");
        return 0;
    }
    if (argc == 2) {
        const auto registration = bundled(QString::fromLocal8Bit(argv[1]));
        check(!registration.clientId.isEmpty(), "Packaged desktop registration");
        check(resolve({}, registration).clientId == registration.clientId, "Fresh installation uses packaged client");
        std::puts("PASS: packaged desktop registration is usable without local credentials (values not logged)");
        return 0;
    }
    QTemporaryDir temporary;
    check(temporary.isValid(), "Temporary directory");
    check(validationError({}).isEmpty(), "Blank fields select built-in app");
    for (const Registration &invalid : {Registration{"only-id.apps.googleusercontent.com", {}},
        Registration{{}, "only-secret"}, Registration{"AIza-not-an-oauth-client", "secret"},
        Registration{"bad/path.apps.googleusercontent.com", "secret"},
        Registration{"fixture.apps.googleusercontent.com", "with spaces"}})
        check(!validationError(invalid).isEmpty(), "Incomplete/invalid overrides cannot be saved");
    check(matchesAccount("old.apps.googleusercontent.com", {"old.apps.googleusercontent.com", "new-secret"}),
          "Token remains bound to the same client across secret rotation");
    check(!matchesAccount("old.apps.googleusercontent.com", {"new.apps.googleusercontent.com", "secret"}),
          "Tokens from another app are never loaded");
    check(matchesAccount({}, {"legacy.apps.googleusercontent.com", "secret"}), "Legacy accounts can acquire a binding");
#ifdef _WIN32
    const auto settingsFile = temporary.filePath("app-credentials.ini");
    {
        QSettings settings(settingsFile, QSettings::IniFormat);
        settings.setValue("twitch/access_token", "untouched-protected-fixture");
        settings.setValue("youtube/refresh_token", "untouched-youtube-fixture");
        settings.setValue("youtube/client_id", "old.apps.googleusercontent.com");
        settings.setValue("youtube/client_secret", PulseAppCredentials::protect("old-secret"));
    }
    check(local(settingsFile).clientSecret == "old-secret", "Read legacy DPAPI override");
    const Registration custom{"mine.apps.googleusercontent.com", "private-synthetic-secret"};
    check(saveLocal(custom, settingsFile), "Save a complete protected custom pair");
    check(local(settingsFile).clientId == custom.clientId && local(settingsFile).clientSecret == custom.clientSecret,
          "Custom pair decrypts on this Windows account");
    QFile settingsBytes(settingsFile); check(settingsBytes.open(QIODevice::ReadOnly), "Read protected fixture");
    const auto before = settingsBytes.readAll(); settingsBytes.close();
    check(!before.contains(custom.clientId.toUtf8()) && !before.contains(custom.clientSecret.toUtf8()),
          "Neither custom field is stored as plaintext");
    check(!saveLocal({custom.clientId, {}}, settingsFile), "Reject partial pair before touching disk");
    check(settingsBytes.open(QIODevice::ReadOnly) && settingsBytes.readAll() == before, "Invalid save preserves the file");
    settingsBytes.close();
    check(saveLocal({}, settingsFile), "Clear custom pair to restore bundled lookup");
    check(local(settingsFile).clientId.isEmpty() && local(settingsFile).clientSecret.isEmpty(), "No legacy override survives clearing");
    {
        QSettings settings(settingsFile, QSettings::IniFormat);
        check(settings.value("twitch/access_token") == "untouched-protected-fixture" &&
              settings.value("youtube/refresh_token") == "untouched-youtube-fixture", "Registration storage preserves other credentials");
    }
    check(!saveLocal(custom, temporary.path()), "Storage failure is reported");
#endif
    const auto fileName = temporary.filePath("desktop.json");
    auto write = [&](const QByteArray &bytes) {
        QFile file(fileName); check(file.open(QIODevice::WriteOnly | QIODevice::Truncate), "Write fixture");
        check(file.write(bytes) == bytes.size(), "Fixture complete");
    };
    write(R"({"installed":{"client_id":"fixture.apps.googleusercontent.com","client_secret":"fixture-secret"}})");
    const auto defaults = bundled(fileName);
    check(forEditor(defaults, defaults).clientId.isEmpty() && forEditor(defaults, defaults).clientSecret.isEmpty(),
          "Publisher registration is never exposed by the editor, including legacy local copies");
    check(forEditor({"custom.apps.googleusercontent.com", "secret"}, defaults).clientSecret == "secret",
          "Only the user's own override can populate the editor");
    check(defaults.clientId == "fixture.apps.googleusercontent.com", "Parse desktop client");
    check(resolve({}, defaults).clientSecret == "fixture-secret", "Fresh install fallback");
    check(resolve({{}, "orphan-secret"}, defaults).clientSecret == "fixture-secret", "Do not mix orphan secret");
    const Registration local{"legacy.apps.googleusercontent.com", "legacy-secret"};
    check(resolve(local, defaults).clientId == local.clientId && resolve(local, defaults).clientSecret == local.clientSecret,
          "Existing registrations unchanged");
    check(resolve({local.clientId, {}}, defaults).clientSecret.isEmpty(), "Never mix different registrations");
    check(resolve({defaults.clientId, {}}, defaults).clientSecret == defaults.clientSecret, "Repair same client missing secret");
    check(resolve(local, {}).clientSecret == local.clientSecret, "Existing configuration works without bundle");
    write(R"({"installed":{"client_id":"fixture.apps.googleusercontent.com"}})");
    const auto publicClient = bundled(fileName);
    check(publicClient.clientId == "fixture.apps.googleusercontent.com" && publicClient.clientSecret.isEmpty(),
          "Public desktop client ID works without a bundled secret");
    check(resolve(local, publicClient).clientSecret == local.clientSecret, "Existing registration is preserved with public default");
    check(bundled(temporary.filePath("missing.json")).clientId.isEmpty(), "Missing bundle fails safely");
    for (const auto &invalid : {QByteArray("not JSON"), QByteArray("{}"),
        QByteArray(R"({"web":{"client_id":"fixture.apps.googleusercontent.com","client_secret":"secret"}})"),
        QByteArray(R"({"installed":{"client_id":"fixture.apps.googleusercontent.com","client_secret":""}})"),
        QByteArray(R"({"installed":{"client_id":"fixture.apps.googleusercontent.com","client_secret":"secret","refresh_token":"never"}})"),
        QByteArray(R"({"installed":{"client_id":"invalid","client_secret":"secret"}})"), QByteArray(16385, 'x')}) {
        write(invalid); check(bundled(fileName).clientId.isEmpty(), "Reject malformed/private payload");
    }
    std::puts("PASS: fresh install, legacy override, matching-client repair, no mixed credentials, malformed/missing/oversize bundle");
}
