#include "../../engine/obs-studio/shared/qt/PulseBroadcastFlow.hpp"
#include <QCoreApplication>
#include <QFile>
#include <QXmlStreamReader>
#include <cstdio>
#include <cstdlib>

static void check(bool ok, const char *message)
{
	if (!ok) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}

int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	using PulseBroadcastFlow::usesYouTube;
	for (const auto *primary : {"Twitch", "Kick", "Custom", ""})
		for (const auto *account : {"YouTube - RTMP", "YouTube - RTMPS", "YouTube - HLS", "Twitch", ""})
			check(!usesYouTube(primary, account, true), "Non-YouTube primary ignores stale YouTube broadcast state");
	for (const auto *primary : {"YouTube - RTMP", "YouTube - RTMPS", "YouTube - HLS"}) {
		for (const auto *account : {"YouTube - RTMP", "YouTube - RTMPS", "YouTube - HLS"}) {
			check(usesYouTube(primary, account, true), "Native YouTube variants retain broadcast setup");
			check(!usesYouTube(primary, account, false), "Account without broadcast capability cannot run setup");
		}
		check(!usesYouTube(primary, "", true), "Missing account cannot open broadcast setup");
		check(!usesYouTube(primary, "Twitch", true), "Mismatched account cannot configure YouTube");
	}
	check(!usesYouTube("YouTube - fake", "YouTube - RTMPS", true), "Unknown service does not inherit YouTube controls");
	check(argc == 2, "UI resource path required");
	QFile file(QString::fromLocal8Bit(argv[1]));
	check(file.open(QIODevice::ReadOnly), "Read real shipped UI resource");
	QXmlStreamReader xml(&file);
	int depth = 0, eventDepth = -1, initialEvents = 0;
	bool found = false;
	while (!xml.atEnd()) {
		xml.readNext();
		if (xml.isStartElement()) {
			++depth;
			if (xml.name() == u"widget" && xml.attributes().value("name") == u"scrollAreaWidgetContents") {
				eventDepth = depth; found = true;
			} else if (eventDepth >= 0 && xml.name() == u"widget") {
				++initialEvents;
			}
		} else if (xml.isEndElement()) {
			if (depth == eventDepth) eventDepth = -1;
			--depth;
		}
	}
	check(!xml.hasError() && found, "Event list resource is valid XML");
	check(initialEvents == 0, "No fake broadcasts are visible before authentication or on initialization failure");
	std::puts("PASS: stale-account isolation, native YouTube setup preserved, missing-account guards, empty initial event list");
}
