/*
FFF Tools for OBS - dock settings tab
Copyright (C) 2026 Student Union MFU <studentunion.developer@gmail.com>
GPL-2.0-or-later
*/

#pragma once

#include <QList>
#include <QPair>
#include <QString>
#include <QStringList>
#include <QWidget>

class FffHttpServer;
class FffSession;
class QCheckBox;
class QComboBox;
class QLabel;
class QPushButton;
class QSpinBox;
class QVBoxLayout;

/*
 * Things set once before the show: the server and its links, LAN access to
 * the monitor, the Bottom Bar cover and layout resets.
 */
class FffSettingsTab : public QWidget {
	Q_OBJECT

public:
	FffSettingsTab(FffSession *session, FffHttpServer *server, QWidget *parent = nullptr);

	void refresh();
	// The server is down or the machine has no LAN address; the dock marks the tab.
	bool needsAttention() const;
	// Reported by FffDock when the server could not start at OBS launch; shown
	// in place of the generic "not started" status until the next start.
	void setStartError(const QString &message);

	QSpinBox *portField() const { return m_port; }
	QPushButton *serverButton() const { return m_serverButton; }
	QLabel *serverStatus() const { return m_serverStatus; }
	QCheckBox *lanToggle() const { return m_lanToggle; }
	QWidget *lanDetails() const { return m_lanDetails; }
	QPushButton *regenerateKeyButton() const { return m_regenerateKey; }
	// Monitor links currently offered, one per LAN address.
	QStringList lanLinks() const { return m_lanLinks.urls; }

signals:
	void errorRaised(const QString &message);

private:
	struct LinkList {
		QWidget *container = nullptr;
		QVBoxLayout *layout = nullptr;
		QString signature;
		QStringList urls;
	};

	void toggleServer();
	void setLanEnabled(bool enabled);
	void regenerateKey();
	void chooseCover();
	void resetLayouts();
	// Rebuilds a list of copyable links only when its content changed, so a
	// phone connecting does not reset a "copied" button under the pointer.
	void setLinks(LinkList &list, const QList<QPair<QString, QString>> &links, const QString &note);

	FffSession *m_session = nullptr;
	FffHttpServer *m_server = nullptr;
	QSpinBox *m_port = nullptr;
	QPushButton *m_serverButton = nullptr;
	QLabel *m_serverStatus = nullptr;
	QString m_startError;
	LinkList m_serverLinks;
	QCheckBox *m_lanToggle = nullptr;
	QWidget *m_lanDetails = nullptr;
	LinkList m_lanLinks;
	QLabel *m_lanStatus = nullptr;
	QPushButton *m_regenerateKey = nullptr;
	QLabel *m_coverStatus = nullptr;
	QComboBox *m_resetMode = nullptr;
};
