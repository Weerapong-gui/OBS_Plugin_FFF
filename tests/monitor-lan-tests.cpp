// LAN access to the monitor through the real session and HTTP server.
#include "fff-http-server.h"
#include "fff-monitor-access.h"
#include "fff-session.h"
#include "obs-stubs.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTimer>

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

// Raw HTTP so redirects and cookies arrive exactly as the server wrote them.
static QByteArray rawHttp(const QString &host, quint16 port, const QByteArray &request)
{
	QTcpSocket socket;
	QByteArray response;
	QEventLoop loop;
	QTimer timer;
	timer.setSingleShot(true);
	QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
	QObject::connect(&socket, &QTcpSocket::connected, [&]() { socket.write(request); });
	QObject::connect(&socket, &QTcpSocket::readyRead, [&]() { response += socket.readAll(); });
	QObject::connect(&socket, &QTcpSocket::disconnected, &loop, &QEventLoop::quit);
	QObject::connect(&socket, &QTcpSocket::errorOccurred, &loop, &QEventLoop::quit);
	socket.connectToHost(host, port);
	timer.start(5000);
	loop.exec();
	response += socket.readAll();
	return response;
}

static QByteArray httpRequest(const QByteArray &method, const QString &host, const QByteArray &target,
			      const QByteArray &cookie = QByteArray(), const QByteArray &body = QByteArray())
{
	QByteArray out = method + ' ' + target + " HTTP/1.1\r\nHost: " + host.toUtf8() + "\r\n";
	if (!cookie.isEmpty())
		out += "Cookie: " + cookie + "\r\n";
	if (method == "POST")
		out += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
	return out + "Connection: close\r\n\r\n" + body;
}

static int statusOf(const QByteArray &response)
{
	const QList<QByteArray> parts = response.left(response.indexOf("\r\n")).split(' ');
	return parts.size() >= 2 ? parts.at(1).toInt() : 0;
}

static QByteArray headerOf(const QByteArray &response, const QByteArray &name)
{
	const QByteArray prefix = name.toLower() + ':';
	for (const QByteArray &line : response.left(response.indexOf("\r\n\r\n")).split('\n')) {
		const QByteArray trimmed = line.trimmed();
		if (trimmed.toLower().startsWith(prefix))
			return trimmed.mid(prefix.size()).trimmed();
	}
	return QByteArray();
}

static QByteArray bodyOf(const QByteArray &response)
{
	const qsizetype end = response.indexOf("\r\n\r\n");
	return end < 0 ? QByteArray() : response.mid(end + 4);
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

static void testServer(FffSession &session, FffHttpServer &server)
{
	const quint16 port = server.boundPort();
	const auto call = [&](const QByteArray &method, const QString &host, const QByteArray &target,
			      const QByteArray &cookie = QByteArray(), const QByteArray &body = QByteArray()) {
		return rawHttp(host, port, httpRequest(method, host, target, cookie, body));
	};
	const QString local = QStringLiteral("127.0.0.1");

	check(statusOf(call("GET", local, "/api/monitor/access")) == 204, "this machine passes the access probe");
	check(bodyOf(call("GET", local, "/monitor-ui.js")) == "missing asset", "monitor-ui.js is routed to the web folder");
	check(statusOf(call("POST", local, "/api/display", QByteArray(), R"({"mode":"scoreboard"})")) == 200 &&
		      session.phase() == FffPhase::Revealed,
	      "this machine still drives the display");
	check(session.hideDisplay(), "blank the board again");

	FffPresident person;
	person.id = QStringLiteral("lan");
	person.pin = QStringLiteral("654321");
	check(session.addPresident(person), "president for uploads");

	const QStringList lan = FffHttpServer::lanAddresses();
	if (lan.isEmpty()) {
		qInfo("SKIP: no LAN address; remote monitor access checks need one");
		return;
	}
	const QString host = lan.first();
	QElapsedTimer clock;

	check(session.setMonitorLanEnabled(false), "switch LAN monitor off");
	const QByteArray key = session.monitorKey().toUtf8();
	clock.start();
	check(statusOf(call("GET", host, "/monitor?key=" + key)) == 403 && clock.elapsed() < 900,
	      "switched off refuses the right link without a delay");
	check(session.setMonitorLanEnabled(true), "switch LAN monitor on");

	const QByteArray cookie = "fff_monitor=" + key;
	check(statusOf(call("GET", host, "/monitor")) == 403, "no key is refused");
	clock.restart();
	const QByteArray guessed = call("GET", host, "/monitor?key=00000000000000000000000000000000");
	check(statusOf(guessed) == 403 && clock.elapsed() >= 900, "a wrong key is refused slowly");
	check(headerOf(guessed, "Content-Type").startsWith("text/html") && bodyOf(guessed).contains("dock"),
	      "the refusal page says where the link lives");

	const QByteArray redirect = call("GET", host, "/monitor?key=" + key);
	check(statusOf(redirect) == 303 && headerOf(redirect, "Location") == "/monitor",
	      "the link redirects to a clean URL");
	check(headerOf(redirect, "Set-Cookie") == FffMonitorAccess::setCookieHeader(session.monitorKey()),
	      "the redirect hands over the cookie");
	check(bodyOf(call("GET", host, "/monitor", cookie)) == "missing asset", "the cookie opens the monitor page");
	check(statusOf(call("GET", host, "/api/monitor/access", cookie)) == 204, "the cookie passes the access probe");
	check(statusOf(call("GET", host, "/api/monitor/access")) == 403, "the probe refuses strangers");
	check(statusOf(call("POST", host, "/api/layout", cookie, R"({"target":"heading","reset":true})")) == 200,
	      "the cookie saves layout");
	check(statusOf(call("POST", host, "/api/display", cookie, R"({"mode":"bottomBar"})")) == 403 &&
		      session.phase() == FffPhase::Collecting,
	      "the cookie never reaches the display");
	check(statusOf(call("POST", host, "/api/logo", cookie, R"({"presidentId":"lan"})")) == 403 &&
		      session.logoPresidentId().isEmpty(),
	      "the cookie never reaches the logo");

	const QByteArray large(100 * 1024, 'x');
	const QByteArray upload = call("POST", host, "/api/asset?presidentId=lan&kind=qualified", cookie, large);
	check(statusOf(upload) == 400 && bodyOf(upload).contains("not a PNG"), "the cookie lifts the LAN upload cap");
	const QByteArray stranger = call("POST", host, "/api/asset?presidentId=lan&kind=qualified", QByteArray(), large);
	check(!bodyOf(stranger).contains("not a PNG") && session.presidentById(QStringLiteral("lan"))->qualified.isEmpty(),
	      "strangers never reach the upload handler");

	check(session.setMonitorLanEnabled(false), "switch LAN monitor off again");
	check(statusOf(call("GET", host, "/api/monitor/access", cookie)) == 403, "switching off retires the cookie");
	check(session.setMonitorLanEnabled(true), "leave LAN monitor on");
}

int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	QTemporaryDir temp;
	check(temp.isValid(), "temporary config");
	fffTestConfigPath = temp.path();

	FffSession session;
	testSession(session, temp);

	FffHttpServer server(&session);
	QString error;
	check(server.start(0, &error), "HTTP starts");
	testServer(session, server);
	qInfo("PASS: LAN monitor session and server access");
	return 0;
}
