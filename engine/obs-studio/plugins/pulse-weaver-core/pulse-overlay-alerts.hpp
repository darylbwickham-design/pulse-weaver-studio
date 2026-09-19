#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QQueue>
#include <QString>

#include <optional>

namespace PulseOverlay {

struct AlertEvent {
	QString eventKey;
	QJsonObject data;
};

QJsonObject matchAlertVariation(const QJsonArray &variations, const QString &eventKey,
				const QJsonObject &data, bool *matched = nullptr);
QString expandAlertTemplate(QString text, const QJsonObject &data);
QJsonObject normalizeAlertEvent(QJsonObject data, const QString &canonical, const QString &platform);

class AlertLane {
public:
	bool enqueue(const AlertEvent &event, int maximumPending = 100);
	std::optional<AlertEvent> beginNext();
	bool completeCurrent();
	bool skipCurrent();
	void clearPending();
	void setPaused(bool paused);
	bool isPaused() const;
	bool isActive() const;
	int pendingCount() const;
	void markReloadPending();
	bool takeReloadPending();

private:
	QQueue<AlertEvent> pending;
	bool active = false;
	bool paused = false;
	bool reloadPending = false;
};

} // namespace PulseOverlay
