#pragma once

#include "PulseYouTubeRegistration.hpp"
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <functional>

namespace PulseYouTubeApplicationUi {
inline void showGuide(QWidget *parent)
{
    QDialog dialog(parent);
    dialog.setWindowTitle("Set up your YouTube app");
    dialog.resize(650, 490);
    auto *layout = new QVBoxLayout(&dialog);
    auto *progress = new QLabel;
    progress->setObjectName("PulseWeaverMuted");
    layout->addWidget(progress);
    auto *pages = new QStackedWidget;
    pages->setObjectName("YouTubeAppGuidePages");
    struct Step { const char *title; const char *body; };
    const Step steps[] = {
        {"Create a project and enable YouTube",
         "Open the <a href='https://console.cloud.google.com/projectcreate'>Google Cloud Console</a> and create "
         "a project, for example <b>My Pulse Weaver</b>. Keep this project selected throughout setup.<br><br>"
         "Go to <b>APIs &amp; Services → Library</b>, search for <b>YouTube Data API v3</b>, then choose <b>Enable</b>. "
         "You do not need a separate API key for Pulse Weaver.<br><br>"
         "A separate project gives you your own quota. Creating another client in Pulse Weaver's project "
         "would still share that project's allowance."},
        {"Configure Google sign-in",
         "Open <b>Google Auth platform → Branding</b>. If prompted, choose <b>Get started</b>. "
         "Enter an app name, your support email and your contact email.<br><br>"
         "Choose <b>External</b> for a personal Google account. Finish Google's setup, then open "
         "<b>Audience → Test users → Add users</b> and add the Google account you use for YouTube.<br><br>"
         "Keep the project in <b>Testing</b> while setting it up. Only your listed test users can sign in. "
         "Google Workspace users may have an Internal option, subject to their administrator's rules."},
        {"Add the YouTube permission",
         "Open <b>Google Auth platform → Data Access → Add or remove scopes</b>. Select or manually add "
         "this scope, then update and save:<br><br>"
         "<b>https://www.googleapis.com/auth/youtube.force-ssl</b><br><br>"
         "Pulse Weaver requests this permission to manage broadcasts, send chat messages and perform "
         "moderation you choose. Review the requested access when signing in. "
         "YouTube Live must also be enabled on your channel; first-time activation can take up to 24 hours."},
        {"Create a Desktop app client",
         "Open <b>Google Auth platform → Clients → Create client</b>. "
         "Set <b>Application type</b> to <b>Desktop app</b>, name it and choose <b>Create</b>.<br><br>"
         "Copy its <b>Client ID</b> and <b>Client secret</b>. Google may also offer a JSON download containing "
         "these values under <b>installed</b>. Keep that file private.<br><br>"
         "Use Desktop app, not Web application, API key or service account. Pulse Weaver opens your normal "
         "browser and receives sign-in through a temporary local loopback address; there is no website "
         "redirect URL to register."},
        {"Save the details and connect",
         "Return to <b>Action → Connections → YouTube → Your YouTube app</b>. "
         "If YouTube is already connected, use <b>Disconnect &amp; revoke</b> first, while your outputs are stopped.<br><br>"
         "Paste both values and select <b>Save app details</b>. Then select <b>Connect YouTube in browser</b> "
         "and sign in with your test-user account. Choose the intended YouTube channel and review Google's permissions.<br><br>"
         "The client secret is masked and protected locally by Windows. To return to the built-in app, "
         "disconnect, clear both fields and save. Its bundled details are never filled into these fields."},
        {"Know the limits and keep access working",
         "Your project still has daily limits. Check <b>APIs &amp; Services → YouTube Data API v3 → "
         "Quotas &amp; System Limits</b> in Google Cloud. Quota resets at <b>midnight Pacific Time</b>; "
         "reconnecting does not refill it. Sending, moderation and broadcast management consume API quota. "
         "An existing broadcast is not stopped just because control requests run out.<br><br>"
         "With an External app in <b>Testing</b>, Google generally expires this YouTube refresh token after "
         "<b>7 days</b>; reconnect when needed. For ongoing use, review Google's publishing and verification "
         "requirements before changing Audience to In production. This does not grant unlimited quota.<br><br>"
         "Official help: <a href='https://developers.google.com/identity/protocols/oauth2/native-app'>Desktop OAuth</a> · "
         "<a href='https://developers.google.com/workspace/guides/configure-oauth-consent'>Consent setup</a> · "
         "<a href='https://developers.google.com/youtube/data/determine_quota_cost'>Quota costs</a>"}
    };
    for (const auto &step : steps) {
        auto *page = new QWidget;
        auto *bodyLayout = new QVBoxLayout(page);
        auto *title = new QLabel(QString("<h3>%1</h3>").arg(step.title));
        title->setWordWrap(true);
        bodyLayout->addWidget(title);
        auto *body = new QLabel(step.body);
        body->setWordWrap(true);
        body->setOpenExternalLinks(true);
        body->setTextInteractionFlags(Qt::TextBrowserInteraction);
        bodyLayout->addWidget(body);
        bodyLayout->addStretch();
        auto *scroll = new QScrollArea;
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setWidget(page);
        pages->addWidget(scroll);
    }
    layout->addWidget(pages, 1);
    auto *navigation = new QHBoxLayout;
    auto *back = new QPushButton("Back");
    back->setObjectName("YouTubeAppGuideBack");
    auto *next = new QPushButton("Next");
    next->setObjectName("YouTubeAppGuideNext");
    auto *close = new QPushButton("Close guide");
    navigation->addWidget(close);
    navigation->addStretch();
    navigation->addWidget(back);
    navigation->addWidget(next);
    layout->addLayout(navigation);
    auto update = [=] {
        progress->setText(QString("STEP %1 OF %2").arg(pages->currentIndex() + 1).arg(pages->count()));
        back->setEnabled(pages->currentIndex() > 0);
        next->setText(pages->currentIndex() == pages->count() - 1 ? "Done" : "Next");
    };
    QObject::connect(back, &QPushButton::clicked, &dialog, [=] {
        pages->setCurrentIndex(pages->currentIndex() - 1); update();
    });
    QObject::connect(next, &QPushButton::clicked, &dialog, [&dialog, pages, update] {
        if (pages->currentIndex() == pages->count() - 1) dialog.accept();
        else { pages->setCurrentIndex(pages->currentIndex() + 1); update(); }
    });
    QObject::connect(close, &QPushButton::clicked, &dialog, &QDialog::reject);
    update();
    dialog.exec();
}

// The frontend owns the save callback; the connections page hosts this widget.
// Only the user's override is passed in. Bundled credentials never enter the UI.
class Editor : public QWidget {
public:
    Editor(const PulseYouTubeRegistration::Registration &saved,
           std::function<QString(const PulseYouTubeRegistration::Registration &)> save,
           QWidget *parent = nullptr) : QWidget(parent)
    {
        setObjectName("PulseWeaverYouTubeApplicationEditor");
        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 8, 0, 0);
        auto *notice = new QLabel(
            "<b>YouTube API quota is limited and shared daily.</b><br>"
            "The built-in app shares its daily allowance across Pulse Weaver users. If it runs out, "
            "API controls such as sending chat, moderation and broadcast setup stop working until the "
            "quota resets at midnight Pacific Time. An existing broadcast is not automatically stopped.<br><br>"
            "For your own allowance, we recommend registering your own Google Cloud app. "
            "Your project still has limits; the guide walks you through setup.");
        notice->setObjectName("YouTubeQuotaNotice");
        notice->setWordWrap(true);
        notice->setMinimumWidth(0);
        notice->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        layout->addWidget(notice);
        auto *row = new QHBoxLayout;
        auto *expand = new QPushButton("Your YouTube app · optional");
        expand->setObjectName("YouTubeAppExpand");
        expand->setCheckable(true);
        expand->setChecked(!saved.clientId.isEmpty() || !saved.clientSecret.isEmpty());
        auto *guide = new QPushButton("Step-by-step guide");
        guide->setObjectName("YouTubeAppGuide");
        row->addWidget(expand);
        row->addStretch();
        row->addWidget(guide);
        layout->addLayout(row);
        auto *mode = new QLabel;
        mode->setObjectName("YouTubeAppMode");
        mode->setWordWrap(true);
        auto updateMode = [mode](bool custom) {
            mode->setText(custom ? "Your app · uses your Google Cloud project's daily quota" :
                "Shared app · leave both fields empty to use Pulse Weaver's built-in connection");
        };
        updateMode(!saved.clientId.isEmpty());
        layout->addWidget(mode);
        auto *details = new QWidget;
        details->setObjectName("YouTubeAppFields");
        auto *form = new QFormLayout(details);
        form->setContentsMargins(0, 4, 0, 0);
        form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
        auto *id = new QLineEdit(saved.clientId);
        id->setObjectName("YouTubeCustomClientId");
        id->setAccessibleName("Your YouTube OAuth client ID");
        id->setPlaceholderText("Desktop app client ID · optional");
        id->setMaxLength(512);
        auto *secret = new QLineEdit(saved.clientSecret);
        secret->setObjectName("YouTubeCustomClientSecret");
        secret->setAccessibleName("Your YouTube OAuth client secret");
        secret->setEchoMode(QLineEdit::Password);
        secret->setPlaceholderText("Desktop app client secret · optional");
        secret->setMaxLength(4096);
        form->addRow("Client ID", id);
        form->addRow("Client secret", secret);
        auto *hint = new QLabel("Disconnect YouTube before saving different app details. Your secret is masked and protected locally by Windows.");
        hint->setWordWrap(true);
        form->addRow(hint);
        auto *actions = new QHBoxLayout;
        auto *clear = new QPushButton("Clear fields");
        clear->setObjectName("YouTubeAppClear");
        auto *apply = new QPushButton("Save app details");
        apply->setObjectName("YouTubeAppSave");
        actions->addWidget(clear);
        actions->addStretch();
        actions->addWidget(apply);
        form->addRow(actions);
        auto *result = new QLabel;
        result->setObjectName("YouTubeAppResult");
        result->setTextFormat(Qt::PlainText);
        result->setWordWrap(true);
        form->addRow(result);
        layout->addWidget(details);
        details->setVisible(expand->isChecked());
        QObject::connect(expand, &QPushButton::toggled, details, &QWidget::setVisible);
        QObject::connect(guide, &QPushButton::clicked, this, [this] { showGuide(this); });
        QObject::connect(clear, &QPushButton::clicked, this, [=] {
            id->clear(); secret->clear(); result->setText("Both fields cleared. Save to use the shared app.");
        });
        QObject::connect(apply, &QPushButton::clicked, this, [=] {
            const PulseYouTubeRegistration::Registration value{id->text().trimmed(), secret->text().trimmed()};
            QString error = PulseYouTubeRegistration::validationError(value);
            if (error.isEmpty()) error = save(value);
            if (!error.isEmpty()) { result->setText(error); return; }
            id->setText(value.clientId); secret->setText(value.clientSecret);
            updateMode(!value.clientId.isEmpty());
            result->setText("App details saved. Select Connect YouTube in browser to sign in.");
        });
    }
};
}
