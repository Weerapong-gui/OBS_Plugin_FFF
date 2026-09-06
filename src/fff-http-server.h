/*
FFF Tools for OBS - embedded HTTP + SSE server
Copyright (C) 2026 Student Union MFU <studentunion.developer@gmail.com>
GPL-2.0-or-later
*/

#pragma once

#include <QByteArray>
#include <QHash>
#include <QObject>
#include <QString>
#include <QStringList>

class FffSession;
class QTcpServer;
class QTcpSocket;
class QTimer;

/*
 * A deliberately small HTTP/1.1 server: it serves the phone page, the overlay
 * page and the president photos, takes votes over POST and pushes state to
 * every open page over Server-Sent Events.
 *
 * SSE rather than WebSocket because obs-deps does not ship qtwebsockets, and
 * because EventSource reconnects on its own - which is exactly what a phone on
 * event Wi-Fi needs.
 *
 * Everything runs on the OBS UI thread. Requests are a few hundred bytes each
 * and files are cached in memory, so nothing here blocks long enough to matter.
 */
class FffHttpServer : public QObject {
	Q_OBJECT

public:
	explicit FffHttpServer(FffSession *session, QObject *parent = nullptr);
	~FffHttpServer() override;

	bool start(quint16 port, QString *error);
	void stop();
	bool isListening() const;
	quint16 boundPort() const { return m_boundPort; }

	int phoneClientCount() const;
	int overlayClientCount() const;

	static QStringList lanAddresses();

signals:
	void clientsChanged();

private:
	struct Conn {
		QByteArray buffer;
		bool sse = false;
		bool overlay = false;
		QString token;
	};

	void acceptConnection();
	void readFrom(QTcpSocket *socket);
	void dropConnection(QTcpSocket *socket);

	void route(QTcpSocket *socket, const QByteArray &method, const QString &path, const QString &token,
		   const QByteArray &body);
	void handleAuth(QTcpSocket *socket, const QByteArray &body);
	void handleVote(QTcpSocket *socket, const QByteArray &body);
	void startSse(QTcpSocket *socket, const QString &token, bool overlay);
	void pushState();
	void sendHeartbeat();

	void send(QTcpSocket *socket, int code, const QByteArray &contentType, const QByteArray &body,
		  const QByteArray &cacheControl = "no-store");
	void sendJson(QTcpSocket *socket, int code, const QByteArray &json);
	void sendWebFile(QTcpSocket *socket, const QString &name, const QByteArray &contentType);
	void sendPhoto(QTcpSocket *socket, const QString &presidentId);

	FffSession *m_session = nullptr;
	QTcpServer *m_server = nullptr;
	QTimer *m_heartbeat = nullptr;
	quint16 m_boundPort = 0;
	QHash<QTcpSocket *, Conn> m_conns;
	QHash<QString, QString> m_tokens;
	QHash<QString, QByteArray> m_fileCache;
	QHash<QString, QByteArray> m_photoCache;
};
