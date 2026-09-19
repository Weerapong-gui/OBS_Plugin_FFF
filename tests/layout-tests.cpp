// Exercise the real session and HTTP server with an isolated OBS config path.
#include "fff-session.h"
#include "fff-http-server.h"
#include "obs-stubs.h"

#include <obs-module.h>
#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QTemporaryDir>
#include <QTimer>
#include <cstdlib>
#include <cstring>

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
	fffTestConfigPath = temp.path();
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
	auto getCover = [&]() {
		auto *reply = network.get(
			QNetworkRequest(QUrl(QStringLiteral("http://127.0.0.1:%1/api/cover").arg(server.boundPort()))));
		QEventLoop loop;
		QTimer timer;
		timer.setSingleShot(true);
		QObject::connect(&timer, &QTimer::timeout, reply, &QNetworkReply::abort);
		QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
		timer.start(3000);
		loop.exec();
		const auto result =
			qMakePair(reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(), reply->readAll());
		reply->deleteLater();
		return result;
	};
	auto coverCacheControl = [&]() {
		auto *reply = network.get(
			QNetworkRequest(QUrl(QStringLiteral("http://127.0.0.1:%1/api/cover").arg(server.boundPort()))));
		QEventLoop loop;
		QTimer timer;
		timer.setSingleShot(true);
		QObject::connect(&timer, &QTimer::timeout, reply, &QNetworkReply::abort);
		QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
		timer.start(3000);
		loop.exec();
		const QByteArray header = reply->rawHeader("Cache-Control");
		reply->deleteLater();
		return header;
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

	// The Show Status title: the one piece on that board made of words.
	check(!state(session).contains("headingTemplate"), "no title template until one is set");
	const QByteArray headingTemplate =
		R"({"piece":"heading","box":{"x":0,"y":0,"width":420,"height":100},"text":"COMPETITION\nSTATUS",)"
		R"("fontFamily":"Bai Jamjuree","fontSize":38,"fontWeight":800,"lineHeight":42,)"
		R"("align":"left","color":"#000000"})";
	check(post(headingTemplate, QStringLiteral("template")) == 200, "title template saves");
	const auto savedHeading = state(session).value("headingTemplate");
	check(savedHeading.toObject().value("text").toString() == QLatin1String("COMPETITION\nSTATUS") &&
		      !savedHeading.toObject().contains("piece") && !savedHeading.toObject().contains("mode"),
	      "the stored title keeps no routing fields");
	check(post(R"({"piece":"heading","mode":"bottomBar","box":{"x":0,"y":0,"width":420,"height":100},)"
		   R"("text":"x","fontFamily":"","fontSize":38,"fontWeight":800,"lineHeight":42,)"
		   R"("align":"left","color":"#000000"})",
		   QStringLiteral("template")) == 400,
	      "the title belongs to Show Status, not the bottom bar");
	// One refusal per field that could reach the stream as something else.
	const auto badHeading = [&](const char *body) {
		return post(
			QByteArray("{\"piece\":\"heading\",\"box\":{\"x\":0,\"y\":0,\"width\":420,\"height\":100},") +
				body,
			QStringLiteral("template"));
	};
	check(badHeading(
		      R"("text":"a\tb","fontFamily":"","fontSize":38,"fontWeight":800,"lineHeight":42,"align":"left","color":"#000000"})") ==
		      400,
	      "a control character in the title is refused");
	check(badHeading(
		      R"("text":"a","fontFamily":"Bad\"Family","fontSize":38,"fontWeight":800,"lineHeight":42,"align":"left","color":"#000000"})") ==
		      400,
	      "a family name that could escape the declaration is refused");
	check(badHeading(
		      R"("text":"a","fontFamily":"","fontSize":4,"fontWeight":800,"lineHeight":42,"align":"left","color":"#000000"})") ==
		      400,
	      "an unreadable size is refused");
	check(badHeading(
		      R"("text":"a","fontFamily":"","fontSize":38,"fontWeight":950,"lineHeight":42,"align":"left","color":"#000000"})") ==
		      400,
	      "a weight no face can have is refused");
	check(badHeading(
		      R"("text":"a","fontFamily":"","fontSize":38,"fontWeight":800,"lineHeight":42,"align":"middle","color":"#000000"})") ==
		      400,
	      "an alignment that is not one of the three is refused");
	check(badHeading(
		      R"("text":"a","fontFamily":"","fontSize":38,"fontWeight":800,"lineHeight":42,"align":"left","color":"black"})") ==
		      400,
	      "a colour that is not #rrggbb is refused");
	check(state(session).value("headingTemplate") == savedHeading, "refused titles change nothing");

	// The /score page. It is a page, not a board: its two pieces reuse the
	// title and counter validators, and "score" must never become something a
	// board API would accept.
	check(!state(session).contains("scoreHeadingTemplate") && !state(session).contains("scoreCountTemplate"),
	      "no score templates until they are set");
	const QByteArray scoreHeading =
		R"({"mode":"score","piece":"heading","box":{"x":660,"y":300,"width":600,"height":120},)"
		R"("text":"พี่เนย","fontFamily":"Bai Jamjuree","fontSize":84,"fontWeight":800,)"
		R"("lineHeight":100,"align":"center","color":"#ffffff"})";
	const QByteArray scoreCount =
		R"({"mode":"score","piece":"count","value":{"x":0,"y":0,"width":240,"height":240},)"
		R"("fontFamily":"","fontSize":160,"fontWeight":800,)"
		R"("colors":{"red":"#e23c3c","green":"#21b04a"}})";
	check(post(scoreHeading, QStringLiteral("template")) == 200, "score title saves");
	check(post(scoreCount, QStringLiteral("template")) == 200, "score counters save");
	const auto savedScoreHeading = state(session).value("scoreHeadingTemplate");
	const auto savedScoreCount = state(session).value("scoreCountTemplate");
	check(savedScoreHeading.toObject().value("text").toString() == QString::fromUtf8("พี่เนย") &&
		      !savedScoreHeading.toObject().contains("piece") && !savedScoreHeading.toObject().contains("mode"),
	      "the stored score title keeps no routing fields");
	check(savedScoreCount.toObject().value("colors").toObject().value("red").toString() ==
			      QLatin1String("#e23c3c") &&
		      !savedScoreCount.toObject().contains("piece") && !savedScoreCount.toObject().contains("mode"),
	      "the stored score counters keep no routing fields");
	check(state(session).value("headingTemplate") == savedHeading, "the score title is not the Show Status one");
	check(post(R"({"mode":"score","piece":"card","image":{"x":0,"y":0,"width":10,"height":10},)"
		   R"("result":{"x":0,"y":0,"width":10,"height":10}})",
		   QStringLiteral("template")) == 400,
	      "the score page has no cards to describe");
	check(post(R"({"mode":"score","piece":"logo","image":{"x":0,"y":0,"width":10,"height":10}})",
		   QStringLiteral("template")) == 400,
	      "the score page has no centre logo");
	check(post(R"({"mode":"score","piece":"heading","box":{"x":0,"y":0,"width":420,"height":100},)"
		   R"("text":"a","fontFamily":"","fontSize":38,"fontWeight":800,"lineHeight":42,)"
		   R"("align":"middle","color":"#000000"})",
		   QStringLiteral("template")) == 400,
	      "the score title is validated like the Show Status one");
	check(post(R"({"mode":"score","piece":"count","value":{"x":0,"y":0,"width":240,"height":240},)"
		   R"("fontFamily":"","fontSize":160,"fontWeight":800,"colors":{"red":"red","green":"#21b04a"}})",
		   QStringLiteral("template")) == 400,
	      "a score counter colour that is not #rrggbb is refused");
	check(state(session).value("scoreHeadingTemplate") == savedScoreHeading &&
		      state(session).value("scoreCountTemplate") == savedScoreCount,
	      "refused score templates change nothing");
	// /score never goes on air, so the display and layout APIs must not know it.
	check(post(R"({"mode":"score"})", QStringLiteral("display")) == 400, "score is not a display mode");
	check(post(R"({"mode":"score","target":"heading","reset":true})") == 400, "score has no layout targets");
	check(post(R"({"mode":"score","target":"heading","action":"front"})", QStringLiteral("layer")) == 400,
	      "score has no layer order");
	// Taking the board down and choosing the logo's round are the two things
	// the dock could do that no request could express. Each travels as its own
	// field so the shapes that already work are untouched.
	check(post(R"({"mode":"bottomBar"})", QStringLiteral("display")) == 200 &&
		      session.phase() == FffPhase::Revealed && session.displayMode() == QLatin1String("bottomBar"),
	      "a mode request still puts a board on air");
	check(post(R"({"hide":true})", QStringLiteral("display")) == 200 && session.phase() == FffPhase::Collecting,
	      "hide takes the board down");
	check(session.displayMode() == QLatin1String("bottomBar"),
	      "hiding remembers the mode, the way the dock's button does");
	check(post(R"({"hide":true,"mode":"bottomBar"})", QStringLiteral("display")) == 400 &&
		      session.phase() == FffPhase::Collecting,
	      "one request cannot both show and hide");
	check(post(R"({"hide":false})", QStringLiteral("display")) == 400, "only true hides");
	check(post(R"({"mode":"nonsense"})", QStringLiteral("display")) == 400, "an unknown mode is still refused");

	check(session.logoRound() == 1, "the logo starts on round 1");
	check(post(R"({"round":2})", QStringLiteral("logo")) == 200 && session.logoRound() == 2,
	      "the logo round can be chosen over HTTP");
	check(post(R"({"round":3})", QStringLiteral("logo")) == 400 && session.logoRound() == 2,
	      "a round that has no artwork set is refused");
	check(post(R"({"round":2,"presidentId":"one"})", QStringLiteral("logo")) == 400,
	      "one request cannot both pick a school and a round");
	check(post(R"({"presidentId":"one"})", QStringLiteral("logo")) == 200 &&
		      session.logoPresidentId() == QLatin1String("one"),
	      "choosing a school still works");
	check(post(R"({"round":1})", QStringLiteral("logo")) == 200 && session.logoRound() == 1, "back to round 1");

	// Ending a round is the dock's most destructive button. The confirmation
	// belongs to whichever UI asks; the route only refuses a request that did
	// not say plainly what it wanted.
	{
		check(session.setVote(QStringLiteral("one"), FffVote::Green), "a vote to clear");
		const int roundBefore = session.round();
		check(post(R"({})", QStringLiteral("round")) == 400 && session.round() == roundBefore &&
			      session.votedCount() == 1,
		      "an empty request never ends a round");
		check(post(R"({"action":"reset"})", QStringLiteral("round")) == 400 && session.round() == roundBefore,
		      "only the named action clears");
		check(post(R"({"action":"clear"})", QStringLiteral("round")) == 200 &&
			      session.round() == roundBefore + 1 && session.votedCount() == 0,
		      "the round is cleared and the next one begins");
	}
	{
		FffSession reopenedScore;
		reopenedScore.load();
		check(state(reopenedScore).value("scoreHeadingTemplate") == savedScoreHeading &&
			      state(reopenedScore).value("scoreCountTemplate") == savedScoreCount,
		      "score templates survive a restart");
	}

	// Animation lengths. Every key is named, so a value can never be filed
	// under a motion it does not belong to, and a session that carries none
	// simply leaves every animation at the length the web pages default to.
	check(!state(session).contains("timing"), "no timing until one is set");
	const QByteArray timing =
		R"({"scoreboard":{"card":900,"stagger":40},"board":{"countRoll":800,"countTick":40},"monitor":{"confirm":5000}})";
	check(post(timing, QStringLiteral("timing")) == 200, "timing API saves");
	const auto savedTiming = state(session).value("timing");
	check(savedTiming.toObject().value("scoreboard").toObject().value("card").toDouble() == 900 &&
		      savedTiming.toObject().value("board").toObject().value("countTick").toDouble() == 40,
	      "timing reaches overlay state");
	check(post(R"({"scoreboard":{"card":-1}})", QStringLiteral("timing")) == 400, "negative duration rejected");
	check(post(R"({"scoreboard":{"card":5001}})", QStringLiteral("timing")) == 400, "over-long duration rejected");
	check(post(R"({"board":{"countTick":5}})", QStringLiteral("timing")) == 400, "too fast a tick rejected");
	check(post(R"({"monitor":{"confirm":100}})", QStringLiteral("timing")) == 400, "too short a confirm rejected");
	check(post(R"({"scoreboard":{"card":"slow"}})", QStringLiteral("timing")) == 400,
	      "nonnumeric duration rejected");
	check(post(R"({"scoreboard":{"nosuchthing":100}})", QStringLiteral("timing")) == 400, "unknown key rejected");
	check(post(R"({"nosuchgroup":{"card":100}})", QStringLiteral("timing")) == 400, "unknown group rejected");
	check(post(R"({"scoreboard":600})", QStringLiteral("timing")) == 400, "group that is not an object rejected");
	check(state(session).value("timing") == savedTiming, "refused timing changes nothing");

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
	check(state(reopened).value("timing") == savedTiming, "clear and restart preserve timing");
	check(state(reopened).value("headingTemplate") == savedHeading, "clear and restart preserve the title");
	check(QJsonDocument::fromJson(session.phoneStateJson(person.id)).object().value("timing") == savedTiming,
	      "the phone's flag buttons get the lengths too");
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
	check(post(R"({"mode":"bottomBar"})", QStringLiteral("display")) == 200, "show bottom bar again");
	// Turning a display mode off is a toggle, not the end of the round.
	check(session.hideDisplay(), "hide the board");
	check(state(session).value("phase").toString() == QLatin1String("collecting"), "hiding blanks the stream");
	check(session.voteOf(person.id) == FffVote::Green && session.round() == roundBefore &&
		      session.displayMode() == QLatin1String("bottomBar"),
	      "hiding keeps votes, round and the remembered mode");
	check(session.hideDisplay() && session.round() == roundBefore, "hiding again is a no-op");
	check(post(R"({"mode":"bottomBar"})", QStringLiteral("display")) == 200 &&
		      state(session).value("phase").toString() == QLatin1String("revealed") &&
		      session.round() == roundBefore,
	      "the same round comes straight back");
	check(post(R"({"mode":"scoreboard"})", QStringLiteral("display")) == 200, "show scoreboard");
	check(post(R"({"mode":"invalid"})", QStringLiteral("display")) == 400, "invalid display rejected");
	check(post(R"({"mode":"bottomBar","target":"heading","reset":true})") == 404, "bottom bar rejects heading");
	check(post(R"({"target":"cover","reset":true})") == 404, "scoreboard rejects cover");
	check(post(R"({"mode":"bottomBar","target":"cover","layout":{"x":0.5,"y":0.5,"scale":1,"imageOpacity":0.5}})") ==
		      200,
	      "cover layout saves");
	check(post(R"({"mode":"bottomBar","target":"cover","action":"front"})", QStringLiteral("layer")) == 200,
	      "cover layer saves");
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
	// The two flag counters are Bottom Bar pieces like the logo and the cover.
	check(post(R"({"mode":"bottomBar","target":"count:red","layout":{"x":0.42,"y":0.62,"scale":1,"imageOpacity":0.75}})") ==
		      200,
	      "red count layout saves");
	check(post(R"({"mode":"bottomBar","target":"count:green","layout":{"x":0.58,"y":0.62,"scale":1}})") == 200,
	      "green count layout saves");
	check(post(R"({"target":"count:red","reset":true})") == 404, "scoreboard rejects counts");
	check(post(R"({"mode":"bottomBar","target":"count:blue","layout":{"x":0.5,"y":0.5,"scale":1}})") == 404,
	      "unknown count colour rejected");
	check(post(R"({"mode":"bottomBar","target":"count:green","action":"front"})", QStringLiteral("layer")) == 200,
	      "count layer saves");
	check(post(R"({"target":"count:green","action":"front"})", QStringLiteral("layer")) == 404,
	      "scoreboard rejects count layers");
	{
		// Natural order runs logo, cards, the counters, then the cover, so a
		// counter starts above every card and below the cover.
		check(session.movePieceLayer(QStringLiteral("count:red"), QStringLiteral("backward"),
					     QStringLiteral("bottomBar")),
		      "counter moves down a layer");
		const auto layers = state(session)
					    .value(QStringLiteral("bottomBar"))
					    .toObject()
					    .value(QStringLiteral("layers"))
					    .toObject();
		check(layers.value(QStringLiteral("count:red")).toInt() <
			      layers.value(QStringLiteral("count:green")).toInt(),
		      "counters keep a stable relative order");
	}

	const QByteArray logoTemplate = R"({"mode":"bottomBar","piece":"logo",
		"image":{"x":10,"y":20,"width":300,"height":180,"opacity":0.8}})";
	const QByteArray countTemplate = R"({"mode":"bottomBar","piece":"count",
		"value":{"x":-5,"y":4,"width":160,"height":140,"opacity":0.9},
		"fontFamily":"Bai Jamjuree","fontSize":120,"fontWeight":900,
		"colors":{"red":"#ff0055","green":"#00cc66"}})";
	const QByteArray countBody = R"("value":{"x":0,"y":0,"width":10,"height":10},)"
				     R"("colors":{"red":"#e23c3c","green":"#21b04a"},"fontWeight":700,)";
	check(post(logoTemplate, QStringLiteral("template")) == 200, "logo template saves");
	check(post(countTemplate, QStringLiteral("template")) == 200, "count template saves");
	check(post(R"({"piece":"logo","image":{"x":0,"y":0,"width":10,"height":10}})", QStringLiteral("template")) ==
		      400,
	      "logo template needs bottom bar");
	check(post(R"({"mode":"bottomBar","piece":"logo","image":{"x":0,"y":0,"width":0,"height":10}})",
		   QStringLiteral("template")) == 400,
	      "logo template rejects an empty box");
	const auto countPost = [&](const QByteArray &tail) {
		return post(R"({"mode":"bottomBar","piece":"count",)" + countBody + tail + "}",
			    QStringLiteral("template"));
	};
	check(countPost(R"("fontFamily":"","fontSize":96)") == 200, "an empty family falls back to the page stack");
	check(countPost(R"("fontFamily":"ไทยสบาย","fontSize":96)") == 200, "a Thai family name is accepted");
	// Anything that could close out of a CSS font-family value is refused.
	check(countPost(R"J("fontFamily":"Bad\";color:red","fontSize":96)J") == 400, "a quoted family is rejected");
	check(countPost(R"J("fontFamily":"Bad;color:red","fontSize":96)J") == 400,
	      "a semicolon in a family is rejected");
	check(countPost(R"J("fontFamily":"url(x)","fontSize":96)J") == 400, "brackets in a family are rejected");
	check(countPost(QByteArray(R"("fontFamily":")") + QByteArray(121, 'a') + R"(","fontSize":96)") == 400,
	      "an overlong family is rejected");
	check(countPost(R"("fontFamily":"Menlo","fontSize":12)") == 400, "font size below the floor rejected");
	// The weight used to be pinned in the stylesheet, so the operator's choice
	// never reached the screen. It is part of the template now.
	check(post(R"({"mode":"bottomBar","piece":"count","value":{"x":0,"y":0,"width":10,"height":10},)"
		   R"("colors":{"red":"#e23c3c","green":"#21b04a"},"fontFamily":"","fontSize":96,"fontWeight":300})",
		   QStringLiteral("template")) == 200,
	      "a light weight saves");
	for (const char *weight : {"50", "1000", "\"700\""}) {
		check(post(QByteArray(
				   R"({"mode":"bottomBar","piece":"count","value":{"x":0,"y":0,"width":10,"height":10},)"
				   R"("colors":{"red":"#e23c3c","green":"#21b04a"},"fontFamily":"","fontSize":96,"fontWeight":)") +
				   weight + "})",
			   QStringLiteral("template")) == 400,
		      "a weight outside 100-900 is rejected");
	}
	check(post(R"({"mode":"bottomBar","piece":"count","value":{"x":0,"y":0,"width":10,"height":10},)"
		   R"("colors":{"red":"tomato","green":"#21b04a"},"fontFamily":"","fontSize":96})",
		   QStringLiteral("template")) == 400,
	      "a non-hex colour is rejected");
	check(post(R"({"mode":"bottomBar","piece":"count","value":{"x":0,"y":0,"width":10,"height":10},)"
		   R"("colors":{"red":"#e23c3c"},"fontFamily":"","fontSize":96})",
		   QStringLiteral("template")) == 400,
	      "both counter colours are required");
	check(post(countTemplate, QStringLiteral("template")) == 200, "count template saves again");
	check(post(R"({"mode":"bottomBar","piece":"heading","image":{"x":0,"y":0,"width":10,"height":10}})",
		   QStringLiteral("template")) == 400,
	      "unknown template piece rejected");
	{
		const auto bottom = state(session).value(QStringLiteral("bottomBar")).toObject();
		check(bottom.value(QStringLiteral("logoTemplate"))
				      .toObject()
				      .value(QStringLiteral("image"))
				      .toObject()
				      .value(QStringLiteral("width"))
				      .toDouble() == 300,
		      "logo template reaches the overlay");
		const auto count = bottom.value(QStringLiteral("countTemplate")).toObject();
		check(count.value(QStringLiteral("fontFamily")).toString() == QLatin1String("Bai Jamjuree") &&
			      count.value(QStringLiteral("fontSize")).toDouble() == 120 &&
			      count.value(QStringLiteral("fontWeight")).toDouble() == 900,
		      "count font and weight reach the overlay");
		const auto colors = count.value(QStringLiteral("colors")).toObject();
		check(colors.value(QStringLiteral("red")).toString() == QLatin1String("#ff0055") &&
			      colors.value(QStringLiteral("green")).toString() == QLatin1String("#00cc66"),
		      "each counter keeps its own colour");
	}
	{
		// Counters shipped naming one of five font keys and took their colour
		// from the stylesheet. Such a session has to keep the box it already
		// has instead of losing the whole template.
		const QString path = QDir(fffTestConfigPath).filePath(QStringLiteral("session.json"));
		QFile file(path);
		check(file.open(QIODevice::ReadOnly), "read the saved session");
		auto root = QJsonDocument::fromJson(file.readAll()).object();
		file.close();
		auto bottom = root.value(QStringLiteral("bottomBar")).toObject();
		auto legacyCount = bottom.value(QStringLiteral("countTemplate")).toObject();
		legacyCount.remove(QStringLiteral("fontFamily"));
		legacyCount.remove(QStringLiteral("colors"));
		legacyCount.remove(QStringLiteral("fontWeight"));
		legacyCount.insert(QStringLiteral("font"), QStringLiteral("mono"));
		bottom.insert(QStringLiteral("countTemplate"), legacyCount);
		root.insert(QStringLiteral("bottomBar"), bottom);
		check(file.open(QIODevice::WriteOnly), "rewrite the session as an older one");
		file.write(QJsonDocument(root).toJson());
		file.close();

		FffSession migrated;
		migrated.load();
		const auto count = state(migrated)
					   .value(QStringLiteral("bottomBar"))
					   .toObject()
					   .value(QStringLiteral("countTemplate"))
					   .toObject();
		check(count.value(QStringLiteral("value")).toObject().value(QStringLiteral("width")).toDouble() == 160,
		      "an older counter template keeps its box");
		check(!count.contains(QStringLiteral("font")) &&
			      count.value(QStringLiteral("fontFamily")).toString().isEmpty(),
		      "an older font key becomes the default stack");
		const auto colors = count.value(QStringLiteral("colors")).toObject();
		check(colors.value(QStringLiteral("red")).toString() == QLatin1String("#e23c3c") &&
			      colors.value(QStringLiteral("green")).toString() == QLatin1String("#21b04a"),
		      "an older counter template gains the stylesheet colours");
		check(count.value(QStringLiteral("fontWeight")).toDouble() == 700,
		      "an older counter template keeps the weight the stylesheet used to force");
		session.load();
		check(!bottom.value(QStringLiteral("logoTemplate")).toObject().contains(QStringLiteral("piece")) &&
			      !count.contains(QStringLiteral("mode")),
		      "routing fields are not stored");
	}

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

	// The scoreboard is a status board: one finished PNG per status, chosen per
	// president, uploaded from the monitor rather than picked from a file dialog.
	{
		const QByteArray pngBytes = QByteArray::fromHex("89504e470d0a1a0a") + "status";
		const auto postAsset = [&](const QString &id, const QString &kind, const QByteArray &bytes) {
			QNetworkRequest request(
				QUrl(QStringLiteral("http://127.0.0.1:%1/api/asset?presidentId=%2&kind=%3")
					     .arg(server.boundPort())
					     .arg(id, kind)));
			request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("image/png"));
			auto *reply = network.post(request, bytes);
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
		const auto entry = [&]() {
			const auto people = state(session).value(QStringLiteral("presidents")).toArray();
			return people.isEmpty() ? QJsonObject() : people.at(0).toObject();
		};
		check(entry().value(QStringLiteral("status")).toString() == QLatin1String("waiting"),
		      "a president starts out waiting");
		// With no artwork for that status yet, the card behind it still goes up.
		check(entry().value(QStringLiteral("statusUrl")).toString() ==
			      entry().value(QStringLiteral("cardUrl")).toString(),
		      "a status with no artwork falls back to the card");
		check(postAsset(person.id, QStringLiteral("qualified"), pngBytes) == 200, "upload qualified PNG");
		check(postAsset(person.id, QStringLiteral("nonsense"), pngBytes) == 400,
		      "unknown status kind rejected");
		check(postAsset(QStringLiteral("gone"), QStringLiteral("qualified"), pngBytes) == 404,
		      "upload for a missing president rejected");
		check(postAsset(person.id, QStringLiteral("waiting"), QByteArray("not a png at all")) == 400,
		      "only PNG bytes are stored");
		check(post(R"({"presidentId":"one","status":"qualified"})", QStringLiteral("status")) == 200,
		      "set a status");
		check(post(R"({"presidentId":"one","status":"sideways"})", QStringLiteral("status")) == 400,
		      "invalid status rejected");
		check(post(R"({"presidentId":"gone","status":"qualified"})", QStringLiteral("status")) == 404,
		      "status for a missing president rejected");
		const QString qualifiedUrl = entry().value(QStringLiteral("statusUrl")).toString();
		check(qualifiedUrl.contains(QLatin1String("/api/qualified/")) &&
			      entry().value(QStringLiteral("status")).toString() == QLatin1String("qualified"),
		      "the status chooses the artwork");
		FffSession restarted;
		restarted.load();
		const auto reopened = QJsonDocument::fromJson(restarted.overlayStateJson())
					      .object()
					      .value(QStringLiteral("presidents"))
					      .toArray()
					      .at(0)
					      .toObject();
		check(reopened.value(QStringLiteral("status")).toString() == QLatin1String("qualified") &&
			      reopened.value(QStringLiteral("statusUrl")).toString() == qualifiedUrl,
		      "status and artwork survive a restart");
		// Clearing one status leaves the others and drops back to the card.
		check(postAsset(person.id, QStringLiteral("qualified"), QByteArray()) == 200, "clear qualified PNG");
		check(entry().value(QStringLiteral("statusUrl")).toString() ==
			      entry().value(QStringLiteral("cardUrl")).toString(),
		      "clearing a status falls back to the card again");
		check(post(R"({"presidentId":"one","status":"waiting"})", QStringLiteral("status")) == 200,
		      "back to waiting");
		person = *session.presidentById(person.id);
	}
	const auto firstUrl = session.assetUrl(person, QStringLiteral("bottomBar"));
	const auto firstPath = session.assetPath(person, QStringLiteral("bottomBar"));
	person.bottomBar = session.importAsset(png.fileName(), person.id, QStringLiteral("bottomBar"));
	check(!person.bottomBar.isEmpty() && QFile::exists(firstPath), "staged replacement preserves old asset");
	check(session.updatePresident(person) && firstUrl != session.assetUrl(person, QStringLiteral("bottomBar")),
	      "same-second replacement changes URL");
	// Artwork can be taken off a president again, not only replaced. The dock
	// buttons clear the reference; the PNG stays on disk so a mistaken press is
	// undone by picking the same file back.
	{
		FffPresident withArt = *session.presidentById(person.id);
		withArt.card = session.importAsset(png.fileName(), person.id, QStringLiteral("card"));
		withArt.logo = session.importAsset(png.fileName(), person.id, QStringLiteral("logo"));
		check(!withArt.card.isEmpty() && !withArt.logo.isEmpty() && session.updatePresident(withArt),
		      "president carries card, bottom bar and logo art");
		const QString cardFile = session.assetPath(withArt, QStringLiteral("card"));
		const auto urlFor = [&](const QString &kind) {
			return session.assetUrl(*session.presidentById(person.id), kind);
		};
		check(!urlFor(QStringLiteral("card")).isEmpty() && !urlFor(QStringLiteral("bottomBar")).isEmpty() &&
			      !urlFor(QStringLiteral("logo")).isEmpty(),
		      "every kind resolves before anything is cleared");

		FffPresident cleared = *session.presidentById(person.id);
		cleared.logo.clear();
		check(session.updatePresident(cleared), "clear the logo");
		check(urlFor(QStringLiteral("logo")).isEmpty(), "a cleared kind stops resolving");
		check(!urlFor(QStringLiteral("card")).isEmpty() && !urlFor(QStringLiteral("bottomBar")).isEmpty(),
		      "clearing one kind leaves the others");

		cleared = *session.presidentById(person.id);
		cleared.card.clear();
		check(session.updatePresident(cleared), "clear the card");
		check(session.statusUrl(*session.presidentById(person.id)).isEmpty(),
		      "with no status art and no card there is nothing to show");
		check(QFile::exists(cardFile), "clearing a reference keeps the PNG on disk");

		FffSession afterClear;
		afterClear.load();
		const FffPresident *reloaded = afterClear.presidentById(person.id);
		check(reloaded && reloaded->card.isEmpty() && reloaded->logo.isEmpty() &&
			      !reloaded->bottomBar.isEmpty(),
		      "cleared artwork stays cleared after a restart");
		person = *session.presidentById(person.id);
	}

	const auto scoreboardBeforeCover = state(session).value("pieces");
	const QString firstCover = session.importAsset(png.fileName(), QString(), QStringLiteral("cover"));
	check(!firstCover.isEmpty() && session.setCover(firstCover), "import and select cover");
	check(getCover() == qMakePair(200, QByteArray::fromHex("89504e470d0a1a0a")),
	      "cover HTTP endpoint serves imported bytes");
	const QString firstCoverPath = session.coverPath();
	const QString firstCoverUrl = session.coverUrl();
	// Imports get a fresh UUID file name that the URL carries as ?v=, so the
	// overlay may keep artwork forever and never refetch it on a reveal.
	check(coverCacheControl() == "public, max-age=31536000, immutable", "cover bytes are cacheable for good");
	const QString movedCover = firstCoverPath + QStringLiteral(".away");
	check(QFile::rename(firstCoverPath, movedCover), "cover file can be moved aside");
	check(getCover() == qMakePair(200, QByteArray::fromHex("89504e470d0a1a0a")),
	      "cover repeats from memory instead of the disk");
	check(session.setVote(person.id, FffVote::Green) && getCover().first == 200,
	      "a vote does not evict artwork that is still in play");
	check(QFile::rename(movedCover, firstCoverPath), "cover file restored");
	const QString replacementCover = session.importAsset(png.fileName(), QString(), QStringLiteral("cover"));
	check(!replacementCover.isEmpty() && replacementCover != firstCover && QFile::exists(firstCoverPath),
	      "staging cover preserves previous file and creates a unique filename");
	check(session.setCover(replacementCover) && session.coverUrl() != firstCoverUrl,
	      "cover replacement changes cache URL");
	check(!session.setCover(QStringLiteral("../source.png")) && !session.setCover(QStringLiteral("missing.png")) &&
		      session.cover() == replacementCover,
	      "invalid cover references preserve selection");
	FffSession coverReopened;
	coverReopened.load();
	check(coverReopened.cover() == replacementCover && coverReopened.coverUrl() == session.coverUrl(),
	      "cover selection survives restart");
	for (const double opacity : {0.0, 0.5, 1.0}) {
		QJsonObject layout{{"x", 0.25}, {"y", 0.75}, {"scale", 1.5}, {"imageOpacity", opacity}};
		check(post(QJsonDocument(QJsonObject{{"mode", "bottomBar"}, {"target", "cover"}, {"layout", layout}})
				   .toJson()) == 200,
		      "cover opacity endpoint saves");
		check(state(session).value("bottomBar")
				      .toObject()
				      .value("pieces")
				      .toObject()
				      .value("cover")
				      .toObject()
				      .value("imageOpacity")
				      .toDouble(-1) == opacity,
		      "cover opacity is retained exactly");
	}
	check(post(R"({"mode":"bottomBar","target":"cover","action":"back"})", QStringLiteral("layer")) == 200,
	      "cover can move behind all bottom pieces");
	const auto coverLayers = state(session).value("bottomBar").toObject().value("layers").toObject();
	check(coverLayers.value("cover").toInt() < coverLayers.value("logo").toInt() &&
		      coverLayers.value("cover").toInt() < coverLayers.value("card:one").toInt(),
	      "cover layer participates in bottom ordering");
	const auto templateBeforeCoverReset = state(session).value("bottomBar").toObject().value("cardTemplate");
	check(post(R"({"mode":"bottomBar","target":"cover","reset":true})") == 200, "reset cover layout");
	check(!state(session).value("bottomBar").toObject().value("pieces").toObject().contains("cover") &&
		      session.cover() == replacementCover &&
		      state(session).value("bottomBar").toObject().value("cardTemplate") == templateBeforeCoverReset &&
		      state(session).value("pieces") == scoreboardBeforeCover,
	      "cover reset preserves asset, template and scoreboard");
	// Keep the asset directory accessible while forcing the session commit to fail.
	const QString sessionFile = temp.filePath(QStringLiteral("session.json"));
	const QString savedSessionFile = temp.filePath(QStringLiteral("session.saved"));
	check(QFile::rename(sessionFile, savedSessionFile) && QDir().mkdir(sessionFile), "block session save only");
	int coverFailures = 0;
	int coverChanges = 0;
	const auto failedConnection = QObject::connect(&session, &FffSession::saveFailed, [&]() { ++coverFailures; });
	const auto changedConnection = QObject::connect(&session, &FffSession::changed, [&]() { ++coverChanges; });
	check(!session.setCover(firstCover) && !session.setCover(QString()), "cover replace and removal fail on save");
	check(session.cover() == replacementCover && QFile::exists(firstCoverPath) &&
		      QFile::exists(session.coverPath()) && coverFailures == 2 && coverChanges == 0,
	      "failed cover writes roll back selection, retain files and emit only failure");
	QObject::disconnect(failedConnection);
	QObject::disconnect(changedConnection);
	check(QDir().rmdir(sessionFile) && QFile::rename(savedSessionFile, sessionFile), "restore session storage");
	check(session.setCover(QString()) && session.coverPath().isEmpty() && session.coverUrl().isEmpty(),
	      "remove cover clears active asset");
	check(getCover().first == 404, "removed cover HTTP endpoint returns missing");
	FffSession removedCover;
	removedCover.load();
	check(removedCover.cover().isEmpty(), "cover removal survives restart");
	check(session.setCover(replacementCover), "restore cover for remaining rollback checks");
	QFile blocked(temp.filePath(QStringLiteral("blocked")));
	check(blocked.open(QIODevice::WriteOnly), "create blocked config");
	blocked.close();
	fffTestConfigPath = blocked.fileName();
	const auto beforeFailure = state(session);
	auto changedTemplate = QJsonDocument::fromJson(cardTemplate).object();
	auto resultLayer = changedTemplate.value("result").toObject();
	resultLayer.insert("x", 100);
	changedTemplate.insert("result", resultLayer);
	check(post(QJsonDocument(changedTemplate).toJson(), QStringLiteral("template")) == 500,
	      "failed template write reported");
	check(post(R"({"scoreboard":{"card":1234}})", QStringLiteral("timing")) == 500, "failed timing write reported");
	check(post(R"({"piece":"heading","box":{"x":0,"y":0,"width":420,"height":100},"text":"changed",)"
		   R"("fontFamily":"","fontSize":38,"fontWeight":800,"lineHeight":42,"align":"left","color":"#000000"})",
		   QStringLiteral("template")) == 500,
	      "failed title write reported");
	check(post(R"({"mode":"score","piece":"heading","box":{"x":0,"y":0,"width":420,"height":100},"text":"changed",)"
		   R"("fontFamily":"","fontSize":38,"fontWeight":800,"lineHeight":42,"align":"left","color":"#000000"})",
		   QStringLiteral("template")) == 500,
	      "failed score title write reported");
	check(post(R"({"mode":"score","piece":"count","value":{"x":0,"y":0,"width":240,"height":240},)"
		   R"("fontFamily":"","fontSize":160,"fontWeight":800,"colors":{"red":"#000000","green":"#ffffff"}})",
		   QStringLiteral("template")) == 500,
	      "failed score counter write reported");
	check(post(R"({"target":"heading","reset":true})") == 500, "failed write reported");
	check(post(R"({"target":"all","reset":true})") == 500, "failed reset reported");
	check(!session.showMode(QStringLiteral("bottomBar")), "failed display reported");
	check(!session.setLogoPresident(QString()), "failed logo selection reported");
	check(!session.setVote(person.id, FffVote::Red), "failed vote reported");
	check(!session.clearRound(), "failed clear reported");
	check(!session.hideDisplay(), "failed hide reported");
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
	fffTestConfigPath = temp.path();
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
		      old.round() == 5 && old.cover().isEmpty() && old.coverUrl().isEmpty() &&
		      state(old).value("bottomBar").toObject().value("pieces").toObject().isEmpty(),
	      "old session retains launch behavior with empty bottom bar");
	check(!state(old).contains("timing"), "a session without timing keeps the default lengths");
	qInfo("PASS: layout/layer APIs, legacy session, persistence, rounds, validation, resets, failed writes");

	// A machine that has just installed the plugin gets the board the plugin
	// ships with. Everything about that has to be true exactly once: it is
	// never allowed to touch a session an operator already has.
	QTemporaryDir bundleDir;
	check(bundleDir.isValid(), "temporary module data");
	const QString cardsSource = QDir(bundleDir.path()).filePath(QStringLiteral("default-session/cards"));
	check(QDir().mkpath(cardsSource), "bundled cards directory");
	const QByteArray seedPng =
		QByteArray::fromHex("89504e470d0a1a0a0000000d49484452000000010000000108060000001f15c4"
				    "890000000d4944415478da63f8ffff3f0005fe02fea735c8500000000049454e44ae426082");
	for (const char *name : {"card-aaa.png", "qualified-bbb.png", "cover-ccc.png"}) {
		QFile artwork(QDir(cardsSource).filePath(QString::fromUtf8(name)));
		check(artwork.open(QIODevice::WriteOnly), "bundled artwork");
		artwork.write(seedPng);
		artwork.close();
	}
	QFile bundledSession(QDir(bundleDir.path()).filePath(QStringLiteral("default-session/session.json")));
	check(bundledSession.open(QIODevice::WriteOnly), "bundled session");
	bundledSession.write(
		R"({"version":5,"phase":"collecting","round":1,"displayMode":"scoreboard",)"
		R"("layout":{"x":0.5,"y":0.5,"scale":1},"pieces":{},"layers":{},)"
		R"("timing":{"scoreboard":{"card":777}},)"
		R"("bottomBar":{"cover":"cover-ccc.png","pieces":{},"layers":{}},)"
		R"("presidents":[{"id":"seed-one","name":"หนึ่ง","school":"ADT",)"
		R"("card":"card-aaa.png","qualified":"qualified-bbb.png","status":"waiting"},)"
		R"({"id":"seed-two","name":"สอง","school":"AI","card":"card-aaa.png","status":"waiting"}]})");
	bundledSession.close();

	QTemporaryDir freshDir;
	check(freshDir.isValid(), "temporary fresh config");
	fffTestConfigPath = freshDir.path();

	// No bundle in sight is the behaviour this plugin always had.
	fffTestDataPath.clear();
	FffSession bare;
	bare.load();
	check(bare.presidents().isEmpty() && !QFile::exists(QDir(freshDir.path()).filePath("session.json")),
	      "a build carrying no bundled session still starts empty");

	fffTestDataPath = bundleDir.path();
	FffSession seeded;
	seeded.load();
	check(seeded.presidents().size() == 2, "a fresh install takes the bundled roster");
	check(seeded.presidents().at(0).school == QLatin1String("ADT") &&
		      seeded.presidents().at(0).card == QLatin1String("card-aaa.png"),
	      "with its schools and its artwork");
	check(QFile::exists(QDir(freshDir.path()).filePath("cards/card-aaa.png")) &&
		      QFile::exists(QDir(freshDir.path()).filePath("cards/cover-ccc.png")),
	      "and the pictures it points at are copied into place");
	check(state(seeded).value("timing").toObject().value("scoreboard").toObject().value("card").toDouble() == 777,
	      "templates and lengths come with it");
	// PINs belong to an event and to one machine, so the bundle carries none.
	const QString firstPin = seeded.presidents().at(0).pin;
	const QString secondPin = seeded.presidents().at(1).pin;
	check(firstPin.size() == 6 && secondPin.size() == 6 && firstPin != secondPin,
	      "every president is given a PIN of this machine's own");
	check(seeded.monitorKey().isEmpty() && !seeded.monitorLanEnabled(),
	      "and LAN access starts off, with no key inherited from anyone");
	check(seeded.phase() == FffPhase::Collecting && seeded.round() == 1 && seeded.votedCount() == 0,
	      "a seeded board starts blank on round one");

	// Seeding happens once. Re-reading must find the machine's own session,
	// PINs and all, not the bundle again.
	FffSession reseeded;
	reseeded.load();
	check(reseeded.presidents().size() == 2 && reseeded.presidents().at(0).pin == firstPin,
	      "the seeded session is saved, so the next launch keeps its PINs");

	// An operator's own board is never touched, however old or small it is.
	QTemporaryDir usedDir;
	check(usedDir.isValid(), "temporary used config");
	fffTestConfigPath = usedDir.path();
	QFile ownSession(QDir(usedDir.path()).filePath(QStringLiteral("session.json")));
	check(ownSession.open(QIODevice::WriteOnly), "existing session");
	ownSession.write(R"({"version":5,"presidents":[],"phase":"collecting","round":9})");
	ownSession.close();
	FffSession untouched;
	untouched.load();
	check(untouched.presidents().isEmpty() && untouched.round() == 9,
	      "a session of its own is never replaced by the bundled one");
	check(!QFile::exists(QDir(usedDir.path()).filePath("cards/card-aaa.png")),
	      "and no bundled artwork is copied over it");

	// The bundle that actually ships, put through the real loader. A session
	// the plugin would refuse is a release where every new install comes up
	// blank, and nothing else in the build would notice.
	if (QFile::exists(
		    QDir(QStringLiteral(FFF_BUNDLED_DATA)).filePath(QStringLiteral("default-session/session.json")))) {
		QTemporaryDir shippedDir;
		check(shippedDir.isValid(), "temporary config for the shipped bundle");
		fffTestConfigPath = shippedDir.path();
		fffTestDataPath = QStringLiteral(FFF_BUNDLED_DATA);
		FffSession shipped;
		shipped.load();
		check(!shipped.presidents().isEmpty(), "the bundled session loads and has a roster");
		check(shipped.monitorKey().isEmpty() && !shipped.monitorLanEnabled(),
		      "and carries no LAN key into a repository anyone can read");
		check(shipped.phase() == FffPhase::Collecting && shipped.round() == 1 && shipped.votedCount() == 0,
		      "and starts blank on round one");
		for (const FffPresident &president : shipped.presidents()) {
			check(president.pin.size() == 6, "every shipped president is given a PIN here");
			check(!president.school.isEmpty(), "and keeps the school it belongs to");
			// Artwork the session names but the bundle forgot would leave a
			// hole on air that only shows up during a show.
			for (const QString &kind :
			     {QStringLiteral("card"), QStringLiteral("bottomBar"), QStringLiteral("logo"),
			      QStringLiteral("logo2"), QStringLiteral("qualified"), QStringLiteral("unqualified"),
			      QStringLiteral("waiting")}) {
				const QString url = shipped.assetUrl(president, kind);
				if (url.isEmpty())
					continue;
				check(QFile::exists(QDir(shippedDir.path())
							    .filePath(QStringLiteral("cards/") +
								      url.section(QLatin1Char('='), 1, 1)
									      .section(QLatin1Char('&'), 0, 0))),
				      "and every picture it names arrived with it");
			}
		}
	}

	fffTestDataPath.clear();
	fffTestConfigPath = temp.path();
	qInfo("PASS: a fresh install starts from the bundled session, an existing one is left alone");
}
