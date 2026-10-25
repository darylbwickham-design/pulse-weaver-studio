#pragma once

#include "pulse-show-templates.hpp"
#include <QSet>
#include <QVector>

namespace PulseSetupPlan {
struct Sources {
	QString camera, content, detail;
};
struct Alert {
	bool enabled = false;
	QString horizontal, vertical;
	QString scope = "main";
};
struct Options {
	QString activity = "gaming", name = "My Show";
	Sources sources;
	QSet<QString> views;
	bool starting = true, brb = true, ending = true;
	QString startingText = "Starting soon", breakText = "Be right back", endingText = "Thanks for watching";
	QString style = "navy";
	bool cropCamera = false, cropDetail = false;
	QJsonObject newSources, newStingers, transitions;
	QVector<Alert> alerts;
	// Optional artwork per holding screen, with independent format fallbacks.
	QJsonObject artwork;
};
struct View {
	QString id, title, description;
	QStringList roles;
};

namespace Detail {
inline QString input(const Sources &sources, const QString &role)
{
	return (role == "feedA" ? sources.camera : role == "feedB" ? sources.content : sources.detail).trimmed();
}
inline QStringList roles(const PulseShow::Look &look)
{
	QStringList result;
	for (const auto &role : {look.main, look.support, look.third}) if (!role.isEmpty()) result.append(role);
	return result;
}
inline const PulseShow::Theme &mainTheme()
{
	return PulseShow::guidedCatalogue().front();
}
inline bool active(const Alert &alert)
{
	return alert.enabled && (!alert.horizontal.trimmed().isEmpty() || !alert.vertical.trimmed().isEmpty());
}
inline QJsonObject policy(const Options &options)
{
	QJsonObject result = options.transitions;
	for (const auto &route : {QString("horizontal"), QString("vertical")}) {
		if (!result.contains(route + "Transition")) result.insert(route + "Transition", "fade");
		if (result.value(route + "Transition") == "fade" && !result.contains(route + "DurationMs")) result.insert(route + "DurationMs", 500);
	}
	return result;
}
inline bool enabledCard(const Options &options, const QString &theme)
{
	return theme == "guided-starting" ? options.starting : theme == "guided-break" ? options.brb : options.ending;
}
inline QString cardText(const Options &options, const QString &theme)
{
	return (theme == "guided-starting" ? options.startingText : theme == "guided-break" ? options.breakText : options.endingText).trimmed();
}
inline QSet<QString> references(const Options &options)
{
	QSet<QString> result;
	for (const auto &look : mainTheme().looks) if (options.views.contains(look.id))
		for (const auto &role : roles(look)) if (!input(options.sources, role).isEmpty()) result.insert(input(options.sources, role));
	for (const auto &theme : {QString("guided-starting"), QString("guided-break"), QString("guided-ending")}) if (enabledCard(options, theme)) {
		const auto artwork = options.artwork.value(theme).toObject();
		for (const auto &route : {QString("horizontal"), QString("vertical")}) if (!artwork.value(route).toString().trimmed().isEmpty())
			result.insert(artwork.value(route).toString().trimmed());
	}
	for (const auto &alert : options.alerts) if (active(alert)) {
		if (!alert.horizontal.trimmed().isEmpty()) result.insert(alert.horizontal.trimmed());
		if (!alert.vertical.trimmed().isEmpty()) result.insert(alert.vertical.trimmed());
	}
	return result;
}
inline QSet<QString> stingerReferences(const Options &options)
{
	QSet<QString> result;
	const auto transitions = policy(options);
	for (const auto &route : {QString("horizontal"), QString("vertical")}) {
		const auto id = transitions.value(route + "Transition").toString();
		if (id.startsWith("new-stinger:")) result.insert(id.mid(12));
	}
	return result;
}
} // namespace Detail

inline QVector<View> availableViews(const Sources &sources)
{
	QVector<View> result;
	QSet<QString> fullViewInputs;
	for (const auto &look : Detail::mainTheme().looks) {
		const auto roles = Detail::roles(look);
		QSet<QString> inputs;
		bool complete = true;
		for (const auto &role : roles) {
			const auto id = Detail::input(sources, role);
			if (id.isEmpty() || inputs.contains(id)) { complete = false; break; }
			inputs.insert(id);
		}
		if (!complete) continue;
		if (roles.size() == 1) {
			const auto id = Detail::input(sources, roles.front());
			if (fullViewInputs.contains(id)) continue;
			fullViewInputs.insert(id);
		}
		result.append({look.id, look.name, look.description, roles});
	}
	return result;
}

inline QSet<QString> recommendedViews(const QString &activity, const Sources &sources)
{
	Q_UNUSED(activity);
	QSet<QString> available;
	for (const auto &view : availableViews(sources)) available.insert(view.id);
	QSet<QString> result;
	auto add = [&](const QString &id) { if (available.contains(id)) result.insert(id); };
	if (available.contains("content-camera")) { add("content-camera"); add("content"); add("camera"); }
	else if (available.contains("content-detail")) { add("content-detail"); add("content"); add("detail"); }
	else if (available.contains("two-cameras")) { add("two-cameras"); add("camera"); add("detail"); }
	else if (available.contains("content")) add("content");
	else if (available.contains("camera")) add("camera");
	else add("detail");
	// Three-up remains an explicit choice, rather than selecting every variant.
	return result;
}

inline QString problem(const Options &options, bool requireSources = true)
{
	const auto name = options.name.trimmed();
	if (name.isEmpty() || name.size() > 60) return "Enter a show name of 1–60 characters.";
	for (const auto character : name) if (!character.isPrint()) return "Use a show name without line breaks or control characters.";
	if (!QSet<QString>{"gaming", "talk", "creative", "guests"}.contains(options.activity)) return "Choose what you are streaming.";
	if (!QSet<QString>{"navy", "cyan", "violet", "amber"}.contains(options.style)) return "Choose an available show style.";
	if (options.alerts.size() > 3) return "Use up to three alert layers.";
	if (requireSources) {
		const QString camera = options.sources.camera.trimmed(), content = options.sources.content.trimmed(), detail = options.sources.detail.trimmed();
		if ((options.activity == "gaming" || options.activity == "creative") && content.isEmpty())
			return options.activity == "gaming" ? "Choose the game or content you want to stream." : "Choose the work area, app or content you want to show.";
		if (camera.isEmpty() && content.isEmpty() && detail.isEmpty()) return "Choose at least one camera, content or guest/detail source.";
		if ((!camera.isEmpty() && camera == content) || (!camera.isEmpty() && camera == detail) || (!content.isEmpty() && content == detail))
			return "The same source is assigned to more than one input. Choose different inputs, or leave an optional input out.";
	}
	if (options.views.isEmpty()) return "Choose at least one view for your main show.";
	QSet<QString> available;
	for (const auto &view : availableViews(options.sources)) available.insert(view.id);
	bool showsContent = false;
	for (const auto &id : options.views) {
		const auto *look = PulseShow::look(Detail::mainTheme(), id);
		if (!look) return "Choose an available view for your main show.";
		if (requireSources && !available.contains(id)) return "A selected view needs a missing or different input. Choose its sources or select another view.";
		if (Detail::roles(*look).contains("feedB")) showsContent = true;
	}
	if (requireSources && (options.activity == "gaming" || options.activity == "creative") && !showsContent)
		return "Choose at least one view that shows your main game or content.";
	for (const auto &theme : {QString("guided-starting"), QString("guided-break"), QString("guided-ending")}) if (Detail::enabledCard(options, theme)) {
		const auto artwork = options.artwork.value(theme).toObject();
		const bool needsFallback = artwork.value("horizontal").toString().trimmed().isEmpty() || artwork.value("vertical").toString().trimmed().isEmpty();
		if (needsFallback && (Detail::cardText(options, theme).isEmpty() || Detail::cardText(options, theme).size() > 160))
			return "Enter a message of 1–160 characters for each enabled built-in screen.";
	}
	QSet<QString> overlaySources[2];
	for (const auto &alert : options.alerts) if (Detail::active(alert)) {
		if (alert.scope != "main" && alert.scope != "all") return "Choose where each alert layer should appear.";
		const QString ids[]{alert.horizontal.trimmed(), alert.vertical.trimmed()};
		for (int index = 0; index < 2; ++index) if (!ids[index].isEmpty()) {
			if (overlaySources[index].contains(ids[index])) return "Use each alert source in only one layer per format.";
			overlaySources[index].insert(ids[index]);
		}
	}
	const auto transitions = Detail::policy(options);
	for (const auto &route : {QString("horizontal"), QString("vertical")}) {
		const auto selection = transitions.value(route + "Transition").toString();
		if (selection != "fade" && selection != "cut" && !(selection.startsWith("stinger:") && selection.size() > 8) &&
		    !(selection.startsWith("new-stinger:") && selection.size() > 12)) return "Choose Fade, Cut or an available stinger for each format.";
		if (transitions.contains(route + "DurationMs")) {
			const auto time = transitions.value(route + "DurationMs");
			if (!time.isDouble() || time.toDouble() != time.toInt() || time.toInt() < 0 || time.toInt() > 3000)
				return "Choose a fade duration from 0 to 3000 ms.";
		}
	}
	QSet<QString> newNames;
	if (requireSources) for (const auto &id : Detail::references(options)) if (id.startsWith("new:")) {
		const auto value = options.newSources.value(id.mid(4));
		if (!value.isObject()) return "A selected source draft is missing. Choose that input again.";
		const auto sourceName = value.toObject().value("name").toString().trimmed();
		if (sourceName.isEmpty() || sourceName.size() > 120) return "Give each selected new source a name of 1–120 characters.";
		if (newNames.contains(sourceName)) return "Two selected new sources have the same name. Give them different names.";
		newNames.insert(sourceName);
	}
	for (const auto &id : Detail::stingerReferences(options)) {
		const auto value = options.newStingers.value(id);
		if (!value.isObject()) return "A selected stinger draft is missing. Choose that transition again.";
		const auto stingerName = value.toObject().value("name").toString().trimmed();
		if (stingerName.isEmpty() || stingerName.size() > 90) return "Give each selected stinger a name of 1–90 characters.";
		if (newNames.contains(stingerName)) return "Selected new sources and stingers need different names.";
		newNames.insert(stingerName);
	}
	return {};
}

inline QJsonObject draft(const Options &options)
{
	if (!problem(options).isEmpty()) return {};
	QJsonObject definitions, stingers;
	QSet<QString> usedNames;
	for (const auto &id : Detail::references(options)) if (id.startsWith("new:")) {
		const auto value = options.newSources.value(id.mid(4)).toObject();
		definitions.insert(id.mid(4), value); usedNames.insert(value.value("name").toString().trimmed());
	}
	for (const auto &id : Detail::stingerReferences(options)) {
		const auto value = options.newStingers.value(id).toObject();
		stingers.insert(id, value); usedNames.insert(value.value("name").toString().trimmed());
	}
	auto card = [&](const QString &baseKey, const QString &label, const QString &text, bool backgroundOnly) {
		QString key = baseKey;
		for (int suffix = 2; options.newSources.contains(key) || definitions.contains(key); ++suffix) key = baseKey + "-" + QString::number(suffix);
		const QString baseName = options.name.trimmed() + " · " + label;
		QString sourceName = baseName;
		for (int suffix = 2; usedNames.contains(sourceName); ++suffix) sourceName = baseName + " " + QString::number(suffix);
		usedNames.insert(sourceName);
		definitions.insert(key, QJsonObject{{"name", sourceName}, {"kind", "setup_card"}, {"text", text},
			{"subtitle", backgroundOnly ? QString() : options.name.trimmed()}, {"style", options.style}, {"backgroundOnly", backgroundOnly}});
		return "new:" + key;
	};
	const auto transitions = Detail::policy(options);
	QJsonArray stages, selectedLooks, mainLooks;
	QJsonObject mainRoles, framing;
	for (const auto &look : Detail::mainTheme().looks) if (options.views.contains(look.id)) {
		mainLooks.append(look.id); selectedLooks.append("guided-main/" + look.id);
		for (const auto &role : Detail::roles(look)) mainRoles.insert(role, Detail::input(options.sources, role));
		const QJsonObject settings{{"feedAFill", options.cropCamera}, {"feedBFill", false}, {"feedCFill", options.cropDetail}};
		framing.insert(look.id + "/horizontal", settings); framing.insert(look.id + "/vertical", settings);
	}
	auto holding = [&](const QString &theme, const QString &label) {
		if (!Detail::enabledCard(options, theme)) return;
		const auto artwork = options.artwork.value(theme).toObject();
		QString horizontal = artwork.value("horizontal").toString().trimmed(), vertical = artwork.value("vertical").toString().trimmed();
		if (horizontal.isEmpty() || vertical.isEmpty()) {
			const auto fallback = card(theme + "-card", label + " screen", Detail::cardText(options, theme), false);
			if (horizontal.isEmpty()) horizontal = fallback;
			if (vertical.isEmpty()) vertical = fallback;
		}
		const QJsonObject roles{{"graphic", horizontal}, {"graphicPortrait", vertical}};
		const QJsonObject art{{"graphic", horizontal}, {"graphicPortrait", vertical}, {"graphicFullCanvas", true}, {"graphicPortraitFullCanvas", true}};
		stages.append(QJsonObject{{"theme", theme}, {"looks", QJsonArray{"card"}}, {"roles", roles},
			{"lookArtwork", QJsonObject{{"card", art}}}, {"transitions", transitions}});
		selectedLooks.append(theme + "/card");
	};
	holding("guided-starting", "Starting");
	stages.append(QJsonObject{{"theme", "guided-main"}, {"looks", mainLooks}, {"roles", mainRoles}, {"framing", framing},
		{"background", card("guided-background", "Show background", {}, true)}, {"transitions", transitions}});
	holding("guided-break", "Break");
	holding("guided-ending", "Ending");
	QJsonArray overlays;
	for (int index = 0; index < options.alerts.size(); ++index) {
		const auto &alert = options.alerts[index]; if (!Detail::active(alert)) continue;
		QJsonArray visible;
		for (const auto &key : selectedLooks) if (alert.scope == "all" || key.toString().startsWith("guided-main/")) visible.append(key);
		for (const auto &route : {QString("horizontal"), QString("vertical")}) {
			const auto id = (route == "horizontal" ? alert.horizontal : alert.vertical).trimmed();
			if (!id.isEmpty()) overlays.append(QJsonObject{{"canvas", route}, {"order", index + 1}, {"source", id}, {"looks", visible}});
		}
	}
	return {{"schema", 2}, {"overlayProvider", "guided"}, {"name", options.name.trimmed()}, {"stages", stages},
		{"newSources", definitions}, {"newStingers", stingers}, {"overlays", overlays}, {"transitions", transitions}};
}
} // namespace PulseSetupPlan
