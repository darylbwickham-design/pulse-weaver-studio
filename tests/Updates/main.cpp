#include "../../engine/obs-studio/shared/qt/PulseGitHubUpdater.hpp"
#include <cstdio>
#include <cstdlib>
#include <QSslSocket>
#include <QFile>
#include <QJsonDocument>

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
	if (argc == 4 && QString::fromLocal8Bit(argv[1]) == "--release-catalog") {
		QFile file(QString::fromLocal8Bit(argv[2]));
		check(file.open(QIODevice::ReadOnly), "Read release catalogue fixture");
		const auto catalog = QJsonDocument::fromJson(file.readAll()).array();
		const QString expected = QString::fromLocal8Bit(argv[3]);
		check(!catalog.isEmpty(), "Release catalogue is populated");
		const auto alphaUpdate = selectRelease(catalog, {"windows-alpha", "v1.14.4-alpha.4"});
		check(alphaUpdate && alphaUpdate->identity.tag == expected, "Existing alpha discovers the exact new installer");
		const auto optedIn = selectRelease(catalog, {"windows-private", "v1.14.3"}, true);
		check(optedIn && optedIn->identity.tag == expected, "Stable with alpha opt-in discovers the new alpha");
		check(!selectRelease(catalog, {"windows-private", "v1.14.3"}), "Stable users without opt-in receive no alpha");
		check(!selectRelease(catalog, {"windows-alpha", expected}), "New alpha does not offer itself as an update");
		std::puts("PASS: live catalogue alpha update, exact installer asset/digest, stable opt-in isolation and current-version checks");
		return 0;
	}
	if (argc > 1) {
		QCoreApplication::setLibraryPaths({QString::fromLocal8Bit(argv[1])});
		check(QSslSocket::supportsSsl(), "Packaged Qt plugins support HTTPS downloads");
	}
	const Identity windows{"windows-public", "v1.12.1"};
	const Identity privateWindows{"windows-private", "v1.12.1"};
	const Identity mac{"mac-arm64-preview", "mac-v0.1.0-alpha.1"};
	const Identity alpha{"windows-alpha", "v1.13.0-alpha.1"};
	const Identity unstable{"windows-unstable", "v1.14.0-unstable.1"};
	const QJsonArray promoted{release({"windows-private", "v1.14.0"}),
		release({"windows-unstable", "v1.14.0-unstable.12"}), release({"windows-alpha", "v1.14.0-alpha.4"})};
	for (const auto &installed : {Identity{"windows-private", "v1.13.3"},
		Identity{"windows-alpha", "v1.14.0-alpha.4"}, Identity{"windows-unstable", "v1.14.0-unstable.13"}}) {
		const auto selected = selectRelease(promoted, installed, true, true);
		check(selected && selected->identity.tag == "v1.14.0" && selected->identity.channel == "windows-private",
			"1.14 final supersedes regular and preview installations even with previews enabled");
	}
	check(!selectRelease(promoted, {"windows-private", "v1.14.0"}, true, true), "Final stays current with historical previews listed");
	check(assetName({"windows-private", "v1.13.0"}) == "PulseWeaver-Setup-1.13.0.exe",
		"Promoted regular release uses its actual plain installer name");
	check(assetName({"windows-private", "v1.12.17"}) == "PulseWeaver-Setup-1.12.17-BETA.exe",
		"Older regular installers retain their published asset name");
	const QJsonArray alphaReleases{release(alpha), release({"windows-alpha", "v1.13.0-alpha.10"}),
		release({"windows-private", "v1.12.16"})};
	check(selectRelease(alphaReleases, privateWindows)->identity.tag == "v1.12.16", "Alpha is opt-in");
	check(selectRelease(alphaReleases, privateWindows, true)->identity.tag == "v1.13.0-alpha.10", "Alpha opt-in selects numeric revision");
	check(selectRelease(alphaReleases, alpha)->identity.tag == "v1.13.0-alpha.10", "Alpha keeps receiving alpha updates");
	const QJsonArray installerRepair{release({"windows-alpha", "v1.14.4-alpha.1"}),
		release({"windows-alpha", "v1.14.4-alpha.2"}), release({"windows-private", "v1.14.3"})};
	check(selectRelease(installerRepair, {"windows-alpha", "v1.14.4-alpha.1"})->identity.tag == "v1.14.4-alpha.2",
		"Alpha 1 discovers the corrected Alpha 2 installer");
	check(selectRelease(installerRepair, {"windows-private", "v1.14.3"}, true)->identity.tag == "v1.14.4-alpha.2",
		"Regular users opted into alpha discover the installer repair");
	check(!selectRelease(installerRepair, {"windows-private", "v1.14.3"}), "Regular users remain on release unless they opt into alpha");
	check(assetName({"windows-alpha", "v1.14.4-alpha.2"}) == "PulseWeaver-Setup-1.14.4-alpha.2.exe",
		"Alpha 2 requires the correctly named installer asset");
	check(!selectRelease(QJsonArray{release({"windows-private", "v1.12.16"})}, alpha), "Return to older release requires recovery");
	check(selectRelease(QJsonArray{release({"windows-private", "v1.13.0"})}, alpha)->identity.channel == "windows-private", "Final release supersedes alpha with same base version");
	check(!selectRelease(QJsonArray{release(alpha)}, {"windows-private", "v1.13.0"}, true), "Alpha cannot downgrade final release");
	check(!selectRelease(QJsonArray{release(alpha)}, windows, true), "Legacy public installation is not cross-targeted");
	const QJsonArray unstableReleases{release(unstable), release({"windows-unstable", "v1.14.0-unstable.2"})};
	check(!selectRelease(unstableReleases, alpha), "Unstable is opt-in for alpha installs");
	check(selectRelease(unstableReleases, alpha, true, true)->identity.tag == "v1.14.0-unstable.2",
		"Opted-in alpha discovers the newest unstable package");
	check(selectRelease(unstableReleases, unstable)->identity.tag == "v1.14.0-unstable.2",
		"Unstable installs receive later unstable builds");
	check(!selectRelease(QJsonArray{release(unstable)}, {"windows-private", "v1.14.0"}, true, true),
		"Unstable cannot replace a same-version stable release");
	check(!selectRelease(QJsonArray{release({"windows-unstable", "v1.14.0-unstable.0"})}, unstable),
		"Unstable revision cannot downgrade");
	const Identity preview{"windows-unstable", "v2.0.0-unstable.1", "studio2-preview"};
	const Identity previewNext{"windows-unstable", "v2.0.0-unstable.2", "studio2-preview"};
	check(assetName(previewNext) == "PulseWeaver-2-Preview-Setup-2.0.0-unstable.2.exe",
		"Isolated 2 Preview requires its dedicated installer family");
	check(maintenanceFileName(preview) == "Uninstall Pulse Weaver 2 Preview.exe" &&
		maintenanceFileName(alpha) == "Uninstall Pulse Weaver.exe",
		"Recovery resolves this product's own maintenance executable");
	const QJsonObject previewIdentity{{"schema", 1}, {"channel", preview.channel},
		{"tag", preview.tag}, {"product", preview.product}};
	check(identityFromJson(previewIdentity).product == "studio2-preview", "Parse the isolated product marker");
	auto legacyIdentity = previewIdentity;
	legacyIdentity.remove("product");
	check(identityFromJson(legacyIdentity).product.isEmpty() && version(identityFromJson(legacyIdentity)),
		"Absent product marker retains the regular installation contract");
	check(!selectRelease(QJsonArray{release(previewNext)}, identityFromJson(legacyIdentity), true, true),
		"A missing product marker cannot adopt a dedicated preview installer");
	for (const QJsonValue &marker : {QJsonValue("unknown-preview"), QJsonValue(""), QJsonValue(true), QJsonValue(QJsonValue::Null)}) {
		auto invalidIdentity = previewIdentity;
		invalidIdentity["product"] = marker;
		check(!version(identityFromJson(invalidIdentity)), "Invalid product markers cannot silently fall back to regular updates");
	}
	check(!version({"windows-alpha", "v2.0.0-alpha.1", "studio2-preview"}),
		"The isolated product only accepts its unstable channel");
	check(!selectRelease(QJsonArray{release(preview)}, preview), "Preview does not offer its current version");
	check(!selectRelease(QJsonArray{release(previewNext)}, alpha, true, true),
		"Regular Alpha with unstable enabled does not select a 2 Preview-only release");
	const QJsonArray previewCandidates{release(previewNext), release({"windows-unstable", "v2.0.0-unstable.3"}),
		release({"windows-alpha", "v2.1.0-alpha.1"}), release({"windows-private", "v3.0.0"})};
	const auto previewUpdate = selectRelease(previewCandidates, preview, true, true);
	check(previewUpdate && previewUpdate->identity.tag == previewNext.tag &&
		previewUpdate->identity.product == preview.product && previewUpdate->name == assetName(previewNext),
		"Preview ignores newer regular unstable, alpha and stable assets and keeps its product marker");
	check(!selectRelease(QJsonArray{release({"windows-unstable", "v2.0.0-unstable.3"}),
		release({"windows-private", "v3.0.0"})}, preview, true, true),
		"Preview never falls back to a generic installer if its dedicated asset is absent");
	auto pairedRelease = release(previewNext);
	const auto genericNext = release({"windows-unstable", previewNext.tag});
	pairedRelease["assets"] = QJsonArray{pairedRelease["assets"].toArray().first(), genericNext["assets"].toArray().first()};
	check(selectRelease(QJsonArray{pairedRelease}, preview)->name == assetName(previewNext),
		"Preview selects its own installer when both product assets share a tag");
	check(selectRelease(QJsonArray{pairedRelease}, {"windows-unstable", preview.tag})->name ==
		assetName({"windows-unstable", previewNext.tag}),
		"Regular unstable retains generic installer selection when both assets share a tag");
	for (const auto &change : QList<QPair<QString, QJsonValue>>{
		{"digest", ""}, {"digest", "sha256:bad"}, {"state", "new"}, {"size", 0},
		{"name", "PulseWeaver-Setup-2.0.0-unstable.2.exe"},
		{"browser_download_url", repository + "/releases/download/" + previewNext.tag + "/PulseWeaver-Setup-2.0.0-unstable.2.exe"}}) {
		auto modified = release(previewNext);
		auto asset = modified["assets"].toArray().first().toObject();
		asset[change.first] = change.second;
		modified["assets"] = QJsonArray{asset};
		check(!selectRelease(QJsonArray{modified}, preview), "Preview rejects missing, incomplete or mismatched installer identity and digest");
	}
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
	std::puts("PASS: update channels, isolated 2 Preview family, semantic ordering, drafts, downgrade prevention, asset validation and HTTPS redirects");
	return 0;
}
