#include "pulse-overlay-alerts.hpp"

namespace PulseOverlay {
namespace {

QJsonValue nestedValue(const QJsonObject &object, const QString &path)
{
	QJsonValue value(object);
	for (const QString &part : path.split('.', Qt::SkipEmptyParts)) {
		if (!value.isObject())
			return {};
		value = value.toObject().value(part);
	}
	return value;
}

QString alertUser(const QJsonObject &data)
{
	for (const QString &path : {QString("user_name"), QString("username"), QString("displayName"),
				   QString("user.displayName"), QString("user.name"), QString("sender.username")}) {
		const QString value = nestedValue(data, path).toVariant().toString();
		if (!value.isEmpty())
			return value;
	}
	const QString flat = data.value("user").toString();
	return flat.isEmpty() ? QStringLiteral("Someone") : flat;
}

} // namespace

QJsonObject matchAlertVariation(const QJsonArray &variations, const QString &eventKey,
				const QJsonObject &data, bool *matched)
{
	const QString requested = eventKey == "pulseweaver.overlay.trigger" ? data.value("event").toString() : eventKey;
	for (const QJsonValue &value : variations) {
		const QJsonObject variation = value.toObject();
		if (!requested.isEmpty() && variation.value("event").toString() != requested)
			continue;
		if (data.value("amount").toDouble() < variation.value("minAmount").toDouble() ||
		    data.value("viewers").toInt() < variation.value("minViewers").toInt())
			continue;
		if (matched)
			*matched = true;
		return variation;
	}
	if (requested.isEmpty() && !variations.isEmpty()) {
		if (matched)
			*matched = true;
		return variations.first().toObject();
	}
	if (matched)
		*matched = false;
	return {};
}

QString expandAlertTemplate(QString text, const QJsonObject &data)
{
	text.replace("{user}", alertUser(data));
	text.replace("{amount}", data.value("amount").toVariant().toString());
	text.replace("{viewers}", data.value("viewers").toVariant().toString());
	text.replace("{platform}", data.value("platform").toString());
	return text;
}

QJsonObject normalizeAlertEvent(QJsonObject data, const QString &canonical, const QString &platform)
{
	data.insert("platform", platform.toLower());
	data.insert("event", canonical);
	if (data.value("user_name").toString().isEmpty()) {
		for (const QString &path : {QString("user_login"), QString("from_broadcaster_user_name"),
					   QString("broadcaster_user_name"), QString("chatter_user_name"),
					   QString("authorDetails.displayName"), QString("member.displayName"),
					   QString("sender.username"), QString("sender.name")}) {
			const QString user = nestedValue(data, path).toVariant().toString();
			if (!user.isEmpty()) {
				data.insert("user_name", user);
				data.insert("user", user);
				break;
			}
		}
	}
	if (!data.contains("viewers")) {
		for (const QString &path : {QString("viewer_count"), QString("viewers_count"), QString("raid.viewers")}) {
			const QJsonValue viewers = nestedValue(data, path);
			if (!viewers.isUndefined() && !viewers.isNull()) {
				data.insert("viewers", viewers.toVariant().toLongLong());
				break;
			}
		}
	}
	if (!data.contains("amount")) {
		for (const QString &path : {QString("bits"), QString("gift_count"), QString("amount.value"),
					   QString("monetaryDetails.amountMicros")}) {
			const QJsonValue amount = nestedValue(data, path);
			if (!amount.isUndefined() && !amount.isNull()) {
				double number = amount.toVariant().toDouble();
				if (path.endsWith("amountMicros"))
					number /= 1000000.0;
				data.insert("amount", number);
				break;
			}
		}
	}
	return data;
}

bool AlertLane::enqueue(const AlertEvent &event, int maximumPending)
{
	if (pending.size() >= maximumPending)
		return false;
	pending.enqueue(event);
	return true;
}

std::optional<AlertEvent> AlertLane::beginNext()
{
	if (active || paused || pending.isEmpty())
		return std::nullopt;
	active = true;
	return pending.dequeue();
}

bool AlertLane::completeCurrent()
{
	if (!active)
		return false;
	active = false;
	return true;
}

bool AlertLane::skipCurrent()
{
	return completeCurrent();
}

void AlertLane::clearPending()
{
	pending.clear();
}

void AlertLane::setPaused(bool value)
{
	paused = value;
}

bool AlertLane::isPaused() const
{
	return paused;
}

bool AlertLane::isActive() const
{
	return active;
}

int AlertLane::pendingCount() const
{
	return pending.size();
}

void AlertLane::markReloadPending()
{
	reloadPending = true;
}

bool AlertLane::takeReloadPending()
{
	const bool result = reloadPending;
	reloadPending = false;
	return result;
}

} // namespace PulseOverlay
