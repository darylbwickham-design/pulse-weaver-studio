#pragma once

#include <QObject>
#include <QJsonObject>
#include <QStringList>

#include <functional>
#include <memory>

class QWidget;

class PulseOverlayRuntime final : public QObject {
public:
	using StatusCallback = std::function<void(const QString &)>;

	explicit PulseOverlayRuntime(QObject *parent, StatusCallback status);
	~PulseOverlayRuntime() override;

	void bindShell(QWidget *mainWindow);
	void sceneCollectionLoaded();
	void openEditor();
	void publishEvent(const QString &eventKey, const QJsonObject &data);
	bool triggerOverlay(const QString &nameOrId, const QJsonObject &data = {});
	QStringList overlayNames() const;
	QString overlayIdForName(const QString &name) const;
	QString urlFor(const QString &id) const;
	QJsonObject catalogueJson() const;
	bool createOverlay(const QString &name, int width, int height, const QString &text,
			   const QString &triggerEvent, QString *createdId = nullptr);
	bool removeOverlay(const QString &nameOrId);
	bool addToCurrentScene(const QString &nameOrId, QString *error = nullptr);
	bool addToCameraScenes(const QString &nameOrId, const QString &target, QString *error = nullptr);
	bool skipAlert(const QString &nameOrId);
	void setAlertsMuted(bool muted);
	bool alertsMuted() const;

private:
	struct Impl;
	std::unique_ptr<Impl> impl;
};
