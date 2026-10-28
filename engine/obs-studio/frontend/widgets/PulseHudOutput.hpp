#pragma once

#include <QObject>
#include <QRect>
#include <QString>
#include <obs.h>
#include <functional>
#include <memory>

class QScreen;
class QImage;

struct PulseHudScreen {
	QString name;
	QString serial;
	QRect pixels;
	bool valid() const { return !name.isEmpty() && pixels.width() > 0 && pixels.height() > 0; }
};

/* The desktop HUD owns a GPU composition surface, never an encoder or a
 * broadcast channel. All public methods run on the Qt thread. */
class PulseHudOutput final : public QObject {
public:
	explicit PulseHudOutput(QObject *parent = nullptr);
	~PulseHudOutput() override;
	static PulseHudScreen Describe(QScreen *screen);
	QString Start(obs_source_t *scene, const PulseHudScreen &screen);
	void Stop();
	bool Running() const;
	bool Active() const;
	std::function<void(bool, const QString &)> stateChanged;
#ifdef PULSE_HUD_TESTS
	QImage ReadbackForTest();
	quintptr WindowForTest() const;
	uint64_t FramesForTest() const;
#endif

private:
	struct Impl;
	std::unique_ptr<Impl> impl;
};
