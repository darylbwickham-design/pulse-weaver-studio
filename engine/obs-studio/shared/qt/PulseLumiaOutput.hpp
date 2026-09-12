#pragma once
#include <obs.h>
#include <callback/calldata.h>
#include <callback/signal.h>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>

namespace PulseLumia {
inline void publish(const QString &event, QJsonObject data = {})
{
	data.insert("event", event);
	const QByteArray json = QJsonDocument(data).toJson(QJsonDocument::Compact);
	calldata_t parameters;
	calldata_init(&parameters);
	calldata_set_string(&parameters, "json", json.constData());
	signal_handler_signal(obs_get_signal_handler(), "pulseweaver_operator_event", &parameters);
	calldata_free(&parameters);
}

// Output-owned callbacks require no polling, timers or retained output refs.
inline void outputEvent(void *, const char *signal, calldata_t *parameters)
{
	const QString name = QString::fromUtf8(signal);
	QString state;
	if (name == "starting") state = "starting";
	else if (name == "start" || name == "reconnect_success") state = "live";
	else if (name == "stopping") state = "stopping";
	else if (name == "reconnect") state = "reconnecting";
	else if (name == "stop") state = calldata_int(parameters, "code") == OBS_OUTPUT_SUCCESS ? "stopped" : "failed";
	else return;
	auto *output = static_cast<obs_output_t *>(calldata_ptr(parameters, "output"));
	if (!output) return;
	const QString outputName = QString::fromUtf8(obs_output_get_name(output));
	const QString provider = outputName.contains("kick") ? "kick" : outputName.contains("youtube") ? "youtube" :
		outputName.contains("record") ? "recording" : "twitch";
	const char *error = state == "failed" ? obs_output_get_last_error(output) : nullptr;
	publish(provider == "recording" ? "recording_state" : "destination_state",
		{{"platform", provider}, {"output", provider == "twitch" ? QString("twitch") : outputName}, {"state", state},
		 {"message", QString::fromUtf8(error ? error : "")}, {"code", double(calldata_int(parameters, "code"))}});
}
inline void watchOutput(obs_output_t *output)
{
	if (output) signal_handler_connect_global(obs_output_get_signal_handler(output), outputEvent, nullptr);
}
}
