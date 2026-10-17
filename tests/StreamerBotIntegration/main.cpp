#include "../../engine/obs-studio/plugins/pulse-weaver-core/pulse-basic-controls.hpp"
#include "../../engine/obs-studio/plugins/pulse-weaver-core/pulse-basic-controls-page.hpp"
#include <QApplication>
#include <QFile>
#include <QSignalBlocker>
#include <QScrollBar>
#include <cstdio>
#include <cstdlib>
#include <limits>

static unsigned checks;
class RecordingOwner final : public QObject {
	Q_OBJECT
public:
	bool actual = false;
	Q_INVOKABLE bool PulseWeaverRecordingActive() const { return actual; }
};

static void check(bool ok, const char *message)
{
	if (!ok) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
	++checks;
}

int main(int argc, char **argv)
{
	QApplication app(argc, argv);
	app.setStyle("windowsvista"); // Match OBSApp::InitTheme on Windows.
	const auto themePath = qEnvironmentVariable("PULSEWEAVER_CONTROL_THEME_QSS");
	if (!themePath.isEmpty()) {
		QFile theme(themePath);
		check(theme.open(QIODevice::ReadOnly), "native audit's resolved theme stylesheet is readable");
		app.setStyleSheet(QString::fromUtf8(theme.readAll()));
	} else {
		// The inherited Yami button cap caused the actual native card clipping.
		// Exercise that rule even when no complete audit stylesheet is supplied.
		app.setStyleSheet("QPushButton { height: 22px; max-height: 22px; margin-top: 2px; margin-bottom: 2px; padding: 4px 16px; border: 1px solid #3b485a; }");
	}
	using namespace PulseBasicControls;
	check(defaultApiPort("{\"product\":\"studio2-preview\"}", 18755) == 18775, "2.0 packaged identity has isolated fresh default port");
	check(defaultApiPort("{\"product\":\"studio2-preview\"}", 18765) == 18775, "preview marker selects isolated default without build flags");
	check(defaultApiPort("{\"product\":\"studio\"}", 18755) == 18755, "regular default remains unchanged");
	check(defaultApiPort("{}", 18765) == 18765 && defaultApiPort("invalid", 18755) == 18755, "motion and invalid/missing identity fall back safely");
	QComboBox stages, mode;
	QPushButton record;
	stages.addItem("Game · Twitch + YouTube"); stages.setItemData(0, "Game", Qt::UserRole + 1);
	stages.addItem("BRB · Twitch + YouTube"); stages.setItemData(1, "BRB", Qt::UserRole + 1);
	mode.addItem("Default", "standard"); mode.addItem("Off", "off"); mode.addItem("Both", "dual");
	bool recording = false, replay = false, vetoStage = false, vetoSource = false;
	int recordClicks = 0, replaySaves = 0, operations = 0, stageChanges = 0;
	QObject::connect(&record, &QPushButton::clicked, [&] { ++recordClicks; recording = !recording; });
	QObject::connect(&stages, &QComboBox::currentIndexChanged, [&](int index) {
		if (vetoStage && index == 1) { const QSignalBlocker blocker(&stages); stages.setCurrentIndex(0); }
		else ++stageChanges;
	});
	QJsonArray sources{
		QJsonObject{{"id", "scene-id"}, {"name", "Game"}, {"audio", false}, {"items", QJsonArray{
			QJsonObject{{"itemId", "9007199254740993"}, {"name", "Repeated label"}, {"visible", false}},
			QJsonObject{{"itemId", "7"}, {"name", "Repeated label"}, {"visible", true}}}}},
		QJsonObject{{"id", "audio-id"}, {"name", "Mic"}, {"audio", true}, {"muted", false}, {"volume", 25.0}}};
	Context context;
	context.stages = &stages; context.recordingMode = &mode; context.record = &record;
	context.sources = [&] { return sources; };
	context.state = [&] { return QJsonObject{{"recording", recording}, {"replayActive", replay}}; };
	context.recordingActive = [&] { return recording; };
	context.replayActive = [&] { return replay; };
	context.saveReplay = [&] { ++replaySaves; };
	context.operateSource = [&](const QUrlQuery &query) {
		++operations;
		if (vetoSource) return QJsonObject{{"ok", false}, {"message", "Fixture native controller rejected change."}};
		const QString action = query.queryItemValue("action");
		if (query.queryItemValue("source") == "scene-id") {
			auto scene = sources[0].toObject(); auto items = scene["items"].toArray();
			for (int index = 0; index < items.size(); ++index) {
				auto item = items[index].toObject();
				if (item["itemId"].toString() != query.queryItemValue("item")) continue;
				item["visible"] = action == "toggle_visibility" ? !item["visible"].toBool() : action == "show";
				items[index] = item;
			}
			scene["items"] = items; sources[0] = scene;
		} else {
			auto audio = sources[1].toObject();
			if (action == "volume") audio["volume"] = query.queryItemValue("volume").toDouble();
			else audio["muted"] = action == "toggle_mute" ? !audio["muted"].toBool() : action == "mute";
			sources[1] = audio;
		}
		return QJsonObject{{"ok", true}};
	};
	auto run = [&](QJsonObject request, int expected) {
		request["requestId"] = "fixture-42";
		const auto result = execute(request, context);
		check(result.status == expected, "command returns expected HTTP status");
		check(result.body["ok"].toBool() == (expected < 300), "ok agrees with status");
		check(result.body["requestId"] == "fixture-42", "request identity echoed without modifying command");
		return result.body;
	};
	const auto catalogueResult = catalogue(context);
	check(catalogueResult["commands"].toArray().size() == 13, "small explicit command catalogue");
	check(catalogueResult["stages"].toArray()[0] == "Game", "friendly Stage names omit route decorations");
	check(!catalogueResult["availability"].toObject()["replaySave"].toBool(), "inactive replay capability reported");
	run({{"command", "stage.select"}, {"name", "BRB"}}, 200);
	check(stages.currentIndex() == 1 && stageChanges == 1, "Stage selection uses native controller signal");
	run({{"command", "stage.next"}}, 200); check(stages.currentIndex() == 0, "next Stage wraps");
	run({{"command", "stage.previous"}}, 200); check(stages.currentIndex() == 1, "previous Stage wraps");
	run({{"command", "stage.select"}, {"name", "missing"}}, 404);
	stages.setCurrentIndex(0); vetoStage = true;
	run({{"command", "stage.select"}, {"name", "BRB"}}, 409);
	check(stages.currentIndex() == 0, "native Stage veto respected"); vetoStage = false;
	stages.setEnabled(false); run({{"command", "stage.next"}}, 409); stages.setEnabled(true);
	stages.addItem("Duplicate"); stages.setItemData(2, "Game", Qt::UserRole + 1);
	run({{"command", "stage.select"}, {"name", "Game"}}, 409); stages.removeItem(2);
	for (const auto &command : {"source.show", "source.hide", "source.toggle"}) {
		const auto response = run({{"command", command}, {"sceneId", "scene-id"}, {"itemId", "9007199254740993"}}, 200);
		const auto items = response["state"].toObject()["items"].toArray();
		check(items[0].toObject()["visible"].toBool() == (QString(command) != "source.hide"), "observed item visibility returned");
		check(items[1].toObject()["visible"].toBool(), "duplicate source label never changes another item");
	}
	const int beforeInvalid = operations;
	run({{"command", "source.show"}, {"sceneId", "scene-id"}, {"itemId", 9007199254740992.0}}, 400);
	run({{"command", "source.show"}, {"sceneId", "scene-id"}, {"itemId", "07"}}, 400);
	run({{"command", "source.show"}, {"sceneId", "scene-id"}, {"itemId", "-1"}}, 400);
	run({{"command", "source.show"}, {"sceneId", "scene-id"}, {"itemId", "8"}}, 404);
	run({{"command", "audio.mute"}, {"sourceId", "private-routing-source"}}, 404);
	run({{"command", "audio.mute"}, {"sourceId", "scene-id"}}, 409);
	for (const QJsonValue &value : {QJsonValue(-1), QJsonValue(101), QJsonValue("50"), QJsonValue(true),
		QJsonValue(QJsonValue::Null), QJsonValue(std::numeric_limits<double>::infinity())})
		run({{"command", "audio.volume"}, {"sourceId", "audio-id"}, {"volume", value}}, 400);
	check(operations == beforeInvalid, "invalid arguments never reach a native source operation");
	run({{"command", "audio.mute"}, {"sourceId", "audio-id"}}, 200); check(sources[1].toObject()["muted"].toBool(), "mute");
	run({{"command", "audio.unmute"}, {"sourceId", "audio-id"}}, 200); check(!sources[1].toObject()["muted"].toBool(), "unmute");
	run({{"command", "audio.toggle"}, {"sourceId", "audio-id"}}, 200); check(sources[1].toObject()["muted"].toBool(), "toggle mute");
	for (double volume : {0.0, 37.125, 100.0}) {
		const auto response = run({{"command", "audio.volume"}, {"sourceId", "audio-id"}, {"volume", volume}}, 200);
		check(response["state"].toObject()["volume"].toDouble() == volume, "volume uses percent and returns observed value");
	}
	vetoSource = true; run({{"command", "audio.mute"}, {"sourceId", "audio-id"}}, 409); vetoSource = false;
	mode.setCurrentIndex(1); run({{"command", "recording.start"}}, 409); check(recordClicks == 0, "Off recording guard");
	mode.setCurrentIndex(0); run({{"command", "recording.start"}}, 202);
	check(recording && mode.currentData() == "standard", "Default recording remains selected and native controller invoked");
	run({{"command", "recording.start"}}, 200); check(recordClicks == 1, "already active start cannot toggle recording off");
	run({{"command", "recording.stop"}}, 202); run({{"command", "recording.stop"}}, 200);
	check(!recording && recordClicks == 2, "already stopped recording stays stopped");
	record.setEnabled(false); run({{"command", "recording.start"}}, 409); record.setEnabled(true);
	run({{"command", "replay.save"}}, 409); check(replaySaves == 0, "save replay never starts a disabled buffer");
	replay = true; run({{"command", "replay.save"}}, 202); check(replaySaves == 1, "active replay save requested once");
	for (const char *command : {"stream.start", "script.run", "source.delete", "motion.raw", ""})
		run({{"command", command}}, 400);
	run({{"command", "stage.next"}, {"url", "https://example.invalid"}}, 400);
	run({{"command", "audio.mute"}, {"sourceId", "audio-id\nforged"}}, 400);
	check(execute({{"command", "stage.next"}, {"requestId", 1}}, context).status == 400, "request identity type checked");
	Context missing;
	check(execute({{"command", "recording.start"}}, missing).status == 409, "missing recording controller is safe");
	check(execute({{"command", "stage.next"}}, missing).status == 409, "missing Stage controller is safe");
	check(execute({{"command", "audio.mute"}, {"sourceId", "audio-id"}}, missing).status == 409, "missing source controller is safe");
	RecordingOwner owner;
	owner.setProperty("pulseWeaverRecordingActive", true); // Displayed status has not refreshed after routed stop.
	context.recordingActive = [&] { return recordingState(&owner, false).active; };
	const int beforeRepeatedStop = recordClicks;
	run({{"command", "recording.stop"}}, 200);
	check(recordClicks == beforeRepeatedStop && !recordingState(&owner, false).active,
	      "stale displayed recording flag cannot turn a second stop into a recording start");
	owner.actual = true;
	check(recordingState(&owner, false).active && recordingState(&owner, true).active, "authoritative native/routed observation combined");
	check(!recordingState(nullptr, false).available, "missing native recording hook fails closed");
	const int operationsBeforePage = operations, recordingsBeforePage = recordClicks, replayBeforePage = replaySaves;
	Page page("fixture-private-token", 18775, [&] { return context; });
	page.setAttribute(Qt::WA_DontShowOnScreen);
	page.resize(1148, 500); page.show(); app.processEvents();
	auto *credential = page.findChild<QLineEdit *>("PulseWeaverControlToken");
	check(credential && credential->isReadOnly() && credential->echoMode() == QLineEdit::Password &&
		credential->displayText() != credential->text(), "connection token is masked by default");
	for (auto *label : page.findChildren<QLabel *>())
		check(!label->text().contains("fixture-private-token"), "no plaintext token in labels or API information");
	check(page.findChildren<QComboBox *>().isEmpty(), "Commands page uses click-through categories instead of command lists");
	const auto categories = page.findChildren<QPushButton *>("PulseWeaverCommandCategory");
	check(categories.size() == 4, "four clear command category cards");
	for (auto *card : categories)
		check(card->sizeHint().height() == card->height() && card->minimumSizeHint().height() == card->height(),
		      "custom card size hints match its complete fixed-height painted rect");
	auto *scroll = page.findChild<QScrollArea *>();
	auto *navigator = page.findChild<QStackedWidget *>("PulseWeaverCommandNavigator");
	check(navigator && navigator->currentIndex() == 0, "command navigator opens at category cards");
	check(navigator->layout()->contentsMargins() == QMargins(), "current-page stack explicitly removes internal layout margins");
	auto landingFits = [&] {
		app.processEvents(); page.layout()->activate(); app.processEvents();
		for (auto *card : categories) {
			const QRect bounds(card->mapTo(scroll->viewport(), QPoint()), card->size());
			if (!scroll->viewport()->rect().contains(bounds)) {
				std::fprintf(stderr, "Viewport clips %s: card=%d,%d,%d,%d viewport=%d,%d\n", qPrintable(card->text()), bounds.x(), bounds.y(), bounds.width(), bounds.height(), scroll->viewport()->width(), scroll->viewport()->height());
				return false;
			}
			// A card can be inside the scroll viewport while its parent clips it.
			if (!navigator->contentsRect().contains(QRect(card->mapTo(navigator, QPoint()), card->size()))) {
				const auto relative = card->mapTo(navigator, QPoint());
				std::fprintf(stderr, "Navigator clips %s: card=%d,%d,%d,%d navigator=%d,%d\n", qPrintable(card->text()), relative.x(), relative.y(), card->width(), card->height(), navigator->width(), navigator->height());
				return false;
			}
			for (auto *ancestor = card->parentWidget(); ancestor && ancestor != scroll->viewport(); ancestor = ancestor->parentWidget())
				if (!ancestor->rect().contains(QRect(card->mapTo(ancestor, QPoint()), card->size()))) return false;
		}
		auto *guide = page.findChild<QPushButton *>("PulseWeaverControlGuide");
		const QRect guideBounds(guide->mapTo(scroll->viewport(), QPoint()), guide->size());
		if (!scroll->viewport()->rect().contains(guideBounds))
			std::fprintf(stderr, "Viewport clips guide: guide=%d,%d,%d,%d viewport=%d,%d\n", guideBounds.x(), guideBounds.y(), guideBounds.width(), guideBounds.height(), scroll->viewport()->width(), scroll->viewport()->height());
		return scroll->viewport()->rect().contains(guideBounds);
	};
	if (argc > 1) page.grab().save(QString::fromLocal8Bit(argv[1]));
	check(landingFits(), "all four category cards and guide fit above the fold at 1180x720 native content height");
	if (!themePath.isEmpty()) {
		for (auto *card : categories) {
			const auto relative = card->mapTo(navigator, QPoint());
			std::printf("THEME: %s rect=%d,%d,%d,%d navigator=%d,%d\n", qPrintable(card->text()), relative.x(), relative.y(), card->width(), card->height(), navigator->width(), navigator->height());
		}
	}
	const int landingHeight = navigator->height();
	categories[2]->click();
	check(navigator->currentIndex() != 0, "category click opens a focused command grid");
	for (auto *choice : page.findChildren<QPushButton *>("PulseWeaverCommandChoice"))
		if (choice->property("pulseCommand").toString() == "audio.volume") choice->click();
	auto *json = page.findChild<QPlainTextEdit *>("PulseWeaverBasicCommandJson");
	check(json && QJsonDocument::fromJson(json->toPlainText().toUtf8()).object()["command"] == "audio.volume",
	      "command click reveals its copyable JSON example");
	navigator->setCurrentIndex(0);
	check(landingFits() && navigator->height() == landingHeight, "returning from the larger JSON page restores compact category geometry");
	check(navigator->height() <= 150, "hidden detail pages cannot inflate the category grid");
	if (argc > 1) page.grab().save(QString::fromLocal8Bit(argv[1]));
	navigator->setStyleSheet("QStackedWidget#PulseWeaverCommandNavigator { border: 2px solid #687585; padding: 9px 7px 11px 5px; }");
	navigator->ensurePolished(); navigator->updateGeometry(); app.processEvents();
	const auto insets = navigator->contentsMargins();
	const QSize insetSize(insets.left() + insets.right(), insets.top() + insets.bottom());
	check(insets.top() > 0 && insets.bottom() > 0 && insets.left() != insets.right(), "themed regression has real asymmetric frame and padding insets");
	check(navigator->sizeHint() == navigator->currentWidget()->sizeHint() + insetSize &&
	      navigator->minimumSizeHint() == navigator->currentWidget()->minimumSizeHint() + insetSize,
	      "current-page hints include the themed stack insets without hidden page sizes");
	check(landingFits(), "themed padding cannot clip the bottom category cards or push the guide below the fold");
	categories[0]->click(); navigator->setCurrentIndex(0);
	check(landingFits() && navigator->height() <= 150 + insetSize.height(), "themed category layout remains compact and fully visible after Back");
	check(operations == operationsBeforePage && recordClicks == recordingsBeforePage && replaySaves == replayBeforePage,
	      "browsing command categories and examples never executes operations");
	auto *reveal = page.findChild<QPushButton *>("PulseWeaverRevealControlToken");
	reveal->click(); check(credential->echoMode() == QLineEdit::Normal, "explicit Reveal shows token");
	reveal->click(); check(credential->echoMode() == QLineEdit::Password, "Hide restores masking");
	page.setConnectionState(true, 18775);
	check(page.findChild<QLabel *>("PulseWeaverControlConnectionStatus")->text().startsWith("Listening"), "actual listener state displayed");
	page.setConnectionState(false, 18775, "Port is in use");
	check(page.findChild<QLabel *>("PulseWeaverControlConnectionStatus")->text().contains("Port is in use"), "listener failure reported without a test command or polling timer");
	std::printf("PASS: %u offline basic-control validation, native-controller guard and feedback checks; no broadcasts or recording files started.\n", checks);
}

#include "main.moc"
