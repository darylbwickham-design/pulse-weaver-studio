#pragma once
#include <QJsonObject>
#include <QString>

namespace PulseOverlay {
QString renderPage(const QJsonObject &document, const QString &layout, const QString &eventPath = {});
QString renderBootstrap();
} // namespace PulseOverlay
