#pragma once
#include <QDir>
#include <QString>
namespace PulseMacPaths {
inline QString root() { return QDir::homePath() + "/Library/Application Support/Pulse Weaver Mac Preview"; }
inline QString path(const char *name) {
    return name && *name ? root() + '/' + QString::fromUtf8(name) : root();
}
}
