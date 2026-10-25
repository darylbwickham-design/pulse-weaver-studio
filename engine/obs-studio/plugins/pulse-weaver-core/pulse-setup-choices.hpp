#pragma once

#include <QAbstractItemModel>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFontMetrics>
#include <QGridLayout>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPainterPath>
#include <QPersistentModelIndex>
#include <QPointer>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollArea>
#include <QScreen>
#include <QSignalBlocker>
#include <QStyleOptionButton>
#include <QTextLayout>
#include <QVBoxLayout>
#include <QtAlgorithms>
#include <QtMath>
#include <functional>

namespace PulseSetupChoices {

// A real checkable button retains native keyboard/accessibility state. The style
// draws its raised surface; the same text layout measures and paints its content.
class ChoiceCard final : public QPushButton {
	QString detail_;
	QPointer<QWidget> model_;
	bool modelItemEnabled_ = true;
	bool include_ = false;
	static constexpr int Side = 18, Top = 15, Indicator = 18, Gap = 12;

	QFont detailFont() const
	{
		auto result = font(); result.setWeight(QFont::Normal);
		if (result.pixelSize() > 0) result.setPixelSize(qMax(11, result.pixelSize() - 1));
		else if (result.pointSizeF() > 0) result.setPointSizeF(qMax(9.0, result.pointSizeF() - 1.0));
		return result;
	}

	qreal textLayout(const QString &text, const QFont &textFont, int width,
		QPainter *painter = nullptr, const QPointF &origin = {}) const
	{
		if (text.isEmpty()) return 0;
		qreal height = 0;
		for (const auto &paragraph : text.split('\n')) {
			if (paragraph.isEmpty()) { height += QFontMetrics(textFont).height(); continue; }
			QTextLayout layout(paragraph, textFont);
			QTextOption option; option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
			option.setTextDirection(layoutDirection()); layout.setTextOption(option);
			qreal paragraphHeight = 0; layout.beginLayout();
			while (true) {
				auto line = layout.createLine(); if (!line.isValid()) break;
				line.setLineWidth(qMax(1, width)); line.setPosition(QPointF(0, paragraphHeight));
				paragraphHeight += line.height();
			}
			layout.endLayout();
			if (painter) layout.draw(painter, origin + QPointF(0, height));
			height += paragraphHeight;
		}
		return height;
	}

public:
	ChoiceCard(QWidget *parent, const QString &title, const QString &detail) : QPushButton(title, parent), detail_(detail)
	{
		setObjectName("PulseWeaverSetupChoiceCard"); setCheckable(true);
		setAutoDefault(false); setFocusPolicy(Qt::StrongFocus); setCursor(Qt::PointingHandCursor);
		setAttribute(Qt::WA_LayoutUsesWidgetRect);
		auto titleFont = font(); titleFont.setWeight(QFont::DemiBold); setFont(titleFont);
		setAccessibleName(title); setAccessibleDescription(detail);
		setProperty("pulseSetupDetail", detail);
		setToolTip(detail.isEmpty() ? title : title + "\n" + detail);
		auto policy = QSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred); policy.setHeightForWidth(true); setSizePolicy(policy);
	}

	void setIncludeIndicator(bool include) { include_ = include; update(); }
	void followEnabledState(QWidget *model, bool itemEnabled = true)
	{
		model_ = model; modelItemEnabled_ = itemEnabled; setEnabled(model && model->isEnabled() && itemEnabled);
		if (model) {
			model->installEventFilter(this);
			QObject::connect(model, &QObject::destroyed, this, [this] { setEnabled(false); });
		}
	}
	bool hasHeightForWidth() const override { return true; }
	int heightForWidth(int width) const override
	{
		const int textWidth = qMax(1, width - Side * 2 - Indicator - Gap);
		const qreal titleHeight = textLayout(text(), font(), textWidth);
		const qreal detailHeight = textLayout(detail_, detailFont(), textWidth);
		return qMax(72, qCeil(Top * 2 + qMax(qreal(Indicator), titleHeight) + (detailHeight > 0 ? 6 + detailHeight : 0)));
	}
	QSize sizeHint() const override { return QSize(300, heightForWidth(300)); }
	QSize minimumSizeHint() const override { return QSize(160, 72); }

protected:
	bool eventFilter(QObject *watched, QEvent *event) override
	{
		if (watched == model_.data() && event->type() == QEvent::EnabledChange) setEnabled(model_ && model_->isEnabled() && modelItemEnabled_);
		return QPushButton::eventFilter(watched, event);
	}
	void changeEvent(QEvent *event) override
	{
		QPushButton::changeEvent(event);
		if (event->type() == QEvent::FontChange || event->type() == QEvent::StyleChange || event->type() == QEvent::LayoutDirectionChange)
			updateGeometry();
	}
	void keyPressEvent(QKeyEvent *event) override
	{
		if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) { click(); event->accept(); return; }
		QPushButton::keyPressEvent(event);
	}
	void paintEvent(QPaintEvent *) override
	{
		QStyleOptionButton option; initStyleOption(&option); option.text.clear(); option.icon = QIcon();
		QPainter painter(this); painter.setRenderHint(QPainter::Antialiasing);
		style()->drawControl(QStyle::CE_PushButton, &option, &painter, this);
		const auto group = isEnabled() ? (isActiveWindow() ? QPalette::Active : QPalette::Inactive) : QPalette::Disabled;
		const QColor foreground = palette().color(group, QPalette::ButtonText);
		const bool rtl = layoutDirection() == Qt::RightToLeft;
		const bool navigation = !isCheckable();
		const int indicatorX = navigation ? (rtl ? Side : width() - Side - Indicator) : (rtl ? width() - Side - Indicator : Side);
		const int textX = navigation ? (rtl ? Side + Indicator + Gap : Side) : (rtl ? Side : Side + Indicator + Gap);
		const int textWidth = qMax(1, width() - Side * 2 - Indicator - Gap);
		const qreal offset = isDown() ? 1 : 0;
		const QRectF indicator(indicatorX, Top + qMax(0, (QFontMetrics(font()).height() - Indicator) / 2) + offset, Indicator, Indicator);
		const QColor accent = isEnabled() ? QColor("#67DDEB") : palette().color(QPalette::Disabled, QPalette::ButtonText);
		if (navigation) {
			painter.setPen(QPen(isEnabled() ? accent : foreground, 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
			const qreal direction = rtl ? -1 : 1; const auto centre = indicator.center();
			QPainterPath chevron; chevron.moveTo(centre + QPointF(-3 * direction, -5));
			chevron.lineTo(centre + QPointF(3 * direction, 0)); chevron.lineTo(centre + QPointF(-3 * direction, 5));
			painter.drawPath(chevron);
		} else {
			painter.setPen(QPen(isChecked() ? accent : QColor("#8394AB"), 1.5));
			painter.setBrush(isChecked() ? accent : QColor("#101C2A"));
			if (include_) painter.drawRoundedRect(indicator, 4, 4); else painter.drawEllipse(indicator);
		}
		if (!navigation && isChecked()) {
			painter.setPen(QPen(QColor("#082332"), 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
			if (include_) {
				QPainterPath check; check.moveTo(indicator.left() + 4, indicator.top() + 9);
				check.lineTo(indicator.left() + 7.5, indicator.top() + 12.5); check.lineTo(indicator.left() + 14, indicator.top() + 5.5);
				painter.drawPath(check);
			} else { painter.setPen(Qt::NoPen); painter.setBrush(QColor("#082332")); painter.drawEllipse(indicator.center(), 4, 4); }
		}
		painter.setPen(foreground);
		const qreal titleHeight = textLayout(text(), font(), textWidth, &painter, QPointF(textX, Top + offset));
		if (!detail_.isEmpty()) {
			auto muted = foreground; muted.setAlpha(isEnabled() ? 205 : 165); painter.setPen(muted);
			textLayout(detail_, detailFont(), textWidth, &painter, QPointF(textX, Top + titleHeight + 6 + offset));
		}
	}
};

inline QPushButton *card(QWidget *parent, const QString &title, const QString &detail = {})
{
	return new ChoiceCard(parent, title, detail);
}

inline QWidget *checkCard(QCheckBox *model, QWidget *parent, const QString &detail = {})
{
	model->hide(); auto *button = new ChoiceCard(parent, model->text(), detail); button->setIncludeIndicator(true);
	button->setChecked(model->isChecked()); button->followEnabledState(model);
	QObject::connect(button, &QPushButton::toggled, model, [model, guard = QPointer<ChoiceCard>(button)](bool checked) {
		const QPointer<QCheckBox> modelGuard(model); model->setChecked(checked);
		if (guard && modelGuard) { const QSignalBlocker blocked(guard); guard->setChecked(modelGuard->isChecked()); }
	});
	QObject::connect(model, &QCheckBox::toggled, button, [model, button] {
		const QSignalBlocker blocked(button); button->setChecked(model->isChecked());
	});
	return button;
}

// Reflow the existing widgets rather than recreating sources or preview objects.
class ChoiceGrid final : public QWidget {
	QGridLayout *grid_;
	QList<QWidget *> items_;
	int maximumColumns_, columns_ = 0;
	static constexpr int MinimumColumnWidth = 236;
	int intrinsicMinimumWidth() const
	{
		int width = 160;
		for (auto *item : items_) if (!item->isHidden())
			width = qMax(width, qMax(item->minimumWidth(), item->minimumSizeHint().width()));
		return width;
	}
	void updateNaturalMinimum()
	{
		const int needed = heightForWidth(qMax(intrinsicMinimumWidth(), width()));
		if (minimumHeight() != needed) setMinimumHeight(needed);
	}
	int columnCount(int width) const
	{
		int count = 0; for (auto *item : items_) if (!item->isHidden()) ++count;
		const auto margins = grid_->contentsMargins();
		const int available = qMax(1, width - margins.left() - margins.right());
		const int minimum = qMax(MinimumColumnWidth, intrinsicMinimumWidth());
		return qMax(1, qMin(qMin(maximumColumns_, qMax(1, count)), (available + grid_->horizontalSpacing()) / (minimum + grid_->horizontalSpacing())));
	}

public:
	explicit ChoiceGrid(QWidget *parent, int maximumColumns = 2) : QWidget(parent), grid_(new QGridLayout), maximumColumns_(qMax(1, maximumColumns))
	{
		setObjectName("PulseWeaverSetupChoiceGrid"); grid_->setContentsMargins(0, 0, 0, 0); grid_->setSpacing(10);
		// AlignTop on the grid caps wrapped rows at their preferred-width hint.
		// An outer stretch preserves compact cards while allowing actual-width HFW.
		auto *outer = new QVBoxLayout(this); outer->setContentsMargins(0, 0, 0, 0); outer->setSpacing(0); outer->addLayout(grid_); outer->addStretch(1);
		auto policy = QSizePolicy(QSizePolicy::Expanding, QSizePolicy::Maximum); policy.setHeightForWidth(true); setSizePolicy(policy);
	}
	void clearCards()
	{
		while (auto *item = grid_->takeAt(0)) delete item;
		qDeleteAll(items_); items_.clear(); columns_ = 0;
	}
	void addCard(QWidget *widget) { widget->setParent(this); items_.append(widget); widget->show(); }
	void reflow(bool force = false)
	{
		const int columns = columnCount(width());
		if (!force && columns == columns_) { updateNaturalMinimum(); return; }
		while (auto *item = grid_->takeAt(0)) delete item;
		for (int column = 0; column < maximumColumns_; ++column) grid_->setColumnStretch(column, column < columns ? 1 : 0);
		int visible = 0;
		for (auto *item : items_) if (!item->isHidden()) { grid_->addWidget(item, visible / columns, visible % columns); ++visible; }
		columns_ = columns; setProperty("pulseSetupColumns", columns); updateNaturalMinimum(); updateGeometry();
	}
	bool hasHeightForWidth() const override { return true; }
	int heightForWidth(int width) const override
	{
		const auto margins = grid_->contentsMargins(); const int columns = columnCount(width);
		const int itemWidth = qMax(1, (width - margins.left() - margins.right() - (columns - 1) * grid_->horizontalSpacing()) / columns);
		int height = margins.top() + margins.bottom(), rowHeight = 0, count = 0;
		for (auto *item : items_) {
			if (item->isHidden()) continue;
			if (count && count % columns == 0) { height += rowHeight + grid_->verticalSpacing(); rowHeight = 0; }
			rowHeight = qMax(rowHeight, item->hasHeightForWidth() ? item->heightForWidth(itemWidth) : item->sizeHint().height()); ++count;
		}
		return height + rowHeight;
	}
	QSize sizeHint() const override { return QSize(620, heightForWidth(qMax(1, width()))); }
	QSize minimumSizeHint() const override { return QSize(intrinsicMinimumWidth(), heightForWidth(qMax(1, width()))); }
protected:
	bool event(QEvent *event) override
	{
		const bool handled = QWidget::event(event);
		if (event->type() == QEvent::LayoutRequest) reflow();
		return handled;
	}
	void resizeEvent(QResizeEvent *event) override
	{
		QWidget::resizeEvent(event); reflow();
		if (event->oldSize().width() != event->size().width()) updateGeometry();
	}
};

// Hidden native selectors remain authoritative, including later model updates.
inline QWidget *cards(QComboBox *model, QWidget *parent, int columns = 2,
	const std::function<bool(const QString &)> &include = {})
{
	model->hide(); auto *panel = new ChoiceGrid(parent, columns);
	auto rebuild = [panel, model, include] {
		panel->clearCards();
		for (int index = 0; index < model->count(); ++index) {
			if (include && !include(model->itemData(index).toString())) continue;
			const QPersistentModelIndex item(model->model()->index(index, model->modelColumn(), model->rootModelIndex()));
			auto detail = model->itemData(index, Qt::ToolTipRole).toString(); if (detail == model->itemText(index)) detail.clear();
			auto *button = new ChoiceCard(panel, model->itemText(index), detail);
			button->setAccessibleName(model->accessibleName().isEmpty() ? model->itemText(index) : model->accessibleName() + " · " + model->itemText(index));
			button->setChecked(model->currentIndex() == index); button->followEnabledState(model, item.flags() & Qt::ItemIsEnabled); panel->addCard(button);
			QObject::connect(button, &QPushButton::clicked, model, [model, item, guard = QPointer<QPushButton>(button)] {
				if (!item.isValid() || item.model() != model->model()) return;
				const QPointer<QComboBox> modelGuard(model); model->setCurrentIndex(item.row());
				if (guard && modelGuard) guard->setChecked(modelGuard->currentIndex() == item.row());
			});
			QObject::connect(model, &QComboBox::currentIndexChanged, button, [button, item, model] { button->setChecked(item.isValid() && model->currentIndex() == item.row()); });
		}
		panel->reflow(true);
	};
	QObject::connect(model->model(), &QAbstractItemModel::modelReset, panel, rebuild);
	QObject::connect(model->model(), &QAbstractItemModel::rowsInserted, panel, [rebuild] { rebuild(); });
	QObject::connect(model->model(), &QAbstractItemModel::rowsRemoved, panel, [rebuild] { rebuild(); });
	QObject::connect(model->model(), &QAbstractItemModel::dataChanged, panel, [rebuild] { rebuild(); });
	QObject::connect(model, &QObject::destroyed, panel, [panel] { panel->setEnabled(false); });
	rebuild(); return panel;
}

inline void choose(QWidget *owner, QComboBox *model, const QString &title,
	const std::function<bool(const QString &)> &include = {}, const QString &emptyMessage = {},
	const QString &noMatchesMessage = {}, const QString &instruction = {})
{
	QDialog dialog(owner); dialog.setObjectName("PulseWeaverSetupChoiceDialog"); dialog.setProperty("pulseSetupDialog", true);
	dialog.setWindowTitle(title);
	const auto *screen = owner ? owner->screen() : QGuiApplication::primaryScreen();
	const QSize available = screen ? (screen->availableGeometry().size() - QSize(40, 64)).expandedTo(QSize(1, 1)) : QSize(760, 560);
	dialog.setMinimumSize(QSize(360, 320).boundedTo(available)); dialog.setMaximumSize(available);
	dialog.resize(QSize(760, 560).boundedTo(available));
	auto *layout = new QVBoxLayout(&dialog); layout->setContentsMargins(22, 22, 22, 18); layout->setSpacing(14);
	auto *heading = new QLabel(title, &dialog); heading->setObjectName("PulseSetupTitle"); heading->setTextFormat(Qt::PlainText); heading->setWordWrap(true); layout->addWidget(heading);
	auto *help = new QLabel(instruction.isEmpty() ? "Choose an option for this step. Your current choice stays selected until you choose another." : instruction, &dialog);
	help->setObjectName("PulseSetupSubtitle"); help->setTextFormat(Qt::PlainText); help->setWordWrap(true); layout->addWidget(help);
	auto *search = new QLineEdit(&dialog); search->setObjectName("PulseSetupChoiceSearch"); search->setPlaceholderText("Search available choices…");
	search->setClearButtonEnabled(true); search->setAccessibleName("Search choices: " + title); layout->addWidget(search);
	auto *scroll = new QScrollArea(&dialog); scroll->setWidgetResizable(true); scroll->setFrameShape(QFrame::NoFrame); layout->addWidget(scroll, 1);
	auto *body = new ChoiceGrid(scroll, 3); scroll->setWidget(body);
	struct Entry { QPersistentModelIndex item; QString searchable; QPushButton *button; };
	QVector<Entry> choices;
	for (int index = 0; index < model->count(); ++index) {
		if (include && !include(model->itemData(index).toString())) continue;
		const QPersistentModelIndex item(model->model()->index(index, model->modelColumn(), model->rootModelIndex()));
		auto detail = model->itemData(index, Qt::ToolTipRole).toString(); if (detail == model->itemText(index)) detail.clear();
		auto *button = card(body, model->itemText(index), detail); button->setChecked(model->currentIndex() == index); body->addCard(button);
		choices.append({item, model->itemText(index) + "\n" + detail, button});
		QObject::connect(button, &QPushButton::clicked, &dialog, [guard = QPointer<QComboBox>(model), item, &dialog] {
			if (!guard || !item.isValid() || item.model() != guard->model() || !(item.flags() & Qt::ItemIsEnabled)) return;
			guard->setCurrentIndex(item.row()); if (guard && guard->currentIndex() == item.row()) dialog.accept();
		});
	}
	auto *empty = new QLabel(body); empty->setObjectName("PulseSetupChoiceEmpty"); empty->setTextFormat(Qt::PlainText); empty->setWordWrap(true); body->addCard(empty);
	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, &dialog); layout->addWidget(buttons);
	auto *clear = buttons->addButton("Clear search", QDialogButtonBox::ActionRole); clear->setAutoDefault(false);
	buttons->button(QDialogButtonBox::Cancel)->setText("Back"); buttons->button(QDialogButtonBox::Cancel)->setAutoDefault(false);
	QObject::connect(clear, &QPushButton::clicked, search, &QLineEdit::clear);
	QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	auto filter = [&] {
		int available = 0, visible = 0; const auto query = search->text().trimmed();
		for (const auto &choice : choices) {
			const bool valid = choice.item.isValid() && choice.item.model() == model->model(); if (valid) ++available;
			const bool matches = valid && choice.searchable.contains(query, Qt::CaseInsensitive);
			choice.button->setEnabled(model->isEnabled() && valid && (choice.item.flags() & Qt::ItemIsEnabled));
			choice.button->setVisible(matches); if (matches) ++visible;
		}
		empty->setText(available == 0 ? (emptyMessage.isEmpty() ? "No choices are available for this step. Go back to add or change an option." : emptyMessage)
			: (noMatchesMessage.isEmpty() ? "No choices match this search. Try another term or clear the search." : noMatchesMessage));
		empty->setVisible(visible == 0); clear->setVisible(!query.isEmpty()); body->reflow(true);
	};
	QObject::connect(search, &QLineEdit::textChanged, &dialog, [&](const QString &) { filter(); });
	QObject::connect(model->model(), &QAbstractItemModel::rowsRemoved, &dialog, filter);
	QObject::connect(model->model(), &QAbstractItemModel::modelReset, &dialog, filter);
	filter(); search->setFocus(); dialog.exec();
}

inline QPushButton *picker(QComboBox *model, QWidget *parent, const QString &title)
{
	model->hide(); auto *button = new QPushButton(title, parent); button->setObjectName("PulseWeaverSetupPicker");
	button->setAccessibleName(title); button->setAutoDefault(false);
	QObject::connect(button, &QPushButton::clicked, model, [parent, model, title] { choose(parent, model, title); });
	return button;
}
} // namespace PulseSetupChoices
