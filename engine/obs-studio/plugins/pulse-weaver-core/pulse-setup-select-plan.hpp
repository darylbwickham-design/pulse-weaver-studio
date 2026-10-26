#pragma once

#include "pulse-setup-plan.hpp"
#include <QMap>

namespace PulseSelectPlan {
struct Options {
	QString name = "My Show", style = "navy";
	QSet<QString> selected;
	QMap<QString, QString> sources;
	QJsonObject newSources, newStingers, transitions, artwork;
	QJsonObject framing{{"cameraSize", .27}, {"portraitCamera", .32}, {"corner", "bottom-right"}};
	QMap<QString, QString> messages{{"guided-starting", "Starting soon"}, {"guided-break", "Be right back"},
		{"guided-ending", "Thanks for watching"}, {"select-celebration", "Let's celebrate"}};
	QVector<PulseSetupPlan::Alert> alerts = QVector<PulseSetupPlan::Alert>(3);
};
struct Choice { QString key, title, detail; };
inline QVector<Choice> choices()
{
	QVector<Choice> result;
	for (const auto &look : PulseShow::selectCatalogue().front().looks)
		result.append({"select-main/" + look.id, look.name, look.description});
	for (const auto &theme : PulseShow::guidedCatalogue()) if (theme.id != "guided-main")
		result.append({theme.id + "/card", theme.looks.front().name, theme.looks.front().description});
	result.append({"select-celebration/announcement", "Celebration / announcement", "A message or your own artwork, with an optional face camera"});
	return result;
}
inline const PulseShow::Look *look(const QString &key)
{
	const auto *theme = PulseShow::theme(key.section('/', 0, 0));
	return theme ? PulseShow::look(*theme, key.section('/', 1)) : nullptr;
}
// Requiredness is derived from the selected results, never from an activity
// question or sources the user happened to have assigned on a previous visit.
inline QMap<QString, bool> inputs(const Options &options)
{
	QMap<QString, bool> result;
	for (const auto &choice : choices()) if (options.selected.contains(choice.key)) {
		const auto *view = look(choice.key); if (!view) continue;
		if (view->main != "graphic") result[view->main] = true;
		if (!view->support.isEmpty()) result[view->support] = result.value(view->support) || !view->optionalPresenter;
		if (!view->third.isEmpty()) result[view->third] = true;
	}
	return result;
}
inline QSet<QString> references(const Options &options)
{
	QSet<QString> result;
	const auto roles = inputs(options);
	for (auto it = roles.begin(); it != roles.end(); ++it) if (!options.sources.value(it.key()).isEmpty()) result.insert(options.sources.value(it.key()));
	for (const auto &choice : choices()) if (options.selected.contains(choice.key)) {
		const auto art = options.artwork.value(choice.key.section('/', 0, 0)).toObject();
		for (const QString &route : {QString("horizontal"), QString("vertical")}) if (!art.value(route).toString().isEmpty()) result.insert(art.value(route).toString());
	}
	for (const auto &alert : options.alerts) if (alert.enabled) {
		if (!alert.horizontal.isEmpty()) result.insert(alert.horizontal);
		if (!alert.vertical.isEmpty()) result.insert(alert.vertical);
	}
	return result;
}
inline QString problem(const Options &options, bool sources = true)
{
	const auto name = options.name.trimmed();
	if (name.isEmpty() || name.size() > 60) return "Give your show a name of 1–60 characters.";
	for (const auto c : name) if (!c.isPrint()) return "Use a show name without line breaks or control characters.";
	if (options.selected.isEmpty()) return "Select at least one layout or screen.";
	QSet<QString> known; for (const auto &choice : choices()) known.insert(choice.key);
	for (const auto &key : options.selected) if (!known.contains(key)) return "Choose an available layout or screen.";
	if (!QSet<QString>{"navy", "cyan", "violet", "amber"}.contains(options.style)) return "Choose an available background colour.";
	if (sources) {
		const auto roles = inputs(options);
		for (auto it = roles.begin(); it != roles.end(); ++it) if (it.value() && options.sources.value(it.key()).isEmpty()) return "Choose " + PulseShow::roleName(it.key()).toLower() + ".";
		for (const auto &key : options.selected) {
			const auto *view = look(key); QSet<QString> seen;
			for (const auto &role : {view->main, view->support, view->third}) {
				if (role.isEmpty() || role == "graphic") continue;
				const auto id = options.sources.value(role); if (id.isEmpty()) continue;
				if (seen.contains(id)) return "Choose different pictures for “" + view->name + "”, or select a simpler layout.";
				seen.insert(id);
			}
		}
	}
	for (const auto &choice : choices()) if (options.selected.contains(choice.key) && look(choice.key)->main == "graphic") {
		const auto theme = choice.key.section('/', 0, 0); const auto art = options.artwork.value(theme).toObject();
		if ((art.value("horizontal").toString().isEmpty() || art.value("vertical").toString().isEmpty()) &&
			(options.messages.value(theme).trimmed().isEmpty() || options.messages.value(theme).size() > 160)) return "Add a message for each built-in screen.";
	}
	if (options.alerts.size() > 3) return "Use up to three alert layers.";
	QSet<QString> overlaySources[2]; bool hasMain = false;
	for (const auto &key : options.selected) if (key.startsWith("select-main/")) hasMain = true;
	for (const auto &layer : options.alerts) if (layer.enabled) {
		if (layer.horizontal.isEmpty() && layer.vertical.isEmpty()) return "Add a browser overlay or remove the empty alert layer.";
		if (layer.scope != "main" && layer.scope != "all") return "Choose where each alert layer should appear.";
		if (layer.scope == "main" && !hasMain) return "This show has no Main layouts. Set the alert layer to every screen, or remove it.";
		const QString ids[]{layer.horizontal, layer.vertical};
		for (int i = 0; i < 2; ++i) if (!ids[i].isEmpty()) {
			if (overlaySources[i].contains(ids[i])) return "Use a browser source in only one alert layer per format.";
			overlaySources[i].insert(ids[i]);
		}
	}
	if (sources) for (const auto &id : references(options)) if (id.startsWith("new:") && !options.newSources.contains(id.mid(4))) return "A selected input draft is missing. Choose it again.";
	return {};
}
inline QJsonObject draft(const Options &options)
{
	if (!problem(options).isEmpty()) return {};
	QJsonObject definitions, stingers;
	QSet<QString> usedNames;
	for (const auto &id : references(options)) if (id.startsWith("new:")) {
		const auto value = options.newSources.value(id.mid(4)).toObject(); definitions.insert(id.mid(4), value); usedNames.insert(value.value("name").toString());
	}
	PulseSetupPlan::Options common; common.transitions = options.transitions;
	const auto policy = PulseSetupPlan::Detail::policy(common);
	for (const auto &id : PulseSetupPlan::Detail::stingerReferences(common)) stingers.insert(id, options.newStingers.value(id));
	auto card = [&](const QString &base, const QString &message, bool background) {
		QString key = base; for (int i = 2; options.newSources.contains(key) || definitions.contains(key); ++i) key = base + "-" + QString::number(i);
		const QString baseName = options.name.trimmed() + " · " + (background ? "Show background" : base + " image");
		QString name = baseName; for (int i = 2; usedNames.contains(name); ++i) name = baseName + " " + QString::number(i);
		usedNames.insert(name); definitions.insert(key, QJsonObject{{"name", name}, {"kind", "setup_card"}, {"text", message.trimmed()},
			{"subtitle", background ? QString() : options.name.trimmed()}, {"style", options.style}, {"backgroundOnly", background}});
		return "new:" + key;
	};
	QJsonArray stages, keys;
	QStringList themes{"guided-starting", "select-main", "guided-break", "guided-ending", "select-celebration"};
	for (const auto &id : themes) {
		const auto *theme = PulseShow::theme(id); QJsonArray selected; QJsonObject roles, framing, artwork;
		for (const auto &view : theme->looks) if (options.selected.contains(id + "/" + view.id)) {
			selected.append(view.id); keys.append(id + "/" + view.id);
			for (const auto &role : {view.main, view.support, view.third}) if (!role.isEmpty() && role != "graphic") roles.insert(role, options.sources.value(role));
			for (const auto &route : {QString("horizontal"), QString("vertical")}) framing.insert(view.id + "/" + route, options.framing);
			if (view.main == "graphic") {
				const auto art = options.artwork.value(id).toObject(); QString h = art.value("horizontal").toString(), v = art.value("vertical").toString();
				if (h.isEmpty() || v.isEmpty()) { const auto fallback = card(id, options.messages.value(id), false); if (h.isEmpty()) h = fallback; if (v.isEmpty()) v = fallback; }
				roles.insert("graphic", h); roles.insert("graphicPortrait", v);
				artwork.insert(view.id, QJsonObject{{"graphic", h}, {"graphicPortrait", v}, {"graphicFullCanvas", true}, {"graphicPortraitFullCanvas", true}});
			}
		}
		if (selected.isEmpty()) continue;
		QJsonObject stage{{"theme", id}, {"looks", selected}, {"roles", roles}, {"framing", framing}, {"lookArtwork", artwork}, {"transitions", policy}};
		if (id == "select-main") stage.insert("background", card("select-background", {}, true));
		stages.append(stage);
	}
	QJsonArray overlays;
	for (int i = 0; i < options.alerts.size(); ++i) {
		const auto &layer = options.alerts[i]; if (!layer.enabled) continue;
		QJsonArray visible; for (const auto &key : keys) if (layer.scope == "all" || key.toString().startsWith("select-main/")) visible.append(key);
		if (visible.isEmpty()) continue;
		for (const auto &route : {QString("horizontal"), QString("vertical")}) {
			const auto source = route == "horizontal" ? layer.horizontal : layer.vertical;
			if (!source.isEmpty()) overlays.append(QJsonObject{{"canvas", route}, {"order", i + 1}, {"source", source}, {"looks", visible}});
		}
	}
	return {{"schema", 2}, {"overlayProvider", "guided"}, {"name", options.name.trimmed()}, {"stages", stages}, {"newSources", definitions},
		{"newStingers", stingers}, {"overlays", overlays}, {"transitions", policy}};
}
} // namespace PulseSelectPlan
