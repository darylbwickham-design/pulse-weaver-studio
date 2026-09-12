#pragma once

#include <QApplication>
#include <QComboBox>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPointer>
#include <QPushButton>
#include <QToolButton>
#include <QVariantAnimation>

namespace PulseVisual {
// A short, input-transparent sweep. No blur, idle timer or full-window repaint.
class Sweep final : public QWidget {
    QVariantAnimation animation;
    qreal progress = 0;
protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        QPainterPath clip;
        clip.addRoundedRect(QRectF(rect()).adjusted(1, 1, -1, -1), 4, 4);
        painter.setClipPath(clip);
        const qreal band = qMin(90.0, width() * 0.55);
        const qreal x = -band + (width() + band * 2) * progress;
        QColor light = palette().color(QPalette::Highlight).lighter(180);
        light.setAlphaF(0.15 * (1.0 - progress));
        QLinearGradient gradient(x - band, 0, x + band, height());
        gradient.setColorAt(0, Qt::transparent);
        gradient.setColorAt(0.5, light);
        gradient.setColorAt(1, Qt::transparent);
        painter.fillRect(rect(), gradient);
        light.setAlphaF(0.45 * (1.0 - progress));
        painter.setPen(QPen(light, 1));
        painter.drawLine(QPointF(qMax(3.0, x - band), height() - 2),
                         QPointF(qMin(width() - 3.0, x + band), height() - 2));
    }
    void hideEvent(QHideEvent *event) override
    {
        animation.stop();
        QWidget::hideEvent(event);
        deleteLater();
    }
public:
    explicit Sweep(QWidget *control) : QWidget(control)
    {
        setObjectName("PulseWeaverInteractionSweep");
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_NoSystemBackground);
        setFocusPolicy(Qt::NoFocus);
        setGeometry(control->rect());
        animation.setDuration(220);
        animation.setStartValue(0.0);
        animation.setEndValue(1.0);
        animation.setEasingCurve(QEasingCurve::OutCubic);
        connect(&animation, &QVariantAnimation::valueChanged, this, [this](const QVariant &value) {
            progress = value.toReal();
            update();
        });
        connect(&animation, &QVariantAnimation::finished, this, &QObject::deleteLater);
        show();
        raise();
        animation.start();
    }
};

class InteractionFeedback final : public QObject {
    QList<QPointer<Sweep>> sweeps;
protected:
    bool eventFilter(QObject *target, QEvent *event) override
    {
        bool activation = event->type() == QEvent::MouseButtonPress &&
            static_cast<QMouseEvent *>(event)->button() == Qt::LeftButton;
        if (event->type() == QEvent::KeyPress) {
            auto *key = static_cast<QKeyEvent *>(event);
            activation = !key->isAutoRepeat() && (key->key() == Qt::Key_Space ||
                key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter);
        }
        if (!activation) return false;
        if (!qApp->property("pulseWeaverInteractionFeedback").toBool()) return false;
        auto *control = qobject_cast<QWidget *>(target);
        if (!control || !control->isEnabled() || !control->isVisible() || control->window()->isMinimized()) return false;
        if (!qobject_cast<QPushButton *>(control) && !qobject_cast<QToolButton *>(control) &&
            !qobject_cast<QComboBox *>(control)) return false;
        // Repeated input cannot accumulate work, even on very fast clicks.
        for (qsizetype i = sweeps.size() - 1; i >= 0; --i) {
            if (!sweeps[i]) sweeps.removeAt(i);
            else if (sweeps[i]->parentWidget() == control) {
                delete sweeps.takeAt(i).data();
            }
        }
        while (sweeps.size() >= 3) delete sweeps.takeFirst().data();
        sweeps.append(new Sweep(control));
        return false; // Never consume or delay the original control event.
    }
public:
    explicit InteractionFeedback(QObject *parent) : QObject(parent) { qApp->installEventFilter(this); }
    ~InteractionFeedback() override { for (auto &sweep : sweeps) delete sweep.data(); }
};
} // namespace PulseVisual
