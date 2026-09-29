#pragma once

#include <string_view>

namespace PulseYouTubeBroadcast {
enum class Cleanup { Keep, Complete, DeleteUnused, Unknown };

// Only an explicitly unused broadcast is safe to delete. A failed cleanup
// can leave an older broadcast ID around until the next stream preparation.
inline Cleanup cleanup(std::string_view status)
{
	if (status == "complete" || status == "revoked")
		return Cleanup::Keep;
	if (status == "live" || status == "liveStarting" || status == "testing" || status == "testStarting")
		return Cleanup::Complete;
	if (status == "created" || status == "ready")
		return Cleanup::DeleteUnused;
	return Cleanup::Unknown;
}
}
