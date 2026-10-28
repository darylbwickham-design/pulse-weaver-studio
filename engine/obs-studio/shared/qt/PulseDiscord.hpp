#pragma once
#include "PulseChat.hpp"
#include "PulseAppCredentials.hpp"
#include <QComboBox>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QLabel>
#include <QPushButton>
#include <QUrlQuery>
#include <QVBoxLayout>
#include <functional>
#include <algorithm>

// Receive-only desktop client. The Discord bot credential never enters this process.
namespace PulseDiscord {
class Client : public QObject {
    QNetworkAccessManager network;
    QTimer poll, pairing;
    QPointer<QListWidget> feed;
    QPointer<QComboBox> filter;
    QPointer<QLabel> status;
    QString token, proof, guild, approval;
    QStringList channels;
    QSet<QString> previous;
    bool busy=false, pairBusy=false;
    quint64 generation=0;
    const QString base="https://pulse-community.darylbwickham.workers.dev";
    using Callback=std::function<void(QJsonObject,int)>;
    void request(const QString &path, const QByteArray &method, const QString &credential, Callback callback) {
        QNetworkRequest req(QUrl(base+path));
        req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,QNetworkRequest::ManualRedirectPolicy);
        req.setTransferTimeout(15000);
        if(!credential.isEmpty())req.setRawHeader("Authorization",("Bearer "+credential).toUtf8());
        req.setRawHeader("Content-Type","application/json");
        auto *reply=method=="GET"?network.get(req):network.post(req,QByteArray("{}"));
        const auto epoch=generation; connect(reply,&QNetworkReply::finished,this,[this,reply,callback,epoch]{
            int code=reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            const QByteArray body=reply->read(8*1024*1024+1);
            const auto document=QJsonDocument::fromJson(body); const auto data=document.object(); if(body.size()>8*1024*1024||!document.isObject())code=502;
            reply->deleteLater();if(epoch==generation)callback(data,code);
        });
    }
    void clearFeed() {
        if(feed)for(int i=feed->count()-1;i>=0;--i)if(feed->item(i)->data(PulseChat::Platform).toString()=="discord")delete feed->takeItem(i);
        previous.clear();
    }
    void fail(const QString &text) { if(status){status->setText(text);status->setVisible(!text.isEmpty());} }
    void load() {
        token=PulseAppCredentials::get("pulse_discord","access_token");
        guild=PulseAppCredentials::get("pulse_discord","guild");
        channels=PulseAppCredentials::get("pulse_discord","channels").split(',',Qt::SkipEmptyParts);
    }
    void snapshot() {
        if(busy||token.isEmpty()||guild.isEmpty()||channels.isEmpty()||!feed)return;
        busy=true;const auto version=generation;
        QUrlQuery query;query.addQueryItem("guild",guild);query.addQueryItem("channels",channels.join(','));
        request("/bridge/messages?"+query.toString(QUrl::FullyEncoded),"GET",token,[this,version](QJsonObject data,int code){
            busy=false;if(version!=generation||!feed)return;
            if(code!=200||!data.value("groups").isArray()){poll.setInterval(std::min(60000,poll.interval()*2));clearFeed();fail(data.value("error").toString("Discord disconnected; retrying…"));if(code==401||code==403)poll.stop();return;}
            poll.setInterval(6000);renderSnapshot(data);
        });
    }
public:
    void renderSnapshot(const QJsonObject &data) {
        if(!feed)return;
        QSet<QString> present;
        QList<QJsonObject> rows;
        for(const auto &entry:data.value("groups").toArray()){
            const auto group=entry.toObject();const QString channel=group.value("channel").toString(),name=group.value("name").toString();
            if(filter&&filter->findData("discord:"+channel)<0)filter->addItem("Discord · #"+name,"discord:"+channel);
            for(const auto &value:group.value("messages").toArray()){auto row=value.toObject();row.insert("_channel",channel);row.insert("_name",name);rows.append(row);}
        }
        std::sort(rows.begin(),rows.end(),[](const QJsonObject &a,const QJsonObject &b){return a.value("id").toString().toULongLong()<b.value("id").toString().toULongLong();});
        for(const auto &m:rows){
            const QString id=m.value("id").toString();present.insert(id);
            QString text=m.value("content").toString();for(const auto &attachment:m.value("attachments").toArray())text+="\n[Attachment: "+attachment.toObject().value("name").toString()+"]";
            QListWidgetItem *existing=nullptr;for(int i=feed->count()-1;i>=0;--i)if(feed->item(i)->data(PulseChat::Platform).toString()=="discord"&&feed->item(i)->data(PulseChat::MessageId).toString()==id){existing=feed->item(i);break;}
            if(existing){existing->setData(PulseChat::Text,text);existing->setData(PulseChat::Fragments,QVariant::fromValue(QJsonArray{}));}
            else PulseChat::append(feed,"discord","#"+m.value("_name").toString()+" · "+m.value("user").toString(),text,{}, {},{},m.value("user_id").toString(),id,false,{},m.value("_channel").toString());
        }
        for(int i=feed->count()-1;i>=0;--i){auto *item=feed->item(i);if(item->data(PulseChat::Platform).toString()=="discord"&&previous.contains(item->data(PulseChat::MessageId).toString())&&!present.contains(item->data(PulseChat::MessageId).toString()))delete feed->takeItem(i);}
        previous=present;applyFilter();feed->doItemsLayout();feed->viewport()->update();fail("Discord connected · receive-only");
    }
private:
    void chooseChannels(QWidget *parent) {
        request("/bridge/guilds","GET",token,[this,parent=QPointer<QWidget>(parent)](QJsonObject data,int code){
            if(!parent||code!=200){fail("Reconnect Pulse to choose Discord channels.");return;}
            auto *dialog=new QDialog(parent);dialog->setAttribute(Qt::WA_DeleteOnClose);dialog->setWindowTitle("Discord channels");
            auto *layout=new QVBoxLayout(dialog);auto *servers=new QComboBox(dialog);auto *list=new QListWidget(dialog);auto *hint=new QLabel("Select up to five channels. Discord is receive-only.",dialog);
            auto *buttons=new QDialogButtonBox(QDialogButtonBox::Save|QDialogButtonBox::Cancel,dialog);layout->addWidget(servers);layout->addWidget(hint);layout->addWidget(list);layout->addWidget(buttons);
            for(const auto &v:data.value("guilds").toArray()){auto g=v.toObject();servers->addItem(g.value("name").toString(),g.value("id").toString());}
            auto refresh=[this,servers,list=QPointer<QListWidget>(list),hint=QPointer<QLabel>(hint)]{
                if(!list)return;list->clear();const QString id=servers->currentData().toString();
                request("/bridge/channels?guild="+id,"GET",token,[this,list,hint,id,servers=QPointer<QComboBox>(servers)](QJsonObject result,int code){if(!list||!servers||servers->currentData().toString()!=id)return;if(code!=200){if(hint)hint->setText(result.value("error").toString("Channels unavailable."));return;}for(const auto &v:result.value("channels").toArray()){auto c=v.toObject();auto *item=new QListWidgetItem("#"+c.value("name").toString(),list);item->setData(Qt::UserRole,c.value("id"));item->setFlags(item->flags()|Qt::ItemIsUserCheckable);item->setCheckState(id==guild&&channels.contains(c.value("id").toString())?Qt::Checked:Qt::Unchecked);}});
            };
            connect(servers,&QComboBox::currentIndexChanged,dialog,refresh);
            connect(buttons,&QDialogButtonBox::rejected,dialog,&QDialog::reject);
            connect(buttons,&QDialogButtonBox::accepted,dialog,[this,dialog,servers,list,hint]{QStringList selected;for(int i=0;i<list->count();++i)if(list->item(i)->checkState()==Qt::Checked)selected<<list->item(i)->data(Qt::UserRole).toString();if(selected.size()>5){hint->setText("Choose up to five channels.");return;}++generation;busy=false;clearFeed();guild=servers->currentData().toString();channels=selected;PulseAppCredentials::set("pulse_discord","guild",guild);PulseAppCredentials::set("pulse_discord","channels",channels.join(','));poll.start();snapshot();dialog->accept();});
            const int selected=servers->findData(guild);if(selected>=0)servers->setCurrentIndex(selected);refresh();dialog->resize(440,460);dialog->open();
        });
    }
public:
    Client(QListWidget *chat,QComboBox *read,QLabel *label,QObject *parent):QObject(parent),network(this),feed(chat),filter(read),status(label){
        load();poll.setInterval(6000);pairing.setInterval(4000);
        connect(&poll,&QTimer::timeout,this,[this]{snapshot();});
        connect(&pairing,&QTimer::timeout,this,[this]{if(pairBusy)return;pairBusy=true;request("/bridge/claim","POST",proof,[this](QJsonObject data,int code){pairBusy=false;if(code!=200){pairing.stop();fail("Discord connection expired. Connect again.");return;}const QString value=data.value("token").toString();if(value.isEmpty())return;pairing.stop();proof.clear();if(!PulseAppCredentials::set("pulse_discord","access_token",value)){fail("Could not securely save the connection.");return;}token=value;chooseChannels(qobject_cast<QWidget*>(this->parent()));});});
        if(filter){filter->addItem("Discord","discord");connect(filter,&QComboBox::currentIndexChanged,this,[this]{applyFilter();});}
        poll.start();
    }
    void applyFilter(){if(!feed||!filter)return;const auto selected=filter->currentData().toString();for(int i=0;i<feed->count();++i){auto *item=feed->item(i);const auto platform=item->data(PulseChat::Platform).toString();item->setHidden(!PulseChat::matchesFilter(selected,platform,item->data(PulseChat::Route).toString()));}}
    void configure(QWidget *parent){if(!token.isEmpty()){chooseChannels(parent);return;}if(!proof.isEmpty()&&pairing.isActive()){QDesktopServices::openUrl(QUrl(approval));return;}request("/bridge/start","POST",{},[this](QJsonObject data,int code){if(code!=200){fail("Unable to start Discord connection.");return;}proof=data.value("secret").toString();approval=data.value("url").toString();if(!QUrl(approval).toString().startsWith(base+"/connect.html?request=")){proof.clear();return;}QDesktopServices::openUrl(QUrl(approval));pairing.start();fail("Approve Pulse Weaver in your browser.");});}
    void disconnectDevice(){++generation;busy=false;pairBusy=false;poll.stop();pairing.stop();clearFeed();if(!token.isEmpty())request("/bridge/revoke","POST",token,[](QJsonObject,int){});token.clear();proof.clear();PulseAppCredentials::remove("pulse_discord","access_token");fail("Discord disconnected");}
};
}
