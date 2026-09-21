#include "../../shared/qt/PulseAppCredentials.hpp"
#include "../../shared/qt/PulseYouTubeRegistration.hpp"
#include "../../shared/qt/PulseChatProtocol.hpp"
#include <QJsonDocument>
#include "YoutubeApiWrappers.hpp"

#include <utility/RemoteTextThread.hpp>
#include <utility/obf.h>
#include <widgets/OBSBasic.hpp>

#include <qt-wrappers.hpp>
#include <ui-config.h>

#include <QFile>
#include <QMimeDatabase>
#include <QUrl>

#include <algorithm>

#include "moc_YoutubeApiWrappers.cpp"

using namespace json11;

namespace {
using std::string_view_literals::operator""sv;

constexpr auto youtubeLiveStreamUrl = "https://www.googleapis.com/youtube/v3/liveStreams"sv;
constexpr auto youtubeLiveBroadcastUrl = "https://www.googleapis.com/youtube/v3/liveBroadcasts"sv;
constexpr auto youtubeLiveBroadcastTransitionUrl = "https://www.googleapis.com/youtube/v3/liveBroadcasts/transition"sv;
constexpr auto youtubeLiveBroadcastBindUrl = "https://www.googleapis.com/youtube/v3/liveBroadcasts/bind"sv;
constexpr auto youtubeLiveChatMessagesUrl = "https://www.googleapis.com/youtube/v3/liveChat/messages"sv;
constexpr auto youtubeLiveChatBansUrl = "https://www.googleapis.com/youtube/v3/liveChat/bans"sv;
constexpr auto youtubeSubscriptionsUrl = "https://www.googleapis.com/youtube/v3/subscriptions"sv;

constexpr auto youtubeLiveChannelUrl = "https://www.googleapis.com/youtube/v3/channels"sv;
constexpr auto youtubeLiveTokenUrl = "https://oauth2.googleapis.com/token"sv;
constexpr auto youtubeLiveVideoCategoriesUrl = "https://www.googleapis.com/youtube/v3/videoCategories"sv;
constexpr auto youtubeLiveVideosUrl = "https://www.googleapis.com/youtube/v3/videos"sv;
constexpr auto youtubeLiveThumbnailUrl = "https://www.googleapis.com/upload/youtube/v3/thumbnails/set"sv;

constexpr auto defaultBroadcastsPerQuery = 50; // acceptable values are 0 to 50, inclusive
} // namespace

bool IsYouTubeService(const std::string &service)
{
	auto it = find_if(youtubeServices.begin(), youtubeServices.end(),
			  [&service](const Auth::Def &yt) { return service == yt.service; });
	return it != youtubeServices.end();
}
bool IsUserSignedIntoYT()
{
	Auth *auth = OBSBasic::Get()->GetAuth();
	if (auth) {
		YoutubeApiWrappers *apiYouTube(dynamic_cast<YoutubeApiWrappers *>(auth));
		if (apiYouTube) {
			return true;
		}
	}
	return false;
}

bool YoutubeApiWrappers::GetTranslatedError(QString &error_message)
{
	const QString errorKey = "YouTube.Errors." + lastErrorReason.toUtf8();
	const QString translated = QTStr(QT_TO_UTF8(errorKey));
	// No translation found
	if (translated.startsWith("YouTube.Errors.")) {
		return false;
	}
	error_message = translated;
	return true;
}

YoutubeApiWrappers::YoutubeApiWrappers(const Def &d) : YoutubeAuth(d) {}

bool YoutubeApiWrappers::TryInsertCommand(const char *url, const char *content_type, std::string request_type,
					  const char *data, Json &json_out, long *error_code, int data_size)
{
	long httpStatusCode = 0;

#ifdef _DEBUG
	blog(LOG_DEBUG, "YouTube API command URL: %s", url);
	if (data && data[0] == '{') { // only log JSON data
		blog(LOG_DEBUG, "YouTube API command data: %s", data);
	}
#endif
	if (token.empty()) {
		lastErrorMessage = "Reconnect YouTube before using chat actions.";
		if (error_code) *error_code = 0;
		return false;
	}
	std::string output;
	std::string error;
	// Increase timeout by the time it takes to transfer `data_size` at 1 Mbps
	int timeout = 60 + data_size / 125000;
	bool success = GetRemoteFile(url, output, error, &httpStatusCode, content_type, request_type, data,
				     {"Authorization: Bearer " + token}, nullptr, timeout, false, data_size);
	if (error_code) {
		*error_code = httpStatusCode;
	}

	if (!success || (output.empty() && httpStatusCode != 204)) {
		lastErrorMessage = error.empty() ? QString("YouTube returned HTTP %1 with no response.").arg(httpStatusCode) : QString::fromStdString(error);
		if (!error.empty()) {
			blog(LOG_WARNING, "YouTube API request failed: %s", error.c_str());
		}
		return false;
	}
	if (output.empty()) {
		json_out = Json::object{};
		return httpStatusCode < 400;
	}

	json_out = Json::parse(output, error);
#ifdef _DEBUG
	blog(LOG_DEBUG, "YouTube API command answer: %s", json_out.dump().c_str());
#endif
	if (!error.empty()) {
		return false;
	}
	return httpStatusCode < 400;
}

bool YoutubeApiWrappers::UpdateAccessToken()
{
	if (refresh_token.empty()) {
		return false;
	}

	const auto registration = PulseYouTubeRegistration::current();
	std::string clientid = registration.clientId.toStdString();
	std::string secret = registration.clientSecret.toStdString();

	std::string r_token = QUrl::toPercentEncoding(refresh_token.c_str()).toStdString();
	QString data = QString("client_id=%1&refresh_token=%2&grant_type=refresh_token")
			       .arg(QString(clientid.c_str()), QString(r_token.c_str()));
	if (!secret.empty())
		data += "&client_secret=" + QString::fromLatin1(QUrl::toPercentEncoding(QString::fromStdString(secret)));
	Json json_out;
	bool success = TryInsertCommand(youtubeLiveTokenUrl.data(), "application/x-www-form-urlencoded", "",
					QT_TO_UTF8(data), json_out);

	if (!success || json_out.object_items().find("error") != json_out.object_items().end()) {
		return false;
	}
	token = json_out["access_token"].string_value();
	return token.empty() ? false : true;
}

bool YoutubeApiWrappers::InsertCommand(const char *url, const char *content_type, std::string request_type,
				       const char *data, Json &json_out, int data_size)
{
	long error_code = 0;
	lastErrorMessage.clear();
	lastErrorReason.clear();
	bool success = TryInsertCommand(url, content_type, request_type, data, json_out, &error_code, data_size);

	if (error_code == 401) {
		// Attempt to update access token and try again
		if (!UpdateAccessToken()) {
			return false;
		}
		success = TryInsertCommand(url, content_type, request_type, data, json_out, &error_code, data_size);
	}

	if (json_out.object_items().find("error") != json_out.object_items().end()) {
		blog(LOG_ERROR, "YouTube API error:\n\tHTTP status: %ld\n\tURL: %s\n\tJSON: %s", error_code, url,
		     json_out.dump().c_str());

		lastError = json_out["error"]["code"].int_value();
		lastErrorReason = QString(json_out["error"]["errors"][0]["reason"].string_value().c_str());
		lastErrorMessage = QString(json_out["error"]["message"].string_value().c_str());

		// The existence of an error implies non-success even if the HTTP status code disagrees.
		success = false;
	}
	return success;
}

bool YoutubeApiWrappers::GetChannelDescription(ChannelDescription &channel_description)
{
	lastErrorMessage.clear();
	lastErrorReason.clear();

	const std::string url =
		std::string(youtubeLiveChannelUrl) + "?part=snippet,contentDetails,statistics&mine=true";
	Json json_out;
	if (!InsertCommand(url.c_str(), "application/json", "", nullptr, json_out)) {
		return false;
	}

	if (json_out["pageInfo"]["totalResults"].int_value() == 0) {
		lastErrorMessage = QTStr("YouTube.Auth.NoChannels");
		return false;
	}

	channel_description.id = QString(json_out["items"][0]["id"].string_value().c_str());
	channel_description.title = QString(json_out["items"][0]["snippet"]["title"].string_value().c_str());
	return channel_description.id.isEmpty() ? false : true;
}

bool YoutubeApiWrappers::InsertBroadcast(BroadcastDescription &broadcast)
{
	lastErrorMessage.clear();
	lastErrorReason.clear();
	const std::string url = std::string(youtubeLiveBroadcastUrl) + "?part=snippet,status,contentDetails";
	const Json data = Json::object{
		{"snippet",
		 Json::object{
			 {"title", QT_TO_UTF8(broadcast.title)},
			 {"description", QT_TO_UTF8(broadcast.description)},
			 {"scheduledStartTime", QT_TO_UTF8(broadcast.schedul_date_time)},
		 }},
		{"status",
		 Json::object{
			 {"privacyStatus", QT_TO_UTF8(broadcast.privacy)},
			 {"selfDeclaredMadeForKids", broadcast.made_for_kids},
		 }},
		{"contentDetails",
		 Json::object{
			 {"latencyPreference", QT_TO_UTF8(broadcast.latency)},
			 {"enableAutoStart", broadcast.auto_start},
			 {"enableAutoStop", broadcast.auto_stop},
			 {"enableDvr", broadcast.dvr},
			 {"projection", QT_TO_UTF8(broadcast.projection)},
			 {
				 "monitorStream",
				 Json::object{
					 {"enableMonitorStream", false},
				 },
			 },
		 }},
	};
	Json json_out;
	if (!InsertCommand(url.c_str(), "application/json", "", data.dump().c_str(), json_out)) {
		return false;
	}
	broadcast.id = QString(json_out["id"].string_value().c_str());
	return broadcast.id.isEmpty() ? false : true;
}

bool YoutubeApiWrappers::InsertStream(StreamDescription &stream)
{
	lastErrorMessage.clear();
	lastErrorReason.clear();
	const std::string url = std::string(youtubeLiveStreamUrl) + "?part=snippet,cdn,status,contentDetails";
	const Json data = Json::object{
		{"snippet",
		 Json::object{
			 {"title", QT_TO_UTF8(stream.title)},
		 }},
		{"cdn",
		 Json::object{
			 {"frameRate", "variable"},
			 {"ingestionType", "rtmp"},
			 {"resolution", "variable"},
		 }},
		{"contentDetails", Json::object{{"isReusable", false}}},
	};
	Json json_out;
	if (!InsertCommand(url.c_str(), "application/json", "", data.dump().c_str(), json_out)) {
		return false;
	}
	stream.id = QString(json_out["id"].string_value().c_str());
	stream.name = QString(json_out["cdn"]["ingestionInfo"]["streamName"].string_value().c_str());
	return stream.id.isEmpty() ? false : true;
}

bool YoutubeApiWrappers::BindStream(const QString broadcast_id, const QString stream_id)
{
	lastErrorMessage.clear();
	lastErrorReason.clear();
	// TODO: Use std::format with std::string instead of QString::arg with C++20
	const QString url_template = QString(youtubeLiveBroadcastBindUrl.data()) +
				     "?id=%1&streamId=%2&part=id,snippet,contentDetails,status";
	const QString url = url_template.arg(broadcast_id, stream_id);
	const Json data = Json::object{};
	this->broadcast_id = broadcast_id;
	Json json_out;
	return InsertCommand(QT_TO_UTF8(url), "application/json", "", data.dump().c_str(), json_out);
}

bool YoutubeApiWrappers::GetBroadcastsList(Json &json_out, const QString &page, const QString &status)
{
	lastErrorMessage.clear();
	lastErrorReason.clear();
	std::string url = std::string(youtubeLiveBroadcastUrl) +
			  "?part=snippet,contentDetails,status&broadcastType=all&maxResults=" +
			  std::to_string(defaultBroadcastsPerQuery);

	if (status.isEmpty()) {
		url += "&mine=true";
	} else {
		url += "&broadcastStatus=" + status.toStdString();
	}

	if (!page.isEmpty()) {
		url += "&pageToken=" + page.toStdString();
	}
	return InsertCommand(url.c_str(), "application/json", "", nullptr, json_out);
}

bool YoutubeApiWrappers::GetVideoCategoriesList(QVector<CategoryDescription> &category_list_out)
{
	lastErrorMessage.clear();
	lastErrorReason.clear();
	// TODO: Use std::format with C++20
	const QString url_template =
		QString(youtubeLiveVideoCategoriesUrl.data()) + "?part=snippet&regionCode=%1&hl=%2";

	/*
	 * All OBS locale regions aside from "US" are missing category id 29
	 * ("Nonprofits & Activism"), but it is still available to channels
	 * set to those regions via the YouTube Studio website.
	 * To work around this inconsistency with the API all locales will
	 * use the "US" region and only set the language part for localisation.
	 * It is worth noting that none of the regions available on YouTube
	 * feature any category not also available to the "US" region.
	 */
	QString url = url_template.arg("US", QLocale().name());

	Json json_out;
	if (!InsertCommand(QT_TO_UTF8(url), "application/json", "", nullptr, json_out)) {
		if (lastErrorReason != "unsupportedLanguageCode" && lastErrorReason != "invalidLanguage") {
			return false;
		}
		// Try again with en-US if YouTube error indicates an unsupported locale
		url = url_template.arg("US", "en_US");
		if (!InsertCommand(QT_TO_UTF8(url), "application/json", "", nullptr, json_out)) {
			return false;
		}
	}
	category_list_out = {};
	for (auto &j : json_out["items"].array_items()) {
		// Assignable only.
		if (j["snippet"]["assignable"].bool_value()) {
			category_list_out.push_back(
				{j["id"].string_value().c_str(), j["snippet"]["title"].string_value().c_str()});
		}
	}
	return category_list_out.isEmpty() ? false : true;
}

bool YoutubeApiWrappers::SetVideoCategory(const QString &video_id, const QString &video_title,
					  const QString &video_description, const QString &categorie_id)
{
	lastErrorMessage.clear();
	lastErrorReason.clear();
	const std::string url = std::string(youtubeLiveVideosUrl) + "?part=snippet";
	const Json data = Json::object{
		{"id", QT_TO_UTF8(video_id)},
		{"snippet",
		 Json::object{
			 {"title", QT_TO_UTF8(video_title)},
			 {"description", QT_TO_UTF8(video_description)},
			 {"categoryId", QT_TO_UTF8(categorie_id)},
		 }},
	};
	Json json_out;
	return InsertCommand(url.c_str(), "application/json", "PUT", data.dump().c_str(), json_out);
}

bool YoutubeApiWrappers::SetVideoThumbnail(const QString &video_id, const QString &thumbnail_file)
{
	lastErrorMessage.clear();
	lastErrorReason.clear();

	// Make sure the file hasn't been deleted since originally selecting it
	if (!QFile::exists(thumbnail_file)) {
		lastErrorMessage = QTStr("YouTube.Actions.Error.FileMissing");
		return false;
	}

	QFile thumbFile(thumbnail_file);
	if (!thumbFile.open(QFile::ReadOnly)) {
		lastErrorMessage = QTStr("YouTube.Actions.Error.FileOpeningFailed");
		return false;
	}

	const QByteArray fileContents = thumbFile.readAll();
	const QString mime = QMimeDatabase().mimeTypeForData(fileContents).name();

	const std::string url = std::string(youtubeLiveThumbnailUrl) + "?videoId=" + video_id.toStdString();
	Json json_out;
	return InsertCommand(url.c_str(), QT_TO_UTF8(mime), "POST", fileContents.constData(), json_out,
			     fileContents.size());
}

bool YoutubeApiWrappers::StartBroadcast(const QString &broadcast_id)
{
	lastErrorMessage.clear();
	lastErrorReason.clear();

	Json json_out;
	if (!FindBroadcast(broadcast_id, json_out)) {
		return false;
	}

	auto lifeCycleStatus = json_out["items"][0]["status"]["lifeCycleStatus"].string_value();

	if (lifeCycleStatus == "live" || lifeCycleStatus == "liveStarting") {
		// Broadcast is already (going to be) live
		return true;
	} else if (lifeCycleStatus == "testStarting") {
		// User will need to wait a few seconds before attempting to start broadcast
		lastErrorMessage = QTStr("YouTube.Actions.Error.BroadcastTestStarting");
		lastErrorReason.clear();
		return false;
	}

	// Only reset if broadcast has monitoring enabled and is not already in "testing" mode
	auto monitorStreamEnabled =
		json_out["items"][0]["contentDetails"]["monitorStream"]["enableMonitorStream"].bool_value();
	if (lifeCycleStatus != "testing" && monitorStreamEnabled && !ResetBroadcast(broadcast_id, json_out)) {
		return false;
	}

	// TODO: Use std::format with C++20
	const QString url_template =
		QString(youtubeLiveBroadcastTransitionUrl.data()) + "?id=%1&broadcastStatus=%2&part=status";
	const QString live = url_template.arg(broadcast_id, "live");
	bool success = InsertCommand(QT_TO_UTF8(live), "application/json", "POST", "{}", json_out);
	// Return a success if the command failed, but was redundant (broadcast already live)
	return success || lastErrorReason == "redundantTransition";
}

bool YoutubeApiWrappers::StartLatestBroadcast()
{
	return StartBroadcast(this->broadcast_id);
}

bool YoutubeApiWrappers::StopBroadcast(const QString &broadcast_id)
{
	lastErrorMessage.clear();
	lastErrorReason.clear();

	const QString url_template =
		QString(youtubeLiveBroadcastTransitionUrl.data()) + "?id=%1&broadcastStatus=complete&part=status";
	const QString url = url_template.arg(broadcast_id);
	Json json_out;
	bool success = InsertCommand(QT_TO_UTF8(url), "application/json", "POST", "{}", json_out);
	// Return a success if the command failed, but was redundant (broadcast already stopped)
	return success || lastErrorReason == "redundantTransition";
}

bool YoutubeApiWrappers::DeleteBroadcast(const QString &broadcast_id)
{
	lastErrorMessage.clear();
	lastErrorReason.clear();
	const QString url = QString::fromUtf8(youtubeLiveBroadcastUrl.data()) + "?id=" +
		QString::fromLatin1(QUrl::toPercentEncoding(broadcast_id));
	Json json_out;
	return InsertCommand(QT_TO_UTF8(url), "application/json", "DELETE", nullptr, json_out);
}

bool YoutubeApiWrappers::FinishBroadcast(const QString &broadcast_id)
{
	lastErrorMessage.clear();
	lastErrorReason.clear();
	Json json_out;
	if (!FindBroadcast(broadcast_id, json_out) || json_out["items"].array_items().empty())
		return false;
	const std::string status = json_out["items"][0]["status"]["lifeCycleStatus"].string_value();
	if (status == "complete")
		return true;
	if (status == "live" || status == "liveStarting" || status == "testing" || status == "testStarting")
		return StopBroadcast(broadcast_id);
	/* A broadcast which never reached a live lifecycle has no replay to
	 * preserve. Remove it so it cannot remain as a phantom Upcoming event. */
	return DeleteBroadcast(broadcast_id);
}

bool YoutubeApiWrappers::StopLatestBroadcast()
{
	return StopBroadcast(this->broadcast_id);
}

bool YoutubeApiWrappers::GetLiveChatId(const QString &broadcast_id, QString &chat_id)
{
	lastErrorMessage.clear();
	lastErrorReason.clear();
	Json json;
	if (!FindBroadcast(broadcast_id, json))
		return false;
	chat_id = QString::fromStdString(json["items"][0]["snippet"]["liveChatId"].string_value());
	return !chat_id.isEmpty();
}

bool YoutubeApiWrappers::GetLiveChatMessages(const QString &chat_id, QString &page_token,
		QVector<YoutubeChatEvent> &events, int &poll_interval_ms)
{
	QString url = QString::fromUtf8(youtubeLiveChatMessagesUrl.data()) +
		"?part=id,snippet,authorDetails&maxResults=200&liveChatId=" +
		QString::fromLatin1(QUrl::toPercentEncoding(chat_id));
	if (!page_token.isEmpty())
		url += "&pageToken=" + QString::fromLatin1(QUrl::toPercentEncoding(page_token));
	Json json;
	if (!InsertCommand(QT_TO_UTF8(url), "application/json", "", nullptr, json))
		return false;
	page_token = QString::fromStdString(json["nextPageToken"].string_value());
	poll_interval_ms = std::max(1000, json["pollingIntervalMillis"].int_value());
	events.clear();
	for (const Json &item : json["items"].array_items()) {
		const Json snippet = item["snippet"];
		const Json author = item["authorDetails"];
		QStringList badges;
		if (author["isChatOwner"].bool_value()) badges << "owner";
		if (author["isChatModerator"].bool_value()) badges << "mod";
		if (author["isChatSponsor"].bool_value()) badges << "member";
		if (author["isVerified"].bool_value()) badges << "verified";
		events.push_back({QString::fromStdString(item["id"].string_value()),
			QString::fromStdString(snippet["type"].string_value()),
			QString::fromStdString(author["displayName"].string_value()),
			QString::fromStdString(snippet["displayMessage"].string_value()),
			QString::fromStdString(snippet["superChatDetails"]["amountDisplayString"].string_value()),
			QString::fromStdString(author["channelId"].string_value()), {}, badges,
			QString::fromStdString(snippet["messageDeletedDetails"]["deletedMessageId"].string_value()),
			QString::fromStdString(snippet["userBannedDetails"]["bannedUserDetails"]["channelId"].string_value())});
	}
	return true;
}

bool YoutubeApiWrappers::DeleteLiveChatMessage(const QString &message_id)
{
	if (message_id.isEmpty())
		return false;
	Json result;
	const std::string url = std::string(youtubeLiveChatMessagesUrl) + "?id=" + QUrl::toPercentEncoding(message_id).toStdString();
	return InsertCommand(url.c_str(), "application/json", "DELETE", nullptr, result);
}

bool YoutubeApiWrappers::ModerateLiveChatUser(const QString &chat_id, const QString &channel_id, int duration_seconds)
{
	if (chat_id.isEmpty() || channel_id.isEmpty())
		return false;
	Json result;
    const std::string url = std::string(youtubeLiveChatBansUrl) + "?part=snippet";
    const std::string payload = QJsonDocument(PulseChat::youtubeBanBody(chat_id, channel_id, duration_seconds)).toJson(QJsonDocument::Compact).toStdString();
    return InsertCommand(url.c_str(), "application/json", "", payload.c_str(), result);
}

bool YoutubeApiWrappers::SendLiveChatMessage(const QString &chat_id, const QString &message)
{
	const std::string url = std::string(youtubeLiveChatMessagesUrl) + "?part=snippet";
	const Json data = Json::object{{"snippet", Json::object{{"liveChatId", QT_TO_UTF8(chat_id)},
		{"type", "textMessageEvent"}, {"textMessageDetails", Json::object{{"messageText", QT_TO_UTF8(message)}}}}}};
	Json result;
	return InsertCommand(url.c_str(), "application/json", "", data.dump().c_str(), result);
}

bool YoutubeApiWrappers::GetRecentSubscribers(QVector<YoutubeSubscriber> &subscribers)
{
	/* YouTube rejects mine=true together with myRecentSubscribers=true.  The
	 * latter already identifies the authenticated creator's recent public
	 * subscribers, so use the documented mutually-compatible form. */
	const std::string url = std::string(youtubeSubscriptionsUrl) +
		"?part=subscriberSnippet&myRecentSubscribers=true&maxResults=50";
	Json json;
	if (!InsertCommand(url.c_str(), "application/json", "", nullptr, json))
		return false;
	subscribers.clear();
	for (const Json &item : json["items"].array_items()) {
		const Json snippet = item["subscriberSnippet"];
		subscribers.push_back({QString::fromStdString(snippet["channelId"].string_value()),
			QString::fromStdString(snippet["title"].string_value())});
	}
	return true;
}

void YoutubeApiWrappers::SetBroadcastId(QString &broadcast_id)
{
	this->broadcast_id = broadcast_id;
}

QString YoutubeApiWrappers::GetBroadcastId()
{
	return this->broadcast_id;
}

bool YoutubeApiWrappers::ResetBroadcast(const QString &broadcast_id, json11::Json &json_out)
{
	lastErrorMessage.clear();
	lastErrorReason.clear();

	auto snippet = json_out["items"][0]["snippet"];
	auto status = json_out["items"][0]["status"];
	auto contentDetails = json_out["items"][0]["contentDetails"];
	auto monitorStream = contentDetails["monitorStream"];

	const Json data = Json::object{
		{"id", QT_TO_UTF8(broadcast_id)},
		{"snippet",
		 Json::object{
			 {"title", snippet["title"]},
			 {"description", snippet["description"]},
			 {"scheduledStartTime", snippet["scheduledStartTime"]},
			 {"scheduledEndTime", snippet["scheduledEndTime"]},
		 }},
		{"status",
		 Json::object{
			 {"privacyStatus", status["privacyStatus"]},
			 {"madeForKids", status["madeForKids"]},
			 {"selfDeclaredMadeForKids", status["selfDeclaredMadeForKids"]},
		 }},
		{"contentDetails",
		 Json::object{
			 {
				 "monitorStream",
				 Json::object{
					 {"enableMonitorStream", false},
					 {"broadcastStreamDelayMs", monitorStream["broadcastStreamDelayMs"]},
				 },
			 },
			 {"enableAutoStart", contentDetails["enableAutoStart"]},
			 {"enableAutoStop", contentDetails["enableAutoStop"]},
			 {"enableClosedCaptions", contentDetails["enableClosedCaptions"]},
			 {"enableDvr", contentDetails["enableDvr"]},
			 {"enableContentEncryption", contentDetails["enableContentEncryption"]},
			 {"enableEmbed", contentDetails["enableEmbed"]},
			 {"recordFromStart", contentDetails["recordFromStart"]},
			 {"startWithSlate", contentDetails["startWithSlate"]},
		 }},
	};

	const std::string put = std::string(youtubeLiveBroadcastUrl) + "?part=id,snippet,contentDetails,status";
	return InsertCommand(put.c_str(), "application/json", "PUT", data.dump().c_str(), json_out);
}

bool YoutubeApiWrappers::FindBroadcast(const QString &id, json11::Json &json_out)
{
	lastErrorMessage.clear();
	lastErrorReason.clear();
	std::string url = std::string(youtubeLiveBroadcastUrl) +
			  "?part=id,snippet,contentDetails,status&broadcastType=all&maxResults=1";
	url += "&id=" + id.toStdString();

	if (!InsertCommand(url.c_str(), "application/json", "", nullptr, json_out)) {
		return false;
	}

	auto items = json_out["items"].array_items();
	if (items.size() != 1) {
		lastErrorMessage = QTStr("YouTube.Actions.Error.BroadcastNotFound");
		return false;
	}

	return true;
}

bool YoutubeApiWrappers::FindStream(const QString &id, json11::Json &json_out)
{
	lastErrorMessage.clear();
	lastErrorReason.clear();
	std::string url = std::string(youtubeLiveStreamUrl) + "?part=id,snippet,cdn,status&maxResults=1";
	url += "&id=" + id.toStdString();

	if (!InsertCommand(url.c_str(), "application/json", "", nullptr, json_out)) {
		return false;
	}

	auto items = json_out["items"].array_items();
	if (items.size() != 1) {
		lastErrorMessage = "No active broadcast found.";
		return false;
	}

	return true;
}
