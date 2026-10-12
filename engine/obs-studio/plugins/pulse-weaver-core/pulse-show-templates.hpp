#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QRectF>
#include <QStringList>
#include <QUrl>
#include <vector>

namespace PulseShow {
inline QString browserLinkProblem(const QString &value)
{
	const QUrl url(value.trimmed());
	if (!url.isValid() || url.host().isEmpty() || (url.scheme() != "https" && url.scheme() != "http"))
		return "Enter a complete HTTP or HTTPS browser link.";
	if (url.host().compare("api.lumiastream.com", Qt::CaseInsensitive) == 0 &&
	    url.path().startsWith("/shared/download/", Qt::CaseInsensitive))
		return "This is a Lumia template import link. Import it in Lumia, then paste the overlay's chain / copy-link browser URL.";
	return {};
}
inline QJsonObject rolesForLook(const QJsonObject &stage, const QString &look)
{
	QJsonObject roles = stage.value("roles").toObject();
	const auto artwork = stage.value("lookArtwork").toObject().value(look).toObject();
	for (const QString &role : {QString("graphic"), QString("graphicPortrait")})
		if (artwork.contains(role)) roles.insert(role, artwork.value(role));
	return roles;
}
struct Look {
	QString id, name, description, main, support, layout;
	bool optionalPresenter = false;
	double divider = .30;
	QString corner = "top-left";
};
struct Theme {
	QString id, name;
	std::vector<Look> looks;
};
inline const std::vector<Theme> &catalogue()
{
	static const std::vector<Theme> themes{
		{"starting",
		 "Starting",
		 {{"countdown", "Countdown focus", "Countdown leads; a small dimmed camera sits on the left", "graphic",
		   "presenter", "starting-small", true, .20},
		  {"countdown-camera", "Camera teaser", "The same grey camera grows into a left clipped-corner panel",
		   "graphic", "presenter", "starting", true, .30},
		  {"introduction", "Ready to begin", "Camera grows again; countdown slides beside it", "graphic",
		   "presenter", "starting-wide", true, .45}}},
		{"viewer",
		 "Viewer focus",
		 {{"chatting", "Just chatting", "Presenter fills the canvas", "presenter", {}, "full"},
		  {"screen-reaction", "Screen reaction", "Screen takes focus; presenter sits in a small left corner",
		   "screen", "presenter", "corner", true, .30},
		  {"community", "Presenter reaction", "Presenter grows on the left; screen becomes the supporting view",
		   "presenter", "screen", "split", false, .60}}},
		{"gameplay",
		 "Gameplay focus",
		 {{"game", "Gameplay focus", "Game leads; optional small presenter in the left corner", "game",
		   "presenter", "corner", true, .20},
		  {"game-camera", "Balanced gameplay", "Game slides right while presenter grows into a left column",
		   "game", "presenter", "rail", false, .35},
		  {"game-reaction", "Presenter reaction",
		   "Presenter expands across the left; game becomes the supporting view", "presenter", "game", "split",
		   false, .60}}},
		{"intermission",
		 "Intermission",
		 {{"brb", "BRB focus", "Holding message beside an optional small activity view", "graphic", "activity",
		   "holding", true, .20},
		  {"return", "Return countdown", "Activity grows into the left column beside the countdown", "graphic",
		   "activity", "holding-wide", true, .35},
		  {"activity-continues", "Activity continues", "Keep the activity visible with a presenter-away label",
		   "activity", "graphic", "label"}}},
		{"craft",
		 "Craft focus",
		 {{"work", "Work focus", "Work area leads; optional small presenter in the left corner", "activity",
		   "presenter", "corner", true, .25},
		  {"work-camera", "Balanced work", "Work area slides right while presenter expands into a left column",
		   "activity", "presenter", "rail", false, .40},
		  {"presenter-work", "Presenter explains",
		   "Presenter takes the large left panel; work area moves into the right panel", "presenter",
		   "activity", "split", false, .60}}},
		{"wrap",
		 "Wrap-up",
		 {{"goodbye", "Presenter goodbye", "Presenter fills the canvas above the closing message", "presenter",
		   "graphic", "footer"},
		  {"credits", "Credits focus", "Presenter shrinks into a left column while credits take the right",
		   "graphic", "presenter", "holding-wide", false, .40},
		  {"final", "Final card", "Final message takes focus; presenter moves to a small lower-left inset",
		   "graphic", "presenter", "corner", true, .20, "bottom-left"}}}};
	return themes;
}
// Feed roles belong to a Stage and may use cameras, media or captures.
inline const std::vector<Theme> &setupCatalogue()
{
	static const std::vector<Theme> groups{
		{"setup-starting", "Starting", {
			{"countdown", "Countdown", "A countdown with an optional supporting feed", "graphic", "feedA", "corner", true},
			{"welcome", "Welcome", "Welcome viewers with an optional supporting feed", "graphic", "feedA", "rail", true},
			{"ready", "Getting ready", "A main feed with a getting-ready message", "feedA", "graphic", "footer"}}},
		{"setup-chat", "Chat", {
			{"talk", "Just chatting", "Focus on one feed", "feedA", {}, "full"},
			{"reaction", "Content reaction", "Content with an optional reaction feed", "feedB", "feedA", "corner", true},
			{"explain", "Explain or showcase", "Lead with feed A alongside supporting content", "feedA", "feedB", "split"}}},
		{"setup-game", "Game", {
			{"play", "Gameplay", "Game or content feed with an optional camera", "feedB", "feedA", "corner", true},
			{"commentary", "Commentary", "Feed A alongside the game or content", "feedA", "feedB", "split"},
			{"secondary", "Secondary view", "An alternate capture or camera with optional support", "feedC", "feedA", "corner", true}}},
		{"setup-projects", "Projects", {
			{"activity", "Activity", "Work area, pet, instrument, making or other activity", "feedB", "feedA", "corner", true},
			{"explain", "Explain the project", "Feed A leads alongside the activity", "feedA", "feedB", "split"},
			{"detail", "Detail view", "Focus on a close-up or alternate activity feed", "feedC", "feedA", "corner", true}}},
		{"setup-celebration", "Celebration", {
			{"reaction", "Celebration reaction", "A reaction feed with your optional celebration overlay", "feedA", {}, "full"},
			{"announcement", "Announcement", "A message with an optional supporting feed", "graphic", "feedA", "holding", true}}},
		{"setup-intermission", "Intermission", {
			{"brb", "Be right back", "An away message with optional activity", "graphic", "feedB", "holding", true},
			{"activity", "Activity continues", "Keep an activity visible while away", "feedB", "graphic", "label"},
			{"ending", "Ending", "A closing message with an optional supporting feed", "graphic", "feedA", "corner", true}}}};
	return groups;
}
inline const Theme *theme(const QString &id)
{
	for (const auto &value : catalogue()) {
		if (value.id == id) {
			return &value;
		}
	}
	for (const auto &value : setupCatalogue()) if (value.id == id) return &value;
	return nullptr;
}
inline const Look *look(const Theme &theme, const QString &id)
{
	for (const auto &value : theme.looks) {
		if (value.id == id) {
			return &value;
		}
	}
	return nullptr;
}
inline QString roleName(const QString &role)
{
	if (role == "feedA") return "Feed A";
	if (role == "feedB") return "Feed B";
	if (role == "feedC") return "Feed C";
	if (role == "presenter") {
		return "Presenter camera";
	}
	if (role == "screen") {
		return "Screen capture";
	}
	if (role == "game") {
		return "Game capture";
	}
	if (role == "activity") {
		return "Work / activity camera";
	}
	if (role == "detail") {
		return "Detail camera";
	}
	if (role == "chat") {
		return "Chat overlay";
	}
	return "Title / countdown graphic";
}
inline bool graphic(const QString &role)
{
	return role == "graphic" || role == "chat" || role.startsWith("overlay");
}
inline QString graphicVariant(const QString &lookId)
{
	if (lookId.startsWith("setup-")) return lookId;
	const QString theme = lookId.section('/', 0, 0);
	return theme + "/" +
	       (theme == "starting"               ? "countdown"
		: lookId == "intermission/return" ? "return"
						  : "message");
}
inline QStringList roles(const Theme &theme, const QJsonArray &selected)
{
	QStringList result;
	for (const auto &id : selected) {
		if (const auto *value = look(theme, id.toString())) {
			for (const auto &role : {value->main, value->support}) {
				if (!role.isEmpty() && !result.contains(role)) {
					result << role;
				}
			}
		}
	}
	return result;
}
struct Panel {
	QString role;
	QRectF rect;
	bool fill = false, treated = false;
};
inline std::vector<Panel> layout(const Look &look, bool portrait, bool support, const QJsonObject &options = {})
{
	const QString composition = options.value("composition").toString();
	if (!composition.isEmpty() && composition != look.layout &&
	    (composition == "corner" || composition == "rail" || composition == "split")) {
		Look adjusted = look; adjusted.layout = composition;
		QJsonObject framing = options; framing.remove("composition");
		return layout(adjusted, portrait, support, framing);
	}
	const double divider = qBound(.15, options.value("divider").toDouble(look.divider), .65);
	const bool mainCamera = look.main == "presenter" || look.main == "activity" || look.main == "detail";
	std::vector<Panel> result{{look.main, {0, 0, 1, 1}, mainCamera, false}};
	if (!support || look.support.isEmpty()) {
		// Full-canvas Looks still honour the user's fit/crop choice.
	} else if (look.layout == "label" || look.layout == "footer") {
		result.push_back({look.support, {0, look.layout == "footer" ? .80 : .03, 1, .17}, false, false});
	} else if (portrait) {
		const bool presenterMain = look.main == "presenter" || look.main == "feedA";
		const double top = divider;
		result[0].rect = presenterMain ? QRectF(0, 0, 1, top) : QRectF(0, top, 1, 1 - top);
		result.push_back({look.support, presenterMain ? QRectF(0, top, 1, 1 - top) : QRectF(0, 0, 1, top),
				  look.support == "presenter" || look.support == "activity",
				  look.layout.startsWith("starting")});
	} else if (look.layout.startsWith("starting")) {
		const double w = look.layout == "starting-small" ? .24 : look.layout == "starting-wide" ? .42 : .36;
		const double h = w * (.84 / .36);
		result[0].rect = {w + .08, 0, .92 - w, 1};
		result.push_back({look.support, {.04, (1 - h) / 2, w, h}, true, true});
	} else if (look.layout == "rail" || look.layout.startsWith("holding")) {
		const double w = look.layout == "holding" ? .24 : look.main == "activity" ? .42 : .36;
		result[0].rect = {w, 0, 1 - w, 1};
		result.push_back({look.support, {0, 0, w, 1}, !graphic(look.support), false});
	} else if (look.layout == "split") {
		result[0].rect = {0, 0, .65, 1};
		result.push_back({look.support,
				  {.65, 0, .35, 1},
				  !graphic(look.support) && look.support != "game" && look.support != "screen",
				  false});
	} else {
		const QString corner = options.value("corner").toString(look.corner);
		const bool reaction = look.layout == "reaction";
		const double w = reaction ? .33 : .24, h = reaction ? .40 : .29;
		const double x = corner.endsWith("right") ? .97 - w : .03;
		const double y = corner.startsWith("bottom") ? .97 - h : .03;
		result.push_back({look.support, {x, y, w, h}, !graphic(look.support), false});
	}
	for (auto &panel : result) {
		const QJsonArray rect = options.value(panel.role + "Rect").toArray();
		if (rect.size() == 4) {
			const double x = qBound(0., rect[0].toDouble(), .99), y = qBound(0., rect[1].toDouble(), .99);
			panel.rect = {x, y, qBound(.01, rect[2].toDouble(), 1 - x),
				      qBound(.01, rect[3].toDouble(), 1 - y)};
		}
		if (options.contains(panel.role + "Fill")) {
			panel.fill = options.value(panel.role + "Fill").toBool();
		}
	}
	return result;
}
} // namespace PulseShow
