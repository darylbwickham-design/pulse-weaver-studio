#include <QtWidgets>
#include <iostream>
#include <stdexcept>
static QString settingsFile;
static QString pulseWeaverUiSettingsPath(){return settingsFile;}
class DestinationHarness : public QWidget {
public:
 struct Pending {bool value=false;bool pending()const{return value;}} pulseStreamStart;
 bool streaming=false;bool StreamingActive()const{return streaming;}
 void *pulseYouTubeOutput=nullptr,*pulseYouTubeSecondOutput=nullptr;
 QString pulseYouTubePreparedMode;
 QComboBox route,hidden,stage;
 QLabel status,dualStatus;
 QLabel *pulseDestinationStatus=&status,*pulseDualFormatStatus=&dualStatus;
 QComboBox *pulseYouTubeCanvas=&hidden,*pulseStageSelector=&stage;
 int stageApplies=0;QList<QStringList> changes;
 void ApplyPulseWeaverStage(int,bool){++stageApplies;}
 void ApplyPulseWeaverLiveDestinationChange(const QString &key,const QString &before,const QString &after){changes.append({key,before,after});}
 DestinationHarness(const QString &key,const QString &label,const QString &initial) {
  for(QComboBox *combo:{&route,&hidden})for(const QString &mode:{QString("off"),QString("horizontal"),QString("vertical"),QString("dual")})combo->addItem(mode,mode);
  stage.addItem("Stage");route.setCurrentIndex(route.findData(initial));hidden.setCurrentIndex(hidden.findData(initial));
  route.setProperty("pulseWeaverPreviousMode",initial);setProperty("pulseWeaverGoLiveSession",true);
  bind(&route,key,label);
  connect(&hidden,&QComboBox::currentIndexChanged,this,[this](int){int index=route.findData(hidden.currentData());if(index>=0&&index!=route.currentIndex())route.setCurrentIndex(index);});
 }
 void bind(QComboBox *route,const QString &key,const QString &label){
#include "callback-under-test.hpp"
 }
 void select(const QString &mode){route.setCurrentIndex(route.findData(mode));}
};
static int checks=0;static void check(bool ok,const char *message){++checks;if(!ok)throw std::runtime_error(message);}
static void drain(){for(int n=0;n<8;++n)QCoreApplication::processEvents();}
int main(int argc,char **argv){
 qputenv("QT_QPA_PLATFORM","minimal");QApplication app(argc,argv);QTemporaryDir temp;settingsFile=temp.filePath("routes.ini");
 try{
  {DestinationHarness h("youtube","YouTube","off");h.select("horizontal");h.select("dual");drain();
   check(h.changes.size()==1&&h.changes.front()==QStringList{"youtube","off","dual"},"Rapid changes did not coalesce to final destination plan");
   check(h.stageApplies==1&&!h.route.property("pulseWeaverPendingPreviousMode").isValid(),"Stage applied repeatedly or pending origin was retained");}
  {DestinationHarness h("youtube","YouTube","off");h.select("horizontal");h.select("off");drain();
   check(h.changes.size()==1&&h.changes.front()==QStringList{"youtube","off","off"},"Transient enable could start stale output");}
  {DestinationHarness h("youtube","YouTube","horizontal");h.pulseYouTubeOutput=&h;h.pulseYouTubePreparedMode="horizontal";
   h.select("dual");drain();check(h.route.currentData()=="horizontal"&&h.changes.isEmpty(),"Live YouTube format change was accepted");
   h.hidden.setCurrentIndex(h.hidden.findData("dual"));drain();check(h.hidden.currentData()=="horizontal"&&h.route.currentData()=="horizontal","Rejected hidden selector diverged from active output");}
  {DestinationHarness h("youtube","YouTube","horizontal");h.pulseYouTubeOutput=&h;h.pulseYouTubePreparedMode="horizontal";
   h.select("off");h.select("dual");drain();check(h.route.currentData()=="off"&&h.changes.size()==1&&h.changes.front()==QStringList{"youtube","horizontal","off"},"Rapid Off/format switch bypassed live YouTube guard");}
  {DestinationHarness h("youtube","YouTube","horizontal");h.pulseYouTubeOutput=&h;h.pulseYouTubePreparedMode="horizontal";
   h.select("off");h.select("horizontal");drain();check(h.route.currentData()=="horizontal"&&h.changes.size()==1&&h.changes.front()==QStringList{"youtube","horizontal","horizontal"},"Returning to existing live format changed its output");}
  {DestinationHarness h("twitch","Twitch","horizontal");h.streaming=true;h.select("dual");drain();check(h.route.currentData()=="horizontal"&&h.changes.isEmpty(),"Live Twitch format guard regressed");
   h.select("off");drain();check(h.changes.size()==1&&h.changes.front().back()=="off","Twitch Off was blocked while streaming");}
  {DestinationHarness h("kick","Kick","off");h.setProperty("pulseWeaverGoLiveSession",false);h.select("horizontal");drain();check(h.changes.isEmpty()&&h.stageApplies==1,"Idle routing change unexpectedly started output");}
  std::cout<<"PASS: "<<checks<<" production destination callback checks (no streams)\n";
 }catch(const std::exception &error){std::cerr<<"FAIL: "<<error.what()<<'\n';return 1;}return 0;
}
