#pragma once

#include <QByteArray>
#include <QGraphicsScene>
#include <QPointer>

namespace PulseRuntimeSafety {

// QObject::destroyed is emitted before children are deleted. A selected item
// can emit selectionChanged during that deletion, after editor state was reset.
inline void silenceSceneOnOwnerDestruction(QGraphicsScene *scene, QObject *owner)
{
    QObject::connect(owner, &QObject::destroyed, scene, [scene] { scene->blockSignals(true); });
}

enum class HttpFrame { Incomplete, Complete, Invalid };
constexpr qsizetype maxRequestBytes = 65536;

// This server closes after one request and deliberately does not accept chunked
// encoding or pipelining. Never dispatch a partial JSON body as default values.
inline HttpFrame httpFrame(const QByteArray &request)
{
    if (request.size() > maxRequestBytes)
        return HttpFrame::Invalid;
    const qsizetype end = request.indexOf("\r\n\r\n");
    if (end < 0)
        return HttpFrame::Incomplete;
    const auto lines = request.left(end).split('\n');
    const auto first = lines.front().trimmed().split(' ');
    if (first.size() != 3 || (first[2] != "HTTP/1.1" && first[2] != "HTTP/1.0"))
        return HttpFrame::Invalid;
    bool hasLength = false;
    qint64 length = 0;
    for (qsizetype i = 1; i < lines.size(); ++i) {
        const QByteArray line = lines[i].trimmed();
        const qsizetype colon = line.indexOf(':');
        if (colon <= 0)
            return HttpFrame::Invalid;
        const QByteArray name = line.left(colon).toLower();
        if (name == "transfer-encoding")
            return HttpFrame::Invalid;
        if (name != "content-length")
            continue;
        if (hasLength)
            return HttpFrame::Invalid;
        hasLength = true;
        const QByteArray value = line.mid(colon + 1).trimmed();
        if (value.isEmpty())
            return HttpFrame::Invalid;
        for (char digit : value)
            if (digit < '0' || digit > '9')
                return HttpFrame::Invalid;
        bool ok = false;
        length = value.toLongLong(&ok);
        if (!ok || length > maxRequestBytes - end - 4)
            return HttpFrame::Invalid;
    }
    const qint64 received = request.size() - end - 4;
    if (received > length)
        return HttpFrame::Invalid;
    return received == length ? HttpFrame::Complete : HttpFrame::Incomplete;
}

} // namespace PulseRuntimeSafety
