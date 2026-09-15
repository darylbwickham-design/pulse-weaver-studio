#pragma once
#include "PulseChatProtocol.hpp"

#include <QApplication>
#include <QCache>
#include <QClipboard>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
#include <QListWidget>
#include <QNetworkAccessManager>
#include <QNetworkDiskCache>
#include <QNetworkReply>
#include <QPainter>
#include <QPointer>
#include <QPushButton>
#include <QScrollBar>
#include <QSet>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QStyledItemDelegate>
#include <QTextDocument>
#include <QAbstractTextDocumentLayout>
#include <QTimer>
#include <QWheelEvent>
#include <QtMath>

namespace PulseChat {
enum Role {
    Platform = Qt::UserRole + 140, User, UserId, MessageId, Text,
    Colour, Badges, Images, Fragments, Time, Deleted, Route
};

inline QHash<QString, QUrl> parseBadges(const QJsonObject &root)
{
    QHash<QString, QUrl> result;
    for (const auto value : root.value("data").toArray()) {
        const auto set = value.toObject();
        for (const auto version : set.value("versions").toArray()) {
            const auto badge = version.toObject();
            const QUrl url(badge.value("image_url_2x").toString(badge.value("image_url_1x").toString()));
            if (!set.value("set_id").toString().isEmpty() && !badge.value("id").toString().isEmpty() &&
                url.scheme() == "https")
                result.insert(set.value("set_id").toString() + "/" + badge.value("id").toString(), url);
        }
    }
    return result;
}

inline bool atBottom(QListWidget *feed)
{
    return feed->verticalScrollBar()->value() >= feed->verticalScrollBar()->maximum() - 3;
}

inline QJsonArray kickFragments(const QString &message)
{
    static const QRegularExpression emote("\\[emote:(\\d+):([^\\]]+)\\]");
    QJsonArray fragments;
    auto matches = emote.globalMatch(message);
    int end = 0;
    while (matches.hasNext()) {
        const auto match = matches.next();
        if (match.capturedStart() > end) fragments.append(QJsonObject{{"text", message.mid(end, match.capturedStart() - end)}});
        fragments.append(QJsonObject{{"text", match.captured(2)}, {"image_url", "https://files.kick.com/emotes/" + match.captured(1) + "/fullsize"}});
        end = match.capturedEnd();
    }
    if (end == 0) return {};
    if (end < message.size()) fragments.append(QJsonObject{{"text", message.mid(end)}});
    return fragments;
}

inline void markDeleted(QListWidget *feed, const QString &platform, const QString &messageId = {}, const QString &userId = {})
{
    if (!feed) return;
    for (int n = 0; n < feed->count(); ++n) {
        auto *item = feed->item(n);
        if (item->data(Platform).toString() != platform) continue;
        if (!messageId.isEmpty() && item->data(MessageId).toString() != messageId) continue;
        if (!userId.isEmpty() && item->data(UserId).toString() != userId) continue;
        item->setData(Deleted, true);
    }
}

// Only visible messages need documents. Images and layouts have bounded caches;
// messages are model items rather than a hierarchy of widgets for every fragment.
class Delegate final : public QStyledItemDelegate {
    QListWidget *feed;
    QNetworkAccessManager network;
    QCache<QString, QImage> images{256};
    QCache<QString, QTextDocument> documents{512};
    QSet<QString> pending;
    QHash<QString, qint64> failed;

    void requestImage(const QString &address)
    {
        const QUrl url(address);
        if (url.scheme() != "https" || pending.contains(address) || pending.size() >= 24 ||
            failed.value(address) > QDateTime::currentMSecsSinceEpoch()) return;
        pending.insert(address);
        QNetworkRequest request(url);
        request.setTransferTimeout(10000);
        request.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::PreferCache);
        auto *reply = network.get(request);
        QObject::connect(reply, &QNetworkReply::finished, this, [this, reply, address] {
            const QImage image = QImage::fromData(reply->readAll());
            pending.remove(address);
            if (reply->error() == QNetworkReply::NoError && !image.isNull()) {
                images.insert(address, new QImage(image.scaled(64, 64, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
                documents.clear();
                feed->viewport()->update(); // All images reserve their final dimensions.
            } else {
                if (failed.size() >= 256) failed.clear();
                failed.insert(address, QDateTime::currentMSecsSinceEpoch() + 60000);
            }
            reply->deleteLater();
        });
    }

    QTextDocument *document(const QModelIndex &index, int width)
    {
        const QString platform = index.data(Platform).toString();
        const auto badgeUrls = feed->property("pulseWeaverBadgeUrls").toMap();
        const auto localUrls = index.data(Images).toMap();
        QString html;
        const QString muted = feed->palette().color(QPalette::PlaceholderText).name();
        const QString textColour = feed->palette().color(QPalette::Text).name();
        const QString platformColour = platform == "twitch" ? "#bf94ff" : platform == "kick" ? "#53fc18" : "#ff7272";
        html += "<span style='color:" + platformColour + ";font-size:10px'>" + platform.left(1).toUpper() + "</span> ";
        if (feed->property("pulseWeaverChatTimestamps").toBool())
            html += "<span style='color:" + muted + ";font-size:11px'>" + index.data(Time).toString() + "</span> ";
		const QString route = index.data(Route).toString();
		if (!route.isEmpty())
			html += "<span style='color:" + muted + ";font-size:9px'>" +
				(route == "vertical" ? QString("9:16") : QString("16:9")) + "</span> ";
        QStringList resources;
        auto imageTag = [&resources](const QString &url, int size) {
            resources << url;
            return QString("<img src=\"%1\" width=\"%2\" height=\"%2\" />").arg(url.toHtmlEscaped()).arg(size);
        };
        for (const QString &badge : index.data(Badges).toStringList()) {
            const QString url = localUrls.value(badge, badgeUrls.value(badge)).toString();
            if (!url.isEmpty()) html += imageTag(url, 18) + " ";
            else if (platform != "twitch") {
                const QString role = badge.toLower();
                const QString symbol = role.contains("moderator") || role == "mod" ? "&#128295;" :
                    role.contains("owner") || role.contains("broadcaster") ? "&#9819;" :
                    role.contains("verified") ? "&#10003;" :
                    role.contains("member") || role.contains("subscriber") || role.contains("sponsor") ? "&#9733;" :
                    role.contains("vip") ? "&#9670;" : role.contains("gifter") ? "&#127873;" : "";
                if (!symbol.isEmpty()) html += "<span style='color:" + platformColour + "'>" + symbol + "</span> ";
            }
        }
        QColor colour(index.data(Colour).toString());
        if (!colour.isValid() || colour.lightness() < 60) colour = QColor(platformColour);
        html += "<b style='color:" + colour.name() + "'>" + index.data(User).toString().toHtmlEscaped() + "</b>: ";
        if (index.data(Deleted).toBool()) {
            html += "<i style='color:" + muted + "'>Message deleted</i>";
        } else {
            const auto fragments = index.data(Fragments).value<QJsonArray>();
            if (fragments.isEmpty()) html += index.data(Text).toString().toHtmlEscaped();
            else for (const auto value : fragments) {
                const auto fragment = value.toObject();
                const QString emote = fragment.value("emote").toObject().value("id").toString();
                if (fragment.contains("image_url")) html += imageTag(fragment.value("image_url").toString(), 24);
                else if (emote.isEmpty()) html += fragment.value("text").toString().toHtmlEscaped();
                else html += imageTag("https://static-cdn.jtvnw.net/emoticons/v2/" +
                    QString::fromLatin1(QUrl::toPercentEncoding(emote)) + "/static/dark/2.0", 24);
            }
        }
        const QString key = QString::number(width) + feed->font().toString() + textColour + html;
        if (auto *cached = documents.object(key)) {
            for (const auto &url : resources) if (!images.contains(url)) requestImage(url);
            return cached;
        }
        auto *doc = new QTextDocument;
        QFont font = feed->font(); font.setPixelSize(14);
        doc->setDefaultFont(font);
        doc->setDocumentMargin(0);
        QTextOption option;
        option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
        doc->setDefaultTextOption(option);
        doc->setDefaultStyleSheet("body{color:" + textColour + ";} p{margin:0;}");
        for (const auto &url : resources) {
            if (auto *image = images.object(url)) doc->addResource(QTextDocument::ImageResource, QUrl(url), *image);
            else {
                QImage blank(24, 24, QImage::Format_ARGB32_Premultiplied); blank.fill(Qt::transparent);
                doc->addResource(QTextDocument::ImageResource, QUrl(url), blank);
                requestImage(url);
            }
        }
        doc->setHtml("<body>" + html + "</body>");
        doc->setTextWidth(qMax(40, width - 16));
        documents.insert(key, doc);
        return doc;
    }
public:
    explicit Delegate(QListWidget *view) : QStyledItemDelegate(view), feed(view), network(this)
    {
        auto *cache = new QNetworkDiskCache(&network);
        cache->setCacheDirectory(QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/pulseweaver-chat-images");
        cache->setMaximumCacheSize(32 * 1024 * 1024);
        network.setCache(cache);
    }
    QSize sizeHint(const QStyleOptionViewItem &, const QModelIndex &index) const override
    {
        const int width = feed->viewport()->width();
        auto *doc = const_cast<Delegate *>(this)->document(index, width);
        return QSize(width, qMax(28, int(qCeil(doc->size().height())) + 8));
    }
    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        painter->save();
        painter->setClipRect(option.rect);
        if (option.state & (QStyle::State_MouseOver | QStyle::State_Selected)) painter->fillRect(option.rect, feed->palette().color(QPalette::AlternateBase));
        painter->translate(option.rect.topLeft() + QPoint(8, 4));
        QAbstractTextDocumentLayout::PaintContext context;
        context.palette = feed->palette();
        const_cast<Delegate *>(this)->document(index, feed->viewport()->width())->documentLayout()->draw(painter, context);
        painter->restore();
    }
};

class Feed final : public QListWidget {
protected:
    void wheelEvent(QWheelEvent *event) override
    {
        if (event->angleDelta().y() > 0 || event->pixelDelta().y() > 0) setProperty("pulseWeaverChatAutoScroll", false);
        QListWidget::wheelEvent(event);
        if (atBottom(this)) setProperty("pulseWeaverChatAutoScroll", true);
    }
    void resizeEvent(QResizeEvent *event) override
    {
        const bool follow = property("pulseWeaverChatAutoScroll").toBool();
        auto *anchor = itemAt(QPoint(2, 2));
        QListWidget::resizeEvent(event);
        doItemsLayout();
        if (follow) scrollToBottom();
        else if (anchor) scrollToItem(anchor, QAbstractItemView::PositionAtTop);
    }
public:
    explicit Feed(QWidget *parent = nullptr) : QListWidget(parent)
    {
        setItemDelegate(new Delegate(this));
        setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
        setMouseTracking(true);
        setSpacing(0);
        setContextMenuPolicy(Qt::CustomContextMenu);
        setAccessibleName("Live chat messages");
        setToolTip("Click a message for user and moderation actions. Scroll up to pause; jump to latest to resume.");
        connect(this, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
            emit customContextMenuRequested(visualItemRect(item).center());
        });
        connect(verticalScrollBar(), &QScrollBar::sliderPressed, this, [this] { setProperty("pulseWeaverChatAutoScroll", false); });
        connect(verticalScrollBar(), &QScrollBar::sliderReleased, this, [this] { setProperty("pulseWeaverChatAutoScroll", atBottom(this)); });
    }
};

inline QListWidgetItem *append(QListWidget *feed, const QString &platform, const QString &user, const QString &message,
    const QString &colour = {}, const QStringList &badges = {}, const QHash<QString, QUrl> &urls = {},
    const QString &userId = {}, const QString &messageId = {}, bool own = false, const QJsonArray &fragments = {},
	const QString &route = {})
{
    Q_UNUSED(own);
    if (!feed || message.trimmed().isEmpty()) return nullptr;
    if (!messageId.isEmpty()) for (int i = feed->count() - 1; i >= 0; --i)
        if (feed->item(i)->data(Platform).toString() == platform && feed->item(i)->data(MessageId).toString() == messageId) return nullptr;
    const bool follow = feed->property("pulseWeaverChatAutoScroll").toBool() && atBottom(feed);
    auto *anchor = feed->itemAt(QPoint(2, 2));
    const int offset = anchor ? feed->visualItemRect(anchor).top() : 0;
    auto *item = new QListWidgetItem;
    item->setData(Platform, platform); item->setData(User, user); item->setData(UserId, userId);
    item->setData(MessageId, messageId); item->setData(Text, message); item->setData(Colour, colour);
    item->setData(Badges, badges);
	item->setData(Route, route);
    item->setData(Fragments, QVariant::fromValue(platform == "kick" && fragments.isEmpty() ? kickFragments(message) : fragments));
    item->setData(Time, QDateTime::currentDateTime().toString("HH:mm"));
    QVariantMap images; for (auto it = urls.begin(); it != urls.end(); ++it) images.insert(it.key(), it.value().toString());
    item->setData(Images, images);
    item->setData(Qt::AccessibleTextRole, platform + ": " + user + ": " + message);
    QStringList roles;
    for (const auto &badge : badges) roles << badge.section('/', 0, 0).replace('_', ' ');
    item->setToolTip(platform.toUpper() + " · " + user + "\n" + roles.join(", ") + "\n" + message);
    feed->addItem(item);
    const QString filter = feed->property("pulseWeaverChatFilter").toString();
    item->setHidden(!filter.isEmpty() && filter != "all" && filter != platform);
    while (feed->count() > 500) {
        if (anchor == feed->item(0)) anchor = feed->item(1);
        delete feed->takeItem(0);
    }
    feed->doItemsLayout();
    if (follow) feed->scrollToBottom();
    else {
        if (anchor) { feed->scrollToItem(anchor, QAbstractItemView::PositionAtTop); feed->verticalScrollBar()->setValue(feed->verticalScrollBar()->value() - offset); }
        if (!item->isHidden()) {
            const int unread = feed->property("pulseWeaverUnreadCount").toInt() + 1;
            feed->setProperty("pulseWeaverUnreadCount", unread);
            if (auto *button = feed->window()->findChild<QPushButton *>("PulseWeaverChatNewMessages")) {
                button->setText(QString("↓ %1 new messages · Jump to latest").arg(unread)); button->show();
            }
        }
    }
	return item;
}
}
