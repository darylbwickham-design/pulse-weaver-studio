#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>
#include <QUrl>
#include <QVersionNumber>
#include <optional>

namespace PulseUpdates {
inline const QString repository = "https://github.com/darylbwickham-design/pulse-weaver-studio";
inline const QString api = "https://api.github.com/repos/darylbwickham-design/pulse-weaver-studio/releases";

struct Identity {
	QString channel;
	QString tag;
};
struct Version {
	QVersionNumber base;
	int alpha = -1;
};
inline std::optional<Version> version(const Identity &identity)
{
	const bool mac = identity.channel == "mac-arm64-preview";
	if (!mac && identity.channel != "windows-public" && identity.channel != "windows-private")
		return {};
	const QRegularExpression expression(mac ? "^mac-v([0-9]+\\.[0-9]+\\.[0-9]+)-alpha\\.([0-9]+)$" :
						 "^v([0-9]+\\.[0-9]+\\.[0-9]+)$");
	const auto match = expression.match(identity.tag);
	if (!match.hasMatch())
		return {};
	const auto base = QVersionNumber::fromString(match.captured(1));
	bool ok = true;
	const int alpha = mac ? match.captured(2).toInt(&ok) : -1;
	if (!ok || base.segmentCount() != 3)
		return {};
	return Version{base, alpha};
}
inline bool newer(const Version &left, const Version &right)
{
	const int comparison = QVersionNumber::compare(left.base, right.base);
	return comparison > 0 || (comparison == 0 && left.alpha > right.alpha);
}
inline QString assetName(const Identity &identity)
{
	if (identity.channel == "mac-arm64-preview")
		return "PulseWeaver-Mac-" + identity.tag + "-AppleSilicon.dmg";
	if (identity.channel == "windows-public")
		return "PulseWeaver-Public-Dist-" + identity.tag.mid(1) + "-Setup.exe";
	return "PulseWeaver-Setup-" + identity.tag.mid(1) + "-BETA.exe";
}
struct Release {
	Identity identity;
	QString name;
	QUrl download;
	QByteArray sha256;
	qint64 size = 0;
};
// Only exact assets from this repository and this installation's channel qualify.
inline std::optional<Release> selectRelease(const QJsonArray &releases, const Identity &installed)
{
	auto bestVersion = version(installed);
	if (!bestVersion)
		return {};
	std::optional<Release> best;
	for (const auto &item : releases) {
		const auto release = item.toObject();
		if (!release.contains("draft") || release.value("draft").toBool(true))
			continue;
		Identity candidate{installed.channel, release.value("tag_name").toString()};
		const auto candidateVersion = version(candidate);
		if (!candidateVersion || !newer(*candidateVersion, *bestVersion))
			continue;
		const QString name = assetName(candidate);
		const QString expectedUrl = repository + "/releases/download/" + candidate.tag + "/" + name;
		for (const auto &value : release.value("assets").toArray()) {
			const auto asset = value.toObject();
			const QString digest = asset.value("digest").toString();
			const auto size = asset.value("size").toInteger();
			if (asset.value("name").toString() != name || asset.value("state").toString() != "uploaded" ||
			    asset.value("browser_download_url").toString() != expectedUrl || size <= 0 ||
			    size > 2LL * 1024 * 1024 * 1024 ||
			    !QRegularExpression("^sha256:[a-fA-F0-9]{64}$").match(digest).hasMatch())
				continue;
			best = Release{candidate, name, QUrl(expectedUrl), QByteArray::fromHex(digest.mid(7).toLatin1()), size};
			bestVersion = candidateVersion;
		}
	}
	return best;
}
inline bool trustedDownloadUrl(const QUrl &url)
{
	return url.scheme() == "https" && url.userInfo().isEmpty() && url.port(443) == 443 &&
	       (url.host() == "github.com" || url.host() == "release-assets.githubusercontent.com" ||
		url.host() == "objects.githubusercontent.com");
}
} // namespace PulseUpdates
