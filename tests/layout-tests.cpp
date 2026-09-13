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
	check(session.setPort(session.port()), "unchanged port succeeds");
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
	auto post = [&](const QByteArray &body, const QString &endpoint = QStringLiteral("layout")) {
		QNetworkRequest request(
			QUrl(QStringLiteral("http://127.0.0.1:%1/api/%2").arg(server.boundPort()).arg(endpoint)));
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
	auto postLayer = [&](const QByteArray &body) {
		QNetworkRequest request(QUrl(QStringLiteral("http://127.0.0.1:%1/api/layer").arg(server.boundPort())));
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
	check(post(R"({"target":"card:one","layout":{"x":0.2,"y":0.3,"scale":1.5,"resultScaleX":1.25,"resultScaleY":0.75}})") ==
		      200,
	      "save card");
	check(post(R"({"target":"heading","layout":{"x":0.5,"y":0.1,"scale":0.8}})") == 200, "save heading");
	const auto saved = state(session).value("pieces");
	const auto savedCard = saved.toObject().value("card:one").toObject();
	check(savedCard.value("resultScaleX").toDouble() == 1.25 && savedCard.value("resultScaleY").toDouble() == 0.75,
	      "save result backdrop scale");
	check(post(R"({"target":"card:missing","layout":{"x":0,"y":0,"scale":1}})") == 404, "unknown ID rejected");
	check(post(R"({"target":"card:one","layout":{"x":"bad","y":0,"scale":1}})") == 400, "invalid number rejected");
	check(post(R"({"target":"card:one","layout":{"x":0,"y":0,"scale":1,"resultScaleX":"bad"}})") == 400,
	      "invalid result scale rejected");
	check(post(R"({"target":"card:one","layout":{"x":0}})") == 400, "missing fields rejected");
	check(state(session).value("pieces") == saved, "invalid requests do not change layout");
	check(postLayer(R"({"target":"card:one","action":"front"})") == 200, "bring card to front");
	auto savedLayers = state(session).value("layers").toObject();
	check(savedLayers.value("card:one").toInt() > savedLayers.value("heading").toInt(), "card is in front");
	check(session.movePieceLayer(QStringLiteral("card:one"), QStringLiteral("backward")),
	      "move card back one layer");
	check(state(session).value("layers").toObject().value("card:one").toInt() <
		      state(session).value("layers").toObject().value("heading").toInt(),
	      "card moves behind heading");
	check(session.movePieceLayer(QStringLiteral("card:one"), QStringLiteral("front")), "move card to front again");
	savedLayers = state(session).value("layers").toObject();
	check(postLayer(R"({"target":"card:one","action":"sideways"})") == 400, "invalid layer action rejected");
	check(postLayer(R"({"target":"card:missing","action":"front"})") == 404, "unknown layer target rejected");
	session.setVote(person.id, FffVote::Red);
	const QByteArray cardTemplate =
		R"({"image":{"x":0,"y":0,"width":416,"height":148},"result":{"x":35,"y":-20,"width":220,"height":100},"order":["result","image"]})";
	check(post(cardTemplate, QStringLiteral("template")) == 200, "template API saves");
	check(post(R"({"image":{}})", QStringLiteral("template")) == 400, "invalid template rejected");
	const auto savedTemplate = state(session).value("cardTemplate");
	session.forceReveal();
	check(session.phase() == FffPhase::Revealed, "reveal works");
	session.clearRound();
	check(session.phase() == FffPhase::Collecting && session.votedCount() == 0, "clear works");
	check(state(session).value("pieces") == saved, "clear preserves layouts");
	FffSession reopened;
	reopened.load();
	check(state(reopened).value("pieces") == saved, "restart preserves layouts");
	check(state(reopened).value("layers") == savedLayers, "restart preserves layers");
	check(state(reopened).value("cardTemplate") == savedTemplate, "clear and restart preserve template");
	check(!QJsonDocument::fromJson(session.phoneStateJson(person.id)).object().contains("pieces"),
	      "phone has no layouts");
	check(post(R"({"target":"card:one","reset":true})") == 200, "reset card");
	check(state(session).value("pieces").toObject().size() == 1, "reset card preserves heading");
	check(post(R"({"target":"card:one","layout":{"x":-1,"y":2,"scale":8,"resultScaleX":-1,"resultScaleY":8}})") ==
		      200,
	      "clamp layout");
	const auto clamped = state(session).value("pieces").toObject().value("card:one").toObject();
	check(clamped.value("x").toDouble() == 0 && clamped.value("y").toDouble() == 1 &&
		      clamped.value("scale").toDouble() == 2 && clamped.value("resultScaleX").toDouble() == 0.5 &&
		      clamped.value("resultScaleY").toDouble() == 2,
	      "layout bounds");

	check(post(R"({"mode":"bottomBar","x":0.3})") == 200 && post(R"({"mode":"bottomBar","y":0.7})") == 200 &&
		      session.layout(QStringLiteral("bottomBar")).x == 0.3,
	      "partial bottom layout preserves coordinates");
	check(session.setVote(person.id, FffVote::Green), "vote before switching");
	const int roundBefore = session.round();
	check(post(R"({"mode":"bottomBar"})", QStringLiteral("display")) == 200, "show bottom bar");
	check(session.displayMode() == QLatin1String("bottomBar") && session.voteOf(person.id) == FffVote::Green &&
		      session.round() == roundBefore,
	      "switch preserves vote and round");
	check(post(R"({"mode":"scoreboard"})", QStringLiteral("display")) == 200, "show scoreboard");
	check(post(R"({"mode":"invalid"})", QStringLiteral("display")) == 400, "invalid display rejected");
	check(post(R"({"mode":"bottomBar","target":"heading","reset":true})") == 404, "bottom bar rejects heading");
	check(post(R"({"mode":"bottomBar","target":"logo","layout":{"x":0.5,"y":0.9,"scale":1,"imageOpacity":0.5}})") ==
		      200,
	      "logo opacity saves");
	check(post(R"({"mode":"bottomBar","target":"card:one","layout":{"x":0.1,"y":0.9,"scale":1,"imageOpacity":0,"resultOpacity":1}})") ==
		      200,
	      "bottom opacity endpoints save");
	check(post(R"({"mode":"bottomBar","target":"logo","layout":{"x":0.5,"y":0.9,"scale":1,"imageOpacity":2}})") ==
		      400,
	      "invalid opacity rejected");
	check(post(R"({"mode":"bottomBar","target":"logo","action":"front"})", QStringLiteral("layer")) == 200,
	      "logo layer saves");
	check(post(R"({"presidentId":"one"})", QStringLiteral("logo")) == 200, "select logo");
	auto bottomTemplate = QJsonDocument::fromJson(cardTemplate).object();
	bottomTemplate.insert("mode", "bottomBar");
	auto imageLayer = bottomTemplate.value("image").toObject();
	imageLayer.insert("opacity", 0.5);
	bottomTemplate.insert("image", imageLayer);
	check(post(QJsonDocument(bottomTemplate).toJson(), QStringLiteral("template")) == 200, "bottom template saves");
	check(state(session).value("cardTemplate") == savedTemplate &&
		      state(session).value("pieces").toObject().value("card:one") == clamped,
	      "bottom edits leave scoreboard intact");
	FffSession bottomReopened;
	bottomReopened.load();
	check(state(bottomReopened) == state(session), "both modes round trip");
	const auto bottomSaved = state(session).value("bottomBar");
	check(session.resetLayouts(), "scoreboard reset succeeds");
	check(state(session).value("bottomBar") == bottomSaved, "scoreboard reset preserves bottom bar");
	QFile png(temp.filePath(QStringLiteral("source.png")));
	check(png.open(QIODevice::WriteOnly), "create PNG source");
	png.write(QByteArray::fromHex("89504e470d0a1a0a"));
	png.close();
	person.bottomBar = session.importAsset(png.fileName(), person.id, QStringLiteral("bottomBar"));
	check(!person.bottomBar.isEmpty() && session.updatePresident(person), "import bottom PNG");
	const auto firstUrl = session.assetUrl(person, QStringLiteral("bottomBar"));
	const auto firstPath = session.assetPath(person, QStringLiteral("bottomBar"));
	person.bottomBar = session.importAsset(png.fileName(), person.id, QStringLiteral("bottomBar"));
	check(!person.bottomBar.isEmpty() && QFile::exists(firstPath), "staged replacement preserves old asset");
	check(session.updatePresident(person) && firstUrl != session.assetUrl(person, QStringLiteral("bottomBar")),
	      "same-second replacement changes URL");
	QFile blocked(temp.filePath(QStringLiteral("blocked")));
	check(blocked.open(QIODevice::WriteOnly), "create blocked config");
	blocked.close();
	configPath = blocked.fileName();
	const auto beforeFailure = state(session);
	auto changedTemplate = QJsonDocument::fromJson(cardTemplate).object();
	auto resultLayer = changedTemplate.value("result").toObject();
	resultLayer.insert("x", 100);
	changedTemplate.insert("result", resultLayer);
	check(post(QJsonDocument(changedTemplate).toJson(), QStringLiteral("template")) == 500,
	      "failed template write reported");
	check(post(R"({"target":"heading","reset":true})") == 500, "failed write reported");
	check(post(R"({"target":"all","reset":true})") == 500, "failed reset reported");
	check(!session.showMode(QStringLiteral("bottomBar")), "failed display reported");
	check(!session.setLogoPresident(QString()), "failed logo selection reported");
	check(!session.setVote(person.id, FffVote::Red), "failed vote reported");
	check(!session.clearRound(), "failed clear reported");
	check(!session.removePresident(person.id), "failed delete reported");
	auto editedPerson = person;
	editedPerson.name = QStringLiteral("changed");
	check(!session.updatePresident(editedPerson), "failed roster edit reported");
	editedPerson.id = QStringLiteral("two");
	check(!session.addPresident(editedPerson), "failed roster addition reported");
	check(post(R"({"mode":"bottomBar","target":"all","reset":true})") == 500, "failed bottom reset reported");
	check(post(R"({"presidentId":"one","color":"red"})", QStringLiteral("operator/vote")) == 500,
	      "failed operator vote reported");
	check(state(session) == beforeFailure, "failed saves roll back");
	configPath = temp.path();
	session.removePresident(person.id);
	check(session.logoPresidentId().isEmpty(), "deleting selected president clears logo");
	check(!state(session).value("pieces").toObject().contains("card:one"), "remove cleans layout");
	check(post(R"({"target":"all","reset":true})") == 200, "reset all");
	check(state(session).value("pieces").toObject().isEmpty() && session.layout().scale == 1 &&
		      session.layout().x == 0.5,
	      "reset all restores default grid");
	check(state(session).value("layers").toObject().isEmpty(), "reset all clears layers");
	QFile oldSession(temp.filePath(QStringLiteral("session.json")));
	check(oldSession.open(QIODevice::WriteOnly | QIODevice::Truncate), "create old session");
	oldSession.write(R"({"version":3,"presidents":[],"phase":"revealed","round":5})");
	oldSession.close();
	FffSession old;
	old.load();
	check(old.displayMode() == QLatin1String("scoreboard") && old.phase() == FffPhase::Revealed &&
		      old.round() == 5 && state(old).value("bottomBar").toObject().value("pieces").toObject().isEmpty(),
	      "old session retains launch behavior with empty bottom bar");
	qInfo("PASS: layout/layer APIs, legacy session, persistence, rounds, validation, resets, failed writes");
}
