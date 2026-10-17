#pragma once
#include <QColor>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkRequest>
#include <QUrl>
#include <algorithm>

namespace PulseLightRequest {
struct Request {
	QNetworkRequest network;
	QByteArray method = "POST";
	QJsonObject body;
	QString error;
	bool valid() const { return error.isEmpty(); }
};
inline Request make(const QString &provider, const QString &endpoint, const QString &credential,
		    const QString &entity, const QColor &colour, int brightness, bool on)
{
	Request result;
	QUrl url(endpoint.contains("://") ? endpoint : "http://" + endpoint);
	if (!url.isValid() || url.host().isEmpty() || !url.userInfo().isEmpty() ||
	    (url.scheme() != "http" && url.scheme() != "https")) {
		result.error = "Enter a valid HTTP(S) device address."; return result;
	}
	if (!colour.isValid()) { result.error = "Choose a valid colour."; return result; }
	brightness = std::clamp(brightness, 1, 255);
	const QJsonArray rgb{colour.red(), colour.green(), colour.blue()};
	if (provider == "WLED") {
		url.setPath("/json/state");
		result.body = {{"on", on}, {"bri", brightness}, {"seg", QJsonArray{QJsonObject{{"col", QJsonArray{QJsonValue(rgb)}}}}}};
	} else if (provider == "Home Assistant") {
		if (credential.trimmed().isEmpty() || entity.trimmed().isEmpty() || credential.contains('\r') || credential.contains('\n')) {
			result.error = "Set the Home Assistant token and light entity in Connection details."; return result;
		}
		url.setPath(on ? "/api/services/light/turn_on" : "/api/services/light/turn_off");
		result.network.setRawHeader("Authorization", "Bearer " + credential.toUtf8());
		result.body = {{"entity_id", entity.trimmed()}};
		if (on) { result.body.insert("brightness", brightness); result.body.insert("rgb_color", rgb); }
	} else if (provider == "Philips Hue") {
		if (credential.trimmed().isEmpty() || entity.trimmed().isEmpty() || credential.contains('/') || entity.contains('/')) {
			result.error = "Set the Hue bridge username and light ID in Connection details."; return result;
		}
		url.setScheme("https"); result.method = "PUT";
		url.setPath("/api/" + credential.trimmed() + "/lights/" + entity.trimmed() + "/state");
		result.body = {{"on", on}, {"bri", std::min(brightness, 254)},
			{"hue", int(std::max(0.0f, colour.hsvHueF()) * 65535)}, {"sat", int(colour.hsvSaturationF() * 254)}};
	} else if (provider == "HTTP / DIY" || provider == "Display / Pixel device") {
		const auto path = entity.trimmed();
		url.setPath(path.startsWith('/') ? path : "/" + (path.isEmpty() ? QString("pulseweaver") : path));
		result.body = {{"on", on}, {"bri", brightness}, {"rgb", rgb}};
	} else { result.error = "Choose a supported device provider."; return result; }
	url.setQuery(QString()); url.setFragment(QString());
	result.network.setUrl(url);
	result.network.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
	// Preserve normal Qt TLS validation; never bypass bridge certificate errors.
	result.network.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
	return result;
}
inline bool accepted(const QString &provider, int status, bool transportOk, const QByteArray &body)
{
	if (!transportOk || status < 200 || status >= 300) return false;
	if (provider != "Philips Hue") return true;
	const auto result = QJsonDocument::fromJson(body);
	if (!result.isArray() || result.array().isEmpty()) return false;
	for (const auto &entry : result.array())
		if (!entry.isObject() || entry.toObject().contains("error") || !entry.toObject().contains("success")) return false;
	return true;
}
} // namespace PulseLightRequest
