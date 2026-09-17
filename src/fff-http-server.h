/*
FFF Tools for OBS - embedded HTTP + SSE server
Copyright (C) 2026 Student Union MFU <studentunion.developer@gmail.com>
GPL-2.0-or-later
*/

#pragma once

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QThreadPool>
#include <QUrlQuery>

#include <optional>

class FffSession;
class QTcpServer;
class QTcpSocket;
class QTimer;

/*
 * A deliberately small HTTP/1.1 server: it serves the phone page, the overlay
 * page and the PNG card assets, takes votes over POST and pushes state to
 * every open page over Server-Sent Events.
 *
 * SSE rather than WebSocket because obs-deps does not ship qtwebsockets, and
 * because EventSource reconnects on its own - which is exactly what a phone on
 * event Wi-Fi needs.
 *
 * Everything runs on the OBS UI thread except shrinking oversized artwork,
 * which a single worker thread does ahead of time. Requests are a few hundred
 * bytes each and files are cached in memory, so nothing here blocks long enough
 * to matter.
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
	// Monitors opened from another machine with the LAN key.
	int remoteMonitorClientCount() const;

	static QStringList lanAddresses();

signals:
	void clientsChanged();

private:
	struct Conn {
		QByteArray buffer;
		bool sse = false;
		bool overlay = false;
		// Opened from another machine with the monitor key; cut when the
		// switch or the key changes.
		bool remoteMonitor = false;
		// A reply is already on its way, possibly after a delay. Anything else
		// the client sends is read and dropped so it cannot be answered twice.
		bool closing = false;
		QString token;
	};

	void acceptConnection();
	void readFrom(QTcpSocket *socket);
	void dropConnection(QTcpSocket *socket);

	void route(QTcpSocket *socket, const QByteArray &method, const QString &path, const QString &token,
		   const QByteArray &body);
	void handleAuth(QTcpSocket *socket, const QByteArray &body);
	void handleVote(QTcpSocket *socket, const QByteArray &body);
	void handleOperatorVote(QTcpSocket *socket, const QByteArray &body);
	void handleLayout(QTcpSocket *socket, const QByteArray &body);
	void handleLayer(QTcpSocket *socket, const QByteArray &body);
	void startSse(QTcpSocket *socket, const QString &token, bool overlay, bool remoteMonitor = false);
	void dropRemoteMonitors();
	void pushState();
	void sendHeartbeat();

	void send(QTcpSocket *socket, int code, const QByteArray &contentType, const QByteArray &body,
		  const QByteArray &cacheControl = "no-store", const QByteArray &extraHeaders = QByteArray());
	// Refuses a route readFrom() did not admit, with the text that route has
	// always answered. `delay` applies the wrong-PIN brake to a guessed key.
	void sendDenied(QTcpSocket *socket, const QString &path, bool delay);
	void sendJson(QTcpSocket *socket, int code, const QByteArray &json);
	void sendWebFile(QTcpSocket *socket, const QString &name, const QByteArray &contentType);
	void sendCard(QTcpSocket *socket, const QString &presidentId, const QString &kind = QStringLiteral("card"));
	// Bytes served for an asset path: its shrunk rendition when one exists,
	// else the file itself. Empty when neither can be read.
	std::optional<QByteArray> cardBytes(const QString &path);
	// Artwork larger than the stream is shrunk once, off the UI thread, as
	// soon as it enters the session; see fff-asset-rendition.h.
	void prepareRenditions();
	void ensureRendition(const QString &path);
	void renditionFinished(const QString &path, bool written);

	// Query of the request being routed; the upload reads presidentId and kind.
	QUrlQuery m_query;
	// True while routing a request from another machine that holds the monitor key.
	bool m_remoteRequest = false;
	FffSession *m_session = nullptr;
	QTcpServer *m_server = nullptr;
	QTimer *m_heartbeat = nullptr;
	quint16 m_boundPort = 0;
	QHash<QTcpSocket *, Conn> m_conns;
	QHash<QString, QString> m_tokens;
	QHash<QString, QByteArray> m_fileCache;
	QHash<QString, QByteArray> m_cardCache;
	// One job at a time: decoding a 4500x8000 PNG alone needs 144 MB.
	QThreadPool m_renditionPool;
	QSet<QString> m_renditionJobs;
	// Paths known to need no job: already small, already shrunk or unreadable.
	QSet<QString> m_renditionChecked;
	// Requests that arrived while their rendition was still being made.
	QHash<QString, QList<QPointer<QTcpSocket>>> m_waitingRenditions;
};
