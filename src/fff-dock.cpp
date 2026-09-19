/*
FFF Tools for OBS - flag board control dock
Copyright (C) 2026 Student Union MFU <studentunion.developer@gmail.com>
GPL-2.0-or-later
*/

#include "fff-dock-live.h"
#include "fff-dock-roster.h"
#include "fff-dock-settings.h"
#include "fff-hotkeys.h"
#include "fff-http-server.h"
#include "fff-session.h"

#include <obs-module.h>
#include <obs-frontend-api.h>
#include <plugin-support.h>

#include <QFrame>
#include <QScrollArea>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QWidget>

namespace {

enum Tab { TabLive = 0, TabRoster = 1, TabSettings = 2 };

QScrollArea *scrollable(QWidget *content)
{
	auto *scroll = new QScrollArea();
	scroll->setWidgetResizable(true);
	scroll->setFrameShape(QFrame::NoFrame);
	scroll->setWidget(content);
	return scroll;
}

} // namespace

/*
 * Operator console. The live bar stays above the tabs so the buttons that
 * change the stream are never a scroll away; everything prepared before the
 * show sits in the tabs below. Nothing reaches the stream until a mode is
 * pressed.
 */
class FffDock : public QWidget {
public:
	FffDock();
	~FffDock() override;

private:
	void refreshAttention();
	// OBS does not announce that a binding changed, but it does announce that
	// it is saving, which is often enough and covers closing down.
	static void onFrontendSave(obs_data_t *save_data, bool saving, void *data);

	FffSession *m_session = nullptr;
	FffHttpServer *m_server = nullptr;
	QTabWidget *m_tabs = nullptr;
	FffSettingsTab *m_settings = nullptr;
	FffLivePanel *m_live = nullptr;
	FffHotkeys *m_hotkeys = nullptr;
};

FffDock::FffDock()
{
	m_session = new FffSession(this);
	m_session->load();
	m_server = new FffHttpServer(m_session, this);

	// Come up listening so the operator has one less thing to remember.
	QString startError;
	const bool started = m_server->start(m_session->port(), &startError);
	if (!started)
		obs_log(LOG_WARNING, "could not start flag board: %s", startError.toUtf8().constData());

	auto *root = new QVBoxLayout(this);
	root->setContentsMargins(0, 0, 0, 0);
	root->setSpacing(0);

	m_live = new FffLivePanel(m_session, m_server, this);
	root->addWidget(m_live);

	// The live bar's buttons, reachable from OBS > Settings > Hotkeys. A child
	// of this dock, so removing the dock unregisters them.
	m_hotkeys = new FffHotkeys(m_live, m_session->configDir(), this);
	obs_frontend_add_save_callback(onFrontendSave, this);

	auto *liveTab = new FffLiveTab(m_session);
	auto *roster = new FffRosterTab(m_session);
	m_settings = new FffSettingsTab(m_session, m_server);
	m_tabs = new QTabWidget(this);
	m_tabs->insertTab(TabLive, scrollable(liveTab), QStringLiteral("ไลฟ์"));
	m_tabs->insertTab(TabRoster, scrollable(roster), QStringLiteral("รายชื่อ"));
	m_tabs->insertTab(TabSettings, scrollable(m_settings), QStringLiteral("ตั้งค่า"));
	root->addWidget(m_tabs, 1);

	if (!started)
		m_settings->setStartError(QStringLiteral("เปิดพอร์ต %1 ไม่ได้: %2").arg(m_session->port()).arg(startError));

	connect(roster, &FffRosterTab::errorRaised, m_live, &FffLivePanel::showError);
	connect(m_settings, &FffSettingsTab::errorRaised, m_live, &FffLivePanel::showError);
	connect(m_session, &FffSession::changed, this, [this]() { refreshAttention(); });
	connect(m_server, &FffHttpServer::clientsChanged, this, [this]() { refreshAttention(); });

	// An empty roster is the one job left before anything can go on air.
	m_tabs->setCurrentIndex(m_session->presidents().isEmpty() ? TabRoster : TabLive);
	refreshAttention();
}

FffDock::~FffDock()
{
	// Before the children go, or a save could arrive at a half-torn dock.
	obs_frontend_remove_save_callback(onFrontendSave, this);
}

void FffDock::onFrontendSave(obs_data_t *, bool saving, void *data)
{
	auto *dock = static_cast<FffDock *>(data);
	// The scene collection this is saving holds nothing of ours; it is only
	// the moment that says "a good time to write".
	if (saving && dock->m_hotkeys)
		dock->m_hotkeys->saveBindings();
}

void FffDock::refreshAttention()
{
	m_tabs->setTabText(TabSettings,
			   m_settings->needsAttention() ? QStringLiteral("ตั้งค่า ⚠") : QStringLiteral("ตั้งค่า"));
}

extern "C" void fff_register_dock(void)
{
	auto *dock = new FffDock();
	if (!obs_frontend_add_dock_by_id("fff_dock", "FFF Flag Board", dock)) {
		obs_log(LOG_WARNING, "could not register dock");
		delete dock;
	}
}

extern "C" void fff_unregister_dock(void)
{
	obs_frontend_remove_dock("fff_dock");
}
