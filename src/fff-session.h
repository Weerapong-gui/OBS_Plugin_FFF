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
#include <QStringList>
#include <QVector>

enum class FffPhase { Collecting, Revealed };

enum class FffVote { None, Red, Green };

// What the scoreboard says about a president. Deliberately its own axis: the
// flags stay a separate thing that drives the bottom bar.
enum class FffStatus { Waiting, Unqualified, Qualified };

struct FffPresident {
	QString id;
	QString name;
	QString school;
	QString card;
	QString pin;
	QString bottomBar;
	QString logo;
	// Round 2 of the centre logo; `logo` is round 1.
	QString logo2;
	// One finished PNG per status. A status with no artwork falls back to the
	// card, so a roster built before these existed still goes on air.
	QString qualified;
	QString unqualified;
	QString waiting;
	FffStatus status = FffStatus::Waiting;
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
	// Negative means inherit the template opacity.
	double imageOpacity = -1.0;
	double resultOpacity = -1.0;
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

	bool addPresident(const FffPresident &president);
	bool updatePresident(const FffPresident &president);
	bool removePresident(const QString &id);

	FffPhase phase() const { return m_phase; }
	int round() const { return m_round; }
	FffVote voteOf(const QString &id) const;
	int votedCount() const;

	bool setVote(const QString &presidentId, FffVote vote);
	bool forceReveal();
	bool clearRound();

	quint16 port() const { return m_port; }
	bool setPort(quint16 port);

	bool monitorLanEnabled() const { return m_monitorLanEnabled; }
	QString monitorKey() const { return m_monitorKey; }
	// Switching LAN access on mints the key the first time. The key outlives
	// switching off, so only regenerateMonitorKey() retires a shared link.
	bool setMonitorLanEnabled(bool enabled);
	bool regenerateMonitorKey();

	FffLayout layout(const QString &mode = QStringLiteral("scoreboard")) const
	{
		return mode == QLatin1String("bottomBar") ? m_bottomLayout : m_layout;
	}
	bool setLayout(const FffLayout &layout, const QString &mode = QStringLiteral("scoreboard"));
	// "heading" or "card:<id>"; nullptr removes an override to use the grid.
	bool setPieceLayout(const QString &target, const FffLayout *layout,
			    const QString &mode = QStringLiteral("scoreboard"));
	bool resetLayouts(const QString &mode = QStringLiteral("scoreboard"));
	static bool validTemplateBox(const QJsonObject &layer);
	static bool validCardTemplate(const QJsonObject &value);
	static bool validLogoTemplate(const QJsonObject &value);
	static bool validCountTemplate(const QJsonObject &value);
	// The title above the Show Status stack: its box, its words and its type.
	static bool validHeadingTemplate(const QJsonObject &value);
	bool setHeadingTemplate(const QJsonObject &value);
	// The /score page's own title and flag counters. They describe the same two
	// shapes the Show Status title and the Bottom Bar counters do, so they reuse
	// those validators rather than inventing a third and a fourth.
	bool setScoreHeadingTemplate(const QJsonObject &value);
	bool setScoreCountTemplate(const QJsonObject &value);
	// A locally installed font family name, or empty for the page's own stack.
	static bool validFontFamily(const QString &family);
	static bool validHexColor(const QString &color);
	// How long each animation runs, in milliseconds. Grouped by the family of
	// motion it belongs to; the web pages carry the defaults, so anything this
	// object leaves out simply keeps its default.
	static bool validTiming(const QJsonObject &value);
	bool setTiming(const QJsonObject &value);
	bool setCardTemplate(const QJsonObject &value, const QString &mode = QStringLiteral("scoreboard"));
	// "logo" or "count"; both exist only in Bottom Bar.
	bool setBottomTemplate(const QString &piece, const QJsonObject &value);
	// "heading"/"logo", "cover", "count:red"/"count:green" or "card:<id>".
	bool validPieceTarget(const QString &mode, const QString &target) const;
	// Moves "heading" or "card:<id>" within the persisted stacking order.
	bool movePieceLayer(const QString &target, const QString &action,
			    const QString &mode = QStringLiteral("scoreboard"));

	static bool validMode(const QString &mode);
	// Which boards POST /api/template may describe. "score" is a page, not a
	// board that goes on air, so it belongs here and never in validMode().
	static bool validTemplateMode(const QString &mode);
	bool showMode(const QString &mode);
	// Take the stream back to blank without ending the round: the operator
	// can put the same votes back up a moment later.
	bool hideDisplay();
	bool setLogoPresident(const QString &id);
	// Which set of centre-logo artwork goes on air: 1 or 2. Independent of
	// the vote round.
	int logoRound() const { return m_logoRound; }
	bool setLogoRound(int round);
	QString displayMode() const { return m_displayMode; }
	QString logoPresidentId() const { return m_logoPresidentId; }
	QString importAsset(const QString &sourcePath, const QString &presidentId, const QString &kind);
	QString assetPath(const FffPresident &president, const QString &kind) const;
	QString assetUrl(const FffPresident &president, const QString &kind) const;
	QString coverPath() const;
	QString coverUrl() const;
	// Every asset file the session currently points at, so a cache can drop
	// only the entries a roster or artwork change actually orphaned.
	QStringList assetPaths() const;
	QString cover() const { return m_cover; }
	bool setCover(const QString &fileName);
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
	static QString statusName(FffStatus status);
	static FffStatus statusFromName(const QString &name);
	static bool validStatusName(const QString &name);
	bool setStatus(const QString &presidentId, FffStatus status);
	// The artwork for a president's current status, or the card behind it.
	QString statusUrl(const FffPresident &president) const;
	// Writes PNG bytes into the cards directory and returns the stored name.
	// Shared by the dock's file picker and the monitor's upload.
	QString storeAsset(const QByteArray &png, const QString &kind);
	// Where this plugin keeps everything of its own on this machine. Public
	// because the hotkey bindings live beside session.json and there must be
	// exactly one answer to where that is.
	QString configDir() const;

signals:
	void changed();
	void saveFailed();
	// The LAN switch or key changed: connections admitted under the old
	// rules have to go.
	void monitorAccessChanged();

private:
	// Copies the session this plugin ships with into place, once, on a machine
	// that has none of its own.
	void seedFromBundle() const;
	QJsonObject bottomBarJson() const;
	FffLayout m_bottomLayout;
	QJsonObject m_bottomLogoTemplate;
	QJsonObject m_bottomCountTemplate;
	QHash<QString, FffLayout> m_bottomPieces;
	QHash<QString, int> m_bottomLayers;
	QJsonObject m_bottomTemplate;
	QString m_displayMode = QStringLiteral("scoreboard");
	QString m_logoPresidentId;
	int m_logoRound = 1;
	QString m_cover;

	QVector<FffPresident> m_presidents;
	QHash<QString, FffVote> m_votes;
	FffPhase m_phase = FffPhase::Collecting;
	int m_round = 1;
	quint16 m_port = 9779;
	bool m_monitorLanEnabled = false;
	QString m_monitorKey;
	FffLayout m_layout;
	QHash<QString, FffLayout> m_pieceLayouts;
	QHash<QString, int> m_pieceLayers;
	QJsonObject m_cardTemplate;
	QJsonObject m_headingTemplate;
	QJsonObject m_scoreHeadingTemplate;
	QJsonObject m_scoreCountTemplate;
	QJsonObject m_timing;
};
