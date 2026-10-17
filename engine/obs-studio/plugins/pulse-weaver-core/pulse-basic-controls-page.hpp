#pragma once

#include "pulse-basic-controls.hpp"
#include <QClipboard>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFontDatabase>
#include <QFormLayout>
#include <QGroupBox>
#include <QGridLayout>
#include <QGuiApplication>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPainter>
#include <QPointer>
#include <QScrollArea>
#include <QStackedWidget>
#include <QStyleOptionButton>
#include <QTextBrowser>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <memory>

namespace PulseBasicControls {
// QStackedWidget normally reserves the largest hidden page's minimum size.
// Category cards should use their own footprint, rather than a future editor's.
class CurrentPageStack final : public QStackedWidget {
	QSize withFrameInsets(const QSize &pageSize) const
	{
		// QFrame includes the active theme's border and padding in these margins.
		// Hidden pages must not affect the hint, but the stack's own insets must.
		const auto margins = contentsMargins();
		return pageSize + QSize(margins.left() + margins.right(), margins.top() + margins.bottom());
	}
public:
	explicit CurrentPageStack(QWidget *parent) : QStackedWidget(parent)
	{
		layout()->setContentsMargins(0, 0, 0, 0);
		layout()->setSizeConstraint(QLayout::SetNoConstraint);
		setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
		connect(this, &QStackedWidget::currentChanged, this, [this] { updateGeometry(); });
	}
	QSize sizeHint() const override { return withFrameInsets(currentWidget() ? currentWidget()->sizeHint() : QSize()); }
	QSize minimumSizeHint() const override { return withFrameInsets(currentWidget() ? currentWidget()->minimumSizeHint() : QSize()); }
};

class CategoryCard final : public QPushButton {
	QString detail;
protected:
	void paintEvent(QPaintEvent *) override
	{
		QStyleOptionButton option; initStyleOption(&option); option.text.clear();
		QPainter painter(this); style()->drawControl(QStyle::CE_PushButton, &option, &painter, this);
		const QRect area = rect().adjusted(12, 8, -26, -8);
		QFont title = font(); title.setBold(true); title.setPixelSize(13); painter.setFont(title);
		painter.setPen(palette().color(QPalette::ButtonText));
		painter.drawText(QRect(area.x(), area.y(), area.width(), 20), Qt::AlignLeft | Qt::AlignVCenter, text());
		QFont description = font(); description.setPixelSize(11); painter.setFont(description);
		painter.setPen(palette().color(QPalette::PlaceholderText));
		painter.drawText(QRect(area.x(), area.y() + 23, area.width(), 25), Qt::AlignLeft | Qt::TextWordWrap, detail);
		painter.drawText(rect().adjusted(width() - 24, 0, -10, 0), Qt::AlignCenter, QStringLiteral("›"));
	}
public:
	CategoryCard(const QString &title, const QString &hint, QWidget *parent) : QPushButton(title, parent), detail(hint)
	{
		setObjectName("PulseWeaverCommandCategory"); setFixedHeight(68); setMinimumWidth(180);
		// The inherited theme caps ordinary buttons at a 22px content height.
		// These custom-painted cards need matching style and widget dimensions:
		// 66px of content plus the two 1px border edges equals the 68px card.
		setStyleSheet("QPushButton#PulseWeaverCommandCategory { height: 66px; min-height: 66px; max-height: 66px; margin: 0px; padding: 0px; border-width: 1px; border-style: solid; }");
		setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed); setCursor(Qt::PointingHandCursor);
		setAccessibleDescription(hint); setToolTip(hint); setAutoDefault(false);
	}
};

inline QJsonObject example(const QString &command, const Context &context)
{
	QJsonObject request{{"command", command}};
	if (command == "stage.select")
		request.insert("name", context.stages && context.stages->count() ? stageName(context.stages, context.stages->currentIndex() < 0 ? 0 : context.stages->currentIndex()) : QString("Game"));
	if (command.startsWith("source.")) {
		request.insert("sceneId", "SCENE-OR-GROUP-UUID");
		request.insert("itemId", "1");
	}
	if (command.startsWith("audio.")) request.insert("sourceId", "AUDIO-SOURCE-UUID");
	if (command == "audio.volume") request.insert("volume", 75);
	return request;
}

class Page final : public QWidget {
	QLineEdit *endpoint;
	QLabel *connection;
public:
	QTreeWidget *modules;
	Page(const QString &token, quint16 port, std::function<Context()> context, QWidget *parent = nullptr) : QWidget(parent)
	{
		setObjectName("PulseWeaverBasicCommandsPage");
		auto *root = new QVBoxLayout(this); root->setContentsMargins(0, 0, 0, 0);
		auto *scroll = new QScrollArea(this); scroll->setWidgetResizable(true); scroll->setFrameShape(QFrame::NoFrame); root->addWidget(scroll);
		auto *content = new QWidget(scroll); scroll->setWidget(content);
		auto *layout = new QVBoxLayout(content); layout->setContentsMargins(10, 8, 10, 8); layout->setSpacing(8);
		auto *title = new QLabel("Automation & commands", content); title->setObjectName("Kicker"); layout->addWidget(title);
		auto *intro = new QLabel("Choose a command category, or use these local credentials to connect your action tool.", content);
		intro->setWordWrap(true); intro->setObjectName("Muted"); layout->addWidget(intro);
		auto *connectionGroup = new QGroupBox("Local connection", content); auto *connectionLayout = new QFormLayout(connectionGroup);
		connectionLayout->setContentsMargins(10, 14, 10, 8); connectionLayout->setVerticalSpacing(4);
		auto *endpointRow = new QHBoxLayout;
		endpoint = new QLineEdit("http://127.0.0.1:" + QString::number(port), connectionGroup);
		endpoint->setObjectName("PulseWeaverControlEndpoint"); endpoint->setReadOnly(true); endpoint->setAccessibleName("Local automation endpoint");
		auto *copyEndpoint = new QPushButton("Copy", connectionGroup); copyEndpoint->setAccessibleName("Copy local automation endpoint");
		endpointRow->addWidget(endpoint, 1); endpointRow->addWidget(copyEndpoint);
		connect(copyEndpoint, &QPushButton::clicked, this, [this] { QGuiApplication::clipboard()->setText(endpoint->text()); });
		connection = new QLabel("Starting local API…", connectionGroup); connection->setObjectName("PulseWeaverControlConnectionStatus"); connection->setWordWrap(true); connection->setMaximumWidth(260);
		endpointRow->addWidget(connection); connectionLayout->addRow("Endpoint", endpointRow);
		auto *tokenRow = new QHBoxLayout;
		auto *credential = new QLineEdit(token, connectionGroup); credential->setReadOnly(true); credential->setEchoMode(QLineEdit::Password);
		credential->setObjectName("PulseWeaverControlToken"); credential->setAccessibleName("Private local automation connection token");
		auto *reveal = new QPushButton("Reveal", connectionGroup); reveal->setCheckable(true); reveal->setObjectName("PulseWeaverRevealControlToken");
		auto *copyToken = new QPushButton("Copy token", connectionGroup); copyToken->setObjectName("PulseWeaverCopyControlToken");
		tokenRow->addWidget(credential, 1); tokenRow->addWidget(reveal); tokenRow->addWidget(copyToken); connectionLayout->addRow("Token", tokenRow);
		connect(reveal, &QPushButton::toggled, this, [credential, reveal](bool shown) {
			credential->setEchoMode(shown ? QLineEdit::Normal : QLineEdit::Password); reveal->setText(shown ? "Hide" : "Reveal");
		});
		connect(copyToken, &QPushButton::clicked, this, [credential] { QGuiApplication::clipboard()->setText(credential->text()); });
		credential->setToolTip("Use Authorization: Bearer <your token>. Keep the token private; it controls this local installation.");
		copyToken->setToolTip("Copy this installation's private local API token"); layout->addWidget(connectionGroup);
		auto *commandGroup = new QGroupBox("Basic commands", content); auto *commandLayout = new QVBoxLayout(commandGroup);
		commandLayout->setContentsMargins(10, 14, 10, 8); commandLayout->setSpacing(6);
		auto *navigator = new CurrentPageStack(commandGroup); navigator->setObjectName("PulseWeaverCommandNavigator"); commandLayout->addWidget(navigator);
		auto *categories = new QWidget(navigator); auto *categoriesLayout = new QGridLayout(categories); categoriesLayout->setContentsMargins(0, 0, 0, 0); categoriesLayout->setSpacing(6); categoriesLayout->setAlignment(Qt::AlignTop); navigator->addWidget(categories);
		auto *examplePage = new QWidget(navigator); auto *exampleLayout = new QVBoxLayout(examplePage); exampleLayout->setContentsMargins(0, 0, 0, 0); navigator->addWidget(examplePage);
		auto returnPage = std::make_shared<QPointer<QWidget>>(categories);
		auto *backExample = new QPushButton("Back to commands", examplePage); exampleLayout->addWidget(backExample, 0, Qt::AlignLeft);
		auto *selectedTitle = new QLabel(examplePage); selectedTitle->setObjectName("Kicker"); exampleLayout->addWidget(selectedTitle);
		connect(backExample, &QPushButton::clicked, this, [navigator, returnPage] { if (*returnPage) navigator->setCurrentWidget(*returnPage); });
		const QStringList labels{"Choose Stage", "Next Stage", "Previous Stage", "Show source item", "Hide source item", "Toggle source item",
			"Mute audio source", "Unmute audio source", "Toggle source mute", "Set audio volume", "Start selected recording mode", "Stop recording", "Save replay"};
		const auto catalogue = commands();
		auto *json = new QPlainTextEdit(examplePage); json->setReadOnly(true); json->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
		json->setObjectName("PulseWeaverBasicCommandJson"); json->setAccessibleName("JSON command example; not executed"); json->setMinimumHeight(96); json->setMaximumHeight(150); exampleLayout->addWidget(json);
		const QStringList categoryNames{"Stages", "Sources", "Audio", "Recording & replay"};
		const QStringList categoryHints{"Choose a Stage or move through your show", "Show or hide an existing scene item",
			"Mute a source or set its volume", "Use your saved recording mode or save a replay"};
		const int starts[] = {0, 3, 6, 10}, ends[] = {3, 6, 10, 13};
		for (int category = 0; category < categoryNames.size(); ++category) {
			auto *tile = new CategoryCard(categoryNames[category], categoryHints[category], categories);
			categoriesLayout->addWidget(tile, category / 2, category % 2);
			auto *choices = new QWidget(navigator); auto *choicesLayout = new QVBoxLayout(choices); choicesLayout->setContentsMargins(0, 0, 0, 0); navigator->addWidget(choices);
			auto *back = new QPushButton("Back to categories", choices); choicesLayout->addWidget(back, 0, Qt::AlignLeft);
			auto *categoryTitle = new QLabel(categoryNames[category], choices); categoryTitle->setObjectName("Kicker"); choicesLayout->addWidget(categoryTitle);
			auto *buttons = new QGridLayout; choicesLayout->addLayout(buttons);
			connect(tile, &QPushButton::clicked, this, [navigator, choices] { navigator->setCurrentWidget(choices); });
			connect(back, &QPushButton::clicked, this, [navigator, categories] { navigator->setCurrentWidget(categories); });
			for (int index = starts[category]; index < ends[category]; ++index) {
				const QString command = catalogue[index].toObject().value("command").toString(), label = labels[index];
				auto *choice = new QPushButton(label, choices); choice->setMinimumHeight(42); choice->setProperty("pulseCommand", command); choice->setObjectName("PulseWeaverCommandChoice");
				buttons->addWidget(choice, (index - starts[category]) / 2, (index - starts[category]) % 2);
				connect(choice, &QPushButton::clicked, this, [navigator, choices, returnPage, selectedTitle, json, examplePage, command, label, context] {
					*returnPage = choices; selectedTitle->setText(label);
					json->setPlainText(QString::fromUtf8(QJsonDocument(example(command, context())).toJson(QJsonDocument::Indented)));
					navigator->setCurrentWidget(examplePage);
				});
			}
			choicesLayout->addStretch();
		}
		auto *copyExample = new QPushButton("Copy JSON", examplePage); exampleLayout->addWidget(copyExample, 0, Qt::AlignLeft);
		connect(copyExample, &QPushButton::clicked, this, [json] { QGuiApplication::clipboard()->setText(json->toPlainText()); });
		auto *copyCatalogue = new QPushButton("Copy source IDs & capabilities", examplePage);
		auto *guide = new QPushButton("Connection guide", commandGroup); guide->setObjectName("PulseWeaverControlGuide");
		auto *helpRow = new QHBoxLayout; helpRow->addWidget(copyCatalogue); helpRow->addStretch(); exampleLayout->addLayout(helpRow);
		commandLayout->addWidget(guide, 0, Qt::AlignLeft);
		connect(copyCatalogue, &QPushButton::clicked, this, [context] {
			QGuiApplication::clipboard()->setText(QString::fromUtf8(QJsonDocument(PulseBasicControls::catalogue(context())).toJson(QJsonDocument::Indented)));
		});
		auto *note = new QLabel("POST JSON to /api/v1/control. Source UUIDs and item IDs come from the catalogue. Recording/replay acceptance does not confirm a finished file.", commandGroup);
		note->setWordWrap(true); note->setObjectName("Muted"); exampleLayout->addWidget(note); layout->addWidget(commandGroup);
		connect(guide, &QPushButton::clicked, this, [this] {
			auto *dialog = new QDialog(this); dialog->setAttribute(Qt::WA_DeleteOnClose); dialog->setWindowTitle("Connect an action tool"); dialog->resize(650, 480);
			auto *body = new QVBoxLayout(dialog); auto *text = new QTextBrowser(dialog); text->setOpenExternalLinks(true);
			text->setMarkdown("# Local automation connection\n\n1. Open Streamer.bot and add an action with a Set Argument sub-action named `pulseweaver_request`. Use a JSON example from this page.\n2. Add an Execute C# Code sub-action using the accompanying `integrations/streamerbot/PulseWeaverBasicControls.cs` example.\n3. Set private persistent globals `pulseweaver_host`, `pulseweaver_port` and `pulseweaver_token` to this page's values.\n4. Attach your chosen button, hotkey or trigger. Check `pulseweaver_ok`, `pulseweaver_message` and `pulseweaver_state` in following sub-actions.\n\nAny HTTP-capable local action tool can GET `/api/v1/control` for the catalogue and POST JSON commands to the same path with `Authorization: Bearer <your token>`.\n\nStage/source commands report applied state. Recording and replay requests return acceptance while native validation and file writing may still be running. Start the replay buffer in Pulse Weaver first. Existing Lumia commands retain their own connection and routes.\n\n[Streamer.bot C# HTTP guide](https://docs.streamer.bot/examples/http-post) · [Streamer.bot arguments](https://docs.streamer.bot/api/csharp/guide/variables)");
			body->addWidget(text); auto *close = new QDialogButtonBox(QDialogButtonBox::Close, dialog); body->addWidget(close);
			connect(close, &QDialogButtonBox::rejected, dialog, &QDialog::close); dialog->show();
		});
		auto *developer = new QToolButton(content); developer->setText("Developer API & module actions"); developer->setCheckable(true); developer->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
		developer->setArrowType(Qt::RightArrow); layout->addWidget(developer, 0, Qt::AlignLeft);
		auto *advanced = new QWidget(content); auto *advancedLayout = new QVBoxLayout(advanced); advancedLayout->setContentsMargins(0, 0, 0, 0);
		auto *api = new QLabel("Existing API: GET /api/v1/health, /scenes, /sources, /automations, /overlays, /modules.\n"
			"Editing routes retain /scene/create, /source/create, /source/transform, /source/visibility and /automation.\n"
			"Existing output routes retain /stream/start, /stream/stop, /recording/start and /recording/stop.\n"
			"Plugins publish through pulseweaver_publish and subscribe to pulseweaver_event(platform, type, json).", advanced);
		api->setWordWrap(true); api->setTextInteractionFlags(Qt::TextSelectableByMouse); advancedLayout->addWidget(api);
		modules = new QTreeWidget(advanced); modules->setHeaderLabels({"MODULE", "ACTION", "ID", "STATUS"}); modules->setMinimumHeight(100); modules->setMaximumHeight(200);
		modules->header()->setSectionResizeMode(0, QHeaderView::Stretch); modules->header()->setSectionResizeMode(1, QHeaderView::Stretch); advancedLayout->addWidget(modules);
		layout->addWidget(advanced); advanced->hide(); layout->addStretch();
		connect(developer, &QToolButton::toggled, this, [developer, advanced](bool expanded) {
			developer->setArrowType(expanded ? Qt::DownArrow : Qt::RightArrow); advanced->setVisible(expanded);
		});
	}
	void setConnectionState(bool listening, quint16 port, const QString &error = {})
	{
		if (listening) endpoint->setText("http://127.0.0.1:" + QString::number(port));
		connection->setText(listening ? "Listening · this computer only" : "Unavailable · " + error);
	}
};
} // namespace PulseBasicControls
