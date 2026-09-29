#pragma once

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSet>
#include <functional>

namespace PulseStageStorage {
using Resolver = std::function<QString(const QString &, const QString &)>;

// OBS source names are C strings, so this can never match an actual source.
inline QString missingName(const QString &uuid)
{
	return QString(QChar(0)) + "pulse-missing-source:" + uuid;
}

inline QJsonObject normalizeAssignment(QJsonObject assignment, const Resolver &uuidForName,
				      const Resolver &nameForUuid, bool forStorage = false)
{
	const QString canvas = assignment.value("canvas").toString();
	const QJsonArray previous = assignment.value("excludedIdentities").toArray();
	QJsonArray excluded, identities;
	QSet<QString> seen;
	for (const auto &value : assignment.value("excluded").toArray()) {
		if (!value.isString() || value.toString().isEmpty()) continue;
		QString name = value.toString(), uuid;
		for (const auto &entry : previous) {
			const auto known = entry.toObject();
			const QString knownUuid = known.value("uuid").toString();
			if (!knownUuid.isEmpty() && (known.value("name").toString() == name || missingName(knownUuid) == name)) {
				uuid = knownUuid;
				name = known.value("name").toString();
				break;
			}
		}
		if (uuid.isEmpty()) uuid = uuidForName(name, canvas);
		if (!uuid.isEmpty()) {
			const QString current = nameForUuid(uuid, canvas);
			if (!current.isEmpty()) name = current;
			if (seen.contains(uuid)) continue;
			seen.insert(uuid);
			identities.append(QJsonObject{{"name", name}, {"uuid", uuid}});
			excluded.append(current.isEmpty() && !forStorage ? missingName(uuid) : name);
		} else {
			if (seen.contains(name)) continue;
			seen.insert(name);
			excluded.append(name); // Preserve unresolved legacy names until the source is available.
		}
	}
	assignment.insert("excluded", excluded);
	if (!identities.isEmpty() || assignment.contains("excludedIdentities")) assignment.insert("excludedIdentities", identities);
	return assignment;
}

inline QJsonArray normalize(QJsonArray stages, const Resolver &uuidForName, const Resolver &nameForUuid,
			   bool forStorage = false)
{
	for (qsizetype i = 0; i < stages.size(); ++i) {
		auto stage = stages[i].toObject();
		auto assignments = stage.value("assignments").toObject();
		for (const auto &key : assignments.keys())
			assignments.insert(key, normalizeAssignment(assignments.value(key).toObject(), uuidForName, nameForUuid, forStorage));
		if (stage.contains("assignments")) stage.insert("assignments", assignments);
		stages[i] = stage;
	}
	return stages;
}

inline bool write(const QString &path, const QJsonArray &stages, QString &error)
{
	// Do not overwrite an unreadable or malformed existing show with an empty/default list.
	if (QFileInfo::exists(path)) {
		QFile previous(path);
		if (!previous.open(QIODevice::ReadOnly) || !QJsonDocument::fromJson(previous.readAll()).isArray()) {
			error = "The existing Stage file could not be read. It has been preserved for recovery.";
			return false;
		}
	}
	if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
		error = "The Stage settings folder could not be created.";
		return false;
	}
	QSaveFile file(path);
	const QByteArray bytes = QJsonDocument(stages).toJson(QJsonDocument::Indented);
	if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
		error = "Stage changes could not be saved: " + file.errorString();
		file.cancelWriting();
		return false;
	}
	error.clear();
	return true;
}
} // namespace PulseStageStorage
