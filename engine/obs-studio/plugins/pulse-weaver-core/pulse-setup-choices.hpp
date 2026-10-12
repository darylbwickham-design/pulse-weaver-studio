#pragma once

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QGridLayout>
#include <QFontMetrics>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>
#include <functional>

namespace PulseSetupChoices {
inline QPushButton *card(QWidget *parent, const QString &title, const QString &detail = {})
{
	auto *button = new QPushButton(parent);
	button->setCheckable(true); button->setAccessibleName(title);
	button->setAccessibleDescription(detail);
	button->setToolTip(detail.isEmpty() ? title : title + "\n" + detail);
	auto policy = button->sizePolicy(); policy.setHorizontalPolicy(QSizePolicy::Expanding); policy.setVerticalPolicy(QSizePolicy::Preferred); button->setSizePolicy(policy);
	button->setStyleSheet("QPushButton { text-align:left; padding:8px; max-height:16777215px; border:1px solid #465267; border-radius:6px; }"
		"QPushButton:checked { border:2px solid #52daef; background:#173544; }"
		"QPushButton:focus { border:2px solid #bceef5; }"
		"QPushButton:hover { background:#223345; }");
	auto font = button->font(); font.setBold(true); button->setFont(font);
	QString wrapped, line; const QFontMetrics metrics(font);
	for (const auto &word : title.split(' ', Qt::SkipEmptyParts)) {
		if (!line.isEmpty() && metrics.horizontalAdvance(line + ' ' + word) > 220) {
			wrapped += line + '\n'; line.clear();
		}
		if (!line.isEmpty()) line += ' ';
		line += word;
	}
	wrapped += line; button->setText(wrapped); button->ensurePolished();
	button->setMinimumHeight(qMax(64, (wrapped.count('\n') + 1) * metrics.height() + 24));
	return button;
}

inline QWidget *checkCard(QCheckBox *model, QWidget *parent, const QString &detail = {})
{
	model->hide(); auto *panel = new QWidget(parent); auto *layout = new QVBoxLayout(panel); layout->setContentsMargins(0, 0, 0, 0);
	auto *button = card(panel, model->text(), detail); layout->addWidget(button);
	if (!detail.isEmpty()) {
		auto *description = new QLabel(detail, panel); description->setTextFormat(Qt::PlainText); description->setWordWrap(true); layout->addWidget(description);
	}
	button->setChecked(model->isChecked());
	QObject::connect(button, &QPushButton::toggled, model, &QCheckBox::setChecked);
	QObject::connect(model, &QCheckBox::toggled, button, &QPushButton::setChecked);
	return panel;
}

// The existing selectors remain the state model; the visible choices are cards.
inline QWidget *cards(QComboBox *model, QWidget *parent, int columns = 2,
	const std::function<bool(const QString &)> &include = {})
{
	model->hide(); auto *panel = new QWidget(parent); auto *grid = new QGridLayout(panel);
	grid->setContentsMargins(0, 0, 0, 0); int count = 0;
	for (int index = 0; index < model->count(); ++index) {
		if (include && !include(model->itemData(index).toString())) continue;
		auto *button = card(panel, model->itemText(index));
		button->setAccessibleName(model->accessibleName() + " · " + model->itemText(index));
		button->setChecked(model->currentIndex() == index); grid->addWidget(button, count / columns, count % columns); ++count;
		QObject::connect(button, &QPushButton::clicked, model, [model, index, button] {
			model->setCurrentIndex(index); button->setChecked(model->currentIndex() == index);
		});
		QObject::connect(model, &QComboBox::currentIndexChanged, button, [button, index](int selected) { button->setChecked(selected == index); });
	}
	const int rows = (count + columns - 1) / columns;
	panel->setMinimumHeight(rows * 76 + qMax(0, rows - 1) * grid->verticalSpacing());
	return panel;
}

inline void choose(QWidget *owner, QComboBox *model, const QString &title,
	const std::function<bool(const QString &)> &include = {})
{
	QDialog dialog(owner); dialog.setWindowTitle(title); dialog.resize(680, 540);
	auto *layout = new QVBoxLayout(&dialog);
	auto *search = new QLineEdit(&dialog); search->setPlaceholderText("Type to find a choice…");
	search->setAccessibleName("Find a choice"); layout->addWidget(search);
	auto *scroll = new QScrollArea(&dialog); scroll->setWidgetResizable(true); layout->addWidget(scroll, 1);
	auto *body = new QWidget(scroll); auto *grid = new QGridLayout(body); grid->setAlignment(Qt::AlignTop); scroll->setWidget(body);
	QVector<QPair<QString, QPushButton *>> choices;
	for (int index = 0; index < model->count(); ++index) {
		if (include && !include(model->itemData(index).toString())) continue;
		auto *button = card(body, model->itemText(index)); button->setChecked(model->currentIndex() == index);
		choices.append({model->itemText(index), button});
		QObject::connect(button, &QPushButton::clicked, &dialog, [model, index, &dialog] { model->setCurrentIndex(index); dialog.accept(); });
	}
	auto *empty = new QLabel("No matching choices. You can go back and create a new source or stinger.", body);
	empty->setWordWrap(true);
	auto filter = [&] {
		int visible = 0;
		for (const auto &choice : choices) {
			grid->removeWidget(choice.second);
			const bool matches = choice.first.contains(search->text().trimmed(), Qt::CaseInsensitive);
			choice.second->setVisible(matches);
			if (matches) { grid->addWidget(choice.second, visible / 2, visible % 2); ++visible; }
		}
		grid->removeWidget(empty); empty->setVisible(visible == 0);
		if (!visible) grid->addWidget(empty, 0, 0, 1, 2);
	};
	QObject::connect(search, &QLineEdit::textChanged, &dialog, [&](const QString &) { filter(); }); filter();
	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, &dialog); layout->addWidget(buttons);
	QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	dialog.exec();
}

inline QPushButton *picker(QComboBox *model, QWidget *parent, const QString &title)
{
	model->hide(); auto *button = new QPushButton(title, parent);
	button->setAccessibleName(title);
	QObject::connect(button, &QPushButton::clicked, model, [parent, model, title] { choose(parent, model, title); });
	return button;
}
}
