/*
FFF Tools for OBS - dock settings tab
Copyright (C) 2026 Student Union MFU <studentunion.developer@gmail.com>
GPL-2.0-or-later
*/

#include "fff-dock-settings.h"
#include "fff-dock-ui.h"
#include "fff-http-server.h"
#include "fff-session.h"

#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QFileDialog>
#include <QGroupBox>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>

namespace {

const QString &noLanText()
{
	static const QString text = QStringLiteral("ยังไม่เจอ IP วง LAN · เช็คว่าต่อ Wi-Fi หรือ router แล้วหรือยัง");
	return text;
}

} // namespace

FffSettingsTab::FffSettingsTab(FffSession *session, FffHttpServer *server, QWidget *parent)
	: QWidget(parent),
	  m_session(session),
	  m_server(server)
{
	auto *layout = new QVBoxLayout(this);

	auto *serverBox = new QGroupBox(QStringLiteral("เซิร์ฟเวอร์"), this);
	auto *serverLayout = new QVBoxLayout(serverBox);
	auto *portRow = new QHBoxLayout();
	portRow->addWidget(new QLabel(QStringLiteral("พอร์ต"), serverBox));
	m_port = new QSpinBox(serverBox);
	m_port->setRange(1024, 65535);
	m_port->setValue(m_session->port());
	portRow->addWidget(m_port);
	m_serverButton = new QPushButton(serverBox);
	portRow->addWidget(m_serverButton);
	serverLayout->addLayout(portRow);
	m_serverStatus = new QLabel(serverBox);
	m_serverStatus->setWordWrap(true);
	serverLayout->addWidget(m_serverStatus);
	m_serverLinks.container = new QWidget(serverBox);
	m_serverLinks.layout = new QVBoxLayout(m_serverLinks.container);
	m_serverLinks.layout->setContentsMargins(0, 0, 0, 0);
	serverLayout->addWidget(m_serverLinks.container);
	layout->addWidget(serverBox);

	auto *lanBox = new QGroupBox(QStringLiteral("Monitor LAN"), this);
	auto *lanLayout = new QVBoxLayout(lanBox);
	m_lanToggle = new QCheckBox(QStringLiteral("อนุญาตเครื่องอื่นใน LAN เปิด monitor"), lanBox);
	lanLayout->addWidget(m_lanToggle);
	m_lanDetails = new QWidget(lanBox);
	auto *details = new QVBoxLayout(m_lanDetails);
	details->setContentsMargins(0, 0, 0, 0);
	m_lanLinks.container = new QWidget(m_lanDetails);
	m_lanLinks.layout = new QVBoxLayout(m_lanLinks.container);
	m_lanLinks.layout->setContentsMargins(0, 0, 0, 0);
	details->addWidget(m_lanLinks.container);
	auto *keyRow = new QHBoxLayout();
	m_lanStatus = new QLabel(m_lanDetails);
	keyRow->addWidget(m_lanStatus, 1);
	m_regenerateKey = new QPushButton(QStringLiteral("สุ่มกุญแจใหม่…"), m_lanDetails);
	keyRow->addWidget(m_regenerateKey);
	details->addLayout(keyRow);
	auto *warning = new QLabel(QStringLiteral("ลิงก์นี้ให้สิทธิ์เห็นผลก่อนเฉลยและแก้ธงได้ ส่งให้เฉพาะคนจัดเลย์เอาต์\n"
						  "ไม่มีการเข้ารหัส ใช้ใน Wi-Fi ของทีมเท่านั้น"),
				   m_lanDetails);
	warning->setWordWrap(true);
	warning->setStyleSheet(QStringLiteral("color:%1;").arg(QLatin1String(FffColor::kAmber)));
	details->addWidget(warning);
	lanLayout->addWidget(m_lanDetails);
	layout->addWidget(lanBox);

	auto *coverBox = new QGroupBox(QStringLiteral("Cover PNG"), this);
	auto *coverLayout = new QHBoxLayout(coverBox);
	m_coverStatus = new QLabel(coverBox);
	coverLayout->addWidget(m_coverStatus, 1);
	auto *coverButton = new QPushButton(QStringLiteral("เลือก Cover PNG…"), coverBox);
	coverButton->setToolTip(QStringLiteral("ภาพส่วนกลาง BOTTOM BAR แนะนำ PNG โปร่งใส 1920×1080"));
	coverLayout->addWidget(coverButton);
	auto *removeCoverButton = new QPushButton(QStringLiteral("ลบ Cover"), coverBox);
	coverLayout->addWidget(removeCoverButton);
	layout->addWidget(coverBox);

	auto *resetBox = new QGroupBox(QStringLiteral("ตำแหน่ง"), this);
	auto *resetLayout = new QVBoxLayout(resetBox);
	auto *hint = new QLabel(QStringLiteral("ลากและปรับขนาดในหน้า monitor โดยไม่สลับภาพออกอากาศ"), resetBox);
	hint->setWordWrap(true);
	resetLayout->addWidget(hint);
	auto *resetRow = new QHBoxLayout();
	m_resetMode = new QComboBox(resetBox);
	m_resetMode->addItem(QStringLiteral("Show Status"), QStringLiteral("scoreboard"));
	m_resetMode->addItem(QStringLiteral("BOTTOM BAR"), QStringLiteral("bottomBar"));
	resetRow->addWidget(m_resetMode, 1);
	auto *resetButton = new QPushButton(QStringLiteral("รีเซ็ตตำแหน่งโหมดที่เลือก…"), resetBox);
	resetRow->addWidget(resetButton);
	resetLayout->addLayout(resetRow);
	layout->addWidget(resetBox);
	layout->addStretch();

	connect(m_serverButton, &QPushButton::clicked, this, [this]() { toggleServer(); });
	connect(m_lanToggle, &QCheckBox::toggled, this, [this](bool enabled) { setLanEnabled(enabled); });
	connect(m_regenerateKey, &QPushButton::clicked, this, [this]() { regenerateKey(); });
	connect(coverButton, &QPushButton::clicked, this, [this]() { chooseCover(); });
	connect(removeCoverButton, &QPushButton::clicked, this, [this]() {
		if (!m_session->setCover(QString()))
			emit errorRaised(fffSaveErrorText());
	});
	connect(resetButton, &QPushButton::clicked, this, [this]() { resetLayouts(); });
	connect(m_session, &FffSession::changed, this, [this]() { refresh(); });
	connect(m_server, &FffHttpServer::clientsChanged, this, [this]() { refresh(); });
	refresh();
}

bool FffSettingsTab::needsAttention() const
{
	return !m_server->isListening() || FffHttpServer::lanAddresses().isEmpty();
}

void FffSettingsTab::setStartError(const QString &message)
{
	m_startError = message;
	refresh();
}

void FffSettingsTab::refresh()
{
	const bool listening = m_server->isListening();
	const quint16 port = m_server->boundPort();
	const QStringList addresses = FffHttpServer::lanAddresses();

	m_serverButton->setText(listening ? QStringLiteral("หยุด") : QStringLiteral("เริ่ม"));
	m_port->setEnabled(!listening);
	if (listening)
		m_serverStatus->setText(QStringLiteral("กำลังฟังพอร์ต %1 · มือถือ %2 · จอในเครื่องนี้ %3 · monitor LAN %4")
						.arg(port)
						.arg(m_server->phoneClientCount())
						.arg(m_server->overlayClientCount())
						.arg(m_server->remoteMonitorClientCount()));
	else
		m_serverStatus->setText(m_startError.isEmpty() ? QStringLiteral("ยังไม่ได้เปิดเซิร์ฟเวอร์") : m_startError);

	QList<QPair<QString, QString>> serverLinks;
	QString serverNote;
	if (listening) {
		serverLinks.append(
			{QStringLiteral("Browser Source"), QStringLiteral("http://127.0.0.1:%1/overlay").arg(port)});
		serverLinks.append(
			{QStringLiteral("จอมอนิเตอร์"), QStringLiteral("http://127.0.0.1:%1/monitor").arg(port)});
		serverLinks.append({QStringLiteral("Score (Browser Source)"),
				    QStringLiteral("http://127.0.0.1:%1/score").arg(port)});
		for (const QString &address : addresses)
			serverLinks.append(
				{QStringLiteral("มือถือ"), QStringLiteral("http://%1:%2").arg(address).arg(port)});
		if (addresses.isEmpty())
			serverNote = noLanText();
	}
	setLinks(m_serverLinks, serverLinks, serverNote);

	const bool lan = m_session->monitorLanEnabled();
	{
		const QSignalBlocker blocker(m_lanToggle);
		m_lanToggle->setChecked(lan);
	}
	m_lanDetails->setVisible(lan);
	QList<QPair<QString, QString>> lanLinks;
	QString lanNote;
	if (lan && !listening) {
		lanNote = QStringLiteral("เปิดเซิร์ฟเวอร์ก่อน");
	} else if (lan && addresses.isEmpty()) {
		lanNote = noLanText();
	} else if (lan) {
		for (const QString &address : addresses) {
			lanLinks.append({QStringLiteral("Monitor"), QStringLiteral("http://%1:%2/monitor?key=%3")
									    .arg(address)
									    .arg(port)
									    .arg(m_session->monitorKey())});
			lanLinks.append({QStringLiteral("Score"), QStringLiteral("http://%1:%2/score?key=%3")
									  .arg(address)
									  .arg(port)
									  .arg(m_session->monitorKey())});
		}
	}
	setLinks(m_lanLinks, lanLinks, lanNote);
	m_lanStatus->setText(QStringLiteral("เชื่อมต่ออยู่: monitor LAN %1").arg(m_server->remoteMonitorClientCount()));

	m_coverStatus->setText(m_session->cover().isEmpty() ? QStringLiteral("ยังไม่มี Cover")
							    : QStringLiteral("มี Cover"));
}

void FffSettingsTab::setLinks(LinkList &list, const QList<QPair<QString, QString>> &links, const QString &note)
{
	QStringList parts{note};
	for (const auto &link : links)
		parts << link.first << link.second;
	const QString signature = parts.join(QLatin1Char('\n'));
	if (signature == list.signature)
		return;
	list.signature = signature;
	list.urls.clear();

	while (QLayoutItem *item = list.layout->takeAt(0)) {
		if (QLayout *child = item->layout()) {
			while (QLayoutItem *inner = child->takeAt(0)) {
				delete inner->widget();
				delete inner;
			}
		}
		delete item->widget();
		delete item;
	}

	for (const auto &link : links) {
		list.urls.append(link.second);
		auto *row = new QHBoxLayout();
		row->addWidget(new QLabel(link.first, list.container));
		auto *field = new QLineEdit(link.second, list.container);
		field->setReadOnly(true);
		field->setCursorPosition(0);
		row->addWidget(field, 1);
		auto *copy = new QPushButton(QStringLiteral("คัดลอก"), list.container);
		row->addWidget(copy);
		const QString url = link.second;
		connect(copy, &QPushButton::clicked, copy, [copy, url]() {
			QGuiApplication::clipboard()->setText(url);
			copy->setText(QStringLiteral("คัดลอกแล้ว"));
			QTimer::singleShot(1500, copy, [copy]() { copy->setText(QStringLiteral("คัดลอก")); });
		});
		list.layout->addLayout(row);
	}
	if (!note.isEmpty()) {
		auto *label = new QLabel(note, list.container);
		label->setWordWrap(true);
		list.layout->addWidget(label);
	}
}

void FffSettingsTab::toggleServer()
{
	if (m_server->isListening()) {
		const int phones = m_server->phoneClientCount();
		const int screens = m_server->overlayClientCount();
		const int monitors = m_server->remoteMonitorClientCount();
		if (phones + screens + monitors > 0 &&
		    !fffConfirm(this, QStringLiteral("หยุดเซิร์ฟเวอร์"),
				QStringLiteral("มือถือ %1 เครื่อง จอ %2 และ monitor LAN %3 จะหลุด overlay จะไม่อัปเดต")
					.arg(phones)
					.arg(screens)
					.arg(monitors),
				QStringLiteral("หยุดเซิร์ฟเวอร์")))
			return;
		m_server->stop();
		refresh();
		return;
	}

	const quint16 port = static_cast<quint16>(m_port->value());
	if (!m_session->setPort(port)) {
		emit errorRaised(fffSaveErrorText());
		m_port->setValue(m_session->port());
		return;
	}
	QString error;
	if (m_server->start(port, &error))
		m_startError.clear();
	else
		m_startError = QStringLiteral("เปิดพอร์ต %1 ไม่ได้: %2").arg(port).arg(error);
	refresh();
}

void FffSettingsTab::setLanEnabled(bool enabled)
{
	if (!m_session->setMonitorLanEnabled(enabled)) {
		emit errorRaised(fffSaveErrorText());
		const QSignalBlocker blocker(m_lanToggle);
		m_lanToggle->setChecked(m_session->monitorLanEnabled());
	}
	refresh();
}

void FffSettingsTab::regenerateKey()
{
	const QString text = QStringLiteral("เครื่อง LAN ที่เปิด monitor อยู่ %1 เครื่องจะถูกตัด ต้องส่งลิงก์ใหม่ให้")
				     .arg(m_server->remoteMonitorClientCount());
	if (!fffConfirm(this, QStringLiteral("สุ่มกุญแจ monitor ใหม่"), text, QStringLiteral("สุ่มกุญแจใหม่")))
		return;
	if (!m_session->regenerateMonitorKey())
		emit errorRaised(fffSaveErrorText());
	refresh();
}

void FffSettingsTab::chooseCover()
{
	const QString file = QFileDialog::getOpenFileName(this, QStringLiteral("เลือก Cover PNG · แนะนำ 1920×1080"),
							  QString(), QStringLiteral("การ์ด PNG (*.png)"));
	if (file.isEmpty())
		return;
	const QString stored = m_session->importAsset(file, QString(), QStringLiteral("cover"));
	if (stored.isEmpty()) {
		emit errorRaised(QStringLiteral("นำเข้า PNG ไม่สำเร็จ รูปเดิมยังอยู่"));
		return;
	}
	if (!m_session->setCover(stored))
		emit errorRaised(fffSaveErrorText());
}

void FffSettingsTab::resetLayouts()
{
	const QString text = QStringLiteral("คืนตำแหน่งทุกชิ้นของโหมด %1 เป็นค่าเริ่มต้น?").arg(m_resetMode->currentText());
	if (!fffConfirm(this, QStringLiteral("รีเซ็ตตำแหน่ง"), text, QStringLiteral("รีเซ็ต")))
		return;
	if (!m_session->resetLayouts(m_resetMode->currentData().toString()))
		emit errorRaised(fffSaveErrorText());
}
