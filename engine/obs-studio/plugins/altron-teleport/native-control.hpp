#pragma once
#include <QObject>
#include <QJsonObject>
#include <QTcpServer>
#include <QNetworkAccessManager>
#include <QTimer>
#include <QPointer>
#include <functional>
#include <obs.h>

struct AltronHost {
  std::function<QString()> currentScene;
  std::function<void(const QString &, const QString &, const QString &)> select;
  QString settingsPath;
  QString stagePath;
  bool pulseWeaver = true;
  std::function<bool()> outputsActive;
  std::function<void()> save;
};

class AltronControl : public QObject {
  QTcpServer server{this};
  QNetworkAccessManager network{this};
  QTimer observer{this};
  AltronHost host;
  QJsonObject config;
  QJsonObject last{{"ok",true},{"status","ready"}};
  int overlayPort = 8766;
  bool busy = false, observing = false;
  quint64 generation = 0;
  qint64 cueId = 0;
  QString lastCamera;
  int openSockets = 0;
  quint16 requestedPort = 18767;
  using Result = std::function<void(const QJsonObject &, const QString &)>;
  Result completion;
  void accept();
  void reply(QTcpSocket *, int, const QJsonObject &);
  void request(const QString &, const QString &, const QJsonObject &, Result);
  void phase(const QString &, const QJsonObject &, quint64, std::function<void()>);
  void fail(quint64, const QString &, Result);
  void finish(quint64, const QString &, const QString &, Result);
  void startCue(const QString &, const QString &, Result);
  void prepared(quint64, const QString &, const QJsonObject &, const QString &, Result);
  void arrived(quint64, const QString &, const QJsonObject &, Result);
  void cue(const QString &);
  void ensureNative();
  QJsonObject setCamera(const QJsonObject &);
  QJsonObject visibleCamera() const;
  void observe();
  void saveSettings();
public:
  explicit AltronControl(AltronHost, QObject *parent = nullptr, quint16 port = 18767);
  ~AltronControl() override;
  bool listening() const { return server.isListening(); }
  quint16 port() const { return server.serverPort(); }
  QJsonObject status() const;
  static QJsonObject normalize(const QJsonObject &);
  QJsonObject setup(const QString &cutout, const QString &alerts);
  void showDialog();
};

void altronNativeLoad(obs_module_t *);
void altronNativeUnload();
