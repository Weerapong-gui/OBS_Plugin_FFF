/*
FFF Tools for OBS - dock roster tab
Copyright (C) 2026 Student Union MFU <studentunion.developer@gmail.com>
GPL-2.0-or-later
*/

#pragma once

#include <QHash>
#include <QString>
#include <QWidget>

class FffSession;
class QAction;
class QLabel;
class QPushButton;
class QTableWidget;
class QToolButton;

/*
 * Pre-show roster: names, schools, PINs and the artwork each president
 * carries. Row actions sit behind two menus so the table keeps the room, and
 * they stay disabled until a row is chosen instead of doing nothing silently.
 */
class FffRosterTab : public QWidget {
	Q_OBJECT

public:
	explicit FffRosterTab(FffSession *session, QWidget *parent = nullptr);

	// Rebuilds the table from the session, keeping the selected president.
	void refresh();
	QString selectedPresidentId() const;
	void selectPresident(const QString &id);

	QTableWidget *table() const { return m_table; }
	QPushButton *addButton() const { return m_add; }
	QToolButton *pngButton() const { return m_png; }
	QToolButton *moreButton() const { return m_more; }
	// kind is "card", "bottomBar" or "logo".
	QAction *chooseAction(const QString &kind) const { return m_choose.value(kind); }
	QAction *clearAction(const QString &kind) const { return m_clear.value(kind); }
	QAction *regeneratePinAction() const { return m_regeneratePin; }
	QAction *removeAction() const { return m_remove; }

signals:
	void errorRaised(const QString &message);

private:
	void updateActions();
	void addPresident();
	void chooseAsset(const QString &kind);
	void applyAsset(const QString &kind, const QString &stored);
	void regeneratePin();
	void removeSelected();

	FffSession *m_session = nullptr;
	QTableWidget *m_table = nullptr;
	QPushButton *m_add = nullptr;
	QToolButton *m_png = nullptr;
	QToolButton *m_more = nullptr;
	QLabel *m_hint = nullptr;
	QHash<QString, QAction *> m_choose;
	QHash<QString, QAction *> m_clear;
	QAction *m_regeneratePin = nullptr;
	QAction *m_remove = nullptr;
	bool m_updating = false;
};
