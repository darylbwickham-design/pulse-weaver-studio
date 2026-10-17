#pragma once

#include <QGridLayout>
#include <QLabel>
#include <QListWidget>
#include <QPainter>
#include <QPersistentModelIndex>
#include <QPushButton>
#include <QScrollArea>
#include <QStackedWidget>
#include <QStyleOptionButton>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <functional>

namespace PulseLightsNavigation {
class Card final : public QPushButton {
	QString hint;
protected:
	void paintEvent(QPaintEvent *) override
	{
		QStyleOptionButton option; initStyleOption(&option); option.text.clear();
		QPainter painter(this); style()->drawControl(QStyle::CE_PushButton, &option, &painter, this);
		auto title = font(); title.setPixelSize(14); title.setBold(true); painter.setFont(title);
		painter.setPen(palette().color(QPalette::ButtonText));
		painter.drawText(rect().adjusted(14, 10, -30, -42), Qt::AlignLeft | Qt::AlignVCenter, text());
		auto detail = font(); detail.setPixelSize(11); painter.setFont(detail);
		painter.setPen(palette().color(QPalette::PlaceholderText));
		painter.drawText(rect().adjusted(14, 40, -30, -10), Qt::AlignLeft | Qt::TextWordWrap, hint);
		painter.drawText(rect().adjusted(width() - 25, 0, -10, 0), Qt::AlignCenter, QStringLiteral("›"));
	}
public:
	Card(const QString &title, const QString &detail, QWidget *parent) : QPushButton(title, parent), hint(detail)
	{
		setMinimumWidth(200); setFixedHeight(86); setAutoDefault(false); setCursor(Qt::PointingHandCursor);
		setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
		setAccessibleDescription(detail); setToolTip(detail);
		// Override the inherited ordinary-button cap for the complete painted card.
		setStyleSheet("QPushButton { height: 84px; min-height: 84px; max-height: 84px; margin: 0; padding: 0; border-width: 1px; border-style: solid; }");
	}
};

struct Context {
	QWidget *setup;
	QWidget *controls;
	QWidget *looksPage;
	QTreeWidget *devices;
	QListWidget *looks;
	QPushButton *removeDevice;
};

class Page final : public QWidget {
	Context context;
	QStackedWidget *pages;
	QWidget *home;
	QWidget *controlPage;
	QWidget *lookPage;
	QWidget *lookReturn;
	QGridLayout *deviceGrid;
	QGridLayout *lookGrid;
	QLabel *deviceEmpty;
	QLabel *lookEmpty;
	QLabel *loaded;

	static void clearGrid(QGridLayout *grid)
	{
		while (auto *item = grid->takeAt(0)) { delete item->widget(); delete item; }
	}
	QWidget *detail(QWidget *body, const QString &backName, std::function<void()> back)
	{
		auto *page = new QWidget(pages); auto *layout = new QVBoxLayout(page); layout->setContentsMargins(0, 0, 0, 0);
		auto *button = new QPushButton(backName, page); button->setObjectName("PulseWeaverLightBack");
		layout->addWidget(button, 0, Qt::AlignLeft); connect(button, &QPushButton::clicked, this, std::move(back));
		auto *scroll = new QScrollArea(page); scroll->setWidgetResizable(true); scroll->setFrameShape(QFrame::NoFrame);
		scroll->setWidget(body); layout->addWidget(scroll, 1); pages->addWidget(page); return page;
	}
	void rebuildDevices()
	{
		clearGrid(deviceGrid); deviceEmpty->setVisible(context.devices->topLevelItemCount() == 0);
		for (int row = 0; row < context.devices->topLevelItemCount(); ++row) {
			auto *item = context.devices->topLevelItem(row);
			const QPersistentModelIndex index(context.devices->model()->index(row, 0));
			auto *card = new Card(item->text(0), item->text(1) + " · " + item->text(2), home);
			card->setObjectName("PulseWeaverLightDeviceCard"); card->setProperty("pulseLightEndpoint", item->text(2));
			deviceGrid->addWidget(card, row / 2, row % 2);
			connect(card, &QPushButton::clicked, this, [this, index] {
				if (!index.isValid()) return;
				context.devices->setCurrentItem(context.devices->topLevelItem(index.row()));
				pages->setCurrentWidget(controlPage);
			});
		}
	}
	void rebuildLooks()
	{
		clearGrid(lookGrid); lookEmpty->setVisible(context.looks->count() == 0);
		for (int row = 0; row < context.looks->count(); ++row) {
			auto *item = context.looks->item(row); const auto fields = item->data(Qt::UserRole).toString().split('|');
			const QPersistentModelIndex index(context.looks->model()->index(row, 0));
			auto *card = new Card(fields.value(0, item->text()), fields.value(1) + " · " + QString::number(qRound(fields.value(2).toInt() * 100.0 / 255)) + "% · " + (fields.value(3) == "1" ? "On" : "Off"), lookPage);
			card->setObjectName("PulseWeaverLightLookCard"); card->setProperty("pulseLightLook", fields.value(0));
			lookGrid->addWidget(card, row / 2, row % 2);
			connect(card, &QPushButton::clicked, this, [this, index] {
				if (!index.isValid()) return;
				auto *item = context.looks->item(index.row()); context.looks->setCurrentItem(item);
				context.looks->itemDoubleClicked(item); // Existing local load handler; never sends a request.
				loaded->setText("Loaded look: " + item->data(Qt::UserRole).toString().section('|', 0, 0) + ". Apply sends it to your selected device.");
				pages->setCurrentWidget(context.devices->currentItem() ? controlPage : home);
			});
		}
	}
public:
	Page(Context source, QWidget *parent = nullptr) : QWidget(parent), context(source)
	{
		setObjectName("PulseWeaverLightsNavigation");
		auto *root = new QVBoxLayout(this); root->setContentsMargins(0, 0, 0, 0);
		pages = new QStackedWidget(this); pages->setObjectName("PulseWeaverLightingPages"); root->addWidget(pages);
		context.devices->hide(); context.looks->hide();
		home = new QWidget(pages); home->setObjectName("PulseWeaverLightingHome"); pages->addWidget(home);
		auto *homeLayout = new QVBoxLayout(home); homeLayout->setContentsMargins(0, 0, 0, 0);
		auto *heading = new QLabel("Choose a light or display", home); heading->setObjectName("Kicker"); homeLayout->addWidget(heading);
		loaded = new QLabel("Changes stay local until you choose Apply look.", home); loaded->setObjectName("Muted"); loaded->setWordWrap(true); homeLayout->addWidget(loaded);
		auto *scroll = new QScrollArea(home); scroll->setFrameShape(QFrame::NoFrame); scroll->setWidgetResizable(true); homeLayout->addWidget(scroll, 1);
		auto *cards = new QWidget(scroll); scroll->setWidget(cards); auto *cardsLayout = new QVBoxLayout(cards); cardsLayout->setContentsMargins(0, 0, 0, 0);
		deviceEmpty = new QLabel("No saved devices yet. Open Device setup to add a known address.", cards); deviceEmpty->setWordWrap(true); cardsLayout->addWidget(deviceEmpty);
		deviceGrid = new QGridLayout; deviceGrid->setContentsMargins(0, 0, 0, 0); deviceGrid->setSpacing(10); cardsLayout->addLayout(deviceGrid); cardsLayout->addStretch();
		auto *actions = new QHBoxLayout; homeLayout->addLayout(actions);
		auto *setup = new QPushButton("Device setup", home); setup->setObjectName("PulseWeaverLightSetup"); actions->addWidget(setup);
		auto *savedLooks = new QPushButton("Saved looks", home); savedLooks->setObjectName("PulseWeaverLightSavedLooks"); actions->addWidget(savedLooks); actions->addStretch();
		auto *setupPage = detail(context.setup, "‹ Devices", [this] { pages->setCurrentWidget(home); }); setupPage->setObjectName("PulseWeaverLightingSetup");
		controlPage = detail(context.controls, "‹ Devices", [this] { pages->setCurrentWidget(home); }); controlPage->setObjectName("PulseWeaverLightingSelected");
		auto *controlActions = new QHBoxLayout; auto *chooseLook = new QPushButton("Saved looks", controlPage); chooseLook->setObjectName("PulseWeaverLightSavedLooks");
		controlActions->addWidget(chooseLook); controlActions->addStretch(); controlActions->addWidget(context.removeDevice);
		qobject_cast<QVBoxLayout *>(controlPage->layout())->insertLayout(1, controlActions);
		lookPage = detail(context.looksPage, "‹ Back", [this] { pages->setCurrentWidget(lookReturn); }); lookPage->setObjectName("PulseWeaverLightingSavedLooks"); lookReturn = home;
		lookEmpty = new QLabel("No saved looks yet. Set a colour and brightness, then save a name here.", context.looksPage); lookEmpty->setWordWrap(true);
		context.looksPage->layout()->addWidget(lookEmpty); lookGrid = new QGridLayout; lookGrid->setSpacing(10);
		qobject_cast<QVBoxLayout *>(context.looksPage->layout())->addLayout(lookGrid); qobject_cast<QVBoxLayout *>(context.looksPage->layout())->addStretch();
		connect(setup, &QPushButton::clicked, this, [this, setupPage] { pages->setCurrentWidget(setupPage); });
		connect(savedLooks, &QPushButton::clicked, this, [this] { lookReturn = home; pages->setCurrentWidget(lookPage); });
		connect(chooseLook, &QPushButton::clicked, this, [this] { lookReturn = controlPage; pages->setCurrentWidget(lookPage); });
		connect(context.removeDevice, &QPushButton::clicked, this, [this] { pages->setCurrentWidget(home); });
		for (auto *model : {context.devices->model(), context.looks->model()}) {
			connect(model, &QAbstractItemModel::rowsInserted, this, [this] { rebuildDevices(); rebuildLooks(); });
			connect(model, &QAbstractItemModel::rowsRemoved, this, [this] { rebuildDevices(); rebuildLooks(); });
			connect(model, &QAbstractItemModel::dataChanged, this, [this] { rebuildDevices(); rebuildLooks(); });
		}
		rebuildDevices(); rebuildLooks(); pages->setCurrentWidget(home);
	}
};
} // namespace PulseLightsNavigation
