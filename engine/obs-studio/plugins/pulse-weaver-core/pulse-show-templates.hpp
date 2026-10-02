#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QRectF>
#include <QStringList>
#include <vector>

namespace PulseShow {
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
inline const Theme *theme(const QString &id)
{
	for (const auto &value : catalogue()) {
		if (value.id == id) {
			return &value;
		}
	}
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
	const double divider = qBound(.15, options.value("divider").toDouble(look.divider), .65);
	const bool mainCamera = look.main == "presenter" || look.main == "activity" || look.main == "detail";
	std::vector<Panel> result{{look.main, {0, 0, 1, 1}, mainCamera, false}};
	if (!support || look.support.isEmpty()) {
		// Full-canvas Looks still honour the user's fit/crop choice.
	} else if (look.layout == "label" || look.layout == "footer") {
		result.push_back({look.support, {0, look.layout == "footer" ? .80 : .03, 1, .17}, false, false});
	} else if (portrait) {
		const bool presenterMain = look.main == "presenter";
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
