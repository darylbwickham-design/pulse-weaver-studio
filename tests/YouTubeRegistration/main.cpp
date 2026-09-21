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
        check(!registration.clientId.isEmpty() && !registration.clientSecret.isEmpty(), "Packaged desktop registration");
        check(resolve({}, registration).clientId == registration.clientId, "Fresh installation uses packaged client");
        std::puts("PASS: packaged desktop registration is usable without local credentials (values not logged)");
        return 0;
    }
    QTemporaryDir temporary;
    check(temporary.isValid(), "Temporary directory");
    const auto fileName = temporary.filePath("desktop.json");
    auto write = [&](const QByteArray &bytes) {
        QFile file(fileName); check(file.open(QIODevice::WriteOnly | QIODevice::Truncate), "Write fixture");
        check(file.write(bytes) == bytes.size(), "Fixture complete");
    };
    write(R"({"installed":{"client_id":"fixture.apps.googleusercontent.com","client_secret":"fixture-secret"}})");
    const auto defaults = bundled(fileName);
    check(defaults.clientId == "fixture.apps.googleusercontent.com", "Parse desktop client");
    check(resolve({}, defaults).clientSecret == "fixture-secret", "Fresh install fallback");
    check(resolve({{}, "orphan-secret"}, defaults).clientSecret == "fixture-secret", "Do not mix orphan secret");
    const Registration local{"legacy.apps.googleusercontent.com", "legacy-secret"};
    check(resolve(local, defaults).clientId == local.clientId && resolve(local, defaults).clientSecret == local.clientSecret,
          "Existing registrations unchanged");
    check(resolve({local.clientId, {}}, defaults).clientSecret.isEmpty(), "Never mix different registrations");
    check(resolve({defaults.clientId, {}}, defaults).clientSecret == defaults.clientSecret, "Repair same client missing secret");
    check(resolve(local, {}).clientSecret == local.clientSecret, "Existing configuration works without bundle");
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
