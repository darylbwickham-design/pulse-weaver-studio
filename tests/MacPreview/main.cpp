#include "../../engine/obs-studio/shared/qt/PulseAppCredentials.hpp"
#include <QCoreApplication>
#include <cstdio>
#include <cstdlib>
static void check(bool ok, const char *message) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    const QString root = PulseMacPaths::root();
    check(root == QDir::homePath() + "/Library/Application Support/Pulse Weaver Mac Preview", "separate settings root");
    check(PulseMacPaths::path(nullptr) == root, "null path");
    check(PulseMacPaths::path("obs-studio/basic") == root + "/obs-studio/basic", "scene profiles remain inside preview root");
    const QString reference = PulseAppCredentials::protect("synthetic-mac-preview-secret");
    check(reference.startsWith("keychain:"), "secret stored in Keychain");
    check(!reference.contains("synthetic"), "file reference does not contain secret");
    check(PulseAppCredentials::reveal(reference) == "synthetic-mac-preview-secret", "Keychain round trip");
    check(PulseAppCredentials::reveal("plaintext-secret").isEmpty(), "plaintext is rejected");
    const QByteArray account = reference.mid(9).toUtf8();
    CFStringRef key = CFStringCreateWithCString(nullptr, account.constData(), kCFStringEncodingUTF8);
    const void *keys[] = {kSecClass, kSecAttrService, kSecAttrAccount};
    const void *values[] = {kSecClassGenericPassword, CFSTR("studio.pulseweaver.macpreview.app-credentials"), key};
    CFDictionaryRef query = CFDictionaryCreate(nullptr, keys, values, 3, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    check(SecItemDelete(query) == errSecSuccess, "synthetic Keychain item cleanup");
    CFRelease(query); CFRelease(key);
    std::puts("PASS: isolated Mac settings and Keychain secret round trip.");
}
