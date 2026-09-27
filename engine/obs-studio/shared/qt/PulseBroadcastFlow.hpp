#pragma once

#include <string_view>

namespace PulseBroadcastFlow {
inline bool isYouTube(std::string_view service)
{
	return service == "YouTube - RTMP" || service == "YouTube - RTMPS" || service == "YouTube - HLS";
}

// An imported account is not an output selection. Only the native primary
// YouTube output participates in OBS's broadcast-setup lifecycle. Pulse Weaver's
// secondary YouTube outputs prepare their own broadcasts separately.
inline bool usesYouTube(std::string_view primaryService, std::string_view accountService, bool broadcastFlow)
{
	return broadcastFlow && isYouTube(primaryService) && isYouTube(accountService);
}
}
