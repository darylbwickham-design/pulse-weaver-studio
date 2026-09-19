#include "pulse-overlay-store.hpp"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>

#include <iostream>

namespace {

int failures = 0;

void expect(bool condition, const char *message)
{
	if (condition)
		return;
	std::cerr << "FAIL: " << message << '\n';
	++failures;
}

bool writeFile(const QString &path, const QByteArray &bytes)
{
	QFile file(path);
	return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}

QByteArray readFile(const QString &path)
{
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly))
		return {};
	return file.readAll();
}

QJsonObject element(QString id, double x = 10.0, double y = 20.0)
{
	return {{"id", id}, {"type", "text"}, {"name", "Layer"}, {"text", "Hello"}, {"x", x}, {"y", y},
		{"width", 333.125}, {"height", 77.75}, {"rotation", -17.625}, {"opacity", .375},
		{"visible", false}, {"locked", true}, {"futureElement", QJsonObject{{"deep", QJsonArray{1, 2, 3}}}}};
}

QJsonObject document(QString id, int width = 1920, int height = 1080)
{
	const QString unicodeName = QStringLiteral("Unicode ") + QChar(0x2728);
	const QString unicodeHtml = QStringLiteral("<div>") + QString::fromUcs4(U"🎥") + QStringLiteral("</div></script>");
	return {{"id", id}, {"name", unicodeName}, {"width", width}, {"height", height},
		{"durationMs", 6123.5}, {"triggerEvent", "audience.followed"}, {"eventDriven", true},
		{"externalUrl", "https://example.invalid/overlay?token=SYNTHETIC_ONLY"},
		{"customHtml", unicodeHtml},
		{"customCss", ".thing{transform:translateX(-1.25px)}"}, {"customJs", "const text = '</script>';"},
		{"elements", QJsonArray{element(id + "-layer", -41.875, 9001.25)}},
		{"futureDocument", QJsonObject{{"nested", QJsonArray{"value", 42}}}}};
}

QByteArray v1Bytes(const QJsonArray &overlays)
{
	return QJsonDocument(QJsonObject{{"schema", 1}, {"overlays", overlays}, {"futureRoot", true}})
		.toJson(QJsonDocument::Indented);
}

void migrationPreservesSourceAndRawDocuments()
{
	QTemporaryDir directory;
	expect(directory.isValid(), "temporary directory created");
	const QJsonArray originals{document("landscape"), document("portrait", 1080, 1920), document("custom", 1333, 777)};
	const QByteArray source = v1Bytes(originals);
	const QString sourcePath = QDir(directory.path()).filePath("overlays.json");
	expect(writeFile(sourcePath, source), "v1 fixture written");

	PulseOverlay::Store store(directory.path());
	const auto loaded = store.load();
	expect(loaded.state == PulseOverlay::StoreState::Migrated, "v1 store reports migration");
	expect(readFile(sourcePath) == source, "migration leaves original v1 bytes untouched");
	expect(store.draftDocuments() == originals, "migration preserves exact draft JSON");
	expect(store.publishedDocuments() == originals, "migration initializes identical published JSON");

	const QByteArray digest = QCryptographicHash::hash(source, QCryptographicHash::Sha256).toHex();
	const QString backup = QDir(directory.path()).filePath("overlays.v1.backup." + QString::fromLatin1(digest.left(12)) + ".json");
	expect(readFile(backup) == source, "migration backup preserves exact v1 bytes");
	const QByteArray v2Before = readFile(store.v2Path());

	PulseOverlay::Store retried(directory.path());
	expect(retried.load().state == PulseOverlay::StoreState::Ready, "second load uses v2 without remigrating");
	expect(readFile(store.v2Path()) == v2Before, "retry does not rewrite v2");
	expect(retried.draftDocuments() == originals, "retry preserves migrated documents");
}

void emptyAndBrokenStoresAreDistinct()
{
	QTemporaryDir emptyDirectory;
	const QByteArray empty = v1Bytes({});
	const QString emptyPath = QDir(emptyDirectory.path()).filePath("overlays.json");
	expect(writeFile(emptyPath, empty), "empty v1 fixture written");
	PulseOverlay::Store emptyStore(emptyDirectory.path());
	expect(emptyStore.load().state == PulseOverlay::StoreState::Migrated, "valid empty v1 migrates");
	expect(emptyStore.draftDocuments().isEmpty(), "valid empty v1 stays empty");
	expect(readFile(emptyPath) == empty, "valid empty v1 remains untouched");

	QTemporaryDir brokenDirectory;
	const QByteArray broken("{ this is not json");
	const QString brokenPath = QDir(brokenDirectory.path()).filePath("overlays.json");
	expect(writeFile(brokenPath, broken), "broken v1 fixture written");
	PulseOverlay::Store brokenStore(brokenDirectory.path());
	expect(brokenStore.load().state == PulseOverlay::StoreState::RecoveryRequired, "malformed v1 requires recovery");
	expect(!QFile::exists(brokenStore.v2Path()), "malformed v1 does not create v2");
	expect(readFile(brokenPath) == broken, "malformed v1 remains untouched");
}

void invalidIdentityAndGeometryStopMigration()
{
	QTemporaryDir duplicateDirectory;
	const QByteArray duplicate = v1Bytes(QJsonArray{document("same"), document("same")});
	const QString duplicatePath = QDir(duplicateDirectory.path()).filePath("overlays.json");
	expect(writeFile(duplicatePath, duplicate), "duplicate fixture written");
	PulseOverlay::Store duplicateStore(duplicateDirectory.path());
	expect(duplicateStore.load().state == PulseOverlay::StoreState::RecoveryRequired, "duplicate IDs stop migration");
	expect(!QFile::exists(duplicateStore.v2Path()), "duplicate IDs create no v2");

	QTemporaryDir geometryDirectory;
	QJsonObject invalid = document("invalid");
	QJsonArray elements = invalid.value("elements").toArray();
	QJsonObject invalidElement = elements.first().toObject();
	invalidElement.insert("width", -4);
	elements.replace(0, invalidElement);
	invalid.insert("elements", elements);
	const QByteArray geometry = v1Bytes(QJsonArray{invalid});
	const QString geometryPath = QDir(geometryDirectory.path()).filePath("overlays.json");
	expect(writeFile(geometryPath, geometry), "invalid geometry fixture written");
	PulseOverlay::Store geometryStore(geometryDirectory.path());
	expect(geometryStore.load().state == PulseOverlay::StoreState::RecoveryRequired, "invalid geometry stops migration");
	expect(readFile(geometryPath) == geometry, "invalid geometry source remains untouched");
}

void transactionFailureDoesNotCommitOrMutateMemory()
{
	QTemporaryDir directory;
	const QByteArray source = v1Bytes(QJsonArray{document("failure")});
	const QString sourcePath = QDir(directory.path()).filePath("overlays.json");
	expect(writeFile(sourcePath, source), "failure fixture written");
	auto committer = [](const QString &path, const QByteArray &bytes, QString *error) {
		if (path.endsWith("overlays.v2.json")) {
			if (error)
				*error = "injected commit failure";
			return false;
		}
		return PulseOverlay::Store::atomicWrite(path, bytes, error);
	};
	PulseOverlay::Store store(directory.path(), committer);
	const auto result = store.load();
	expect(result.state == PulseOverlay::StoreState::RecoveryRequired, "injected commit failure is reported");
	expect(!QFile::exists(store.v2Path()), "failed transaction creates no authoritative v2");
	expect(store.snapshot().isEmpty(), "failed transaction does not mutate in-memory root");
	expect(readFile(sourcePath) == source, "failed transaction leaves v1 untouched");
}

void draftsPreserveUnknownFieldsUntilPublish()
{
	QTemporaryDir directory;
	PulseOverlay::Store store(directory.path());
	expect(store.load().state == PulseOverlay::StoreState::Created, "missing store is reported distinctly");
	const QJsonObject original = document("draft-test");
	expect(store.initialize(QJsonArray{original}).ok(), "new v2 store initialized");

	QJsonObject edited = original;
	edited.insert("name", "Edited Draft");
	QJsonArray elements = edited.value("elements").toArray();
	QJsonObject editedElement = elements.first().toObject();
	editedElement.insert("text", "Changed");
	// Simulate the legacy runtime projection, which does not know futureElement.
	editedElement.remove("futureElement");
	elements.replace(0, editedElement);
	edited.insert("elements", elements);
	edited.remove("futureDocument");
	expect(store.syncDrafts(QJsonArray{edited}).ok(), "draft edit committed");
	const QJsonObject storedDraft = store.draftDocuments().first().toObject();
	expect(storedDraft.value("futureDocument") == original.value("futureDocument"), "unknown document fields survive known edit");
	expect(storedDraft.value("elements").toArray().first().toObject().contains("futureElement"),
	       "unknown element fields survive known edit");
	expect(store.publishedDocuments().first().toObject().value("name").toString() != "Edited Draft",
	       "draft edit does not change published revision");
	expect(!store.isDraftPublished("draft-test"), "store reports unpublished draft");

	PulseOverlay::Store draftReopened(directory.path());
	expect(draftReopened.load().state == PulseOverlay::StoreState::Ready, "newer draft store reopens");
	expect(!draftReopened.isDraftPublished("draft-test"), "unpublished state survives restart");
	expect(draftReopened.draftDocuments().first().toObject().value("name").toString() == "Edited Draft",
	       "newer draft survives restart");
	expect(draftReopened.publish("draft-test").ok(), "draft publishes explicitly");
	expect(draftReopened.publishedDocuments() == draftReopened.draftDocuments(),
	       "published revision matches draft after publish");
	expect(draftReopened.isDraftPublished("draft-test"), "store reports matching published draft");

	PulseOverlay::Store reopened(directory.path());
	expect(reopened.load().state == PulseOverlay::StoreState::Ready, "published store reopens");
	expect(reopened.publishedDocuments() == draftReopened.publishedDocuments(), "published revision survives restart");
}

void failedDraftSaveRetainsCommittedSnapshot()
{
	QTemporaryDir directory;
	bool failWrites = false;
	auto committer = [&failWrites](const QString &path, const QByteArray &bytes, QString *error) {
		if (failWrites && path.endsWith("overlays.v2.json")) {
			if (error)
				*error = "injected draft failure";
			return false;
		}
		return PulseOverlay::Store::atomicWrite(path, bytes, error);
	};
	PulseOverlay::Store store(directory.path(), committer);
	expect(store.load().state == PulseOverlay::StoreState::Created, "draft failure fixture starts empty");
	expect(store.initialize(QJsonArray{document("durable")}).ok(), "draft failure fixture initializes");
	const QJsonObject before = store.snapshot();
	QJsonObject changed = document("durable");
	changed.insert("name", "Must Not Commit");
	failWrites = true;
	expect(!store.syncDrafts(QJsonArray{changed}).ok(), "draft commit failure is reported");
	expect(store.snapshot() == before, "failed draft commit leaves in-memory snapshot unchanged");
	failWrites = false;
	PulseOverlay::Store reopened(directory.path());
	expect(reopened.load().state == PulseOverlay::StoreState::Ready, "store remains readable after failed draft commit");
	expect(reopened.draftDocuments().first().toObject().value("name") == before.value("projects").toArray().first()
												.toObject().value("draft").toObject().value("document").toObject().value("name"),
	       "failed draft commit leaves disk snapshot unchanged");
}

void futureSchemaIsReadOnly()
{
	QTemporaryDir directory;
	const QString path = QDir(directory.path()).filePath("overlays.v2.json");
	const QByteArray bytes = QJsonDocument(QJsonObject{{"schema", 99}, {"opaque", "future"}}).toJson();
	expect(writeFile(path, bytes), "future schema fixture written");
	PulseOverlay::Store store(directory.path());
	expect(store.load().state == PulseOverlay::StoreState::ReadOnlyFuture, "future schema opens read-only");
	expect(!store.syncDrafts({}).ok(), "future schema cannot be overwritten");
	expect(readFile(path) == bytes, "future schema bytes remain unchanged");
}

} // namespace

int main(int argc, char **argv)
{
	QCoreApplication application(argc, argv);
	if (argc == 2) {
		// Read-only input; migration and restart happen solely in a private temp copy.
		QTemporaryDir temporary;
		const QByteArray original = readFile(QString::fromLocal8Bit(argv[1]));
		expect(!original.isEmpty() && temporary.isValid(), "private legacy store input available");
		const auto documents = QJsonDocument::fromJson(original).object().value("overlays").toArray();
		expect(writeFile(temporary.filePath("overlays.json"), original), "copy legacy overlay store");
		PulseOverlay::Store store(temporary.path());
		expect(store.load().ok(), "real prior-version overlay store migrates");
		expect(store.draftDocuments() == documents && store.publishedDocuments() == documents, "real draft/published documents preserved exactly");
		expect(store.syncDrafts(store.draftDocuments()).ok(), "real store unchanged save");
		PulseOverlay::Store restarted(temporary.path());
		expect(restarted.load().ok() && restarted.draftDocuments() == documents, "real migrated store survives restart");
		expect(readFile(QString::fromLocal8Bit(argv[1])) == original, "real input file remains byte-identical");
		std::cout << "Private copied overlay migration: " << documents.size() << " documents, " << failures << " failures\n";
		return failures ? 1 : 0;
	}
	{
		QJsonObject raw = document("projection");
		QJsonObject rawElement = raw.value("elements").toArray().first().toObject();
		rawElement.insert("width", 5.125);
		raw.insert("elements", QJsonArray{rawElement});
		QJsonObject baseline = raw;
		baseline.insert("durationMs", 6123);
		baseline.insert("newDefault", false);
		QJsonObject projectedElement = rawElement;
		projectedElement.insert("width", 10.0);
		baseline.insert("elements", QJsonArray{projectedElement});
		expect(PulseOverlay::mergeEditedDocument(raw, baseline, baseline) == raw, "untouched runtime projection retains exact authoritative JSON");
		QJsonObject edited = baseline;
		edited.insert("name", "Renamed");
		QJsonObject merged = PulseOverlay::mergeEditedDocument(raw, baseline, edited);
		QJsonObject expected = raw;
		expected.insert("name", "Renamed");
		expect(merged == expected, "rename preserves fractional duration, tiny layer, unknown fields and missing optional defaults");
		projectedElement.insert("x", 123.25);
		edited.insert("elements", QJsonArray{projectedElement});
		merged = PulseOverlay::mergeEditedDocument(raw, baseline, edited);
		expect(merged.value("elements").toArray().first().toObject().value("width").toDouble() == 5.125 &&
		       merged.value("elements").toArray().first().toObject().value("x").toDouble() == 123.25,
		       "editing position changes only that field, retaining projected layer width");
	}
	migrationPreservesSourceAndRawDocuments();
	emptyAndBrokenStoresAreDistinct();
	invalidIdentityAndGeometryStopMigration();
	transactionFailureDoesNotCommitOrMutateMemory();
	draftsPreserveUnknownFieldsUntilPublish();
	failedDraftSaveRetainsCommittedSnapshot();
	futureSchemaIsReadOnly();
	if (failures == 0)
		std::cout << "Pulse overlay store tests passed\n";
	return failures == 0 ? 0 : 1;
}
