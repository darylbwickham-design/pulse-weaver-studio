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
#include <QPointer>
#include <QResizeEvent>
#include <QSignalBlocker>
#include <QSlider>
#include <QToolButton>
#include <QVariantAnimation>
#include <QVBoxLayout>

namespace PulseStudioControls {
class LivePreviewToggle final : public QCheckBox {
	QVariantAnimation slide;
	qreal position = 0;
public:
	explicit LivePreviewToggle(QWidget *parent) : QCheckBox("Live previews", parent), slide(this) {
		setCursor(Qt::PointingHandCursor);
		slide.setDuration(160); slide.setEasingCurve(QEasingCurve::OutCubic);
		connect(&slide, &QVariantAnimation::valueChanged, this, [this](const QVariant &value) {position = value.toReal(); update();});
		connect(this, &QCheckBox::toggled, this, [this](bool checked) {
			slide.stop();
			if (isVisible()) {slide.setStartValue(position); slide.setEndValue(checked ? 1.0 : 0.0); slide.start();}
			else {position = checked ? 1 : 0; update();}
		});
	}
	QSize sizeHint() const override { return {fontMetrics().horizontalAdvance(text()) + 45, qMax(26, fontMetrics().height() + 4)}; }
	QSize minimumSizeHint() const override { return sizeHint(); }
protected:
	bool hitButton(const QPoint &point) const override { return rect().contains(point); }
	void paintEvent(QPaintEvent *) override {
		QPainter painter(this); painter.setRenderHint(QPainter::Antialiasing);
		const QRectF track(1, (height() - 18) / 2.0, 32, 18);
		const QColor base = palette().color(isChecked() ? QPalette::Highlight : QPalette::Mid);
		QLinearGradient rail(track.topLeft(), track.bottomLeft()); rail.setColorAt(0, base.darker(140)); rail.setColorAt(1, base);
		painter.setPen(QPen(base.darker(160), 1)); painter.setBrush(rail);
		painter.drawRoundedRect(track, 9, 9);
		const QRectF thumb(track.x() + 3 + 14 * position, track.y() + 2, 12, 12);
		painter.setPen(Qt::NoPen); painter.setBrush(QColor(0, 0, 0, 90)); painter.drawEllipse(thumb.translated(0, 2));
		QLinearGradient cap(thumb.topLeft(), thumb.bottomLeft()); cap.setColorAt(0, Qt::white); cap.setColorAt(1, QColor("#BFC9D9"));
		painter.setPen(QPen(QColor(255, 255, 255, 140), 1)); painter.setBrush(cap); painter.drawEllipse(thumb);
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
		rate->setStyleSheet("QSlider::groove:horizontal{height:4px;background:palette(mid);border:1px solid palette(dark);border-radius:3px;}"
			"QSlider::sub-page:horizontal{background:palette(highlight);border-radius:3px;}"
			"QSlider::handle:horizontal{width:13px;margin:-5px 0;background:qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 #FFFFFF,stop:1 #AFBED1);border:1px solid #8091A8;border-radius:7px;}");
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
inline QColor routeAccent(const QString &provider)
{
	if (provider.compare("Twitch", Qt::CaseInsensitive) == 0) return QColor("#9146FF");
	if (provider.compare("YouTube", Qt::CaseInsensitive) == 0) return QColor("#FF0033");
	if (provider.compare("Kick", Qt::CaseInsensitive) == 0) return QColor("#53FC18");
	return QColor("#F5AA29"); // Recording
}

class SlidingChoiceTrack final : public QWidget {
	QVariantAnimation slide;
	QColor accent;
	qreal position = -1;
	int selected = -1;
public:
	static constexpr int cellWidth = 47;
	static constexpr int cellHeight = 28;
	static constexpr int inset = 2;
	explicit SlidingChoiceTrack(int count, const QColor &colour, QWidget *parent) : QWidget(parent), slide(this), accent(colour) {
		setObjectName("PulseWeaverRouteTrack");
		setFixedSize(count * cellWidth + inset * 2, cellHeight + inset * 2);
		slide.setObjectName("PulseWeaverRouteThumbAnimation");
		slide.setDuration(180); slide.setEasingCurve(QEasingCurve::OutCubic);
		connect(&slide, &QVariantAnimation::valueChanged, this, [this](const QVariant &value) {
			position = value.toReal(); setProperty("thumbPosition", position); update();
		});
	}
	void select(int index, bool animate) {
		if (index == selected) return;
		slide.stop(); selected = index; setProperty("thumbIndex", index);
		if (animate && position >= 0 && isVisible() && isEnabled()) {
			slide.setStartValue(position); slide.setEndValue(qreal(index)); slide.start();
		} else {position = index; setProperty("thumbPosition", position); update();}
	}
protected:
	void resizeEvent(QResizeEvent *event) override {
		QWidget::resizeEvent(event); slide.stop(); position = selected; setProperty("thumbPosition", position);
	}
	void paintEvent(QPaintEvent *) override {
		QPainter painter(this); painter.setRenderHint(QPainter::Antialiasing);
		if (!isEnabled()) painter.setOpacity(.45);
		const QRectF rail = QRectF(rect()).adjusted(.5, .5, -.5, -.5);
		const int grey = qGray(palette().color(QPalette::Base).rgb());
		const QColor base(grey, grey, grey);
		QLinearGradient recess(rail.topLeft(), rail.bottomLeft()); recess.setColorAt(0, base.darker(155)); recess.setColorAt(1, base.lighter(125));
		const int edgeGrey = qGray(palette().color(QPalette::Mid).rgb());
		painter.setPen(QPen(QColor(edgeGrey, edgeGrey, edgeGrey).darker(130), 1)); painter.setBrush(recess);
		painter.drawRoundedRect(rail, 15, 15);
		painter.setPen(QPen(QColor(255, 255, 255, 22), 1));
		painter.drawLine(QPointF(14, height() - 1.5), QPointF(width() - 14, height() - 1.5));
		if (position < 0) return;
		const QRectF thumb(inset + position * cellWidth + 1, inset, cellWidth - 2, cellHeight - 2);
		painter.setPen(Qt::NoPen); painter.setBrush(QColor(0, 0, 0, 110)); painter.drawRoundedRect(thumb.translated(0, 2), 12, 12);
		QLinearGradient cap(thumb.topLeft(), thumb.bottomLeft()); cap.setColorAt(0, accent.lighter(150)); cap.setColorAt(.48, accent.lighter(110)); cap.setColorAt(1, accent.darker(120));
		painter.setPen(QPen(accent.lighter(160), 1)); painter.setBrush(cap); painter.drawRoundedRect(thumb, 12, 12);
		painter.setPen(QPen(QColor(255, 255, 255, 95), 1));
		painter.drawLine(thumb.topLeft() + QPointF(10, 2), thumb.topRight() + QPointF(-10, 2));
	}
};

class RouteChoices final : public QWidget {
	QPointer<QComboBox> model;
protected:
	bool eventFilter(QObject *watched, QEvent *event) override {
		if(watched == model && event->type() == QEvent::EnabledChange) setEnabled(model->isEnabled());
		return QWidget::eventFilter(watched, event);
	}
public:
	RouteChoices(const QString &provider, QComboBox *controller, QWidget *parent = nullptr, bool showTitle = true) : QWidget(parent), model(controller)
	{
		setObjectName("PulseWeaverRouteChoices" + provider);
		setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
		auto *column = new QVBoxLayout(this); column->setContentsMargins(0, 0, 0, 0); column->setSpacing(2);
		if(showTitle) {auto *title = new QLabel(provider, this); title->setObjectName("PulseWeaverKicker"); column->addWidget(title);}
		const QColor accent = routeAccent(provider);
		const QString selectedText = qGray(accent.rgb()) > 145 ? "#15191F" : "#FFFFFF";
		auto *track = new SlidingChoiceTrack(controller->count(), accent, this);
		auto *row = new QHBoxLayout(track);
		row->setContentsMargins(SlidingChoiceTrack::inset, SlidingChoiceTrack::inset, SlidingChoiceTrack::inset, SlidingChoiceTrack::inset);
		row->setSpacing(0); column->addWidget(track, 0, Qt::AlignLeft);
		auto *group = new QButtonGroup(this); group->setExclusive(true);
		for (int index = 0; index < controller->count(); ++index) {
			const QString mode = controller->itemData(index).toString();
			const QString label = mode == "standard" ? "Default" : mode == "off" ? "Off" : mode == "horizontal" ? "16:9" : mode == "vertical" ? "9:16" : "Both";
			auto *button = new QToolButton(track); button->setText(label); button->setCheckable(true);
			button->setObjectName("PulseWeaverRouteChoice"); button->setProperty("routeMode", mode);
			button->setFixedSize(SlidingChoiceTrack::cellWidth, SlidingChoiceTrack::cellHeight);
			button->setCursor(Qt::PointingHandCursor);
			button->setAccessibleName(provider + " " + label);
			button->setToolTip(provider + (mode == "standard" ? ": use your recording settings" : mode == "dual" ? ": landscape and portrait" : mode == "off" ? ": output disabled" : ": " + label));
			button->setStyleSheet(QString("QToolButton{background:transparent;color:#9DA4AF;border:1px solid transparent;border-radius:12px;padding:0px;font-size:9pt;}"
				"QToolButton:checked{color:%1;}QToolButton:focus{border-color:%2;}QToolButton:disabled{color:palette(placeholder-text);}").arg(selectedText, accent.name()));
			group->addButton(button, index); row->addWidget(button);
		}
		const QPointer<QComboBox> guarded = controller;
		auto sync = [guarded, group, track](bool animate) {
			if (!guarded) return;
			if (auto *button = group->button(guarded->currentIndex())) button->setChecked(true);
			track->select(guarded->currentIndex(), animate);
		};
		connect(group, &QButtonGroup::idClicked, this, [guarded, sync](int index) {
			if(guarded && guarded->isEnabled()) guarded->setCurrentIndex(index); sync(true);
		});
		connect(controller, &QComboBox::currentIndexChanged, this, [sync](int) { sync(true); });
		controller->installEventFilter(this); setEnabled(controller->isEnabled());
		sync(false);
	}
};
} // namespace PulseStudioControls
