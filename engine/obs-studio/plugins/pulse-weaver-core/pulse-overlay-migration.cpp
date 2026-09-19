#include "pulse-overlay-migration.hpp"
#include <obs.h>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDateTime>
#include <QSaveFile>
#include <QUrl>
#include <QUrlQuery>
#include <utility>

namespace PulseOverlay {
int migrateManagedSources(const QString &directory, quint16 port, const QString &liveCapability,
			  const QSet<QString> &publishedIds, const std::function<void(const QString &)> &status)
{
	if (port == 0 || liveCapability.isEmpty())
		return -1;
	struct Migration {
		QString uuid;
		QString name;
		QString originalUrl;
		QString newUrl;
		QString originalSettings;
	};
	QList<Migration> migrations;
	struct Context { quint16 port; const QString &capability; const QSet<QString> &ids; QList<Migration> &migrations; };
	Context migrationContext{port, liveCapability, publishedIds, migrations};
	obs_enum_sources([](void *opaque, obs_source_t *source) {
		auto *context = static_cast<Context *>(opaque);
		if (QString::fromUtf8(obs_source_get_unversioned_id(source)) != "browser_source")
			return true;
		obs_data_t *settings = obs_source_get_settings(source);
		const QString original = QString::fromUtf8(obs_data_get_string(settings, "url"));
		const QUrl url(original);
		const QStringList parts = url.path().split('/', Qt::SkipEmptyParts);
		const bool managed = url.scheme() == "http" && url.host() == "127.0.0.1" && url.port() == context->port &&
			parts.size() == 2 && parts[0] == "overlay" && context->ids.contains(parts[1]);
		if (managed && QUrlQuery(url).queryItemValue("token").isEmpty() && !url.fragment().startsWith("token=")) {
			QUrl updated(url);
			updated.setFragment("token=" + context->capability);
			const char *json = obs_data_get_json(settings);
			context->migrations.append({QString::fromUtf8(obs_source_get_uuid(source)),
				QString::fromUtf8(obs_source_get_name(source)), original, updated.toString(),
				json ? QString::fromUtf8(json) : QString()});
		}
		obs_data_release(settings);
		return true;
	}, &migrationContext);
	if (migrations.isEmpty())
		return 0;

	const QString journalPath = QDir(directory).filePath("managed-source-migration.json");
	QJsonArray journalEntries;
	QSet<QString> journaledUuids;
	QFile existingJournal(journalPath);
	if (existingJournal.exists()) {
		if (!existingJournal.open(QIODevice::ReadOnly)) {
			if (status)
				status("Existing overlay source migration journal is unreadable; no sources were changed.");
			return -1;
		}
		QJsonParseError parseError{};
		const QJsonDocument parsed = QJsonDocument::fromJson(existingJournal.readAll(), &parseError);
		existingJournal.close(); // Windows cannot atomically replace an open journal.
		if (parseError.error != QJsonParseError::NoError || !parsed.isObject() ||
		    parsed.object().value("version").toInt() != 1) {
			if (status)
				status("Existing overlay source migration journal is invalid; no sources were changed.");
			return -1;
		}
		journalEntries = parsed.object().value("sources").toArray();
		for (const QJsonValue &value : journalEntries)
			journaledUuids.insert(value.toObject().value("sourceUuid").toString());
	}
	for (const Migration &migration : std::as_const(migrations)) {
		if (journaledUuids.contains(migration.uuid))
			continue;
		QUrl redacted(migration.newUrl);
		redacted.setFragment("capability:live-render.capability");
		journalEntries.append(QJsonObject{{"sourceUuid", migration.uuid}, {"sourceName", migration.name},
			{"originalUrl", migration.originalUrl}, {"newUrl", redacted.toString()},
			{"originalSettings", migration.originalSettings}});
	}
	QSaveFile journal(journalPath);
	const QByteArray journalData = QJsonDocument(QJsonObject{{"version", 1},
		{"createdUtc", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
		{"sources", journalEntries}}).toJson(QJsonDocument::Indented);
	if (!journal.open(QIODevice::WriteOnly) || journal.write(journalData) != journalData.size() || !journal.commit()) {
		if (status)
			status("Managed overlay source migration journal could not be saved; no sources were changed.");
		return -1;
	}
	for (const Migration &migration : std::as_const(migrations)) {
		obs_source_t *source = obs_get_source_by_uuid(migration.uuid.toUtf8().constData());
		if (!source)
			continue;
		obs_data_t *settings = obs_source_get_settings(source);
		obs_data_set_string(settings, "url", migration.newUrl.toUtf8().constData());
		obs_data_set_int(settings, "webpage_control_level", 0);
		obs_source_update(source, settings);
		obs_data_release(settings);
		obs_source_release(source);
	}
	if (status)
		status(QString("Secured %1 existing managed overlay source(s); identities and transforms were preserved.").arg(migrations.size()));
	return migrations.size();
}

} // namespace PulseOverlay
