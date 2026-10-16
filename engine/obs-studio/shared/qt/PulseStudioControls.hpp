#pragma once

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QFrame>
#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPaintEvent>
#include <QSignalBlocker>
#include <QSlider>
#include <QToolButton>
#include <QVBoxLayout>

namespace PulseStudioControls {
class LivePreviewToggle final : public QCheckBox {
public:
	explicit LivePreviewToggle(QWidget *parent) : QCheckBox("Live previews", parent) { setCursor(Qt::PointingHandCursor); }
	QSize sizeHint() const override { return {fontMetrics().horizontalAdvance(text()) + 45, qMax(26, fontMetrics().height() + 4)}; }
	QSize minimumSizeHint() const override { return sizeHint(); }
protected:
	bool hitButton(const QPoint &point) const override { return rect().contains(point); }
	void paintEvent(QPaintEvent *) override {
		QPainter painter(this); painter.setRenderHint(QPainter::Antialiasing);
		const QRectF track(1, (height() - 18) / 2.0, 32, 18);
		painter.setPen(Qt::NoPen); painter.setBrush(palette().color(isChecked() ? QPalette::Highlight : QPalette::Mid));
		painter.drawRoundedRect(track, 9, 9);
		painter.setBrush(palette().color(QPalette::HighlightedText));
		painter.drawEllipse(QRectF(track.x() + (isChecked() ? 17 : 3), track.y() + 3, 12, 12));
		painter.setPen(palette().color(isEnabled() ? QPalette::WindowText : QPalette::PlaceholderText));
		painter.drawText(QRect(43, 0, width() - 43, height()), Qt::AlignLeft | Qt::AlignVCenter, text());
		if(hasFocus()) { painter.setBrush(Qt::NoBrush); painter.setPen(QPen(palette().color(QPalette::Highlight), 1)); painter.drawRoundedRect(track.adjusted(-1, -1, 1, 1), 10, 10); }
	}
};
class PreviewStrip final : public QFrame {
public:
	QCheckBox *live;
	QSlider *rate;
	QLabel *value;
	static int fps(int position) { return position == 2 ? 60 : position == 1 ? 30 : 15; }
	PreviewStrip(bool enabled, int savedFps, QWidget *parent = nullptr, bool compact = false) : QFrame(parent)
	{
		setObjectName("PulseWeaverPreviewControls");
		setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
		setStyleSheet("QFrame#PulseWeaverPreviewControls{background:transparent;border:0;}");
		auto *row = new QHBoxLayout(this);
		row->setContentsMargins(0, 0, 0, 0); row->setSpacing(10);
		live = new LivePreviewToggle(this);
		live->setObjectName("PulseWeaverLivePreviews");
		live->setAccessibleName("Live previews while streaming");
		live->setChecked(enabled);
		live->setToolTip("Keep the previews moving during streaming or recording. Untick to pause only the previews and leave more headroom for games. Stream output keeps running.");
		row->addWidget(live);
		QLabel *caption = nullptr;
		if(!compact) {row->addStretch(); caption = new QLabel("Preview refresh", this); row->addWidget(caption);}
		rate = new QSlider(Qt::Horizontal, this);
		rate->setObjectName("PulseWeaverPreviewRate");
		rate->setRange(0, 2); rate->setSingleStep(1); rate->setPageStep(1);
		rate->setTickInterval(1); rate->setTickPosition(QSlider::NoTicks);
		rate->setMinimumWidth(compact ? 60 : 90); rate->setMaximumWidth(compact ? 80 : 150);
		rate->setValue(savedFps == 60 ? 2 : savedFps == 30 ? 1 : 0);
		rate->setAccessibleName("Preview refresh rate"); if(caption) caption->setBuddy(rate);
		rate->setToolTip("15 FPS: less CPU · 30 FPS: smoother · 60 FPS: full preview rate. Changes previews only, not stream output.");
		row->addWidget(rate, 1);
		value = new QLabel(this); value->setMinimumWidth(value->fontMetrics().horizontalAdvance("60 FPS") + 4);
		row->addWidget(value);
		auto update = [this](int position) {
			value->setText(QString::number(fps(position)) + " FPS");
			rate->setAccessibleDescription(value->text() + "; changes previews only. Use arrow keys to choose 15, 30 or 60 FPS.");
		};
		update(rate->value()); connect(rate, &QSlider::valueChanged, this, update);
	}
};

// The existing controller owns persistence, output guards and accepted mode.
// Mirror its accepted value after every click, including rejected live changes.
class RouteChoices final : public QWidget {
    QComboBox *model;
protected:
	bool eventFilter(QObject *watched, QEvent *event) override {
		if(watched == model && event->type() == QEvent::EnabledChange) setEnabled(model->isEnabled());
		return QWidget::eventFilter(watched, event);
	}
public:
	RouteChoices(const QString &provider, QComboBox *controller, QWidget *parent = nullptr, bool showTitle = true) : QWidget(parent), model(controller)
	{
		setObjectName("PulseWeaverRouteChoices" + provider);
		setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
		auto *column = new QVBoxLayout(this); column->setContentsMargins(0, 0, 0, 0); column->setSpacing(2);
		if(showTitle) {auto *title = new QLabel(provider, this); title->setObjectName("PulseWeaverKicker"); column->addWidget(title);}
		auto *track = new QFrame(this); track->setObjectName("PulseWeaverRouteTrack");
		track->setStyleSheet("QFrame#PulseWeaverRouteTrack{background:palette(base);border:1px solid palette(mid);border-radius:13px;}");
		auto *row = new QHBoxLayout(track); const int inset = showTitle ? 2 : 1;
		row->setContentsMargins(inset, inset, inset, inset); row->setSpacing(0); column->addWidget(track);
		auto *group = new QButtonGroup(this); group->setExclusive(true);
		for (int index = 0; index < controller->count(); ++index) {
			const QString mode = controller->itemData(index).toString();
			const QString label = mode == "standard" ? "Default" : mode == "off" ? "Off" : mode == "horizontal" ? "16:9" : mode == "vertical" ? "9:16" : "Both";
			auto *button = new QToolButton(track); button->setText(label); button->setCheckable(true);
			button->setObjectName("PulseWeaverRouteChoice"); button->setProperty("routeMode", mode);
			button->setMinimumHeight(26); button->setMinimumWidth(34);
			button->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
			button->setAccessibleName(provider + " " + label);
			button->setToolTip(provider + (mode == "standard" ? ": use your recording settings" : mode == "dual" ? ": landscape and portrait" : mode == "off" ? ": output disabled" : ": " + label));
			button->setStyleSheet("QToolButton{background:transparent;color:palette(text);border:1px solid transparent;border-radius:11px;padding:2px 6px;}QToolButton:hover{background:palette(alternate-base);}QToolButton:checked{background:palette(highlight);color:palette(highlighted-text);}QToolButton:focus{border-color:palette(highlight);}");
			// The transport has one fixed-size slot. Keep all five recording
			// choices readable without growing the footer at larger UI fonts.
			if(!showTitle) button->setStyleSheet(button->styleSheet() + "QToolButton{padding:0px 3px;font-size:10pt;}");
			group->addButton(button, index); row->addWidget(button);
		}
		auto sync = [controller, group] {
			if (auto *button = group->button(controller->currentIndex())) button->setChecked(true);
		};
		connect(group, &QButtonGroup::idClicked, this, [controller, sync](int index) {
			if(controller->isEnabled()) controller->setCurrentIndex(index); sync();
		});
		connect(controller, &QComboBox::currentIndexChanged, this, [sync](int) { sync(); });
		controller->installEventFilter(this); setEnabled(controller->isEnabled());
		sync();
	}
};
} // namespace PulseStudioControls
