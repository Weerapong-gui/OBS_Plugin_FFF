/*
FFF Tools for OBS - who may open the operator monitor from the LAN
Copyright (C) 2026 Student Union MFU <studentunion.developer@gmail.com>
GPL-2.0-or-later
*/

#pragma once

#include <QByteArray>
#include <QString>

/*
 * Access rules for the monitor, kept free of sockets so every rule can be
 * tested on its own. The server describes a request; these functions say
 * whether it may proceed.
 *
 * This machine always gets in. Another machine needs the operator's switch on
 * and the current key: once in the link (traded for a cookie by a redirect),
 * then in the cookie on every request the page makes. On-air controls never
 * open to the LAN, key or not.
 */
namespace FffMonitorAccess {

enum class Endpoint { Public, MonitorPage, MonitorApi, LocalOnly };
enum class Decision { Allow, Redirect, Deny };

struct Request {
	bool loopback = false;
	Endpoint endpoint = Endpoint::LocalOnly;
	bool enabled = false;
	QString storedKey;
	QString queryKey;
	QString cookieKey;
};

// 32 lowercase hex characters (128 bits) from the system generator.
QString generateKey();
// Walks the longer input whatever happens; empty keys never match.
bool keysEqual(const QString &a, const QString &b);
// Value of `name` in a raw Cookie header ("a=1; fff_monitor=abc"), empty when absent.
QString cookieValue(const QByteArray &cookieHeader, const QByteArray &name = QByteArrayLiteral("fff_monitor"));
QByteArray setCookieHeader(const QString &key);
Endpoint classify(const QByteArray &method, const QString &path);
Decision decide(const Request &request);
// A key arrived and none matched. The caller slows down a Deny only.
bool presentedWrongKey(const Request &request);

} // namespace FffMonitorAccess
