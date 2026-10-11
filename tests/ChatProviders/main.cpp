#include "../../engine/obs-studio/shared/qt/PulseChatPanel.hpp"
#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <cstdio>
#include <cstdlib>
using namespace PulseChatPanel;
static int checks = 0;
static const QString A = "https://chat.example.invalid/embed?room=A%2FB&key=x%26y#messages";
static const QString B = "https://friends.example.invalid/chat";
static void check(bool ok, const char *message)
{
	++checks;
	if (!ok) {
		std::fprintf(stderr, "FAIL: %s\n", message);
		std::exit(1);
	}
}
static QByteArray bytes(const QString &path)
{
	QFile f(path);
	check(f.open(QIODevice::ReadOnly), "temporary settings readable");
	return f.readAll();
}
static Preferences fixture(QString active = "native")
{
	return normalize(
		{active, {{"lumia", "Lumia", LumiaDockUrl, -4}, {"team", "Team", A, 0}, {"friends", "Friends", B, 2}}});
}
static void links()
{
	check(normalizeLink("  HTTPS://CHAT.EXAMPLE.INVALID/embed  ") == "https://chat.example.invalid/embed",
	      "normalize scheme/host/outer whitespace");
	check(normalizeLink(A) == A, "preserve encoded query and fragment");
	check(normalizeLink("http://127.0.0.1:8123/chat") == "http://127.0.0.1:8123/chat", "HTTP port preserved");
	check(normalizeLink(QString::fromUtf8("https://chat.example.invalid/caf\xc3\xa9")) ==
		      "https://chat.example.invalid/caf%C3%A9",
	      "Unicode path encoded");
	check(normalizeLink("https://chat.example.invalid/a%20b") == "https://chat.example.invalid/a%20b",
	      "no double encoding");
	for (auto *u :
	     {"", "   ", "chat.example.invalid", "/chat", "//chat.example.invalid/chat", "https://", "http:///chat",
	      "https://chat.example.invalid/%zz", "javascript:alert(1)", "data:text/html,chat", "file:///C:/chat.html",
	      "C:\\chat.html", "qrc:/chat.html", "about:blank", "ftp://chat.example.invalid/chat",
	      "https://user:pass@chat.example.invalid/chat", "https://user@chat.example.invalid/chat"}) {
		check(normalizeLink(u).isEmpty(), "reject unsafe/malformed/relative/credential links");
	}
	for (const auto &u : {A, B, QString("http://localhost:8123/chat")}) {
		check(normalizeLink(normalizeLink(u)) == normalizeLink(u), "normalization idempotent");
	}
	for (auto *u : {"https://lumiastream.com/dock/chat", "https://www.lumiastream.com/widget/test",
			"HTTP://LUMIASTREAM.COM/dock?x=y"}) {
		check(isLumiaLink(u), "exact normalized Lumia hosts accepted");
		check(defaultZoomSteps(u) == -4, "Lumia default67");
	}
	for (auto *u :
	     {"https://lumiastream.com.evil.invalid/dock", "https://evil-lumiastream.com/dock",
	      "https://chat.example.invalid/?host=lumiastream.com", "https://evil.invalid/lumiastream.com",
	      "https://sub.lumiastream.com/dock", "javascript://lumiastream.com/", "file://lumiastream.com/chat"}) {
		check(!isLumiaLink(u), "substring/lookalike/unsafe hosts not Lumia");
		check(defaultZoomSteps(u) == 0, "other default100");
	}
}
static void panels()
{
	Preferences empty;
	check(empty.panels.isEmpty() && enabledProviders(empty) == QStringList{"native"}, "fresh native-only");
	check(nextProvider(empty) == "native" && title(empty) == "Chat" && empty.url().isEmpty(),
	      "native title no target");
	auto p = fixture();
	QStringList order{"native", "lumia", "team", "friends"};
	check(enabledProviders(p) == order, "arbitrary ordered panels");
	for (int i = 0; i < 12; ++i) {
		p.provider = nextProvider(p);
		check(p.provider == order[(i + 1) % 4], "cycle wraps stable IDs");
		const auto *item = selectedPanel(p);
		check(title(p) == (item ? item->name : QString("Chat")), "title uses name");
		check(p.url() == (item ? item->url : QString()), "target uses selected URL");
	}
	check(!selectedPanel(empty) && !findPanel(p, "unknown"), "no external native/missing panel");
	p.provider = "team";
	auto *edit = selectedPanel(p);
	edit->name = "Renamed team";
	edit->url = B + "?edit=1";
	edit->zoomSteps = -2;
	p = normalize(p);
	check(p.provider == "team" && title(p) == "Renamed team" && p.url().endsWith("?edit=1"),
	      "edit preserves active ID");
	p.panels.append({"duplicate-url", "Alternative", p.url(), 3});
	p = normalize(p);
	check(findPanel(p, "duplicate-url") && findPanel(p, "duplicate-url")->zoomSteps == 3 &&
		      findPanel(p, "team")->zoomSteps == -2,
	      "same URL distinct IDs/zooms");
	auto removed = removeProvider(p, "friends");
	check(removed.provider == "team" && !findPanel(removed, "friends"), "inactive removal preserves current");
	removed = removeProvider(p, "team");
	check(removed.provider == "native" && !findPanel(removed, "team"), "active removal restores native");
	check(enabledProviders(removeProvider(p, "native")) == enabledProviders(p) &&
		      removeProvider(p, "native").provider == p.provider,
	      "native not removable");
	check(removeProvider(p, "unknown").provider == p.provider, "unknown removal harmless");
	check(normalize({"unknown", p.panels}).provider == "native", "missing active fallback");
	const auto bad = normalize({"bad",
				    {{"bad", "Bad", "javascript:alert(1)", 0},
				     {"native", "Hijack", A, 0},
				     {"", "Empty", A, 0},
				     {" valid ", "  Team chat  ", A, -999},
				     {"valid", "Duplicate", B, 999}}});
	check(bad.panels.size() == 1 && bad.provider == "native", "invalid/empty/reserved/duplicate IDs excluded");
	check(findPanel(bad, "valid") && findPanel(bad, "valid")->name == "Team chat" &&
		      findPanel(bad, "valid")->url == A && findPanel(bad, "valid")->zoomSteps == -7,
	      "first valid entry preserved and normalized");
	const auto repaired = normalize({"id", {{"id", "Bad", "javascript:alert(1)", 0}, {"id", "Good", A, 999}}});
	check(selectedPanel(repaired) && title(repaired) == "Good" && zoomSteps(repaired) == 8,
	      "invalid first entry does not reserve ID");
	const auto names = normalize({"name", {{"name", " \t Team\n chat\x01 ", A, 0}, {"infer", "  ", B, 0}}});
	check(findPanel(names, "name")->name == "Team chat", "name single-line without controls");
	check(findPanel(names, "infer")->name == defaultPanelName(B) && !findPanel(names, "infer")->name.isEmpty(),
	      "blank name inferred");
	const auto longName =
		normalize({"long", {{"long", QString(79, 'A') + QString::fromUtf8("\xf0\x9f\x98\x80") + "end", A, 0}}});
	check(selectedPanel(longName)->name.size() <= 80 && !selectedPanel(longName)->name.back().isHighSurrogate(),
	      "name cap does not split surrogate");
}
static void storage(const QString &dir)
{
	const QString path = dir + "/generic.ini";
	{
		QSettings s(path, QSettings::IniFormat);
		check(load(s).panels.isEmpty() && s.allKeys().isEmpty(), "load default read-only");
		s.setValue("chat/draft", "keep draft");
		s.setValue("show/stage", "Starting");
		s.setValue("chat/lumia_dock_url", "https://lumiastream.com/dock?old=yes");
		s.setValue("chat/custom_dock_url", "https://old.example.invalid/chat");
		save(s, fixture("friends"));
		check(s.status() == QSettings::NoError && s.value("chat/panel_version").toInt() == 3, "save schema3");
		auto d = QJsonDocument::fromJson(s.value("chat/panels_json").toString().toUtf8());
		check(d.isArray() && d.array().size() == 3, "save generic JSON list");
		check(s.value("chat/lumia_dock_url").toString().endsWith("?old=yes") &&
			      s.value("chat/custom_dock_url").toString() == "https://old.example.invalid/chat",
		      "deprecated keys untouched");
	}
	{
		QSettings s(path, QSettings::IniFormat);
		const auto p = load(s);
		check(p.provider == "friends" && p.panels.size() == 3 && p.url() == B && zoomPercent(p) == 125,
		      "restart IDs/order/URL/size");
		check(findPanel(p, "team")->url == A && findPanel(p, "lumia")->zoomSteps == -4,
		      "encoded links independent sizes survive");
		check(s.value("chat/draft").toString() == "keep draft" &&
			      s.value("show/stage").toString() == "Starting",
		      "native/show settings unchanged");
		save(s, removeProvider(p, "friends"));
	}
	{
		QSettings s(path, QSettings::IniFormat);
		auto p = load(s);
		check(p.provider == "native" && !findPanel(p, "friends") && p.panels.size() == 2,
		      "removal survives restart");
	}
	for (const auto &json : {QString("[]"), QString("{broken"), QString("{}"), QString("null"), QString()}) {
		{
			QSettings s(path, QSettings::IniFormat);
			s.setValue("chat/panel_version", 3);
			s.setValue("chat/panel_provider", "lumia");
			s.setValue("chat/lumia_enabled", true);
			if (json.isNull()) {
				s.remove("chat/panels_json");
			} else {
				s.setValue("chat/panels_json", json);
			}
			s.sync();
		}
		auto before = bytes(path);
		{
			QSettings s(path, QSettings::IniFormat);
			auto p = load(s);
			check(p.panels.isEmpty() && p.provider == "native",
			      "authoritative corrupt/empty/missing schema3 never resurrects legacy");
		}
		check(bytes(path) == before, "bad schema3 load read-only");
	}
	{
		QSettings s(path, QSettings::IniFormat);
		QJsonArray rows{
			QJsonObject{{"id", "lumia"}, {"name", "Renamed"}, {"url", LumiaDockUrl}},
			QJsonObject{{"id", "other"}, {"name", "Other"}, {"url", A}},
			QJsonObject{{"id", "zero"}, {"name", "Explicit"}, {"url", LumiaDockUrl}, {"zoomSteps", 0}},
			QJsonObject{{"id", "badsize"},
				    {"name", "Bad size"},
				    {"url", LumiaDockUrl},
				    {"zoomSteps", "oops"}}};
		s.setValue("chat/panels_json", QString::fromUtf8(QJsonDocument(rows).toJson(QJsonDocument::Compact)));
		const auto p = load(s);
		check(findPanel(p, "lumia")->zoomSteps == -4 && findPanel(p, "other")->zoomSteps == 0 &&
			      findPanel(p, "badsize")->zoomSteps == -4,
		      "missing/nonnumeric sizes infer only Lumia67");
		check(findPanel(p, "zero")->zoomSteps == 0, "explicit zero remains100");
		save(s, p);
	}
	{
		QSettings s(path, QSettings::IniFormat);
		const auto p = load(s);
		check(findPanel(p, "zero")->zoomSteps == 0 && findPanel(p, "lumia")->zoomSteps == -4,
		      "explicit zero and inferred default survive restart independently");
	}
}
static void migration(const QString &dir)
{
	struct Row {
		QString active, url, other;
		int enabled, version, zoom;
		bool lumia, custom;
		QString selected;
	};
	const QString widget = "https://lumiastream.com/widget/fixture?room=A%2FB#chat";
	const Row rows[]{{"native", LumiaControlRoomUrl, {}, -1, 0, -999, false, false, "native"},
			 {"lumia", LumiaControlRoomUrl, A, -1, 0, -999, true, true, "lumia"},
			 {"custom", LumiaDockUrl, A, -1, 2, 0, false, true, "custom"},
			 {"native", widget, A, -1, 2, -3, true, true, "native"},
			 {"lumia", widget, A, 0, 2, -3, false, true, "native"},
			 {"native", widget, {}, 0, 2, -3, false, false, "native"},
			 {"lumia", LumiaControlRoomUrl, {}, 1, 2, 0, true, false, "lumia"},
			 {"custom", LumiaDockUrl, "javascript:alert(1)", 0, 2, -999, false, false, "native"}};
	for (int i = 0; i < int(sizeof(rows) / sizeof(rows[0])); ++i) {
		const auto &r = rows[i];
		QString path = dir + "/migration-" + QString::number(i) + ".ini";
		{
			QSettings s(path, QSettings::IniFormat);
			s.setValue("chat/panel_provider", r.active);
			s.setValue("chat/lumia_dock_url", r.url);
			s.setValue("chat/custom_dock_url", r.other);
			s.setValue("chat/panel_version", r.version);
			s.setValue("chat/custom_zoom_steps", 2);
			if (r.enabled != -1) {
				s.setValue("chat/lumia_enabled", r.enabled == 1);
			}
			if (r.zoom != -999) {
				s.setValue("chat/lumia_zoom_steps", r.zoom);
			}
			s.sync();
		}
		auto before = bytes(path);
		Preferences p;
		{
			QSettings s(path, QSettings::IniFormat);
			p = load(s);
			check(bool(findPanel(p, "lumia")) == r.lumia && bool(findPanel(p, "custom")) == r.custom &&
				      p.provider == r.selected,
			      "legacy membership/explicit removals/active preserved");
			if (auto *item = findPanel(p, "lumia")) {
				check(item->zoomSteps == (r.zoom == -999 ? -4 : r.zoom),
				      "legacy missing67 explicitzero100");
				check(item->url == (r.version < 2 && isGenericLumiaLink(r.url) ? LumiaDockUrl : r.url),
				      "old generic migration but explicit full/widget/query retained");
			}
			if (auto *item = findPanel(p, "custom")) {
				check(item->url == r.other && item->zoomSteps == 2,
				      "legacy other URL and independent size retained");
			}
			check(!s.contains("chat/panels_json"), "legacy load does not save conversion");
		}
		check(bytes(path) == before, "legacy file untouched on load");
		{
			QSettings s(path, QSettings::IniFormat);
			save(s, p);
		}
		{
			QSettings s(path, QSettings::IniFormat);
			auto again = load(s);
			check(again.provider == p.provider && enabledProviders(again) == enabledProviders(p),
			      "migration stable after save/restart");
		}
	}
}
static void zoom()
{
	auto p = fixture("lumia");
	check(zoomPercent(p) == 67, "Lumia67 retained");
	int last = 0;
	for (int i = -7; i <= 8; ++i) {
		setZoomSteps(p, i);
		check(zoomSteps(p) == i && zoomPercent(p) > last, "supported sizes distinct increasing");
		last = zoomPercent(p);
	}
	setZoomSteps(p, -999);
	check(zoomSteps(p) == -7 && zoomPercent(p) == 25, "minimum clamp");
	setZoomSteps(p, 999);
	check(zoomSteps(p) == 8 && zoomPercent(p) == 400, "maximum clamp");
	setZoomSteps(p, 0);
	check(zoomPercent(p) == 100, "reset100");
	setZoomSteps(p, -4);
	p.provider = "team";
	check(zoomPercent(p) == 100, "other default100 unchanged");
	setZoomSteps(p, -2);
	check(findPanel(p, "lumia")->zoomSteps == -4 && findPanel(p, "friends")->zoomSteps == 2,
	      "selected size independent");
	p.provider = "native";
	setZoomSteps(p, 6);
	check(zoomPercent(p) == 100 && findPanel(p, "team")->zoomSteps == -2, "native does not mutate external sizes");
}
static void lifecycle()
{
	const auto native = fixture(), lumia = fixture("lumia"), other = fixture("friends");
	const auto invalid = normalize({"unsafe", {{"unsafe", "Unsafe", "javascript:alert(1)", 0}}});
	for (bool ready : {false, true}) {
		for (bool available : {false, true}) {
			for (bool show : {false, true}) {
				for (const auto &url : {QString(), LumiaDockUrl, A}) {
					check(browserAction(native, ready, available, show, url) ==
						      BrowserAction::Release,
					      "native release all states");
					check(browserAction(invalid, ready, available, show, url) ==
						      BrowserAction::Release,
					      "invalid never creates/navigates");
				}
			}
		}
	}
	for (const auto &p : {lumia, other}) {
		for (bool ready : {false, true}) {
			for (bool available : {false, true}) {
				for (const auto &url : {QString(), LumiaDockUrl, A}) {
					check(browserAction(p, ready, available, false, url) == BrowserAction::Release,
					      "leaving Show releases");
				}
			}
		}
		for (bool available : {false, true}) {
			for (const auto &url : {QString(), LumiaDockUrl, A}) {
				check(browserAction(p, false, available, true, url) == BrowserAction::Wait,
				      "restored panel waits startup");
			}
		}
		for (const auto &url : {QString(), LumiaDockUrl, A}) {
			check(browserAction(p, true, false, true, url) == BrowserAction::Unavailable,
			      "unavailable does not create");
		}
		check(browserAction(p, true, true, true, {}) == BrowserAction::Create, "create after readiness");
		check(browserAction(p, true, true, true, p.url()) == BrowserAction::Keep, "same target no reload");
		check(browserAction(p, true, true, true, "https://old.example.invalid/chat") == BrowserAction::Navigate,
		      "new target reuses browser");
	}
}
int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	QTemporaryDir dir;
	check(dir.isValid(), "temporary directory available");
	links();
	panels();
	storage(dir.path());
	migration(dir.path());
	zoom();
	lifecycle();
	std::printf(
		"PASS: %d Chat panel preference and lifecycle checks; no browser, platform calls or installed settings used.\n",
		checks);
	return 0;
}
