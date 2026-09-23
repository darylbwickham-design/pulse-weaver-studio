#pragma once
#include "../../shared/qt/PulseLumiaOutput.hpp"
#include <obs-frontend-api.h>
#include <QComboBox>
#include <QJsonArray>
#include <QPointer>
#include <QTcpSocket>
#include <QTimer>
#include <QUrlQuery>
#include <functional>
#include <cmath>

inline QString pulseLumiaStageName(const QComboBox *selector, int index)
{
	if (!selector || index < 0 || index >= selector->count()) return {};
	const QString name = selector->itemData(index, Qt::UserRole + 1).toString();
	return name.isEmpty() ? selector->itemText(index) : name;
}

// Restricted operating controls. No settings, transforms, creation or raw requests.
class PulseLumiaBridge : public QObject {
	QHash<QString, obs_weak_source_t *> sources;
	QList<QPointer<QTcpSocket>> clients;
	QJsonObject outputStates;
	QTimer heartbeat{this}, catalogueTimer{this};
	std::function<QJsonObject()> baseState;
	quint64 sequence = 0;
	bool primaryObserved = false;
	static void operatorEvent(void *data, calldata_t *parameters)
	{
		auto *self = static_cast<PulseLumiaBridge *>(data);
		const QJsonObject event = QJsonDocument::fromJson(QByteArray(calldata_string(parameters, "json"))).object();
		QMetaObject::invokeMethod(self, [self, event] { self->deliver(event); }, Qt::QueuedConnection);
	}
	static void sourceCreated(void *data, calldata_t *parameters)
	{
		auto *self = static_cast<PulseLumiaBridge *>(data);
		auto *source = static_cast<obs_source_t *>(calldata_ptr(parameters, "source"));
		if (!source || obs_obj_is_private(source)) return;
		const QString uuid = QString::fromUtf8(obs_source_get_uuid(source));
		QMetaObject::invokeMethod(self, [self, uuid] {
			obs_source_t *current = obs_get_source_by_uuid(uuid.toUtf8().constData());
			if (current) { self->watch(current); obs_source_release(current); }
			self->catalogueChanged();
		}, Qt::QueuedConnection);
	}
	static void sourceEvent(void *data, const char *signal, calldata_t *parameters)
	{
		auto *self = static_cast<PulseLumiaBridge *>(data);
		const QString name = QString::fromUtf8(signal);
		if (name == "item_add" || name == "item_remove" || name == "rename" || name == "remove" || name == "destroy") {
			QMetaObject::invokeMethod(self, [self] { self->catalogueChanged(); }, Qt::QueuedConnection);
			return;
		}
		QString event;
		if (name == "item_visible") event = calldata_bool(parameters, "visible") ? "source_shown" : "source_hidden";
		else if (name == "mute") event = calldata_bool(parameters, "muted") ? "source_muted" : "source_unmuted";
		else if (name == "volume") event = "source_volume";
		else if (name == "media_started" || name == "media_ended" || name == "media_pause" || name == "media_play" || name == "media_stopped") event = name;
		else if (name == "transition_start") event = "transition_begin";
		else if (name == "transition_stop") event = "transition_end";
		else return;
		auto *source = static_cast<obs_source_t *>(calldata_ptr(parameters, "source"));
		QJsonObject payload{{"event", event}};
		if (name == "item_visible") {
			auto *item = static_cast<obs_sceneitem_t *>(calldata_ptr(parameters, "item"));
			auto *scene = static_cast<obs_scene_t *>(calldata_ptr(parameters, "scene"));
			if (!item || !scene) return;
			source = obs_sceneitem_get_source(item);
			payload.insert("scene", QString::fromUtf8(obs_source_get_name(obs_scene_get_source(scene))));
			payload.insert("itemId", double(obs_sceneitem_get_id(item)));
			payload.insert("visible", calldata_bool(parameters, "visible"));
		}
		if (!source) return;
		payload.insert("source", QString::fromUtf8(obs_source_get_name(source)));
		payload.insert("sourceId", QString::fromUtf8(obs_source_get_uuid(source)));
		if (name == "volume") payload.insert("volume", calldata_float(parameters, "volume") * 100.0);
		if (name == "mute") payload.insert("muted", calldata_bool(parameters, "muted"));
		QMetaObject::invokeMethod(self, [self, payload] { self->deliver(payload); }, Qt::QueuedConnection);
	}
	void watch(obs_source_t *source)
	{
		if (obs_obj_is_private(source)) return;
		const QString uuid = QString::fromUtf8(obs_source_get_uuid(source));
		if (sources.contains(uuid)) return;
		sources.insert(uuid, obs_source_get_weak_source(source));
		signal_handler_connect_global(obs_source_get_signal_handler(source), sourceEvent, this);
	}
	void catalogueChanged()
	{
		for (auto it = sources.begin(); it != sources.end();) {
			obs_source_t *source = obs_weak_source_get_source(it.value());
			if (!source || obs_source_removed(source)) {
				if (source) signal_handler_disconnect_global(obs_source_get_signal_handler(source), sourceEvent, this);
				obs_weak_source_release(it.value()); it = sources.erase(it);
			} else ++it;
			obs_source_release(source);
		}
		if (!clients.isEmpty() && !catalogueTimer.isActive()) catalogueTimer.start(200);
	}
	void send(QTcpSocket *socket, const QJsonObject &payload)
	{
		if (!socket || socket->state() != QAbstractSocket::ConnectedState) return;
		if (socket->bytesToWrite() > 256 * 1024) { socket->disconnectFromHost(); return; }
		socket->write("data: " + QJsonDocument(payload).toJson(QJsonDocument::Compact) + "\n\n");
	}
	void broadcast(const QJsonObject &payload)
	{
		for (const auto &client : std::as_const(clients)) if (client) send(client, payload);
	}
public:
	explicit PulseLumiaBridge(QObject *parent, std::function<QJsonObject()> state) : QObject(parent), baseState(std::move(state))
	{
		signal_handler_add(obs_get_signal_handler(), "void pulseweaver_operator_event(string json)");
		signal_handler_connect(obs_get_signal_handler(), "pulseweaver_operator_event", operatorEvent, this);
		signal_handler_connect(obs_get_signal_handler(), "source_create", sourceCreated, this);
		obs_enum_all_sources([](void *data, obs_source_t *source) { static_cast<PulseLumiaBridge *>(data)->watch(source); return true; }, this);
		catalogueTimer.setSingleShot(true);
		connect(&catalogueTimer, &QTimer::timeout, this, [this] { broadcast(QJsonObject{{"kind", "catalogue"}, {"state", stateJson()}}); });
		connect(&heartbeat, &QTimer::timeout, this, [this] {
			for (const auto &client : std::as_const(clients)) if (client) {
				if (client->bytesToWrite() > 256 * 1024) client->disconnectFromHost();
				else client->write(": heartbeat\n\n");
			}
		});
		auto *window = static_cast<QWidget *>(obs_frontend_get_main_window());
		if (auto *selector = window ? window->findChild<QComboBox *>("PulseWeaverStageSelector") : nullptr) {
			connect(selector, &QComboBox::currentTextChanged, this, [this, selector](const QString &) {
				deliver({{"event", "stage_changed"}, {"stage", pulseLumiaStageName(selector, selector->currentIndex())}});
			});
			connect(selector->model(), &QAbstractItemModel::rowsInserted, this, [this] { catalogueChanged(); });
			connect(selector->model(), &QAbstractItemModel::rowsRemoved, this, [this] { catalogueChanged(); });
			connect(selector->model(), &QAbstractItemModel::dataChanged, this, [this] { catalogueChanged(); });
		}
		for (const QString &provider : {QString("Twitch"), QString("YouTube"), QString("Kick")}) {
			if (auto *route = window ? window->findChild<QComboBox *>("PulseWeaverDestination" + provider) : nullptr)
				connect(route, &QComboBox::currentIndexChanged, this, [this] { catalogueChanged(); });
		}
	}
	~PulseLumiaBridge() override
	{
		signal_handler_disconnect(obs_get_signal_handler(), "pulseweaver_operator_event", operatorEvent, this);
		signal_handler_disconnect(obs_get_signal_handler(), "source_create", sourceCreated, this);
		for (auto weak : std::as_const(sources)) {
			obs_source_t *source = obs_weak_source_get_source(weak);
			if (source) { signal_handler_disconnect_global(obs_source_get_signal_handler(source), sourceEvent, this); obs_source_release(source); }
			obs_weak_source_release(weak);
		}
		for (const auto &client : std::as_const(clients)) if (client) client->disconnectFromHost();
	}
	QJsonObject stateJson() const
	{
		QJsonObject state = baseState();
		state.insert("operatorApi", 2);
		state.insert("outputs", outputStates);
		state.insert("sources", catalogue());
		return state;
	}
	QJsonArray catalogue() const
	{
		QJsonArray result;
		for (auto weak : sources) {
			obs_source_t *source = obs_weak_source_get_source(weak);
			if (!source) continue;
			if (!obs_source_removed(source)) {
				QJsonObject row{{"id", QString::fromUtf8(obs_source_get_uuid(source))}, {"name", QString::fromUtf8(obs_source_get_name(source))},
					{"audio", bool(obs_source_get_output_flags(source) & OBS_SOURCE_AUDIO)},
					{"media", bool(obs_source_get_output_flags(source) & OBS_SOURCE_CONTROLLABLE_MEDIA)},
					{"muted", obs_source_muted(source)}, {"volume", obs_source_get_volume(source) * 100.0}};
				QJsonArray items;
				obs_scene_t *scene = obs_scene_from_source(source);
				if (!scene) scene = obs_group_from_source(source);
				if (scene) obs_scene_enum_items(scene, [](obs_scene_t *, obs_sceneitem_t *item, void *data) {
					auto *items = static_cast<QJsonArray *>(data);
					items->append(QJsonObject{{"itemId", QString::number(obs_sceneitem_get_id(item))},
						{"name", QString::fromUtf8(obs_source_get_name(obs_sceneitem_get_source(item)))}, {"visible", obs_sceneitem_visible(item)}});
					return true;
				}, &items);
				row.insert("items", items); result.append(row);
			}
			obs_source_release(source);
		}
		return result;
	}
	void subscribe(QTcpSocket *socket)
	{
		if (clients.size() >= 4) { socket->write("HTTP/1.1 503 Busy\r\nContent-Length: 0\r\n\r\n"); socket->disconnectFromHost(); return; }
		socket->write("HTTP/1.1 200 OK\r\nContent-Type: text/event-stream\r\nCache-Control: no-cache\r\nConnection: keep-alive\r\n\r\n");
		clients.append(socket);
		connect(socket, &QTcpSocket::disconnected, this, [this, socket] {
			clients.removeAll(socket); if (clients.isEmpty()) heartbeat.stop();
		});
		if (!heartbeat.isActive()) heartbeat.start(20000);
		send(socket, {{"kind", "snapshot"}, {"state", stateJson()}});
	}
	void deliver(QJsonObject payload)
	{
		const QString event = payload.value("event").toString();
		if (event == "destination_state" || event == "recording_state") {
			const QString key = payload.value("output").toString(payload.value("platform").toString());
			if (outputStates.value(key).toObject().value("state") == payload.value("state")) return;
			outputStates.insert(key, payload);
		}
		payload.insert("kind", "event"); payload.insert("sequence", double(++sequence));
		broadcast(payload);
	}
	void frontendEvent(obs_frontend_event event)
	{
		if (event == OBS_FRONTEND_EVENT_STREAMING_STARTING) {
			obs_output_t *output = obs_frontend_get_streaming_output();
			primaryObserved = output != nullptr;
			PulseLumia::watchOutput(output);
			obs_output_release(output);
		} else if (primaryObserved && (event == OBS_FRONTEND_EVENT_STREAMING_STARTED ||
			event == OBS_FRONTEND_EVENT_STREAMING_STOPPING || event == OBS_FRONTEND_EVENT_STREAMING_STOPPED)) return;
		QString state, platform;
		switch (event) {
		case OBS_FRONTEND_EVENT_STREAMING_STARTING: platform = "twitch"; state = "starting"; break;
		case OBS_FRONTEND_EVENT_STREAMING_STARTED: platform = "twitch"; state = "live"; break;
		case OBS_FRONTEND_EVENT_STREAMING_STOPPING: platform = "twitch"; state = "stopping"; break;
		case OBS_FRONTEND_EVENT_STREAMING_STOPPED: platform = "twitch"; state = "stopped"; break;
		case OBS_FRONTEND_EVENT_RECORDING_STARTING: platform = "recording"; state = "starting"; break;
		case OBS_FRONTEND_EVENT_RECORDING_STARTED: platform = "recording"; state = "live"; break;
		case OBS_FRONTEND_EVENT_RECORDING_STOPPING: platform = "recording"; state = "stopping"; break;
		case OBS_FRONTEND_EVENT_RECORDING_STOPPED: platform = "recording"; state = "stopped"; break;
		case OBS_FRONTEND_EVENT_SCENE_LIST_CHANGED:
		case OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGED: catalogueChanged(); return;
		default: return;
		}
		deliver({{"event", platform == "recording" ? "recording_state" : "destination_state"},
			{"platform", platform}, {"output", platform}, {"state", state}});
	}
	QJsonObject operateSource(const QUrlQuery &query)
	{
		const QString action = query.queryItemValue("action");
		const QString uuid = query.queryItemValue("source", QUrl::FullyDecoded);
		obs_source_t *source = sources.contains(uuid) ? obs_weak_source_get_source(sources.value(uuid)) : nullptr;
		if (!source || obs_source_removed(source)) { obs_source_release(source); return {{"ok", false}, {"message", "Source no longer exists. Choose an existing source in Pulse Weaver."}}; }
		bool ok = true;
		QString message = "Operation applied.";
		if (action == "show" || action == "hide" || action == "toggle_visibility") {
			obs_scene_t *scene = obs_scene_from_source(source);
			if (!scene) scene = obs_group_from_source(source);
			bool valid = false;
			const qlonglong id = query.queryItemValue("item").toLongLong(&valid);
			obs_sceneitem_t *item = scene && valid ? obs_scene_find_sceneitem_by_id(scene, id) : nullptr;
			if (!item) { ok = false; message = "Scene item no longer exists."; }
			else obs_sceneitem_set_visible(item, action == "toggle_visibility" ? !obs_sceneitem_visible(item) : action == "show");
		} else if (action == "mute" || action == "unmute" || action == "toggle_mute" || action == "volume") {
			if (!(obs_source_get_output_flags(source) & OBS_SOURCE_AUDIO)) { ok = false; message = "This source has no audio."; }
			else if (action == "volume") {
				bool valid = false; const double volume = query.queryItemValue("volume").toDouble(&valid);
				if (!valid || !std::isfinite(volume) || volume < 0 || volume > 100) { ok = false; message = "Volume must be between 0 and 100 percent."; }
				else obs_source_set_volume(source, float(volume / 100.0));
			} else obs_source_set_muted(source, action == "toggle_mute" ? !obs_source_muted(source) : action == "mute");
		} else if (action == "play" || action == "pause" || action == "restart" || action == "stop_media" || action == "next_media" || action == "previous_media") {
			if (!(obs_source_get_output_flags(source) & OBS_SOURCE_CONTROLLABLE_MEDIA)) { ok = false; message = "This source does not support media playback control."; }
			else if (action == "play" || action == "pause") obs_source_media_play_pause(source, action == "pause");
			else if (action == "restart") obs_source_media_restart(source);
			else if (action == "stop_media") obs_source_media_stop(source);
			else if (action == "next_media") obs_source_media_next(source);
			else obs_source_media_previous(source);
		} else { ok = false; message = "Only live source visibility, audio and playback controls are supported."; }
		obs_source_release(source);
		return {{"ok", ok}, {"message", message}};
	}
};
