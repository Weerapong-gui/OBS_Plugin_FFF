/*
FFF Tools for OBS - embedded HTTP + SSE server
Copyright (C) 2026 Student Union MFU <studentunion.developer@gmail.com>
GPL-2.0-or-later
*/

#include "fff-http-server.h"
#include "fff-session.h"

#include <obs-module.h>
#include <plugin-support.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkInterface>
#include <QPointer>
#include <QRandomGenerator>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>

#include <cmath>

namespace {

constexpr int kMaxRequestBytes = 64 * 1024;
constexpr int kHeartbeatMs = 15000;
constexpr int kBadPinDelayMs = 1000;

QByteArray reasonPhrase(int code)
{
	switch (code) {
	case 200:
		return "OK";
	case 400:
		return "Bad Request";
	case 401:
		return "Unauthorized";
	case 403:
		return "Forbidden";
	case 404:
		return "Not Found";
	case 413:
		return "Payload Too Large";
	case 500:
		return "Internal Server Error";
	default:
		return "Error";
	}
}

QString randomToken()
{
	QRandomGenerator *generator = QRandomGenerator::system();
	return QStringLiteral("%1%2")
		.arg(generator->generate64(), 16, 16, QLatin1Char('0'))
		.arg(generator->generate64(), 16, 16, QLatin1Char('0'));
}

} // namespace

FffHttpServer::FffHttpServer(FffSession *session, QObject *parent) : QObject(parent), m_session(session)
{
	m_heartbeat = new QTimer(this);
	m_heartbeat->setInterval(kHeartbeatMs);
	connect(m_heartbeat, &QTimer::timeout, this, &FffHttpServer::sendHeartbeat);

	connect(m_session, &FffSession::changed, this, [this]() {
		m_cardCache.clear();
		pushState();
	});
}

FffHttpServer::~FffHttpServer()
{
	stop();
}

bool FffHttpServer::start(quint16 port, QString *error)
{
	stop();

	m_server = new QTcpServer(this);
	connect(m_server, &QTcpServer::newConnection, this, &FffHttpServer::acceptConnection);

	if (!m_server->listen(QHostAddress::Any, port)) {
		if (error)
			*error = m_server->errorString();
		m_server->deleteLater();
		m_server = nullptr;
		return false;
	}

	m_boundPort = m_server->serverPort();
	m_heartbeat->start();
	obs_log(LOG_INFO, "flag board listening on port %u", static_cast<unsigned>(m_boundPort));
	emit clientsChanged();
	return true;
}

void FffHttpServer::stop()
{
	m_heartbeat->stop();

	const QList<QTcpSocket *> sockets = m_conns.keys();
	m_conns.clear();
	for (QTcpSocket *socket : sockets) {
		socket->disconnect(this);
		socket->close();
		socket->deleteLater();
	}

	m_tokens.clear();
	m_fileCache.clear();
	m_cardCache.clear();

	if (m_server) {
		m_server->close();
		m_server->deleteLater();
		m_server = nullptr;
	}
	m_boundPort = 0;
	emit clientsChanged();
}

bool FffHttpServer::isListening() const
{
	return m_server && m_server->isListening();
}

int FffHttpServer::phoneClientCount() const
{
	int count = 0;
	for (const Conn &conn : m_conns) {
		if (conn.sse && !conn.overlay)
			++count;
	}
	return count;
}

int FffHttpServer::overlayClientCount() const
{
	int count = 0;
	for (const Conn &conn : m_conns) {
		if (conn.sse && conn.overlay)
			++count;
	}
	return count;
}

QStringList FffHttpServer::lanAddresses()
{
	QStringList out;
	for (const QNetworkInterface &iface : QNetworkInterface::allInterfaces()) {
		const QNetworkInterface::InterfaceFlags flags = iface.flags();
		if (!flags.testFlag(QNetworkInterface::IsUp) || !flags.testFlag(QNetworkInterface::IsRunning))
			continue;
		if (flags.testFlag(QNetworkInterface::IsLoopBack))
			continue;

		for (const QNetworkAddressEntry &entry : iface.addressEntries()) {
			const QHostAddress ip = entry.ip();
			if (ip.protocol() == QAbstractSocket::IPv4Protocol)
				out.append(ip.toString());
		}
	}
	return out;
}

void FffHttpServer::acceptConnection()
{
	while (m_server && m_server->hasPendingConnections()) {
		QTcpSocket *socket = m_server->nextPendingConnection();
		if (!socket)
			break;

		m_conns.insert(socket, Conn());
		connect(socket, &QTcpSocket::readyRead, this, [this, socket]() { readFrom(socket); });
		connect(socket, &QTcpSocket::disconnected, this, [this, socket]() { dropConnection(socket); });
	}
}

void FffHttpServer::dropConnection(QTcpSocket *socket)
{
	const bool wasSse = m_conns.contains(socket) && m_conns.value(socket).sse;
	m_conns.remove(socket);
	socket->deleteLater();
	if (wasSse)
		emit clientsChanged();
}

void FffHttpServer::readFrom(QTcpSocket *socket)
{
	auto it = m_conns.find(socket);
	if (it == m_conns.end())
		return;

	Conn &conn = it.value();
	if (conn.sse) {
		socket->readAll();
		return;
	}

	conn.buffer.append(socket->readAll());
	if (conn.buffer.size() > kMaxRequestBytes) {
		send(socket, 413, "text/plain; charset=utf-8", "too large");
		return;
	}

	const int headerEnd = conn.buffer.indexOf("\r\n\r\n");
	if (headerEnd < 0)
		return;

	const QByteArray head = conn.buffer.left(headerEnd);
	const QList<QByteArray> lines = head.split('\n');
	if (lines.isEmpty()) {
		send(socket, 400, "text/plain; charset=utf-8", "bad request");
		return;
	}

	int contentLength = 0;
	for (int i = 1; i < lines.size(); ++i) {
		const QByteArray line = lines.at(i).trimmed();
		if (line.toLower().startsWith("content-length:"))
			contentLength = line.mid(line.indexOf(':') + 1).trimmed().toInt();
	}
	if (contentLength < 0 || contentLength > kMaxRequestBytes) {
		send(socket, 400, "text/plain; charset=utf-8", "bad request");
		return;
	}
	if (conn.buffer.size() < headerEnd + 4 + contentLength)
		return;

	const QByteArray body = conn.buffer.mid(headerEnd + 4, contentLength);
	const QList<QByteArray> parts = lines.at(0).trimmed().split(' ');
	if (parts.size() < 2) {
		send(socket, 400, "text/plain; charset=utf-8", "bad request");
		return;
	}

	const QUrl url = QUrl::fromEncoded(parts.at(1));
	const QString token = QUrlQuery(url).queryItemValue(QStringLiteral("token"));
	route(socket, parts.at(0), url.path(), token, body);
}

void FffHttpServer::route(QTcpSocket *socket, const QByteArray &method, const QString &path, const QString &token,
			  const QByteArray &body)
{
	if (method == "GET") {
		if (path == QLatin1String("/") || path == QLatin1String("/vote")) {
			sendWebFile(socket, QStringLiteral("phone.html"), "text/html; charset=utf-8");
			return;
		}
		if (path == QLatin1String("/overlay")) {
			sendWebFile(socket, QStringLiteral("overlay.html"), "text/html; charset=utf-8");
			return;
		}
		if (path == QLatin1String("/monitor")) {
			// Shows the flags before they go on air, same as the overlay feed.
			if (!socket->peerAddress().isLoopback()) {
				send(socket, 403, "text/plain; charset=utf-8", "monitor is local only");
				return;
			}
			sendWebFile(socket, QStringLiteral("monitor.html"), "text/html; charset=utf-8");
			return;
		}
		if (path == QLatin1String("/app.css")) {
			sendWebFile(socket, QStringLiteral("app.css"), "text/css; charset=utf-8");
			return;
		}
		if (path == QLatin1String("/board.js")) {
			sendWebFile(socket, QStringLiteral("board.js"), "application/javascript; charset=utf-8");
			return;
		}
		if (path == QLatin1String("/template-editor.js")) {
			sendWebFile(socket, QStringLiteral("template-editor.js"),
				    "application/javascript; charset=utf-8");
			return;
		}
		if (path == QLatin1String("/api/events/overlay")) {
			// The overlay sees every flag before the reveal, so it is
			// only ever served to OBS on this machine.
			if (!socket->peerAddress().isLoopback()) {
				send(socket, 403, "text/plain; charset=utf-8", "overlay is local only");
				return;
			}
			startSse(socket, QString(), true);
			return;
		}
		if (path == QLatin1String("/api/events")) {
			if (!m_tokens.contains(token)) {
				send(socket, 401, "text/plain; charset=utf-8", "unknown token");
				return;
			}
			startSse(socket, token, false);
			return;
		}
		if (path == QLatin1String("/api/cover")) {
			sendCard(socket, QString(), QStringLiteral("cover"));
			return;
		}
		for (const QString &kind : {QStringLiteral("bottomBar"), QStringLiteral("logo")}) {
			const QString prefix = QStringLiteral("/api/") + kind + QStringLiteral("/");
			if (path.startsWith(prefix)) {
				sendCard(socket, path.mid(prefix.size()), kind);
				return;
			}
		}
		if (path.startsWith(QLatin1String("/api/card/"))) {
			sendCard(socket, path.mid(QStringLiteral("/api/card/").size()));
			return;
		}
	} else if (method == "POST") {
		if (path == QLatin1String("/api/display") || path == QLatin1String("/api/logo")) {
			if (!socket->peerAddress().isLoopback()) {
				sendJson(socket, 403, "{\"error\":\"local only\"}");
				return;
			}
			const auto request = QJsonDocument::fromJson(body).object();
			const QString value = request.value(path == QLatin1String("/api/display")
								    ? QStringLiteral("mode")
								    : QStringLiteral("presidentId"))
						      .toString();
			if ((path == QLatin1String("/api/display") && !FffSession::validMode(value)) ||
			    (path == QLatin1String("/api/logo") && !value.isEmpty() &&
			     !m_session->presidentById(value))) {
				sendJson(socket, 400, "{\"error\":\"invalid selection\"}");
				return;
			}
			const bool saved = path == QLatin1String("/api/display") ? m_session->showMode(value)
										 : m_session->setLogoPresident(value);
			sendJson(socket, saved ? 200 : 500, saved ? "{\"ok\":true}" : "{\"error\":\"save failed\"}");
			return;
		}
		if (path == QLatin1String("/api/template")) {
			if (!socket->peerAddress().isLoopback()) {
				send(socket, 403, "text/plain; charset=utf-8", "template is local only");
				return;
			}
			auto value = QJsonDocument::fromJson(body).object();
			const QString mode = value.take(QStringLiteral("mode")).toString(QStringLiteral("scoreboard"));
			if (!FffSession::validMode(mode)) {
				sendJson(socket, 400, "{\"error\":\"invalid mode\"}");
				return;
			}
			if (!FffSession::validCardTemplate(value)) {
				sendJson(socket, 400, "{\"error\":\"invalid template\"}");
				return;
			}
			const bool saved = m_session->setCardTemplate(value, mode);
			sendJson(socket, saved ? 200 : 500, saved ? "{\"ok\":true}" : "{\"error\":\"save failed\"}");
			return;
		}
		if (path == QLatin1String("/api/auth")) {
			handleAuth(socket, body);
			return;
		}
		if (path == QLatin1String("/api/vote")) {
			handleVote(socket, body);
			return;
		}
		if (path == QLatin1String("/api/operator/vote")) {
			if (!socket->peerAddress().isLoopback()) {
				send(socket, 403, "text/plain; charset=utf-8", "operator controls are local only");
				return;
			}
			handleOperatorVote(socket, body);
			return;
		}
		if (path == QLatin1String("/api/layout")) {
			if (!socket->peerAddress().isLoopback()) {
				send(socket, 403, "text/plain; charset=utf-8", "layout is local only");
				return;
			}
			handleLayout(socket, body);
			return;
		}
		if (path == QLatin1String("/api/layer")) {
			if (!socket->peerAddress().isLoopback()) {
				send(socket, 403, "text/plain; charset=utf-8", "layer controls are local only");
				return;
			}
			handleLayer(socket, body);
			return;
		}
	}

	send(socket, 404, "text/plain; charset=utf-8", "not found");
}

void FffHttpServer::handleAuth(QTcpSocket *socket, const QByteArray &body)
{
	const QJsonObject request = QJsonDocument::fromJson(body).object();
	const QString pin = request.value(QStringLiteral("pin")).toString().trimmed();
	const FffPresident *president = m_session->presidentByPin(pin);

	if (!president) {
		// Slow a guessing attempt down without blocking the UI thread.
		QPointer<QTcpSocket> guard(socket);
		QTimer::singleShot(kBadPinDelayMs, this, [this, guard]() {
			if (guard && m_conns.contains(guard))
				sendJson(guard, 401, "{\"error\":\"bad pin\"}");
		});
		return;
	}

	const QString token = randomToken();
	m_tokens.insert(token, president->id);

	QJsonObject reply;
	reply.insert(QStringLiteral("token"), token);
	reply.insert(QStringLiteral("state"),
		     QJsonDocument::fromJson(m_session->phoneStateJson(president->id)).object());
	sendJson(socket, 200, QJsonDocument(reply).toJson(QJsonDocument::Compact));
}

void FffHttpServer::handleVote(QTcpSocket *socket, const QByteArray &body)
{
	const QJsonObject request = QJsonDocument::fromJson(body).object();
	const QString token = request.value(QStringLiteral("token")).toString();
	const QString presidentId = m_tokens.value(token);

	if (presidentId.isEmpty()) {
		sendJson(socket, 401, "{\"error\":\"unknown token\"}");
		return;
	}

	// Idempotent on purpose: the phone sends the colour it currently shows,
	// not "a vote happened", so a retry after a dropout is always safe.
	const FffVote vote = FffSession::voteFromName(request.value(QStringLiteral("color")).toString());
	if (!m_session->setVote(presidentId, vote)) {
		sendJson(socket, m_session->presidentById(presidentId) ? 500 : 404,
			 "{\"error\":\"vote could not be saved\"}");
		return;
	}

	sendJson(socket, 200, m_session->phoneStateJson(presidentId));
}

void FffHttpServer::handleOperatorVote(QTcpSocket *socket, const QByteArray &body)
{
	const QJsonObject request = QJsonDocument::fromJson(body).object();
	const QString presidentId = request.value(QStringLiteral("presidentId")).toString();
	const FffVote vote = FffSession::voteFromName(request.value(QStringLiteral("color")).toString());
	if (!m_session->presidentById(presidentId)) {
		sendJson(socket, 404, "{\"error\":\"president is gone\"}");
		return;
	}
	const bool saved = m_session->setVote(presidentId, vote);
	sendJson(socket, saved ? 200 : 500, saved ? "{\"ok\":true}" : "{\"error\":\"save failed\"}");
}

void FffHttpServer::handleLayout(QTcpSocket *socket, const QByteArray &body)
{
	const QJsonObject request = QJsonDocument::fromJson(body).object();
	const QString mode = request.value(QStringLiteral("mode")).toString(QStringLiteral("scoreboard"));
	if (!FffSession::validMode(mode)) {
		sendJson(socket, 400, "{\"error\":\"invalid mode\"}");
		return;
	}
	const QString special = mode == QLatin1String("bottomBar") ? QStringLiteral("logo") : QStringLiteral("heading");
	if (request.contains(QStringLiteral("target"))) {
		const QString target = request.value(QStringLiteral("target")).toString();
		const bool reset = request.value(QStringLiteral("reset")).toBool();
		if (target == QLatin1String("all") && reset) {
			const bool saved = m_session->resetLayouts(mode);
			sendJson(socket, saved ? 200 : 500, saved ? "{\"ok\":true}" : "{\"error\":\"save failed\"}");
			return;
		}
		if (target != special && !(mode == QLatin1String("bottomBar") && target == QLatin1String("cover")) &&
		    (!target.startsWith(QLatin1String("card:")) || !m_session->presidentById(target.mid(5)))) {
			sendJson(socket, 404, "{\"error\":\"unknown layout target\"}");
			return;
		}
		FffLayout piece;
		if (!reset) {
			const QJsonObject value = request.value(QStringLiteral("layout")).toObject();
			for (const QString &key : {QStringLiteral("x"), QStringLiteral("y"), QStringLiteral("scale")}) {
				if (!value.value(key).isDouble() || !std::isfinite(value.value(key).toDouble())) {
					sendJson(socket, 400, "{\"error\":\"invalid layout\"}");
					return;
				}
			}
			piece.x = qBound(0.0, value.value(QStringLiteral("x")).toDouble(), 1.0);
			piece.y = qBound(0.0, value.value(QStringLiteral("y")).toDouble(), 1.0);
			piece.scale = qBound(0.5, value.value(QStringLiteral("scale")).toDouble(), 2.0);
			for (const QString &key : {QStringLiteral("scaleX"), QStringLiteral("scaleY"),
						   QStringLiteral("resultScaleX"), QStringLiteral("resultScaleY")}) {
				if (value.contains(key) &&
				    (!value.value(key).isDouble() || !std::isfinite(value.value(key).toDouble()))) {
					sendJson(socket, 400, "{\"error\":\"invalid layout\"}");
					return;
				}
			}
			for (const QString &key : {QStringLiteral("imageOpacity"), QStringLiteral("resultOpacity")}) {
				if (value.contains(key) &&
				    (!value.value(key).isDouble() || !std::isfinite(value.value(key).toDouble()) ||
				     value.value(key).toDouble() < 0 || value.value(key).toDouble() > 1)) {
					sendJson(socket, 400, "{\"error\":\"invalid opacity\"}");
					return;
				}
			}
			piece.imageOpacity = value.value(QStringLiteral("imageOpacity")).toDouble(-1);
			piece.resultOpacity = value.value(QStringLiteral("resultOpacity")).toDouble(-1);
			piece.scaleX = qBound(0.5, value.value(QStringLiteral("scaleX")).toDouble(piece.scale), 2.0);
			piece.scaleY = qBound(0.5, value.value(QStringLiteral("scaleY")).toDouble(piece.scale), 2.0);
			piece.resultScaleX =
				qBound(0.5, value.value(QStringLiteral("resultScaleX")).toDouble(1.0), 2.0);
			piece.resultScaleY =
				qBound(0.5, value.value(QStringLiteral("resultScaleY")).toDouble(1.0), 2.0);
		}
		const bool saved = m_session->setPieceLayout(target, reset ? nullptr : &piece, mode);
		sendJson(socket, saved ? 200 : 500, saved ? "{\"ok\":true}" : "{\"error\":\"save failed\"}");
		return;
	}
	// Keep the original whole-board endpoint for existing local clients.
	FffLayout layout = m_session->layout(mode);
	if (request.contains(QStringLiteral("x")))
		layout.x = request.value(QStringLiteral("x")).toDouble(layout.x);
	if (request.contains(QStringLiteral("y")))
		layout.y = request.value(QStringLiteral("y")).toDouble(layout.y);
	if (request.contains(QStringLiteral("scale")))
		layout.scale = request.value(QStringLiteral("scale")).toDouble(layout.scale);

	const bool saved = m_session->setLayout(layout, mode);
	sendJson(socket, saved ? 200 : 500, saved ? "{\"ok\":true}" : "{\"error\":\"save failed\"}");
}

void FffHttpServer::handleLayer(QTcpSocket *socket, const QByteArray &body)
{
	const QJsonObject request = QJsonDocument::fromJson(body).object();
	const QString mode = request.value(QStringLiteral("mode")).toString(QStringLiteral("scoreboard"));
	if (!FffSession::validMode(mode)) {
		sendJson(socket, 400, "{\"error\":\"invalid mode\"}");
		return;
	}
	const QString special = mode == QLatin1String("bottomBar") ? QStringLiteral("logo") : QStringLiteral("heading");
	const QString target = request.value(QStringLiteral("target")).toString();
	const QString action = request.value(QStringLiteral("action")).toString();
	if (target != special && !(mode == QLatin1String("bottomBar") && target == QLatin1String("cover")) &&
	    (!target.startsWith(QLatin1String("card:")) || !m_session->presidentById(target.mid(5)))) {
		sendJson(socket, 404, "{\"error\":\"unknown layer target\"}");
		return;
	}
	if (action != QLatin1String("front") && action != QLatin1String("forward") &&
	    action != QLatin1String("backward") && action != QLatin1String("back")) {
		sendJson(socket, 400, "{\"error\":\"invalid layer action\"}");
		return;
	}
	const bool saved = m_session->movePieceLayer(target, action, mode);
	sendJson(socket, saved ? 200 : 500, saved ? "{\"ok\":true}" : "{\"error\":\"save failed\"}");
}

void FffHttpServer::startSse(QTcpSocket *socket, const QString &token, bool overlay)
{
	auto it = m_conns.find(socket);
	if (it == m_conns.end())
		return;

	Conn &conn = it.value();
	conn.sse = true;
	conn.overlay = overlay;
	conn.token = token;
	conn.buffer.clear();

	QByteArray head = "HTTP/1.1 200 OK\r\n";
	head += "Content-Type: text/event-stream; charset=utf-8\r\n";
	head += "Cache-Control: no-store\r\n";
	head += "Connection: keep-alive\r\n";
	head += "X-Accel-Buffering: no\r\n";
	head += "\r\n";
	head += "retry: 2000\n\n";
	socket->write(head);

	const QByteArray state = overlay ? m_session->overlayStateJson()
					 : m_session->phoneStateJson(m_tokens.value(token));
	socket->write("data: " + state + "\n\n");
	socket->flush();

	emit clientsChanged();
}

void FffHttpServer::pushState()
{
	QByteArray overlayFrame;

	for (auto it = m_conns.begin(); it != m_conns.end(); ++it) {
		const Conn &conn = it.value();
		if (!conn.sse)
			continue;

		QByteArray frame;
		if (conn.overlay) {
			if (overlayFrame.isEmpty())
				overlayFrame = "data: " + m_session->overlayStateJson() + "\n\n";
			frame = overlayFrame;
		} else {
			frame = "data: " + m_session->phoneStateJson(m_tokens.value(conn.token)) + "\n\n";
		}

		it.key()->write(frame);
		it.key()->flush();
	}
}

void FffHttpServer::sendHeartbeat()
{
	for (auto it = m_conns.begin(); it != m_conns.end(); ++it) {
		if (!it.value().sse)
			continue;
		it.key()->write(": ping\n\n");
		it.key()->flush();
	}
}

void FffHttpServer::send(QTcpSocket *socket, int code, const QByteArray &contentType, const QByteArray &body,
			 const QByteArray &cacheControl)
{
	QByteArray response = "HTTP/1.1 " + QByteArray::number(code) + " " + reasonPhrase(code) + "\r\n";
	response += "Content-Type: " + contentType + "\r\n";
	response += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
	response += "Cache-Control: " + cacheControl + "\r\n";
	response += "Connection: close\r\n\r\n";
	response += body;

	socket->write(response);
	socket->flush();
	socket->disconnectFromHost();
}

void FffHttpServer::sendJson(QTcpSocket *socket, int code, const QByteArray &json)
{
	send(socket, code, "application/json; charset=utf-8", json);
}

void FffHttpServer::sendWebFile(QTcpSocket *socket, const QString &name, const QByteArray &contentType)
{
	if (!m_fileCache.contains(name)) {
		char *path = obs_module_file(QStringLiteral("web/%1").arg(name).toUtf8().constData());
		QFile file(QString::fromUtf8(path ? path : ""));
		bfree(path);

		if (!file.open(QIODevice::ReadOnly)) {
			obs_log(LOG_WARNING, "missing web asset %s", name.toUtf8().constData());
			send(socket, 404, "text/plain; charset=utf-8", "missing asset");
			return;
		}
		m_fileCache.insert(name, file.readAll());
	}

	send(socket, 200, contentType, m_fileCache.value(name));
}

void FffHttpServer::sendCard(QTcpSocket *socket, const QString &presidentId, const QString &kind)
{
	const FffPresident *president = m_session->presidentById(presidentId);
	const QString path = kind == QLatin1String("cover")
				     ? m_session->coverPath()
				     : (president ? m_session->assetPath(*president, kind) : QString());
	QFile file(path);
	if (path.isEmpty() || !file.open(QIODevice::ReadOnly)) {
		send(socket, 404, "text/plain; charset=utf-8", "no asset");
		return;
	}
	send(socket, 200, "image/png", file.readAll());
}
