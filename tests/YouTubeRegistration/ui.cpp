#include "../../engine/obs-studio/shared/qt/PulseYouTubeApplicationUi.hpp"
#include <QApplication>
#include <QTimer>
#include <cstdio>
#include <cstdlib>

static int checks = 0;
static void check(bool ok, const char *message)
{
    ++checks;
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    int saves = 0;
    QString failure;
    PulseYouTubeRegistration::Registration received;
    PulseYouTubeApplicationUi::Editor editor({}, [&](const auto &value) {
        ++saves; received = value; return failure;
    });
    editor.resize(650, 520); editor.show(); app.processEvents();
    auto *id = editor.findChild<QLineEdit *>("YouTubeCustomClientId");
    auto *secret = editor.findChild<QLineEdit *>("YouTubeCustomClientSecret");
    auto *expand = editor.findChild<QPushButton *>("YouTubeAppExpand");
    auto *save = editor.findChild<QPushButton *>("YouTubeAppSave");
    auto *clear = editor.findChild<QPushButton *>("YouTubeAppClear");
    auto *mode = editor.findChild<QLabel *>("YouTubeAppMode");
    auto *result = editor.findChild<QLabel *>("YouTubeAppResult");
    check(id && secret && id->text().isEmpty() && secret->text().isEmpty(), "Shared mode never displays bundled values");
    check(secret->echoMode() == QLineEdit::Password, "Custom secret is masked");
    check(!id->isVisible(), "Optional fields start collapsed for the shared app");
    expand->click(); app.processEvents();
    check(id->isVisible() && secret->isVisible(), "One click reveals both credentials");
    id->setText("mine.apps.googleusercontent.com"); save->click();
    check(saves == 0 && result->text().contains("both"), "Incomplete input never reaches persistence");
    secret->setText("synthetic-secret");
    failure = "Disconnect YouTube first"; save->click();
    check(saves == 1 && result->text() == failure && mode->text().startsWith("Shared app"),
          "Rejected live/account change preserves the selected application");
    failure.clear(); save->click();
    check(saves == 2 && received.clientId == id->text() && mode->text().startsWith("Your app"), "Complete override is applied together");
    clear->click();
    check(id->text().isEmpty() && secret->text().isEmpty() && saves == 2, "Clearing fields does not silently change a live connection");
    save->click();
    check(saves == 3 && received.clientId.isEmpty() && received.clientSecret.isEmpty() && mode->text().startsWith("Shared app"),
          "Saving two blank fields restores the shared application");
    for (int width : {560, 650, 720}) {
        editor.resize(width, 540); app.processEvents();
        for (auto *button : editor.findChildren<QPushButton *>())
            if (button->isVisible()) {
                check(button->fontMetrics().horizontalAdvance(button->text()) + 16 <= button->width(), "Button labels fit at supported card widths");
                check(button->mapTo(&editor, QPoint(button->width(), 0)).x() <= editor.width(), "Buttons remain within the card");
            }
    }
    PulseYouTubeApplicationUi::Editor custom({"saved.apps.googleusercontent.com", "saved-secret"}, [](const auto &) { return QString(); });
    custom.show(); app.processEvents();
    check(custom.findChild<QLineEdit *>("YouTubeCustomClientId")->isVisible() &&
          custom.findChild<QLineEdit *>("YouTubeCustomClientSecret")->text() == "saved-secret", "Saved custom details remain editable, with masked secret");
    QTimer::singleShot(0, [] {
        auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        check(dialog, "Guide opens modally");
        auto *pages = dialog->findChild<QStackedWidget *>("YouTubeAppGuidePages");
        auto *next = dialog->findChild<QPushButton *>("YouTubeAppGuideNext");
        auto *back = dialog->findChild<QPushButton *>("YouTubeAppGuideBack");
        check(pages && pages->count() == 6 && !back->isEnabled(), "Guide starts on the first of six click-through steps");
        next->click(); back->click();
        check(pages->currentIndex() == 0, "Guide supports moving back without losing its place");
        for (int step = 0; step < 5; ++step) next->click();
        check(next->text() == "Done", "Last step gives a clear completion action");
        next->click();
    });
    editor.findChild<QPushButton *>("YouTubeAppGuide")->click();
    check(saves == 3, "Reading or closing the guide never changes credentials");
    std::printf("PASS: %d credential UI, masking, validation, layout and guide checks\n", checks);
}
