#pragma once

#include <QComboBox>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPushButton>
#include <QSet>
#include <QUrlQuery>
#include <cmath>
#include <functional>

// Small operation surface for local action tools. Native controllers retain
// ownership of Stage application, routing and recording guards.
namespace PulseBasicControls {
inline quint16 defaultApiPort(const QByteArray &identity, quint16 existingDefault)
{
	const auto document = QJsonDocument::fromJson(identity);
	return document.isObject() && document.object().value("product").toString() == "studio2-preview" ? 18775 : existingDefault;
}

struct Context {
	QComboBox *stages = nullptr;
	QComboBox *recordingMode = nullptr;
	QPushButton *record = nullptr;
	std::function<QJsonArray()> sources;
	std::function<QJsonObject(const QUrlQuery &)> operateSource;
	std::function<QJsonObject()> state;
	std::function<bool()> recordingActive;
	std::function<bool()> replayActive;
	std::function<void()> saveReplay;
};

struct Result {
	int status;
	QJsonObject body;
};

struct RecordingState {
	bool available;
	bool active;
};

inline RecordingState recordingState(QObject *window, bool nativeRecording)
{
	bool routedRecording = false;
	const bool available = window && window->metaObject()->indexOfMethod("PulseWeaverRecordingActive()") >= 0 &&
		QMetaObject::invokeMethod(window, "PulseWeaverRecordingActive", Qt::DirectConnection, Q_RETURN_ARG(bool, routedRecording));
	return {available, nativeRecording || routedRecording};
}

inline QJsonArray commands()
{
	return QJsonArray{
		QJsonObject{{"command", "stage.select"}, {"arguments", QJsonObject{{"name", "existing Stage name"}}}},
		QJsonObject{{"command", "stage.next"}, {"arguments", QJsonObject{}}},
		QJsonObject{{"command", "stage.previous"}, {"arguments", QJsonObject{}}},
		QJsonObject{{"command", "source.show"}, {"arguments", QJsonObject{{"sceneId", "scene/group UUID"}, {"itemId", "decimal string"}}}},
		QJsonObject{{"command", "source.hide"}, {"arguments", QJsonObject{{"sceneId", "scene/group UUID"}, {"itemId", "decimal string"}}}},
		QJsonObject{{"command", "source.toggle"}, {"arguments", QJsonObject{{"sceneId", "scene/group UUID"}, {"itemId", "decimal string"}}}},
		QJsonObject{{"command", "audio.mute"}, {"arguments", QJsonObject{{"sourceId", "audio source UUID"}}}},
		QJsonObject{{"command", "audio.unmute"}, {"arguments", QJsonObject{{"sourceId", "audio source UUID"}}}},
		QJsonObject{{"command", "audio.toggle"}, {"arguments", QJsonObject{{"sourceId", "audio source UUID"}}}},
		QJsonObject{{"command", "audio.volume"}, {"arguments", QJsonObject{{"sourceId", "audio source UUID"}, {"volume", "number, 0–100 percent"}}}},
		QJsonObject{{"command", "recording.start"}, {"arguments", QJsonObject{}}},
		QJsonObject{{"command", "recording.stop"}, {"arguments", QJsonObject{}}},
		QJsonObject{{"command", "replay.save"}, {"arguments", QJsonObject{}}}};
}

inline QString stageName(const QComboBox *selector, int index)
{
	const QString name = selector->itemData(index, Qt::UserRole + 1).toString();
	return name.isEmpty() ? selector->itemText(index) : name;
}

inline QJsonObject catalogue(const Context &context)
{
	QJsonArray stages;
	if (context.stages)
		for (int index = 0; index < context.stages->count(); ++index)
			stages.append(stageName(context.stages, index));
	return {{"api", "pulseweaver.basic-controls"}, {"version", 1}, {"commands", commands()},
		{"stages", stages}, {"sources", context.sources ? context.sources() : QJsonArray{}},
		{"state", context.state ? context.state() : QJsonObject{}},
		{"availability", QJsonObject{{"stages", context.stages && context.stages->isEnabled() && context.stages->count() > 0},
			{"sourceControls", bool(context.sources) && bool(context.operateSource)},
			{"recordingMode", context.recordingMode ? context.recordingMode->currentData().toString() : QString()},
			{"recording", context.record && context.recordingMode && bool(context.recordingActive)},
			{"replaySave", context.replayActive && context.replayActive()}}},
		{"semantics", "Stage/source operations report observed state. Recording and replay acceptance does not confirm a finished file."}};
}

inline bool text(const QJsonObject &input, const QString &key, int maximum = 256)
{
	const QJsonValue value = input.value(key);
	if (!value.isString() || value.toString().trimmed().isEmpty() || value.toString().size() > maximum)
		return false;
	for (QChar character : value.toString())
		if (character.unicode() < 0x20 || character.unicode() == 0x7f)
			return false;
	return true;
}

inline QJsonObject sourceRow(const QJsonArray &sources, const QString &id)
{
	for (const auto &entry : sources) {
		const QJsonObject row = entry.toObject();
		if (row.value("id").toString() == id)
			return row;
	}
	return {};
}

inline Result execute(const QJsonObject &input, const Context &context)
{
	const QString command = input.value("command").toString();
	auto reply = [&](int status, const QString &code, const QString &message, QJsonObject observed = {}, bool accepted = false) {
		QJsonObject body{{"ok", status < 300}, {"accepted", accepted}, {"command", command},
			{"code", code}, {"message", message}, {"state", observed}};
		if (text(input, "requestId", 128)) body.insert("requestId", input.value("requestId"));
		return Result{status, body};
	};
	QJsonObject definition;
	for (const auto &entry : commands())
		if (entry.toObject().value("command").toString() == command) definition = entry.toObject();
	if (definition.isEmpty()) return reply(400, "unknown_command", "Choose a command listed by GET /api/v1/control.");
	QSet<QString> allowed{"command", "requestId"};
	const auto arguments = definition.value("arguments").toObject();
	for (auto it = arguments.begin(); it != arguments.end(); ++it) allowed.insert(it.key());
	for (auto it = input.begin(); it != input.end(); ++it)
		if (!allowed.contains(it.key())) return reply(400, "invalid_arguments", "Unexpected argument: " + it.key());
	if (input.contains("requestId") && !text(input, "requestId", 128))
		return reply(400, "invalid_arguments", "requestId must be a nonempty string of at most 128 characters.");
	for (auto it = arguments.begin(); it != arguments.end(); ++it) {
		if (it.key() == "volume") {
			const auto value = input.value("volume");
			if (!value.isDouble() || !std::isfinite(value.toDouble()) || value.toDouble() < 0 || value.toDouble() > 100)
				return reply(400, "invalid_arguments", "volume must be a number between 0 and 100 percent.");
		} else if (!text(input, it.key())) return reply(400, "invalid_arguments", it.key() + " must be a nonempty string of at most 256 characters.");
	}
	if (command.startsWith("stage.")) {
		auto *selector = context.stages;
		if (!selector || !selector->isEnabled() || !selector->count())
			return reply(409, "unavailable", "Choose or create a Stage in Pulse Weaver first.");
		int target = -1;
		if (command == "stage.next") target = (selector->currentIndex() + 1) % selector->count();
		else if (command == "stage.previous") target = selector->currentIndex() < 0 ? selector->count() - 1 :
			(selector->currentIndex() - 1 + selector->count()) % selector->count();
		else {
			for (int index = 0; index < selector->count(); ++index)
				if (stageName(selector, index) == input.value("name").toString()) {
					if (target >= 0) return reply(409, "ambiguous_stage", "More than one Stage has that name. Rename it in Pulse Weaver first.");
					target = index;
				}
		}
		if (target < 0) return reply(404, "not_found", "Stage was not found.");
		selector->setCurrentIndex(target);
		if (selector->currentIndex() != target) return reply(409, "rejected", "Pulse Weaver rejected the Stage change.");
		return reply(200, "applied", "Stage selected; its configured transition may still be running.",
			{{"activeStage", stageName(selector, target)}, {"activeStageIndex", target}}, true);
	}
	if (command.startsWith("source.") || command.startsWith("audio.")) {
		if (!context.sources || !context.operateSource) return reply(409, "unavailable", "Source controls are unavailable.");
		const bool visibility = command.startsWith("source.");
		const QString id = input.value(visibility ? "sceneId" : "sourceId").toString();
		const auto row = sourceRow(context.sources(), id);
		if (row.isEmpty()) return reply(404, "not_found", "Source or scene no longer exists. Refresh the catalogue.");
		QUrlQuery query;
		query.addQueryItem("source", id);
		if (visibility) {
			const QString itemId = input.value("itemId").toString();
			bool valid = false;
			const qlonglong number = itemId.toLongLong(&valid);
			if (!valid || number < 0 || QString::number(number) != itemId)
				return reply(400, "invalid_arguments", "itemId must be a nonnegative decimal string from the catalogue.");
			bool found = false;
			for (const auto &item : row.value("items").toArray())
				if (item.toObject().value("itemId").toString() == itemId) found = true;
			if (!found) return reply(404, "not_found", "Scene/group item no longer exists. Refresh the catalogue.");
			query.addQueryItem("item", itemId);
			query.addQueryItem("action", command == "source.toggle" ? "toggle_visibility" : command.mid(7));
		} else {
			if (!row.value("audio").toBool()) return reply(409, "unsupported_source", "This source has no audio.");
			query.addQueryItem("action", command == "audio.toggle" ? "toggle_mute" : command.mid(6));
			if (command == "audio.volume") query.addQueryItem("volume", QString::number(input.value("volume").toDouble(), 'g', 17));
		}
		const auto operated = context.operateSource(query);
		if (!operated.value("ok").toBool()) return reply(409, "rejected", operated.value("message").toString("Source operation was rejected."));
		const auto observed = sourceRow(context.sources(), id);
		if (observed.isEmpty()) return reply(409, "unavailable", "Source disappeared before its state could be confirmed.");
		return reply(200, "applied", "Source operation applied.", observed, true);
	}
	if (command.startsWith("recording.")) {
		if (!context.record || !context.recordingMode || !context.recordingActive || !context.record->isEnabled())
			return reply(409, "unavailable", "Recording controls are unavailable.");
		const bool start = command == "recording.start";
		const bool active = context.recordingActive();
		if (start == active) return reply(200, "unchanged", start ? "Recording is already active." : "Recording is already stopped.", {{"recording", active}}, true);
		const QString mode = context.recordingMode->currentData().toString();
		if (start && mode != "standard" && mode != "horizontal" && mode != "vertical" && mode != "dual")
			return reply(409, "recording_disabled", "Choose a recording mode inside Pulse Weaver first.");
		context.record->click();
		return reply(202, "accepted", start ? "Recording start requested. Confirm the recording state in Pulse Weaver." :
			"Recording stop requested; file finalisation may still be running.", {{"recording", context.recordingActive()}}, true);
	}
	if (!context.replayActive || !context.saveReplay || !context.replayActive())
		return reply(409, "replay_inactive", "Start the replay buffer inside Pulse Weaver before saving a replay.");
	context.saveReplay();
	return reply(202, "accepted", "Replay save requested; wait for Pulse Weaver to finish writing the file.", {{"replayActive", true}}, true);
}
} // namespace PulseBasicControls
