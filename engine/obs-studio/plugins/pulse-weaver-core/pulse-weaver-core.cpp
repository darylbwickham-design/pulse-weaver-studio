#include "../../shared/qt/PulseAppCredentials.hpp"
#include "../../shared/qt/PulseLegal.hpp"
#include "../../shared/qt/PulsePlatformApplicationIds.hpp"
#include "../../shared/qt/PulseChat.hpp"
#include "../../shared/qt/PulseLumiaOutput.hpp"
#include "../../shared/qt/PulseOutputBitrates.hpp"
#include "pulse-lumia-bridge.hpp"
#include "pulse-motion-engine.hpp"
#include "pulse-overlay-runtime.hpp"
#include "pulse-overlay-alerts.hpp"
#include "pulse-runtime-safety.hpp"
#include "pulse-scene-item-ref.hpp"

#include <obs-module.h>
#include <obs-frontend-api.h>
#include <callback/calldata.h>
#include <callback/proc.h>
#include <callback/signal.h>
#include <util/bmem.h>
#include <util/platform.h>

#include <QAbstractSocket>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDesktopServices>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QHostAddress>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPointer>
#include <QPixmap>
#include <QScrollBar>
#include <QProgressBar>
#include <QProcess>
#include <QPushButton>
#include <QMenu>
#include <QQueue>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QNetworkAccessManager>
#include <QNetworkDiskCache>
#include <QNetworkInterface>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSet>
#include <QSignalBlocker>
#include <QSettings>
#include <QSaveFile>
#include <QSlider>
#include <QSpinBox>
#include <QStandardPaths>
#include <QSplitter>
#include <QTabWidget>
#include <QTableWidget>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTextEdit>
#include <QTextDocument>
#include <QTreeWidget>
#include <QUrl>
#include <QUrlQuery>
#include <QUuid>
#include <QVBoxLayout>
#include <QTimer>

#include <algorithm>
#include <atomic>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <Windows.h>
#include <wincrypt.h>
#include <winhttp.h>
#endif

OBS_DECLARE_MODULE()

MODULE_EXPORT const char *obs_module_description(void)
{
	return "Pulse Weaver native event, automation, API and multi-canvas core";
}

namespace {




enum PulseChatDataRole {
	PulseChatPlatformRole = Qt::UserRole + 140,
	PulseChatUserRole,
	PulseChatUserIdRole,
	PulseChatMessageIdRole,
	PulseChatTextRole,
};

static QColor pulseChatColour(const QString &platform, const QString &requested)
{
	QColor colour(requested);
	if (colour.isValid() && colour.lightness() > 50)
		return colour;
	if (platform == "twitch") return QColor("#B78CFF");
	if (platform == "youtube") return QColor("#FF7B8C");
	if (platform == "kick") return QColor("#6EE7B7");
	return QColor("#D9E3F2");
}

static bool pulseChatAtBottom(QListWidget *feed)
{
	if (!feed || !feed->verticalScrollBar())
		return true;
	auto *bar = feed->verticalScrollBar();
	return bar->value() >= bar->maximum() - 3;
}

static QNetworkAccessManager &pulseChatImageNetwork()
{
	static QNetworkAccessManager manager;
	static const bool configured = [] {
		auto *cache = new QNetworkDiskCache(&manager);
		cache->setCacheDirectory(QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/pulseweaver-chat-images");
		cache->setMaximumCacheSize(32 * 1024 * 1024);
		manager.setCache(cache);
		return true;
	}();
	Q_UNUSED(configured);
	return manager;
}

static void pulseLoadChatImage(QLabel *target, const QUrl &url, int height)
{
	if (!target || !url.isValid())
		return;
	QPointer<QLabel> guardedTarget(target);
	QNetworkRequest request(url);
	request.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::PreferCache);
	QNetworkReply *reply = pulseChatImageNetwork().get(request);
	QObject::connect(reply, &QNetworkReply::finished, target, [reply, guardedTarget, height] {
		const QPixmap pixmap = QPixmap::fromImage(QImage::fromData(reply->readAll()));
		reply->deleteLater();
		if (guardedTarget && !pixmap.isNull())
			guardedTarget->setPixmap(pixmap.scaledToHeight(height, Qt::SmoothTransformation));
	});
}

static void pulseAppendUnifiedChat(QListWidget *feed, const QString &platform, const QString &user,
    const QString &message, const QString &colour = {}, const QStringList &badges = {},
    const QHash<QString, QUrl> &images = {}, const QString &userId = {}, const QString &messageId = {},
    bool own = false, const QJsonArray &fragments = {})
{
    PulseChat::append(feed, platform, user, message, colour, badges, images, userId, messageId, own, fragments);
}
bool pulseEncoderAvailable(const char *requestedId)
{
	const char *id = nullptr;
	for (size_t index = 0; obs_enum_encoder_types(index, &id); ++index) {
		if (id && strcmp(id, requestedId) == 0)
			return true;
	}
	return false;
}

obs_encoder_t *pulseCreateStreamingEncoder(const QByteArray &name, int bitrate)
{
	obs_data_t *settings = obs_data_create();
	obs_encoder_t *encoder = nullptr;
	if (pulseEncoderAvailable("obs_nvenc_h264_tex")) {
		obs_data_set_string(settings, "rate_control", "cbr");
		obs_data_set_int(settings, "bitrate", bitrate);
		obs_data_set_int(settings, "keyint_sec", 2);
		/* Additional live destinations favour encoder headroom. P4 single-pass
		 * keeps the same resolution, bitrate, profile and adaptive quantisation
		 * while avoiding the second analysis pass competing with the primary
		 * stream for GPU scheduling time. */
		obs_data_set_string(settings, "preset", "p4");
		obs_data_set_string(settings, "multipass", "disabled");
		/* Secondary platform outputs must never inherit OBS's look-ahead
		 * default. It consumes extra GPU scheduling time for little benefit at
		 * livestream bitrates, and becomes costly beside the primary encoder. */
		obs_data_set_bool(settings, "lookahead", false);
		obs_data_set_string(settings, "profile", "high");
		encoder = obs_video_encoder_create("obs_nvenc_h264_tex", name.constData(), settings, nullptr);
	}
	if (!encoder) {
		obs_data_clear(settings);
		obs_data_set_int(settings, "bitrate", bitrate);
		obs_data_set_int(settings, "keyint_sec", 2);
		obs_data_set_string(settings, "preset", "veryfast");
		obs_data_set_string(settings, "profile", "high");
		encoder = obs_video_encoder_create("obs_x264", name.constData(), settings, nullptr);
	}
	obs_data_release(settings);
	return encoder;
}

QString pulseSettingsPath()
{
	char *directory = obs_module_config_path(nullptr);
	if (directory) {
		os_mkdirs(directory);
		bfree(directory);
	}
	char *path = obs_module_config_path("pulse-weaver.ini");
	const QString result = path ? QString::fromUtf8(path) : QStringLiteral("pulse-weaver.ini");
	bfree(path);
	return result;
}

QString protectCredential(const QString &plain)
{
	if (plain.isEmpty())
		return {};
#ifdef _WIN32
	const QByteArray input = plain.toUtf8();
	DATA_BLOB in{DWORD(input.size()), reinterpret_cast<BYTE *>(const_cast<char *>(input.constData()))};
	DATA_BLOB out{};
	if (CryptProtectData(&in, L"Pulse Weaver isolated Twitch credential", nullptr, nullptr, nullptr,
			     CRYPTPROTECT_UI_FORBIDDEN, &out)) {
		const QByteArray encrypted(reinterpret_cast<const char *>(out.pbData), int(out.cbData));
		LocalFree(out.pbData);
		return QString::fromLatin1(encrypted.toBase64());
	}
#endif
	return {};
}

QString unprotectCredential(const QString &stored)
{
	if (stored.isEmpty())
		return {};
#ifdef _WIN32
	QByteArray input = QByteArray::fromBase64(stored.toLatin1());
	DATA_BLOB in{DWORD(input.size()), reinterpret_cast<BYTE *>(input.data())};
	DATA_BLOB out{};
	if (CryptUnprotectData(&in, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out)) {
		const QString plain = QString::fromUtf8(reinterpret_cast<const char *>(out.pbData), int(out.cbData));
		SecureZeroMemory(out.pbData, out.cbData);
		LocalFree(out.pbData);
		return plain;
	}
#endif
	return {};
}

QByteArray formBody(const QList<QPair<QString, QString>> &fields)
{
	QUrlQuery query;
	for (const auto &field : fields)
		query.addQueryItem(field.first, field.second);
	return query.query(QUrl::FullyEncoded).toUtf8();
}

QJsonObject flattenEvent(const QJsonObject &source)
{
	QJsonObject result;
	std::function<void(const QJsonObject &, const QString &)> visit = [&](const QJsonObject &object,
								       const QString &prefix) {
		for (auto it = object.begin(); it != object.end(); ++it) {
			const QString key = prefix.isEmpty() ? it.key() : prefix + "." + it.key();
			if (it->isObject())
				visit(it->toObject(), key);
			else if (it->isString())
				result.insert(key, it->toString());
			else if (it->isBool())
				result.insert(key, it->toBool());
			else if (it->isDouble())
				result.insert(key, it->toDouble());
			else
				result.insert(key, QString::fromUtf8(QJsonDocument(QJsonObject{{"value", *it}})
									       .toJson(QJsonDocument::Compact)));
		}
	};
	visit(source, {});
	return result;
}

QString canonicalEventKey(const QString &platform, const QString &type)
{
	const QString key = platform.toLower() + "." + type.toLower();
	if (key == "twitch.channel.follow" || key == "youtube.channel.subscribe" ||
	    key == "kick.channel.followed")
		return "audience.followed";
	if (key == "twitch.channel.subscribe" || key == "twitch.channel.subscription.message" ||
	    key == "youtube.membership.received" || key == "youtube.membership.milestone" ||
	    key == "kick.channel.subscription.new" || key == "kick.channel.subscription.renewal")
		return "support.paid_subscription";
	if (key == "twitch.channel.cheer" || key == "youtube.super_chat.received" ||
	    key == "youtube.super_sticker.received" || key == "kick.kicks.gifted")
		return "support.contribution";
	if (key.endsWith(".chat.message") || key.endsWith(".chat.message.sent"))
		return "chat.message";
	if (key == "twitch.channel.raid")
		return "audience.raid";
	return {};
}

class TwitchRuntime final : public QObject {
public:
	using EventCallback = std::function<void(const QString &, const QJsonObject &)>;
	using ChatCallback = std::function<void(const QString &, const QString &, const QJsonObject &)>;

	explicit TwitchRuntime(QObject *parent, EventCallback events, ChatCallback chats)
		: QObject(parent), eventCallback(std::move(events)), chatCallback(std::move(chats))
	{
		QSettings settings(pulseSettingsPath(), QSettings::IniFormat);
		// Twitch Device Code authentication supports public desktop clients. The
		// shipped Client ID is deliberately not editable; no app secret is held
		// by Pulse Weaver and streamer access/refresh tokens remain local.
		clientId = PulsePlatformApplicationIds::TwitchClientId();
		network.setTransferTimeout(10000);
		settings.remove("twitch/client_id");
		accessToken = unprotectCredential(settings.value("twitch/access_token").toString());
		refreshToken = unprotectCredential(settings.value("twitch/refresh_token").toString());
		pollTimer.setSingleShot(false);
		connect(&pollTimer, &QTimer::timeout, this, [this] { pollDeviceToken(); });
		retryTimer.setSingleShot(true);
		connect(&retryTimer, &QTimer::timeout, this, [this] { validateToken(); });
		tokenTimer.setSingleShot(true);
		connect(&tokenTimer, &QTimer::timeout, this, [this] { validateToken(false); });
		watchdog.setInterval(1000);
		connect(&watchdog, &QTimer::timeout, this, [this] {
			if (!stopping && lastSocketMessage > 0 && QDateTime::currentMSecsSinceEpoch() - lastSocketMessage > keepaliveMs) {
				stopSocket();
				scheduleReconnect("Twitch chat connection timed out.");
			}
		});
	}

	~TwitchRuntime() override { disconnectAll(); }

	void bindShell(QWidget *mainWindow)
	{
		if (!mainWindow)
			return;
		chatFeed = mainWindow->findChild<QListWidget *>("PulseWeaverChatFeed");
		chatInput = mainWindow->findChild<QLineEdit *>("PulseWeaverChatInput");
		chatSend = mainWindow->findChild<QPushButton *>("PulseWeaverChatSend");
		chatConnect = mainWindow->findChild<QPushButton *>("PulseWeaverChatConnect");
		chatStatus = mainWindow->findChild<QLabel *>("PulseWeaverChatStatus");
		if (chatFeed) {
			chatFeed->setContextMenuPolicy(Qt::CustomContextMenu);
			connect(chatFeed, &QListWidget::customContextMenuRequested, this,
				[this](const QPoint &position) { showChatActions(position); });
		}
		if (auto *provider = mainWindow->findChild<QComboBox *>("PulseWeaverChatProvider"))
			connect(provider, &QComboBox::currentIndexChanged, this, [this](int) { updateUi(); });
		if (chatSend)
			connect(chatSend, &QPushButton::clicked, this, [this] { sendFromComposer(); });
		if (chatInput)
			connect(chatInput, &QLineEdit::returnPressed, this, [this] { sendFromComposer(); });
		if (chatConnect)
			connect(chatConnect, &QPushButton::clicked, this, [this] { beginLogin(); });
		setStatus(accessToken.isEmpty() ? "Twitch chat is not connected." : "Validating saved Twitch connection…");
		if (!accessToken.isEmpty())
			validateToken();
	}

	void setConnectionWidgets(QLineEdit *client, QLabel *account, QLabel *state, QPushButton *connectButton,
				  QPushButton *disconnectButton)
	{
		clientField = client;
		accountLabel = account;
		connectionStatus = state;
		loginButton = connectButton;
		logoutButton = disconnectButton;
		if (clientField) {
			clientField->setText("Pulse Weaver public desktop app");
			clientField->setReadOnly(true);
			clientField->setToolTip("Twitch uses Pulse Weaver's registered public desktop application. No app secret is stored here.");
		}
		if (loginButton)
			connect(loginButton, &QPushButton::clicked, this, [this] { beginLogin(); });
		if (logoutButton)
			connect(logoutButton, &QPushButton::clicked, this, [this] { clearLogin(); });
		updateUi();
	}

	bool connected() const { return socketConnected && !userId.isEmpty(); }
	QString account() const { return accountName; }
	QString broadcaster() const { return userId; }
	void reconnect()
	{
		if (accessToken.isEmpty() || userId.isEmpty()) {
			setStatus("Connect Twitch before applying EventSub changes.");
			return;
		}
		startSocket();
	}

	void beginLogin()
	{
		++authGeneration;
		refreshing = false; refreshWaiters.clear();
		sendingChat = false;
		pollTimer.stop(); tokenTimer.stop(); retryTimer.stop();
		stopSocket();
		const QString scopes = "user:read:chat user:write:chat channel:manage:broadcast moderator:read:followers "
			"moderator:manage:chat_messages moderator:manage:banned_users moderator:manage:chat_settings "
			"channel:read:subscriptions bits:read channel:read:redemptions channel:read:hype_train channel:read:goals channel:read:stream_key";
		QNetworkRequest request(QUrl("https://id.twitch.tv/oauth2/device"));
		request.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
		QNetworkReply *reply = network.post(request, formBody({{"client_id", clientId}, {"scopes", scopes}}));
		setStatus("Requesting a secure Twitch device login…");
		connect(reply, &QNetworkReply::finished, this, [this, reply, generation = authGeneration] {
			if (generation != authGeneration) { reply->deleteLater(); return; }
			const QByteArray body = reply->readAll();
			const QJsonObject json = QJsonDocument::fromJson(body).object();
			const int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
			const QNetworkReply::NetworkError networkError = reply->error();
			const QString networkErrorText = reply->errorString();
			reply->deleteLater();
			if (statusCode < 200 || statusCode >= 300) {
				QString detail = json.value("message").toString().trimmed();
				if (detail.isEmpty() && networkError != QNetworkReply::NoError)
					detail = networkErrorText;
				if (detail.isEmpty())
					detail = "HTTP " + QString::number(statusCode);
				setStatus("Twitch login could not start: " + detail);
				blog(LOG_WARNING, "[Pulse Weaver] Twitch device login request failed: HTTP %d, Qt network error %d (%s)",
				     statusCode, int(networkError), networkErrorText.toUtf8().constData());
				return;
			}
			deviceCode = json.value("device_code").toString();
			userCode = json.value("user_code").toString();
			verificationUrl = QUrl(json.value("verification_uri").toString());
			pollInterval = std::max(1, json.value("interval").toInt(5));
			deviceDeadline = QDateTime::currentDateTimeUtc().addSecs(json.value("expires_in").toInt(900));
			setStatus("Open Twitch and enter code " + userCode + ". Waiting for approval…");
			QDesktopServices::openUrl(verificationUrl);
			pollTimer.start(pollInterval * 1000);
		});
	}

	void clearLogin()
	{
		++authGeneration;
		refreshing = false; refreshWaiters.clear();
		sendingChat = false;
		tokenTimer.stop(); retryTimer.stop();
		pollTimer.stop();
		stopSocket();
		accessToken.clear();
		refreshToken.clear();
		userId.clear();
		accountName.clear();
		sessionChatters.clear(); seenEvents.clear(); seenEventOrder.clear();
		QSettings settings(pulseSettingsPath(), QSettings::IniFormat);
		settings.remove("twitch/access_token");
		settings.remove("twitch/refresh_token");
		settings.remove("twitch/user_id");
		settings.remove("twitch/account_name");
		setStatus("Twitch disconnected. Pulse Weaver's isolated credential was removed.");
		updateUi();
	}

	void sendMessage(const QString &message, bool retried = false)
	{
		if (sendingChat && !retried) return;
		const QString text = message.trimmed();
		if (text.isEmpty() || accessToken.isEmpty() || userId.isEmpty()) {
			if (!text.isEmpty())
				setStatus("Connect Twitch before sending chat messages.");
			return;
		}
		QNetworkRequest request(QUrl("https://api.twitch.tv/helix/chat/messages"));
		sendingChat = true;
		request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
		request.setRawHeader("Client-Id", clientId.toUtf8());
		request.setRawHeader("Authorization", "Bearer " + accessToken.toUtf8());
		const QJsonObject body{{"broadcaster_id", userId}, {"sender_id", userId}, {"message", text}};
		QNetworkReply *reply = network.post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
		connect(reply, &QNetworkReply::finished, this, [this, reply, text, retried, generation = authGeneration] {
			if (generation != authGeneration) { reply->deleteLater(); return; }
			const QByteArray body = reply->readAll();
			const int code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
			reply->deleteLater();
			if (code == 401 && !retried && !refreshToken.isEmpty()) {
				refreshAccessToken([this, text](bool ok) { if (ok) sendMessage(text, true); else sendingChat = false; });
				return;
			}
			sendingChat = false;
			if (code < 200 || code >= 300) {
				const QJsonObject error = QJsonDocument::fromJson(body).object();
				setStatus("Chat send failed: " + error.value("message").toString("HTTP " + QString::number(code)));
				return;
			}
			/* EventSub echoes successful sends. Render that authoritative event
			 * once instead of adding an optimistic duplicate here. */
			const auto sent = QJsonDocument::fromJson(body).object().value("data").toArray();
			if (sent.isEmpty() || !sent.first().toObject().value("is_sent").toBool())
				setStatus("Twitch did not send the message: " + (sent.isEmpty() ? QString("empty response") :
					sent.first().toObject().value("drop_reason").toObject().value("message").toString("message rejected")));
			else {
				if (chatInput && chatInput->text().trimmed() == text && shellTwitchOnly()) chatInput->clear();
				setStatus("Twitch message sent.");
			}
		});
	}

	void updateChannel(const QString &title, const QString &categoryId)
	{
		if (userId.isEmpty() || accessToken.isEmpty()) {
			setStatus("Connect Twitch before editing stream information.");
			return;
		}
		QUrl url("https://api.twitch.tv/helix/channels");
		QUrlQuery query;
		query.addQueryItem("broadcaster_id", userId);
		url.setQuery(query);
		QNetworkRequest request(url);
		request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
		request.setRawHeader("Client-Id", clientId.toUtf8());
		request.setRawHeader("Authorization", "Bearer " + accessToken.toUtf8());
		QJsonObject body;
		if (!title.trimmed().isEmpty())
			body.insert("title", title.trimmed());
		if (!categoryId.trimmed().isEmpty())
			body.insert("game_id", categoryId.trimmed());
		QNetworkReply *reply = network.sendCustomRequest(request, "PATCH", QJsonDocument(body).toJson(QJsonDocument::Compact));
		connect(reply, &QNetworkReply::finished, this, [this, reply] {
			const int code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
			const QJsonObject error = QJsonDocument::fromJson(reply->readAll()).object();
			reply->deleteLater();
			setStatus(code >= 200 && code < 300 ? "Twitch stream information updated." :
								 "Stream information update failed: " + error.value("message").toString("HTTP " + QString::number(code)));
		});
	}

	void searchCategories(const QString &search,
			      std::function<void(const QJsonArray &, const QString &)> completed)
	{
		const QString term = search.trimmed();
		if (term.size() < 2) {
			completed({}, {});
			return;
		}
		if (accessToken.isEmpty()) {
			completed({}, "Connect Twitch to search its categories.");
			return;
		}
		QUrl url("https://api.twitch.tv/helix/search/categories");
		QUrlQuery query;
		query.addQueryItem("query", term);
		query.addQueryItem("first", "25");
		url.setQuery(query);
		QNetworkRequest request(url);
		request.setRawHeader("Client-Id", clientId.toUtf8());
		request.setRawHeader("Authorization", "Bearer " + accessToken.toUtf8());
		QNetworkReply *reply = network.get(request);
		connect(reply, &QNetworkReply::finished, this,
			[this, reply, completed = std::move(completed)] {
				const int code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
				const QJsonObject response = QJsonDocument::fromJson(reply->readAll()).object();
				reply->deleteLater();
				if (code < 200 || code >= 300) {
					completed({}, response.value("message").toString("Twitch category search failed."));
					return;
				}
				completed(response.value("data").toArray(), {});
			});
	}

private:
	QNetworkAccessManager network{this};
	QTimer pollTimer{this};
	QTimer retryTimer{this}, tokenTimer{this}, watchdog{this};
	quint64 authGeneration = 0;
	std::atomic<quint64> socketGeneration{0};
	bool chatSubscribed = false;
	bool refreshing = false;
	bool sendingChat = false;
	std::vector<std::function<void(bool)>> refreshWaiters;
	int retryAttempt = 0;
	qint64 lastSocketMessage = 0;
	qint64 keepaliveMs = 15000;
	QString clientId;
	QString accessToken;
	QString refreshToken;
	QString userId;
	QString accountName;
	QString deviceCode;
	QString userCode;
	QUrl verificationUrl;
	QDateTime deviceDeadline;
	int pollInterval = 5;
	int activeSubscriptions = 0;
	std::atomic_bool stopping{false};
	std::atomic_bool socketConnected{false};
#ifdef _WIN32
	struct SocketWorker {
		std::atomic_bool cancelled{false};
		std::atomic<HINTERNET> handle{nullptr};
		std::thread thread;
		quint64 generation = 0;
		bool resume = false;
	};
	std::shared_ptr<SocketWorker> activeSocket, pendingSocket;
#endif
	EventCallback eventCallback;
	ChatCallback chatCallback;
	QPointer<QListWidget> chatFeed;
	QPointer<QLineEdit> chatInput;
	QPointer<QPushButton> chatSend;
	QPointer<QPushButton> chatConnect;
	QPointer<QLabel> chatStatus;
	QPointer<QLineEdit> clientField;
	QPointer<QLabel> accountLabel;
	QPointer<QLabel> connectionStatus;
	QPointer<QPushButton> loginButton;
	QPointer<QPushButton> logoutButton;
	QSet<QString> sessionChatters;
	QSet<QString> seenEvents;
	QQueue<QString> seenEventOrder;
	QHash<QString, QUrl> chatBadgeUrls;
	QHash<QString, QUrl> globalChatBadgeUrls, channelChatBadgeUrls;
	QStringList grantedScopes;

	void setStatus(const QString &message)
	{
		if (connectionStatus)
			connectionStatus->setText(message);
		if (chatStatus && shellSelected()) chatStatus->setText(message);
		if (QWidget *window = static_cast<QWidget *>(obs_frontend_get_main_window()))
			window->setProperty("pulseWeaverTwitchChatStatus", message);
	}
	bool shellTwitchOnly() const
	{
		auto *window = static_cast<QWidget *>(obs_frontend_get_main_window());
		auto *provider = window ? window->findChild<QComboBox *>("PulseWeaverChatProvider") : nullptr;
		return provider && provider->currentData().toString() == "twitch";
	}
	bool shellSelected() const
	{
		QWidget *mainWindow = static_cast<QWidget *>(obs_frontend_get_main_window());
		auto *provider = mainWindow ? mainWindow->findChild<QComboBox *>("PulseWeaverChatProvider") : nullptr;
		return provider && (provider->currentData().toString() == "twitch" ||
			provider->currentData().toString() == "all");
	}

	void updateUi()
	{
		const bool ready = !userId.isEmpty() && !accessToken.isEmpty();
		if (QWidget *mainWindow = static_cast<QWidget *>(obs_frontend_get_main_window())) {
			mainWindow->setProperty("pulseWeaverTwitchChatReady", ready && socketConnected && chatSubscribed);
			/* The frontend owns the shared composer because All chats must combine
			 * Twitch, YouTube and Kick readiness. Queue the refresh so every
			 * provider has finished publishing its current property first. */
			QMetaObject::invokeMethod(mainWindow, "RefreshPulseWeaverChatComposer", Qt::QueuedConnection);
		}
		if (chatConnect)
			chatConnect->setText(ready ? "RECONNECT" : "CONNECT");
		if (accountLabel)
			accountLabel->setText(ready ? "Connected as " + accountName : "No Twitch account connected");
		if (logoutButton)
			logoutButton->setEnabled(ready);
	}

	void showChatActions(const QPoint &position)
	{
		if (!chatFeed)
			return;
		auto *item = chatFeed->itemAt(position);
		if (!item)
			return;
		const QString platform = item->data(PulseChatPlatformRole).toString();
		if (platform != "twitch")
			return;
		const QString user = item->data(PulseChatUserRole).toString();
		const QString userId = item->data(PulseChatUserIdRole).toString();
		const QString messageId = item->data(PulseChatMessageIdRole).toString();
		QMenu menu(chatFeed);
		menu.addSection(platform.toUpper() + " · " + user);
		auto *copy = menu.addAction("Copy message");
		connect(copy, &QAction::triggered, this, [text = item->data(PulseChatTextRole).toString()] { QApplication::clipboard()->setText(text); });
		auto *mention = menu.addAction("Mention @" + user);
		connect(mention, &QAction::triggered, this, [this, user] { if (chatInput) { chatInput->setText("@" + user + " "); chatInput->setFocus(); } });
		const bool canDelete = grantedScopes.contains("moderator:manage:chat_messages");
		const bool canBan = grantedScopes.contains("moderator:manage:banned_users");
		if (!canDelete || !canBan) {
			menu.addSection("Reconnect Twitch to enable moderation");
			auto *reconnect = menu.addAction("Grant Twitch moderation access…");
			connect(reconnect, &QAction::triggered, this, [this] { beginLogin(); });
		}
		menu.addSeparator();
		QAction *deleteMessage = nullptr;
		QAction *timeoutTen = nullptr;
		QAction *timeoutHour = nullptr;
		QAction *ban = nullptr;
		deleteMessage = menu.addAction("Delete message");
		menu.addSeparator();
		timeoutTen = menu.addAction("Timeout 10 minutes");
		timeoutHour = menu.addAction("Timeout 1 hour");
		ban = menu.addAction("Ban user");
		if (userId.isEmpty() || userId == this->userId || !canBan) {
			timeoutTen->setEnabled(false); timeoutHour->setEnabled(false); ban->setEnabled(false);
		}
		if (messageId.isEmpty() || !canDelete || item->data(PulseChat::Deleted).toBool()) deleteMessage->setEnabled(false);
		const QAction *choice = menu.exec(chatFeed->viewport()->mapToGlobal(position));
		if (choice == deleteMessage) moderateDelete(messageId);
		else if (choice == timeoutTen) moderateBan(userId, 600);
		else if (choice == timeoutHour) moderateBan(userId, 3600);
		else if (choice == ban) moderateBan(userId, 0);
	}

	void moderateDelete(const QString &messageId, bool retried = false)
	{
		if (accessToken.isEmpty() || userId.isEmpty() || messageId.isEmpty()) return;
		setStatus("Deleting Twitch message…");
		QUrl url("https://api.twitch.tv/helix/moderation/chat");
		QUrlQuery query;
		query.addQueryItem("broadcaster_id", userId);
		query.addQueryItem("moderator_id", userId);
		query.addQueryItem("message_id", messageId);
		url.setQuery(query);
		QNetworkRequest request(url);
		request.setRawHeader("Client-Id", clientId.toUtf8());
		request.setRawHeader("Authorization", "Bearer " + accessToken.toUtf8());
		QNetworkReply *reply = network.deleteResource(request);
		connect(reply, &QNetworkReply::finished, this, [this, reply, messageId, retried, generation = authGeneration] {
			if (generation != authGeneration) { reply->deleteLater(); return; }
			const int code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
			const QJsonObject error = QJsonDocument::fromJson(reply->readAll()).object();
			reply->deleteLater();
			if (code == 401 && !retried && !refreshToken.isEmpty()) {
				refreshAccessToken([this, messageId](bool ok) { if (ok) moderateDelete(messageId, true); }); return;
			}
			if (code == 204) PulseChat::markDeleted(chatFeed, "twitch", messageId);
			setStatus(code >= 200 && code < 300 ? "Twitch message deleted." :
				QString("Twitch delete failed (%1): ").arg(code) + error.value("message").toString(reply->errorString()));
		});
	}

	void moderateBan(const QString &targetUserId, int duration, bool retried = false)
	{
		if (accessToken.isEmpty() || userId.isEmpty() || targetUserId.isEmpty()) return;
		setStatus("Applying Twitch moderation…");
		QUrl url("https://api.twitch.tv/helix/moderation/bans");
		QUrlQuery query;
		query.addQueryItem("broadcaster_id", userId);
		query.addQueryItem("moderator_id", userId);
		url.setQuery(query);
		QNetworkRequest request(url);
		request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
		request.setRawHeader("Client-Id", clientId.toUtf8());
		request.setRawHeader("Authorization", "Bearer " + accessToken.toUtf8());
		QNetworkReply *reply = network.post(request, QJsonDocument(PulseChat::twitchBanBody(targetUserId, duration)).toJson(QJsonDocument::Compact));
		connect(reply, &QNetworkReply::finished, this, [this, reply, duration, targetUserId, retried, generation = authGeneration] {
			if (generation != authGeneration) { reply->deleteLater(); return; }
			const int code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
			const QJsonObject error = QJsonDocument::fromJson(reply->readAll()).object();
			reply->deleteLater();
			if (code == 401 && !retried && !refreshToken.isEmpty()) {
				refreshAccessToken([this, targetUserId, duration](bool ok) { if (ok) moderateBan(targetUserId, duration, true); }); return;
			}
			if (code >= 200 && code < 300) PulseChat::markDeleted(chatFeed, "twitch", {}, targetUserId);
			const QString success = duration > 0 ? QString("Twitch user timed out for %1 minutes.").arg(duration / 60) : "Twitch user banned.";
			setStatus(code >= 200 && code < 300 ? success :
				QString("Twitch moderation failed (%1): ").arg(code) + error.value("message").toString(reply->errorString()));
		});
	}

	void sendFromComposer()
	{
		if (!chatInput)
			return;
		if (QWidget *mainWindow = static_cast<QWidget *>(obs_frontend_get_main_window())) {
			if (auto *provider = mainWindow->findChild<QComboBox *>("PulseWeaverChatProvider");
			    provider && provider->currentData().toString() != "twitch" &&
			    provider->currentData().toString() != "all")
				return;
		}
		QWidget *mainWindow = static_cast<QWidget *>(obs_frontend_get_main_window());
		auto *provider = mainWindow ? mainWindow->findChild<QComboBox *>("PulseWeaverChatProvider") : nullptr;
		const bool all = provider && provider->currentData().toString() == "all";
		const QString message = (all ? chatInput->property("pulseWeaverBroadcastMessage").toString() :
			chatInput->text()).trimmed();
		if (message.isEmpty())
			return;
		sendMessage(message);
	}

	void appendChat(const QString &user, const QString &message, bool outgoing = false, const QJsonObject &event = {})
	{
		if (!chatFeed)
			return;
		QStringList badges;
		QHash<QString, QUrl> badgeImages;
		for (const QJsonValue &badgeValue : event.value("badges").toArray()) {
			const QJsonObject badge = badgeValue.toObject();
			const QString label = badge.value("set_id").toString(badge.value("name").toString());
			const QString key = label + "/" + badge.value("id").toString();
			badges << key;
		}
		pulseAppendUnifiedChat(chatFeed, "twitch", user, message, event.value("chatter_color").toString(), badges,
			badgeImages, event.value("chatter_user_id").toString(), event.value("message_id").toString(), outgoing,
			event.value("message").toObject().value("fragments").toArray());
	}

	void saveLogin()
	{
		QSettings settings(pulseSettingsPath(), QSettings::IniFormat);
		settings.remove("twitch/client_id");
		settings.setValue("twitch/access_token", protectCredential(accessToken));
		settings.setValue("twitch/refresh_token", protectCredential(refreshToken));
		settings.setValue("twitch/user_id", userId);
		settings.setValue("twitch/account_name", accountName);
	}

	void pollDeviceToken()
	{
		if (deviceCode.isEmpty() || QDateTime::currentDateTimeUtc() >= deviceDeadline) {
			pollTimer.stop();
			setStatus("Twitch device login expired. Press Connect to try again.");
			return;
		}
		QNetworkRequest request(QUrl("https://id.twitch.tv/oauth2/token"));
		request.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
		QNetworkReply *reply = network.post(
			request, formBody({{"client_id", clientId}, {"scopes", "user:read:chat user:write:chat channel:manage:broadcast moderator:read:followers moderator:manage:chat_messages moderator:manage:banned_users moderator:manage:chat_settings channel:read:subscriptions bits:read channel:read:redemptions channel:read:hype_train channel:read:goals channel:read:stream_key"}, {"device_code", deviceCode}, {"grant_type", "urn:ietf:params:oauth:grant-type:device_code"}}));
		connect(reply, &QNetworkReply::finished, this, [this, reply, generation = authGeneration] {
			if (generation != authGeneration) { reply->deleteLater(); return; }
			const QByteArray body = reply->readAll();
			const int code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
			reply->deleteLater();
			const QJsonObject json = QJsonDocument::fromJson(body).object();
			if (code >= 200 && code < 300) {
				pollTimer.stop();
				accessToken = json.value("access_token").toString();
				refreshToken = json.value("refresh_token").toString();
				validateToken();
			} else if (json.value("message").toString().contains("pending", Qt::CaseInsensitive)) {
				setStatus("Waiting for Twitch approval — code " + userCode);
			} else {
				pollTimer.stop();
				setStatus("Twitch authorization failed: " + json.value("message").toString("HTTP " + QString::number(code)));
			}
		});
	}

	void validateToken(bool restart = true)
	{
		if (accessToken.isEmpty()) return;
		QNetworkRequest request(QUrl("https://id.twitch.tv/oauth2/validate"));
		request.setTransferTimeout(10000);
		request.setRawHeader("Authorization", "OAuth " + accessToken.toUtf8());
		QNetworkReply *reply = network.get(request);
		connect(reply, &QNetworkReply::finished, this, [this, reply, restart, generation = authGeneration, checkedToken = accessToken] {
			if (generation != authGeneration || checkedToken != accessToken) { reply->deleteLater(); return; }
			const QJsonObject json = QJsonDocument::fromJson(reply->readAll()).object();
			const int code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
			reply->deleteLater();
			if (code != 401 && (code < 200 || code >= 300)) {
				if (restart) scheduleReconnect("Twitch validation temporarily unavailable.");
				else tokenTimer.start(30000);
				return;
			}
			if (code < 200 || code >= 300) {
				if (!refreshToken.isEmpty()) {
					refreshAccessToken();
					return;
				}
				accessToken.clear();
				setStatus("Saved Twitch login expired. Press Connect to authorize again.");
				updateUi();
				return;
			}
			if (json.value("client_id").toString() != clientId) {
				stopSocket(); tokenTimer.stop(); retryTimer.stop();
				setStatus("The saved Twitch login belongs to another application Client ID. Reconnect Twitch.");
				return;
			}
			const int expires = json.value("expires_in").toInt();
			if (expires > 0 && expires <= 120 && !refreshToken.isEmpty()) { refreshAccessToken(); return; }
			tokenTimer.start(std::clamp(expires > 0 ? expires - 120 : 3600, 30, 3600) * 1000);
			userId = json.value("user_id").toString();
			accountName = json.value("login").toString();
			grantedScopes.clear();
			for (const auto &scope : json.value("scopes").toArray()) grantedScopes << scope.toString();
			if (!grantedScopes.contains("user:read:chat") || !grantedScopes.contains("user:write:chat")) {
				stopSocket(); retryTimer.stop();
				setStatus("Reconnect Twitch to grant chat read and write access."); return;
			}
			if (!restart) return;
			loadChatBadges();
			saveLogin();
			updateUi();
			setStatus("Twitch connected as " + accountName + "; opening EventSub chat…");
			startSocket();
			configureBroadcastDestination();
		});
	}

	void loadChatBadges()
	{
		if (accessToken.isEmpty() || userId.isEmpty())
			return;
		auto load = [this](const QUrl &url, bool channel) {
			QNetworkRequest request(url);
			request.setRawHeader("Client-Id", clientId.toUtf8());
			request.setRawHeader("Authorization", "Bearer " + accessToken.toUtf8());
			QNetworkReply *reply = network.get(request);
			connect(reply, &QNetworkReply::finished, this, [this, reply, channel] {
				const QJsonObject root = QJsonDocument::fromJson(reply->readAll()).object();
				reply->deleteLater();
				if (channel) channelChatBadgeUrls = PulseChat::parseBadges(root);
				else globalChatBadgeUrls = PulseChat::parseBadges(root);
				chatBadgeUrls = globalChatBadgeUrls;
				for (auto it = channelChatBadgeUrls.begin(); it != channelChatBadgeUrls.end(); ++it) chatBadgeUrls.insert(it.key(), it.value());
				if (chatFeed) {
					QVariantMap urls;
					for (auto it = chatBadgeUrls.begin(); it != chatBadgeUrls.end(); ++it) urls.insert(it.key(), it.value().toString());
					chatFeed->setProperty("pulseWeaverBadgeUrls", urls);
					chatFeed->doItemsLayout(); chatFeed->viewport()->update();
				}
			});
		};
		load(QUrl("https://api.twitch.tv/helix/chat/badges/global"), false);
		QUrl local("https://api.twitch.tv/helix/chat/badges");
		QUrlQuery query; query.addQueryItem("broadcaster_id", userId); local.setQuery(query);
		load(local, true);
	}

	void configureBroadcastDestination()
	{
		if (obs_frontend_streaming_active()) {
			setStatus("Twitch connected. End the current stream before Pulse Weaver updates its isolated destination.");
			return;
		}
		QUrl url("https://api.twitch.tv/helix/streams/key");
		QUrlQuery query;
		query.addQueryItem("broadcaster_id", userId);
		url.setQuery(query);
		QNetworkRequest request(url);
		request.setRawHeader("Client-Id", clientId.toUtf8());
		request.setRawHeader("Authorization", "Bearer " + accessToken.toUtf8());
		QNetworkReply *reply = network.get(request);
		connect(reply, &QNetworkReply::finished, this, [this, reply] {
			const QByteArray body = reply->readAll();
			const int code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
			reply->deleteLater();
			const QJsonObject json = QJsonDocument::fromJson(body).object();
			const QJsonArray data = json.value("data").toArray();
			const QString streamKey = data.isEmpty() ? QString() : data.first().toObject().value("stream_key").toString();
			if (code < 200 || code >= 300 || streamKey.isEmpty()) {
				setStatus("Twitch connected, but broadcast setup needs reconnecting with stream-key permission: " +
					  json.value("message").toString("HTTP " + QString::number(code)));
				return;
			}
			obs_data_t *settings = obs_data_create();
			obs_data_set_string(settings, "service", "Twitch");
			obs_data_set_string(settings, "server", "auto");
			obs_data_set_string(settings, "key", streamKey.toUtf8().constData());
			obs_service_t *current = obs_frontend_get_streaming_service();
			if (current && strcmp(obs_service_get_type(current), "rtmp_common") == 0) {
				obs_service_update(current, settings);
			} else {
				obs_service_t *service = obs_service_create("rtmp_common", "pulse_weaver_twitch", settings, nullptr);
				if (service) {
					obs_frontend_set_streaming_service(service);
					obs_service_release(service);
				}
			}
			/* obs_frontend_get_streaming_service() returns the frontend's
			 * borrowed service pointer. Releasing it here leaves OBSBasic's
			 * service wrapper dangling and crashes in applicationShutdown(). */
			obs_data_release(settings);
			obs_frontend_save_streaming_service();
			if (chatSubscribed) setStatus("Twitch chat and broadcast destination are ready.");
		});
	}

	void refreshAccessToken(std::function<void(bool)> completed = {})
	{
		if (completed) refreshWaiters.push_back(std::move(completed));
		if (refreshing) return;
		refreshing = true;
		QNetworkRequest request(QUrl("https://id.twitch.tv/oauth2/token"));
		request.setTransferTimeout(10000);
		request.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
		QNetworkReply *reply = network.post(request, formBody({{"grant_type", "refresh_token"},
										 {"refresh_token", refreshToken}, {"client_id", clientId}}));
		connect(reply, &QNetworkReply::finished, this, [this, reply, generation = authGeneration] {
			if (generation != authGeneration) { reply->deleteLater(); return; }
			const QJsonObject json = QJsonDocument::fromJson(reply->readAll()).object();
			const int code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
			reply->deleteLater();
			refreshing = false;
			auto waiters = std::move(refreshWaiters); refreshWaiters.clear();
			if (code < 200 || code >= 300) {
				if (code == 400 || code == 401) {
					stopSocket(); tokenTimer.stop(); retryTimer.stop();
					accessToken.clear(); refreshToken.clear(); saveLogin();
					setStatus("Twitch login expired. Press Connect to authorize again.");
				} else { tokenTimer.start(30000); setStatus("Twitch token refresh temporarily failed; retrying."); }
				updateUi();
				for (auto &waiter : waiters) waiter(false);
				return;
			}
			accessToken = json.value("access_token").toString();
			refreshToken = json.value("refresh_token").toString(refreshToken);
			saveLogin();
			validateToken();
			for (auto &waiter : waiters) waiter(!accessToken.isEmpty());
		});
	}

	void disconnectAll()
	{
		tokenTimer.stop(); retryTimer.stop();
		pollTimer.stop();
		stopSocket();
	}

	void scheduleReconnect(const QString &reason)
	{
		chatSubscribed = false; socketConnected = false;
		updateUi();
		if (accessToken.isEmpty() || retryTimer.isActive()) return;
		const int delay = std::min(30, 1 << std::min(retryAttempt++, 5));
		setStatus(reason + QString(" Retrying in %1 seconds…").arg(delay));
		retryTimer.start(delay * 1000);
	}

	void startSocket()
	{
		stopSocket();
		retryTimer.stop();
		stopping = false;
		lastSocketMessage = QDateTime::currentMSecsSinceEpoch();
		keepaliveMs = 15000;
		watchdog.start();
#ifdef _WIN32
		activeSocket = launchSocket(QUrl("wss://eventsub.wss.twitch.tv/ws"), false);
#else
		setStatus("Native Twitch EventSub is currently available in the Windows build.");
#endif
	}

#ifdef _WIN32
	void closeWorker(const std::shared_ptr<SocketWorker> &worker)
	{
		if (!worker) return;
		worker->cancelled = true;
		// Closing the handle interrupts a blocked receive; the worker owns the
		// connection/session handles and releases them before the join returns.
		if (auto handle = worker->handle.exchange(nullptr)) WinHttpCloseHandle(handle);
		if (worker->thread.joinable()) worker->thread.join();
	}

	std::shared_ptr<SocketWorker> launchSocket(const QUrl &url, bool resume)
	{
		auto worker = std::make_shared<SocketWorker>();
		worker->generation = socketGeneration.load();
		worker->resume = resume;
		worker->thread = std::thread([this, worker, url] { runSocket(worker, url); });
		return worker;
	}
#endif

	void stopSocket()
	{
		stopping = true;
		++socketGeneration;
		watchdog.stop();
#ifdef _WIN32
		closeWorker(pendingSocket); pendingSocket.reset();
		closeWorker(activeSocket); activeSocket.reset();
#endif
		socketConnected = false; chatSubscribed = false;
		updateUi();
	}

#ifdef _WIN32
	void runSocket(const std::shared_ptr<SocketWorker> &worker, const QUrl &url)
	{
		auto failed = [this, worker] {
			QMetaObject::invokeMethod(this, [this, worker] {
				if (worker->generation != socketGeneration || worker->cancelled) return;
				// The old connection can close during a handover. Let the new
				// connection finish; the watchdog bounds the whole handover.
				if (pendingSocket && worker == activeSocket) return;
				stopSocket();
				scheduleReconnect("Twitch chat connection interrupted.");
			}, Qt::QueuedConnection);
		};
		HINTERNET session = WinHttpOpen(L"PulseWeaver Twitch EventSub", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
			WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
		if (!session) { failed(); return; }
		WinHttpSetTimeouts(session, 5000, 5000, 5000, 1000);
		const auto host = url.host().toStdWString();
		QString path = url.path(QUrl::FullyEncoded);
		if (path.isEmpty()) path = "/";
		if (url.hasQuery()) path += "?" + url.query(QUrl::FullyEncoded);
		const auto resource = path.toStdWString();
		HINTERNET connection = WinHttpConnect(session, host.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);
		HINTERNET request = connection ? WinHttpOpenRequest(connection, L"GET", resource.c_str(), nullptr,
			WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE) : nullptr;
		bool ok = request && WinHttpSetOption(request, WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET, nullptr, 0) &&
			WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
			WinHttpReceiveResponse(request, nullptr);
		HINTERNET socket = ok ? WinHttpWebSocketCompleteUpgrade(request, 0) : nullptr;
		if (request) WinHttpCloseHandle(request);
		if (socket) {
			worker->handle = socket;
			QByteArray message;
			std::vector<char> buffer(65536);
			while (!worker->cancelled) {
				DWORD bytes = 0;
				WINHTTP_WEB_SOCKET_BUFFER_TYPE type = WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE;
				const DWORD result = WinHttpWebSocketReceive(socket, buffer.data(), DWORD(buffer.size()), &bytes, &type);
				if (result == ERROR_WINHTTP_TIMEOUT) continue;
				if (result != NO_ERROR || type == WINHTTP_WEB_SOCKET_CLOSE_BUFFER_TYPE) break;
				if (bytes) message.append(buffer.data(), int(bytes));
				if (message.size() > 1024 * 1024) break;
				if (type != WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE) continue;
				const QByteArray completed = message; message.clear();
				QMetaObject::invokeMethod(this, [this, worker, completed] {
					deliverSocketMessage(worker, completed);
				}, Qt::QueuedConnection);
			}
			if (worker->handle.exchange(nullptr) == socket) WinHttpCloseHandle(socket);
		}
		if (connection) WinHttpCloseHandle(connection);
		WinHttpCloseHandle(session);
		if (!worker->cancelled) failed();
	}
	void deliverSocketMessage(const std::shared_ptr<SocketWorker> &worker, const QByteArray &completed)
	{
		if (worker->generation != socketGeneration || worker->cancelled) return;
		const auto root = QJsonDocument::fromJson(completed).object();
		const auto kind = root.value("metadata").toObject().value("message_type").toString();
		if (kind == "session_welcome") {
			const auto sessionData = root.value("payload").toObject().value("session").toObject();
			if (sessionData.value("id").toString().isEmpty()) return;
			keepaliveMs = std::clamp(sessionData.value("keepalive_timeout_seconds").toInt(10), 10, 600) * 1000LL + 2000;
			socketConnected = true;
			if (worker == pendingSocket) {
				auto previous = activeSocket;
				activeSocket = pendingSocket; pendingSocket.reset();
				closeWorker(previous);
				lastSocketMessage = QDateTime::currentMSecsSinceEpoch();
				updateUi(); setStatus("Twitch chat reconnected.");
				return; // Twitch transferred subscriptions; do not recreate them.
			}
		}
		handleSocketMessage(completed);
	}
#endif

	void handleSocketMessage(const QByteArray &payload)
	{
		const QJsonObject root = QJsonDocument::fromJson(payload).object();
		const QString messageType = root.value("metadata").toObject().value("message_type").toString();
		if (messageType.isEmpty()) return;
		lastSocketMessage = QDateTime::currentMSecsSinceEpoch();
		if (messageType == "session_welcome") {
			const QString sessionId = root.value("payload").toObject().value("session").toObject().value("id").toString();
			subscribeEvents(sessionId);
			return;
		}
		if (messageType == "session_keepalive")
			return;
		if (messageType == "session_reconnect") {
#ifdef _WIN32
			if (pendingSocket) return;
			const QUrl url(root.value("payload").toObject().value("session").toObject().value("reconnect_url").toString());
			if (url.scheme() != "wss" || url.host() != "eventsub.wss.twitch.tv" ||
			    !url.userInfo().isEmpty() || url.hasFragment() || (url.port() != -1 && url.port() != 443)) {
				stopSocket(); scheduleReconnect("Twitch provided an invalid reconnect address."); return;
			}
			setStatus("Twitch is moving chat to a new connection…");
			pendingSocket = launchSocket(url, true);
#endif
			return;
		}
		if (messageType == "revocation") {
			const auto subscription = root.value("payload").toObject().value("subscription").toObject();
			if (subscription.value("status").toString() == "authorization_revoked") {
				clearLogin(); setStatus("Twitch access was revoked. Connect again to enable chat.");
			} else if (subscription.value("type").toString() == "channel.chat.message") {
				stopSocket(); scheduleReconnect("Twitch revoked the chat subscription.");
			}
			return;
		}
		if (messageType != "notification")
			return;
		const QString eventId = root.value("metadata").toObject().value("message_id").toString();
		if (!eventId.isEmpty()) {
			if (seenEvents.contains(eventId)) return;
			seenEvents.insert(eventId); seenEventOrder.enqueue(eventId);
			while (seenEventOrder.size() > 2048) seenEvents.remove(seenEventOrder.dequeue());
		}
		const QJsonObject payloadObject = root.value("payload").toObject();
		const QString eventType = payloadObject.value("subscription").toObject().value("type").toString();
		const QJsonObject event = payloadObject.value("event").toObject();
		if (eventType == "channel.chat.message_delete") PulseChat::markDeleted(chatFeed, "twitch", event.value("message_id").toString());
		else if (eventType == "channel.chat.clear_user_messages") PulseChat::markDeleted(chatFeed, "twitch", {}, event.value("target_user_id").toString());
		else if (eventType == "channel.chat.clear") PulseChat::markDeleted(chatFeed, "twitch");
		const QJsonObject flat = flattenEvent(event);
		if (eventCallback)
			eventCallback(eventType, flat);
		if (eventType == "channel.chat.message") {
			const QString user = event.value("chatter_user_name").toString(event.value("chatter_user_login").toString());
			const QString chatterId = event.value("chatter_user_id").toString(user.toLower());
			const QString text = event.value("message").toObject().value("text").toString();
			if (!chatterId.isEmpty() && !sessionChatters.contains(chatterId)) {
				sessionChatters.insert(chatterId);
				if (eventCallback)
					eventCallback("viewer.entrance", flat);
			}
			appendChat(user, text, !userId.isEmpty() && chatterId == userId, event);
			if (chatCallback)
				chatCallback(user, text, flat);
			if (event.value("first_message").toBool() && eventCallback)
				eventCallback("viewer.first_message", flat);
		}
	}

	void subscribeEvents(const QString &sessionId)
	{
		struct Definition {
			const char *type;
			const char *version;
			enum Condition { Broadcaster, Moderator, RaidTo, Chat } condition;
		};
		const std::vector<Definition> definitions{{"channel.chat.message", "1", Definition::Chat},
			{"channel.chat.message_delete", "1", Definition::Chat}, {"channel.chat.clear_user_messages", "1", Definition::Chat},
			{"channel.chat.clear", "1", Definition::Chat},
			{"channel.follow", "2", Definition::Moderator}, {"channel.subscribe", "1", Definition::Broadcaster},
			{"channel.subscription.message", "1", Definition::Broadcaster},
			{"channel.subscription.gift", "1", Definition::Broadcaster}, {"channel.cheer", "1", Definition::Broadcaster},
			{"channel.raid", "1", Definition::RaidTo},
			{"channel.channel_points_custom_reward_redemption.add", "1", Definition::Broadcaster},
			{"channel.hype_train.begin", "2", Definition::Broadcaster}, {"channel.goal.progress", "1", Definition::Broadcaster}};
		const QStringList defaults{"channel.chat.message", "channel.follow", "channel.subscribe", "channel.subscription.message",
			"channel.subscription.gift", "channel.cheer", "channel.raid",
			"channel.channel_points_custom_reward_redemption.add", "channel.hype_train.begin", "channel.goal.progress"};
		QSettings settings(pulseSettingsPath(), QSettings::IniFormat);
		const QStringList selected = settings.contains("twitch/events") ? settings.value("twitch/events").toStringList() : defaults;
		activeSubscriptions = 0;
		for (const Definition &definition : definitions) {
			if (!selected.contains(QString::fromLatin1(definition.type)) && !QString::fromLatin1(definition.type).startsWith("channel.chat."))
				continue;
			QJsonObject condition;
			if (definition.condition == Definition::RaidTo)
				condition.insert("to_broadcaster_user_id", userId);
			else {
				condition.insert("broadcaster_user_id", userId);
				if (definition.condition == Definition::Moderator)
					condition.insert("moderator_user_id", userId);
				if (definition.condition == Definition::Chat)
					condition.insert("user_id", userId);
			}
			const QJsonObject body{{"type", definition.type}, {"version", definition.version}, {"condition", condition},
					       {"transport", QJsonObject{{"method", "websocket"}, {"session_id", sessionId}}}};
			QNetworkRequest request(QUrl("https://api.twitch.tv/helix/eventsub/subscriptions"));
			request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
			request.setRawHeader("Client-Id", clientId.toUtf8());
			request.setRawHeader("Authorization", "Bearer " + accessToken.toUtf8());
			QNetworkReply *reply = network.post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
			connect(reply, &QNetworkReply::finished, this, [this, reply, type = QString::fromLatin1(definition.type), generation = socketGeneration.load()] {
				if (generation != socketGeneration || stopping) { reply->deleteLater(); return; }
				const QJsonObject response = QJsonDocument::fromJson(reply->readAll()).object();
				const int code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
				reply->deleteLater();
				if (code >= 200 && code < 300) {
					++activeSubscriptions;
					if (type == "channel.chat.message") {
						chatSubscribed = true; retryAttempt = 0;
						updateUi(); setStatus("Twitch chat connected as " + accountName + ".");
					}
				} else if (type == "channel.chat.message") {
					stopSocket();
					if (code == 401) refreshAccessToken();
					else if (code == 403) setStatus("Reconnect Twitch to grant chat access: " + response.value("message").toString());
					else scheduleReconnect("Twitch chat subscription failed (" + QString::number(code) + ").");
				}
			});
		}
	}
};

class KickRuntime final : public QObject {
public:
	using EventCallback = std::function<void(const QString &, const QJsonObject &)>;
	explicit KickRuntime(QObject *parent, EventCallback events) : QObject(parent), eventCallback(std::move(events))
	{
		QSettings settings(pulseSettingsPath(), QSettings::IniFormat);
		clientId = PulsePlatformApplicationIds::KickClientId();
		settings.remove("kick/client_id");
		settings.remove("kick/client_secret");
		accessToken = unprotectCredential(settings.value("kick/access_token").toString());
		refreshToken = unprotectCredential(settings.value("kick/refresh_token").toString());
		serverUrl = settings.value("kick/server_url").toString();
		serverUrl = normaliseIngestUrl(serverUrl);
		streamKey = unprotectCredential(settings.value("kick/stream_key").toString());
		accountName = settings.value("kick/account_name").toString();
		broadcasterUserId = settings.value("kick/broadcaster_user_id").toLongLong();
		moderationScopes = settings.value("kick/scopes").toString().split(' ', Qt::SkipEmptyParts);
		tokenExpiresAtMs = settings.value("kick/token_expires_at_ms").toLongLong();
		connect(&callback, &QTcpServer::newConnection, this, [this] { acceptCallback(); });
		relayPollTimer.setInterval(1500);
		connect(&relayPollTimer, &QTimer::timeout, this, [this] { pollRelayEvents(); });
	}
	~KickRuntime() override { relayPollTimer.stop(); stopOutput(); }

	void setWidgets(QLineEdit *application, QLabel *account, QLabel *state, QComboBox *route,
		QPushButton *connectButton, QPushButton *disconnectButton, QLineEdit *chat, QPushButton *send)
	{
		clientField = application; accountLabel = account; status = state; canvasRoute = route;
		chatInput = chat; chatSend = send;
		if (clientField) {
			clientField->setText("Pulse Weaver registered Kick app");
			clientField->setReadOnly(true);
			clientField->setToolTip("The public Kick application is included. Its confidential secret is held only by the Pulse Weaver relay.");
		}
		connect(connectButton, &QPushButton::clicked, this, [this] { beginLogin(); });
		connect(disconnectButton, &QPushButton::clicked, this, [this] { clearLogin(); });
		connect(chatSend, &QPushButton::clicked, this, [this] { sendMessage(); });
		connect(chatInput, &QLineEdit::returnPressed, this, [this] { sendMessage(); });
		updateUi();
		if (!accessToken.isEmpty())
			fetchChannel();
	}
	void bindShell(QWidget *mainWindow)
	{
		if (!mainWindow)
			return;
		shellChatInput = mainWindow->findChild<QLineEdit *>("PulseWeaverChatInput");
		shellChatSend = mainWindow->findChild<QPushButton *>("PulseWeaverChatSend");
		shellChatStatus = mainWindow->findChild<QLabel *>("PulseWeaverChatStatus");
		shellChatFeed = mainWindow->findChild<QListWidget *>("PulseWeaverChatFeed");
		if (shellChatFeed) connect(shellChatFeed, &QListWidget::customContextMenuRequested, this,
			[this](const QPoint &position) { showChatActions(position); });
		shellDestinationStatus = mainWindow->findChild<QLabel *>("PulseWeaverDestinationStatus");
		if (auto *provider = mainWindow->findChild<QComboBox *>("PulseWeaverChatProvider"))
			connect(provider, &QComboBox::currentIndexChanged, this, [this](int) { updateUi(); });
		if (shellChatSend)
			connect(shellChatSend, &QPushButton::clicked, this, [this, mainWindow] {
				auto *provider = mainWindow->findChild<QComboBox *>("PulseWeaverChatProvider");
				if (provider && (provider->currentData().toString() == "kick" || provider->currentData().toString() == "all") && shellChatInput) {
					const bool all = provider->currentData().toString() == "all";
					sendText((all ? shellChatInput->property("pulseWeaverBroadcastMessage").toString() : shellChatInput->text()).trimmed());
					if (!all) shellChatInput->clear();
				}
			});
		if (shellChatInput)
			connect(shellChatInput, &QLineEdit::returnPressed, this, [this, mainWindow] {
				auto *provider = mainWindow->findChild<QComboBox *>("PulseWeaverChatProvider");
				if (provider && (provider->currentData().toString() == "kick" || provider->currentData().toString() == "all") && shellChatInput) {
					const bool all = provider->currentData().toString() == "all";
					sendText((all ? shellChatInput->property("pulseWeaverBroadcastMessage").toString() : shellChatInput->text()).trimmed());
					if (!all) shellChatInput->clear();
				}
			});
		if (auto *outputControl = mainWindow->findChild<QPushButton *>("PulseWeaverKickOutputControl"))
			connect(outputControl, &QPushButton::clicked, this, [this, outputControl] {
				if (outputControl->property("command").toString() == "stop")
					stopOutput();
				else
					startOutput();
			});
		updateUi();
	}

	void beginLogin()
	{
		clientId = PulsePlatformApplicationIds::KickClientId();
		if (clientId.isEmpty()) {
			setStatus("The Pulse Weaver Kick application registration is unavailable in this build.");
			return;
		}
		callback.close();
		if (!callback.listen(QHostAddress::LocalHost, 18757)) {
			setStatus("Kick callback port 18757 is already in use.");
			return;
		}
		stateToken = QUuid::createUuid().toString(QUuid::WithoutBraces);
		QByteArray verifierBytes(48, Qt::Uninitialized);
		for (char &value : verifierBytes)
			value = char(QRandomGenerator::global()->bounded(256));
		codeVerifier = verifierBytes.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
		const QByteArray challenge = QCryptographicHash::hash(codeVerifier, QCryptographicHash::Sha256)
			.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
		QUrl url("https://id.kick.com/oauth/authorize");
		QUrlQuery query;
		query.addQueryItem("response_type", "code"); query.addQueryItem("client_id", clientId);
		query.addQueryItem("redirect_uri", "http://localhost:18757/auth/callback"); query.addQueryItem("state", stateToken);
		query.addQueryItem("scope", "user:read channel:read channel:write chat:write streamkey:read events:subscribe moderation:ban moderation:chat_message:manage");
		query.addQueryItem("code_challenge", QString::fromLatin1(challenge)); query.addQueryItem("code_challenge_method", "S256");
		url.setQuery(query);
		setStatus("Opening Kick in your normal browser…");
		QDesktopServices::openUrl(url);
	}

	void startOutput()
	{
		QWidget *mainWindow = static_cast<QWidget *>(obs_frontend_get_main_window());
		auto *selected = mainWindow ? mainWindow->findChild<QComboBox *>("PulseWeaverDestinationKick") : nullptr;
		const QString outputMode = selected ? selected->currentData().toString() :
			canvasRoute ? canvasRoute->currentData().toString() : QString("off");
		if (output) {
			if (obs_output_active(output))
				return;
			stopOutput();
		}
		if (serverUrl.isEmpty() || streamKey.isEmpty()) {
			setStatus("Kick cannot go live: reconnect the account to obtain its ingest destination.");
			return;
		}
		if (outputMode == "off") {
			setStatus("Kick is connected but its output is Off.");
			return;
		}
		serverUrl = normaliseIngestUrl(serverUrl);
		const bool vertical = outputMode == "vertical";
		const QString route = vertical ? "vertical" : "horizontal";
		obs_canvas_t *canvas = obs_get_canvas_by_name(("Pulse Weaver Output kick " + route).toUtf8().constData());
		if (canvas && !obs_canvas_has_video(canvas)) {
			obs_canvas_release(canvas);
			canvas = nullptr;
		}
		if (!canvas)
			canvas = obs_get_canvas_by_name(vertical ? "Pulse Weaver Vertical" : "Main");
		/* Prefer the GPU texture encoder for secondary outputs. This avoids a
		 * complete CPU x264 pass for Kick while retaining x264 as a fallback. */
		ownedVideo = pulseCreateStreamingEncoder("pulse_weaver_kick_video",
			PulseOutputBitrates::Read(obs_frontend_get_profile_config(), vertical ? 1 : 0));
		if (ownedVideo && canvas)
			obs_encoder_set_video(ownedVideo, obs_canvas_get_video(canvas));
		obs_canvas_release(canvas);
		obs_data_t *audioSettings = obs_data_create();
		obs_data_set_int(audioSettings, "bitrate", 160);
		const int audioMix = std::clamp(mainWindow ? mainWindow->property("pulseWeaverKickAudioMix").toInt() : 0, 0, 5);
		ownedAudio = obs_audio_encoder_create("ffmpeg_aac", "pulse_weaver_kick_audio", audioSettings, audioMix, nullptr);
		obs_data_release(audioSettings);
		if (ownedAudio)
			obs_encoder_set_audio(ownedAudio, obs_get_audio());
		obs_data_t *serviceSettings = obs_data_create();
		obs_data_set_string(serviceSettings, "server", serverUrl.toUtf8().constData());
		obs_data_set_string(serviceSettings, "key", streamKey.toUtf8().constData());
		ownedService = obs_service_create("rtmp_custom", "pulse_weaver_kick_service", serviceSettings, nullptr);
		obs_data_release(serviceSettings);
		output = obs_output_create("rtmp_output", "pulse_weaver_kick_output", nullptr, nullptr);
		PulseLumia::watchOutput(output);
		if (output && ownedService && ownedVideo && ownedAudio) {
			obs_output_set_service(output, ownedService); obs_output_set_video_encoder(output, ownedVideo);
			obs_output_set_audio_encoder(output, ownedAudio, 0); obs_output_set_reconnect_settings(output, 10, 2);
			signal_handler_t *outputSignals = obs_output_get_signal_handler(output);
			signal_handler_connect(outputSignals, "start", outputStarted, this);
			signal_handler_connect(outputSignals, "stop", outputStopped, this);
			activeOutputRoute = vertical ? "9:16" : "16:9";
			if (obs_output_start(output)) {
				setStatus("Kick connecting · " + activeOutputRoute);
			} else {
				const QString error = QString::fromUtf8(obs_output_get_last_error(output));
				blog(LOG_ERROR, "[Pulse Weaver] Kick output failed: %s (server scheme/path validated)",
				     error.toUtf8().constData());
				setStatus("Kick output failed: " + (error.isEmpty() ? QString("libobs rejected the output") : error));
				PulseLumia::publish("destination_state", {{"platform", "kick"}, {"output", "pulse_weaver_kick_output"}, {"state", "failed"}, {"message", error}});
				stopOutput();
			}
		} else { setStatus("Kick could not acquire the selected canvas encoder."); stopOutput(); }
	}

	void stopOutput()
	{
		if (output) { if (obs_output_active(output)) obs_output_stop(output); obs_output_release(output); output = nullptr; }
		if (ownedService) { obs_service_release(ownedService); ownedService = nullptr; }
		if (ownedVideo) { obs_encoder_release(ownedVideo); ownedVideo = nullptr; }
		if (ownedAudio) { obs_encoder_release(ownedAudio); ownedAudio = nullptr; }
		if (QWidget *mainWindow = static_cast<QWidget *>(obs_frontend_get_main_window())) mainWindow->setProperty("pulseWeaverKickLive", false);
	}

private:
	QNetworkAccessManager network{this}; QTcpServer callback{this}; EventCallback eventCallback;
	QTimer relayPollTimer{this};
	QString clientId, accessToken, refreshToken, stateToken, serverUrl, streamKey, accountName;
	QString relaySessionToken;
	qint64 broadcasterUserId = 0;
	qint64 tokenExpiresAtMs = 0;
	qint64 relayCursor = 0;
	bool refreshInFlight = false;
	bool relaySessionInFlight = false;
	bool relayPollInFlight = false;
	int relayFailures = 0;
	QByteArray codeVerifier; obs_output_t *output = nullptr; obs_service_t *ownedService = nullptr;
	obs_encoder_t *ownedVideo = nullptr; obs_encoder_t *ownedAudio = nullptr;
	QString activeOutputRoute;
	QPointer<QLineEdit> clientField, chatInput, shellChatInput;
	QPointer<QLabel> accountLabel, status, shellChatStatus, shellDestinationStatus;
	QPointer<QListWidget> shellChatFeed;
	QStringList moderationScopes;
	void showChatActions(const QPoint &position)
	{
		auto *item = shellChatFeed ? shellChatFeed->itemAt(position) : nullptr;
		if (!item || item->data(PulseChat::Platform).toString() != "kick") return;
		const QString name = item->data(PulseChat::User).toString();
		const QString messageId = item->data(PulseChat::MessageId).toString();
		const QString target = item->data(PulseChat::UserId).toString();
		const QString text = item->data(PulseChat::Text).toString();
		QMenu menu(shellChatFeed);
		menu.addSection("KICK · " + name);
		auto *copy = menu.addAction("Copy message");
		connect(copy, &QAction::triggered, this, [text] { QApplication::clipboard()->setText(text); });
		const bool canDelete = moderationScopes.contains("moderation:chat_message:manage");
		const bool canBan = moderationScopes.contains("moderation:ban");
		if (!canDelete || !canBan) {
			menu.addSection("Reconnect Kick to enable moderation");
			auto *reconnect = menu.addAction("Grant Kick moderation access…");
			connect(reconnect, &QAction::triggered, this, [this] { beginLogin(); });
		}
		auto *remove = menu.addAction("Delete message");
		remove->setEnabled(canDelete && !messageId.isEmpty() && !item->data(PulseChat::Deleted).toBool());
		auto *timeout = menu.addAction("Timeout 10 minutes");
		auto *ban = menu.addAction("Ban user");
		auto *unban = menu.addAction("Unban / remove timeout");
		const bool validTarget = canBan && target.toLongLong() > 0 && target.toLongLong() != broadcasterUserId;
		timeout->setEnabled(validTarget); ban->setEnabled(validTarget); unban->setEnabled(validTarget);
		const QAction *choice = menu.exec(shellChatFeed->viewport()->mapToGlobal(position));
		if (choice == remove) moderateChat(messageId, {}, 0);
		else if (choice == timeout) moderateChat({}, target, 10);
		else if (choice == ban) moderateChat({}, target, 0);
		else if (choice == unban) moderateChat({}, target, -1);
	}
	void moderateChat(const QString &messageId, const QString &target, int minutes, bool retried = false)
	{
		if (accessToken.isEmpty() || broadcasterUserId <= 0) { setStatus("Reconnect Kick before moderating chat."); return; }
		if (!retried && tokenExpired()) {
			refreshAccessToken([this, messageId, target, minutes](bool ok) { if (ok) moderateChat(messageId, target, minutes, true); });
			return;
		}
		const bool deleting = !messageId.isEmpty();
		QNetworkRequest request(QUrl(deleting ? "https://api.kick.com/public/v1/chat/" +
			QString::fromLatin1(QUrl::toPercentEncoding(messageId)) : "https://api.kick.com/public/v1/moderation/bans"));
		request.setTransferTimeout(15000);
		request.setRawHeader("Authorization", "Bearer " + accessToken.toUtf8());
		request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
		const QJsonObject body = PulseChat::kickBanBody(broadcasterUserId, target.toLongLong(), minutes);
		const auto json = QJsonDocument(body).toJson(QJsonDocument::Compact);
		auto *reply = deleting ? network.deleteResource(request) : minutes < 0 ?
			network.sendCustomRequest(request, "DELETE", json) : network.post(request, json);
		setStatus("Applying Kick moderation…");
		connect(reply, &QNetworkReply::finished, this, [this, reply, messageId, target, minutes, deleting, retried] {
			const int code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
			const auto result = QJsonDocument::fromJson(reply->readAll()).object();
			const QString error = reply->errorString(); reply->deleteLater();
			if (code == 401 && !retried) {
				refreshAccessToken([this, messageId, target, minutes](bool ok) { if (ok) moderateChat(messageId, target, minutes, true); }); return;
			}
			if (code >= 200 && code < 300) {
				if (deleting || minutes >= 0) PulseChat::markDeleted(shellChatFeed, "kick", messageId, target);
				setStatus(deleting ? "Kick message deleted." : minutes > 0 ? "Kick user timed out for 10 minutes." :
					minutes < 0 ? "Kick ban / timeout removed." : "Kick user banned.");
			} else setStatus(QString("Kick moderation failed (%1): ").arg(code) + result.value("message").toString(error) +
				(code == 403 ? " Reconnect Kick to grant moderation access." : ""));
		});
	}
	QPointer<QComboBox> canvasRoute; QPointer<QPushButton> chatSend, shellChatSend;
	static constexpr const char *relayOrigin = "https://pulse-weaver-kick-relay.darylbwickham.chatgpt.site";

	static QString normaliseIngestUrl(const QString &value)
	{
		QUrl url(value.trimmed());
		if (!url.isValid() || (url.scheme() != "rtmp" && url.scheme() != "rtmps"))
			return value.trimmed();
		if (url.path().isEmpty() || url.path() == "/")
			url.setPath("/app");
		return url.toString(QUrl::FullyEncoded);
	}
	static void outputStarted(void *data, calldata_t *)
	{
		auto *self = static_cast<KickRuntime *>(data);
		QPointer<KickRuntime> guard(self);
		QMetaObject::invokeMethod(self, [guard] {
			if (!guard)
				return;
			guard->setStatus("Kick LIVE · " + guard->activeOutputRoute);
			if (QWidget *mainWindow = static_cast<QWidget *>(obs_frontend_get_main_window()))
				mainWindow->setProperty("pulseWeaverKickLive", true);
		}, Qt::QueuedConnection);
	}
	static void outputStopped(void *data, calldata_t *parameters)
	{
		auto *self = static_cast<KickRuntime *>(data);
		const int code = int(calldata_int(parameters, "code"));
		const char *lastError = self->output ? obs_output_get_last_error(self->output) : nullptr;
		const QString detail = lastError && *lastError ? QString::fromUtf8(lastError) :
			QString("output code %1").arg(code);
		QPointer<KickRuntime> guard(self);
		QMetaObject::invokeMethod(self, [guard, code, detail] {
			if (!guard)
				return;
			if (QWidget *mainWindow = static_cast<QWidget *>(obs_frontend_get_main_window()))
				mainWindow->setProperty("pulseWeaverKickLive", false);
			if (code == OBS_OUTPUT_SUCCESS) {
				guard->setStatus("Kick stream stopped.");
			} else {
				guard->setStatus("Kick stream stopped unexpectedly: " + detail);
				blog(LOG_ERROR, "[Pulse Weaver] Kick output stopped with code %d: %s", code,
				     detail.toUtf8().constData());
			}
		}, Qt::QueuedConnection);
	}
	bool shellSelected() const
	{
		QWidget *mainWindow = static_cast<QWidget *>(obs_frontend_get_main_window());
		auto *provider = mainWindow ? mainWindow->findChild<QComboBox *>("PulseWeaverChatProvider") : nullptr;
		return provider && (provider->currentData().toString() == "kick" ||
			provider->currentData().toString() == "all");
	}
	void setStatus(const QString &text)
	{
		if (status) status->setText(text);
		if (shellDestinationStatus) shellDestinationStatus->setText(text);
	}
	void updateUi()
	{
		const bool ready = !accessToken.isEmpty() && !streamKey.isEmpty();
		if (accountLabel) accountLabel->setText(ready ? "Connected as " + accountName : "No Kick account connected");
		if (chatInput) chatInput->setEnabled(ready); if (chatSend) chatSend->setEnabled(ready);
		if (QWidget *mainWindow = static_cast<QWidget *>(obs_frontend_get_main_window())) {
			mainWindow->setProperty("pulseWeaverKickReady", ready);
			QMetaObject::invokeMethod(mainWindow, "RefreshPulseWeaverChatComposer", Qt::QueuedConnection);
		}
	}
	void stopRelay()
	{
		relayPollTimer.stop(); relaySessionToken.clear(); relayCursor = 0;
		relaySessionInFlight = false; relayPollInFlight = false; relayFailures = 0;
		if (QWidget *mainWindow = static_cast<QWidget *>(obs_frontend_get_main_window()))
			mainWindow->setProperty("pulseWeaverKickReceiveReady", false);
	}
	void connectRelay(bool retried = false)
	{
		if (relaySessionInFlight || accessToken.isEmpty() || broadcasterUserId <= 0)
			return;
		if (tokenExpired() && !refreshToken.isEmpty() && !retried) {
			refreshAccessToken([this](bool ok) { if (ok) connectRelay(true); });
			return;
		}
		relaySessionInFlight = true;
		QNetworkRequest request(QUrl(QString::fromLatin1(relayOrigin) + "/api/session"));
		request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
		request.setRawHeader("Authorization", "Bearer " + accessToken.toUtf8());
		request.setTransferTimeout(12000);
		QNetworkReply *reply = network.post(request, QByteArray("{}"));
		connect(reply, &QNetworkReply::finished, this, [this, reply] {
			const QByteArray body = reply->readAll();
			const int code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
			const QString networkError = reply->errorString(); reply->deleteLater();
			relaySessionInFlight = false;
			const QJsonObject json = QJsonDocument::fromJson(body).object();
			const QString token = json.value("token").toString();
			if (code < 200 || code >= 300 || token.isEmpty()) {
				stopRelay();
				setStatus("Kick output is connected, but incoming chat could not connect: " +
					json.value("error").toString(code ? "HTTP " + QString::number(code) : networkError));
				QTimer::singleShot(5000, this, [this] { connectRelay(); });
				return;
			}
			relaySessionToken = token;
			relayCursor = qint64(json.value("cursor").toDouble());
			relayFailures = 0; relayPollTimer.start();
			if (QWidget *mainWindow = static_cast<QWidget *>(obs_frontend_get_main_window()))
				mainWindow->setProperty("pulseWeaverKickReceiveReady", true);
			setStatus("Kick account, output and incoming chat are connected."); updateUi();
			pollRelayEvents();
		});
	}
	void pollRelayEvents()
	{
		if (relayPollInFlight || relaySessionToken.isEmpty())
			return;
		relayPollInFlight = true;
		QUrl url(QString::fromLatin1(relayOrigin) + "/api/events");
		QUrlQuery query; query.addQueryItem("cursor", QString::number(relayCursor)); url.setQuery(query);
		QNetworkRequest request(url); request.setRawHeader("Authorization", "Bearer " + relaySessionToken.toUtf8());
		request.setTransferTimeout(10000);
		QNetworkReply *reply = network.get(request);
		connect(reply, &QNetworkReply::finished, this, [this, reply] {
			const QByteArray body = reply->readAll();
			const int code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(); reply->deleteLater();
			relayPollInFlight = false;
			if (code == 401) { stopRelay(); connectRelay(); return; }
			if (code < 200 || code >= 300) {
				if (++relayFailures >= 3) {
					if (QWidget *mainWindow = static_cast<QWidget *>(obs_frontend_get_main_window()))
						mainWindow->setProperty("pulseWeaverKickReceiveReady", false);
					setStatus("Kick incoming chat is reconnecting…");
				}
				return;
			}
			relayFailures = 0;
			if (QWidget *mainWindow = static_cast<QWidget *>(obs_frontend_get_main_window()))
				mainWindow->setProperty("pulseWeaverKickReceiveReady", true);
			const QJsonObject json = QJsonDocument::fromJson(body).object();
			for (const QJsonValue &value : json.value("events").toArray()) {
				const QJsonObject envelope = value.toObject();
				const QString type = envelope.value("type").toString();
				const QJsonObject payload = envelope.value("payload").toObject();
				if (!type.isEmpty() && eventCallback) eventCallback(type, payload);
				relayCursor = std::max(relayCursor, qint64(envelope.value("sequence").toDouble()));
			}
			relayCursor = std::max(relayCursor, qint64(json.value("cursor").toDouble()));
		});
	}
	void save()
	{
		QSettings settings(pulseSettingsPath(), QSettings::IniFormat);
		settings.remove("kick/client_id");
		settings.remove("kick/client_secret");
		settings.setValue("kick/access_token", protectCredential(accessToken)); settings.setValue("kick/refresh_token", protectCredential(refreshToken));
		settings.setValue("kick/server_url", serverUrl); settings.setValue("kick/stream_key", protectCredential(streamKey));
		settings.setValue("kick/account_name", accountName);
		settings.setValue("kick/broadcaster_user_id", broadcasterUserId);
		settings.setValue("kick/token_expires_at_ms", tokenExpiresAtMs);
		settings.setValue("kick/scopes", moderationScopes.join(' '));
	}
	void clearLogin()
	{
		stopRelay(); stopOutput(); accessToken.clear(); refreshToken.clear(); serverUrl.clear(); streamKey.clear(); accountName.clear();
		broadcasterUserId = 0; tokenExpiresAtMs = 0;
		moderationScopes.clear();
		QSettings settings(pulseSettingsPath(), QSettings::IniFormat);
		settings.remove("kick/client_id"); settings.remove("kick/client_secret");
		for (const QString &key : {"kick/access_token", "kick/refresh_token", "kick/server_url", "kick/stream_key",
					   "kick/account_name", "kick/broadcaster_user_id", "kick/token_expires_at_ms", "kick/scopes"}) settings.remove(key);
		setStatus("Kick disconnected; isolated credentials removed."); updateUi();
	}
	void acceptCallback()
	{
		QTcpSocket *socket = callback.nextPendingConnection();
		connect(socket, &QTcpSocket::readyRead, this, [this, socket] {
			const QByteArray request = socket->readAll();
			const QByteArray target = request.split('\n').value(0).split(' ').value(1);
			const QUrl url("http://localhost" + QString::fromUtf8(target)); const QUrlQuery query(url);
			const QString code = query.queryItemValue("code"); const QString state = query.queryItemValue("state");
			const bool valid = !code.isEmpty() && state == stateToken;
			const QByteArray page = valid ? "Kick approved. You can return to Pulse Weaver." : "Kick sign-in could not be verified.";
			socket->write("HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: " + QByteArray::number(page.size()) + "\r\n\r\n" + page);
			socket->disconnectFromHost(); callback.close();
			if (valid) exchangeCode(code); else setStatus("Kick callback state did not match; sign-in was rejected.");
		});
	}
	void exchangeCode(const QString &code)
	{
		QNetworkRequest request(QUrl(QString::fromLatin1(relayOrigin) + "/api/oauth/token"));
		request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
		request.setTransferTimeout(15000);
		const QJsonObject body{{"grant_type", "authorization_code"}, {"code_verifier", QString::fromLatin1(codeVerifier)}, {"code", code}};
		QNetworkReply *reply = network.post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
		connect(reply, &QNetworkReply::finished, this, [this, reply] {
			const QJsonObject json = QJsonDocument::fromJson(reply->readAll()).object(); const int code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(); reply->deleteLater();
			if (code < 200 || code >= 300) { setStatus("Kick token exchange failed: " + json.value("error_description").toString("HTTP " + QString::number(code))); return; }
			accessToken = json.value("access_token").toString(); refreshToken = json.value("refresh_token").toString();
			moderationScopes = json.value("scope").toString().split(' ', Qt::SkipEmptyParts);
			tokenExpiresAtMs = QDateTime::currentMSecsSinceEpoch() + std::max(60, json.value("expires_in").toInt(3600)) * 1000LL;
			save(); fetchChannel();
		});
	}
	bool tokenExpired() const
	{
		return tokenExpiresAtMs > 0 && QDateTime::currentMSecsSinceEpoch() >= tokenExpiresAtMs - 60000;
	}
	void refreshAccessToken(std::function<void(bool)> completed)
	{
		if (refreshInFlight) {
			setStatus("Kick authorization is already refreshing; try again in a moment.");
			completed(false);
			return;
		}
		if (refreshToken.isEmpty()) {
			setStatus("Kick authorization expired. Reconnect Kick in your browser.");
			completed(false);
			return;
		}
		refreshInFlight = true;
		setStatus("Refreshing Kick authorization…");
		QNetworkRequest request(QUrl(QString::fromLatin1(relayOrigin) + "/api/oauth/token"));
		request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
		request.setTransferTimeout(15000);
		const QJsonObject body{{"grant_type", "refresh_token"}, {"refresh_token", refreshToken}};
		QNetworkReply *reply = network.post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
		connect(reply, &QNetworkReply::finished, this, [this, reply, completed = std::move(completed)]() mutable {
			const QByteArray responseBody = reply->readAll();
			const QJsonObject json = QJsonDocument::fromJson(responseBody).object();
			const int code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
			reply->deleteLater();
			refreshInFlight = false;
			const QString nextAccessToken = json.value("access_token").toString();
			if (code < 200 || code >= 300 || nextAccessToken.isEmpty()) {
				accessToken.clear(); tokenExpiresAtMs = 0; updateUi();
				setStatus("Kick authorization refresh failed: " + json.value("error_description").toString(
					json.value("message").toString("HTTP " + QString::number(code))) + ". Reconnect Kick in your browser.");
				completed(false);
				return;
			}
			accessToken = nextAccessToken;
			const QString nextRefreshToken = json.value("refresh_token").toString();
			if (!nextRefreshToken.isEmpty())
				refreshToken = nextRefreshToken;
			tokenExpiresAtMs = QDateTime::currentMSecsSinceEpoch() + std::max(60, json.value("expires_in").toInt(3600)) * 1000LL;
			save(); updateUi(); completed(true);
		});
	}
	void fetchChannel(bool retried = false)
	{
		if ((accessToken.isEmpty() || tokenExpired()) && !refreshToken.isEmpty() && !retried) {
			refreshAccessToken([this](bool ok) { if (ok) fetchChannel(true); });
			return;
		}
		QNetworkRequest request(QUrl("https://api.kick.com/public/v1/channels")); request.setRawHeader("Authorization", "Bearer " + accessToken.toUtf8());
		QNetworkReply *reply = network.get(request);
		connect(reply, &QNetworkReply::finished, this, [this, reply, retried] {
			const QJsonObject json = QJsonDocument::fromJson(reply->readAll()).object(); const int code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(); reply->deleteLater();
			if (code == 401 && !retried && !refreshToken.isEmpty()) {
				refreshAccessToken([this](bool ok) { if (ok) fetchChannel(true); });
				return;
			}
			const QJsonArray data = json.value("data").toArray(); const QJsonObject channel = data.isEmpty() ? QJsonObject{} : data.first().toObject(); const QJsonObject stream = channel.value("stream").toObject();
			if (code < 200 || code >= 300 || stream.value("key").toString().isEmpty()) {
				setStatus("Kick connected, but channel/stream access failed: " + json.value("message").toString("HTTP " + QString::number(code)));
				return;
			}
			accountName = channel.value("slug").toString("Kick creator");
			broadcasterUserId = qint64(channel.value("broadcaster_user_id").toDouble());
			serverUrl = normaliseIngestUrl(stream.value("url").toString());
			streamKey = stream.value("key").toString(); save(); updateUi();
			connectRelay();
			subscribeEvents();
		});
	}
	void subscribeEvents(bool retried = false)
	{
		if (accessToken.isEmpty() || broadcasterUserId <= 0)
			return;
		const QJsonArray events{
			QJsonObject{{"name", "chat.message.sent"}, {"version", 1}},
			QJsonObject{{"name", "channel.followed"}, {"version", 1}},
			QJsonObject{{"name", "channel.subscription.new"}, {"version", 1}},
			QJsonObject{{"name", "channel.subscription.renewal"}, {"version", 1}},
			QJsonObject{{"name", "channel.subscription.gifts"}, {"version", 1}},
			QJsonObject{{"name", "channel.reward.redemption.updated"}, {"version", 1}},
			QJsonObject{{"name", "kicks.gifted"}, {"version", 1}},
			QJsonObject{{"name", "livestream.status.updated"}, {"version", 1}},
		};
		const QJsonObject body{{"broadcaster_user_id", double(broadcasterUserId)},
			{"events", events}, {"method", "webhook"}};
		QNetworkRequest request(QUrl("https://api.kick.com/public/v1/events/subscriptions"));
		request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
		request.setRawHeader("Authorization", "Bearer " + accessToken.toUtf8());
		QNetworkReply *reply = network.post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
		connect(reply, &QNetworkReply::finished, this, [this, reply, retried] {
			const QByteArray responseBody = reply->readAll();
			const QJsonObject json = QJsonDocument::fromJson(responseBody).object();
			const int code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
			reply->deleteLater();
			if (code == 401 && !retried && !refreshToken.isEmpty()) {
				refreshAccessToken([this](bool ok) { if (ok) subscribeEvents(true); });
				return;
			}
			const bool accepted = code >= 200 && code < 300;
			if (QWidget *mainWindow = static_cast<QWidget *>(obs_frontend_get_main_window()))
				mainWindow->setProperty("pulseWeaverKickSubscriptionsReady", accepted);
			setStatus(accepted ? (relaySessionToken.isEmpty() ?
				"Kick event subscriptions are ready; connecting incoming chat…" :
				"Kick account, output and incoming chat are connected.") :
				"Kick connected, but incoming-event subscription failed: " +
					json.value("message").toString("HTTP " + QString::number(code)));
		});
	}
	void sendMessage()
	{
		const QString text = chatInput ? chatInput->text().trimmed() : QString(); sendText(text); if (chatInput) chatInput->clear();
	}
	void sendText(const QString &text, bool retried = false)
	{
		if (text.isEmpty()) return;
		if ((accessToken.isEmpty() || tokenExpired()) && !refreshToken.isEmpty() && !retried) {
			refreshAccessToken([this, text](bool ok) { if (ok) sendText(text, true); });
			return;
		}
		if (accessToken.isEmpty()) { setStatus("Kick is not authorized. Reconnect Kick in your browser."); return; }
		if (broadcasterUserId <= 0) { setStatus("Kick is still loading your channel identity; reconnect if this does not clear."); fetchChannel(); return; }
		QNetworkRequest request(QUrl("https://api.kick.com/public/v1/chat")); request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json"); request.setRawHeader("Authorization", "Bearer " + accessToken.toUtf8());
		const QJsonObject body{{"content",text}, {"type","user"}, {"broadcaster_user_id", double(broadcasterUserId)}};
		QNetworkReply *reply = network.post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
		connect(reply, &QNetworkReply::finished, this, [this, reply, text, retried] {
			const QJsonObject json = QJsonDocument::fromJson(reply->readAll()).object();
			const int code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(); reply->deleteLater();
			if (code == 401 && !retried && !refreshToken.isEmpty()) {
				refreshAccessToken([this, text](bool ok) { if (ok) sendText(text, true); });
				return;
			}
			setStatus(code >= 200 && code < 300 ? "Kick chat message sent." :
				"Kick chat send failed: " + json.value("message").toString("HTTP " + QString::number(code)));
		});
	}
};

struct Rule {
	QString eventKey;
	QString action;
	QString target;
	QString value;
	QString conditionField;
	QString conditionOperator = "Always";
	QString conditionValue;
	int delayMs = 0;
	bool enabled = true;
};

struct SourceRow {
	QString name;
	QString uuid;
	int64_t itemId = -1;
	bool visible = true;
};

struct AudioRow {
	QString name;
	QString uuid;
	float volume = 1.0f;
	bool muted = false;
};

struct BotCommand {
	QString command;
	QString reply;
};

struct ModuleAction {
	QString moduleId;
	QString moduleName;
	QString actionId;
	QString actionName;
	QString procName;

	QString displayName() const { return "Module · " + moduleName + " · " + actionName; }
};

class LanScanner final : public QObject {
public:
	using FoundCallback = std::function<void(const QString &)>;
	using ProgressCallback = std::function<void(int, int)>;

	explicit LanScanner(QObject *parent) : QObject(parent) {}

	void start(const QString &prefix, quint16 port, FoundCallback found, ProgressCallback progress)
	{
		++generation;
		queue.clear();
		active = completed = 0;
		this->port = port;
		this->found = std::move(found);
		this->progress = std::move(progress);
		for (int host = 1; host < 255; ++host)
			queue.enqueue(prefix + "." + QString::number(host));
		total = queue.size();
		pump(generation);
	}

private:
	QQueue<QString> queue;
	int generation = 0;
	int active = 0;
	int completed = 0;
	int total = 0;
	quint16 port = 80;
	FoundCallback found;
	ProgressCallback progress;

	void pump(int run)
	{
		if (run != generation)
			return;
		while (active < 12 && !queue.isEmpty()) {
			const QString host = queue.dequeue();
			++active;
			auto *socket = new QTcpSocket(this);
			auto finished = std::make_shared<bool>(false);
			auto complete = [this, run, host, socket, finished](bool connected) {
				if (*finished || run != generation)
					return;
				*finished = true;
				if (connected && found)
					found(host);
				++completed;
				--active;
				if (progress)
					progress(completed, total);
				socket->abort();
				socket->deleteLater();
				pump(run);
			};
			connect(socket, &QTcpSocket::connected, this, [complete] { complete(true); });
			connect(socket, &QTcpSocket::errorOccurred, this,
				[complete](QAbstractSocket::SocketError) { complete(false); });
			QTimer::singleShot(650, socket, [complete] { complete(false); });
			socket->connectToHost(host, port);
		}
	}
};

class PulseWeaverDock final : public QWidget {
	PulseLumiaBridge *lumiaBridge = nullptr;
public:
	explicit PulseWeaverDock(QWidget *parent = nullptr) : QWidget(parent)
	{
		QSettings settings(pulseSettingsPath(), QSettings::IniFormat);
		apiToken = settings.value("api/token").toString();
		if (apiToken.isEmpty()) {
			apiToken = QUuid::createUuid().toString(QUuid::WithoutBraces).remove('-');
			settings.setValue("api/token", apiToken);
		}
		setObjectName("PulseWeaverNativeCore");
		setMinimumWidth(430);
		// Use the application theme, including when these pages are mounted in the shell.

		auto *root = new QVBoxLayout(this);
		root->setContentsMargins(0, 0, 0, 0);
		root->setSpacing(6);

		tabs = new QTabWidget;
		tabs->setObjectName("PulseWeaverPlatformTabs");
		root->addWidget(tabs, 1);
		auto *connectionsPage = new QWidget(tabs);
		auto *connectionsLayout = new QVBoxLayout(connectionsPage);
		connectionsLayout->setContentsMargins(6, 6, 6, 6);
		connectionsTabs = new QTabWidget(connectionsPage);
		connectionsTabs->setObjectName("PulseWeaverConnectionTabs");
		connectionsLayout->addWidget(connectionsTabs, 1);
		tabs->addTab(connectionsPage, "CONNECTIONS");
		status = new QLabel;
		status->setObjectName("Muted");
		status->setWordWrap(false);
		root->addWidget(status);
		twitch = new TwitchRuntime(
			this,
			[this](const QString &type, const QJsonObject &data) {
				publishEvent("twitch", type, QString::fromUtf8(QJsonDocument(data).toJson(QJsonDocument::Compact)));
			},
			[this](const QString &user, const QString &message, const QJsonObject &) { handleTwitchChat(user, message); });
		kick = new KickRuntime(this, [this](const QString &type, const QJsonObject &data) {
			publishEvent("kick", type, QString::fromUtf8(QJsonDocument(data).toJson(QJsonDocument::Compact)));
		});
		overlays = new PulseOverlayRuntime(this, [this](const QString &message) {
			if (eventLog)
				eventLog->append("<span style='color:#fb7185'>" + message.toHtmlEscaped() + "</span>");
		});
		motion = new PulseMotionEngine(this, QFileInfo(pulseSettingsPath()).dir().filePath("pulseweaver-motion-actions.json"),
			[this](QJsonObject event) {
				if (lumiaBridge) lumiaBridge->deliver(event);
				const QString type = event.value("event").toString("motion_state");
				publishEvent("pulseweaver", type, QString::fromUtf8(QJsonDocument(event).toJson(QJsonDocument::Compact)));
			});
		actionNetwork = new QNetworkAccessManager(this);
		loadBotCommands();
		buildTwitchTab();
		buildYouTubeTab();
		buildKickTab();
		buildStreamInfoPanel();
		buildChatSettingsTab();
		buildEventsTab();
		buildAutomationTab();
		buildAiTab();
		buildApiTab();
		QWidget *mainWindow = static_cast<QWidget *>(obs_frontend_get_main_window());
		QWidget *motionMount = mainWindow ? mainWindow->findChild<QWidget *>("PulseWeaverMotionPluginMount") : nullptr;
		if (motionMount && motionMount->layout())
			motionMount->layout()->addWidget(motion->createEditor(motionMount));
		else
			tabs->addTab(motion->createEditor(tabs), "MOTION");
		/* Keep unfinished systems intact for continued development without
		 * presenting them as operator-ready. Connections and stream metadata are
		 * the only Action surfaces enabled in this preview. */
		auto *comingSoonPage = new QWidget(tabs);
		auto *comingSoonLayout = new QVBoxLayout(comingSoonPage);
		auto *comingSoonTitle = new QLabel("ACTION EXPANSION · COMING SOON", comingSoonPage);
		comingSoonTitle->setObjectName("Kicker");
		comingSoonLayout->addWidget(comingSoonTitle);
		auto *comingSoonCopy = new QLabel(
			"Events, alerts, automations, Pulse AI and the public plug-in control surface are retained but hidden "
			"until they are ready for dependable live use.", comingSoonPage);
		comingSoonCopy->setWordWrap(true);
		comingSoonCopy->setObjectName("Muted");
		comingSoonLayout->addWidget(comingSoonCopy);
		comingSoonLayout->addStretch();
		tabs->addTab(comingSoonPage, "COMING SOON");
		for (int index = 0; index < tabs->count(); ++index) {
			const QString label = tabs->tabText(index);
			tabs->setTabVisible(index, label == "CONNECTIONS" || (label == "MOTION" && !motionMount) || label == "COMING SOON");
		}
		loadRules();
		lumiaBridge = new PulseLumiaBridge(this, [this] { return lumiaStateJson(); });
		startApi();
		refreshAll();
		twitch->bindShell(static_cast<QWidget *>(obs_frontend_get_main_window()));
		kick->bindShell(static_cast<QWidget *>(obs_frontend_get_main_window()));
		overlays->bindShell(static_cast<QWidget *>(obs_frontend_get_main_window()));
		if (QWidget *mainWindow = static_cast<QWidget *>(obs_frontend_get_main_window())) {
			if (auto *aiButton = mainWindow->findChild<QPushButton *>("PulseWeaverAiButton"))
				connect(aiButton, &QPushButton::clicked, this, [this] { tabs->setCurrentWidget(aiPage); });
		}
	}

	~PulseWeaverDock() override { delete lumiaBridge; lumiaBridge = nullptr; saveRules(); }

	void refreshAll()
	{
		refreshing = true;
		if (sceneList)
			refreshScenes();
		if (sourceTree)
			refreshSources();
		if (audioTable)
			refreshAudio();
		refreshTargets();
		refreshing = false;
		refreshStatus();
	}

	void refreshStatus()
	{
		status->setText(QString("Platform connections  •  Stream %1  •  Recording %2")
					.arg(obs_frontend_streaming_active() ? "LIVE" : "idle")
					.arg(obs_frontend_recording_active() ? "ON" : "idle"));
		if (streamButton)
			streamButton->setText(obs_frontend_streaming_active() ? "STOP STREAM" : "START STREAM");
		if (recordButton)
			recordButton->setText(obs_frontend_recording_active() ? "STOP RECORDING" : "START RECORDING");
	}

	void publishEvent(const QString &platform, const QString &type, const QString &json)
	{
		const QString key = platform + "." + type;
		QJsonParseError parseError{};
		const QJsonDocument parsed = QJsonDocument::fromJson(json.toUtf8(), &parseError);
		QJsonObject eventData = parseError.error == QJsonParseError::NoError && parsed.isObject()
						      ? parsed.object()
						      : QJsonObject{{"value", json}};
		const QString canonical = canonicalEventKey(platform, type);
		if (!canonical.isEmpty()) {
			eventData.insert("canonical", canonical);
			eventData = PulseOverlay::normalizeAlertEvent(eventData, canonical, platform);
		}
		const QString line = QDateTime::currentDateTime().toString("HH:mm:ss") + "  " + key + "  " + json;
		eventLog->append(line.toHtmlEscaped());
		if (QWidget *mainWindow = static_cast<QWidget *>(obs_frontend_get_main_window())) {
			if (auto *activity = mainWindow->findChild<QListWidget *>("PulseWeaverActivity")) {
				activity->insertItem(0, line);
				while (activity->count() > 100)
					delete activity->takeItem(activity->count() - 1);
			}
			if (platform == "kick" && canonical == "chat.message") {
				if (auto *feed = mainWindow->findChild<QListWidget *>("PulseWeaverChatFeed")) {
					QString user = eventData.value("username").toString(eventData.value("user").toString());
					QString message = eventData.value("message").toString(eventData.value("content").toString());
					const QJsonObject sender = eventData.value("sender").toObject();
					if (user.isEmpty()) user = sender.value("username").toString(sender.value("name").toString("Kick"));
					QStringList badges;
					QHash<QString, QUrl> badgeImages;
					const QJsonObject identity = sender.value("identity").toObject();
					for (const QJsonValue &value : identity.value("badges").toArray(sender.value("badges").toArray())) {
						const QJsonObject badge = value.toObject();
						const QString name = badge.value("type").toString(badge.value("name").toString(badge.value("text").toString(value.toString())));
						if (name.isEmpty())
							continue;
						badges << name;
						const QUrl image(badge.value("image_url").toString(
							badge.value("icon_url").toString(badge.value("image").toString())));
						if (image.isValid())
							badgeImages.insert(name, image);
					}
					pulseAppendUnifiedChat(feed, "kick", user, message,
						identity.value("username_color").toString(sender.value("color").toString(eventData.value("color").toString())), badges,
						badgeImages, sender.value("user_id").toVariant().toString().isEmpty() ? sender.value("id").toVariant().toString() : sender.value("user_id").toVariant().toString(),
						eventData.value("id").toString(eventData.value("message_id").toString()));
				}
			}
		}

		calldata_t data;
		calldata_init(&data);
		calldata_set_string(&data, "platform", platform.toUtf8().constData());
		calldata_set_string(&data, "type", type.toUtf8().constData());
		calldata_set_string(&data, "json", json.toUtf8().constData());
		signal_handler_signal(obs_get_signal_handler(), "pulseweaver_event", &data);
		calldata_free(&data);
		if (overlays)
			overlays->publishEvent(key, eventData);
		if (overlays && !canonical.isEmpty())
			overlays->publishEvent(canonical, eventData);

		for (const Rule &rule : rules) {
			if (!rule.enabled || (rule.eventKey != key && rule.eventKey != canonical && rule.eventKey != "*"))
				continue;
			executeRule(rule, eventData);
		}
	}

	bool activateScene(const QString &name)
	{
		obs_frontend_source_list scenes = {};
		obs_frontend_get_scenes(&scenes);
		bool found = false;
		for (size_t i = 0; i < scenes.sources.num; ++i) {
			obs_source_t *source = scenes.sources.array[i];
			if (name == QString::fromUtf8(obs_source_get_name(source))) {
				obs_frontend_set_current_scene(source);
				found = true;
				break;
			}
		}
		obs_frontend_source_list_free(&scenes);
		return found;
	}

	void onFrontendEvent(obs_frontend_event event)
	{
		if (lumiaBridge) lumiaBridge->frontendEvent(event);
		if (motion) motion->frontendEvent(event);
		switch (event) {
		case OBS_FRONTEND_EVENT_FINISHED_LOADING:
			if (overlays) overlays->sceneCollectionLoaded();
			break;
		case OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGED:
			if (overlays) overlays->sceneCollectionLoaded();
			refreshAll();
			break;
		case OBS_FRONTEND_EVENT_SCENE_CHANGED:
		case OBS_FRONTEND_EVENT_SCENE_LIST_CHANGED:
		case OBS_FRONTEND_EVENT_CANVAS_ADDED:
		case OBS_FRONTEND_EVENT_CANVAS_REMOVED:
			refreshAll();
			break;
		case OBS_FRONTEND_EVENT_STREAMING_STARTED:
			publishEvent("pulseweaver", "stream.started", "{}");
			refreshStatus();
			break;
		case OBS_FRONTEND_EVENT_STREAMING_STOPPING:
			break;
		case OBS_FRONTEND_EVENT_STREAMING_STOPPED:
			publishEvent("pulseweaver", "stream.stopped", "{}");
			refreshStatus();
			break;
		case OBS_FRONTEND_EVENT_RECORDING_STARTED:
			publishEvent("pulseweaver", "recording.started", "{}");
			refreshStatus();
			break;
		case OBS_FRONTEND_EVENT_RECORDING_STOPPED:
			publishEvent("pulseweaver", "recording.stopped", "{}");
			refreshStatus();
			break;
		default:
			break;
		}
	}

	void registerModuleAction(const ModuleAction &action)
	{
		for (const ModuleAction &existing : moduleActions)
			if (existing.moduleId == action.moduleId && existing.actionId == action.actionId)
				return;
		moduleActions.push_back(action);
		if (ruleAction)
			ruleAction->addItem(action.displayName());
		refreshModuleActions();
		publishEvent("plugin", "action.registered",
			QString::fromUtf8(QJsonDocument(QJsonObject{{"module", action.moduleId}, {"action", action.actionId}}).toJson(QJsonDocument::Compact)));
	}

private:
	QTabWidget *tabs = nullptr;
	QTabWidget *connectionsTabs = nullptr;
	QLabel *status = nullptr;
	QListWidget *sceneList = nullptr;
	QTreeWidget *sourceTree = nullptr;
	QTableWidget *audioTable = nullptr;
	QTreeWidget *eventTree = nullptr;
	QTextEdit *eventLog = nullptr;
	QComboBox *ruleEvent = nullptr;
	QComboBox *ruleAction = nullptr;
	QComboBox *ruleTarget = nullptr;
	QLineEdit *ruleValue = nullptr;
	QLineEdit *ruleConditionField = nullptr;
	QComboBox *ruleConditionOperator = nullptr;
	QLineEdit *ruleConditionValue = nullptr;
	QSpinBox *ruleDelay = nullptr;
	QTableWidget *rulesTable = nullptr;
	QPushButton *streamButton = nullptr;
	QPushButton *recordButton = nullptr;
	TwitchRuntime *twitch = nullptr;
	KickRuntime *kick = nullptr;
	PulseOverlayRuntime *overlays = nullptr;
	PulseMotionEngine *motion = nullptr;
	QNetworkAccessManager *actionNetwork = nullptr;
	QLabel *twitchAccount = nullptr;
	QLabel *twitchStatus = nullptr;
	QLineEdit *streamTitle = nullptr;
	QComboBox *streamCategory = nullptr;
	QPushButton *botEnabled = nullptr;
	QLineEdit *botCommand = nullptr;
	QLineEdit *botReply = nullptr;
	QTableWidget *botTable = nullptr;
	QWidget *aiPage = nullptr;
	QTextEdit *aiPrompt = nullptr;
	QTextEdit *aiResult = nullptr;
	QLabel *aiStatus = nullptr;
	QPushButton *aiRun = nullptr;
	QPushButton *aiUndo = nullptr;
	QPointer<QProcess> aiProcess;
	QJsonObject lastAiRestore;
	QTreeWidget *moduleTree = nullptr;
	QTcpServer *api = nullptr;
#if defined(PULSEWEAVER_MOTION_PREVIEW)
	quint16 apiPort = 18765;
#else
	quint16 apiPort = 18755;
#endif
	QString apiToken;
	bool refreshing = false;
	std::vector<Rule> rules;
	std::vector<BotCommand> botCommands;
	std::vector<ModuleAction> moduleActions;

public:
	void mountLights(QWidget *host)
	{
		if (!host || !host->layout())
			return;
		/* The full implementation below is deliberately retained. The shell marks
		 * this mount as deferred so unfinished controls consume no runtime/UI work. */
		if (host->property("pulseWeaverComingSoon").toBool())
			return;
		auto *surface = new QWidget(host);
		auto *layout = new QVBoxLayout(surface);
		layout->setContentsMargins(0, 8, 0, 0);
		layout->setSpacing(8);

		auto *tabs = new QTabWidget;
		layout->addWidget(tabs, 1);
		auto *devicesPage = new QWidget;
		auto *devicesLayout = new QVBoxLayout(devicesPage);
		auto *form = new QHBoxLayout;
		auto *provider = new QComboBox;
		provider->addItems({"WLED", "Philips Hue", "Home Assistant", "HTTP / DIY", "Display / Pixel device"});
		auto *address = new QLineEdit;
		address->setPlaceholderText("Device IP or hostname — for example 192.168.1.50");
		auto *add = new QPushButton("ADD / TEST DEVICE");
		add->setObjectName("Primary");
		form->addWidget(provider);
		form->addWidget(address, 1);
		form->addWidget(add);
		devicesLayout->addLayout(form);

		auto *discovery = new QHBoxLayout;
		auto *subnet = new QComboBox;
		subnet->setEditable(true);
		for (const QHostAddress &hostAddress : QNetworkInterface::allAddresses()) {
			if (hostAddress.protocol() != QAbstractSocket::IPv4Protocol || hostAddress.isLoopback())
				continue;
			const QString ip = hostAddress.toString();
			const QString prefix = ip.section('.', 0, 2);
			if (!prefix.isEmpty() && subnet->findText(prefix) < 0)
				subnet->addItem(prefix);
		}
		subnet->setPlaceholderText("Local /24 prefix, e.g. 192.168.1");
		auto *discover = new QPushButton("DISCOVER DEVICES ON LOCAL /24");
		auto *scanProgress = new QProgressBar;
		scanProgress->setRange(0, 254);
		scanProgress->setValue(0);
		scanProgress->setMaximumWidth(220);
		discovery->addWidget(subnet, 1);
		discovery->addWidget(discover);
		discovery->addWidget(scanProgress);
		devicesLayout->addLayout(discovery);

		auto *devices = new QTreeWidget;
		devices->setHeaderLabels({"DEVICE / DISPLAY", "PROVIDER", "ADDRESS", "STATUS"});
		devices->header()->setSectionResizeMode(0, QHeaderView::Stretch);
		devices->header()->setSectionResizeMode(2, QHeaderView::Stretch);
		devicesLayout->addWidget(devices, 1);
		auto *statusLabel = new QLabel("Add a known address or scan one selected local /24. Discovery uses at most 12 short TCP probes at once.");
		statusLabel->setObjectName("Muted");
		statusLabel->setWordWrap(true);
		devicesLayout->addWidget(statusLabel);

		auto *removeDevice = new QPushButton("REMOVE SELECTED DEVICE");
		devicesLayout->addWidget(removeDevice, 0, Qt::AlignRight);
		tabs->addTab(devicesPage, "DEVICES + DISPLAYS");

		auto *controlPage = new QWidget;
		auto *controlLayout = new QVBoxLayout(controlPage);
		auto *selectedDevice = new QLabel("Select a device in Devices + Displays.");
		selectedDevice->setObjectName("Kicker");
		controlLayout->addWidget(selectedDevice);
		auto *controlForm = new QFormLayout;
		auto *authToken = new QLineEdit;
		authToken->setEchoMode(QLineEdit::Password);
		authToken->setPlaceholderText("Hue bridge username or Home Assistant long-lived token");
		auto *entity = new QLineEdit;
		entity->setPlaceholderText("Hue light ID, Home Assistant entity_id or DIY path");
		auto *brightness = new QSlider(Qt::Horizontal);
		brightness->setRange(1, 255);
		brightness->setValue(180);
		auto *power = new QPushButton("●  OUTPUT ON");
		power->setCheckable(true);
		power->setChecked(true);
		auto *colour = new QPushButton("COLOUR  #D946EF");
		auto selectedColour = std::make_shared<QColor>("#D946EF");
		controlForm->addRow("Provider credential", authToken);
		controlForm->addRow("Entity / path", entity);
		controlForm->addRow("Brightness", brightness);
		controlForm->addRow("Power", power);
		controlForm->addRow("Colour", colour);
		controlLayout->addLayout(controlForm);
		auto *apply = new QPushButton("APPLY LOOK TO SELECTED DEVICE");
		apply->setObjectName("Primary");
		controlLayout->addWidget(apply);
		auto *controlStatus = new QLabel("Pulse Weaver sends provider-native local requests. Credentials remain in this isolated portable profile.");
		controlStatus->setObjectName("Muted");
		controlStatus->setWordWrap(true);
		controlLayout->addWidget(controlStatus);
		controlLayout->addStretch();
		tabs->addTab(controlPage, "LIVE CONTROL");

		auto *looksPage = new QWidget;
		auto *looksLayout = new QVBoxLayout(looksPage);
		auto *lookName = new QLineEdit;
		lookName->setPlaceholderText("Look name, e.g. Raid Warning");
		auto *saveLook = new QPushButton("SAVE CURRENT COLOUR / LEVEL AS LOOK");
		auto *looks = new QListWidget;
		looksLayout->addWidget(lookName);
		looksLayout->addWidget(saveLook);
		looksLayout->addWidget(looks, 1);
		looksLayout->addWidget(new QLabel("Double-click a look to load it, then apply it to any selected device or display."));
		tabs->addTab(looksPage, "SHOW LOOKS");

		QSettings settings(pulseSettingsPath(), QSettings::IniFormat);
		for (const QString &saved : settings.value("lights/devices").toStringList()) {
			const QStringList fields = saved.split('|');
			if (fields.size() >= 2)
				new QTreeWidgetItem(devices, {fields.value(2, fields.value(1)), fields.value(0), fields.value(1), "Saved"});
		}
		for (const QString &saved : settings.value("lights/looks").toStringList()) {
			const QStringList fields = saved.split('|');
			if (fields.size() >= 4) {
				auto *item = new QListWidgetItem(fields[0] + "  •  " + fields[1] + "  •  " + fields[2], looks);
				item->setData(Qt::UserRole, saved);
			}
		}

		auto persistDevices = [devices] {
			QStringList values;
			for (int row = 0; row < devices->topLevelItemCount(); ++row) {
				QTreeWidgetItem *item = devices->topLevelItem(row);
				values.append(item->text(1) + "|" + item->text(2) + "|" + item->text(0));
			}
			QSettings saved(pulseSettingsPath(), QSettings::IniFormat);
			saved.setValue("lights/devices", values);
		};
		auto addDevice = [devices, persistDevices](const QString &deviceProvider, const QString &endpoint,
						       const QString &state) {
			for (int row = 0; row < devices->topLevelItemCount(); ++row) {
				if (devices->topLevelItem(row)->text(2) == endpoint) {
					devices->topLevelItem(row)->setText(3, state);
					return;
				}
			}
			new QTreeWidgetItem(devices, {endpoint, deviceProvider, endpoint, state});
			persistDevices();
		};

		connect(add, &QPushButton::clicked, surface, [provider, address, statusLabel, addDevice] {
			QString hostName = address->text().trimmed();
			if (hostName.isEmpty()) {
				statusLabel->setText("Enter an IP address or hostname first.");
				return;
			}
			hostName.remove("http://", Qt::CaseInsensitive);
			hostName.remove("https://", Qt::CaseInsensitive);
			hostName = hostName.section('/', 0, 0);
			quint16 port = provider->currentText() == "Home Assistant" ? 8123 : 80;
			if (hostName.contains(':')) {
				bool ok = false;
				const quint16 explicitPort = hostName.section(':', 1, 1).toUShort(&ok);
				if (ok)
					port = explicitPort;
				hostName = hostName.section(':', 0, 0);
			}
			auto *socket = new QTcpSocket(statusLabel);
			statusLabel->setText("Testing " + hostName + "…");
			QObject::connect(socket, &QTcpSocket::connected, statusLabel,
					 [socket, provider, hostName, statusLabel, port, addDevice] {
						 const QString endpoint = hostName + (port == 80 ? "" : ":" + QString::number(port));
						 addDevice(provider->currentText(), endpoint, "Online");
						 statusLabel->setText("Device is reachable and saved inside Pulse Weaver.");
						 socket->disconnectFromHost();
						 socket->deleteLater();
					 });
			QObject::connect(socket, &QTcpSocket::errorOccurred, statusLabel, [socket, statusLabel](QAbstractSocket::SocketError) {
				statusLabel->setText("Device did not respond: " + socket->errorString());
				socket->deleteLater();
			});
			socket->connectToHost(hostName, port);
		});

		auto *scanner = new LanScanner(surface);
		connect(discover, &QPushButton::clicked, surface, [scanner, subnet, provider, scanProgress, statusLabel, addDevice] {
			const QString prefix = subnet->currentText().trimmed();
			QRegularExpressionMatch match = QRegularExpression(R"(^\d{1,3}\.\d{1,3}\.\d{1,3}$)").match(prefix);
			if (!match.hasMatch()) {
				statusLabel->setText("Enter a three-part local prefix such as 192.168.1.");
				return;
			}
			const quint16 port = provider->currentText() == "Home Assistant" ? 8123 : 80;
			scanProgress->setValue(0);
			statusLabel->setText("Scanning " + prefix + ".1–254 on port " + QString::number(port) + "…");
			scanner->start(prefix, port,
				[provider, addDevice, port](const QString &hostName) {
					addDevice(provider->currentText(), hostName + (port == 80 ? "" : ":" + QString::number(port)), "Discovered");
				},
				[scanProgress, statusLabel](int complete, int total) {
					scanProgress->setMaximum(total);
					scanProgress->setValue(complete);
					if (complete == total)
						statusLabel->setText("Discovery complete. Review discovered addresses before controlling them.");
				});
		});

		connect(removeDevice, &QPushButton::clicked, surface, [devices, persistDevices] {
			delete devices->takeTopLevelItem(devices->indexOfTopLevelItem(devices->currentItem()));
			persistDevices();
		});
		connect(devices, &QTreeWidget::currentItemChanged, surface, [selectedDevice, authToken, entity](QTreeWidgetItem *item) {
			if (!item)
				return;
			selectedDevice->setText(item->text(0) + "  •  " + item->text(1));
			QSettings settings(pulseSettingsPath(), QSettings::IniFormat);
			authToken->setText(unprotectCredential(settings.value("lights/auth/" + item->text(2)).toString()));
			entity->setText(settings.value("lights/entity/" + item->text(2)).toString());
		});
		connect(power, &QPushButton::toggled, surface, [power](bool on) { power->setText(on ? "●  OUTPUT ON" : "○  OUTPUT OFF"); });
		connect(colour, &QPushButton::clicked, surface, [surface, colour, selectedColour] {
			const QColor chosen = QColorDialog::getColor(*selectedColour, surface, "Choose show colour");
			if (chosen.isValid()) {
				*selectedColour = chosen;
				colour->setText("COLOUR  " + chosen.name().toUpper());
				colour->setStyleSheet("border-color:" + chosen.name());
			}
		});
		connect(apply, &QPushButton::clicked, surface, [this, surface, devices, authToken, entity, brightness, power, controlStatus, selectedColour] {
			QTreeWidgetItem *item = devices->currentItem();
			if (!item) {
				controlStatus->setText("Select a device first.");
				return;
			}
			const QString providerName = item->text(1);
			const QString endpoint = item->text(2);
			QUrl url("http://" + endpoint);
			QJsonObject body{{"on", power->isChecked()}, {"bri", brightness->value()},
					 {"rgb", QJsonArray{selectedColour->red(), selectedColour->green(), selectedColour->blue()}}};
			QNetworkRequest request;
			if (providerName == "WLED") {
				url.setPath("/json/state");
				body.remove("rgb");
				body.insert("seg", QJsonArray{QJsonObject{{"col", QJsonArray{QJsonArray{selectedColour->red(), selectedColour->green(), selectedColour->blue()}}}}});
			} else if (providerName == "Home Assistant") {
				url.setPath(power->isChecked() ? "/api/services/light/turn_on" : "/api/services/light/turn_off");
				body.insert("entity_id", entity->text().trimmed());
				body.insert("brightness", brightness->value());
				body.insert("rgb_color", body.take("rgb"));
			} else if (providerName == "Philips Hue") {
				url.setPath("/api/" + authToken->text().trimmed() + "/lights/" + entity->text().trimmed() + "/state");
				body.remove("rgb");
				body.insert("hue", int(std::max(0.0f, selectedColour->hsvHueF()) * 65535));
				body.insert("sat", int(selectedColour->hsvSaturationF() * 254));
			} else {
				QString path = entity->text().trimmed();
				url.setPath(path.startsWith('/') ? path : "/" + (path.isEmpty() ? QString("pulseweaver") : path));
			}
			request.setUrl(url);
			request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
			if (providerName == "Home Assistant" && !authToken->text().isEmpty())
				request.setRawHeader("Authorization", "Bearer " + authToken->text().toUtf8());
			QSettings saved(pulseSettingsPath(), QSettings::IniFormat);
			saved.setValue("lights/auth/" + endpoint, protectCredential(authToken->text()));
			saved.setValue("lights/entity/" + endpoint, entity->text().trimmed());
			QNetworkReply *reply = actionNetwork->post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
			controlStatus->setText("Sending look to " + url.toString() + "…");
			connect(reply, &QNetworkReply::finished, surface, [reply, controlStatus, item] {
				const int code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
				const bool ok = reply->error() == QNetworkReply::NoError && code >= 200 && code < 300;
				controlStatus->setText(ok ? "Device accepted the show look." : "Device control failed: HTTP " + QString::number(code) + " — " + reply->errorString());
				item->setText(3, ok ? "Controlled" : "Error");
				reply->deleteLater();
			});
		});

		connect(saveLook, &QPushButton::clicked, surface, [lookName, looks, brightness, power, selectedColour] {
			const QString name = lookName->text().trimmed();
			if (name.isEmpty())
				return;
			const QString value = name + "|" + selectedColour->name() + "|" + QString::number(brightness->value()) + "|" + (power->isChecked() ? "1" : "0");
			QSettings settings(pulseSettingsPath(), QSettings::IniFormat);
			QStringList saved = settings.value("lights/looks").toStringList();
			saved.removeIf([&name](const QString &entry) { return entry.section('|', 0, 0).compare(name, Qt::CaseInsensitive) == 0; });
			saved.append(value);
			settings.setValue("lights/looks", saved);
			auto *item = new QListWidgetItem(name + "  •  " + selectedColour->name() + "  •  " + QString::number(brightness->value()), looks);
			item->setData(Qt::UserRole, value);
			lookName->clear();
		});
		connect(looks, &QListWidget::itemDoubleClicked, surface, [brightness, power, colour, selectedColour](QListWidgetItem *item) {
			const QStringList fields = item->data(Qt::UserRole).toString().split('|');
			if (fields.size() < 4)
				return;
			*selectedColour = QColor(fields[1]);
			brightness->setValue(fields[2].toInt());
			power->setChecked(fields[3] == "1");
			colour->setText("COLOUR  " + selectedColour->name().toUpper());
			colour->setStyleSheet("border-color:" + selectedColour->name());
		});
		host->layout()->addWidget(surface);
	}

private:
	void buildChatSettingsTab()
	{
		auto *page = new QWidget;
		auto *pageLayout = new QHBoxLayout(page);
		pageLayout->setContentsMargins(18, 16, 18, 16);
		auto *panel = new QGroupBox("CHAT SETTINGS", page);
		panel->setObjectName("ConnectionCard");
		panel->setMaximumWidth(720);
		auto *form = new QGridLayout(panel);
		form->setColumnStretch(1, 1);
		auto *identityAliases = new QLineEdit(panel);
		identityAliases->setPlaceholderText("Your Twitch, YouTube, Kick and bot usernames — separated by commas");
		QSettings identitySettings(pulseSettingsPath(), QSettings::IniFormat);
		identityAliases->setText(identitySettings.value("chat/identity_aliases").toStringList().join(", "));
		form->addWidget(new QLabel("My usernames", panel), 0, 0);
		form->addWidget(identityAliases, 0, 1);
		auto *hint = new QLabel("Messages from these names are condensed in the unified chat, so your own copies do not clutter the feed.", panel);
		hint->setObjectName("Muted");
		hint->setWordWrap(true);
		form->addWidget(hint, 1, 0, 1, 2);
		auto applyIdentityAliases = [identityAliases] {
			QStringList aliases;
			for (const QString &part : identityAliases->text().split(',', Qt::SkipEmptyParts)) {
				const QString alias = part.trimmed().toLower();
				if (!alias.isEmpty() && !aliases.contains(alias)) aliases << alias;
			}
			QSettings settings(pulseSettingsPath(), QSettings::IniFormat);
			settings.setValue("chat/identity_aliases", aliases);
			if (QWidget *mainWindow = static_cast<QWidget *>(obs_frontend_get_main_window()))
				if (auto *feed = mainWindow->findChild<QListWidget *>("PulseWeaverChatFeed"))
					feed->setProperty("pulseWeaverIdentityAliases", aliases);
		};
		connect(identityAliases, &QLineEdit::editingFinished, this, applyIdentityAliases);
		applyIdentityAliases();
		pageLayout->addWidget(panel, 0, Qt::AlignLeft | Qt::AlignTop);
		pageLayout->addStretch(1);
		connectionsTabs->addTab(page, "CHAT SETTINGS");
	}

	void buildStreamInfoPanel()
	{
		/* Metadata belongs alongside account connections, but its own compact
		 * page keeps connection setup focused on one job at a time. */
		auto *page = new QWidget;
		auto *pageLayout = new QHBoxLayout(page);
		pageLayout->setContentsMargins(18, 16, 18, 16);
		auto *panel = new QGroupBox("STREAM DETAILS", page);
		panel->setObjectName("ConnectionCard");
		panel->setMaximumWidth(720);
		auto *form = new QGridLayout(panel);
		form->setColumnStretch(1, 1);
		streamTitle = new QLineEdit(panel);
		streamTitle->setPlaceholderText("Twitch stream title");
		streamCategory = new QComboBox(panel);
		streamCategory->setEditable(true);
		streamCategory->setInsertPolicy(QComboBox::NoInsert);
		streamCategory->setMaxVisibleItems(12);
		streamCategory->lineEdit()->setPlaceholderText("Search Twitch categories…");
		auto *categorySearch = new QTimer(streamCategory);
		categorySearch->setSingleShot(true);
		categorySearch->setInterval(300);
		auto *updateTwitch = new QPushButton("UPDATE TWITCH", panel);
		form->addWidget(new QLabel("Twitch title", panel), 0, 0);
		form->addWidget(streamTitle, 0, 1);
		form->addWidget(updateTwitch, 0, 2);
		form->addWidget(new QLabel("Twitch category", panel), 1, 0);
		form->addWidget(streamCategory, 1, 1, 1, 2);
		connect(streamCategory->lineEdit(), &QLineEdit::textEdited, this, [this, categorySearch] {
			streamCategory->setProperty("pulseWeaverCategoryId", QString());
			categorySearch->start();
		});
		connect(streamCategory, &QComboBox::activated, this, [this](int index) {
			streamCategory->setProperty("pulseWeaverCategoryId", streamCategory->itemData(index).toString());
		});
		connect(categorySearch, &QTimer::timeout, this, [this] {
			const QString requested = streamCategory->currentText().trimmed();
			twitch->searchCategories(requested, [this, requested](const QJsonArray &results, const QString &error) {
				if (!streamCategory || streamCategory->currentText().trimmed() != requested)
					return;
				streamCategory->blockSignals(true);
				streamCategory->clear();
				for (const QJsonValue &value : results) {
					const QJsonObject category = value.toObject();
					streamCategory->addItem(category.value("name").toString(), category.value("id").toString());
				}
				streamCategory->setEditText(requested);
				streamCategory->blockSignals(false);
				if (!error.isEmpty()) {
					twitchStatus->setText(error);
					return;
				}
				if (!results.isEmpty())
					streamCategory->showPopup();
			});
		});
		connect(updateTwitch, &QPushButton::clicked, this, [this] {
			const QString categoryText = streamCategory->currentText().trimmed();
			const QString categoryId = streamCategory->property("pulseWeaverCategoryId").toString();
			if (!categoryText.isEmpty() && categoryId.isEmpty()) {
				twitchStatus->setText("Choose a Twitch category from the search results.");
				return;
			}
			twitch->updateChannel(streamTitle->text(), categoryId);
		});

		QSettings settings(pulseSettingsPath(), QSettings::IniFormat);
		auto *youtubeTitle = new QLineEdit(panel);
		youtubeTitle->setPlaceholderText("YouTube broadcast title");
		youtubeTitle->setText(settings.value("youtube/broadcast_title").toString());
		auto *saveYoutube = new QPushButton("SAVE FOR NEXT GO LIVE", panel);
		form->addWidget(new QLabel("YouTube title", panel), 2, 0);
		form->addWidget(youtubeTitle, 2, 1);
		form->addWidget(saveYoutube, 2, 2);
		auto applyYoutubeTitle = [youtubeTitle] {
			const QString title = youtubeTitle->text().trimmed();
			QSettings saved(pulseSettingsPath(), QSettings::IniFormat);
			saved.setValue("youtube/broadcast_title", title);
			if (QWidget *mainWindow = static_cast<QWidget *>(obs_frontend_get_main_window()))
				mainWindow->setProperty("pulseWeaverYouTubeBroadcastTitle", title);
		};
		connect(saveYoutube, &QPushButton::clicked, this, applyYoutubeTitle);
		applyYoutubeTitle();

		auto *kickInfo = new QLabel("Kick metadata is changed in Kick Creator Dashboard; its current public API has no title-update endpoint.", panel);
		kickInfo->setObjectName("Muted");
		kickInfo->setWordWrap(true);
		auto *openKick = new QPushButton("OPEN KICK CREATOR DASHBOARD", panel);
		form->addWidget(new QLabel("Kick", panel), 3, 0);
		form->addWidget(kickInfo, 3, 1);
		form->addWidget(openKick, 3, 2);
		connect(openKick, &QPushButton::clicked, panel, [] { QDesktopServices::openUrl(QUrl("https://kick.com/dashboard/stream")); });
		pageLayout->addWidget(panel, 0, Qt::AlignLeft | Qt::AlignTop);
		pageLayout->addStretch(1);
		connectionsTabs->addTab(page, "STREAM DETAILS");
	}

	void buildKickTab()
	{
		auto *page = new QWidget;
		auto *layout = new QHBoxLayout(page);
		layout->setContentsMargins(18, 16, 18, 16);
		auto *account = new QGroupBox("KICK", page);
		account->setObjectName("ConnectionCard");
		account->setMaximumWidth(720);
		auto *form = new QGridLayout(account);
		form->setColumnStretch(1, 1);
		auto *accountName = new QLabel("No Kick account connected"); accountName->setObjectName("Kicker");
		auto *state = new QLabel("Sign in in your browser. Pulse Weaver retrieves your Kick destination automatically."); state->setWordWrap(true); state->setObjectName("Muted");
		auto *route = new QComboBox; route->setObjectName("PulseWeaverKickStageRoute"); route->addItem("Kick · Off", "off"); route->addItem("Horizontal 16:9", "horizontal");
		auto *connectButton = new QPushButton("CONNECT KICK IN BROWSER"); connectButton->setObjectName("Primary"); connectButton->setMinimumWidth(240);
		auto *disconnectButton = new QPushButton("DISCONNECT");
		form->addWidget(accountName, 0, 0, 1, 2); form->addWidget(connectButton, 0, 2);
		form->addWidget(new QLabel("Output mode"), 1, 0); form->addWidget(route, 1, 1); form->addWidget(disconnectButton, 1, 2);
		form->addWidget(state, 2, 0, 1, 3);
		auto *chatGroup = new QGroupBox("KICK CHAT", account); auto *chatLayout = new QHBoxLayout(chatGroup);
		auto *chat = new QLineEdit; chat->setPlaceholderText("Message Kick chat…"); auto *send = new QPushButton("SEND");
		chatLayout->addWidget(chat, 1); chatLayout->addWidget(send); form->addWidget(chatGroup, 3, 0, 1, 3);
		layout->addWidget(account, 0, Qt::AlignLeft | Qt::AlignTop); layout->addStretch(1);
		if (QWidget *mainWindow = static_cast<QWidget *>(obs_frontend_get_main_window())) {
			if (auto *showRoute = mainWindow->findChild<QComboBox *>("PulseWeaverDestinationKick")) {
				const int selected = route->findData(showRoute->currentData());
				route->setCurrentIndex(selected >= 0 ? selected : 0);
				connect(showRoute, &QComboBox::currentIndexChanged, route, [showRoute, route](int) {
					const int item = route->findData(showRoute->currentData()); if (item >= 0) route->setCurrentIndex(item);
				});
				connect(route, &QComboBox::currentIndexChanged, showRoute, [showRoute, route](int) {
					const int item = showRoute->findData(route->currentData()); if (item >= 0) showRoute->setCurrentIndex(item);
				});
			}
		}
		auto *client = new QLineEdit(account); client->setAccessibleName("Kick application");
		form->addWidget(new QLabel("Application"), 4, 0); form->addWidget(client, 4, 1, 1, 2);
        auto *callbackHint = new QLabel("Register callback: http://localhost:18757/auth/callback", account);
		callbackHint->setTextInteractionFlags(Qt::TextSelectableByMouse); form->addWidget(callbackHint, 5, 0, 1, 3);
		kick->setWidgets(client, accountName, state, route, connectButton, disconnectButton, chat, send);
		connectionsTabs->addTab(page, "KICK");
	}

	void buildYouTubeTab()
	{
		auto *page = new QWidget;
		auto *layout = new QHBoxLayout(page);
		layout->setContentsMargins(18, 16, 18, 16);
		auto *account = new QGroupBox("YOUTUBE", page);
		account->setObjectName("ConnectionCard");
		account->setMaximumWidth(720);
		auto *form = new QGridLayout(account);
		form->setColumnStretch(1, 1);
		auto *connectButton = new QPushButton("CONNECT YOUTUBE IN BROWSER");
		connectButton->setObjectName("Primary"); connectButton->setMinimumWidth(240);
		auto *disconnectButton = new QPushButton("DISCONNECT & REVOKE");
		disconnectButton->setMinimumWidth(190);
		auto *accountName = new QLabel("No YouTube account connected");
		accountName->setObjectName("Kicker");
		auto *route = new QComboBox;
		route->addItem("YouTube · Off", "off");
		route->addItem("Horizontal 16:9", "horizontal");
		route->addItem("Vertical 9:16", "vertical");
		route->addItem("Dual 16:9 + 9:16", "dual");
		auto *state = new QLabel("Sign in with your Google account in the browser. Pulse Weaver never asks for a password or stream key.");
		state->setObjectName("Muted");
		state->setWordWrap(true);
		form->addWidget(accountName, 0, 0, 1, 2);
		form->addWidget(connectButton, 0, 2);
		form->addWidget(new QLabel("Output mode"), 1, 0);
		form->addWidget(route, 1, 1);
		form->addWidget(disconnectButton, 1, 2);
		form->addWidget(state, 2, 0, 1, 3);
		auto *legal = new QLabel(
			"<a href='" + QString::fromUtf8(PulseLegal::PrivacyUrl) + "'>Privacy Policy</a> · "
			"<a href='" + QString::fromUtf8(PulseLegal::TermsUrl) + "'>Terms of Service</a> · "
			"<a href='" + QString::fromUtf8(PulseLegal::GooglePermissionsUrl) + "'>Google permissions</a>", account);
		legal->setTextFormat(Qt::RichText);
		legal->setOpenExternalLinks(true);
		form->addWidget(legal, 3, 0, 1, 3);
		// OAuth registration is supplied by application configuration, not edited
		// through the account connection screen. Keep saved credentials untouched.

		layout->addWidget(account, 0, Qt::AlignLeft | Qt::AlignTop);
		layout->addStretch(1);
		if (QWidget *mainWindow = static_cast<QWidget *>(obs_frontend_get_main_window())) {
			auto *nativeConnect = mainWindow->findChild<QPushButton *>("PulseWeaverYouTubeConnectButton");
			auto *nativeDisconnect = mainWindow->findChild<QPushButton *>("PulseWeaverYouTubeDisconnectButton");
			auto *nativeRoute = mainWindow->findChild<QComboBox *>("PulseWeaverYouTubeCanvasRoute");
			if (nativeConnect) {
				connect(connectButton, &QPushButton::clicked, nativeConnect, &QPushButton::click);
				auto syncAccount = [nativeConnect, nativeDisconnect, accountName, disconnectButton] {
					const bool connected = nativeConnect->text().contains("CONNECTED", Qt::CaseInsensitive);
					accountName->setText(connected ? "YouTube account connected" : "No YouTube account connected");
					disconnectButton->setEnabled(connected && nativeDisconnect);
				};
				connect(connectButton, &QPushButton::clicked, accountName, [syncAccount] {
					QTimer::singleShot(1200, [syncAccount] { syncAccount(); });
				});
				if (nativeDisconnect) {
					connect(disconnectButton, &QPushButton::clicked, nativeDisconnect, &QPushButton::click);
					connect(disconnectButton, &QPushButton::clicked, accountName, [syncAccount] {
						QTimer::singleShot(150, [syncAccount] { syncAccount(); });
					});
				}
				syncAccount();
			} else {
				connectButton->setEnabled(false);
				disconnectButton->setEnabled(false);
			}
			if (nativeRoute) {
				route->setCurrentIndex(nativeRoute->currentIndex());
				connect(route, &QComboBox::currentIndexChanged, nativeRoute, &QComboBox::setCurrentIndex);
				connect(nativeRoute, &QComboBox::currentIndexChanged, route, &QComboBox::setCurrentIndex);
			}
		}
		connectionsTabs->addTab(page, "YOUTUBE");
	}

	void buildTwitchTab()
	{
		auto *page = new QWidget;
		auto *layout = new QHBoxLayout(page);
		layout->setContentsMargins(18, 16, 18, 16);
		auto *column = new QWidget(page);
		column->setMaximumWidth(720);
		auto *columnLayout = new QVBoxLayout(column);
		columnLayout->setContentsMargins(0, 0, 0, 0);
		auto *connection = new QGroupBox("TWITCH", column);
		connection->setObjectName("ConnectionCard");
		auto *connectionLayout = new QGridLayout(connection);
		connectionLayout->setColumnStretch(1, 1);
		twitchAccount = new QLabel("No Twitch account connected");
		twitchAccount->setObjectName("Kicker");
		twitchStatus = new QLabel("Twitch is isolated from every other OBS profile.");
		twitchStatus->setWordWrap(true);
		twitchStatus->setObjectName("Muted");
		auto *connectButton = new QPushButton("CONNECT TWITCH");
		connectButton->setObjectName("Primary"); connectButton->setMinimumWidth(240);
		auto *disconnectButton = new QPushButton("DISCONNECT");
		auto *outputMode = new QComboBox;
		outputMode->addItem("Off", "off");
		outputMode->addItem("Horizontal 16:9", "horizontal");
		outputMode->addItem("Vertical 9:16", "vertical");
		outputMode->addItem("Dual 16:9 + 9:16", "dual");
		connectionLayout->addWidget(twitchAccount, 0, 0, 1, 2);
		connectionLayout->addWidget(connectButton, 0, 2);
		connectionLayout->addWidget(new QLabel("Output mode"), 1, 0);
		connectionLayout->addWidget(outputMode, 1, 1);
		connectionLayout->addWidget(disconnectButton, 1, 2);
		connectionLayout->addWidget(twitchStatus, 2, 0, 1, 3);
		if (QWidget *mainWindow = static_cast<QWidget *>(obs_frontend_get_main_window())) {
			if (auto *showRoute = mainWindow->findChild<QComboBox *>("PulseWeaverDestinationTwitch")) {
				const int selected = outputMode->findData(showRoute->currentData());
				outputMode->setCurrentIndex(selected >= 0 ? selected : 0);
				connect(showRoute, &QComboBox::currentIndexChanged, outputMode, [showRoute, outputMode](int) {
					const int item = outputMode->findData(showRoute->currentData()); if (item >= 0) outputMode->setCurrentIndex(item);
				});
				connect(outputMode, &QComboBox::currentIndexChanged, showRoute, [showRoute, outputMode](int) {
					const int item = showRoute->findData(outputMode->currentData()); if (item >= 0) showRoute->setCurrentIndex(item);
				});
			}
		}
		columnLayout->addWidget(connection);
		auto *client = new QLineEdit(connection); client->setAccessibleName("Twitch application");
        connectionLayout->addWidget(new QLabel("Application"), 3, 0); connectionLayout->addWidget(client, 3, 1, 1, 2);
        twitch->setConnectionWidgets(client, twitchAccount, twitchStatus, connectButton, disconnectButton);
		auto *moderation = new QGroupBox("TWITCH CHAT & MODERATION", column);
		moderation->setObjectName("ConnectionCard");
		auto *moderationLayout = new QVBoxLayout(moderation);
		auto *moderationCopy = new QLabel(
			"The unified chat supports delete message, 10-minute timeout, 1-hour timeout and ban from a Twitch message's right-click menu. "
			"Reconnect Twitch once after this update to grant the moderation permissions.", moderation);
		moderationCopy->setObjectName("Muted");
		moderationCopy->setWordWrap(true);
		moderationLayout->addWidget(moderationCopy);
		columnLayout->addWidget(moderation);

		auto *eventGroup = new QGroupBox("EVENTS TO LISTEN FOR", page);
		auto *eventLayout = new QVBoxLayout(eventGroup);
		auto *eventTree = new QTreeWidget;
		eventTree->setHeaderLabels({"EVENT", "PULSE WEAVER KEY"});
		eventTree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
		eventTree->header()->setSectionResizeMode(1, QHeaderView::Stretch);
		eventTree->setMaximumHeight(190);
		const QList<QPair<QString, QString>> eventChoices{
			{"Chat + first-time chatters + entrances", "channel.chat.message"}, {"Followers", "channel.follow"},
			{"Subscriptions", "channel.subscribe"}, {"Resubscriptions", "channel.subscription.message"},
			{"Gift subscriptions", "channel.subscription.gift"}, {"Bits / cheers", "channel.cheer"},
			{"Raids", "channel.raid"}, {"Channel point redeems", "channel.channel_points_custom_reward_redemption.add"},
			{"Hype Trains", "channel.hype_train.begin"}, {"Goals", "channel.goal.progress"}};
		QSettings eventSettings(pulseSettingsPath(), QSettings::IniFormat);
		QStringList selectedEvents;
		if (eventSettings.contains("twitch/events"))
			selectedEvents = eventSettings.value("twitch/events").toStringList();
		else
			for (const auto &choice : eventChoices)
				selectedEvents.append(choice.second);
		for (const auto &choice : eventChoices) {
			auto *item = new QTreeWidgetItem(eventTree, {choice.first, "twitch." + choice.second});
			item->setData(0, Qt::UserRole, choice.second);
			item->setCheckState(0, selectedEvents.contains(choice.second) ? Qt::Checked : Qt::Unchecked);
		}
		eventLayout->addWidget(eventTree);
		auto *eventButtons = new QHBoxLayout;
		auto *selectAll = new QPushButton("SELECT ALL");
		auto *selectNone = new QPushButton("SELECT NONE");
		auto *applyEvents = new QPushButton("APPLY + RECONNECT");
		applyEvents->setObjectName("Primary");
		eventButtons->addWidget(selectAll);
		eventButtons->addWidget(selectNone);
		eventButtons->addStretch();
		eventButtons->addWidget(applyEvents);
		eventLayout->addLayout(eventButtons);
		connect(selectAll, &QPushButton::clicked, eventTree, [eventTree] {
			for (int row = 0; row < eventTree->topLevelItemCount(); ++row)
				eventTree->topLevelItem(row)->setCheckState(0, Qt::Checked);
		});
		connect(selectNone, &QPushButton::clicked, eventTree, [eventTree] {
			for (int row = 0; row < eventTree->topLevelItemCount(); ++row)
				eventTree->topLevelItem(row)->setCheckState(0, Qt::Unchecked);
		});
		connect(applyEvents, &QPushButton::clicked, this, [this, eventTree] {
			QStringList enabled;
			for (int row = 0; row < eventTree->topLevelItemCount(); ++row) {
				QTreeWidgetItem *item = eventTree->topLevelItem(row);
				if (item->checkState(0) == Qt::Checked)
					enabled.append(item->data(0, Qt::UserRole).toString());
			}
			QSettings settings(pulseSettingsPath(), QSettings::IniFormat);
			settings.setValue("twitch/events", enabled);
			twitch->reconnect();
		});
		eventGroup->hide();

		auto *bot = new QGroupBox("CHAT BOT — SIMPLE COMMANDS, NOT A SCRIPTING WALL", page);
		auto *botLayout = new QVBoxLayout(bot);
		auto *botHead = new QHBoxLayout;
		botEnabled = new QPushButton;
		botEnabled->setCheckable(true);
		QSettings settings(pulseSettingsPath(), QSettings::IniFormat);
		botEnabled->setChecked(settings.value("chatbot/enabled", false).toBool());
		auto updateBotLabel = [this] {
			botEnabled->setText(botEnabled->isChecked() ? "🤖  CHAT BOT ON" : "◯  CHAT BOT OFF");
		};
		updateBotLabel();
		connect(botEnabled, &QPushButton::toggled, this, [this, updateBotLabel](bool enabled) {
			updateBotLabel();
			QSettings settings(pulseSettingsPath(), QSettings::IniFormat);
			settings.setValue("chatbot/enabled", enabled);
		});
		botHead->addWidget(botEnabled);
		botHead->addWidget(new QLabel("Replies are sent through the connected broadcaster account."), 1);
		botLayout->addLayout(botHead);
		auto *commandRow = new QHBoxLayout;
		botCommand = new QLineEdit;
		botCommand->setPlaceholderText("!command");
		botReply = new QLineEdit;
		botReply->setPlaceholderText("Reply; use {user} for the chatter name");
		auto *addCommand = new QPushButton("ADD COMMAND");
		commandRow->addWidget(botCommand);
		commandRow->addWidget(botReply, 1);
		commandRow->addWidget(addCommand);
		botLayout->addLayout(commandRow);
		botTable = new QTableWidget(0, 3);
		botTable->setHorizontalHeaderLabels({"COMMAND", "REPLY", ""});
		botTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
		botTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
		botTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
		botTable->verticalHeader()->setVisible(false);
		botLayout->addWidget(botTable, 1);
		connect(addCommand, &QPushButton::clicked, this, [this] {
			QString command = botCommand->text().trimmed();
			const QString reply = botReply->text().trimmed();
			if (!command.startsWith('!'))
				command.prepend('!');
			if (command.size() < 2 || reply.isEmpty())
				return;
			botCommands.push_back({command.toLower(), reply});
			botCommand->clear();
			botReply->clear();
			refreshBotCommands();
			saveBotCommands();
		});
		bot->hide();
		refreshBotCommands();
		layout->addWidget(column, 0, Qt::AlignLeft | Qt::AlignTop);
		layout->addStretch(1);
		connectionsTabs->addTab(page, "TWITCH");
	}

	void handleTwitchChat(const QString &user, const QString &message)
	{
		if (!botEnabled || !botEnabled->isChecked() || !twitch)
			return;
		const QString key = message.section(' ', 0, 0).trimmed().toLower();
		for (const BotCommand &command : botCommands) {
			if (command.command.compare(key, Qt::CaseInsensitive) != 0)
				continue;
			twitch->sendMessage(QString(command.reply).replace("{user}", user, Qt::CaseInsensitive));
			break;
		}
	}

	void loadBotCommands()
	{
		QSettings settings(pulseSettingsPath(), QSettings::IniFormat);
		const int count = settings.beginReadArray("chatbot/commands");
		for (int i = 0; i < count; ++i) {
			settings.setArrayIndex(i);
			const QString command = settings.value("command").toString();
			const QString reply = settings.value("reply").toString();
			if (!command.isEmpty() && !reply.isEmpty())
				botCommands.push_back({command, reply});
		}
		settings.endArray();
		if (botCommands.empty())
			botCommands.push_back({"!hello", "Welcome to the show, {user}!"});
	}

	void saveBotCommands()
	{
		QSettings settings(pulseSettingsPath(), QSettings::IniFormat);
		settings.beginWriteArray("chatbot/commands");
		for (int i = 0; i < int(botCommands.size()); ++i) {
			settings.setArrayIndex(i);
			settings.setValue("command", botCommands[size_t(i)].command);
			settings.setValue("reply", botCommands[size_t(i)].reply);
		}
		settings.endArray();
	}

	void refreshBotCommands()
	{
		if (!botTable)
			return;
		botTable->setRowCount(int(botCommands.size()));
		for (int row = 0; row < int(botCommands.size()); ++row) {
			botTable->setItem(row, 0, new QTableWidgetItem(botCommands[size_t(row)].command));
			botTable->setItem(row, 1, new QTableWidgetItem(botCommands[size_t(row)].reply));
			auto *remove = new QPushButton("×");
			connect(remove, &QPushButton::clicked, this, [this, row] {
				botCommands.erase(botCommands.begin() + row);
				refreshBotCommands();
				saveBotCommands();
			});
			botTable->setCellWidget(row, 2, remove);
		}
	}

	void buildStudioTab()
	{
		auto *page = new QWidget;
		auto *layout = new QVBoxLayout(page);
		auto *outputRow = new QHBoxLayout;
		auto *horizontal = new QPushButton("HORIZONTAL 16:9");
		horizontal->setObjectName("Primary");
		auto *vertical = new QPushButton("CREATE / REPAIR VERTICAL 9:16");
		connect(vertical, &QPushButton::clicked, this, [this] { ensureVerticalCanvas(); });
		outputRow->addWidget(horizontal);
		outputRow->addWidget(vertical);
		layout->addLayout(outputRow);

		auto *splitter = new QSplitter;
		sceneList = new QListWidget;
		sceneList->setMinimumWidth(150);
		sourceTree = new QTreeWidget;
		sourceTree->setHeaderLabels({"SOURCE", "TYPE"});
		sourceTree->header()->setStretchLastSection(false);
		sourceTree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
		splitter->addWidget(sceneList);
		splitter->addWidget(sourceTree);
		splitter->setStretchFactor(1, 1);
		layout->addWidget(splitter, 1);
		connect(sceneList, &QListWidget::currentTextChanged, this, [this](const QString &name) {
			if (!refreshing && !name.isEmpty())
				activateScene(name);
		});
		connect(sourceTree, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem *item, int column) {
			if (refreshing || !item || column != 0)
				return;
			obs_source_t *sceneSource = obs_frontend_get_current_scene();
			if (!sceneSource)
				return;
			obs_scene_t *scene = obs_scene_from_source(sceneSource);
			OBSSceneItem sceneItem = PulseRuntimeSafety::findSceneItemById(scene, item->data(0, Qt::UserRole).toLongLong());
			if (sceneItem)
				obs_sceneitem_set_visible(sceneItem, item->checkState(0) == Qt::Checked);
			obs_source_release(sceneSource);
		});

		auto *controls = new QHBoxLayout;
		streamButton = new QPushButton;
		streamButton->setObjectName("Live");
		recordButton = new QPushButton;
		auto *replay = new QPushButton("SAVE REPLAY");
		connect(streamButton, &QPushButton::clicked, this, [this] {
			obs_frontend_streaming_active() ? obs_frontend_streaming_stop() : obs_frontend_streaming_start();
			refreshStatus();
		});
		connect(recordButton, &QPushButton::clicked, this, [this] {
			obs_frontend_recording_active() ? obs_frontend_recording_stop() : obs_frontend_recording_start();
			refreshStatus();
		});
		connect(replay, &QPushButton::clicked, this, [] {
			if (obs_frontend_replay_buffer_active())
				obs_frontend_replay_buffer_save();
			else
				obs_frontend_replay_buffer_start();
		});
		controls->addWidget(streamButton);
		controls->addWidget(recordButton);
		controls->addWidget(replay);
		layout->addLayout(controls);

		audioTable = new QTableWidget(0, 4);
		audioTable->setHorizontalHeaderLabels({"AUDIO", "LEVEL", "MUTE", "FILTERS"});
		audioTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
		audioTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
		audioTable->verticalHeader()->setVisible(false);
		layout->addWidget(audioTable);
		tabs->addTab(page, "STUDIO");
	}

	void buildEventsTab()
	{
		auto *page = new QWidget;
		auto *layout = new QVBoxLayout(page);
		auto *splitter = new QSplitter;
		eventTree = new QTreeWidget;
		eventTree->setHeaderLabel("EVENT PROVIDERS");
		auto addProvider = [this](const QString &provider, const QStringList &items) {
			auto *root = new QTreeWidgetItem(eventTree, {provider});
			for (const QString &entry : items) {
				const QStringList parts = entry.split('|');
				auto *child = new QTreeWidgetItem(root, {parts.value(0)});
				child->setData(0, Qt::UserRole, parts.value(1));
			}
			root->setExpanded(true);
		};
		addProvider("PULSE WEAVER", {"App started|pulseweaver.app.started", "Stream started|pulseweaver.stream.started", "Stream stopped|pulseweaver.stream.stopped", "Recording started|pulseweaver.recording.started", "Recording stopped|pulseweaver.recording.stopped", "API event|external.custom"});
		addProvider("TWITCH", {"Follower|twitch.channel.follow", "Subscriber|twitch.channel.subscribe", "Resubscription|twitch.channel.subscription.message", "Gift subscription|twitch.channel.subscription.gift", "Bits / Cheer|twitch.channel.cheer", "Raid|twitch.channel.raid", "Channel point redeem|twitch.channel.channel_points_custom_reward_redemption.add", "First-time chatter|twitch.viewer.first_message", "Viewer entrance|twitch.viewer.entrance", "Chat message|twitch.channel.chat.message", "Hype Train|twitch.channel.hype_train.begin", "Goal progress|twitch.channel.goal.progress"});
		addProvider("YOUTUBE", {"Channel subscriber (follow)|youtube.channel.subscribe", "Paid membership|youtube.membership.received", "Membership milestone|youtube.membership.milestone", "Super Chat|youtube.super_chat.received", "Super Sticker|youtube.super_sticker.received", "Chat message|youtube.chat.message"});
		addProvider("KICK", {"Follower|kick.channel.followed", "Subscription|kick.channel.subscription.new", "Renewal|kick.channel.subscription.renewal", "KICKs gifted|kick.kicks.gifted", "Chat message|kick.chat.message.sent"});
		addProvider("UNIFIED", {"Follow / YouTube subscriber|audience.followed", "Paid subscription / membership|support.paid_subscription", "Contribution / cheer / Super Chat|support.contribution", "Chat message|chat.message", "Raid|audience.raid"});
		addProvider("EXTENSIONS", {"Plugin event|plugin.custom", "Game bridge|game.custom", "DIY hardware|hardware.custom"});
		eventLog = new QTextEdit;
		eventLog->setReadOnly(true);
		eventLog->document()->setMaximumBlockCount(250);
		eventLog->setPlaceholderText("Live native events appear here. Plugins publish through the pulseweaver_publish proc; local integrations can POST to /api/v1/event.");
		splitter->addWidget(eventTree);
		splitter->addWidget(eventLog);
		splitter->setStretchFactor(1, 1);
		layout->addWidget(splitter, 1);
		auto *test = new QPushButton("SEND SELECTED TEST EVENT");
		test->setObjectName("Primary");
		connect(test, &QPushButton::clicked, this, [this] {
			auto *item = eventTree->currentItem();
			const QString key = item ? item->data(0, Qt::UserRole).toString() : QString();
			if (key.isEmpty())
				return;
			const int dot = key.indexOf('.');
			publishEvent(key.left(dot), key.mid(dot + 1), R"({"user":"PulseTester","viewers":42,"amount":100})");
		});
		layout->addWidget(test);
		tabs->addTab(page, "EVENTS");
	}

	void buildAutomationTab()
	{
		auto *page = new QWidget;
		auto *layout = new QVBoxLayout(page);
		auto *copy = new QLabel("IF THIS EVENT HAPPENS → RUN THESE ACTIONS DIRECTLY INSIDE LIBOBS");
		copy->setObjectName("Kicker");
		layout->addWidget(copy);
		auto *form = new QFormLayout;
		ruleEvent = new QComboBox;
		ruleEvent->setEditable(true);
		ruleEvent->addItems({"audience.followed", "support.paid_subscription", "support.contribution", "chat.message",
			"twitch.channel.follow", "twitch.channel.raid", "twitch.channel.cheer",
			"twitch.channel.channel_points_custom_reward_redemption.add", "youtube.channel.subscribe",
			"youtube.membership.received", "youtube.super_chat.received", "kick.channel.followed",
			"kick.channel.subscription.new", "pulseweaver.stream.started", "external.custom", "*"});
		ruleAction = new QComboBox;
		ruleAction->addItems({"Switch scene", "Show source", "Hide source", "Toggle source", "Mute source", "Unmute source", "Set source text", "Restart media", "Trigger overlay", "Apply light look", "Send Twitch chat", "HTTP request", "Write log", "Emit event", "Start stream", "Stop stream", "Start recording", "Stop recording"});
		ruleTarget = new QComboBox;
		ruleTarget->setEditable(true);
		ruleValue = new QLineEdit;
		ruleValue->setPlaceholderText("Message, text, request body or event JSON");
		ruleConditionField = new QLineEdit;
		ruleConditionField->setPlaceholderText("Optional event field, e.g. viewers or amount");
		ruleConditionOperator = new QComboBox;
		ruleConditionOperator->addItems({"Always", "Exists", "Equals", "Not equal", "Greater", "Greater or equal", "Contains"});
		ruleConditionValue = new QLineEdit;
		ruleConditionValue->setPlaceholderText("Comparison value");
		ruleDelay = new QSpinBox;
		ruleDelay->setRange(0, 120000);
		ruleDelay->setSuffix(" ms");
		form->addRow("Event", ruleEvent);
		form->addRow("Action", ruleAction);
		form->addRow("Target", ruleTarget);
		form->addRow("Value / body", ruleValue);
		form->addRow("Condition field", ruleConditionField);
		form->addRow("Condition", ruleConditionOperator);
		form->addRow("Compare with", ruleConditionValue);
		form->addRow("Delay before action", ruleDelay);
		layout->addLayout(form);
		connect(ruleAction, &QComboBox::currentTextChanged, this, [this] { refreshTargets(); });
		auto *add = new QPushButton("ADD TO COMMAND FLOW");
		add->setObjectName("Primary");
		connect(add, &QPushButton::clicked, this, [this] {
			rules.push_back({ruleEvent->currentText().trimmed(), ruleAction->currentText(), ruleTarget->currentText().trimmed(),
					 ruleValue->text(), ruleConditionField->text().trimmed(), ruleConditionOperator->currentText(),
					 ruleConditionValue->text(), ruleDelay->value(), true});
			refreshRules();
			saveRules();
		});
		layout->addWidget(add);
		rulesTable = new QTableWidget(0, 8);
		rulesTable->setHorizontalHeaderLabels({"ON", "EVENT", "CONDITION", "DELAY", "ACTION", "TARGET", "VALUE", ""});
		rulesTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
		rulesTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
		rulesTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
		rulesTable->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
		rulesTable->horizontalHeader()->setSectionResizeMode(5, QHeaderView::Stretch);
		rulesTable->horizontalHeader()->setSectionResizeMode(6, QHeaderView::Stretch);
		rulesTable->verticalHeader()->setVisible(false);
		layout->addWidget(rulesTable, 1);
		tabs->addTab(page, "AUTOMATIONS");
	}

	QString codexExecutable() const
	{
		QSettings settings(pulseSettingsPath(), QSettings::IniFormat);
		const QString configured = settings.value("ai/codex_path").toString();
		if (QFileInfo::exists(configured))
			return configured;
		const QString onPath = QStandardPaths::findExecutable("codex");
		if (!onPath.isEmpty())
			return onPath;
		const QString localRoot = qEnvironmentVariable("LOCALAPPDATA") + "/OpenAI/Codex/bin";
		QDirIterator candidates(localRoot, {"codex.exe"}, QDir::Files, QDirIterator::Subdirectories);
		if (candidates.hasNext())
			return candidates.next();
		return {};
	}

	QJsonArray automationJson() const
	{
		QJsonArray result;
		for (const Rule &rule : rules)
			result.append(QJsonObject{{"event", rule.eventKey}, {"action", rule.action}, {"target", rule.target},
				{"value", rule.value}, {"conditionField", rule.conditionField},
				{"conditionOperator", rule.conditionOperator}, {"conditionValue", rule.conditionValue},
				{"delayMs", rule.delayMs}, {"enabled", rule.enabled}});
		return result;
	}

	QJsonObject aiContext() const
	{
		return {{"scenes", sceneJson()}, {"sources", sourceJson()},
			{"overlays", overlays ? overlays->catalogueJson().value("overlays") : QJsonArray{}},
			{"automations", automationJson()}, {"moduleActions", moduleJson()}};
	}

	QString aiDirectory() const
	{
		const QString result = QFileInfo(pulseSettingsPath()).absoluteDir().filePath("ai");
		QDir().mkpath(result);
		return result;
	}

	QString writeAiSchema() const
	{
		const QString path = QDir(aiDirectory()).filePath("pulseweaver-operation.schema.json");
		QJsonObject arguments;
		arguments.insert("name", QJsonObject{{"type", "string"}});
		arguments.insert("width", QJsonObject{{"type", "integer"}, {"minimum", 64}, {"maximum", 7680}});
		arguments.insert("height", QJsonObject{{"type", "integer"}, {"minimum", 64}, {"maximum", 7680}});
		for (const QString &key : {"text", "triggerEvent", "event", "action", "target", "value",
					   "conditionField", "conditionOperator", "conditionValue", "dataJson"})
			arguments.insert(key, QJsonObject{{"type", "string"}});
		arguments.insert("visible", QJsonObject{{"type", "boolean"}});
		for (const QString &key : {"x", "y", "scaleX", "scaleY", "rotation"})
			arguments.insert(key, QJsonObject{{"type", "number"}});
		arguments.insert("delayMs", QJsonObject{{"type", "integer"}, {"minimum", 0}, {"maximum", 120000}});
		const QJsonObject argumentSchema{{"type", "object"}, {"additionalProperties", false}, {"properties", arguments}};
		const QJsonObject operationProperties{
			{"type", QJsonObject{{"type", "string"}, {"enum", QJsonArray{"create_scene", "switch_scene",
				"set_source_transform", "set_source_visible", "create_overlay", "add_overlay_to_scene",
				"create_automation", "trigger_test_event"}}}},
			{"arguments", argumentSchema}};
		const QJsonObject operationSchema{{"type", "object"}, {"additionalProperties", false},
			{"required", QJsonArray{"type", "arguments"}}, {"properties", operationProperties}};
		const QJsonObject schema{{"type", "object"}, {"additionalProperties", false},
			{"required", QJsonArray{"summary", "operations"}},
			{"properties", QJsonObject{{"summary", QJsonObject{{"type", "string"}}},
				{"operations", QJsonObject{{"type", "array"}, {"items", operationSchema}}}}}};
		QSaveFile file(path);
		if (file.open(QIODevice::WriteOnly)) {
			file.write(QJsonDocument(schema).toJson(QJsonDocument::Indented));
			file.commit();
		}
		return path;
	}

	QString currentSceneName() const
	{
		obs_source_t *source = obs_frontend_get_current_scene();
		const QString name = source ? QString::fromUtf8(obs_source_get_name(source)) : QString();
		if (source)
			obs_source_release(source);
		return name;
	}

	OBSSceneItem findCurrentSceneItem(const QString &name) const
	{
		obs_source_t *sceneSource = obs_frontend_get_current_scene();
		obs_scene_t *scene = sceneSource ? obs_scene_from_source(sceneSource) : nullptr;
		OBSSceneItem item = PulseRuntimeSafety::findSceneItem(scene, name.toUtf8().constData());
		if (sceneSource)
			obs_source_release(sceneSource);
		return item;
	}

	bool applyAiOperation(const QJsonObject &operation, QJsonObject &restore, QStringList &results)
	{
		const QString type = operation.value("type").toString();
		const QJsonObject args = operation.value("arguments").toObject();
		if (type == "create_scene") {
			const QString name = args.value("name").toString().trimmed();
			obs_source_t *existing = name.isEmpty() ? nullptr : obs_get_source_by_name(name.toUtf8().constData());
			if (name.isEmpty() || existing) {
				if (existing)
					obs_source_release(existing);
				results.append("Create scene rejected: the name is empty or already exists.");
				return false;
			}
			obs_scene_t *scene = obs_scene_create(name.toUtf8().constData());
			if (!scene) {
				results.append("Create scene failed: " + name);
				return false;
			}
			obs_scene_release(scene);
			QJsonArray created = restore.value("createdScenes").toArray();
			created.append(name);
			restore.insert("createdScenes", created);
			results.append("Created scene " + name);
			return true;
		}
		if (type == "switch_scene") {
			const QString name = args.value("name").toString();
			const bool ok = activateScene(name);
			results.append(ok ? "Switched to " + name : "Scene not found: " + name);
			return ok;
		}
		if (type == "set_source_visible" || type == "set_source_transform") {
			const QString name = args.value("name").toString();
			OBSSceneItem item = findCurrentSceneItem(name);
			if (!item) {
				results.append("Source not found in the active scene: " + name);
				return false;
			}
			obs_transform_info transform{};
			obs_sceneitem_get_info2(item, &transform);
			QJsonArray changed = restore.value("changedSources").toArray();
			changed.append(QJsonObject{{"scene", currentSceneName()}, {"itemId", double(obs_sceneitem_get_id(item))},
				{"x", transform.pos.x}, {"y", transform.pos.y}, {"scaleX", transform.scale.x},
				{"scaleY", transform.scale.y}, {"rotation", transform.rot},
				{"visible", obs_sceneitem_visible(item)}});
			restore.insert("changedSources", changed);
			if (type == "set_source_visible")
				obs_sceneitem_set_visible(item, args.value("visible").toBool(true));
			else {
				transform.pos.x = float(args.value("x").toDouble(transform.pos.x));
				transform.pos.y = float(args.value("y").toDouble(transform.pos.y));
				transform.scale.x = float(args.value("scaleX").toDouble(transform.scale.x));
				transform.scale.y = float(args.value("scaleY").toDouble(transform.scale.y));
				transform.rot = float(args.value("rotation").toDouble(transform.rot));
				obs_sceneitem_set_info2(item, &transform);
			}
			results.append("Updated source " + name);
			return true;
		}
		if (type == "create_overlay") {
			QString id;
			const bool ok = overlays && overlays->createOverlay(args.value("name").toString(),
				args.value("width").toInt(1920), args.value("height").toInt(1080),
				args.value("text").toString(), args.value("triggerEvent").toString(), &id);
			if (ok) {
				QJsonArray created = restore.value("createdOverlays").toArray();
				created.append(id);
				restore.insert("createdOverlays", created);
			}
			results.append(ok ? "Created overlay " + args.value("name").toString() : "Overlay creation was rejected.");
			return ok;
		}
		if (type == "add_overlay_to_scene") {
			QString error;
				const bool ok = overlays && overlays->addToCameraScenes(args.value("name").toString(),
					args.value("target").toString("landscape"), &error);
			results.append(ok ? "Added overlay to the active scene." : error);
			return ok;
		}
		if (type == "create_automation") {
			const QString event = args.value("event").toString().trimmed();
			const QString action = args.value("action").toString().trimmed();
			if (event.isEmpty() || !ruleAction || ruleAction->findText(action) < 0) {
				results.append("Automation rejected: the event or action is not valid.");
				return false;
			}
			rules.push_back({event, action, args.value("target").toString(), args.value("value").toString(),
				args.value("conditionField").toString(), args.value("conditionOperator").toString("Always"),
				args.value("conditionValue").toString(), std::clamp(args.value("delayMs").toInt(), 0, 120000), true});
			refreshRules();
			saveRules();
			results.append("Created automation for " + event);
			return true;
		}
		if (type == "trigger_test_event") {
			const QString event = args.value("event").toString();
			const int dot = event.indexOf('.');
			if (dot < 1)
				return false;
			QJsonObject eventData = args.value("data").toObject();
			if (eventData.isEmpty() && args.value("dataJson").isString())
				eventData = QJsonDocument::fromJson(args.value("dataJson").toString().toUtf8()).object();
			publishEvent(event.left(dot), event.mid(dot + 1),
				QString::fromUtf8(QJsonDocument(eventData).toJson(QJsonDocument::Compact)));
			results.append("Triggered test event " + event);
			return true;
		}
		results.append("Unsupported AI operation: " + type);
		return false;
	}

	void saveAiRestore(const QJsonObject &restore)
	{
		lastAiRestore = restore;
		QSaveFile file(QDir(aiDirectory()).filePath("last-restore.json"));
		if (file.open(QIODevice::WriteOnly)) {
			file.write(QJsonDocument(restore).toJson(QJsonDocument::Indented));
			file.commit();
		}
		if (aiUndo)
			aiUndo->setEnabled(true);
	}

	void undoLastAiChange()
	{
		if (lastAiRestore.isEmpty()) {
			QFile file(QDir(aiDirectory()).filePath("last-restore.json"));
			if (file.open(QIODevice::ReadOnly))
				lastAiRestore = QJsonDocument::fromJson(file.readAll()).object();
		}
		if (lastAiRestore.isEmpty()) {
			aiStatus->setText("No AI restore point is available.");
			return;
		}
		for (const QJsonValue &value : lastAiRestore.value("changedSources").toArray()) {
			const QJsonObject before = value.toObject();
			obs_source_t *sceneSource = obs_get_source_by_name(before.value("scene").toString().toUtf8().constData());
			obs_scene_t *scene = sceneSource ? obs_scene_from_source(sceneSource) : nullptr;
			OBSSceneItem item = PulseRuntimeSafety::findSceneItemById(scene, int64_t(before.value("itemId").toDouble()));
			if (item) {
				obs_transform_info transform{};
				obs_sceneitem_get_info2(item, &transform);
				transform.pos = {float(before.value("x").toDouble()), float(before.value("y").toDouble())};
				transform.scale = {float(before.value("scaleX").toDouble()), float(before.value("scaleY").toDouble())};
				transform.rot = float(before.value("rotation").toDouble());
				obs_sceneitem_set_info2(item, &transform);
				obs_sceneitem_set_visible(item, before.value("visible").toBool(true));
			}
			if (sceneSource)
				obs_source_release(sceneSource);
		}
		const int ruleCount = lastAiRestore.value("ruleCount").toInt(int(rules.size()));
		if (ruleCount >= 0 && ruleCount <= int(rules.size()))
			rules.resize(size_t(ruleCount));
		for (const QJsonValue &value : lastAiRestore.value("createdOverlays").toArray())
			if (overlays)
				overlays->removeOverlay(value.toString());
		for (const QJsonValue &value : lastAiRestore.value("createdScenes").toArray()) {
			obs_source_t *source = obs_get_source_by_name(value.toString().toUtf8().constData());
			if (source) {
				obs_source_remove(source);
				obs_source_release(source);
			}
		}
		activateScene(lastAiRestore.value("currentScene").toString());
		saveRules();
		refreshRules();
		obs_frontend_save();
		aiStatus->setText("The previous AI task was restored.");
		lastAiRestore = {};
		QFile::remove(QDir(aiDirectory()).filePath("last-restore.json"));
		aiUndo->setEnabled(false);
	}

	void runAiRequest()
	{
		if (aiProcess) {
			aiStatus->setText("Codex is already working on a request.");
			return;
		}
		const QString request = aiPrompt->toPlainText().trimmed();
		if (request.isEmpty())
			return;
		const QString executable = codexExecutable();
		if (executable.isEmpty()) {
			aiStatus->setText("Codex CLI was not found. Install/sign in to Codex, then reopen Pulse Weaver.");
			return;
		}
		const QString outputPath = QDir(aiDirectory()).filePath("last-response.json");
		QFile::remove(outputPath);
		const QString prompt = "You are Pulse Weaver's project-aware show builder. Do not edit files and do not provide instructions. "
			"Return a single JSON object containing a concise summary and validated operations that accomplish the user's request. "
			"Use only the operation types in the supplied schema. For create_automation, action must exactly match one available Pulse Weaver action name. "
			"Prefer existing scenes, sources and overlays when appropriate. Current live project state:\n" +
			QString::fromUtf8(QJsonDocument(aiContext()).toJson(QJsonDocument::Indented)) + "\nUser request:\n" + request;
		auto *process = new QProcess(this);
		aiProcess = process;
		aiRun->setEnabled(false);
		aiStatus->setText("Codex is inspecting the current show…");
		connect(process, &QProcess::errorOccurred, this, [this, process](QProcess::ProcessError) {
			aiStatus->setText("Codex could not start: " + process->errorString());
			aiRun->setEnabled(true);
			process->deleteLater();
			aiProcess = nullptr;
		});
		connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
			[this, process, outputPath](int exitCode, QProcess::ExitStatus exitStatus) {
				aiRun->setEnabled(true);
				QFile output(outputPath);
				const QByteArray bytes = output.open(QIODevice::ReadOnly) ? output.readAll() : process->readAllStandardOutput();
				QJsonParseError error{};
				const QJsonDocument response = QJsonDocument::fromJson(bytes, &error);
				if (exitStatus != QProcess::NormalExit || exitCode != 0 || error.error != QJsonParseError::NoError || !response.isObject()) {
					aiStatus->setText("Codex did not return a valid Pulse Weaver operation: " + process->readAllStandardError().right(500));
					process->deleteLater();
					aiProcess = nullptr;
					return;
				}
				QJsonObject restore{{"createdAt", QDateTime::currentDateTimeUtc().toString(Qt::ISODate)},
					{"currentScene", currentSceneName()}, {"ruleCount", int(rules.size())},
					{"createdScenes", QJsonArray{}}, {"createdOverlays", QJsonArray{}}, {"changedSources", QJsonArray{}}};
				QStringList applied;
				int appliedCount = 0;
				for (const QJsonValue &value : response.object().value("operations").toArray())
					appliedCount += applyAiOperation(value.toObject(), restore, applied) ? 1 : 0;
				if (appliedCount > 0) {
					saveAiRestore(restore);
					obs_frontend_save();
				}
				aiResult->setPlainText(response.object().value("summary").toString() + "\n\n" + applied.join('\n'));
				aiStatus->setText(appliedCount == 0 ? "Codex returned no applicable operations." : "Codex applied validated operations. Undo is available.");
				process->deleteLater();
				aiProcess = nullptr;
			});
		process->setProgram(executable);
		process->setWorkingDirectory(aiDirectory());
		process->setArguments({"exec", "--ephemeral", "--sandbox", "read-only", "--skip-git-repo-check",
			"--output-schema", writeAiSchema(), "--output-last-message", outputPath, "-C", aiDirectory(), "-"});
		process->start();
		if (process->waitForStarted(3000)) {
			process->write(prompt.toUtf8());
			process->closeWriteChannel();
		}
	}

	void buildAiTab()
	{
		aiPage = new QWidget;
		auto *layout = new QVBoxLayout(aiPage);
		auto *title = new QLabel("PULSE AI — ASK FOR THE SHOW, NOT THE SETTINGS");
		title->setObjectName("Kicker");
		layout->addWidget(title);
		layout->addWidget(new QLabel("Codex receives a read-only snapshot and returns constrained operations. Pulse Weaver validates and applies them through libobs, then keeps a restore point."));
		aiPrompt = new QTextEdit;
		aiPrompt->setPlaceholderText("Create a scene called Racing, make a vertical raid overlay, or move my camera to the bottom-right…");
		aiPrompt->setMaximumHeight(150);
		layout->addWidget(aiPrompt);
		auto *buttons = new QHBoxLayout;
		aiRun = new QPushButton("BUILD IT WITH CODEX");
		aiRun->setObjectName("Primary");
		aiUndo = new QPushButton("UNDO LAST AI CHANGE");
		aiUndo->setEnabled(QFileInfo::exists(QDir(aiDirectory()).filePath("last-restore.json")));
		buttons->addWidget(aiRun);
		buttons->addWidget(aiUndo);
		buttons->addStretch();
		layout->addLayout(buttons);
		aiStatus = new QLabel("Codex: " + (codexExecutable().isEmpty() ? QString("not detected") : QString("ready with the local signed-in installation")));
		aiStatus->setObjectName("Muted");
		aiStatus->setWordWrap(true);
		layout->addWidget(aiStatus);
		aiResult = new QTextEdit;
		aiResult->setReadOnly(true);
		layout->addWidget(aiResult, 1);
		connect(aiRun, &QPushButton::clicked, this, [this] { runAiRequest(); });
		connect(aiUndo, &QPushButton::clicked, this, [this] { undoLastAiChange(); });
		tabs->addTab(aiPage, "PULSE AI");
	}

	void buildApiTab()
	{
		auto *page = new QWidget;
		auto *layout = new QVBoxLayout(page);
		auto *title = new QLabel("NATIVE IN-PROCESS CONTROL API");
		title->setObjectName("Kicker");
		layout->addWidget(title);
		auto *body = new QLabel(
			"Pulse Weaver is the OBS process. These endpoints call the frontend/libobs API directly — no OBS WebSocket hop.\n\n"
			"GET  /api/v1/health\nGET  /api/v1/scenes\nPOST /api/v1/scene?name=Gameplay\n"
			"GET  /api/v1/sources  |  GET /api/v1/automations  |  GET /api/v1/overlays  |  GET /api/v1/modules\n"
			"POST /api/v1/scene/create  |  /source/create  |  /source/transform  |  /source/visibility  |  /automation\n"
			"POST /api/v1/stream/start  |  /stream/stop\nPOST /api/v1/recording/start  |  /recording/stop\n"
			"POST /api/v1/event?platform=external&type=custom\nPOST /api/v1/overlay/trigger?name=Follower%20Spotlight\n\n"
			"Every request requires Authorization: Bearer " + apiToken + "\n\n"
			"Plugin ABI: proc_handler_call(obs_get_proc_handler(), \"pulseweaver_publish\", ...).\n"
			"Subscribers receive the global libobs signal pulseweaver_event(platform, type, json).");
		body->setWordWrap(true);
		body->setTextInteractionFlags(Qt::TextSelectableByMouse);
		layout->addWidget(body);
		layout->addWidget(new QLabel("LOADED PULSE WEAVER MODULE ACTIONS"));
		moduleTree = new QTreeWidget;
		moduleTree->setHeaderLabels({"MODULE", "ACTION", "ID", "STATUS"});
		moduleTree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
		moduleTree->header()->setSectionResizeMode(1, QHeaderView::Stretch);
		layout->addWidget(moduleTree, 1);
		refreshModuleActions();
		tabs->addTab(page, "API / PLUGINS");
	}

	void refreshModuleActions()
	{
		if (!moduleTree)
			return;
		moduleTree->clear();
		for (const ModuleAction &action : moduleActions)
			new QTreeWidgetItem(moduleTree, {action.moduleName, action.actionName,
						 action.moduleId + "/" + action.actionId, "Ready"});
		if (moduleActions.empty())
			new QTreeWidgetItem(moduleTree, {"No extension actions registered", "", "", "Waiting"});
	}

	void refreshScenes()
	{
		const QSignalBlocker blocked(sceneList);
		const QString selected = sceneList->currentItem() ? sceneList->currentItem()->text() : QString();
		obs_source_t *current = obs_frontend_get_current_scene();
		const QString currentName = current ? QString::fromUtf8(obs_source_get_name(current)) : QString();
		if (current)
			obs_source_release(current);
		sceneList->clear();
		obs_frontend_source_list scenes = {};
		obs_frontend_get_scenes(&scenes);
		for (size_t i = 0; i < scenes.sources.num; ++i) {
			const QString name = QString::fromUtf8(obs_source_get_name(scenes.sources.array[i]));
			sceneList->addItem(name);
			if (name == currentName || (currentName.isEmpty() && name == selected))
				sceneList->setCurrentRow(sceneList->count() - 1);
		}
		obs_frontend_source_list_free(&scenes);
	}

	void refreshSources()
	{
		const QSignalBlocker blocked(sourceTree);
		sourceTree->clear();
		obs_source_t *sceneSource = obs_frontend_get_current_scene();
		if (!sceneSource)
			return;
		obs_scene_t *scene = obs_scene_from_source(sceneSource);
		std::vector<SourceRow> rows;
		if (scene) {
			obs_scene_enum_items(scene, [](obs_scene_t *, obs_sceneitem_t *item, void *data) {
				auto &items = *static_cast<std::vector<SourceRow> *>(data);
				obs_source_t *source = obs_sceneitem_get_source(item);
				items.push_back({QString::fromUtf8(obs_source_get_name(source)), QString::fromUtf8(obs_source_get_uuid(source)), obs_sceneitem_get_id(item), obs_sceneitem_visible(item)});
				return true;
			}, &rows);
		}
		for (const SourceRow &row : rows) {
			obs_source_t *source = obs_get_source_by_uuid(row.uuid.toUtf8().constData());
			const char *id = source ? obs_source_get_id(source) : "";
			auto *item = new QTreeWidgetItem(sourceTree, {row.name, QString::fromUtf8(id)});
			item->setData(0, Qt::UserRole, QVariant::fromValue<qlonglong>(row.itemId));
			item->setCheckState(0, row.visible ? Qt::Checked : Qt::Unchecked);
			if (source)
				obs_source_release(source);
		}
		obs_source_release(sceneSource);
	}

	void refreshAudio()
	{
		std::vector<AudioRow> rows;
		obs_enum_sources([](void *data, obs_source_t *source) {
			if ((obs_source_get_output_flags(source) & OBS_SOURCE_AUDIO) != 0) {
				auto &items = *static_cast<std::vector<AudioRow> *>(data);
				items.push_back({QString::fromUtf8(obs_source_get_name(source)), QString::fromUtf8(obs_source_get_uuid(source)), obs_source_get_volume(source), obs_source_muted(source)});
			}
			return true;
		}, &rows);
		audioTable->setRowCount(static_cast<int>(rows.size()));
		for (int rowIndex = 0; rowIndex < static_cast<int>(rows.size()); ++rowIndex) {
			const AudioRow row = rows[static_cast<size_t>(rowIndex)];
			audioTable->setItem(rowIndex, 0, new QTableWidgetItem(row.name));
			auto *level = new QSlider(Qt::Horizontal);
			level->setRange(0, 100);
			level->setValue(static_cast<int>(std::clamp(row.volume, 0.0f, 1.0f) * 100));
			connect(level, &QSlider::valueChanged, this, [uuid = row.uuid](int value) {
				if (obs_source_t *source = obs_get_source_by_uuid(uuid.toUtf8().constData())) {
					obs_source_set_volume(source, value / 100.0f);
					obs_source_release(source);
				}
			});
			audioTable->setCellWidget(rowIndex, 1, level);
			auto *mute = new QPushButton(row.muted ? "UNMUTE" : "MUTE");
			connect(mute, &QPushButton::clicked, this, [uuid = row.uuid, mute] {
				if (obs_source_t *source = obs_get_source_by_uuid(uuid.toUtf8().constData())) {
					const bool next = !obs_source_muted(source);
					obs_source_set_muted(source, next);
					mute->setText(next ? "UNMUTE" : "MUTE");
					obs_source_release(source);
				}
			});
			audioTable->setCellWidget(rowIndex, 2, mute);
			auto *filters = new QPushButton("FILTERS");
			connect(filters, &QPushButton::clicked, this, [uuid = row.uuid] {
				if (obs_source_t *source = obs_get_source_by_uuid(uuid.toUtf8().constData())) {
					obs_frontend_open_source_filters(source);
					obs_source_release(source);
				}
			});
			audioTable->setCellWidget(rowIndex, 3, filters);
		}
	}

	void ensureVerticalCanvas()
	{
		if (obs_canvas_t *existing = obs_get_canvas_by_name("Pulse Weaver Vertical")) {
			obs_video_info info = {};
			if (obs_canvas_get_video_info(existing, &info) && info.base_width == 1080 && info.base_height == 1920) {
				status->setText("Vertical canvas is ready: 1080 × 1920, native libobs canvas.");
				obs_canvas_release(existing);
				return;
			}
			info.base_width = info.output_width = 1080;
			info.base_height = info.output_height = 1920;
			obs_canvas_reset_video(existing, &info);
			obs_canvas_release(existing);
			status->setText("Vertical canvas repaired to 1080 × 1920.");
			return;
		}
		obs_video_info info = {};
		if (!obs_get_video_info(&info)) {
			status->setText("Video is not initialised yet; finish the first-run video setup and try again.");
			return;
		}
		info.base_width = info.output_width = 1080;
		info.base_height = info.output_height = 1920;
		obs_canvas_t *canvas = obs_frontend_add_canvas("Pulse Weaver Vertical", &info, PROGRAM);
		if (!canvas) {
			status->setText("Could not create the native vertical canvas.");
			return;
		}
		obs_scene_t *scene = obs_canvas_scene_create(canvas, "Vertical Starting Soon");
		if (scene)
			obs_canvas_set_channel(canvas, 0, obs_scene_get_source(scene));
		obs_canvas_release(canvas);
		status->setText("Native 1080 × 1920 canvas created and persisted in this scene collection.");
		refreshAll();
	}

	void refreshTargets()
	{
		if (!ruleTarget || !ruleAction)
			return;
		const QString previous = ruleTarget->currentText();
		ruleTarget->clear();
		const QString action = ruleAction->currentText();
		if (action == "Switch scene") {
			obs_frontend_source_list scenes = {};
			obs_frontend_get_scenes(&scenes);
			for (size_t i = 0; i < scenes.sources.num; ++i)
				ruleTarget->addItem(QString::fromUtf8(obs_source_get_name(scenes.sources.array[i])));
			obs_frontend_source_list_free(&scenes);
		} else if (action == "Trigger overlay" && overlays) {
			ruleTarget->addItems(overlays->overlayNames());
		} else if (action == "Apply light look") {
			QSettings settings(pulseSettingsPath(), QSettings::IniFormat);
			for (const QString &saved : settings.value("lights/devices").toStringList()) {
				const QStringList fields = saved.split('|');
				if (fields.size() >= 2)
					ruleTarget->addItem(fields.value(2, fields.value(1)));
			}
		} else if (action.contains("source", Qt::CaseInsensitive) || action == "Restart media") {
			obs_enum_sources([](void *data, obs_source_t *source) {
				static_cast<QStringList *>(data)->append(QString::fromUtf8(obs_source_get_name(source)));
				return true;
			}, &targetNames);
			targetNames.removeDuplicates();
			targetNames.sort(Qt::CaseInsensitive);
			ruleTarget->addItems(targetNames);
			targetNames.clear();
		}
		const int match = ruleTarget->findText(previous);
		if (match >= 0)
			ruleTarget->setCurrentIndex(match);
	}

	QStringList targetNames;

	bool applyLightLook(const QString &target, const QString &lookName)
	{
		QSettings settings(pulseSettingsPath(), QSettings::IniFormat);
		QStringList device;
		for (const QString &saved : settings.value("lights/devices").toStringList()) {
			const QStringList fields = saved.split('|');
			if (fields.size() >= 2 && (fields.value(1).compare(target, Qt::CaseInsensitive) == 0 ||
						fields.value(2).compare(target, Qt::CaseInsensitive) == 0)) {
				device = fields;
				break;
			}
		}
		QStringList look;
		for (const QString &saved : settings.value("lights/looks").toStringList()) {
			const QStringList fields = saved.split('|');
			if (fields.size() >= 4 && fields[0].compare(lookName, Qt::CaseInsensitive) == 0) {
				look = fields;
				break;
			}
		}
		if (device.size() < 2 || look.size() < 4)
			return false;
		const QString provider = device[0];
		const QString endpoint = device[1];
		const QColor colour(look[1]);
		const int brightness = std::clamp(look[2].toInt(), 1, 255);
		const bool on = look[3] == "1";
		const QString credential = unprotectCredential(settings.value("lights/auth/" + endpoint).toString());
		const QString entity = settings.value("lights/entity/" + endpoint).toString();
		QUrl url("http://" + endpoint);
		QJsonObject body{{"on", on}, {"bri", brightness}, {"rgb", QJsonArray{colour.red(), colour.green(), colour.blue()}}};
		QNetworkRequest request;
		if (provider == "WLED") {
			url.setPath("/json/state");
			body.remove("rgb");
			body.insert("seg", QJsonArray{QJsonObject{{"col", QJsonArray{QJsonArray{colour.red(), colour.green(), colour.blue()}}}}});
		} else if (provider == "Home Assistant") {
			url.setPath(on ? "/api/services/light/turn_on" : "/api/services/light/turn_off");
			body.insert("entity_id", entity);
			body.insert("brightness", brightness);
			body.insert("rgb_color", body.take("rgb"));
		} else if (provider == "Philips Hue") {
			url.setPath("/api/" + credential + "/lights/" + entity + "/state");
			body.remove("rgb");
			body.insert("hue", int(std::max(0.0f, colour.hsvHueF()) * 65535));
			body.insert("sat", int(colour.hsvSaturationF() * 254));
		} else {
			url.setPath(entity.startsWith('/') ? entity : "/" + (entity.isEmpty() ? QString("pulseweaver") : entity));
		}
		request.setUrl(url);
		request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
		if (provider == "Home Assistant" && !credential.isEmpty())
			request.setRawHeader("Authorization", "Bearer " + credential.toUtf8());
		QNetworkReply *reply = actionNetwork->post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
		connect(reply, &QNetworkReply::finished, this, [this, reply, target, lookName] {
			const int code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
			const bool ok = reply->error() == QNetworkReply::NoError && code >= 200 && code < 300;
			eventLog->append(QString("LIGHT %1 → %2 %3").arg(lookName.toHtmlEscaped(), target.toHtmlEscaped(), ok ? "✓" : "FAILED"));
			reply->deleteLater();
		});
		return true;
	}

	static QJsonValue eventValue(const QJsonObject &eventData, const QString &field)
	{
		if (field.isEmpty())
			return {};
		if (eventData.contains(field))
			return eventData.value(field);
		QJsonValue value(eventData);
		for (const QString &part : field.split('.', Qt::SkipEmptyParts)) {
			if (!value.isObject())
				return {};
			value = value.toObject().value(part);
		}
		return value;
	}

	static QString valueText(const QJsonValue &value)
	{
		if (value.isString())
			return value.toString();
		if (value.isDouble())
			return QString::number(value.toDouble());
		if (value.isBool())
			return value.toBool() ? "true" : "false";
		if (!value.isUndefined() && !value.isNull())
			return QString::fromUtf8(QJsonDocument(QJsonObject{{"value", value}}).toJson(QJsonDocument::Compact));
		return {};
	}

	static QString expandTemplate(QString text, const QJsonObject &eventData)
	{
		static const QRegularExpression token(R"(\{\{\s*([^}\s]+)\s*\}\})");
		QRegularExpressionMatch match;
		int offset = 0;
		while ((offset = text.indexOf(token, offset, &match)) >= 0) {
			const QString replacement = valueText(eventValue(eventData, match.captured(1)));
			text.replace(offset, match.capturedLength(), replacement);
			offset += replacement.size();
		}
		return text;
	}

	bool conditionMatches(const Rule &rule, const QJsonObject &eventData) const
	{
		if (rule.conditionOperator == "Always" || rule.conditionField.isEmpty())
			return true;
		const QJsonValue found = eventValue(eventData, rule.conditionField);
		if (rule.conditionOperator == "Exists")
			return !found.isUndefined() && !found.isNull();
		const QString actual = valueText(found);
		if (rule.conditionOperator == "Equals")
			return actual.compare(rule.conditionValue, Qt::CaseInsensitive) == 0;
		if (rule.conditionOperator == "Not equal")
			return actual.compare(rule.conditionValue, Qt::CaseInsensitive) != 0;
		if (rule.conditionOperator == "Contains")
			return actual.contains(rule.conditionValue, Qt::CaseInsensitive);
		bool actualNumber = false;
		bool expectedNumber = false;
		const double lhs = actual.toDouble(&actualNumber);
		const double rhs = rule.conditionValue.toDouble(&expectedNumber);
		if (!actualNumber || !expectedNumber)
			return false;
		return rule.conditionOperator == "Greater" ? lhs > rhs : lhs >= rhs;
	}

	void executeRule(const Rule &rule, const QJsonObject &eventData)
	{
		if (!conditionMatches(rule, eventData)) {
			eventLog->append(QString("<span style='color:#94a3b8'>SKIP %1 — condition %2 %3 %4 was false</span>")
					 .arg(rule.action.toHtmlEscaped(), rule.conditionField.toHtmlEscaped(),
					      rule.conditionOperator.toHtmlEscaped(), rule.conditionValue.toHtmlEscaped()));
			return;
		}
		const Rule copy = rule;
		QTimer::singleShot(std::max(0, rule.delayMs), this, [this, copy, eventData] {
			executeAction(copy.action, expandTemplate(copy.target, eventData), expandTemplate(copy.value, eventData), eventData);
		});
	}

	void executeAction(const QString &action, const QString &target, const QString &value, const QJsonObject &eventData)
	{
		bool success = true;
		if (action == "Switch scene")
			success = activateScene(target);
		else if (action == "Trigger overlay")
			success = overlays && overlays->triggerOverlay(target, eventData);
		else if (action == "Apply light look")
			success = applyLightLook(target, value);
		else if (action == "Start stream")
			obs_frontend_streaming_start();
		else if (action == "Stop stream")
			obs_frontend_streaming_stop();
		else if (action == "Start recording")
			obs_frontend_recording_start();
		else if (action == "Stop recording")
			obs_frontend_recording_stop();
		else if (action == "Mute source" || action == "Unmute source") {
			obs_source_t *source = obs_get_source_by_name(target.toUtf8().constData());
			success = source != nullptr;
			if (source) {
				obs_source_set_muted(source, action == "Mute source");
				obs_source_release(source);
			}
		} else if (action == "Set source text") {
			obs_source_t *source = obs_get_source_by_name(target.toUtf8().constData());
			success = source != nullptr;
			if (source) {
				obs_data_t *settings = obs_source_get_settings(source);
				obs_data_set_string(settings, "text", value.toUtf8().constData());
				obs_source_update(source, settings);
				obs_data_release(settings);
				obs_source_release(source);
			}
		} else if (action == "Restart media") {
			obs_source_t *source = obs_get_source_by_name(target.toUtf8().constData());
			success = source != nullptr;
			if (source) {
				obs_source_media_restart(source);
				obs_source_release(source);
			}
		} else if (action == "Show source" || action == "Hide source" || action == "Toggle source") {
			obs_source_t *sceneSource = obs_frontend_get_current_scene();
			obs_scene_t *scene = sceneSource ? obs_scene_from_source(sceneSource) : nullptr;
			OBSSceneItem item = PulseRuntimeSafety::findSceneItem(scene, target.toUtf8().constData());
			success = item != nullptr;
			if (item)
				obs_sceneitem_set_visible(item, action == "Toggle source" ? !obs_sceneitem_visible(item) : action == "Show source");
			if (sceneSource)
				obs_source_release(sceneSource);
		} else if (action == "Send Twitch chat") {
			success = twitch != nullptr && !(value.isEmpty() ? target : value).isEmpty();
			if (success)
				twitch->sendMessage(value.isEmpty() ? target : value);
		} else if (action == "HTTP request") {
			const QUrl url(target);
			success = url.isValid() && (url.scheme() == "http" || url.scheme() == "https");
			if (success) {
				QNetworkRequest request(url);
				request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
				QNetworkReply *reply = value.isEmpty() ? actionNetwork->get(request) : actionNetwork->post(request, value.toUtf8());
				connect(reply, &QNetworkReply::finished, this, [this, reply] {
					const int code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
					eventLog->append(QString("HTTP action completed: %1 %2").arg(code).arg(reply->errorString()).toHtmlEscaped());
					reply->deleteLater();
				});
			}
		} else if (action == "Write log") {
			eventLog->append((value.isEmpty() ? target : value).toHtmlEscaped());
		} else if (action == "Emit event") {
			const int dot = target.indexOf('.');
			success = dot > 0;
			if (success)
				publishEvent(target.left(dot), target.mid(dot + 1), value.isEmpty() ? "{}" : value);
		} else if (action.startsWith("Module · ")) {
			success = false;
			for (const ModuleAction &moduleAction : moduleActions) {
				if (moduleAction.displayName() != action)
					continue;
				calldata_t call;
				calldata_init(&call);
				calldata_set_string(&call, "value", value.toUtf8().constData());
				calldata_set_string(&call, "json", QJsonDocument(eventData).toJson(QJsonDocument::Compact).constData());
				const bool called = proc_handler_call(obs_get_proc_handler(), moduleAction.procName.toUtf8().constData(), &call);
				success = called && calldata_bool(&call, "success");
				const char *message = calldata_string(&call, "message");
				if (message && *message)
					eventLog->append(QString::fromUtf8(message).toHtmlEscaped());
				calldata_free(&call);
				break;
			}
		} else {
			success = false;
		}
		eventLog->append(QString("<span style='color:%1'>ACTION %2 → %3 %4</span>")
					 .arg(success ? "#22d3ee" : "#fb7185", action.toHtmlEscaped(), target.toHtmlEscaped(), success ? "✓" : "FAILED"));
	}

	void refreshRules()
	{
		if (!rulesTable)
			return;
		rulesTable->setRowCount(static_cast<int>(rules.size()));
		for (int row = 0; row < static_cast<int>(rules.size()); ++row) {
			const Rule &rule = rules[static_cast<size_t>(row)];
			auto *enabled = new QPushButton(rule.enabled ? "●" : "○");
			enabled->setCheckable(true);
			enabled->setChecked(rule.enabled);
			enabled->setToolTip("Enable or disable this command-flow step");
			connect(enabled, &QPushButton::toggled, this, [this, row, enabled](bool value) {
				rules[static_cast<size_t>(row)].enabled = value;
				enabled->setText(value ? "●" : "○");
				saveRules();
			});
			rulesTable->setCellWidget(row, 0, enabled);
			rulesTable->setItem(row, 1, new QTableWidgetItem(rule.eventKey));
			const QString condition = rule.conditionOperator == "Always" || rule.conditionField.isEmpty()
						  ? "Always"
						  : rule.conditionField + " " + rule.conditionOperator + " " + rule.conditionValue;
			rulesTable->setItem(row, 2, new QTableWidgetItem(condition));
			rulesTable->setItem(row, 3, new QTableWidgetItem(QString::number(rule.delayMs) + " ms"));
			rulesTable->setItem(row, 4, new QTableWidgetItem(rule.action));
			rulesTable->setItem(row, 5, new QTableWidgetItem(rule.target));
			rulesTable->setItem(row, 6, new QTableWidgetItem(rule.value));
			auto *tools = new QWidget;
			auto *toolsLayout = new QHBoxLayout(tools);
			toolsLayout->setContentsMargins(0, 0, 0, 0);
			for (const QString &label : {QString("↑"), QString("↓"), QString("×")}) {
				auto *button = new QPushButton(label);
				toolsLayout->addWidget(button);
				connect(button, &QPushButton::clicked, this, [this, row, label] {
					if (label == "×")
						rules.erase(rules.begin() + row);
					else if (label == "↑" && row > 0)
						std::swap(rules[size_t(row)], rules[size_t(row - 1)]);
					else if (label == "↓" && row + 1 < int(rules.size()))
						std::swap(rules[size_t(row)], rules[size_t(row + 1)]);
					refreshRules();
					saveRules();
				});
			}
			rulesTable->setCellWidget(row, 7, tools);
		}
	}

	void loadRules()
	{
		QSettings settings(pulseSettingsPath(), QSettings::IniFormat);
		const int count = settings.beginReadArray("automations");
		for (int i = 0; i < count; ++i) {
			settings.setArrayIndex(i);
			rules.push_back({settings.value("event").toString(), settings.value("action").toString(),
					 settings.value("target").toString(), settings.value("value").toString(),
					 settings.value("conditionField").toString(), settings.value("conditionOperator", "Always").toString(),
					 settings.value("conditionValue").toString(), settings.value("delayMs", 0).toInt(),
					 settings.value("enabled", true).toBool()});
		}
		settings.endArray();
		refreshRules();
	}

	void saveRules()
	{
		QSettings settings(pulseSettingsPath(), QSettings::IniFormat);
		settings.beginWriteArray("automations");
		for (int i = 0; i < static_cast<int>(rules.size()); ++i) {
			settings.setArrayIndex(i);
			const Rule &rule = rules[static_cast<size_t>(i)];
			settings.setValue("event", rule.eventKey);
			settings.setValue("action", rule.action);
			settings.setValue("target", rule.target);
			settings.setValue("value", rule.value);
			settings.setValue("conditionField", rule.conditionField);
			settings.setValue("conditionOperator", rule.conditionOperator);
			settings.setValue("conditionValue", rule.conditionValue);
			settings.setValue("delayMs", rule.delayMs);
			settings.setValue("enabled", rule.enabled);
		}
		settings.endArray();
	}

	QJsonArray sceneJson() const
	{
		QJsonArray result;
		obs_frontend_source_list scenes = {};
		obs_frontend_get_scenes(&scenes);
		obs_source_t *current = obs_frontend_get_current_scene();
		const QString currentUuid = current ? QString::fromUtf8(obs_source_get_uuid(current)) : QString();
		if (current)
			obs_source_release(current);
		for (size_t i = 0; i < scenes.sources.num; ++i) {
			obs_source_t *scene = scenes.sources.array[i];
			const QString uuid = QString::fromUtf8(obs_source_get_uuid(scene));
			result.append(QJsonObject{{"name", QString::fromUtf8(obs_source_get_name(scene))}, {"uuid", uuid}, {"active", uuid == currentUuid}});
		}
		obs_frontend_source_list_free(&scenes);
		return result;
	}

	QJsonArray sourceJson() const
	{
		QJsonArray result;
		obs_frontend_source_list scenes = {};
		obs_frontend_get_scenes(&scenes);
		for (size_t index = 0; index < scenes.sources.num; ++index) {
			obs_source_t *sceneSource = scenes.sources.array[index];
			obs_scene_t *scene = obs_scene_from_source(sceneSource);
			if (!scene)
				continue;
			struct Context {
				QJsonArray *result;
				QString scene;
			} context{&result, QString::fromUtf8(obs_source_get_name(sceneSource))};
			obs_scene_enum_items(scene, [](obs_scene_t *, obs_sceneitem_t *item, void *opaque) {
				auto *context = static_cast<Context *>(opaque);
				obs_source_t *source = obs_sceneitem_get_source(item);
				obs_transform_info transform{};
				obs_sceneitem_get_info2(item, &transform);
				context->result->append(QJsonObject{
					{"scene", context->scene},
					{"name", QString::fromUtf8(obs_source_get_name(source))},
					{"uuid", QString::fromUtf8(obs_source_get_uuid(source))},
					{"kind", QString::fromUtf8(obs_source_get_id(source))},
					{"itemId", double(obs_sceneitem_get_id(item))},
					{"visible", obs_sceneitem_visible(item)},
					{"locked", obs_sceneitem_locked(item)},
					{"x", transform.pos.x}, {"y", transform.pos.y},
					{"scaleX", transform.scale.x}, {"scaleY", transform.scale.y},
					{"rotation", transform.rot}});
				return true;
			}, &context);
		}
		obs_frontend_source_list_free(&scenes);
		return result;
	}

	QJsonArray moduleJson() const
	{
		QJsonArray result;
		for (const ModuleAction &action : moduleActions)
			result.append(QJsonObject{{"moduleId", action.moduleId}, {"moduleName", action.moduleName},
				{"actionId", action.actionId}, {"actionName", action.actionName}, {"status", "ready"}});
		return result;
	}

	QJsonObject lumiaStateJson() const
	{
		QWidget *mainWindow = static_cast<QWidget *>(obs_frontend_get_main_window());
		auto *stageSelector = mainWindow ? mainWindow->findChild<QComboBox *>("PulseWeaverStageSelector") : nullptr;
		QJsonArray stages;
		if (stageSelector) {
			for (int index = 0; index < stageSelector->count(); ++index)
				stages.append(pulseLumiaStageName(stageSelector, index));
		}
		QJsonObject destinations;
		for (const QString &provider : {QString("Twitch"), QString("YouTube"), QString("Kick")}) {
			auto *route = mainWindow ? mainWindow->findChild<QComboBox *>("PulseWeaverDestination" + provider) : nullptr;
			if (route)
				destinations.insert(provider.toLower(), route->currentData().toString());
		}
		QString status;
		if (auto *label = mainWindow ? mainWindow->findChild<QLabel *>("PulseWeaverDestinationStatus") : nullptr)
			status = label->text();
		QJsonObject state{{"product", "Pulse Weaver"}, {"connected", mainWindow != nullptr},
			{"live", mainWindow && mainWindow->property("pulseWeaverAnyLive").toBool()},
			{"recording", mainWindow && mainWindow->property("pulseWeaverRecordingActive").toBool()},
			{"activeStage", stageSelector ? pulseLumiaStageName(stageSelector, stageSelector->currentIndex()) : QString()},
			{"activeStageIndex", stageSelector ? stageSelector->currentIndex() : -1},
			{"stages", stages}, {"destinations", destinations}, {"status", status}};
		if (motion) {
			const QJsonObject motionState = motion->stateJson();
			state.insert("motionActions", motionState.value("actions"));
			state.insert("motion", motionState);
		}
		return state;
	}

	void startApi()
	{
		api = new QTcpServer(this);
		if (!api->listen(QHostAddress::LocalHost, apiPort)) {
			apiPort = 0;
			return;
		}
		connect(api, &QTcpServer::newConnection, this, [this] {
			while (QTcpSocket *socket = api->nextPendingConnection()) {
				connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
				if (api->findChildren<QTcpSocket *>().size() > 32) {
					socket->abort();
					socket->deleteLater();
					continue;
				}
				socket->setReadBufferSize(PulseRuntimeSafety::maxRequestBytes + 1);
				QTimer::singleShot(5000, socket, [socket] {
					if (!socket->property("pulseLumiaSubscribed").toBool()) {
						socket->abort();
						socket->deleteLater();
					}
				});
				connect(socket, &QTcpSocket::readyRead, this, [this, socket] {
					if (socket->property("pulseLumiaSubscribed").toBool()) { socket->readAll(); return; }
					if (socket->property("pulseHttpHandled").toBool()) { socket->readAll(); return; }
					const QByteArray request = socket->property("pulseHttpBuffer").toByteArray() + socket->readAll();
					const auto frame = PulseRuntimeSafety::httpFrame(request);
					if (frame == PulseRuntimeSafety::HttpFrame::Incomplete) {
						socket->setProperty("pulseHttpBuffer", request);
						return;
					}
					socket->setProperty("pulseHttpHandled", true);
					socket->setProperty("pulseHttpBuffer", QVariant());
					if (frame == PulseRuntimeSafety::HttpFrame::Invalid) {
						respond(socket, 400, {{"error", "Invalid or oversized request framing"}});
						return;
					}
					const QByteArray headerBlock = request.left(request.indexOf("\r\n\r\n"));
					const QByteArray expected = "Bearer " + apiToken.toUtf8();
					bool authorized = false;
					QByteArray integrationClient;
					for (QByteArray line : headerBlock.split('\n')) {
						line = line.trimmed();
						if (line.toLower().startsWith("authorization:") && line.mid(line.indexOf(':') + 1).trimmed() == expected)
							authorized = true;
						else if (line.toLower().startsWith("x-pulse-weaver-client:"))
							integrationClient = line.mid(line.indexOf(':') + 1).trimmed();
					}
					if (!authorized) {
						respond(socket, 401, QJsonObject{{"error", "unauthorized"}});
						return;
					}
					const QList<QByteArray> first = request.left(request.indexOf("\r\n")).split(' ');
					if (first.size() < 2) {
						respond(socket, 400, QJsonObject{{"error", "invalid request"}});
						return;
					}
					const QByteArray method = first[0];
					const QUrl url = QUrl::fromEncoded(first[1]);
					const QUrlQuery query(url);
					const QByteArray body = request.mid(request.indexOf("\r\n\r\n") + 4);
					QJsonObject input;
					if (method == "POST" && !body.isEmpty()) {
						QJsonParseError error;
						const auto document = QJsonDocument::fromJson(body, &error);
						if (error.error != QJsonParseError::NoError || !document.isObject()) {
							respond(socket, 400, {{"error", "Expected a JSON object"}});
							return;
						}
						input = document.object();
					}
					const QString path = url.path();
					if (integrationClient == "lumia-plugin" && !path.startsWith("/api/v1/lumia/")) {
						respond(socket, 403, {{"error", "Lumia may only use the restricted show-operation routes."}});
						return;
					}
					if (path.startsWith("/api/v1/lumia/") && integrationClient != "lumia-plugin") {
						respond(socket, 403, QJsonObject{{"error", "This control surface is reserved for the Pulse Weaver Lumia plugin."}});
						return;
					}
					if (method == "GET" && path == "/api/v1/health")
						respond(socket, 200, QJsonObject{{"product", "Pulse Weaver Core"}, {"engine", "libobs in-process"}, {"websocketRequired", false}, {"streaming", obs_frontend_streaming_active()}, {"recording", obs_frontend_recording_active()}});
					else if (method == "GET" && path == "/api/v1/scenes")
						respond(socket, 200, QJsonObject{{"scenes", sceneJson()}});
					else if (method == "GET" && path == "/api/v1/sources")
						respond(socket, 200, QJsonObject{{"sources", sourceJson()}});
					else if (method == "GET" && path == "/api/v1/automations")
						respond(socket, 200, QJsonObject{{"automations", automationJson()}});
					else if (method == "GET" && path == "/api/v1/overlays")
						respond(socket, 200, overlays ? overlays->catalogueJson() : QJsonObject{{"overlays", QJsonArray{}}});
					else if (method == "GET" && path == "/api/v1/modules")
						respond(socket, 200, QJsonObject{{"actions", moduleJson()}});
					else if (method == "GET" && path == "/api/v1/lumia/state")
						respond(socket, 200, lumiaBridge->stateJson());
					else if (method == "GET" && (path == "/api/v1/lumia/motion" || path == "/api/v1/motion"))
						respond(socket, 200, motion ? motion->stateJson() : QJsonObject{{"actions", QJsonArray{}}});
					else if (method == "GET" && path == "/api/v1/lumia/events") {
						socket->setProperty("pulseLumiaSubscribed", true);
						lumiaBridge->subscribe(socket);
					}
					else if (method == "POST" && path == "/api/v1/lumia/source") {
						const QJsonObject result = lumiaBridge->operateSource(query);
						respond(socket, result.value("ok").toBool() ? 200 : 400, result);
					}
					else if (method == "POST" && (path == "/api/v1/lumia/destination/start" || path == "/api/v1/lumia/destination/stop")) {
						QWidget *window = static_cast<QWidget *>(obs_frontend_get_main_window());
						QJsonObject result;
						const QString provider = query.queryItemValue("platform");
						const bool invoked = window && QMetaObject::invokeMethod(window, "PulseWeaverLumiaDestination", Qt::DirectConnection,
							Q_RETURN_ARG(QJsonObject, result), Q_ARG(QString, provider), Q_ARG(bool, path.endsWith("/start")));
						if (!invoked) result = {{"ok", false}, {"message", "Update Pulse Weaver to use platform controls."}};
						respond(socket, result.value("ok").toBool() ? 202 : 400, result);
					}
					else if (method == "POST" && (path == "/api/v1/lumia/stage" ||
						 path == "/api/v1/lumia/stage/next" || path == "/api/v1/lumia/stage/previous")) {
						QWidget *mainWindow = static_cast<QWidget *>(obs_frontend_get_main_window());
						auto *selector = mainWindow ? mainWindow->findChild<QComboBox *>("PulseWeaverStageSelector") : nullptr;
						int target = -1;
						if (selector && selector->count() > 0) {
							if (path.endsWith("/next"))
								target = (selector->currentIndex() + 1) % selector->count();
							else if (path.endsWith("/previous"))
								target = (selector->currentIndex() - 1 + selector->count()) % selector->count();
							else {
								const QString requested = query.queryItemValue("name", QUrl::FullyDecoded);
								target = selector->findData(requested, Qt::UserRole + 1, Qt::MatchFixedString);
								if (target < 0)
									target = selector->findText(requested, Qt::MatchFixedString);
								if (target < 0) {
									const int separator = requested.indexOf("  ·  ");
									if (separator > 0)
										target = selector->findData(requested.left(separator), Qt::UserRole + 1, Qt::MatchFixedString);
								}
							}
						}
						if (target >= 0)
							selector->setCurrentIndex(target);
						QJsonObject result = lumiaStateJson();
						result.insert("ok", target >= 0);
						result.insert("message", target >= 0 ? "Stage changed to “" + pulseLumiaStageName(selector, target) + "”." : "Stage was not found.");
						respond(socket, target >= 0 ? 200 : 404, result);
					}
					else if (method == "POST" && (path == "/api/v1/lumia/motion/run" || path == "/api/v1/motion/run")) {
						const QString action = query.queryItemValue("id", QUrl::FullyDecoded).isEmpty() ?
							query.queryItemValue("name", QUrl::FullyDecoded) : query.queryItemValue("id", QUrl::FullyDecoded);
						const QJsonObject result = motion ? motion->runAction(action, query.queryItemValue("request", QUrl::FullyDecoded)) :
							QJsonObject{{"ok", false}, {"message", "Motion is unavailable."}};
						respond(socket, result.value("ok").toBool() ? 202 : 400, result);
					}
					else if (method == "POST" && (path == "/api/v1/lumia/motion/stop" || path == "/api/v1/motion/stop")) {
						const QJsonObject result = motion ? motion->stopAction(query.queryItemValue("execution", QUrl::FullyDecoded), true) :
							QJsonObject{{"ok", false}, {"message", "Motion is unavailable."}};
						respond(socket, result.value("ok").toBool() ? 202 : 400, result);
					}
					else if (method == "POST" && (path == "/api/v1/lumia/motion/restore" || path == "/api/v1/motion/restore")) {
						const QJsonObject result = motion ? motion->restoreLast() :
							QJsonObject{{"ok", false}, {"message", "Motion is unavailable."}};
						respond(socket, result.value("ok").toBool() ? 200 : 400, result);
					}
					else if (method == "POST" && (path == "/api/v1/lumia/motion/original" || path == "/api/v1/motion/original")) {
						const QJsonObject result = motion ? motion->restoreOriginals() : QJsonObject{{"ok", false}, {"message", "Motion is unavailable."}};
						respond(socket, result.value("ok").toBool() ? 200 : 400, result);
					}
					else if (method == "POST" && path == "/api/v1/motion/show/create") {
						const QJsonObject result = motion ? motion->createShowStages() : QJsonObject{{"ok", false}, {"message", "Motion is unavailable."}};
						respond(socket, result.value("ok").toBool() ? 200 : 400, result);
					}
					else if (method == "POST" && path == "/api/v1/motion/import") {
						const QJsonObject result = motion ? motion->importDocument(input, integrationClient) :
							QJsonObject{{"ok", false}, {"message", "Motion is unavailable."}};
						respond(socket, result.value("ok").toBool() ? 200 : 400, result);
					}
					else if (method == "POST" && (path == "/api/v1/lumia/go-live" ||
						 path == "/api/v1/lumia/end-stream")) {
						QWidget *mainWindow = static_cast<QWidget *>(obs_frontend_get_main_window());
						auto *button = mainWindow ? mainWindow->findChild<QPushButton *>("PulseWeaverLive") : nullptr;
						const bool currentlyLive = mainWindow && mainWindow->property("pulseWeaverAnyLive").toBool();
						const bool start = path.endsWith("/go-live");
						if (!start && mainWindow) {
							mainWindow->setProperty("pulseWeaverGoLiveSession", false);
							bool stopped = true;
							for (const QString &provider : {QString("twitch"), QString("kick"), QString("youtube")}) {
								QJsonObject result;
								const bool invoked = QMetaObject::invokeMethod(mainWindow, "PulseWeaverLumiaDestination", Qt::DirectConnection,
									Q_RETURN_ARG(QJsonObject, result), Q_ARG(QString, provider), Q_ARG(bool, false));
								stopped = invoked && result.value("ok").toBool() && stopped;
							}
							respond(socket, stopped ? 200 : 400, QJsonObject{{"ok", stopped}, {"accepted", stopped},
								{"message", stopped ? "Stop requested for all show destinations." : "One or more destinations could not be stopped."}});
							return;
						}
						if (button && start != currentlyLive) {
							mainWindow->setProperty("pulseWeaverLumiaGoLiveConfirmed", start);
							button->click();
						}
						const bool accepted = button && (start == currentlyLive ||
							mainWindow->property("pulseWeaverControlAccepted").toBool());
						QString message = mainWindow ? mainWindow->property("pulseWeaverControlResult").toString() : QString();
						if (start == currentlyLive)
							message = start ? "Pulse Weaver is already live." : "Pulse Weaver outputs are already stopped.";
						if (message.isEmpty())
							message = accepted ? (start ? "Pulse Weaver accepted Lumia’s Go Live command." :
								"Pulse Weaver is stopping all outputs.") : "Pulse Weaver rejected the command.";
						QJsonObject result = lumiaStateJson();
						result.insert("ok", accepted);
						result.insert("accepted", accepted);
						result.insert("message", message);
						respond(socket, accepted ? (start && !currentlyLive ? 202 : 200) : 400, result);
					}
					else if (method == "POST" && (path == "/api/v1/lumia/record/start" ||
						 path == "/api/v1/lumia/record/stop")) {
						QWidget *mainWindow = static_cast<QWidget *>(obs_frontend_get_main_window());
						auto *button = mainWindow ? mainWindow->findChild<QPushButton *>("PulseWeaverRecord") : nullptr;
						const bool active = mainWindow && mainWindow->property("pulseWeaverRecordingActive").toBool();
						const bool start = path.endsWith("/start");
						auto *route = mainWindow ? mainWindow->findChild<QComboBox *>("PulseWeaverRecordingDestination") : nullptr;
						if (start && route && route->currentData().toString() == "off") {
							respond(socket, 400, {{"ok", false}, {"message", "Choose a recording mode inside Pulse Weaver first."}});
							return;
						}
						if (button && start != active)
							button->click();
						QJsonObject result = lumiaStateJson();
						result.insert("ok", button != nullptr);
						result.insert("message", start ? "Pulse Weaver recording command accepted." :
							"Pulse Weaver stop-recording command accepted.");
						respond(socket, button ? 200 : 400, result);
					}
					else if (method == "POST" && path == "/api/v1/overlay/trigger") {
						const bool found = overlays && overlays->triggerOverlay(query.queryItemValue("name", QUrl::FullyDecoded), QJsonDocument::fromJson(body).object());
						respond(socket, found ? 200 : 404, QJsonObject{{"ok", found}});
					}
					else if (method == "POST" && path == "/api/v1/overlay/add-to-scene") {
						QString error;
						const QString target = query.queryItemValue("target", QUrl::FullyDecoded);
						const bool added = overlays && overlays->addToCameraScenes(
							query.queryItemValue("name", QUrl::FullyDecoded), target.isEmpty() ? "landscape" : target, &error);
						respond(socket, added ? 200 : 400, QJsonObject{{"ok", added}, {"error", error}});
					}
					else if (method == "POST" && path == "/api/v1/scene") {
						const bool found = activateScene(query.queryItemValue("name"));
						respond(socket, found ? 200 : 404, QJsonObject{{"ok", found}});
					}
					else if (method == "POST" && path == "/api/v1/scene/create") {
						const QString name = query.queryItemValue("name", QUrl::FullyDecoded).trimmed();
						obs_source_t *existing = name.isEmpty() ? nullptr : obs_get_source_by_name(name.toUtf8().constData());
						if (existing)
							obs_source_release(existing);
						obs_scene_t *scene = (!name.isEmpty() && !existing) ? obs_scene_create(name.toUtf8().constData()) : nullptr;
						if (scene) {
							obs_scene_release(scene);
							obs_frontend_save();
						}
						respond(socket, scene ? 201 : 400, QJsonObject{{"ok", scene != nullptr}, {"name", name}});
					}
					else if (method == "POST" && path == "/api/v1/source/visibility") {
						const QString name = query.queryItemValue("name", QUrl::FullyDecoded);
						OBSSceneItem item = findCurrentSceneItem(name);
						const QJsonObject input = QJsonDocument::fromJson(body).object();
						if (item) {
							obs_sceneitem_set_visible(item, input.value("visible").toBool(true));
							obs_frontend_save();
						}
						respond(socket, item ? 200 : 404, QJsonObject{{"ok", item != nullptr}});
					}
					else if (method == "POST" && path == "/api/v1/source/create") {
						const QString name = query.queryItemValue("name", QUrl::FullyDecoded).trimmed();
						const QString kind = query.queryItemValue("kind", QUrl::FullyDecoded).trimmed();
						obs_source_t *sceneSource = obs_frontend_get_current_scene();
						obs_scene_t *scene = sceneSource ? obs_scene_from_source(sceneSource) : nullptr;
						obs_source_t *existing = name.isEmpty() ? nullptr : obs_get_source_by_name(name.toUtf8().constData());
						const QJsonObject input = QJsonDocument::fromJson(body).object();
						const QByteArray settingsJson = QJsonDocument(input.value("settings").toObject()).toJson(QJsonDocument::Compact);
						obs_data_t *settings = obs_data_create_from_json(settingsJson.constData());
						obs_source_t *source = (!name.isEmpty() && !kind.isEmpty() && scene && !existing)
							? obs_source_create(kind.toUtf8().constData(), name.toUtf8().constData(), settings, nullptr)
							: nullptr;
						OBSSceneItem item = source ? obs_scene_add(scene, source) : nullptr;
						if (settings)
							obs_data_release(settings);
						if (existing)
							obs_source_release(existing);
						if (source)
							obs_source_release(source);
						if (sceneSource)
							obs_source_release(sceneSource);
						if (item)
							obs_frontend_save();
						respond(socket, item ? 201 : 400, QJsonObject{{"ok", item != nullptr}, {"name", name},
							{"itemId", item ? double(obs_sceneitem_get_id(item)) : -1.0}});
					}
					else if (method == "POST" && path == "/api/v1/source/transform") {
						const QString name = query.queryItemValue("name", QUrl::FullyDecoded);
						OBSSceneItem item = findCurrentSceneItem(name);
						const QJsonObject input = QJsonDocument::fromJson(body).object();
						if (item) {
							obs_transform_info transform{};
							obs_sceneitem_get_info2(item, &transform);
							transform.pos.x = float(std::clamp(input.value("x").toDouble(transform.pos.x), -10000.0, 10000.0));
							transform.pos.y = float(std::clamp(input.value("y").toDouble(transform.pos.y), -10000.0, 10000.0));
							transform.scale.x = float(std::clamp(input.value("scaleX").toDouble(transform.scale.x), -100.0, 100.0));
							transform.scale.y = float(std::clamp(input.value("scaleY").toDouble(transform.scale.y), -100.0, 100.0));
							transform.rot = float(std::clamp(input.value("rotation").toDouble(transform.rot), -3600.0, 3600.0));
							obs_sceneitem_set_info2(item, &transform);
							obs_frontend_save();
						}
						respond(socket, item ? 200 : 404, QJsonObject{{"ok", item != nullptr}});
					}
					else if (method == "POST" && path == "/api/v1/automation") {
						const QJsonObject input = QJsonDocument::fromJson(body).object();
						const QString event = input.value("event").toString().trimmed();
						const QString action = input.value("action").toString().trimmed();
						const bool valid = !event.isEmpty() && ruleAction && ruleAction->findText(action) >= 0;
						if (valid) {
							rules.push_back({event, action, input.value("target").toString(), input.value("value").toString(),
								input.value("conditionField").toString(), input.value("conditionOperator").toString("Always"),
								input.value("conditionValue").toString(), std::clamp(input.value("delayMs").toInt(), 0, 120000), true});
							refreshRules();
							saveRules();
						}
						respond(socket, valid ? 201 : 400, QJsonObject{{"ok", valid}, {"count", int(rules.size())}});
					}
					else if (method == "POST" && path == "/api/v1/stream/start") { obs_frontend_streaming_start(); respond(socket, 200, QJsonObject{{"ok", true}}); }
					else if (method == "POST" && path == "/api/v1/stream/stop") { obs_frontend_streaming_stop(); respond(socket, 200, QJsonObject{{"ok", true}}); }
					else if (method == "POST" && path == "/api/v1/recording/start") { obs_frontend_recording_start(); respond(socket, 200, QJsonObject{{"ok", true}}); }
					else if (method == "POST" && path == "/api/v1/recording/stop") { obs_frontend_recording_stop(); respond(socket, 200, QJsonObject{{"ok", true}}); }
					else if (method == "POST" && path == "/api/v1/app/exit") {
						respond(socket, 200, QJsonObject{{"ok", true}, {"closing", true}});
						QTimer::singleShot(0, this, [] {
							if (QWidget *window = static_cast<QWidget *>(obs_frontend_get_main_window()))
								window->close();
						});
					}
					else if (method == "POST" && path == "/api/v1/event") {
						publishEvent(query.queryItemValue("platform", QUrl::FullyDecoded), query.queryItemValue("type", QUrl::FullyDecoded), QString::fromUtf8(body.isEmpty() ? QByteArray("{}") : body));
						respond(socket, 202, QJsonObject{{"accepted", true}});
					} else
						respond(socket, 404, QJsonObject{{"error", "not found"}});
				});
			}
		});
	}

	static void respond(QTcpSocket *socket, int statusCode, const QJsonObject &payload)
	{
		const QByteArray body = QJsonDocument(payload).toJson(QJsonDocument::Compact);
		const QByteArray reason = statusCode < 300 ? "OK" : (statusCode == 401 ? "Unauthorized" : (statusCode == 404 ? "Not Found" : "Bad Request"));
		const QByteArray response = "HTTP/1.1 " + QByteArray::number(statusCode) + " " + reason + "\r\nContent-Type: application/json\r\nConnection: close\r\nContent-Length: " + QByteArray::number(body.size()) + "\r\n\r\n" + body;
		socket->write(response);
		socket->disconnectFromHost();
	}
};

QPointer<PulseWeaverDock> dock;
bool fallbackDockRegistered = false;

void registerModuleActionProc(void *, calldata_t *data)
{
	ModuleAction action{QString::fromUtf8(calldata_string(data, "module_id")),
			    QString::fromUtf8(calldata_string(data, "module_name")),
			    QString::fromUtf8(calldata_string(data, "action_id")),
			    QString::fromUtf8(calldata_string(data, "action_name")),
			    QString::fromUtf8(calldata_string(data, "proc_name"))};
	if (!dock || action.moduleId.isEmpty() || action.actionId.isEmpty() || action.procName.isEmpty())
		return;
	QMetaObject::invokeMethod(dock.data(), [action] {
		if (dock)
			dock->registerModuleAction(action);
	}, Qt::QueuedConnection);
}

void frontendEvent(obs_frontend_event event, void *)
{
	if (!dock)
		return;
	QMetaObject::invokeMethod(dock.data(), [event] {
		if (dock)
			dock->onFrontendEvent(event);
	}, Qt::QueuedConnection);
}

void publishProc(void *, calldata_t *data)
{
	const QString platform = QString::fromUtf8(calldata_string(data, "platform"));
	const QString type = QString::fromUtf8(calldata_string(data, "type"));
	const QString json = QString::fromUtf8(calldata_string(data, "json"));
	if (!dock)
		return;
	QMetaObject::invokeMethod(dock.data(), [platform, type, json] {
		if (dock)
			dock->publishEvent(platform, type, json.isEmpty() ? "{}" : json);
	}, Qt::QueuedConnection);
}

} // namespace

bool obs_module_load(void)
{
	blog(LOG_INFO, "[Pulse Weaver] Loading native in-process core; OBS WebSocket is not used.");
	signal_handler_add(obs_get_signal_handler(), "void pulseweaver_event(string platform, string type, string json)");
	proc_handler_add(obs_get_proc_handler(), "void pulseweaver_publish(in string platform, in string type, in string json)", publishProc, nullptr);
	proc_handler_add(obs_get_proc_handler(),
			 "void pulseweaver_register_action(in string module_id, in string module_name, in string action_id, in string action_name, in string proc_name)",
			 registerModuleActionProc, nullptr);

	QWidget *mainWindow = static_cast<QWidget *>(obs_frontend_get_main_window());
	QWidget *actionMount = mainWindow ? mainWindow->findChild<QWidget *>("PulseWeaverActionPluginMount") : nullptr;
	QWidget *lightsMount = mainWindow ? mainWindow->findChild<QWidget *>("PulseWeaverLightsPluginMount") : nullptr;
	dock = new PulseWeaverDock(actionMount ? actionMount : mainWindow);
	if (actionMount && actionMount->layout()) {
		actionMount->layout()->setContentsMargins(0, 0, 0, 0);
		actionMount->layout()->addWidget(dock);
		dock->mountLights(lightsMount);
		blog(LOG_INFO, "[Pulse Weaver] Mounted Lights and Action directly into the native product shell.");
	} else {
		fallbackDockRegistered = obs_frontend_add_dock_by_id("pulseWeaverNativeCore", "Pulse Weaver", dock);
		if (!fallbackDockRegistered) {
			delete dock;
			dock = nullptr;
			return false;
		}
		blog(LOG_WARNING, "[Pulse Weaver] Native shell mount was unavailable; using compatibility dock.");
	}
	obs_frontend_add_event_callback(frontendEvent, nullptr);
	return true;
}

void obs_module_unload(void)
{
	obs_frontend_remove_event_callback(frontendEvent, nullptr);
	if (fallbackDockRegistered)
		obs_frontend_remove_dock("pulseWeaverNativeCore");
	else if (dock)
		delete dock.data();
	dock = nullptr;
	fallbackDockRegistered = false;
}
