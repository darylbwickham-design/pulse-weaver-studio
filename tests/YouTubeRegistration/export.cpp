// Maintainer-only build helper. Run ONLY after the owner confirms this saved
// registration is a Google Desktop app, not a confidential web client.
#include "../../engine/obs-studio/shared/qt/PulseYouTubeRegistration.hpp"
#include <cstdio>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    if (argc != 4 || QString::fromLocal8Bit(argv[1]) != "--confirmed-desktop-client") return 64;
    const auto registration = PulseYouTubeRegistration::local(QString::fromLocal8Bit(argv[2]));
    const auto id = registration.clientId;
    const auto secret = registration.clientSecret;
    if (!id.endsWith(".apps.googleusercontent.com") || secret.isEmpty()) {
        std::fputs("Registration missing or cannot be decrypted; no values logged.\n", stderr); return 1;
    }
    const auto data = QJsonDocument(QJsonObject{{"installed", QJsonObject{{"client_id", id}, {"client_secret", secret}}}}).toJson();
    QFile output(QString::fromLocal8Bit(argv[3]));
    if (!output.open(QIODevice::WriteOnly | QIODevice::NewOnly) || output.write(data) != data.size()) return 2;
    std::puts("Desktop application registration exported; no user tokens read or copied.");
}
