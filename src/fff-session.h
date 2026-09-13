/*
FFF Tools for OBS - flag board session state
Copyright (C) 2026 Student Union MFU <studentunion.developer@gmail.com>
GPL-2.0-or-later
*/

#pragma once

#include <QByteArray>
#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QVector>

enum class FffPhase { Collecting, Revealed };

enum class FffVote { None, Red, Green };

struct FffPresident {
	QString id;
	QString name;
	QString school;
	QString card;
	QString pin;
};

// Centre and scale on the 1920x1080 canvas, used for both the legacy board
// and individual pieces. x/y are fractions of the canvas.
struct FffLayout {
	double x = 0.5;
	double y = 0.5;
	double scale = 1.0;
	// scale is retained for old saved sessions and the whole-board layout.
	// Piece overrides can stretch independently on each axis.
	double scaleX = 1.0;
	double scaleY = 1.0;
	// Cards may resize their red/green result backdrop without scaling the PNG.
	double resultScaleX = 1.0;
	double resultScaleY = 1.0;
};

/*
 * Holds every president in the session plus the votes of the current round.
 * Lives on the OBS UI thread; the HTTP server and the dock both talk to it
 * directly and react to changed().
 */
class FffSession : public QObject {
	Q_OBJECT

public:
	explicit FffSession(QObject *parent = nullptr);

	const QVector<FffPresident> &presidents() const { return m_presidents; }
	const FffPresident *presidentById(const QString &id) const;
	const FffPresident *presidentByPin(const QString &pin) const;
	int indexOf(const QString &id) const;

	void addPresident(const FffPresident &president);
	void updatePresident(const FffPresident &president);
	void removePresident(const QString &id);

	FffPhase phase() const { return m_phase; }
	int round() const { return m_round; }
	FffVote voteOf(const QString &id) const;
	int votedCount() const;

	bool setVote(const QString &presidentId, FffVote vote);
	void forceReveal();
	void clearRound();

	quint16 port() const { return m_port; }
	void setPort(quint16 port);

	FffLayout layout() const { return m_layout; }
	void setLayout(const FffLayout &layout);
	// "heading" or "card:<id>"; nullptr removes an override to use the grid.
	bool setPieceLayout(const QString &target, const FffLayout *layout);
	bool resetLayouts();
	static bool validCardTemplate(const QJsonObject &value);
	bool setCardTemplate(const QJsonObject &value);
	// Moves "heading" or "card:<id>" within the persisted stacking order.
	bool movePieceLayer(const QString &target, const QString &action);

	QString cardsDir() const;
	QString cardPath(const FffPresident &president) const;
	QString cardUrl(const FffPresident &president) const;
	QString importCard(const QString &sourcePath, const QString &presidentId);

	void load();
	bool save() const;

	QByteArray overlayStateJson() const;
	QByteArray phoneStateJson(const QString &presidentId) const;

	QString uniquePin() const;

	static QString newId();
	static QString voteName(FffVote vote);
	static FffVote voteFromName(const QString &name);

signals:
	void changed();

private:
	QString configDir() const;

	QVector<FffPresident> m_presidents;
	QHash<QString, FffVote> m_votes;
	FffPhase m_phase = FffPhase::Collecting;
	int m_round = 1;
	quint16 m_port = 9779;
	FffLayout m_layout;
	QHash<QString, FffLayout> m_pieceLayouts;
	QHash<QString, int> m_pieceLayers;
	QJsonObject m_cardTemplate;
};
