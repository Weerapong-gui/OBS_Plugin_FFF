// Round 1 and Round 2 centre-logo artwork, and which set goes on air.
#include "fff-http-server.h"
#include "fff-session.h"
#include "obs-stubs.h"

#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QTemporaryDir>
#include <QTimer>

static void check(bool ok, const char *message)
{
	if (!ok)
		qFatal("%s", message);
}

static QString writePng(const QString &path, const QSize &size)
{
	QImage image(size, QImage::Format_ARGB32);
	image.fill(QColor(226, 60, 60, 128));
	check(image.save(path, "PNG"), "write test PNG");
	return path;
}

static QJsonObject stateOf(const FffSession &session)
{
	return QJsonDocument::fromJson(session.overlayStateJson()).object();
}

static QJsonObject presidentState(const FffSession &session, const QString &id)
{
	for (const QJsonValue &value : stateOf(session).value(QStringLiteral("presidents")).toArray()) {
		if (value.toObject().value(QStringLiteral("id")).toString() == id)
			return value.toObject();
	}
	return QJsonObject();
}

static int stateRound(const FffSession &session)
{
	return stateOf(session).value(QStringLiteral("bottomBar")).toObject().value(QStringLiteral("logoRound")).toInt();
}

int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	QTemporaryDir temp;
	check(temp.isValid(), "temporary config");
	fffTestConfigPath = temp.path();
	const QString small = writePng(temp.filePath(QStringLiteral("round1.png")), QSize(300, 200));
	const QString large = writePng(temp.filePath(QStringLiteral("round2.png")), QSize(3000, 2000));

	FffSession session;
	check(session.logoRound() == 1, "a new session shows round 1 logos");
	FffPresident a;
	a.id = QStringLiteral("a");
	a.pin = QStringLiteral("111111");
	a.logo = session.importAsset(small, a.id, QStringLiteral("logo"));
	a.logo2 = session.importAsset(large, a.id, QStringLiteral("logo2"));
	FffPresident b;
	b.id = QStringLiteral("b");
	b.pin = QStringLiteral("222222");
	b.logo = session.importAsset(small, b.id, QStringLiteral("logo"));
	check(!a.logo.isEmpty() && !a.logo2.isEmpty() && !b.logo.isEmpty() && session.addPresident(a) &&
		      session.addPresident(b),
	      "roster with round 1 and round 2 logos");

	const FffPresident &storedA = *session.presidentById(QStringLiteral("a"));
	const QString round1Url = session.assetUrl(storedA, QStringLiteral("logo"));
	const QString round2Url = session.assetUrl(storedA, QStringLiteral("logo2"));
	check(round2Url.startsWith(QStringLiteral("/api/logo2/a?v=")) && round1Url != round2Url,
	      "round 2 artwork has its own URL");
	check(session.assetPaths().contains(QDir(session.cardsDir()).filePath(a.logo2)),
	      "round 2 artwork is prepared like any other asset");

	QJsonObject stateA = presidentState(session, QStringLiteral("a"));
	check(stateA.value(QStringLiteral("logoUrl")).toString() == round1Url &&
		      stateA.value(QStringLiteral("logoRound1Url")).toString() == round1Url &&
		      stateA.value(QStringLiteral("logoRound2Url")).toString() == round2Url,
	      "round 1 puts round 1 artwork on air and names both rounds");
	check(stateRound(session) == 1, "state carries the chosen round");

	int changes = 0;
	const auto counter = QObject::connect(&session, &FffSession::changed, [&]() { ++changes; });
	check(!session.setLogoRound(3) && !session.setLogoRound(0) && session.logoRound() == 1 && changes == 0,
	      "only rounds 1 and 2 exist");
	check(session.setLogoRound(2) && session.logoRound() == 2 && changes == 1, "switch to round 2");
	check(session.setLogoRound(2) && changes == 1, "choosing the same round again announces nothing");
	stateA = presidentState(session, QStringLiteral("a"));
	check(stateA.value(QStringLiteral("logoUrl")).toString() == round2Url, "round 2 puts round 2 artwork on air");
	const QJsonObject stateB = presidentState(session, QStringLiteral("b"));
	check(stateB.value(QStringLiteral("logoUrl")).toString().isEmpty() &&
		      stateB.value(QStringLiteral("logoRound2Url")).toString().isEmpty(),
	      "a school without round 2 artwork shows no logo in round 2");
	check(stateRound(session) == 2, "state follows the chosen round");
	QObject::disconnect(counter);

	FffSession reloaded;
	reloaded.load();
	check(reloaded.logoRound() == 2 && reloaded.presidentById(QStringLiteral("a"))->logo2 == a.logo2,
	      "round and round 2 artwork survive a restart");

	const int voteRound = session.round();
	check(session.clearRound() && session.round() == voteRound + 1 && session.logoRound() == 2,
	      "a new vote round leaves the logo round alone");

	QFile file(temp.filePath(QStringLiteral("session.json")));
	check(file.open(QIODevice::ReadOnly), "read session file");
	QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
	file.close();
	QJsonObject bottom = root.value(QStringLiteral("bottomBar")).toObject();
	bottom.insert(QStringLiteral("logoRound"), 7);
	root.insert(QStringLiteral("bottomBar"), bottom);
	check(file.open(QIODevice::WriteOnly | QIODevice::Truncate) && file.write(QJsonDocument(root).toJson()) > 0,
	      "write an unknown round");
	file.close();
	FffSession odd;
	odd.load();
	check(odd.logoRound() == 1, "an unknown round loads as round 1");
	check(session.save(), "restore the session file");

	int failures = 0;
	const auto failed = QObject::connect(&session, &FffSession::saveFailed, [&]() { ++failures; });
	QFile blocked(temp.filePath(QStringLiteral("blocked")));
	check(blocked.open(QIODevice::WriteOnly), "create blocked config");
	blocked.close();
	fffTestConfigPath = blocked.fileName();
	check(!session.setLogoRound(1) && session.logoRound() == 2 && failures == 1, "a failed switch keeps the round");
	fffTestConfigPath = temp.path();
	QObject::disconnect(failed);

	FffHttpServer server(&session);
	QString error;
	check(server.start(0, &error), "HTTP starts");
	QNetworkAccessManager network;
	auto *reply = network.get(
		QNetworkRequest(QUrl(QStringLiteral("http://127.0.0.1:%1%2").arg(server.boundPort()).arg(round2Url))));
	QEventLoop loop;
	QTimer timer;
	timer.setSingleShot(true);
	QObject::connect(&timer, &QTimer::timeout, reply, &QNetworkReply::abort);
	QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
	timer.start(15000);
	loop.exec();
	QImage served;
	check(reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 200 &&
		      served.loadFromData(reply->readAll(), "PNG") && served.size() == QSize(1620, 1080),
	      "round 2 artwork is served, shrunk to the stream");
	reply->deleteLater();

	qInfo("PASS: centre logo rounds persist, select artwork and serve it");
	return 0;
}
