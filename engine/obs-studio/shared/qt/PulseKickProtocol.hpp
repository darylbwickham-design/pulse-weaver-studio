#pragma once

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <QStringList>

#include <optional>

namespace PulseKick {

inline constexpr auto RequiredScopes = "user:read channel:read channel:write chat:write streamkey:read "
					  "events:subscribe moderation:ban moderation:chat_message:manage";

struct Grant {
	bool active = false;
	QString tokenType;
	QString clientId;
	QSet<QString> scopes;
	qint64 expiresAtSeconds = 0;
};

inline Grant parseGrant(const QJsonObject &reply)
{
	const QJsonObject data = reply.value("data").toObject();
	Grant grant;
	grant.active = data.value("active").toBool();
	grant.tokenType = data.value("token_type").toString();
	grant.clientId = data.value("client_id").toString();
	grant.expiresAtSeconds = data.value("exp").toVariant().toLongLong();
	const QJsonValue scope = data.value("scope");
	if (scope.isString()) {
		for (const QString &value : scope.toString().split(' ', Qt::SkipEmptyParts))
			grant.scopes.insert(value);
	} else if (scope.isArray()) {
		for (const QJsonValue &value : scope.toArray())
			if (value.isString()) grant.scopes.insert(value.toString());
	}
	return grant;
}

inline bool validUserGrant(const Grant &grant, const QString &clientId, qint64 nowSeconds)
{
	return grant.active && grant.tokenType.compare("user", Qt::CaseInsensitive) == 0 &&
	       grant.expiresAtSeconds > nowSeconds &&
	       !clientId.isEmpty() && grant.clientId == clientId;
}

struct Tokens {
	QString access;
	QString refresh;
	qint64 expiresAtMs = 0;
};

inline std::optional<Tokens> replacementTokens(const QJsonObject &reply, const QString &oldRefresh,
						       qint64 nowMs)
{
	const QString access = reply.value("access_token").toString().trimmed();
	const QString refresh = reply.value("refresh_token").toString().trimmed();
	const QString type = reply.value("token_type").toString();
	const qint64 lifetime = reply.value("expires_in").toVariant().toLongLong();
	if (access.isEmpty() || (refresh.isEmpty() && oldRefresh.isEmpty()) || lifetime <= 0 ||
	    (!type.isEmpty() && type.compare("bearer", Qt::CaseInsensitive) != 0))
		return std::nullopt;
	return Tokens{access, refresh.isEmpty() ? oldRefresh : refresh, nowMs + lifetime * 1000};
}

inline QJsonObject titleBody(const QString &title)
{
	return {{"stream_title", title}};
}

inline bool accepted(const QString &action, int status)
{
	return action == "title" || action == "delete_message" ? status == 204 : status == 200;
}

inline QString safeError(int status, const QByteArray &body, const QString &networkError = {},
			 const QStringList &secrets = {})
{
	QString detail;
	QJsonParseError parseError{};
	const QJsonDocument parsed = QJsonDocument::fromJson(body, &parseError);
	if (parseError.error == QJsonParseError::NoError && parsed.isObject()) {
		const QJsonObject object = parsed.object();
		for (const QString &field : {QString("error_description"), QString("message"), QString("error")}) {
			const QJsonValue value = object.value(field);
			if (value.isString() && !value.toString().trimmed().isEmpty()) {
				detail = value.toString().trimmed();
				break;
			}
		}
	} else {
		const QString plain = QString::fromUtf8(body).trimmed();
		if (!plain.startsWith('<') && !plain.contains(QChar::Null)) detail = plain;
	}
	if (detail.isEmpty()) detail = status ? QStringLiteral("Kick returned no detail") : networkError;
	for (const QString &secret : secrets)
		if (!secret.isEmpty()) detail.replace(secret, "[redacted]", Qt::CaseSensitive);
	static const QRegularExpression labelledSecret(
		"(?i)(access_token|refresh_token|client_secret|stream_key|authorization)\\s*[:=]\\s*[^\\s,;]+", QRegularExpression::CaseInsensitiveOption);
	detail.replace(labelledSecret, "\\1=[redacted]");
	static const QRegularExpression bearer("(?i)bearer\\s+[A-Za-z0-9._~+/-]+", QRegularExpression::CaseInsensitiveOption);
	detail.replace(bearer, "Bearer [redacted]");
	detail.replace(QRegularExpression("[\\r\\n\\t]+"), " ");
	if (detail.size() > 180) detail = detail.left(180) + QChar(0x2026);
	return QString("HTTP %1: %2").arg(status ? QString::number(status) : QStringLiteral("network")).arg(detail);
}

} // namespace PulseKick
