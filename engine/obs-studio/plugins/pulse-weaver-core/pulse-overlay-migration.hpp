#pragma once
#include <QString>
#include <QSet>
#include <functional>
namespace PulseOverlay {
int migrateManagedSources(const QString &directory, quint16 port, const QString &liveCapability,
                          const QSet<QString> &publishedIds, const std::function<void(const QString &)> &status = {});
}
