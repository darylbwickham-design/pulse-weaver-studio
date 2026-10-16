#pragma once
#include <QCheckBox>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QSlider>
#include <QTimer>
#include <QWidget>

// Explicit, one-shot development audit. The caller only enables this for a
// separate inert portable fixture; normal launches allocate no audit timer.
namespace PulseUiLayoutAudit {
inline void run(QWidget *window, QWidget *landscape, QWidget *portrait, QWidget *chat,
                QCheckBox *toggle, QSlider *rate, QLabel *status, const QString &directory)
{
	if(!landscape || !portrait || !chat || !toggle || !rate) return;
	QDir().mkpath(directory);
	auto rectangles = [=] {
		QJsonObject result;
		for(const auto &entry : {qMakePair("landscape", landscape), qMakePair("portrait", portrait), qMakePair("chat", chat)}) {
			const QPoint origin = entry.second->mapTo(window, QPoint());
			result.insert(entry.first, QJsonArray{origin.x(), origin.y(), entry.second->width(), entry.second->height()});
		}
		for(const char *name : {"PulseWeaverTransport", "PulseWeaverRouteChoicesRecording", "PulseWeaverRecord", "PulseWeaverLive"}) {
			if(auto *widget = window->findChild<QWidget *>(name)) {
				const QPoint origin = widget->mapTo(window, QPoint());
				result.insert(name, QJsonArray{origin.x(), origin.y(), widget->width(), widget->height()});
			}
		}
		return result;
	};
	window->setAttribute(Qt::WA_DontShowOnScreen); window->showNormal(); window->resize(1280, 1000);
	QTimer::singleShot(500, window, [=] {
		const auto before = rectangles();
		window->grab().save(directory + "/main-1280x1000.png");
		const bool wasChecked = toggle->isChecked(); const int previousRate = rate->value();
		const QString previousStatus = status ? status->text() : QString();
		toggle->setChecked(!wasChecked); rate->setValue(previousRate == 2 ? 0 : 2);
		if(status) status->setText("Test connection failed: a long diagnostic must not take space from the program previews or chat.");
		QTimer::singleShot(250, window, [=] {
			const auto after = rectangles();
			QJsonObject report{{"largeBefore", before}, {"largeAfter", after}, {"largeStable", before == after}};
			toggle->setChecked(wasChecked); rate->setValue(previousRate); if(status) status->setText(previousStatus);
			window->resize(1180, 720);
			QTimer::singleShot(500, window, [=]() mutable {
				const auto smallBefore = rectangles(); window->grab().save(directory + "/main-1180x720.png");
				toggle->setChecked(!wasChecked); rate->setValue(previousRate == 2 ? 0 : 2);
				QTimer::singleShot(250, window, [=]() mutable {
					const auto smallAfter = rectangles();
					report.insert("smallBefore", smallBefore); report.insert("smallAfter", smallAfter);
					report.insert("smallStable", smallBefore == smallAfter);
					toggle->setChecked(wasChecked); rate->setValue(previousRate);
					QFile file(directory + "/layout-audit.json");
					if(file.open(QIODevice::WriteOnly)) file.write(QJsonDocument(report).toJson());
				});
			});
		});
	});
}
}
