#include "../../engine/obs-studio/shared/qt/PulseDiscord.hpp"
#include <cassert>
int main(int argc,char **argv) {
 QApplication app(argc,argv);
 QWidget parent;QListWidget feed;QComboBox filter;QLabel status;
 filter.addItem("All chats","all");
 PulseDiscord::Client client(&feed,&filter,&status,&parent);
 auto *discord=PulseChat::append(&feed,"discord","#mod-chat · Test","Example",{},{},{},"123","456",false,{},"789");
 auto *twitch=PulseChat::append(&feed,"twitch","Viewer","Hello");
 filter.addItem("Discord · #mod-chat","discord:789");filter.setCurrentIndex(filter.findData("discord:789"));client.applyFilter();
 assert(!discord->isHidden()&&twitch->isHidden());
 filter.setCurrentIndex(0);client.applyFilter();assert(!discord->isHidden()&&!twitch->isHidden());
 const QJsonObject message{{"id","900"},{"user","Mod"},{"user_id","901"},{"content","Private example"}};
 const QJsonObject group{{"channel","789"},{"name","mod-chat"},{"messages",QJsonArray{message}}};
 client.renderSnapshot(QJsonObject{{"groups",QJsonArray{group}}});
 const int count=feed.count();client.renderSnapshot(QJsonObject{{"groups",QJsonArray{group}}});assert(feed.count()==count);
 auto edited=message;edited.insert("content","Edited example");auto editedGroup=group;editedGroup.insert("messages",QJsonArray{edited});
 client.renderSnapshot(QJsonObject{{"groups",QJsonArray{editedGroup}}});
 QListWidgetItem *received=nullptr;for(int i=0;i<feed.count();++i)if(feed.item(i)->data(PulseChat::MessageId).toString()=="900")received=feed.item(i);
 assert(received&&received->data(PulseChat::Text).toString()=="Edited example");
 auto empty=group;empty.insert("messages",QJsonArray{});client.renderSnapshot(QJsonObject{{"groups",QJsonArray{empty}}});assert(feed.count()==count-1);
 return 0;
}
