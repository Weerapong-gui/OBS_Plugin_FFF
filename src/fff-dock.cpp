/*
FFF Tools for OBS - flag board control dock
Copyright (C) 2026 Student Union MFU <studentunion.developer@gmail.com>
GPL-2.0-or-later
*/

#include "fff-http-server.h"
#include "fff-session.h"

#include <obs-module.h>
#include <obs-frontend-api.h>
#include <plugin-support.h>

#include <QClipboard>
#include <QComboBox>
#include <QSignalBlocker>
#include <QFileDialog>
#include <QGroupBox>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

namespace {

enum Column { ColName = 0, ColSchool = 1, ColPin = 2, ColCard = 3, ColBottomBar = 4, ColLogo = 5 };

constexpr int kVisibleRows = 6;

} // namespace

/*
 * Operator console: set the presidents up before the event, then drive the
 * rounds from the buttons at the bottom. Nothing reaches the stream until
 * a display mode button is pressed.
 */
class FffDock : public QWidget {
public:
	FffDock();

private:
	void buildUi();
	void refreshServer();
	void refreshTable();
	void refreshLive();

	void addUrlRow(const QString &label, const QString &url);
	QString selectedPresidentId() const;
	void toggleServer();
	void addPresident();
	void removeSelected();
	void chooseCard(const QString &kind);
	void showSaveError();
	void regeneratePin();

	FffSession *m_session = nullptr;
	FffHttpServer *m_server = nullptr;

	QSpinBox *m_port = nullptr;
	QPushButton *m_serverButton = nullptr;
	QLabel *m_serverStatus = nullptr;
	QWidget *m_urls = nullptr;
	QVBoxLayout *m_urlLayout = nullptr;

	QTableWidget *m_table = nullptr;
	QLabel *m_summary = nullptr;
	QLabel *m_layoutLabel = nullptr;
	QListWidget *m_live = nullptr;
	QPushButton *m_revealButton = nullptr;
	QPushButton *m_bottomBarButton = nullptr;
	QComboBox *m_logoPresident = nullptr;
	QComboBox *m_resetMode = nullptr;
	QLabel *m_saveError = nullptr;

	bool m_updating = false;
};

FffDock::FffDock()
{
	m_session = new FffSession(this);
	m_session->load();
	m_server = new FffHttpServer(m_session, this);

	buildUi();

	connect(m_session, &FffSession::changed, this, [this]() {
		m_saveError->clear();
		refreshLive();
	});
	connect(m_server, &FffHttpServer::clientsChanged, this, [this]() {
		refreshServer();
		refreshLive();
	});

	refreshTable();

	// Come up listening so the operator has one less thing to remember.
	QString error;
	if (!m_server->start(m_session->port(), &error))
		obs_log(LOG_WARNING, "could not start flag board: %s", error.toUtf8().constData());
	refreshServer();
	refreshLive();
}

void FffDock::buildUi()
{
	// The roster table wants to be tall; a scroll area keeps it from
	// squeezing the panels above and below it in a narrow dock.
	auto *outer = new QVBoxLayout(this);
	outer->setContentsMargins(0, 0, 0, 0);
	auto *scroll = new QScrollArea(this);
	scroll->setWidgetResizable(true);
	scroll->setFrameShape(QFrame::NoFrame);
	outer->addWidget(scroll);

	auto *page = new QWidget(scroll);
	scroll->setWidget(page);
	auto *root = new QVBoxLayout(page);

	auto *serverBox = new QGroupBox(QStringLiteral("เซิร์ฟเวอร์"), page);
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

	m_urls = new QWidget(serverBox);
	m_urlLayout = new QVBoxLayout(m_urls);
	m_urlLayout->setContentsMargins(0, 0, 0, 0);
	serverLayout->addWidget(m_urls);

	root->addWidget(serverBox);

	auto *rosterBox = new QGroupBox(QStringLiteral("รายชื่อนายก"), page);
	auto *rosterLayout = new QVBoxLayout(rosterBox);

	m_table = new QTableWidget(0, 6, rosterBox);
	m_table->setHorizontalHeaderLabels({QStringLiteral("ชื่อนายก"), QStringLiteral("สำนักวิชา"), QStringLiteral("PIN"),
					    QStringLiteral("SCOREBOARD PNG"), QStringLiteral("BOTTOM BAR PNG"),
					    QStringLiteral("โลโก้ PNG")});
	m_table->horizontalHeader()->setSectionResizeMode(ColName, QHeaderView::Stretch);
	m_table->horizontalHeader()->setSectionResizeMode(ColSchool, QHeaderView::Stretch);
	m_table->verticalHeader()->setVisible(false);
	m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
	m_table->setSelectionMode(QAbstractItemView::SingleSelection);
	m_table->setMinimumHeight(m_table->verticalHeader()->defaultSectionSize() * kVisibleRows +
				  m_table->horizontalHeader()->sizeHint().height() + 4);
	rosterLayout->addWidget(m_table);

	auto *rosterButtons = new QHBoxLayout();
	auto *addButton = new QPushButton(QStringLiteral("เพิ่ม"), rosterBox);
	auto *removeButton = new QPushButton(QStringLiteral("ลบ"), rosterBox);
	auto *cardButton = new QPushButton(QStringLiteral("เลือกการ์ด PNG"), rosterBox);
	auto *pinButton = new QPushButton(QStringLiteral("สุ่ม PIN"), rosterBox);
	rosterButtons->addWidget(addButton);
	rosterButtons->addWidget(removeButton);
	rosterButtons->addWidget(cardButton);
	rosterButtons->addWidget(pinButton);
	rosterLayout->addLayout(rosterButtons);

	auto *assetButtons = new QHBoxLayout();
	auto *bottomBarAsset = new QPushButton(QStringLiteral("เลือก BOTTOM BAR PNG"), rosterBox);
	auto *logoAsset = new QPushButton(QStringLiteral("เลือกโลโก้กลาง PNG"), rosterBox);
	assetButtons->addWidget(bottomBarAsset);
	assetButtons->addWidget(logoAsset);
	rosterLayout->addLayout(assetButtons);
	connect(bottomBarAsset, &QPushButton::clicked, this, [this]() { chooseCard(QStringLiteral("bottomBar")); });
	connect(logoAsset, &QPushButton::clicked, this, [this]() { chooseCard(QStringLiteral("logo")); });

	auto *coverButtons = new QHBoxLayout();
	auto *coverAsset = new QPushButton(QStringLiteral("เลือก Cover PNG"), rosterBox);
	auto *removeCover = new QPushButton(QStringLiteral("ลบ Cover"), rosterBox);
	coverAsset->setToolTip(QStringLiteral("ภาพส่วนกลาง BOTTOM BAR แนะนำ PNG โปร่งใส 1920×1080"));
	coverButtons->addWidget(coverAsset);
	coverButtons->addWidget(removeCover);
	rosterLayout->addLayout(coverButtons);
	connect(coverAsset, &QPushButton::clicked, this, [this]() { chooseCard(QStringLiteral("cover")); });
	connect(removeCover, &QPushButton::clicked, this, [this]() {
		if (!m_session->setCover(QString()))
			showSaveError();
	});

	root->addWidget(rosterBox, 1);
	m_saveError = new QLabel(page);
	m_saveError->setWordWrap(true);
	m_saveError->setStyleSheet(QStringLiteral("color:#ff8075;"));
	root->addWidget(m_saveError);

	auto *liveBox = new QGroupBox(QStringLiteral("รอบปัจจุบัน"), page);
	auto *liveLayout = new QVBoxLayout(liveBox);

	m_summary = new QLabel(liveBox);
	m_summary->setWordWrap(true);
	liveLayout->addWidget(m_summary);

	m_live = new QListWidget(liveBox);
	liveLayout->addWidget(m_live);

	m_revealButton = new QPushButton(QStringLiteral("SCOREBOARD"), liveBox);
	m_revealButton->setMinimumHeight(38);
	liveLayout->addWidget(m_revealButton);
	m_revealButton->setCheckable(true);
	m_bottomBarButton = new QPushButton(QStringLiteral("BOTTOM BAR"), liveBox);
	m_bottomBarButton->setMinimumHeight(38);
	m_bottomBarButton->setCheckable(true);
	liveLayout->addWidget(m_bottomBarButton);
	auto *resultButton = new QPushButton(QStringLiteral("RESUIT — เร็ว ๆ นี้"), liveBox);
	resultButton->setEnabled(false);
	resultButton->setToolTip(QStringLiteral("เร็ว ๆ นี้"));
	liveLayout->addWidget(resultButton);
	auto *logoRow = new QHBoxLayout();
	logoRow->addWidget(new QLabel(QStringLiteral("สำนักสำหรับโลโก้กลาง"), liveBox));
	m_logoPresident = new QComboBox(liveBox);
	logoRow->addWidget(m_logoPresident, 1);
	liveLayout->addLayout(logoRow);
	connect(m_logoPresident, &QComboBox::currentIndexChanged, this, [this](int) {
		if (!m_session->setLogoPresident(m_logoPresident->currentData().toString())) {
			showSaveError();
			refreshLive();
		}
	});

	auto *clearButton = new QPushButton(QStringLiteral("Clear — เริ่มรอบใหม่"), liveBox);
	clearButton->setMinimumHeight(44);
	liveLayout->addWidget(clearButton);

	auto *layoutRow = new QHBoxLayout();
	m_layoutLabel = new QLabel(liveBox);
	m_layoutLabel->setWordWrap(true);
	layoutRow->addWidget(m_layoutLabel, 1);
	m_resetMode = new QComboBox(liveBox);
	m_resetMode->addItem(QStringLiteral("SCOREBOARD"), QStringLiteral("scoreboard"));
	m_resetMode->addItem(QStringLiteral("BOTTOM BAR"), QStringLiteral("bottomBar"));
	layoutRow->addWidget(m_resetMode);
	auto *resetLayoutButton = new QPushButton(QStringLiteral("รีเซ็ตตำแหน่งโหมดที่เลือก"), liveBox);
	layoutRow->addWidget(resetLayoutButton);
	liveLayout->addLayout(layoutRow);

	root->addWidget(liveBox);
	root->addStretch();

	connect(m_serverButton, &QPushButton::clicked, this, [this]() { toggleServer(); });
	connect(addButton, &QPushButton::clicked, this, [this]() { addPresident(); });
	connect(removeButton, &QPushButton::clicked, this, [this]() { removeSelected(); });
	connect(cardButton, &QPushButton::clicked, this, [this]() { chooseCard(QStringLiteral("card")); });
	connect(pinButton, &QPushButton::clicked, this, [this]() { regeneratePin(); });
	connect(m_revealButton, &QPushButton::clicked, this, [this]() {
		if (!m_session->showMode(QStringLiteral("scoreboard")))
			showSaveError();
		refreshLive();
	});
	connect(m_bottomBarButton, &QPushButton::clicked, this, [this]() {
		if (!m_session->showMode(QStringLiteral("bottomBar")))
			showSaveError();
		refreshLive();
	});
	connect(clearButton, &QPushButton::clicked, this, [this]() {
		if (!m_session->clearRound())
			showSaveError();
	});
	connect(resetLayoutButton, &QPushButton::clicked, this, [this]() {
		if (!m_session->resetLayouts(m_resetMode->currentData().toString()))
			showSaveError();
	});

	connect(m_table, &QTableWidget::itemChanged, this, [this](QTableWidgetItem *item) {
		if (m_updating || !item)
			return;
		if (item->column() != ColName && item->column() != ColSchool)
			return;

		QTableWidgetItem *key = m_table->item(item->row(), ColName);
		if (!key)
			return;
		const FffPresident *existing = m_session->presidentById(key->data(Qt::UserRole).toString());
		if (!existing)
			return;

		FffPresident updated = *existing;
		if (item->column() == ColName)
			updated.name = item->text();
		else
			updated.school = item->text();
		if (!m_session->updatePresident(updated)) {
			showSaveError();
			refreshTable();
		}
	});
}

void FffDock::addUrlRow(const QString &label, const QString &url)
{
	auto *row = new QHBoxLayout();
	row->addWidget(new QLabel(label, m_urls));

	auto *field = new QLineEdit(url, m_urls);
	field->setReadOnly(true);
	field->setCursorPosition(0);
	row->addWidget(field, 1);

	auto *copy = new QPushButton(QStringLiteral("คัดลอก"), m_urls);
	row->addWidget(copy);
	connect(copy, &QPushButton::clicked, copy, [copy, url]() {
		QGuiApplication::clipboard()->setText(url);
		copy->setText(QStringLiteral("คัดลอกแล้ว"));
		QTimer::singleShot(1500, copy, [copy]() { copy->setText(QStringLiteral("คัดลอก")); });
	});

	m_urlLayout->addLayout(row);
}

void FffDock::refreshServer()
{
	const bool listening = m_server->isListening();
	m_serverButton->setText(listening ? QStringLiteral("หยุด") : QStringLiteral("เริ่ม"));
	m_port->setEnabled(!listening);

	while (QLayoutItem *item = m_urlLayout->takeAt(0)) {
		if (QLayout *child = item->layout()) {
			while (QLayoutItem *inner = child->takeAt(0)) {
				delete inner->widget();
				delete inner;
			}
		}
		delete item->widget();
		delete item;
	}

	if (!listening) {
		m_serverStatus->setText(QStringLiteral("ยังไม่ได้เปิดเซิร์ฟเวอร์"));
		return;
	}

	const quint16 port = m_server->boundPort();
	m_serverStatus->setText(QStringLiteral("กำลังฟังพอร์ต %1 · มือถือ %2 เครื่อง · จอ %3")
					.arg(port)
					.arg(m_server->phoneClientCount())
					.arg(m_server->overlayClientCount()));

	addUrlRow(QStringLiteral("Browser Source"), QStringLiteral("http://127.0.0.1:%1/overlay").arg(port));
	addUrlRow(QStringLiteral("จอมอนิเตอร์"), QStringLiteral("http://127.0.0.1:%1/monitor").arg(port));

	const QStringList addresses = FffHttpServer::lanAddresses();
	if (addresses.isEmpty()) {
		auto *warning = new QLabel(QStringLiteral("ยังไม่เจอ IP วง LAN — เช็คว่าต่อ Wi-Fi/router แล้วหรือยัง"), m_urls);
		warning->setWordWrap(true);
		m_urlLayout->addWidget(warning);
		return;
	}
	for (const QString &address : addresses)
		addUrlRow(QStringLiteral("มือถือ"), QStringLiteral("http://%1:%2").arg(address).arg(port));
}

void FffDock::refreshTable()
{
	const QString selectedId = selectedPresidentId();
	m_updating = true;
	m_table->setRowCount(m_session->presidents().size());

	int row = 0;
	for (const FffPresident &president : m_session->presidents()) {
		auto *name = new QTableWidgetItem(president.name);
		name->setData(Qt::UserRole, president.id);
		m_table->setItem(row, ColName, name);
		m_table->setItem(row, ColSchool, new QTableWidgetItem(president.school));

		auto *pin = new QTableWidgetItem(president.pin);
		pin->setFlags(pin->flags() & ~Qt::ItemIsEditable);
		m_table->setItem(row, ColPin, pin);

		auto *cardItem =
			new QTableWidgetItem(president.card.isEmpty() ? QStringLiteral("—") : QStringLiteral("มี PNG"));
		cardItem->setFlags(cardItem->flags() & ~Qt::ItemIsEditable);
		m_table->setItem(row, ColCard, cardItem);
		for (const auto &asset :
		     {qMakePair(ColBottomBar, president.bottomBar), qMakePair(ColLogo, president.logo)}) {
			auto *item = new QTableWidgetItem(asset.second.isEmpty() ? QStringLiteral("—")
										 : QStringLiteral("มี PNG"));
			item->setFlags(item->flags() & ~Qt::ItemIsEditable);
			m_table->setItem(row, asset.first, item);
		}
		if (president.id == selectedId)
			m_table->selectRow(row);
		++row;
	}
	m_updating = false;
}

void FffDock::refreshLive()
{
	const bool revealed = m_session->phase() == FffPhase::Revealed;
	const int total = m_session->presidents().size();
	const int voted = m_session->votedCount();
	const bool ready = total > 0 && voted == total;

	QString phase = QStringLiteral("กำลังรอ");
	if (revealed)
		phase = QStringLiteral("ขึ้นจอแล้ว");
	else if (ready)
		phase = QStringLiteral("ครบแล้ว พร้อมแสดง");

	m_summary->setText(
		QStringLiteral("รอบ %1 · กดแล้ว %2/%3 · %4").arg(m_session->round()).arg(voted).arg(total).arg(phase));

	m_revealButton->setEnabled(true);
	m_bottomBarButton->setEnabled(true);
	m_revealButton->setChecked(revealed && m_session->displayMode() == QLatin1String("scoreboard"));
	m_bottomBarButton->setChecked(revealed && m_session->displayMode() == QLatin1String("bottomBar"));
	{
		const QSignalBlocker blocker(m_logoPresident);
		m_logoPresident->clear();
		m_logoPresident->addItem(QStringLiteral("ไม่เลือกโลโก้"), QString());
		for (const auto &president : m_session->presidents())
			m_logoPresident->addItem(president.school.isEmpty() ? president.name : president.school,
						 president.id);
		m_logoPresident->setCurrentIndex(qMax(0, m_logoPresident->findData(m_session->logoPresidentId())));
	}
	// Nothing reaches the stream on its own any more, so make the moment
	// everyone has answered impossible to miss.
	m_revealButton->setStyleSheet(
		ready && !revealed ? QStringLiteral("background:#21b04a;color:#ffffff;font-weight:700;") : QString());

	m_layoutLabel->setText(QStringLiteral("เลือกโหมดแก้ไขในจอมอนิเตอร์ เพื่อลากและปรับขนาด โดยไม่สลับภาพออกอากาศ"));

	m_live->clear();
	for (const FffPresident &president : m_session->presidents()) {
		const FffVote vote = m_session->voteOf(president.id);
		QString colour = QStringLiteral("ยังไม่กด");
		if (vote == FffVote::Red)
			colour = QStringLiteral("แดง");
		else if (vote == FffVote::Green)
			colour = QStringLiteral("เขียว");

		m_live->addItem(QStringLiteral("%1 %2 — %3")
					.arg(vote == FffVote::None ? QStringLiteral("○") : QStringLiteral("●"),
					     president.name.isEmpty() ? president.school : president.name, colour));
	}
}

QString FffDock::selectedPresidentId() const
{
	const int row = m_table->currentRow();
	if (row < 0)
		return QString();
	QTableWidgetItem *item = m_table->item(row, ColName);
	return item ? item->data(Qt::UserRole).toString() : QString();
}

void FffDock::toggleServer()
{
	if (m_server->isListening()) {
		m_server->stop();
		refreshServer();
		return;
	}

	const quint16 port = static_cast<quint16>(m_port->value());
	if (!m_session->setPort(port)) {
		showSaveError();
		m_port->setValue(m_session->port());
		return;
	}

	QString error;
	if (!m_server->start(port, &error))
		m_serverStatus->setText(QStringLiteral("เปิดพอร์ต %1 ไม่ได้: %2").arg(port).arg(error));
	else
		refreshServer();
}

void FffDock::addPresident()
{
	FffPresident president;
	president.id = FffSession::newId();
	president.name = QStringLiteral("นายกคนใหม่");
	president.school = QStringLiteral("สำนักวิชา");
	president.pin = m_session->uniquePin();
	if (!m_session->addPresident(president))
		showSaveError();
	refreshTable();
}

void FffDock::removeSelected()
{
	const QString id = selectedPresidentId();
	if (id.isEmpty())
		return;
	if (!m_session->removePresident(id))
		showSaveError();
	refreshTable();
}

void FffDock::chooseCard(const QString &kind)
{
	const QString id = selectedPresidentId();
	if (id.isEmpty() && kind != QLatin1String("cover"))
		return;

	const QString title = kind == QLatin1String("cover")       ? QStringLiteral("เลือก Cover PNG — แนะนำ 1920×1080")
			      : kind == QLatin1String("bottomBar") ? QStringLiteral("เลือก BOTTOM BAR PNG")
			      : kind == QLatin1String("logo")      ? QStringLiteral("เลือกโลโก้กลาง PNG")
								   : QStringLiteral("เลือก SCOREBOARD PNG");
	const QString file = QFileDialog::getOpenFileName(this, title, QString(), QStringLiteral("การ์ด PNG (*.png)"));
	if (file.isEmpty())
		return;

	const QString stored = m_session->importAsset(file, id, kind);
	if (stored.isEmpty()) {
		m_saveError->setText(QStringLiteral("นำเข้า PNG ไม่สำเร็จ — รูปเดิมยังอยู่"));
		return;
	}

	if (kind == QLatin1String("cover")) {
		if (!m_session->setCover(stored))
			showSaveError();
		return;
	}

	const FffPresident *existing = m_session->presidentById(id);
	if (!existing)
		return;
	FffPresident updated = *existing;
	if (kind == QLatin1String("bottomBar"))
		updated.bottomBar = stored;
	else if (kind == QLatin1String("logo"))
		updated.logo = stored;
	else
		updated.card = stored;
	if (!m_session->updatePresident(updated)) {
		showSaveError();
		refreshTable();
	}
	refreshTable();
}

void FffDock::showSaveError()
{
	m_saveError->setText(QStringLiteral("บันทึกไม่สำเร็จ — คืนค่าก่อนแก้ไขแล้ว กรุณาตรวจสอบพื้นที่และสิทธิ์เขียนไฟล์"));
}

void FffDock::regeneratePin()
{
	const QString id = selectedPresidentId();
	if (id.isEmpty())
		return;

	const FffPresident *existing = m_session->presidentById(id);
	if (!existing)
		return;
	FffPresident updated = *existing;
	updated.pin = m_session->uniquePin();
	if (!m_session->updatePresident(updated)) {
		showSaveError();
		refreshTable();
	}
	refreshTable();
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
