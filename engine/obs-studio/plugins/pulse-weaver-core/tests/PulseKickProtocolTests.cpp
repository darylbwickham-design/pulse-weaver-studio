#include "../../../shared/qt/PulseKickProtocol.hpp"

#include <QCoreApplication>
#include <QJsonDocument>

#include <cstdio>

static int failures = 0;
static void check(bool ok, const char *name)
{
	if (!ok) {
		std::fprintf(stderr, "FAIL: %s\n", name);
		++failures;
	}
}

int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	const qint64 now = 1790000000;
	const auto grant = PulseKick::parseGrant(QJsonObject{{"data", QJsonObject{
		{"active", true}, {"token_type", "user"}, {"client_id", "app-id"},
		{"scope", "channel:write moderation:chat_message:manage"}, {"exp", now + 300}}}});
	check(PulseKick::validUserGrant(grant, "app-id", now), "valid connected user grant");
	check(!grant.scopes.contains("moderation:ban"), "missing ban grant detected");
	check(!PulseKick::validUserGrant(grant, "app-id", now + 301), "expired grant rejected");
	check(!PulseKick::validUserGrant(grant, "another-app", now), "other app rejected");
	check(!PulseKick::validUserGrant(PulseKick::parseGrant(QJsonObject{{"data", QJsonObject{
		{"active", true}, {"token_type", "user"}, {"exp", now + 300}}}}), "app-id", now),
		"unattributed grant rejected");
	const auto appGrant = PulseKick::parseGrant(QJsonObject{{"data", QJsonObject{
		{"active", true}, {"token_type", "app"}, {"exp", now + 300}}}});
	check(!PulseKick::validUserGrant(appGrant, "app-id", now), "app-only token rejected");

	const auto rotated = PulseKick::replacementTokens(QJsonObject{{"access_token", "new-access"},
		{"refresh_token", "new-refresh"}, {"token_type", "Bearer"}, {"expires_in", 3600}},
		"old-refresh", 100000);
	check(rotated && rotated->access == "new-access" && rotated->refresh == "new-refresh" &&
		rotated->expiresAtMs == 3700000, "refresh rotation persisted with expiry");
	check(!PulseKick::replacementTokens(QJsonObject{{"error", "invalid_grant"}}, "old-refresh", 0),
		"rejected refresh is not treated as success");

	const QJsonObject title = PulseKick::titleBody("New title");
	check(title.size() == 1 && title.value("stream_title").toString() == "New title",
		"title request changes only title");
	const QJsonObject category = PulseKick::categoryBody(42);
	check(category.size() == 1 && category.value("category_id").toVariant().toLongLong() == 42 &&
		!category.contains("stream_title") && !category.contains("custom_tags"),
		"category request changes only category by numeric ID");
	check(PulseKick::accepted("title", 204) && PulseKick::accepted("category", 204) &&
		PulseKick::accepted("delete_message", 204),
		"empty 204 success accepted");
	check(!PulseKick::accepted("title", 200) && !PulseKick::accepted("category", 200) &&
		!PulseKick::accepted("category", 403) && !PulseKick::accepted("ban", 403),
		"unexpected and rejected status not treated as success");
	const QString error = PulseKick::safeError(403,
		R"({"message":"Forbidden Bearer private-access"})", {}, {"private-access"});
	check(error.contains("HTTP 403") && error.contains("Forbidden") && !error.contains("private-access"),
		"upstream error propagated and token redacted");
	check(PulseKick::safeError(502, "upstream temporarily unavailable").contains("upstream temporarily unavailable"),
		"non-JSON relay error propagated");
	check(PulseKick::safeError(204, {}).contains("HTTP 204"), "empty response parsed safely");
	return failures ? 1 : 0;
}
