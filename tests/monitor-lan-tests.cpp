// LAN access to the monitor through the real session and HTTP server.
#include "fff-http-server.h"
#include "fff-monitor-access.h"
#include "fff-session.h"
#include "obs-stubs.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>

static void check(bool ok, const char *message)
{
	if (!ok)
		qFatal("%s", message);
}

// A plain file where the config directory should be makes every save fail.
static QString blockedConfig(const QTemporaryDir &temp)
{
	const QString path = temp.filePath(QStringLiteral("blocked"));
	QFile file(path);
	check(file.open(QIODevice::WriteOnly), "create blocked config");
	return path;
}

static void testSession(FffSession &session, const QTemporaryDir &temp)
{
	check(!session.monitorLanEnabled() && session.monitorKey().isEmpty(), "LAN monitor starts off without a key");
	int accessChanges = 0;
	const auto counter =
		QObject::connect(&session, &FffSession::monitorAccessChanged, [&]() { ++accessChanges; });

	check(session.setMonitorLanEnabled(true), "switch LAN monitor on");
	const QString firstKey = session.monitorKey();
	check(session.monitorLanEnabled() && firstKey.size() == 32 && accessChanges == 1,
	      "switching on mints a key and announces it");
	FffSession reloaded;
	reloaded.load();
	check(reloaded.monitorLanEnabled() && reloaded.monitorKey() == firstKey, "switch and key survive a restart");

	check(session.setMonitorLanEnabled(false) && session.monitorKey() == firstKey && accessChanges == 2,
	      "switching off keeps the key");
	check(session.setMonitorLanEnabled(true) && session.monitorKey() == firstKey && accessChanges == 3,
	      "switching on again reuses the key");
	check(session.setMonitorLanEnabled(true) && accessChanges == 3, "an unchanged switch announces nothing");
	check(session.regenerateMonitorKey() && session.monitorKey() != firstKey && accessChanges == 4,
	      "regenerating retires the old key");

	const QByteArray key = session.monitorKey().toUtf8();
	for (const QByteArray &json : {session.overlayStateJson(), session.phoneStateJson(QString())})
		check(!json.contains(key) && !json.contains("monitorKey") && !json.contains("monitorLanEnabled"),
		      "page state never carries the key");

	// A hand-edited file cannot switch access on without a well-formed key.
	QFile file(temp.filePath(QStringLiteral("session.json")));
	check(file.open(QIODevice::WriteOnly | QIODevice::Truncate), "open session for tampering");
	file.write(R"({"version":5,"presidents":[],"monitorLanEnabled":true,"monitorKey":"not-a-key"})");
	file.close();
	FffSession tampered;
	tampered.load();
	check(!tampered.monitorLanEnabled() && tampered.monitorKey().isEmpty(), "a malformed key loads switched off");
	check(session.save(), "restore the session file");

	int failures = 0;
	const auto failed = QObject::connect(&session, &FffSession::saveFailed, [&]() { ++failures; });
	const QString keyBeforeFailure = session.monitorKey();
	fffTestConfigPath = blockedConfig(temp);
	check(!session.setMonitorLanEnabled(false) && session.monitorLanEnabled(), "a failed switch-off rolls back");
	check(!session.regenerateMonitorKey() && session.monitorKey() == keyBeforeFailure,
	      "a failed regeneration keeps the key");
	check(failures == 2 && accessChanges == 4, "failures are reported and announce no access change");
	fffTestConfigPath = temp.path();
	QObject::disconnect(failed);
	QObject::disconnect(counter);
}

int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	QTemporaryDir temp;
	check(temp.isValid(), "temporary config");
	fffTestConfigPath = temp.path();

	FffSession session;
	testSession(session, temp);
	qInfo("PASS: LAN monitor session");
	return 0;
}
