/*
FFF Tools for OBS - who may open the operator monitor from the LAN
Copyright (C) 2026 Student Union MFU <studentunion.developer@gmail.com>
GPL-2.0-or-later
*/

#include "fff-monitor-access.h"

#include <QRandomGenerator>
#include <QStringList>

namespace FffMonitorAccess {

QString generateKey()
{
	QRandomGenerator *generator = QRandomGenerator::system();
	return QStringLiteral("%1%2")
		.arg(generator->generate64(), 16, 16, QLatin1Char('0'))
		.arg(generator->generate64(), 16, 16, QLatin1Char('0'));
}

bool keysEqual(const QString &a, const QString &b)
{
	if (a.isEmpty() || b.isEmpty())
		return false;
	const QByteArray left = a.toUtf8();
	const QByteArray right = b.toUtf8();
	const qsizetype length = qMax(left.size(), right.size());
	// A mismatch in the first byte takes as long to report as one in the last.
	unsigned char difference = left.size() == right.size() ? 0 : 1;
	for (qsizetype i = 0; i < length; ++i) {
		const unsigned char l = i < left.size() ? static_cast<unsigned char>(left.at(i)) : 0;
		const unsigned char r = i < right.size() ? static_cast<unsigned char>(right.at(i)) : 0;
		difference |= l ^ r;
	}
	return difference == 0;
}

QString cookieValue(const QByteArray &cookieHeader, const QByteArray &name)
{
	for (const QByteArray &part : cookieHeader.split(';')) {
		const QByteArray pair = part.trimmed();
		const qsizetype equals = pair.indexOf('=');
		if (equals <= 0)
			continue;
		if (pair.left(equals).trimmed() == name)
			return QString::fromUtf8(pair.mid(equals + 1).trimmed());
	}
	return QString();
}

QByteArray setCookieHeader(const QString &key)
{
	return QByteArrayLiteral("fff_monitor=") + key.toUtf8() +
	       QByteArrayLiteral("; HttpOnly; SameSite=Strict; Path=/");
}

Endpoint classify(const QByteArray &method, const QString &path)
{
	if (method == "GET") {
		// Both monitor pages trade a key in the address bar for a cookie, so
		// the redirect in the server may reuse `path` verbatim: this returns
		// MonitorPage only for one of these two string literals.
		if (path == QLatin1String("/monitor") || path == QLatin1String("/score"))
			return Endpoint::MonitorPage;
		if (path == QLatin1String("/api/events/overlay") || path == QLatin1String("/api/monitor/access"))
			return Endpoint::MonitorApi;
		return Endpoint::Public;
	}
	if (method == "POST") {
		static const QStringList monitorWrites = {
			QStringLiteral("/api/layout"),        QStringLiteral("/api/layer"),
			QStringLiteral("/api/operator/vote"), QStringLiteral("/api/status"),
			QStringLiteral("/api/asset"),         QStringLiteral("/api/template"),
			QStringLiteral("/api/timing")};
		if (monitorWrites.contains(path))
			return Endpoint::MonitorApi;
		if (path == QLatin1String("/api/display") || path == QLatin1String("/api/logo"))
			return Endpoint::LocalOnly;
	}
	return Endpoint::Public;
}

Decision decide(const Request &request)
{
	if (request.loopback || request.endpoint == Endpoint::Public)
		return Decision::Allow;
	if (request.endpoint == Endpoint::LocalOnly || !request.enabled || request.storedKey.isEmpty())
		return Decision::Deny;
	if (request.endpoint == Endpoint::MonitorPage && keysEqual(request.queryKey, request.storedKey))
		return Decision::Redirect;
	if (keysEqual(request.cookieKey, request.storedKey))
		return Decision::Allow;
	return Decision::Deny;
}

bool presentedWrongKey(const Request &request)
{
	const bool presented = !request.queryKey.isEmpty() || !request.cookieKey.isEmpty();
	return presented && !keysEqual(request.queryKey, request.storedKey) &&
	       !keysEqual(request.cookieKey, request.storedKey);
}

} // namespace FffMonitorAccess
