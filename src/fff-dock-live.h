/*
FFF Tools for OBS - dock live bar and live tab
Copyright (C) 2026 Student Union MFU <studentunion.developer@gmail.com>
GPL-2.0-or-later
*/

#pragma once

#include <QString>
#include <QWidget>

class FffHttpServer;
class FffSession;
class QComboBox;
class QLabel;
class QListWidget;
class QPushButton;

/*
 * The part of the dock that never scrolls away: what the stream shows right
 * now and the buttons that change it. Showing and hiding act at once; only
 * the button that throws votes away asks first.
 */
class FffLivePanel : public QWidget {
public:
	FffLivePanel(FffSession *session, FffHttpServer *server, QWidget *parent = nullptr);

	void refresh();
	// An empty message hides the line.
	void showError(const QString &message);

	QLabel *banner() const { return m_banner; }
	QLabel *summary() const { return m_summary; }
	QLabel *error() const { return m_error; }
	QPushButton *scoreboardButton() const { return m_scoreboard; }
	QPushButton *bottomBarButton() const { return m_bottomBar; }
	QPushButton *hideButton() const { return m_hide; }
	QPushButton *newRoundButton() const { return m_newRound; }

private:
	void pressMode(const QString &mode);
	void startNewRound();

	FffSession *m_session = nullptr;
	FffHttpServer *m_server = nullptr;
	QLabel *m_banner = nullptr;
	QLabel *m_summary = nullptr;
	QLabel *m_error = nullptr;
	QPushButton *m_scoreboard = nullptr;
	QPushButton *m_bottomBar = nullptr;
	QPushButton *m_hide = nullptr;
	QPushButton *m_newRound = nullptr;
};

/* Who has voted this round, and which school's logo sits in the Bottom Bar. */
class FffLiveTab : public QWidget {
	Q_OBJECT

public:
	explicit FffLiveTab(FffSession *session, QWidget *parent = nullptr);

	void refresh();

	QListWidget *votes() const { return m_votes; }
	QComboBox *logo() const { return m_logo; }

signals:
	void errorRaised(const QString &message);

private:
	FffSession *m_session = nullptr;
	QListWidget *m_votes = nullptr;
	QComboBox *m_logo = nullptr;
	// Rebuilding the list closes an open popup, so only a roster or logo
	// change rebuilds it, never an incoming vote.
	QString m_logoSignature;
};
