#include "../../engine/obs-studio/shared/qt/PulseGitHubUpdater.hpp"
#include <QThread>
#include <cstdio>

int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	const auto args = app.arguments();
	if (args.size() == 3 && args[1] == "--child") {
		QThread::msleep(1500);
		QFile marker(args[2]);
		return marker.open(QIODevice::WriteOnly) && marker.write("survived") == 8 ? 0 : 2;
	}
	if (args.size() == 3 && args[1] == "--job") {
		HANDLE job = CreateJobObjectW(nullptr, nullptr);
		JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
		limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE | JOB_OBJECT_LIMIT_BREAKAWAY_OK;
		if (!job || !SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits)) ||
		    !AssignProcessToJobObject(job, GetCurrentProcess())) return 3;
		if (!QProcess::startDetached(app.applicationFilePath(), {"--child", args[2] + "/ordinary"})) return 4;
		if (!PulseUpdates::startUpdateInstaller(app.applicationFilePath(), {"--child", args[2] + "/installer"})) return 5;
		// Exiting closes our last job handle, just like the studio's shutdown.
		return 0;
	}
	QTemporaryDir directory;
	if (!directory.isValid()) return 6;
	QProcess parent;
	parent.start(app.applicationFilePath(), {"--job", directory.path()});
	if (!parent.waitForFinished(10000) || parent.exitCode() != 0) return 7;
	for (int n = 0; n < 100 && !QFile::exists(directory.path() + "/installer"); ++n)
		QThread::msleep(50);
	if (!QFile::exists(directory.path() + "/installer") || QFile::exists(directory.path() + "/ordinary")) return 8;
	std::puts("PASS: installer survives studio job exit; ordinary child is terminated");
	return 0;
}
