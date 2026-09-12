/******************************************************************************
    Copyright (C) 2023 by Lain Bailey <lain@obsproject.com>
                          Zachary Lund <admin@computerquip.com>
                          Philippe Groarke <philippe.groarke@gmail.com>

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
******************************************************************************/

#include "OBSBasic.hpp"

#include <qt-wrappers.hpp>

void setupDockAction(QDockWidget *dock)
{
	QAction *action = dock->toggleViewAction();

	auto neverDisable = [action]() {
		QSignalBlocker block(action);
		action->setEnabled(true);
	};

	auto newToggleView = [dock](bool check) {
		QSignalBlocker block(dock);
		dock->setVisible(check);
	};

	// Replace the slot connected by default
	QObject::disconnect(action, &QAction::triggered, nullptr, 0);
	QObject::connect(action, &QAction::triggered, dock, newToggleView);

	// Make the action unable to be disabled
	QObject::connect(action, &QAction::enabledChanged, action, neverDisable);
}

void OBSBasic::on_resetDocks_triggered(bool force)
{
#ifdef BROWSER_AVAILABLE
	if ((extraDocks.size() || extraCustomDocks.size() || extraBrowserDocks.size()) && !force)
#else
	if ((extraDocks.size() || extraCustomDocks.size()) && !force)
#endif
	{
		QMessageBox::StandardButton button =
			OBSMessageBox::question(this, QTStr("ResetUIWarning.Title"), QTStr("ResetUIWarning.Text"));

		if (button == QMessageBox::No) {
			return;
		}
	}

#define RESET_DOCKLIST(dockList)                                                                               \
	for (int i = dockList.size() - 1; i >= 0; i--) {                                                       \
		dockList[i]->setVisible(true);                                                                 \
		dockList[i]->setFloating(true);                                                                \
		dockList[i]->move(frameGeometry().topLeft() + rect().center() - dockList[i]->rect().center()); \
		dockList[i]->setVisible(false);                                                                \
	}

	RESET_DOCKLIST(extraDocks)
	RESET_DOCKLIST(extraCustomDocks)
#ifdef BROWSER_AVAILABLE
	RESET_DOCKLIST(extraBrowserDocks)
#endif
#undef RESET_DOCKLIST

	QMainWindow *dockHost = pulseCameraDockHost ? pulseCameraDockHost.data() : this;
	if (pulseCameraDockHost && !pulseCameraStartingDockLayout.isEmpty())
		pulseCameraDockHost->restoreState(pulseCameraStartingDockLayout, 1);
	else
		restoreState(startingDockLayout);

	/* Saved layouts from early Pulse Weaver builds could leave the native
	 * docks floating without a valid docking path. Rebuild the same layout
	 * through QMainWindow's public docking API, as upstream OBS does, so Qt
	 * owns every docking target and subsequent title-bar drags snap normally. */
	dockHost->setDockOptions(QMainWindow::AnimatedDocks | QMainWindow::AllowNestedDocks |
		       QMainWindow::AllowTabbedDocks | QMainWindow::GroupedDragging);
	dockHost->setDockNestingEnabled(true);
	for (QDockWidget *dock : QList<QDockWidget *>{ui->scenesDock, ui->sourcesDock, ui->mixerDock,
						      ui->transitionsDock, controlsDock}) {
		dock->setAllowedAreas(Qt::AllDockWidgetAreas);
		dock->setFloating(false);
		dockHost->removeDockWidget(dock);
	}
	dockHost->addDockWidget(Qt::LeftDockWidgetArea, ui->scenesDock);
	dockHost->addDockWidget(Qt::RightDockWidgetArea, ui->sourcesDock);
	dockHost->addDockWidget(Qt::BottomDockWidgetArea, ui->mixerDock);
	dockHost->addDockWidget(Qt::BottomDockWidgetArea, ui->transitionsDock);
	dockHost->splitDockWidget(ui->mixerDock, ui->transitionsDock, Qt::Horizontal);
	ui->sideDocks->setChecked(true);

	int cx = width();
	int bottomDocksHeight = height();

	bottomDocksHeight = bottomDocksHeight * 225 / 1000;

	ui->scenesDock->setVisible(true);
	ui->sourcesDock->setVisible(true);
	ui->mixerDock->setVisible(true);
	ui->transitionsDock->setVisible(true);
	controlsDock->setVisible(false);
	statsDock->setVisible(false);
	statsDock->setFloating(true);

	QList<QDockWidget *> bottomDocks{ui->mixerDock, ui->transitionsDock};

	dockHost->resizeDocks(bottomDocks, {bottomDocksHeight, bottomDocksHeight}, Qt::Vertical);
	dockHost->resizeDocks(bottomDocks, {cx * 70 / 100, cx * 30 / 100}, Qt::Horizontal);

	int sideDockWidth = std::min(width() * 30 / 100, 280);
	dockHost->resizeDocks({ui->scenesDock, ui->sourcesDock}, {sideDockWidth, sideDockWidth}, Qt::Horizontal);

	activateWindow();
}

void OBSBasic::on_lockDocks_toggled(bool lock)
{
	QMainWindow *dockHost = pulseCameraDockHost ? pulseCameraDockHost.data() : this;
	dockHost->setDockOptions(QMainWindow::AnimatedDocks | QMainWindow::AllowNestedDocks |
		       QMainWindow::AllowTabbedDocks | QMainWindow::GroupedDragging);
	dockHost->setDockNestingEnabled(true);
	QDockWidget::DockWidgetFeatures features =
		lock ? QDockWidget::NoDockWidgetFeatures
		     : (QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable |
			QDockWidget::DockWidgetFloatable);

	QDockWidget::DockWidgetFeatures mainFeatures = features;
	mainFeatures &= ~QDockWidget::QDockWidget::DockWidgetClosable;

	ui->scenesDock->setFeatures(mainFeatures);
	ui->sourcesDock->setFeatures(mainFeatures);
	ui->mixerDock->setFeatures(mainFeatures);
	ui->transitionsDock->setFeatures(mainFeatures);
	controlsDock->setFeatures(mainFeatures);
	statsDock->setFeatures(features);

	for (int i = extraDocks.size() - 1; i >= 0; i--) {
		extraDocks[i]->setFeatures(features);
	}

	for (int i = extraCustomDocks.size() - 1; i >= 0; i--) {
		extraCustomDocks[i]->setFeatures(features);
	}

#ifdef BROWSER_AVAILABLE
	for (int i = extraBrowserDocks.size() - 1; i >= 0; i--) {
		extraBrowserDocks[i]->setFeatures(features);
	}
#endif
}

void OBSBasic::on_sideDocks_toggled(bool side)
{
	config_set_bool(App()->GetUserConfig(), "BasicWindow", "SideDocks", side);

	setDockCornersVertical(side);
}

void OBSBasic::AddDockWidget(QDockWidget *dock, Qt::DockWidgetArea area, bool extraBrowser)
{
	if (dock->objectName().isEmpty()) {
		return;
	}

	bool lock = ui->lockDocks->isChecked();
	QDockWidget::DockWidgetFeatures features =
		lock ? QDockWidget::NoDockWidgetFeatures
		     : (QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable |
			QDockWidget::DockWidgetFloatable);

	setupDockAction(dock);
	dock->setFeatures(features);
	QMainWindow *dockHost = pulseCameraDockHost ? pulseCameraDockHost.data() : this;
	/* Third-party docks can be created after Camera's embedded QMainWindow has
	 * taken ownership of the standard docks.  Explicitly give every new dock
	 * the same docking contract as those standard docks; otherwise Qt accepts
	 * the drag but has no valid drop areas inside the Camera workspace. */
	dockHost->setDockOptions(QMainWindow::AnimatedDocks | QMainWindow::AllowNestedDocks |
			       QMainWindow::AllowTabbedDocks | QMainWindow::GroupedDragging);
	dockHost->setDockNestingEnabled(true);
	dock->setAllowedAreas(Qt::AllDockWidgetAreas);
	dock->setFloating(false);
	dockHost->addDockWidget(area, dock);

#ifdef BROWSER_AVAILABLE
	if (extraBrowser && extraBrowserMenuDocksSeparator.isNull()) {
		extraBrowserMenuDocksSeparator = ui->menuDocks->addSeparator();
	}

	if (!extraBrowser && !extraBrowserMenuDocksSeparator.isNull()) {
		ui->menuDocks->insertAction(extraBrowserMenuDocksSeparator, dock->toggleViewAction());
	} else {
		ui->menuDocks->addAction(dock->toggleViewAction());
	}

	if (extraBrowser) {
		return;
	}
#else
	UNUSED_PARAMETER(extraBrowser);

	ui->menuDocks->addAction(dock->toggleViewAction());
#endif

	extraDockNames.push_back(dock->objectName());
	extraDocks.push_back(std::shared_ptr<QDockWidget>(dock));
}

void OBSBasic::RemoveDockWidget(const QString &name)
{
	if (extraDockNames.contains(name)) {
		int idx = extraDockNames.indexOf(name);
		extraDockNames.removeAt(idx);
		extraDocks[idx].reset();
		extraDocks.removeAt(idx);
	} else if (extraCustomDockNames.contains(name)) {
		int idx = extraCustomDockNames.indexOf(name);
		extraCustomDockNames.removeAt(idx);
		if (pulseCameraDockHost)
			pulseCameraDockHost->removeDockWidget(extraCustomDocks[idx]);
		else
			removeDockWidget(extraCustomDocks[idx]);
		extraCustomDocks.removeAt(idx);
	}
}

bool OBSBasic::IsDockObjectNameUsed(const QString &name)
{
	QStringList list;
	list << "scenesDock"
	     << "sourcesDock"
	     << "mixerDock"
	     << "transitionsDock"
	     << "controlsDock"
	     << "statsDock";
	list << extraDockNames;
	list << extraCustomDockNames;

	return list.contains(name);
}

void OBSBasic::AddCustomDockWidget(QDockWidget *dock)
{
	// Prevent the object name from being changed
	connect(dock, &QObject::objectNameChanged, this, &OBSBasic::RepairCustomExtraDockName);

	bool lock = ui->lockDocks->isChecked();
	QDockWidget::DockWidgetFeatures features =
		lock ? QDockWidget::NoDockWidgetFeatures
		     : (QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable |
			QDockWidget::DockWidgetFloatable);

	setupDockAction(dock);
	dock->setFeatures(features);
	QMainWindow *dockHost = pulseCameraDockHost ? pulseCameraDockHost.data() : this;
	dockHost->setDockOptions(QMainWindow::AnimatedDocks | QMainWindow::AllowNestedDocks |
			       QMainWindow::AllowTabbedDocks | QMainWindow::GroupedDragging);
	dockHost->setDockNestingEnabled(true);
	dock->setAllowedAreas(Qt::AllDockWidgetAreas);
	dock->setFloating(false);
	dockHost->addDockWidget(Qt::RightDockWidgetArea, dock);

	extraCustomDockNames.push_back(dock->objectName());
	extraCustomDocks.push_back(dock);
}

void OBSBasic::setDockCornersVertical(bool vertical)
{
	QMainWindow *dockHost = pulseCameraDockHost ? pulseCameraDockHost.data() : this;
	if (vertical) {
		dockHost->setCorner(Qt::TopLeftCorner, Qt::LeftDockWidgetArea);
		dockHost->setCorner(Qt::TopRightCorner, Qt::RightDockWidgetArea);
		dockHost->setCorner(Qt::BottomLeftCorner, Qt::LeftDockWidgetArea);
		dockHost->setCorner(Qt::BottomRightCorner, Qt::RightDockWidgetArea);
	} else {
		dockHost->setCorner(Qt::TopLeftCorner, Qt::TopDockWidgetArea);
		dockHost->setCorner(Qt::TopRightCorner, Qt::TopDockWidgetArea);
		dockHost->setCorner(Qt::BottomLeftCorner, Qt::BottomDockWidgetArea);
		dockHost->setCorner(Qt::BottomRightCorner, Qt::BottomDockWidgetArea);
	}
}

void OBSBasic::RepairCustomExtraDockName()
{
	QDockWidget *dock = reinterpret_cast<QDockWidget *>(sender());
	int idx = extraCustomDocks.indexOf(dock);
	QSignalBlocker block(dock);

	if (idx == -1) {
		blog(LOG_WARNING, "A custom dock got its object name changed");
		return;
	}

	blog(LOG_WARNING, "The custom dock '%s' got its object name restored", QT_TO_UTF8(extraCustomDockNames[idx]));

	dock->setObjectName(extraCustomDockNames[idx]);
}
