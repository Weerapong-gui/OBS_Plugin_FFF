// Pure access rules for opening the monitor from another machine on the LAN.
#include "fff-monitor-access.h"

#include <QCoreApplication>
#include <QRegularExpression>

static void check(bool ok, const char *message)
{
	if (!ok)
		qFatal("%s", message);
}

int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	using namespace FffMonitorAccess;

	const QString key = generateKey();
	check(QRegularExpression(QStringLiteral("^[0-9a-f]{32}$")).match(key).hasMatch(),
	      "key is 32 lowercase hex characters");
	check(generateKey() != key, "every key is fresh");

	check(keysEqual(key, key), "a key matches itself");
	check(!keysEqual(key, generateKey()), "different keys do not match");
	check(!keysEqual(key, key.left(31)), "a prefix does not match");
	check(!keysEqual(key.left(31), key), "a longer key does not match its prefix");
	check(!keysEqual(QString(), QString()), "empty keys never match");

	check(cookieValue("fff_monitor=abc") == QLatin1String("abc"), "single cookie");
	check(cookieValue("theme=dark; fff_monitor=abc; lang=th") == QLatin1String("abc"), "cookie among others");
	check(cookieValue("  fff_monitor = abc  ") == QLatin1String("abc"), "spaces around a cookie");
	check(cookieValue("xfff_monitor=abc; fff_monitorx=def").isEmpty(), "similar names are not the monitor cookie");
	check(cookieValue(QByteArray()).isEmpty(), "no cookie header");
	check(setCookieHeader(QStringLiteral("abc")) == "fff_monitor=abc; HttpOnly; SameSite=Strict; Path=/",
	      "cookie attributes");

	check(classify("GET", QStringLiteral("/monitor")) == Endpoint::MonitorPage, "monitor page");
	// /score shows the tally before it is on air, so it is admitted the same
	// way. The server sends `path` back as the redirect's Location, which is
	// only safe while these two exact literals are the whole of MonitorPage.
	check(classify("GET", QStringLiteral("/score")) == Endpoint::MonitorPage, "score page");
	check(classify("GET", QStringLiteral("/score/")) == Endpoint::Public &&
		      classify("GET", QStringLiteral("/scoreboard")) == Endpoint::Public &&
		      classify("POST", QStringLiteral("/score")) == Endpoint::Public,
	      "only the exact GET /score is the score page");
	for (const char *path : {"/api/events/overlay", "/api/monitor/access"})
		check(classify("GET", QString::fromLatin1(path)) == Endpoint::MonitorApi, "monitor reads");
	for (const char *path : {"/api/layout", "/api/layer", "/api/operator/vote", "/api/status", "/api/asset",
				 "/api/template", "/api/timing"})
		check(classify("POST", QString::fromLatin1(path)) == Endpoint::MonitorApi, "monitor writes");
	for (const char *path : {"/api/display", "/api/logo", "/api/round"})
		check(classify("POST", QString::fromLatin1(path)) == Endpoint::LocalOnly, "on-air controls");
	check(classify("GET", QStringLiteral("/")) == Endpoint::Public &&
		      classify("GET", QStringLiteral("/overlay")) == Endpoint::Public &&
		      classify("GET", QStringLiteral("/api/cover")) == Endpoint::Public &&
		      classify("POST", QStringLiteral("/api/vote")) == Endpoint::Public &&
		      classify("POST", QStringLiteral("/api/auth")) == Endpoint::Public &&
		      classify("GET", QStringLiteral("/api/display")) == Endpoint::Public,
	      "phone, overlay page and assets stay public");

	const QString stored = QStringLiteral("0123456789abcdef0123456789abcdef");
	const auto request = [&](bool loopback, Endpoint endpoint, bool enabled, const QString &query,
				 const QString &cookie) {
		Request value;
		value.loopback = loopback;
		value.endpoint = endpoint;
		value.enabled = enabled;
		value.storedKey = stored;
		value.queryKey = query;
		value.cookieKey = cookie;
		return value;
	};
	const QString none;
	check(decide(request(true, Endpoint::LocalOnly, false, none, none)) == Decision::Allow,
	      "this machine keeps on-air controls");
	check(decide(request(true, Endpoint::MonitorPage, false, none, none)) == Decision::Allow,
	      "this machine opens the monitor without a key");
	check(decide(request(false, Endpoint::Public, false, none, none)) == Decision::Allow,
	      "public routes stay open");
	check(decide(request(false, Endpoint::LocalOnly, true, stored, stored)) == Decision::Deny,
	      "a LAN monitor never reaches on-air controls");
	check(decide(request(false, Endpoint::MonitorPage, false, stored, none)) == Decision::Deny,
	      "switched off refuses the right link");
	check(decide(request(false, Endpoint::MonitorApi, false, none, stored)) == Decision::Deny,
	      "switched off refuses the right cookie");
	check(decide(request(false, Endpoint::MonitorPage, true, stored, none)) == Decision::Redirect,
	      "the right link redirects");
	check(decide(request(false, Endpoint::MonitorApi, true, stored, none)) == Decision::Deny,
	      "a link key only works on the page");
	check(decide(request(false, Endpoint::MonitorPage, true, none, stored)) == Decision::Allow,
	      "the cookie opens the page");
	check(decide(request(false, Endpoint::MonitorApi, true, none, stored)) == Decision::Allow,
	      "the cookie reaches monitor APIs");
	check(decide(request(false, Endpoint::MonitorApi, true, none, QStringLiteral("stale"))) == Decision::Deny,
	      "a stale cookie is refused");
	check(decide(request(false, Endpoint::MonitorPage, true, none, none)) == Decision::Deny, "no key is refused");
	Request unkeyed = request(false, Endpoint::MonitorApi, true, none, none);
	unkeyed.storedKey.clear();
	check(decide(unkeyed) == Decision::Deny, "no stored key refuses everyone remote");

	check(presentedWrongKey(request(false, Endpoint::MonitorPage, true, QStringLiteral("guess"), none)),
	      "a guessed link is a wrong key");
	check(presentedWrongKey(request(false, Endpoint::MonitorApi, true, none, QStringLiteral("stale"))),
	      "a stale cookie is a wrong key");
	check(!presentedWrongKey(request(false, Endpoint::MonitorPage, true, none, none)), "no key is not a guess");
	check(!presentedWrongKey(request(false, Endpoint::MonitorPage, true, QStringLiteral("old"), stored)),
	      "a valid cookie outweighs an old link");

	qInfo("PASS: monitor access keys, cookies, endpoint classes and decisions");
	return 0;
}
