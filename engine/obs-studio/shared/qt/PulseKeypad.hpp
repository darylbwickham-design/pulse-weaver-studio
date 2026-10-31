#pragma once

#include <QButtonGroup>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QImageReader>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QResizeEvent>
#include <QSaveFile>
#include <QScrollArea>
#include <QSettings>
#include <QTimer>
#include <QToolButton>
#include <QUuid>
#include <QVBoxLayout>
#include <array>
#include <algorithm>

namespace PulseKeypad {
constexpr int Slots = 15;
constexpr int Columns = 3;
constexpr int Gap = 8;
inline QString alertKey(int slot) { return slot >= 1 && slot <= Slots ? QString("keypad_%1").arg(slot, 2, 10, QLatin1Char('0')) : QString(); }
inline QString alertTitle(int slot) { return QString("Keypad %1").arg(slot, 2, 10, QLatin1Char('0')); }
inline int buttonSide(int width) { return std::clamp((width - Gap * (Columns - 1)) / Columns, 48, 112); }
inline QString cleanName(QString name) {
	name = name.simplified();
	QString result;
	for (const auto character : name) if (character.isPrint()) result += character;
	return result.left(32);
}
inline QStringList iconNames() { return {"Camera", "Game", "Microphone", "Chat", "Break", "Play", "Stop", "News", "Heart", "Printer", "Star", "Lightning"}; }
inline QIcon icon(const QString &name, const QColor &color = QColor("#76d7ed")) {
	QPixmap pixels(96, 96); pixels.fill(Qt::transparent);
	QPainter p(&pixels); p.setRenderHint(QPainter::Antialiasing); p.scale(4, 4);
	p.setPen(QPen(color, 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin)); p.setBrush(Qt::NoBrush);
	if (name == "Camera") { p.drawRoundedRect(QRectF(3, 6, 13, 12), 2, 2); p.drawPolygon(QPolygonF{{16,10},{21,7},{21,17},{16,14}}); }
	else if (name == "Game") { p.drawRoundedRect(QRectF(2,6,20,13),5,5);p.drawLine(6,12,12,12);p.drawLine(9,9,9,15);p.drawEllipse(QPointF(17,10),.7,.7);p.drawEllipse(QPointF(19,13),.7,.7); }
	else if (name == "Microphone") {p.drawRoundedRect(QRectF(9,2,6,13),3,3);p.drawArc(QRectF(6,6,12,13),180*16,180*16);p.drawLine(12,19,12,22);p.drawLine(8,22,16,22);}
	else if (name == "Chat") {p.drawRoundedRect(QRectF(3,3,18,14),3,3);p.drawPolyline(QPolygonF{{7,17},{6,21},{12,17}});p.drawLine(7,8,17,8);p.drawLine(7,12,14,12);}
	else if (name == "Break") {p.drawRoundedRect(QRectF(4,8,13,11),2,2);p.drawArc(QRectF(14,9,7,7),-90*16,180*16);p.drawLine(3,22,19,22);p.drawLine(7,3,7,5);p.drawLine(12,2,12,5);}
	else if (name == "Play") {p.setBrush(color);p.drawPolygon(QPolygonF{{7,3},{21,12},{7,21}});}
	else if (name == "Stop") {p.setBrush(color);p.drawRoundedRect(QRectF(5,5,14,14),2,2);}
	else if (name == "News") {p.drawRoundedRect(QRectF(3,3,18,18),2,2);p.drawRect(QRectF(6,7,5,6));p.drawLine(14,7,18,7);p.drawLine(14,11,18,11);p.drawLine(6,17,18,17);}
	else if (name == "Heart") {QPainterPath path;path.moveTo(12,21);path.cubicTo(-5,10,5,-3,12,6);path.cubicTo(19,-3,29,10,12,21);p.drawPath(path);}
	else if (name == "Printer") {p.drawRoundedRect(QRectF(2,8,20,10),2,2);p.drawRect(QRectF(6,2,12,6));p.drawRect(QRectF(6,14,12,8));p.drawPoint(18,11);}
	else if (name == "Star") {p.drawPolygon(QPolygonF{{12,2},{15,8},{22,9},{17,14},{18,22},{12,18},{6,22},{7,14},{2,9},{9,8}});}
	else if (name == "Lightning") {p.drawPolygon(QPolygonF{{14,2},{4,14},{11,14},{9,23},{21,9},{13,9}});}
	else {for(int y=0;y<3;++y)for(int x=0;x<3;++x)p.drawRoundedRect(QRectF(3+x*7,3+y*7,4,4),1,1);}
	return QIcon(pixels);
}
struct Button { QString name, glyph = "Camera", image; bool assigned() const { return !name.isEmpty(); } };
inline QString imagePath(const QString &settingsFile, const QString &relative) {
	if (!relative.startsWith("keypad-icons/") || relative.contains("..") || QDir::isAbsolutePath(relative)) return {};
	return QFileInfo(settingsFile).dir().filePath(relative);
}
inline Button load(QSettings &settings, int slot) {
	settings.beginGroup(QString("keypad/button%1").arg(slot));
	Button button{cleanName(settings.value("name").toString()), settings.value("glyph", "Camera").toString(), settings.value("image").toString()};
	settings.endGroup();
	if (!iconNames().contains(button.glyph)) button.glyph = "Camera";
	if (imagePath(settings.fileName(), button.image).isEmpty()) button.image.clear();
	return button;
}
inline void save(QSettings &settings, int slot, const Button &button) {
	settings.beginGroup(QString("keypad/button%1").arg(slot));
	settings.setValue("name", cleanName(button.name));settings.setValue("glyph", button.glyph);settings.setValue("image", button.image);
	settings.endGroup();settings.sync();
}
inline QImage readIcon(const QString &file) {
	if (QFileInfo(file).size() > 16 * 1024 * 1024) return {};
	QImageReader reader(file);reader.setAutoTransform(true);
	const auto size = reader.size();
	if (!size.isValid() || size.width() > 8192 || size.height() > 8192 || qint64(size.width()) * size.height() > 32000000) return {};
	reader.setScaledSize(size.scaled(256,256,Qt::KeepAspectRatio));
	return reader.read();
}

class Key final : public QToolButton {
public:
	QIcon artwork;
	QString label;
	int slot = 1;
	bool assigned = false, edit = false, flash = false;
	explicit Key(QWidget *parent) : QToolButton(parent) {setFocusPolicy(Qt::StrongFocus);setCursor(Qt::PointingHandCursor);}
protected:
	void paintEvent(QPaintEvent *) override {
		QPainter p(this);p.setRenderHint(QPainter::Antialiasing);
		QRectF box = rect().adjusted(1,1,-1,-2);
		QLinearGradient finish(0,0,0,height());
		finish.setColorAt(0,isDown()?QColor("#142438"):underMouse()?QColor("#3a536e"):QColor("#30465e"));
		finish.setColorAt(1,isDown()?QColor("#263c54"):QColor("#162739"));
		p.setBrush(finish);p.setPen(QPen(flash||hasFocus()?QColor("#6cdeef"):QColor("#49647e"),1));p.drawRoundedRect(box,11,11);
		p.setPen(QColor("#7590a8"));p.drawLine(QPointF(12,2),QPointF(width()-12,2));
		if (isDown()) p.translate(0,1);
		const int extent = std::clamp(width()/3,22,38);
		artwork.paint(&p,QRect((width()-extent)/2,assigned?12:height()/3-4,extent,extent));
		QFont f=font();f.setPixelSize(width()<76?10:11);f.setBold(true);p.setFont(f);p.setPen(QColor("#edf6ff"));
		QString visible=label;const QRect textBox(5,assigned?height()/2+5:height()-29,width()-10,height()/2-10);
		while(visible.size()>1 && p.fontMetrics().boundingRect(textBox,Qt::TextWordWrap,visible).height()>textBox.height()) visible.chop(1);
		if(visible!=label)visible=visible.left(std::max(0,int(visible.size())-1))+QChar(0x2026);
		p.drawText(textBox,Qt::AlignHCenter|Qt::AlignTop|Qt::TextWordWrap,visible);
		f.setPixelSize(9);f.setBold(false);p.setFont(f);p.setPen(QColor("#a5bad0"));
		if(assigned)p.drawText(QRect(5,4,width()-10,12),Qt::AlignLeft,QString("%1").arg(slot,2,10,QLatin1Char('0')));
		if(edit && assigned)p.drawText(QRect(width()-18,4,12,12),Qt::AlignCenter,QString::fromUtf8("✎"));
	}
};

class Panel final : public QWidget {
	QString settingsFile;
	std::array<Button,Slots> definitions;
	std::array<Key *,Slots> keys{};
	std::array<QIcon,Slots> artwork;
	void refreshArtwork(int slot) {
		const auto &definition = definitions[size_t(slot)];
		artwork[size_t(slot)] = definition.assigned() ? icon(definition.glyph) : icon("Grid");
		if (definition.assigned() && !definition.image.isEmpty()) {
			const auto image = readIcon(imagePath(settingsFile, definition.image));
			if (!image.isNull()) artwork[size_t(slot)] = QIcon(QPixmap::fromImage(image));
		}
	}
	QWidget *surface;
	QGridLayout *grid;
	QScrollArea *scroll;
	QToolButton *editButton;
	QLabel *status;
	QElapsedTimer debounce;
	bool editing = false;
	void relayout() {
		while(auto *item=grid->takeAt(0))delete item;
		const int side=buttonSide(scroll->viewport()->width()-4);
		int index=0,empty=-1;
		for(int slot=0;slot<Slots;++slot){
			auto *key=keys[size_t(slot)];const auto &definition=definitions[size_t(slot)];
			key->assigned=definition.assigned();key->edit=editing;key->setFixedSize(side,side);
			key->setProperty("pulseKeypadConfigured",definition.assigned());key->setProperty("pulseKeypadName",definition.name);
			key->label=definition.assigned()?definition.name:"Add button";
			key->setText(key->label);key->setAccessibleName(definition.assigned()?(editing?"Edit ":"Trigger ")+definition.name+" · "+alertTitle(slot+1):"Add Lumia keypad button");
			key->setToolTip(definition.assigned()?definition.name+"\nLumia: "+alertTitle(slot+1):"Add a named button and choose its icon");
			key->artwork=artwork[size_t(slot)];
			if(definition.assigned()){grid->addWidget(key,index/Columns,index%Columns);++index;key->show();}
			else{key->hide();if(empty<0)empty=slot;}
			key->update();
		}
		if(empty>=0){auto *key=keys[size_t(empty)];grid->addWidget(key,index/Columns,index%Columns);key->show();}
	}
	void configure(int slot) {
		auto value=definitions[size_t(slot)];QImage imported;
		QDialog dialog(this);dialog.setWindowTitle("Lumia keypad button");dialog.setProperty("pulseSetupDialog",true);
		auto *layout=new QVBoxLayout(&dialog);layout->addWidget(new QLabel(alertTitle(slot+1)+" · PulseWeaver Stream Studio"));
		auto *name=new QLineEdit(value.name);name->setMaxLength(32);name->setPlaceholderText("Button name, e.g. Gameplay");name->setAccessibleName("Button name");layout->addWidget(name);
		auto *help=new QLabel("In Lumia, find this numbered alert and add the scene change or other reactions you want. Renaming this button keeps the same alert.");help->setWordWrap(true);layout->addWidget(help);
		auto *choices=new QGridLayout;auto *group=new QButtonGroup(&dialog);group->setExclusive(true);layout->addLayout(choices);
		int index=0;
		for(const auto &glyph:iconNames()){
			auto *choice=new QToolButton(&dialog);choice->setIcon(icon(glyph));choice->setIconSize(QSize(28,28));choice->setText(glyph);choice->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);choice->setCheckable(true);choice->setMinimumSize(76,64);choice->setAccessibleName(glyph+" icon");group->addButton(choice);
			choice->setChecked(value.image.isEmpty()&&value.glyph==glyph);choices->addWidget(choice,index/4,index%4);++index;
			connect(choice,&QToolButton::clicked,&dialog,[&,glyph]{value.glyph=glyph;value.image.clear();imported={};});
		}
		auto *custom=new QPushButton(value.image.isEmpty()?"Use my image…":"Replace my image…");custom->setAccessibleName("Choose custom button icon");layout->addWidget(custom);
		auto *error=new QLabel;error->setTextFormat(Qt::PlainText);error->setWordWrap(true);layout->addWidget(error);
		connect(custom,&QPushButton::clicked,&dialog,[&]{
			const auto file=QFileDialog::getOpenFileName(&dialog,"Button icon",{},"Images (*.png *.jpg *.jpeg *.webp *.bmp)");if(file.isEmpty())return;
			const auto image=readIcon(file);if(image.isNull()){error->setText("Choose a readable image under 16 MB and 8192 pixels per side.");return;}
			imported=image;group->setExclusive(false);for(auto *button:group->buttons())button->setChecked(false);group->setExclusive(true);custom->setText("My image selected");custom->setIcon(QIcon(QPixmap::fromImage(image)));error->clear();
		});
		auto *buttons=new QDialogButtonBox(QDialogButtonBox::Save|QDialogButtonBox::Cancel);layout->addWidget(buttons);
		if(value.assigned()){
			auto *clear=buttons->addButton("Clear button",QDialogButtonBox::ResetRole);connect(clear,&QPushButton::clicked,&dialog,[&]{name->clear();value={};imported={};});
		}
		connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
		connect(buttons,&QDialogButtonBox::accepted,&dialog,[&]{
			value.name=cleanName(name->text());
			if(value.name.isEmpty()&&!definitions[size_t(slot)].assigned()){error->setText("Give this button a name.");return;}
			if(!imported.isNull() && value.assigned()){
				value.image="keypad-icons/"+QUuid::createUuid().toString(QUuid::WithoutBraces)+".png";
				const QString file=imagePath(settingsFile,value.image);QDir().mkpath(QFileInfo(file).absolutePath());QSaveFile output(file);
				if(!output.open(QIODevice::WriteOnly)||!imported.save(&output,"PNG")||!output.commit()){error->setText("The custom icon could not be saved. Your button has not changed.");return;}
			}
			QSettings settings(settingsFile,QSettings::IniFormat);save(settings,slot+1,value);
			if(settings.status()!=QSettings::NoError){error->setText("The button settings could not be saved.");return;}
			definitions[size_t(slot)]=value;refreshArtwork(slot);dialog.accept();
		});
		dialog.setMinimumWidth(360);if(dialog.exec()==QDialog::Accepted){status->setText(value.assigned()?"Use Lumia alert “"+alertTitle(slot+1)+"”.":"Button cleared.");relayout();}
	}
public:
	explicit Panel(const QString &file,QWidget *parent=nullptr):QWidget(parent),settingsFile(file){
		setObjectName("PulseWeaverKeypadPanel");setProperty("pulseKeypadEditing",false);
		auto *layout=new QVBoxLayout(this);layout->setContentsMargins(0,0,0,0);layout->setSpacing(8);
		scroll=new QScrollArea(this);scroll->setWidgetResizable(true);scroll->setFrameShape(QFrame::NoFrame);scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);layout->addWidget(scroll,1);
		surface=new QWidget(scroll);grid=new QGridLayout(surface);grid->setContentsMargins(2,2,2,2);grid->setSpacing(Gap);grid->setAlignment(Qt::AlignTop|Qt::AlignHCenter);scroll->setWidget(surface);
		QSettings settings(settingsFile,QSettings::IniFormat);
		for(int slot=0;slot<Slots;++slot){definitions[size_t(slot)]=load(settings,slot+1);refreshArtwork(slot);auto *key=new Key(surface);keys[size_t(slot)]=key;key->slot=slot+1;key->setObjectName(QString("PulseWeaverKeypad%1").arg(slot+1,2,10,QLatin1Char('0')));key->setProperty("pulseKeypadSlot",slot+1);
			connect(key,&QToolButton::clicked,this,[this,key,slot]{
				key->setProperty("pulseKeypadTriggerRequested",false);
				if(editing||!definitions[size_t(slot)].assigned()){configure(slot);return;}
				if(debounce.isValid()&&debounce.elapsed()<300)return;debounce.restart();
				key->setProperty("pulseKeypadTriggerRequested",true);
				status->setText(property("pulseKeypadBound").toBool()?"Sending to Lumia…":"The Lumia bridge is not available in this build.");
				key->flash=true;key->update();QTimer::singleShot(180,key,[key]{key->flash=false;key->update();});
			});
		}
		status=new QLabel("Add up to 15 buttons. Each has its own numbered Lumia alert.",this);status->setObjectName("PulseWeaverKeypadStatus");status->setTextFormat(Qt::PlainText);status->setWordWrap(true);layout->addWidget(status);
		editButton=new QToolButton(this);editButton->setText("Edit buttons");editButton->setCheckable(true);editButton->setAccessibleName("Edit Lumia keypad buttons");layout->addWidget(editButton);
		connect(editButton,&QToolButton::toggled,this,[this](bool enabled){editing=enabled;setProperty("pulseKeypadEditing",enabled);editButton->setText(enabled?"Done editing":"Edit buttons");relayout();});
		relayout();
	}
protected:
	void resizeEvent(QResizeEvent *event) override {QWidget::resizeEvent(event);QTimer::singleShot(0,this,[this]{relayout();});}
};
} // namespace PulseKeypad
