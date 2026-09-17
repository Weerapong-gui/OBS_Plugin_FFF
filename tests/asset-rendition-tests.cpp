// Artwork larger than the 1920x1080 stream is served shrunk; originals stay as imported.
#include "fff-asset-rendition.h"
#include "fff-http-server.h"
#include "fff-session.h"
#include "obs-stubs.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QImage>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPainter>
#include <QTemporaryDir>
#include <QTimer>

#include <functional>

static void check(bool ok, const char *message)
{
	if (!ok)
		qFatal("%s", message);
}

// Transparent all round with an opaque red square in the middle, so a shrink
// that loses the alpha channel or the colour shows up in the pixels.
static QString writePng(const QString &path, const QSize &size)
{
	QImage image(size, QImage::Format_ARGB32);
	image.fill(Qt::transparent);
	QPainter painter(&image);
	painter.fillRect(QRect(size.width() / 4, size.height() / 4, size.width() / 2, size.height() / 2),
			 QColor(226, 60, 60));
	painter.end();
	check(image.save(path, "PNG"), "write test PNG");
	return path;
}

static bool waitFor(const std::function<bool()> &condition, int timeoutMs = 15000)
{
	QElapsedTimer clock;
	clock.start();
	while (!condition() && clock.elapsed() < timeoutMs) {
		QEventLoop loop;
		QTimer::singleShot(10, &loop, &QEventLoop::quit);
		loop.exec();
	}
	return condition();
}

static void testRules(const QTemporaryDir &temp)
{
	using namespace FffAssetRendition;

	check(fittedSize(QSize(4500, 8000)) == QSize(607, 1080), "portrait artwork fits the stream height");
	check(fittedSize(QSize(8000, 4500)) == QSize(1920, 1080), "wide artwork fits the stream width");
	check(fittedSize(QSize(2062, 1184)) == QSize(1880, 1080), "landscape artwork fits");
	check(fittedSize(QSize(1666, 546)) == QSize(1666, 546), "artwork that already fits is left alone");
	check(fittedSize(QSize()) == QSize(), "an unknown size stays unknown");
	check(urlTag() == QLatin1String("fit=1920x1080"), "URL tag names the bounds");

	const QString big = writePng(temp.filePath(QStringLiteral("big.png")), QSize(3000, 2000));
	const QString fits = writePng(temp.filePath(QStringLiteral("fits.png")), QSize(400, 300));
	const QString fake = temp.filePath(QStringLiteral("fake.png"));
	QFile fakeFile(fake);
	check(fakeFile.open(QIODevice::WriteOnly) && fakeFile.write(QByteArray::fromHex("89504e470d0a1a0a")) == 8,
	      "write header-only PNG");
	fakeFile.close();

	check(needsRendition(big), "artwork larger than the stream needs a rendition");
	check(!needsRendition(fits), "artwork that fits needs none");
	check(!needsRendition(fake) && !needsRendition(temp.filePath(QStringLiteral("missing.png"))),
	      "unreadable or missing files need none");
	check(renditionPath(big) == QDir(temp.path()).filePath(QStringLiteral("renditions/big.png")),
	      "renditions live beside the originals");

	check(writeRendition(big, renditionPath(big)), "write a rendition");
	const QImage shrunk(renditionPath(big));
	check(shrunk.size() == QSize(1620, 1080), "the rendition fits the stream");
	check(shrunk.hasAlphaChannel() && shrunk.pixelColor(10, 10).alpha() == 0, "transparency survives");
	check(shrunk.pixelColor(810, 540) == QColor(226, 60, 60), "colour survives");
	check(QImage(big).size() == QSize(3000, 2000), "the original is untouched");
}

static void testServing(const QTemporaryDir &temp)
{
	const QString config = temp.filePath(QStringLiteral("config"));
	check(QDir().mkpath(config), "config directory");
	fffTestConfigPath = config;

	FffSession session;
	FffPresident person;
	person.id = QStringLiteral("p");
	person.pin = QStringLiteral("123456");
	person.bottomBar = session.importAsset(temp.filePath(QStringLiteral("big.png")), person.id,
					       QStringLiteral("bottomBar"));
	person.logo =
		session.importAsset(temp.filePath(QStringLiteral("fits.png")), person.id, QStringLiteral("logo"));
	check(!person.bottomBar.isEmpty() && !person.logo.isEmpty() && session.addPresident(person),
	      "roster with large and small artwork");
	const FffPresident &stored = *session.presidentById(person.id);
	const QString barUrl = session.assetUrl(stored, QStringLiteral("bottomBar"));
	const QString logoUrl = session.assetUrl(stored, QStringLiteral("logo"));
	check(barUrl.endsWith(QStringLiteral("&fit=1920x1080")) && logoUrl.endsWith(QStringLiteral("&fit=1920x1080")),
	      "asset URLs name the bounds, so a cache holding full-size bytes is never reused");

	FffHttpServer server(&session);
	QString error;
	check(server.start(0, &error), "HTTP starts");
	QNetworkAccessManager network;
	const auto get = [&](const QString &url) {
		auto *reply = network.get(
			QNetworkRequest(QUrl(QStringLiteral("http://127.0.0.1:%1%2").arg(server.boundPort()).arg(url))));
		QEventLoop loop;
		QTimer timer;
		timer.setSingleShot(true);
		QObject::connect(&timer, &QTimer::timeout, reply, &QNetworkReply::abort);
		QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
		timer.start(15000);
		loop.exec();
		const auto result =
			qMakePair(reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(), reply->readAll());
		reply->deleteLater();
		return result;
	};

	const QString cards = session.cardsDir();
	const auto bar = get(barUrl);
	QImage served;
	check(bar.first == 200 && served.loadFromData(bar.second, "PNG"), "oversized artwork is served as a PNG");
	check(served.size() == QSize(1620, 1080), "oversized artwork is served shrunk to the stream");
	check(QFile::exists(QDir(cards).filePath(QStringLiteral("renditions/") + person.bottomBar)),
	      "the rendition is kept on disk for the next start");
	check(QImage(QDir(cards).filePath(person.bottomBar)).size() == QSize(3000, 2000),
	      "the imported original stays full size");
	check(get(barUrl) == bar, "later requests get the same rendition");

	QFile logoFile(QDir(cards).filePath(person.logo));
	check(logoFile.open(QIODevice::ReadOnly), "open small original");
	const auto logo = get(logoUrl);
	check(logo.first == 200 && logo.second == logoFile.readAll(), "artwork that fits is served byte for byte");

	// Renditions are made as soon as artwork enters the session, before any
	// page asks, so the heavy work never lands on a reveal.
	const QString cover = session.importAsset(temp.filePath(QStringLiteral("big.png")), QString(),
						  QStringLiteral("cover"));
	check(!cover.isEmpty() && session.setCover(cover), "select a large cover");
	check(waitFor([&]() { return QFile::exists(QDir(cards).filePath(QStringLiteral("renditions/") + cover)); }),
	      "a new asset is shrunk without being requested");
	QImage coverImage;
	check(coverImage.loadFromData(get(session.coverUrl()).second, "PNG") && coverImage.size() == QSize(1620, 1080),
	      "the prepared cover is served shrunk");
}

int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	QTemporaryDir temp;
	check(temp.isValid(), "temporary directory");
	testRules(temp);
	testServing(temp);
	qInfo("PASS: asset renditions fit the stream and keep originals");
	return 0;
}
