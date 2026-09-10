// Exercise the real session and HTTP server with an isolated OBS config path.
#include "fff-session.h"
#include "fff-http-server.h"

#include <obs-module.h>
#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QTemporaryDir>
#include <QTimer>
#include <cstdlib>
#include <cstring>

static QString configPath;
extern "C" {
obs_module_t *obs_current_module(void)
{
	return nullptr;
}
char *obs_module_get_config_path(obs_module_t *, const char *file)
{
	return strdup(QDir(configPath).filePath(QString::fromUtf8(file)).toUtf8().constData());
}
char *obs_find_module_file(obs_module_t *, const char *)
{
	return nullptr;
}
void bfree(void *ptr)
{
	free(ptr);
}
int os_mkdirs(const char *path)
{
	return QDir().mkpath(QString::fromUtf8(path)) ? 0 : -1;
}
void obs_log(int, const char *, ...) {}
}

static void check(bool ok, const char *message)
{
	if (!ok)
		qFatal("%s", message);
}

static QJsonObject state(const FffSession &session)
{
	return QJsonDocument::fromJson(session.overlayStateJson()).object();
}

int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	QTemporaryDir temp;
	check(temp.isValid(), "temporary config");
	configPath = temp.path();
	FffSession session;
	FffPresident person;
	person.id = QStringLiteral("one");
	person.pin = QStringLiteral("123456");
	session.addPresident(person);
	session.setLayout({0.4, 0.6, 1.2});
	FffSession legacy;
	legacy.load();
	check(legacy.layout().x == 0.4 && state(legacy).value("pieces").toObject().isEmpty(), "legacy layout loads");

	FffHttpServer server(&session);
	QString error;
	check(server.start(0, &error), "HTTP starts");
	QNetworkAccessManager network;
	auto post = [&](const QByteArray &body) {
		QNetworkRequest request(QUrl(QStringLiteral("http://127.0.0.1:%1/api/layout").arg(server.boundPort())));
		request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
		auto *reply = network.post(request, body);
		QEventLoop loop;
		QTimer timer;
		timer.setSingleShot(true);
		QObject::connect(&timer, &QTimer::timeout, reply, &QNetworkReply::abort);
		QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
		timer.start(3000);
		loop.exec();
		const int code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
		reply->deleteLater();
		return code;
	};
	check(post(R"({"target":"card:one","layout":{"x":0.2,"y":0.3,"scale":1.5}})") == 200, "save card");
	check(post(R"({"target":"heading","layout":{"x":0.5,"y":0.1,"scale":0.8}})") == 200, "save heading");
	const auto saved = state(session).value("pieces");
	check(post(R"({"target":"card:missing","layout":{"x":0,"y":0,"scale":1}})") == 404, "unknown ID rejected");
	check(post(R"({"target":"card:one","layout":{"x":"bad","y":0,"scale":1}})") == 400, "invalid number rejected");
	check(post(R"({"target":"card:one","layout":{"x":0}})") == 400, "missing fields rejected");
	check(state(session).value("pieces") == saved, "invalid requests do not change layout");
	session.setVote(person.id, FffVote::Red);
	session.forceReveal();
	check(session.phase() == FffPhase::Revealed, "reveal works");
	session.clearRound();
	check(session.phase() == FffPhase::Collecting && session.votedCount() == 0, "clear works");
	check(state(session).value("pieces") == saved, "clear preserves layouts");
	FffSession reopened;
	reopened.load();
	check(state(reopened).value("pieces") == saved, "restart preserves layouts");
	check(!QJsonDocument::fromJson(session.phoneStateJson(person.id)).object().contains("pieces"),
	      "phone has no layouts");
	check(post(R"({"target":"card:one","reset":true})") == 200, "reset card");
	check(state(session).value("pieces").toObject().size() == 1, "reset card preserves heading");
	check(post(R"({"target":"card:one","layout":{"x":-1,"y":2,"scale":8}})") == 200, "clamp layout");
	const auto clamped = state(session).value("pieces").toObject().value("card:one").toObject();
	check(clamped.value("x").toDouble() == 0 && clamped.value("y").toDouble() == 1 &&
		      clamped.value("scale").toDouble() == 2,
	      "layout bounds");

	const auto beforeFailure = state(session);
	QFile blocked(temp.filePath(QStringLiteral("blocked")));
	check(blocked.open(QIODevice::WriteOnly), "create blocked config");
	blocked.close();
	configPath = blocked.fileName();
	check(post(R"({"target":"heading","reset":true})") == 500, "failed write reported");
	check(post(R"({"target":"all","reset":true})") == 500, "failed reset reported");
	check(state(session) == beforeFailure, "failed saves roll back");
	configPath = temp.path();
	session.removePresident(person.id);
	check(!state(session).value("pieces").toObject().contains("card:one"), "remove cleans layout");
	check(post(R"({"target":"all","reset":true})") == 200, "reset all");
	check(state(session).value("pieces").toObject().isEmpty() && session.layout().scale == 1 &&
		      session.layout().x == 0.5,
	      "reset all restores default grid");
	qInfo("PASS: layout API, legacy session, persistence, rounds, validation, resets, failed writes");
}
