#pragma once

#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QMessageBox>
#include <QUrl>
#include <QWidget>

namespace PulseManual {
inline QString path()
{
	const QDir bin(QCoreApplication::applicationDirPath());
	const QString relative = "../../docs/manual/Pulse-Weaver-Studio-Nerds-Introduction.html";
	const QString installed = bin.absoluteFilePath(relative);
	return QFileInfo::exists(installed) ? QFileInfo(installed).absoluteFilePath() : QString{};
}

inline bool exists()
{
	return !path().isEmpty();
}

inline void open(QWidget *parent)
{
	const QString manual = path();
	if (manual.isEmpty() || !QDesktopServices::openUrl(QUrl::fromLocalFile(manual)))
		QMessageBox::warning(parent, "Pulse Weaver manual",
				     "The bundled manual could not be opened. Repair or reinstall Pulse Weaver to restore it.");
}
} // namespace PulseManual
