#pragma once

#include <QByteArray>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>

#include <functional>

namespace PulseOverlay {

// Apply edits made through a possibly clamped/rounded UI projection without
// replacing untouched authoritative values, optional fields or unknown data.
QJsonObject mergeEditedDocument(const QJsonObject &raw, const QJsonObject &baseline, const QJsonObject &edited);

enum class StoreState {
	Ready,
	Created,
	Migrated,
	RecoveryRequired,
	ReadOnlyFuture,
};

struct StoreResult {
	StoreState state = StoreState::RecoveryRequired;
	QString message;

	bool ok() const
	{
		return state == StoreState::Ready || state == StoreState::Created || state == StoreState::Migrated;
	}
};

class Store final {
public:
	using Committer = std::function<bool(const QString &, const QByteArray &, QString *)>;

	explicit Store(QString directory, Committer committer = {});

	StoreResult load();
	StoreResult initialize(const QJsonArray &documents);
	StoreResult syncDrafts(const QJsonArray &documents);
	StoreResult publish(const QString &projectId);

	QJsonArray draftDocuments() const;
	QJsonArray publishedDocuments() const;
	bool isDraftPublished(const QString &projectId) const;
	bool writable() const { return !readOnly; }
	QString v1Path() const;
	QString v2Path() const;
	QJsonObject snapshot() const { return root; }

	static bool atomicWrite(const QString &path, const QByteArray &data, QString *error);

private:
	QString directory;
	Committer commit;
	QJsonObject root;
	bool readOnly = false;

	StoreResult commitRoot(const QJsonObject &next, StoreState successState, const QString &message = {});
	StoreResult migrate(const QByteArray &sourceBytes, const QJsonObject &source);
};

} // namespace PulseOverlay
