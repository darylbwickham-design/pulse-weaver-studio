#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QMap>
#include <QRectF>
#include <QStringList>
#include <QUrl>
#include <vector>
#include <algorithm>

namespace PulseShow {
// Generated Looks control the complete shared scene. A layer hidden in one
// Look can retain the index it had in another, colliding with a visible layer.
// Give hidden layers distinct trailing indices before applying the whole stack.
// Visible order, geometry, filters and audio are left exactly as requested.
inline QJsonArray orderedSetupTargets(const QJsonArray &targets)
{
	QMap<QString, int> nextHidden;
	for (const auto &value : targets) {
		const auto item=value.toObject(), transform=item.value("transform").toObject();
		if (transform.value("visible").toBool()) {
			const auto container=item.value("container").toString();
			nextHidden[container]=qMax(nextHidden.value(container),transform.value("order").toInt()+1);
		}
	}
	std::vector<QJsonObject> ordered;
	for (const auto &value : targets) {
		auto item=value.toObject(), transform=item.value("transform").toObject();
		if (!transform.value("visible").toBool()) {
			transform.insert("order",nextHidden[item.value("container").toString()]++);
			item.insert("transform",transform);
		}
		ordered.push_back(item);
	}
	std::stable_sort(ordered.begin(),ordered.end(),[](const auto &a,const auto &b) {
		const auto ac=a.value("container").toString(),bc=b.value("container").toString();
		return ac==bc ? a.value("transform").toObject().value("order").toInt()<b.value("transform").toObject().value("order").toInt() : ac<bc;
	});
	QJsonArray result; for (const auto &item : ordered) result.append(item); return result;
}
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
	for (const QString &role : {QString("graphic"), QString("graphicPortrait"),
		QString("graphicFullCanvas"), QString("graphicPortraitFullCanvas")})
		if (artwork.contains(role)) roles.insert(role, artwork.value(role));
	return roles;
}
struct Look {
	QString id, name, description, main, support, layout;
	bool optionalPresenter = false;
	double divider = .30;
	QString corner = "top-left";
	QString third;
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
// The task-first builder has a separate catalogue. Existing saved recipes and
// the detailed builder keep their original IDs, selections and framing.
inline const std::vector<Theme> &guidedCatalogue()
{
	static const std::vector<Theme> groups{
		{"guided-main", "Main", {
			{"content-camera", "Content and camera", "Keep the main content visible with your camera alongside it", "feedB", "feedA", "guided-pip"},
			{"content-detail", "Content and detail/guest", "Give your content and separate detail or guest view equal space", "feedB", "feedC", "guided-equal"},
			{"content", "Content only", "Show the complete game, screen or activity without a camera", "feedB", {}, "guided-full"},
			{"camera", "Camera only", "Give your main camera the whole canvas", "feedA", {}, "guided-full"},
			{"detail", "Detail only", "Focus on your separate close-up or second view", "feedC", {}, "guided-full"},
			{"two-cameras", "Two cameras", "Give the main camera and detail view equal space", "feedA", "feedC", "guided-equal"},
			{"three-up", "Content and two cameras", "Show the main content, main camera and detail view together", "feedB", "feedA", "guided-three", false, .30, "top-left", "feedC"}}},
		{"guided-starting", "Starting", {{"card", "Starting screen", "A readable local starting message", "graphic", {}, "guided-full"}}},
		{"guided-break", "Break", {{"card", "Break screen", "A readable away message without showing your cameras", "graphic", {}, "guided-full"}}},
		{"guided-ending", "Ending", {{"card", "Ending screen", "A readable local closing message", "graphic", {}, "guided-full"}}}};
	return groups;
}
// Selection-first setup binds different content kinds once, then shares them
// between Looks. Cosmetic framing choices do not create duplicate presets.
inline const std::vector<Theme> &selectCatalogue()
{
	static const std::vector<Theme> groups{
		{"select-main", "Main", {
			{"camera", "Camera", "Talk directly to viewers", "presenter", {}, "select-full"},
			{"camera-activity", "Face + activity", "Your face leads; keep a work area or second camera visible", "presenter", "activity", "select-pip"},
			{"activity-camera", "Activity + face", "The work area, instrument, pet or demonstration leads", "activity", "presenter", "select-pip", true},
			{"screen-camera", "Screen share", "Show a window or display, with an optional face camera", "screen", "presenter", "select-pip", true},
			{"screen-cameras", "Screen + two cameras", "Show the screen, your face and an activity together", "screen", "presenter", "select-three", false, .30, "top-left", "activity"},
			{"game-camera", "Gameplay", "Show your game, with an optional face camera", "game", "presenter", "select-pip", true},
			{"game-cameras", "Game + two cameras", "Keep the game, your face and an activity visible", "game", "presenter", "select-three", false, .30, "top-left", "activity"}}},
		{"select-celebration", "Celebration", {{"announcement", "Celebration / announcement", "A message or your own artwork, with an optional face camera", "graphic", "presenter", "select-pip", true}}}};
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
	for (const auto &value : guidedCatalogue()) if (value.id == id) return &value;
	for (const auto &value : selectCatalogue()) if (value.id == id) return &value;
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
			for (const auto &role : {value->main, value->support, value->third}) {
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
	if (look.layout.startsWith("select-")) {
		std::vector<Panel> result{{look.main, {0, 0, 1, 1}, false, false}};
		if (support && !look.support.isEmpty()) {
			const double size = qBound(.18, options.value("cameraSize").toDouble(.27), .40);
			const double top = qBound(.22, options.value("portraitCamera").toDouble(.32), .45);
			const bool three = look.layout == "select-three";
			if (portrait) {
				result[0].rect = {0, top, 1, three ? 1 - top * 2 : 1 - top};
				result.push_back({look.support, {0, 0, 1, top}, false, false});
				if (three) result.push_back({look.third, {0, 1 - top, 1, top}, false, false});
			} else if (three) {
				result[0].rect = {0, 0, .72, 1};
				result.push_back({look.support, {.73, 0, .27, .5}, false, false});
				result.push_back({look.third, {.73, .5, .27, .5}, false, false});
			} else {
				const auto corner = options.value("corner").toString("bottom-right");
				const double height = size; // 16:9 inset on a 16:9 canvas.
				result.push_back({look.support, {corner.endsWith("right") ? .97 - size : .03,
					corner.startsWith("bottom") ? .97 - height : .03, size, height}, false, false});
			}
		}
		for (auto &panel : result) {
			// Screens and games always fit; camera cropping is an explicit choice.
			if (panel.role == "presenter" || panel.role == "activity")
				panel.fill = options.value(panel.role + "Fill").toBool();
		}
		return result;
	}
	if (look.layout.startsWith("guided-")) {
		// These are panel bounds, not promises that a fitted source fills them.
		// In particular a wide content feed remains letterboxed in portrait.
		std::vector<Panel> result{{look.main, {0, 0, 1, 1}, false, false}};
		if (support && !look.support.isEmpty()) {
			if (look.layout == "guided-pip") {
				if (portrait) {
					result[0].rect = {0, .33, 1, .67};
					result.push_back({look.support, {0, 0, 1, .33}, false, false});
				} else {
					result.push_back({look.support, {.70, .66, .265, .30}, false, false});
				}
			} else if (look.layout == "guided-equal") {
				result[0].rect = portrait ? QRectF(0, 0, 1, .5) : QRectF(0, 0, .5, 1);
				result.push_back({look.support, portrait ? QRectF(0, .5, 1, .5) : QRectF(.5, 0, .5, 1), false, false});
			} else if (look.layout == "guided-three") {
				result[0].rect = portrait ? QRectF(0, 0, 1, .42) : QRectF(0, 0, .70, 1);
				result.push_back({look.support, portrait ? QRectF(0, .42, 1, .29) : QRectF(.70, 0, .30, .5), false, false});
				if (!look.third.isEmpty())
					result.push_back({look.third, portrait ? QRectF(0, .71, 1, .29) : QRectF(.70, .5, .30, .5), false, false});
			}
		}
		for (auto &panel : result) {
			const QJsonArray rect = options.value(panel.role + "Rect").toArray();
			if (rect.size() == 4) {
				const double x = qBound(0., rect[0].toDouble(), .99), y = qBound(0., rect[1].toDouble(), .99);
				panel.rect = {x, y, qBound(.01, rect[2].toDouble(), 1 - x), qBound(.01, rect[3].toDouble(), 1 - y)};
			}
			// Content always fits. Cropping camera/detail requires an explicit choice.
			if (panel.role != "feedB" && options.contains(panel.role + "Fill"))
				panel.fill = options.value(panel.role + "Fill").toBool();
		}
		return result;
	}
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
