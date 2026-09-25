#pragma once

#include "PulseUpdateRelease.hpp"
#include <QAction>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QMenu>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPointer>
#include <QProcess>
#include <QProgressDialog>
#include <QSaveFile>
#include <QSettings>
#include <QTemporaryDir>
#include <QTimer>
#include <functional>
#include <memory>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace PulseUpdates {
inline bool startUpdateInstaller(const QString &path, const QStringList &arguments = {})
{
	QProcess installer;
	installer.setProgram(path);
	installer.setArguments(arguments);
#ifdef Q_OS_WIN
	installer.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *args) {
		args->flags |= CREATE_BREAKAWAY_FROM_JOB;
		args->inheritHandles = false; // Do not keep the app's log open inside setup.
	});
#endif
	return installer.startDetached();
}

// Deliberately independent of the upstream OBS/Sparkle updater and OBS version.
class Updater : public QObject {
	QWidget *window;
	std::function<bool()> outputsActive;
	Identity installed;
	QNetworkAccessManager network{this};
	std::unique_ptr<QSettings> settings;
	QJsonArray releases;
	bool checking = false;
	bool manual = false;
	std::optional<Release> pending;
	QPointer<QNetworkReply> download;
	QPointer<QProgressDialog> progress;
	std::unique_ptr<QTemporaryDir> directory;
	std::unique_ptr<QSaveFile> file;
	QCryptographicHash hash{QCryptographicHash::Sha256};
	qint64 received = 0;
	bool writeFailed = false;
	bool cancelled = false;

	void message(const QString &text)
	{
		QMessageBox::information(window, "Pulse Weaver updates", text);
	}
	void checkFailed()
	{
		checking = false;
		if (manual)
			message("GitHub could not be checked. Check your internet connection and try again later. "
				"GitHub may temporarily limit update requests.");
	}
	void fetchPage(int page)
	{
		QNetworkRequest request(QUrl(api + "?per_page=100&page=" + QString::number(page)));
		request.setRawHeader("Accept", "application/vnd.github+json");
		request.setRawHeader("X-GitHub-Api-Version", "2022-11-28");
		request.setRawHeader("User-Agent", "PulseWeaver-Updater");
		request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
		request.setTransferTimeout(30000);
		auto *reply = network.get(request);
		connect(reply, &QIODevice::readyRead, this, [reply] {
			if (reply->bytesAvailable() > 4 * 1024 * 1024)
				reply->abort();
		});
		connect(reply, &QNetworkReply::finished, this, [this, reply, page] {
			reply->deleteLater();
			if (reply->error() != QNetworkReply::NoError ||
			    reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() != 200) {
				checkFailed();
				return;
			}
			const auto document = QJsonDocument::fromJson(reply->readAll());
			if (!document.isArray()) {
				checkFailed();
				return;
			}
			const auto items = document.array();
			for (const auto &item : items)
				releases.append(item);
			if (items.size() == 100) {
				if (page >= 10)
					checkFailed();
				else
					fetchPage(page + 1);
				return;
			}
			checking = false;
			settings->setValue("lastCheck", QDateTime::currentSecsSinceEpoch());
			settings->sync();
			if (!manual && !settings->value("automatic", true).toBool())
				return;
			pending = selectRelease(releases, installed, settings->value("includeAlpha", false).toBool());
			if (pending)
				offer();
			else if (manual)
				message("No newer downloadable update was found for this installation's channel (" +
					installed.channel + "). Installed: " + installed.tag +
					". Only published packages with a SHA-256 digest are eligible.");
		});
	}
	void offer()
	{
		if (!pending || download)
			return;
		if (outputsActive()) {
			if (manual) {
				message("An update is available. Stop streaming, recording, replay buffer and virtual camera, "
					"then check for updates again to install it.");
				manual = false;
			}
			return; // The idle timer offers it when all outputs have stopped.
		}
		const Release candidate = *pending;
		pending.reset();
		if (QMessageBox::question(window, "Pulse Weaver update available",
			"Installed: " + installed.tag + "\nAvailable: " + candidate.identity.tag +
			(candidate.identity.channel == "windows-alpha" ?
			 "\n\nThis is an experimental alpha. Setup will verify a full backup before upgrading your existing installation. "
			 "Your credentials, scenes and Lumia connection stay in place. Use Updates → Restore a backup to return to your previous version.\n\nDownload the alpha?" :
			 "\n\nDownload this update from GitHub? Setup backs up your app and settings before updating."),
			QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
			return;
		directory = std::make_unique<QTemporaryDir>(QDir::tempPath() + "/PulseWeaver-update-XXXXXX");
		file = std::make_unique<QSaveFile>(directory->path() + "/" + candidate.name);
		if (!directory->isValid() || !file->open(QIODevice::WriteOnly)) {
			message("The update download folder could not be created. Check your available disk space.");
			return;
		}
		hash.reset();
		received = 0;
		writeFailed = cancelled = false;
		progress = new QProgressDialog("Downloading " + candidate.identity.tag + " from GitHub…", "Cancel", 0, 100, window);
		progress->setWindowTitle("Pulse Weaver update");
		progress->setWindowModality(Qt::NonModal);
		progress->setAutoClose(false);
		progress->setAutoReset(false);
		connect(progress, &QProgressDialog::canceled, this, [this] {
			cancelled = true;
			if (download)
				download->abort();
		});
		progress->show();
		getAsset(candidate, candidate.download, 0);
	}
	void consume(QNetworkReply *reply, const Release &candidate)
	{
		if (reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() != 200) {
			reply->readAll(); // Drain redirect/error bodies so the bounded reply buffer cannot stall.
			return;
		}
		while (reply->bytesAvailable() > 0 && !writeFailed) {
			const auto bytes = reply->read(256 * 1024);
			if (bytes.isEmpty())
				break;
			received += bytes.size();
			if (received > candidate.size || file->write(bytes) != bytes.size()) {
				writeFailed = true;
				reply->abort();
				break;
			}
			hash.addData(bytes);
		}
		if (progress)
			progress->setValue(int(qMin<qint64>(99, received * 100 / candidate.size)));
	}
	void getAsset(const Release &candidate, const QUrl &url, int redirects)
	{
		QNetworkRequest request(url);
		request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
		request.setRawHeader("User-Agent", "PulseWeaver-Updater");
		request.setTransferTimeout(60000);
		auto *reply = network.get(request);
		download = reply;
		// Keep a download bounded in memory even for large installers.
		reply->setReadBufferSize(512 * 1024);
		connect(reply, &QIODevice::readyRead, this, [this, reply, candidate] { consume(reply, candidate); });
		connect(reply, &QNetworkReply::finished, this, [this, reply, candidate, redirects] {
			consume(reply, candidate);
			reply->deleteLater();
			const auto redirect = reply->attribute(QNetworkRequest::RedirectionTargetAttribute).toUrl();
			if (!cancelled && reply->error() == QNetworkReply::NoError && !redirect.isEmpty() && redirects < 5) {
				const auto next = reply->url().resolved(redirect);
				if (trustedDownloadUrl(next) && received == 0) {
					getAsset(candidate, next, redirects + 1);
					return;
				}
			}
			download.clear();
			if (progress) {
				progress->hide();
				progress->deleteLater();
			}
			if (cancelled || writeFailed || reply->error() != QNetworkReply::NoError ||
			    reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() != 200 ||
			    received != candidate.size || hash.result() != candidate.sha256 || !file->commit()) {
				file->cancelWriting();
				if (!cancelled)
					message("The update could not be downloaded and verified. Nothing was installed. Try again later.");
				file.reset();
				directory.reset();
				return;
			}
			const QString path = file->fileName();
			file.reset();
			if (outputsActive()) {
				message("The download was verified, but an output is now active. Nothing was installed. "
					"Stop your outputs and check for updates again when ready.");
				return;
			}
#ifdef Q_OS_MACOS
			const QString instruction = "Open the verified update disk image and close Pulse Weaver?\n\n"
				"Drag Pulse Weaver Mac Preview to Applications and replace the previous app. "
				"Your settings stay in your Library folder. Reopen the app after copying.";
#else
			const QString instruction = "Close Pulse Weaver and open the verified installer?\n\n"
				"Follow the setup window to update this installation. Your shows and connections will be preserved.";
#endif
			if (QMessageBox::question(window, "Update ready", instruction, QMessageBox::Yes | QMessageBox::No,
				QMessageBox::No) != QMessageBox::Yes)
				return;
			// Outputs can start while a modal confirmation is open.
			if (outputsActive()) {
				message("An output is active. Stop all outputs before installing this update.");
				return;
			}
#ifdef Q_OS_MACOS
			const bool started = QDesktopServices::openUrl(QUrl::fromLocalFile(path));
#else
			const bool started = startUpdateInstaller(path);
#endif
			if (!started) {
				message("The verified installer could not be opened. Nothing was installed.");
				return;
			}
			directory->setAutoRemove(false); // The installer/disk image still needs the download after this process exits.
			window->close();
		});
	}

public:
	Updater(QWidget *owner, QMenu *menu, std::function<bool()> active) :
		QObject(owner), window(owner), outputsActive(std::move(active))
	{
		QFile identityFile(QCoreApplication::applicationDirPath() + "/pulseweaver-update.json");
		if (identityFile.open(QIODevice::ReadOnly) && identityFile.size() < 4096) {
			const auto identity = QJsonDocument::fromJson(identityFile.readAll()).object();
			if (identity.value("schema").toInt() == 1)
				installed = {identity.value("channel").toString(), identity.value("tag").toString()};
		}
#ifdef Q_OS_MACOS
		if (installed.channel != "mac-arm64-preview")
			installed = {};
		const QString state = QDir::homePath() + "/Library/Application Support/Pulse Weaver Mac Preview/pulseweaver/updates.ini";
#else
		if (installed.channel != "windows-public" && installed.channel != "windows-private" && installed.channel != "windows-alpha")
			installed = {};
		const QString state = QDir(QCoreApplication::applicationDirPath()).absoluteFilePath("../../config/pulseweaver/updates.ini");
#endif
		settings = std::make_unique<QSettings>(state, QSettings::IniFormat);
		auto *updates = menu->addMenu("Updates");
		auto *check = updates->addAction("Check for updates…");
		connect(check, &QAction::triggered, this, [this] { checkNow(true); });
#ifdef Q_OS_WIN
		if (installed.channel == "windows-private" || installed.channel == "windows-alpha") {
			auto *alpha = updates->addAction("Include experimental alpha builds");
			alpha->setCheckable(true);
			alpha->setChecked(installed.channel == "windows-alpha" || settings->value("includeAlpha", false).toBool());
			alpha->setEnabled(installed.channel != "windows-alpha");
			connect(alpha, &QAction::toggled, this, [this](bool enabled) {
				settings->setValue("includeAlpha", enabled);
				settings->sync();
				pending.reset();
				if (enabled) checkNow(true);
			});
			auto *restore = updates->addAction("Restore a backup / leave alpha…");
			connect(restore, &QAction::triggered, this, [this] {
				if (outputsActive()) {
					message("Stop all outputs before restoring a backup.");
					return;
				}
				const QString maintenance = QDir(QCoreApplication::applicationDirPath()).absoluteFilePath("../../Uninstall Pulse Weaver.exe");
				if (!QFile::exists(maintenance)) {
					message("Open the release or alpha installer and choose Restore backup to recover your previous app and settings.");
					return;
				}
				if (QMessageBox::question(window, "Restore Pulse Weaver",
					"Close Pulse Weaver and open Setup & Recovery? Choose your pre-alpha backup there to restore the previous version and settings together.",
					QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes || outputsActive()) return;
				if (startUpdateInstaller(maintenance)) window->close();
				else message("Setup & Recovery could not be opened. Run your downloaded installer instead.");
			});
		}
#endif
		auto *automatic = updates->addAction("Check automatically each day");
		automatic->setCheckable(true);
		automatic->setChecked(settings->value("automatic", true).toBool());
		connect(automatic, &QAction::toggled, this, [this](bool enabled) {
			settings->setValue("automatic", enabled);
			settings->sync();
			if (!enabled)
				pending.reset();
		});
		auto *timer = new QTimer(this);
		connect(timer, &QTimer::timeout, this, [this] {
			if (pending)
				offer();
			else
				checkNow(false);
		});
		timer->start(60000);
		QTimer::singleShot(15000, this, [this] { checkNow(false); });
	}
	void checkNow(bool userRequested)
	{
		if (QCoreApplication::arguments().contains("--disable-updater"))
			return;
		if (checking || download)
			return;
		if (!version(installed)) {
			if (userRequested)
				message("This development or portable copy has no update channel. Install a packaged Pulse Weaver build to enable updates.");
			return;
		}
		const auto now = QDateTime::currentSecsSinceEpoch();
		const auto last = settings->value("lastCheck", 0).toLongLong();
		const auto attempt = settings->value("lastAttempt", 0).toLongLong();
		if (!userRequested && (!settings->value("automatic", true).toBool() ||
			(now >= last && now - last < 86400) || (now >= attempt && now - attempt < 3600)))
			return;
		settings->setValue("lastAttempt", now);
		manual = userRequested;
		checking = true;
		releases = {};
		fetchPage(1);
	}
};
} // namespace PulseUpdates
