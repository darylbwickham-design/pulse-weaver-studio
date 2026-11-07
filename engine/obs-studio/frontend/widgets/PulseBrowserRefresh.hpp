#pragma once

#include <obs.hpp>
#include <QByteArray>
#include <QIcon>
#include <QPainter>
#include <QPainterPath>
#include <QPointer>
#include <QPixmap>
#include <QSet>
#include <QTimer>
#include <QToolButton>
#include <QToolTip>
#include <QVariant>
#include <cstring>
#include <functional>
#include <utility>
#include <vector>

namespace PulseBrowserRefresh {

inline constexpr const char *ToolTip = "Refresh all browser sources on every canvas, including inactive sources";

inline void updateIcon(QToolButton *button)
{
	const qreal ratio = button->devicePixelRatioF();
	QPixmap image(qRound(24 * ratio), qRound(24 * ratio));
	image.setDevicePixelRatio(ratio);
	image.fill(Qt::transparent);
	QPainter painter(&image);
	painter.setRenderHint(QPainter::Antialiasing);
	painter.setPen(QPen(button->palette().color(QPalette::ButtonText), 1.5));
	painter.setBrush(Qt::NoBrush);
	painter.drawEllipse(QRectF(2.5, 2.5, 15.4, 15.4));
	painter.drawEllipse(QRectF(6.5, 2.5, 7.4, 15.4));
	painter.drawLine(QPointF(2.8, 10.2), QPointF(17.6, 10.2));
	painter.drawArc(QRectF(3.5, 5.1, 13.4, 10.2), 14 * 16, 152 * 16);
	painter.drawArc(QRectF(3.5, 5.1, 13.4, 10.2), 194 * 16, 152 * 16);
	// Clear a small badge so the refresh arrow remains distinct from the globe.
	painter.setCompositionMode(QPainter::CompositionMode_Clear);
	painter.setPen(Qt::NoPen);
	painter.setBrush(Qt::transparent);
	painter.drawEllipse(QRectF(11.2, 11.2, 12.6, 12.6));
	painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
	const QColor accent = button->palette().color(QPalette::Link);
	painter.setPen(QPen(accent, 1.9, Qt::SolidLine, Qt::RoundCap));
	painter.setBrush(Qt::NoBrush);
	painter.drawArc(QRectF(12.6, 12.6, 9, 9), 45 * 16, 285 * 16);
	QPainterPath arrow;
	arrow.moveTo(21, 19.35);
	arrow.lineTo(21.3, 21.83);
	arrow.lineTo(18.7, 20.33);
	arrow.closeSubpath();
	painter.setPen(Qt::NoPen);
	painter.setBrush(accent);
	painter.drawPath(arrow);
	painter.end();
	button->setIcon(QIcon(image));
	button->setIconSize(QSize(24, 24));
}

inline std::vector<OBSSource> collectSources()
{
	struct Snapshot {
		std::vector<OBSSource> sources;
		QSet<QByteArray> uuids;
		QSet<obs_source_t *> pointers;
	} snapshot;
	// This registry is global across canvases, unlike canvas/scene enumeration.
	// Snapshot strong references under its lock; refresh only after it is released.
	obs_enum_all_sources([](void *opaque, obs_source_t *source) {
		auto &snapshot = *static_cast<Snapshot *>(opaque);
		const char *id = source ? obs_source_get_unversioned_id(source) : nullptr;
		if (!source || obs_source_removed(source) || !id || std::strcmp(id, "browser_source") != 0)
			return true;
		const QByteArray uuid(obs_source_get_uuid(source));
		if (snapshot.pointers.contains(source) || (!uuid.isEmpty() && snapshot.uuids.contains(uuid)))
			return true;
		OBSSource reference(source);
		if (reference) {
			snapshot.pointers.insert(source);
			if (!uuid.isEmpty()) snapshot.uuids.insert(uuid);
			snapshot.sources.emplace_back(std::move(reference));
		}
		return true;
	}, &snapshot);
	return std::move(snapshot.sources);
}

struct Summary {
	int requested = 0;
	int unavailable = 0;
};

class Batch final : public QObject {
	std::vector<OBSSource> sources;
	size_t index = 0;
	Summary summary;
	std::function<void(Summary)> completed;

	void next()
	{
		if (index == sources.size()) {
			completed(summary);
			deleteLater();
			return;
		}
		const OBSSource &source = sources[index++];
		if (!obs_source_removed(source)) {
			OBSProperties properties(obs_source_properties(source));
			obs_property_t *refresh = properties ? obs_properties_get(properties, "refreshnocache") : nullptr;
			if (refresh && obs_property_get_type(refresh) == OBS_PROPERTY_BUTTON) {
				// The return value requests a properties-view rebuild, not success.
				// obs-browser queues ReloadIgnoreCache itself; no settings are changed.
				obs_property_button_clicked(refresh, source);
				++summary.requested;
			} else {
				++summary.unavailable;
			}
		} else {
			++summary.unavailable;
		}
		QTimer::singleShot(0, this, [this] { next(); });
	}

public:
	Batch(QObject *owner, std::function<void(Summary)> finished) : QObject(owner), completed(std::move(finished))
	{
		QTimer::singleShot(0, this, [this] {
			sources = collectSources();
			next();
		});
	}
};

inline void configure(QToolButton *button)
{
	button->setObjectName("PulseWeaverRefreshBrowsers");
	button->setFixedSize(32, 34);
	button->setAccessibleName("Refresh all browser sources");
	button->setAccessibleDescription(ToolTip);
	button->setToolTip(ToolTip);
	button->setCursor(Qt::PointingHandCursor);
	button->setFocusPolicy(Qt::StrongFocus);
	button->setAutoRepeat(false);
	updateIcon(button);
	QObject::connect(button, &QToolButton::clicked, button, [button] {
		if (button->property("pulseBrowserRefreshBusy").toBool()) return;
		button->setProperty("pulseBrowserRefreshBusy", true);
		button->setEnabled(false);
		button->setToolTip("Requesting browser refreshes…");
		new Batch(button, [button](Summary summary) {
			const QString message = summary.requested ?
				QString("Refresh requested for %1 browser source%2%3.").arg(summary.requested)
					.arg(summary.requested == 1 ? "" : "s")
					.arg(summary.unavailable ? QString("; %1 unavailable").arg(summary.unavailable) : QString()) :
				summary.unavailable ? QString("%1 browser sources are unavailable.").arg(summary.unavailable) :
				QString("No browser sources found.");
			button->setToolTip(message + "\n" + ToolTip);
			button->setAccessibleDescription(message);
			QToolTip::showText(button->mapToGlobal(QPoint(button->width() / 2, button->height())), message, button, QRect(), 3000);
			// Prevent a double click from immediately queuing the same reload again.
			QTimer::singleShot(1200, button, [button] {
				button->setProperty("pulseBrowserRefreshBusy", false);
				button->setEnabled(true);
			});
		});
	});
}

} // namespace PulseBrowserRefresh
