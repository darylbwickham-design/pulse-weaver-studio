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
	int unstable = -1;
};
inline std::optional<Version> version(const Identity &identity)
{
	const bool mac = identity.channel == "mac-arm64-preview";
	const bool alphaChannel = identity.channel == "windows-alpha";
	const bool unstableChannel = identity.channel == "windows-unstable";
	if (!mac && !alphaChannel && !unstableChannel && identity.channel != "windows-public" && identity.channel != "windows-private")
		return {};
	const QRegularExpression expression(mac ? "^mac-v([0-9]+\\.[0-9]+\\.[0-9]+)-alpha\\.([0-9]+)$" :
					 alphaChannel ? "^v([0-9]+\\.[0-9]+\\.[0-9]+)-alpha\\.([0-9]+)$" :
					 unstableChannel ? "^v([0-9]+\\.[0-9]+\\.[0-9]+)-unstable\\.([0-9]+)$" :
						 "^v([0-9]+\\.[0-9]+\\.[0-9]+)$");
	const auto match = expression.match(identity.tag);
	if (!match.hasMatch())
		return {};
	const auto base = QVersionNumber::fromString(match.captured(1));
	bool ok = true;
	const int revision = (mac || alphaChannel || unstableChannel) ? match.captured(2).toInt(&ok) : -1;
	if (!ok || base.segmentCount() != 3)
		return {};
	return Version{base, alphaChannel || mac ? revision : -1, unstableChannel ? revision : -1};
}
inline bool newer(const Version &left, const Version &right)
{
	const int comparison = QVersionNumber::compare(left.base, right.base);
	if (comparison != 0) return comparison > 0;
	auto rank = [](const Version &v) { return v.alpha >= 0 ? 0 : v.unstable >= 0 ? 1 : 2; };
	if (rank(left) != rank(right)) return rank(left) > rank(right);
	return left.alpha > right.alpha || left.unstable > right.unstable;
}
inline QString assetName(const Identity &identity)
{
	if (identity.channel == "mac-arm64-preview")
		return "PulseWeaver-Mac-" + identity.tag + "-AppleSilicon.dmg";
	if (identity.channel == "windows-public")
		return "PulseWeaver-Public-Dist-" + identity.tag.mid(1) + "-Setup.exe";
	if (identity.channel == "windows-alpha" || identity.channel == "windows-unstable")
		return "PulseWeaver-Setup-" + identity.tag.mid(1) + ".exe";
	if (identity.channel == "windows-private" &&
		QVersionNumber::compare(QVersionNumber::fromString(identity.tag.mid(1)), QVersionNumber(1, 13, 0)) >= 0)
		return "PulseWeaver-Setup-" + identity.tag.mid(1) + ".exe";
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
inline std::optional<Release> selectRelease(const QJsonArray &releases, const Identity &installed,
					   bool includeAlpha = false, bool includeUnstable = false)
{
	auto bestVersion = version(installed);
	if (!bestVersion)
		return {};
	std::optional<Release> best;
	for (const auto &item : releases) {
		const auto release = item.toObject();
		if (!release.contains("draft") || release.value("draft").toBool(true))
			continue;
		const QString tag = release.value("tag_name").toString();
		QString channel = installed.channel;
		if (installed.channel == "mac-arm64-preview") {
			channel = installed.channel;
		} else if (tag.contains("-unstable.")) {
			if (installed.channel == "windows-public" ||
				(installed.channel != "windows-unstable" && !includeUnstable)) continue;
			channel = "windows-unstable";
		} else if (tag.contains("-alpha.")) {
			if (installed.channel == "windows-public" ||
				(installed.channel != "windows-alpha" && !includeAlpha)) continue;
			channel = "windows-alpha";
		} else if (installed.channel == "windows-alpha" || installed.channel == "windows-unstable") {
			channel = "windows-private";
		}
		Identity candidate{channel, tag};
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
