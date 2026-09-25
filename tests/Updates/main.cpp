#include "../../engine/obs-studio/shared/qt/PulseGitHubUpdater.hpp"
#include <cstdio>
#include <cstdlib>
#include <QSslSocket>

using namespace PulseUpdates;
static void check(bool ok, const char *description)
{
	if (!ok) {
		std::fprintf(stderr, "FAIL: %s\n", description);
		std::exit(1);
	}
}
static QJsonObject release(const Identity &identity)
{
	const auto name = assetName(identity);
	return {{"tag_name", identity.tag}, {"draft", false}, {"prerelease", true},
		{"assets", QJsonArray{QJsonObject{{"name", name}, {"state", "uploaded"},
			{"size", 12345}, {"digest", "sha256:" + QString(64, 'a')},
			{"browser_download_url", repository + "/releases/download/" + identity.tag + "/" + name}}}}};
}
int main(int argc, char **argv)
{
	QCoreApplication application(argc, argv);
	if (argc > 1) {
		QCoreApplication::setLibraryPaths({QString::fromLocal8Bit(argv[1])});
		check(QSslSocket::supportsSsl(), "Packaged Qt plugins support HTTPS downloads");
	}
	const Identity windows{"windows-public", "v1.12.1"};
	const Identity privateWindows{"windows-private", "v1.12.1"};
	const Identity mac{"mac-arm64-preview", "mac-v0.1.0-alpha.1"};
	const Identity alpha{"windows-alpha", "v1.13.0-alpha.1"};
	const QJsonArray alphaReleases{release(alpha), release({"windows-alpha", "v1.13.0-alpha.10"}),
		release({"windows-private", "v1.12.16"})};
	check(selectRelease(alphaReleases, privateWindows)->identity.tag == "v1.12.16", "Alpha is opt-in");
	check(selectRelease(alphaReleases, privateWindows, true)->identity.tag == "v1.13.0-alpha.10", "Alpha opt-in selects numeric revision");
	check(selectRelease(alphaReleases, alpha)->identity.tag == "v1.13.0-alpha.10", "Alpha keeps receiving alpha updates");
	check(!selectRelease(QJsonArray{release({"windows-private", "v1.12.16"})}, alpha), "Return to older release requires recovery");
	check(selectRelease(QJsonArray{release({"windows-private", "v1.13.0"})}, alpha)->identity.channel == "windows-private", "Final release supersedes alpha with same base version");
	check(!selectRelease(QJsonArray{release(alpha)}, {"windows-private", "v1.13.0"}, true), "Alpha cannot downgrade final release");
	check(!selectRelease(QJsonArray{release(alpha)}, windows, true), "Legacy public installation is not cross-targeted");
	if (argc > 2) {
		QFile fixture(QString::fromLocal8Bit(argv[2]));
		check(fixture.open(QIODevice::ReadOnly), "Open GitHub metadata fixture");
		const auto actual = QJsonDocument::fromJson(fixture.readAll()).array();
		check(selectRelease(actual, {"windows-public", "v1.12.0"}).has_value(), "Actual GitHub Windows assets are discoverable");
		check(selectRelease(actual, {"mac-arm64-preview", "mac-v0.1.0-alpha.0"}).has_value(), "Actual GitHub Mac assets are discoverable");
		check(!selectRelease(actual, {"windows-private", "v1.12.1"}), "Private channel cannot fall back to public assets");
	}
	QJsonArray releases{release({"windows-public", "v1.12.2"}),
		release({"mac-arm64-preview", "mac-v0.1.0-alpha.10"}),
		release({"windows-public", "v1.12.10"}),
		release({"windows-private", "v1.12.3"}),
		release({"mac-arm64-preview", "mac-v0.1.0-alpha.2"})};
	check(selectRelease(releases, windows)->identity.tag == "v1.12.10", "Windows numeric version order and channel");
	check(selectRelease(releases, privateWindows)->identity.tag == "v1.12.3", "Private installer never selects public installer");
	check(selectRelease(releases, mac)->identity.tag == "mac-v0.1.0-alpha.10", "Mac alpha numeric order and separate channel");
	check(!selectRelease(QJsonArray{release(windows)}, windows), "Same version is not an update");
	check(!selectRelease(QJsonArray{release({"windows-public", "v1.11.99"})}, windows), "No downgrade");
	check(!selectRelease(releases, {"unknown", "v1.0.0"}), "Reject unidentified installations");
	check(!version({"windows-public", "v1.12.2-evil"}), "Reject unrecognized Windows tag");
	check(!version({"mac-arm64-preview", "mac-v0.1.0-alpha.999999999999999"}), "Reject overflowing alpha");
	check(!version({"windows-public", "v99999999999999.1.2"}), "Reject overflowing major version");
	const auto valid = release({"windows-public", "v1.12.2"});
	auto draft = valid;
	draft["draft"] = true;
	check(!selectRelease(QJsonArray{draft}, windows), "Draft release cannot install");
	for (const auto &change : QList<QPair<QString, QJsonValue>>{
		{"state", "new"}, {"digest", ""}, {"digest", "sha256:bad"}, {"size", 0}, {"size", 2147483649.0},
		{"browser_download_url", "http://github.com/untrusted.exe"},
		{"browser_download_url", "https://github.com/other/repo/releases/download/v1.12.2/setup.exe"},
		{"name", "../setup.exe"}}) {
		auto modified = valid;
		auto asset = valid["assets"].toArray().first().toObject();
		asset[change.first] = change.second;
		modified["assets"] = QJsonArray{asset};
		check(!selectRelease(QJsonArray{modified}, windows), "Reject unsafe or incomplete release asset");
	}
	check(trustedDownloadUrl(QUrl("https://release-assets.githubusercontent.com/path?token=value")), "Allow GitHub CDN");
	for (const auto &url : {"http://github.com/file", "https://github.com.evil.test/file", "file:///tmp/setup.exe",
		"https://user:password@github.com/file", "https://github.com:1234/file"})
		check(!trustedDownloadUrl(QUrl(url)), "Reject untrusted redirects");
	std::puts("PASS: update channels, semantic ordering, drafts, downgrade prevention, asset validation and HTTPS redirects");
	return 0;
}
