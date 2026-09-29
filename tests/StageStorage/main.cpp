#include "../../engine/obs-studio/shared/qt/PulseStageStorage.hpp"
#include <QCoreApplication>
#include <QMap>
#include <QTemporaryDir>
#include <cstdio>
#include <cstdlib>

using namespace PulseStageStorage;
static int checks;
static void check(bool ok, const char *message)
{
	++checks;
	if (!ok) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}

int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	QMap<QString, QString> names{{"camera-uuid", "Facecam"}, {"mic-uuid", "Microphone"}};
	const Resolver byUuid = [&names](const QString &uuid, const QString &) { return names.value(uuid); };
	const Resolver byName = [&names](const QString &name, const QString &) { return names.key(name); };
	QJsonObject legacy{{"canvas", "horizontal"}, {"scene", "Hangout"}, {"custom", 42},
		{"excluded", QJsonArray{"Facecam", "Microphone"}}};
	auto pinned = normalizeAssignment(legacy, byName, byUuid, true);
	check(pinned.value("excludedIdentities").toArray().size() == 2, "Legacy exclusions pin identities");
	check(pinned.value("excluded") == legacy.value("excluded"), "Legacy string array preserved");
	check(pinned.value("custom").toInt() == 42, "Other assignment fields preserved");
	names["camera-uuid"] = "Printer Camera";
	auto renamed = normalizeAssignment(pinned, byName, byUuid);
	check(renamed.value("excluded").toArray().first().toString() == "Printer Camera", "Renamed source remains excluded");
	check(renamed.value("excludedIdentities").toArray().first().toObject().value("uuid") == "camera-uuid", "Rename preserves original identity");
	names["new-camera"] = "Facecam";
	check(!renamed.value("excluded").toArray().contains("Facecam"), "Reused old name does not inherit exclusion");
	names.remove("camera-uuid");
	auto missing = normalizeAssignment(pinned, byName, byUuid);
	check(missing.value("excluded").toArray().first().toString() == missingName("camera-uuid"), "Absent source has a safe unmatched marker");
	check(!missing.value("excluded").toArray().contains("Facecam"), "Absent UUID does not bind a replacement with the same name");
	auto storedMissing = normalizeAssignment(missing, byName, byUuid, true);
	check(storedMissing.value("excluded").toArray().first().toString() == "Facecam", "Stored legacy name remains readable");
	check(storedMissing.value("excludedIdentities").toArray().first().toObject().value("uuid") == "camera-uuid", "Missing identity survives another save");
	names["camera-uuid"] = "Restored Camera";
	check(normalizeAssignment(storedMissing, byName, byUuid).value("excluded").toArray().first() == "Restored Camera", "Restored UUID resumes exclusion");
	auto removed = missing;
	removed["excluded"] = QJsonArray{"Microphone"};
	removed = normalizeAssignment(removed, byName, byUuid, true);
	check(removed.value("excludedIdentities").toArray().size() == 1, "Explicit checkbox removal drops stale identity metadata");
	check(removed.value("excluded").toArray() == QJsonArray{"Microphone"}, "Removed source is not resurrected");
	removed["excluded"] = QJsonArray{};
	check(normalizeAssignment(removed, byName, byUuid, true).value("excludedIdentities").toArray().isEmpty(), "Clearing exclusions clears identities");
	auto unknown = legacy;
	unknown["excluded"] = QJsonArray{"Not loaded yet"};
	check(normalizeAssignment(unknown, byName, byUuid).value("excluded").toArray() == QJsonArray{"Not loaded yet"}, "Unresolved legacy names survive until loading");
	QJsonArray stages{QJsonObject{{"name", "My stage"}, {"custom", "keep"}, {"assignments", QJsonObject{{"youtube_horizontal", pinned}}}}};
	auto saved = normalize(stages, byName, byUuid, true);
	check(saved.first().toObject().value("custom") == "keep", "Stage fields preserved");
	check(normalize(saved, byName, byUuid, true) == saved, "Persistence normalization is stable");
	QTemporaryDir directory;
	check(directory.isValid(), "Create disposable fixture directory");
	const auto path = directory.filePath("nested/stages.json");
	QString error;
	check(write(path, saved, error) && error.isEmpty(), "Atomic stage save succeeds");
	QFile file(path); check(file.open(QIODevice::ReadOnly), "Read saved stage fixture");
	const auto loaded = QJsonDocument::fromJson(file.readAll()).array(); file.close();
	check(loaded == saved, "Saved stage identity JSON round trips");
	names["camera-uuid"] = "After restart";
	check(normalize(loaded, byName, byUuid).first().toObject().value("assignments").toObject()
		.value("youtube_horizontal").toObject().value("excluded").toArray().first() == "After restart", "Reload resolves later rename by UUID");
	check(file.open(QIODevice::WriteOnly | QIODevice::Truncate), "Prepare corrupt synthetic stage file");
	file.write("broken fixture"); file.close();
	check(!write(path, {}, error) && !error.isEmpty(), "Invalid prior stage file is preserved and reported");
	check(file.open(QIODevice::ReadOnly) && file.readAll() == "broken fixture", "Save failure does not erase prior file"); file.close();
	check(!write(directory.path(), saved, error) && !error.isEmpty(), "Invalid destination reports failure");
	std::printf("PASS: %d stage identity, rename, removal, reload and atomic storage checks\n", checks);
}
