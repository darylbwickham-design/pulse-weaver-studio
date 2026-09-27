#pragma once
#include <string>
#include <string_view>

namespace PulseTwitch {
inline std::string normalizeMode(std::string_view mode)
{
	if (mode == "off") return "off";
	if (mode == "dual" || mode == "vertical") return "dual";
	return "horizontal";
}
struct Route {
	bool enhanced;
	std::string extraCanvas;
};
inline bool matches(std::string_view mode, bool landscape, bool portrait)
{
	return landscape && (mode == "dual" ? portrait : mode == "horizontal" && !portrait);
}
inline Route route(std::string_view mode, bool enhancedPreference, std::string_view portrait)
{
	return {mode == "dual" || enhancedPreference, mode == "dual" ? std::string(portrait) : std::string{}};
}

// Start acceptance and a live output are distinct. OBS starts connections asynchronously.
class StartState {
public:
	enum class Phase { Idle, Preparing, Connecting, Live, Failed };
	unsigned begin() { phase = Phase::Preparing; return ++generation; }
	bool prepared(unsigned attempt, bool ok) {
		if (attempt != generation || phase != Phase::Preparing) return false;
		phase = ok ? Phase::Connecting : Phase::Failed;
		return ok;
	}
	void started() { phase = Phase::Live; }
	void failed() { phase = Phase::Failed; }
	void stop() { ++generation; phase = Phase::Idle; }
	bool pending() const { return phase == Phase::Preparing || phase == Phase::Connecting; }
	bool connecting() const { return phase == Phase::Connecting; }
	unsigned attempt() const { return generation; }
private:
	unsigned generation = 0;
	Phase phase = Phase::Idle;
};
}
