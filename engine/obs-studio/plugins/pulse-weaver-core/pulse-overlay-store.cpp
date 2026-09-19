#include "pulse-overlay-store.hpp"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QHash>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QSaveFile>
#include <QSet>

#include <cmath>

namespace PulseOverlay {

QJsonObject mergeEditedDocument(const QJsonObject &raw, const QJsonObject &baseline, const QJsonObject &edited)
{
	QJsonObject result = raw;
	for (auto it = edited.begin(); it != edited.end(); ++it) {
		if (it.key() != "elements" && it.value() != baseline.value(it.key()))
			result.insert(it.key(), it.value());
	}
	QHash<QString, QJsonObject> originals, projections;
	for (const QJsonValue &value : raw.value("elements").toArray()) {
		const auto element = value.toObject();
		originals.insert(element.value("id").toString(), element);
	}
	for (const QJsonValue &value : baseline.value("elements").toArray()) {
		const auto element = value.toObject();
		projections.insert(element.value("id").toString(), element);
	}
	QJsonArray elements;
	for (const QJsonValue &value : edited.value("elements").toArray()) {
		const auto element = value.toObject();
		const QString id = element.value("id").toString();
		if (!originals.contains(id)) { elements.append(element); continue; }
		QJsonObject merged = originals.value(id);
		const QJsonObject before = projections.value(id);
		for (auto it = element.begin(); it != element.end(); ++it)
			if (it.value() != before.value(it.key())) merged.insert(it.key(), it.value());
		elements.append(merged);
	}
	if (edited.value("elements") != baseline.value("elements"))
		result.insert("elements", elements);
	return result;
}

namespace {

StoreResult failure(const QString &message, StoreState state = StoreState::RecoveryRequired)
{
	return {state, message};
}

QString stableId(const QByteArray &purpose, const QString &sourceId)
{
	QByteArray hex = QCryptographicHash::hash(purpose + sourceId.toUtf8(), QCryptographicHash::Sha256).toHex().left(32);
	return QString::fromLatin1(hex.left(8) + '-' + hex.mid(8, 4) + '-' + hex.mid(12, 4) + '-' +
				 hex.mid(16, 4) + '-' + hex.mid(20, 12));
}

bool finiteNumber(const QJsonObject &object, const QString &key, bool positive, QString *error)
{
	if (!object.contains(key))
		return true;
	const QJsonValue value = object.value(key);
	if (!value.isDouble() || !std::isfinite(value.toDouble()) || (positive && value.toDouble() <= 0.0)) {
		if (error)
			*error = QString("%1 must be a %2finite number.").arg(key, positive ? "positive " : "");
		return false;
	}
	return true;
}

bool validateDocument(const QJsonObject &document, QString *error)
{
	const QString id = document.value("id").toString();
	if (id.isEmpty()) {
		if (error)
			*error = "Overlay is missing an id.";
		return false;
	}
	if (!finiteNumber(document, "width", true, error) || !finiteNumber(document, "height", true, error) ||
	    !finiteNumber(document, "durationMs", true, error))
		return false;
	if (document.contains("elements") && !document.value("elements").isArray()) {
		if (error)
			*error = "Overlay elements must be an array.";
		return false;
	}
	QSet<QString> ids;
	for (const QJsonValue &value : document.value("elements").toArray()) {
		if (!value.isObject()) {
			if (error)
				*error = "Every overlay element must be an object.";
			return false;
		}
		const QJsonObject element = value.toObject();
		const QString elementId = element.value("id").toString();
		if (elementId.isEmpty() || ids.contains(elementId)) {
			if (error)
				*error = elementId.isEmpty() ? "Overlay element is missing an id."
								     : "Overlay contains duplicate element id " + elementId + '.';
			return false;
		}
		ids.insert(elementId);
		for (const QString &key : {QString("x"), QString("y"), QString("rotation"), QString("opacity")})
			if (!finiteNumber(element, key, false, error))
				return false;
		for (const QString &key : {QString("width"), QString("height")})
			if (!finiteNumber(element, key, true, error))
				return false;
	}
	return true;
}

bool validateDocuments(const QJsonArray &documents, QString *error)
{
	QSet<QString> ids;
	for (const QJsonValue &value : documents) {
		if (!value.isObject()) {
			if (error)
				*error = "Every overlay must be an object.";
			return false;
		}
		const QJsonObject document = value.toObject();
		if (!validateDocument(document, error))
			return false;
		const QString id = document.value("id").toString();
		if (ids.contains(id)) {
			if (error)
				*error = "Overlay store contains duplicate id " + id + '.';
			return false;
		}
		ids.insert(id);
	}
	return true;
}

QJsonObject revisionObject(const QJsonObject &document, const QString &revisionId, qint64 number)
{
	const QString projectId = document.value("id").toString();
	const int width = document.value("width").toInt(1920);
	const int height = document.value("height").toInt(1080);
	return {
		{"id", revisionId},
		{"number", number},
		{"rendererProfile", "legacy-v1"},
		{"document", document},
		{"layouts", QJsonArray{QJsonObject{{"id", stableId("layout:", projectId)},
								 {"role", "legacy"}, {"width", width}, {"height", height}}}},
	};
}

QJsonObject initialProject(const QJsonObject &document)
{
	const QString id = document.value("id").toString();
	const QString revisionId = stableId("revision:1:", id);
	const QJsonObject revision = revisionObject(document, revisionId, 1);
	return {
		{"id", id},
		{"name", document.value("name").toString("Untitled Overlay")},
		{"draftRevision", 1},
		{"publishedRevision", revisionId},
		{"draft", revision},
		{"revisions", QJsonArray{revision}},
		{"legacy", document},
	};
}

QJsonObject mergeElement(const QJsonObject &original, const QJsonObject &known)
{
	QJsonObject merged = original;
	for (auto it = known.begin(); it != known.end(); ++it)
		merged.insert(it.key(), it.value());
	return merged;
}

QJsonObject mergeDocument(const QJsonObject &original, const QJsonObject &known)
{
	QJsonObject merged = original;
	for (auto it = known.begin(); it != known.end(); ++it) {
		if (it.key() != "elements")
			merged.insert(it.key(), it.value());
	}
	QHash<QString, QJsonObject> originals;
	for (const QJsonValue &value : original.value("elements").toArray()) {
		const QJsonObject element = value.toObject();
		originals.insert(element.value("id").toString(), element);
	}
	QJsonArray elements;
	for (const QJsonValue &value : known.value("elements").toArray()) {
		const QJsonObject element = value.toObject();
		elements.append(mergeElement(originals.value(element.value("id").toString()), element));
	}
	merged.insert("elements", elements);
	return merged;
}

bool validateV2(const QJsonObject &candidate, QString *error)
{
	if (candidate.value("schema").toInt(-1) != 2) {
		if (error)
			*error = "Overlay store is not schema 2.";
		return false;
	}
	if (!candidate.value("projects").isArray()) {
		if (error)
			*error = "Overlay projects must be an array.";
		return false;
	}
	QSet<QString> projectIds;
	for (const QJsonValue &value : candidate.value("projects").toArray()) {
		const QJsonObject project = value.toObject();
		const QString id = project.value("id").toString();
		if (id.isEmpty() || projectIds.contains(id)) {
			if (error)
				*error = id.isEmpty() ? "Overlay project is missing an id."
							 : "Overlay store contains duplicate project id " + id + '.';
			return false;
		}
		projectIds.insert(id);
		const QJsonObject draft = project.value("draft").toObject();
		const QJsonObject draftDocument = draft.value("document").toObject();
		if (!validateDocument(draftDocument, error))
			return false;
		if (draftDocument.value("id").toString() != id) {
			if (error)
				*error = "Overlay project and draft ids do not match.";
			return false;
		}
		const QString published = project.value("publishedRevision").toString();
		bool foundPublished = false;
		QSet<QString> revisionIds;
		for (const QJsonValue &revisionValue : project.value("revisions").toArray()) {
			const QJsonObject revision = revisionValue.toObject();
			const QString revisionId = revision.value("id").toString();
			const QJsonObject revisionDocument = revision.value("document").toObject();
			if (revisionId.isEmpty() || revisionIds.contains(revisionId)) {
				if (error)
					*error = "Overlay project contains a missing or duplicate revision id.";
				return false;
			}
			revisionIds.insert(revisionId);
			if (!validateDocument(revisionDocument, error))
				return false;
			if (revisionDocument.value("id").toString() != id) {
				if (error)
					*error = "Overlay project and revision document ids do not match.";
				return false;
			}
			if (revisionId == published)
				foundPublished = true;
		}
		if (!foundPublished) {
			if (error)
				*error = "Overlay project " + id + " references a missing published revision.";
			return false;
		}
	}
	return true;
}

QJsonObject parseObject(const QByteArray &bytes, QString *error)
{
	QJsonParseError parseError{};
	const QJsonDocument parsed = QJsonDocument::fromJson(bytes, &parseError);
	if (parseError.error != QJsonParseError::NoError || !parsed.isObject()) {
		if (error)
			*error = parseError.error != QJsonParseError::NoError ? parseError.errorString()
											 : QStringLiteral("JSON root is not an object.");
		return {};
	}
	return parsed.object();
}

} // namespace

Store::Store(QString directory_, Committer committer)
	: directory(std::move(directory_)), commit(std::move(committer))
{
	if (!commit)
		commit = atomicWrite;
}

QString Store::v1Path() const
{
	return QDir(directory).filePath("overlays.json");
}

QString Store::v2Path() const
{
	return QDir(directory).filePath("overlays.v2.json");
}

bool Store::atomicWrite(const QString &path, const QByteArray &data, QString *error)
{
	QSaveFile file(path);
	if (!file.open(QIODevice::WriteOnly)) {
		if (error)
			*error = file.errorString();
		return false;
	}
	if (file.write(data) != data.size()) {
		if (error)
			*error = file.errorString().isEmpty() ? QStringLiteral("Incomplete write.") : file.errorString();
		file.cancelWriting();
		return false;
	}
	if (!file.commit()) {
		if (error)
			*error = file.errorString();
		return false;
	}
	return true;
}

StoreResult Store::load()
{
	readOnly = false;
	root = {};
	QFile v2(v2Path());
	if (v2.exists()) {
		if (!v2.open(QIODevice::ReadOnly))
			return failure("Cannot read overlays.v2.json: " + v2.errorString());
		QString error;
		const QJsonObject candidate = parseObject(v2.readAll(), &error);
		if (candidate.isEmpty())
			return failure("Invalid overlays.v2.json: " + error);
		const int schema = candidate.value("schema").toInt(-1);
		if (schema > 2) {
			root = candidate;
			readOnly = true;
			return failure(QString("Overlay schema %1 is newer than this build; opened read-only.").arg(schema),
				       StoreState::ReadOnlyFuture);
		}
		if (!validateV2(candidate, &error))
			return failure("Invalid overlays.v2.json: " + error);
		root = candidate;
		return {StoreState::Ready, {}};
	}

	QFile v1(v1Path());
	if (!v1.exists())
		return {StoreState::Created, "No overlay store exists yet."};
	if (!v1.open(QIODevice::ReadOnly))
		return failure("Cannot read overlays.json: " + v1.errorString());
	const QByteArray bytes = v1.readAll();
	QString error;
	const QJsonObject source = parseObject(bytes, &error);
	if (source.isEmpty())
		return failure("Invalid overlays.json; original file was preserved: " + error);
	if (source.value("schema").toInt(-1) != 1 || !source.value("overlays").isArray())
		return failure("Unsupported overlays.json structure; original file was preserved.");
	if (!validateDocuments(source.value("overlays").toArray(), &error))
		return failure("Cannot migrate overlays.json; original file was preserved: " + error);
	return migrate(bytes, source);
}

StoreResult Store::migrate(const QByteArray &sourceBytes, const QJsonObject &source)
{
	const QByteArray digest = QCryptographicHash::hash(sourceBytes, QCryptographicHash::Sha256).toHex();
	QJsonArray projects;
	for (const QJsonValue &value : source.value("overlays").toArray())
		projects.append(initialProject(value.toObject()));
	const QJsonObject next{{"schema", 2}, {"projects", projects}, {"instances", QJsonArray{}},
				       {"migration", QJsonObject{{"sourceSchema", 1}, {"sourceSha256", QString::fromLatin1(digest)}}}};

	const QString backupPath = QDir(directory).filePath("overlays.v1.backup." + QString::fromLatin1(digest.left(12)) + ".json");
	QFile backup(backupPath);
	if (backup.exists()) {
		if (!backup.open(QIODevice::ReadOnly) || backup.readAll() != sourceBytes)
			return failure("Existing overlay migration backup does not match overlays.json; migration stopped.");
	} else {
		QString error;
		if (!commit(backupPath, sourceBytes, &error))
			return failure("Could not create overlay migration backup: " + error);
	}
	return commitRoot(next, StoreState::Migrated, "Overlay store migrated to schema 2.");
}

StoreResult Store::initialize(const QJsonArray &documents)
{
	if (QFile::exists(v1Path()) || QFile::exists(v2Path()))
		return failure("Overlay initialization refused because a store already exists.");
	QString error;
	if (!validateDocuments(documents, &error))
		return failure("Cannot initialize overlays: " + error);
	QJsonArray projects;
	for (const QJsonValue &value : documents)
		projects.append(initialProject(value.toObject()));
	return commitRoot(QJsonObject{{"schema", 2}, {"projects", projects}, {"instances", QJsonArray{}},
						      {"migration", QJsonObject{{"sourceSchema", 0}}}},
			  StoreState::Created, "Overlay store initialized as schema 2.");
}

StoreResult Store::commitRoot(const QJsonObject &next, StoreState successState, const QString &message)
{
	if (readOnly)
		return failure("Overlay store is read-only because it was created by a newer build.", StoreState::ReadOnlyFuture);
	QString error;
	if (!validateV2(next, &error))
		return failure("Overlay transaction validation failed: " + error);
	const QByteArray bytes = QJsonDocument(next).toJson(QJsonDocument::Indented);
	if (!commit(v2Path(), bytes, &error))
		return failure("Overlay transaction failed: " + error);
	QFile verify(v2Path());
	if (!verify.open(QIODevice::ReadOnly))
		return failure("Overlay transaction could not be verified: " + verify.errorString());
	const QJsonObject written = parseObject(verify.readAll(), &error);
	if (written.isEmpty() || !validateV2(written, &error) || written != next)
		return failure("Overlay transaction verification failed: " + error);
	root = next;
	return {successState, message};
}

QJsonArray Store::draftDocuments() const
{
	QJsonArray documents;
	for (const QJsonValue &value : root.value("projects").toArray())
		documents.append(value.toObject().value("draft").toObject().value("document"));
	return documents;
}

QJsonArray Store::publishedDocuments() const
{
	QJsonArray documents;
	for (const QJsonValue &value : root.value("projects").toArray()) {
		const QJsonObject project = value.toObject();
		const QString published = project.value("publishedRevision").toString();
		for (const QJsonValue &revisionValue : project.value("revisions").toArray()) {
			const QJsonObject revision = revisionValue.toObject();
			if (revision.value("id").toString() == published) {
				documents.append(revision.value("document"));
				break;
			}
		}
	}
	return documents;
}

bool Store::isDraftPublished(const QString &projectId) const
{
	for (const QJsonValue &value : root.value("projects").toArray()) {
		const QJsonObject project = value.toObject();
		if (project.value("id").toString() != projectId)
			continue;
		const QString published = project.value("publishedRevision").toString();
		for (const QJsonValue &revisionValue : project.value("revisions").toArray()) {
			const QJsonObject revision = revisionValue.toObject();
			if (revision.value("id").toString() == published)
				return revision.value("document") == project.value("draft").toObject().value("document");
		}
	}
	return false;
}

StoreResult Store::syncDrafts(const QJsonArray &documents)
{
	QString error;
	if (!validateDocuments(documents, &error))
		return failure("Cannot save overlay drafts: " + error);
	QHash<QString, QJsonObject> existing;
	for (const QJsonValue &value : root.value("projects").toArray()) {
		const QJsonObject project = value.toObject();
		existing.insert(project.value("id").toString(), project);
	}
	QJsonArray projects;
	for (const QJsonValue &value : documents) {
		const QJsonObject known = value.toObject();
		const QString id = known.value("id").toString();
		if (!existing.contains(id)) {
			projects.append(initialProject(known));
			continue;
		}
		QJsonObject project = existing.value(id);
		QJsonObject draft = project.value("draft").toObject();
		const QJsonObject merged = mergeDocument(draft.value("document").toObject(), known);
		if (merged != draft.value("document").toObject()) {
			const qint64 number = project.value("draftRevision").toInteger(1) + 1;
			project.insert("draftRevision", number);
			draft.insert("number", number);
			draft.insert("id", stableId("draft:", id + ':' + QString::number(number)));
			draft.insert("document", merged);
			QJsonArray layouts = draft.value("layouts").toArray();
			if (!layouts.isEmpty()) {
				QJsonObject layout = layouts.first().toObject();
				layout.insert("width", merged.value("width").toInt(1920));
				layout.insert("height", merged.value("height").toInt(1080));
				layouts.replace(0, layout);
				draft.insert("layouts", layouts);
			}
			project.insert("draft", draft);
			project.insert("name", merged.value("name").toString("Untitled Overlay"));
		}
		projects.append(project);
	}
	QJsonObject next = root;
	next.insert("projects", projects);
	return commitRoot(next, StoreState::Ready);
}

StoreResult Store::publish(const QString &projectId)
{
	QJsonObject next = root;
	QJsonArray projects = next.value("projects").toArray();
	bool found = false;
	for (qsizetype index = 0; index < projects.size(); ++index) {
		QJsonObject project = projects.at(index).toObject();
		if (project.value("id").toString() != projectId)
			continue;
		found = true;
		const QJsonObject draftDocument = project.value("draft").toObject().value("document").toObject();
		if (isDraftPublished(projectId))
			return {StoreState::Ready, "Overlay is already published."};
		QJsonArray revisions = project.value("revisions").toArray();
		const qint64 number = project.value("draftRevision").toInteger(1);
		const QString revisionId = stableId("published:", projectId + ':' + QString::number(number));
		const QJsonObject revision = revisionObject(draftDocument, revisionId, number);
		revisions.append(revision);
		project.insert("revisions", revisions);
		project.insert("publishedRevision", revisionId);
		projects.replace(index, project);
		break;
	}
	if (!found)
		return failure("Overlay project was not found.");
	next.insert("projects", projects);
	return commitRoot(next, StoreState::Ready, "Overlay published.");
}

} // namespace PulseOverlay
