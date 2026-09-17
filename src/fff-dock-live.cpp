/*
FFF Tools for OBS - dock live bar and live tab
Copyright (C) 2026 Student Union MFU <studentunion.developer@gmail.com>
GPL-2.0-or-later
*/

#include "fff-dock-live.h"
#include "fff-dock-ui.h"
#include "fff-http-server.h"
#include "fff-session.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStringList>
#include <QVBoxLayout>

FffLivePanel::FffLivePanel(FffSession *session, FffHttpServer *server, QWidget *parent)
	: QWidget(parent),
	  m_session(session),
	  m_server(server)
{
	auto *layout = new QVBoxLayout(this);
	layout->setContentsMargins(8, 8, 8, 6);

	m_banner = new QLabel(this);
	m_banner->setWordWrap(true);
	layout->addWidget(m_banner);

	m_summary = new QLabel(this);
	m_summary->setWordWrap(true);
	layout->addWidget(m_summary);

	auto *modes = new QHBoxLayout();
	m_scoreboard = new QPushButton(QStringLiteral("Show Status"), this);
	m_bottomBar = new QPushButton(QStringLiteral("BOTTOM BAR"), this);
	for (QPushButton *button : {m_scoreboard, m_bottomBar}) {
		button->setCheckable(true);
		button->setMinimumHeight(44);
		button->setStyleSheet(fffModeButtonStyle());
		modes->addWidget(button);
	}
	layout->addLayout(modes);

	auto *actions = new QHBoxLayout();
	m_hide = new QPushButton(QStringLiteral("■ ซ่อนจอ"), this);
	m_hide->setToolTip(QStringLiteral("เอาภาพลงจากจอสตรีม รอบและผลโหวตยังอยู่"));
	m_newRound = new QPushButton(QStringLiteral("↻ เริ่มรอบใหม่…"), this);
	m_newRound->setToolTip(QStringLiteral("ล้างผลโหวตทุกคนแล้วขึ้นรอบถัดไป รายชื่อ PNG และ PIN ยังอยู่ครบ"));
	for (QPushButton *button : {m_hide, m_newRound}) {
		button->setMinimumHeight(36);
		actions->addWidget(button);
	}
	layout->addLayout(actions);

	m_error = new QLabel(this);
	m_error->setWordWrap(true);
	m_error->setStyleSheet(QStringLiteral("color:%1;").arg(QLatin1String(FffColor::kError)));
	m_error->hide();
	layout->addWidget(m_error);

	connect(m_scoreboard, &QPushButton::clicked, this, [this]() { pressMode(QStringLiteral("scoreboard")); });
	connect(m_bottomBar, &QPushButton::clicked, this, [this]() { pressMode(QStringLiteral("bottomBar")); });
	connect(m_hide, &QPushButton::clicked, this, [this]() {
		if (!m_session->hideDisplay())
			showError(fffSaveErrorText());
		refresh();
	});
	connect(m_newRound, &QPushButton::clicked, this, [this]() { startNewRound(); });
	connect(m_session, &FffSession::changed, this, [this]() {
		showError(QString());
		refresh();
	});
	// Some session writes (a phone's vote, the monitor, a LAN monitor) never
	// touch this panel, so a failure there only reaches the operator here.
	connect(m_session, &FffSession::saveFailed, this, [this]() { showError(fffSaveErrorText()); });
	connect(m_server, &FffHttpServer::clientsChanged, this, [this]() { refresh(); });
	refresh();
}

void FffLivePanel::refresh()
{
	const bool listening = m_server->isListening();
	const bool revealed = m_session->phase() == FffPhase::Revealed;
	const bool bottomBar = m_session->displayMode() == QLatin1String("bottomBar");
	const int total = m_session->presidents().size();
	const int voted = m_session->votedCount();
	const bool ready = total > 0 && voted == total && !revealed;

	// A board "on air" that no overlay can hear is the worse surprise, so the
	// server state outranks the board state.
	const char *state = !listening ? "serverOff" : revealed ? "onAir" : "blank";
	const char *background = !listening ? FffColor::kAmber : revealed ? FffColor::kRed : FffColor::kGrey;
	const char *foreground = !listening ? "#0e1116" : "#ffffff";
	if (!listening)
		m_banner->setText(QStringLiteral("⚠ เซิร์ฟเวอร์ปิด — overlay/มือถือไม่อัปเดต"));
	else if (revealed)
		m_banner->setText(
			QStringLiteral("● ออกอากาศ · %1")
				.arg(bottomBar ? QStringLiteral("BOTTOM BAR") : QStringLiteral("Show Status")));
	else
		m_banner->setText(QStringLiteral("○ จอว่าง"));
	m_banner->setProperty("fffState", QString::fromLatin1(state));
	m_banner->setStyleSheet(QStringLiteral("QLabel { background: %1; color: %2; font-weight: 800; font-size: 15px;"
					       " padding: 8px 10px; border-radius: 6px; }")
					.arg(QLatin1String(background), QLatin1String(foreground)));

	if (ready)
		m_summary->setText(
			QStringLiteral("รอบ %1 · ✓ ครบ %2/%3 พร้อมขึ้นจอ").arg(m_session->round()).arg(voted).arg(total));
	else
		m_summary->setText(
			QStringLiteral("รอบ %1 · โหวตแล้ว %2/%3").arg(m_session->round()).arg(voted).arg(total));
	m_summary->setProperty("fffReady", ready);
	m_summary->setStyleSheet(
		ready ? QStringLiteral("color:%1;font-weight:700;").arg(QLatin1String(FffColor::kGreen)) : QString());

	m_scoreboard->setChecked(revealed && !bottomBar);
	m_bottomBar->setChecked(revealed && bottomBar);
	m_hide->setEnabled(revealed);
}

void FffLivePanel::showError(const QString &message)
{
	m_error->setText(message);
	m_error->setVisible(!message.isEmpty());
}

void FffLivePanel::pressMode(const QString &mode)
{
	// Pressing the mode already on air changes nothing. Blanking the stream is
	// its own button, so a double click can never take the board down.
	const bool onAir = m_session->phase() == FffPhase::Revealed && m_session->displayMode() == mode;
	if (!onAir && !m_session->showMode(mode))
		showError(fffSaveErrorText());
	refresh();
}

void FffLivePanel::startNewRound()
{
	const int round = m_session->round();
	const QString text = QStringLiteral("ล้างผลโหวตรอบ %1 (โหวตแล้ว %2 คน) แล้วขึ้นรอบ %3 จอสตรีมจะว่าง\n"
					    "รายชื่อ PNG และ PIN ยังอยู่ครบ")
				     .arg(round)
				     .arg(m_session->votedCount())
				     .arg(round + 1);
	if (!fffConfirm(this, QStringLiteral("เริ่มรอบใหม่"), text, QStringLiteral("เริ่มรอบใหม่")))
		return;
	if (!m_session->clearRound())
		showError(fffSaveErrorText());
	refresh();
}

FffLiveTab::FffLiveTab(FffSession *session, QWidget *parent) : QWidget(parent), m_session(session)
{
	auto *layout = new QVBoxLayout(this);
	m_votes = new QListWidget(this);
	layout->addWidget(m_votes, 1);

	auto *logoRow = new QHBoxLayout();
	logoRow->addWidget(new QLabel(QStringLiteral("โลโก้กลาง"), this));
	m_logo = new QComboBox(this);
	logoRow->addWidget(m_logo, 1);
	layout->addLayout(logoRow);

	connect(m_logo, &QComboBox::currentIndexChanged, this, [this](int) {
		if (!m_session->setLogoPresident(m_logo->currentData().toString())) {
			emit errorRaised(fffSaveErrorText());
			m_logoSignature.clear();
			refresh();
		}
	});
	connect(m_session, &FffSession::changed, this, [this]() { refresh(); });
	refresh();
}

void FffLiveTab::refresh()
{
	QStringList signature{m_session->logoPresidentId()};
	for (const FffPresident &president : m_session->presidents())
		signature << president.id << president.name << president.school;
	const QString joined = signature.join(QChar(0x1f));
	if (joined != m_logoSignature) {
		m_logoSignature = joined;
		const QSignalBlocker blocker(m_logo);
		m_logo->clear();
		m_logo->addItem(QStringLiteral("ไม่เลือกโลโก้"), QString());
		for (const FffPresident &president : m_session->presidents())
			m_logo->addItem(president.school.isEmpty() ? president.name : president.school, president.id);
		m_logo->setCurrentIndex(qMax(0, m_logo->findData(m_session->logoPresidentId())));
	}

	m_votes->clear();
	for (const FffPresident &president : m_session->presidents()) {
		const FffVote vote = m_session->voteOf(president.id);
		QString colour = QStringLiteral("ยังไม่กด");
		if (vote == FffVote::Red)
			colour = QStringLiteral("แดง");
		else if (vote == FffVote::Green)
			colour = QStringLiteral("เขียว");
		m_votes->addItem(QStringLiteral("%1 %2 · %3")
					 .arg(vote == FffVote::None ? QStringLiteral("○") : QStringLiteral("●"), colour,
					      president.name.isEmpty() ? president.school : president.name));
	}
}
