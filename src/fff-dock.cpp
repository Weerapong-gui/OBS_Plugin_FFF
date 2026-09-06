/*
FFF Tools for OBS - flag board control dock
Copyright (C) 2026 Student Union MFU <studentunion.developer@gmail.com>
GPL-2.0-or-later
*/

#include "fff-http-server.h"
#include "fff-photo-editor.h"
#include "fff-session.h"

#include <obs-module.h>
#include <obs-frontend-api.h>
#include <plugin-support.h>

#include <QClipboard>
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

enum Column { ColName = 0, ColSchool = 1, ColPin = 2, ColPhoto = 3 };

constexpr int kVisibleRows = 6;

} // namespace

/*
 * Operator console: set the presidents up before the event, then drive the
 * rounds from the buttons at the bottom. Nothing reaches the stream until
 * "แสดงผลขึ้นจอ" is pressed.
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
	void choosePhoto();
	void adjustPhoto();
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

	bool m_updating = false;
};

FffDock::FffDock()
{
	m_session = new FffSession(this);
	m_session->load();
	m_server = new FffHttpServer(m_session, this);

	buildUi();

	connect(m_session, &FffSession::changed, this, [this]() { refreshLive(); });
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

	m_table = new QTableWidget(0, 4, rosterBox);
	m_table->setHorizontalHeaderLabels(
		{QStringLiteral("ชื่อนายก"), QStringLiteral("สำนักวิชา"), QStringLiteral("PIN"), QStringLiteral("รูป")});
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
	auto *photoButton = new QPushButton(QStringLiteral("เลือกรูป"), rosterBox);
	auto *cropButton = new QPushButton(QStringLiteral("ปรับรูป"), rosterBox);
	auto *pinButton = new QPushButton(QStringLiteral("สุ่ม PIN"), rosterBox);
	rosterButtons->addWidget(addButton);
	rosterButtons->addWidget(removeButton);
	rosterButtons->addWidget(photoButton);
	rosterButtons->addWidget(cropButton);
	rosterButtons->addWidget(pinButton);
	rosterLayout->addLayout(rosterButtons);

	root->addWidget(rosterBox, 1);

	auto *liveBox = new QGroupBox(QStringLiteral("รอบปัจจุบัน"), page);
	auto *liveLayout = new QVBoxLayout(liveBox);

	m_summary = new QLabel(liveBox);
	m_summary->setWordWrap(true);
	liveLayout->addWidget(m_summary);

	m_live = new QListWidget(liveBox);
	liveLayout->addWidget(m_live);

	m_revealButton = new QPushButton(QStringLiteral("แสดงผลขึ้นจอ"), liveBox);
	m_revealButton->setMinimumHeight(38);
	liveLayout->addWidget(m_revealButton);

	auto *clearButton = new QPushButton(QStringLiteral("Clear — เริ่มรอบใหม่"), liveBox);
	clearButton->setMinimumHeight(44);
	liveLayout->addWidget(clearButton);

	auto *layoutRow = new QHBoxLayout();
	m_layoutLabel = new QLabel(liveBox);
	m_layoutLabel->setWordWrap(true);
	layoutRow->addWidget(m_layoutLabel, 1);
	auto *resetLayoutButton = new QPushButton(QStringLiteral("รีเซ็ตตำแหน่ง"), liveBox);
	layoutRow->addWidget(resetLayoutButton);
	liveLayout->addLayout(layoutRow);

	root->addWidget(liveBox);
	root->addStretch();

	connect(m_serverButton, &QPushButton::clicked, this, [this]() { toggleServer(); });
	connect(addButton, &QPushButton::clicked, this, [this]() { addPresident(); });
	connect(removeButton, &QPushButton::clicked, this, [this]() { removeSelected(); });
	connect(photoButton, &QPushButton::clicked, this, [this]() { choosePhoto(); });
	connect(cropButton, &QPushButton::clicked, this, [this]() { adjustPhoto(); });
	connect(pinButton, &QPushButton::clicked, this, [this]() { regeneratePin(); });
	connect(m_revealButton, &QPushButton::clicked, this, [this]() { m_session->forceReveal(); });
	connect(clearButton, &QPushButton::clicked, this, [this]() { m_session->clearRound(); });
	connect(resetLayoutButton, &QPushButton::clicked, this, [this]() { m_session->setLayout(FffLayout()); });

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
		m_session->updatePresident(updated);
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

		QString photo = QStringLiteral("—");
		if (!president.photo.isEmpty()) {
			photo = qFuzzyCompare(president.photoZoom, 1.0)
					? QStringLiteral("มีรูป")
					: QStringLiteral("มีรูป ×%1").arg(president.photoZoom, 0, 'f', 1);
		}
		auto *photoItem = new QTableWidgetItem(photo);
		photoItem->setFlags(photoItem->flags() & ~Qt::ItemIsEditable);
		m_table->setItem(row, ColPhoto, photoItem);
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

	m_revealButton->setEnabled(!revealed && total > 0);
	// Nothing reaches the stream on its own any more, so make the moment
	// everyone has answered impossible to miss.
	m_revealButton->setStyleSheet(
		ready && !revealed ? QStringLiteral("background:#21b04a;color:#ffffff;font-weight:700;") : QString());

	const FffLayout layout = m_session->layout();
	m_layoutLabel->setText(QStringLiteral("ตำแหน่งจอ %1% / %2% · ขนาด %3% (ลากปรับได้ที่จอมอนิเตอร์)")
				       .arg(layout.x * 100, 0, 'f', 1)
				       .arg(layout.y * 100, 0, 'f', 1)
				       .arg(layout.scale * 100, 0, 'f', 0));

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
	m_session->setPort(port);

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
	m_session->addPresident(president);
	refreshTable();
}

void FffDock::removeSelected()
{
	const QString id = selectedPresidentId();
	if (id.isEmpty())
		return;
	m_session->removePresident(id);
	refreshTable();
}

void FffDock::choosePhoto()
{
	const QString id = selectedPresidentId();
	if (id.isEmpty())
		return;

	const QString file = QFileDialog::getOpenFileName(this, QStringLiteral("เลือกรูปนายก"), QString(),
							  QStringLiteral("รูปภาพ (*.png *.jpg *.jpeg *.webp)"));
	if (file.isEmpty())
		return;

	const QString stored = m_session->importPhoto(file, id);
	if (stored.isEmpty())
		return;

	const FffPresident *existing = m_session->presidentById(id);
	if (!existing)
		return;
	FffPresident updated = *existing;
	updated.photo = stored;
	updated.photoZoom = 1.0;
	updated.photoX = 0.0;
	updated.photoY = 0.0;
	m_session->updatePresident(updated);
	refreshTable();

	// Straight into framing: a fresh picture almost always needs it.
	adjustPhoto();
}

void FffDock::adjustPhoto()
{
	const QString id = selectedPresidentId();
	if (id.isEmpty())
		return;

	const FffPresident *existing = m_session->presidentById(id);
	if (!existing)
		return;
	if (existing->photo.isEmpty()) {
		choosePhoto();
		return;
	}

	FffPhotoEditor editor(m_session->photoPath(*existing), existing->photoZoom, existing->photoX, existing->photoY,
			      this);
	if (editor.exec() != QDialog::Accepted)
		return;

	// Re-read: the dialog was modal, but the roster can still have moved.
	const FffPresident *current = m_session->presidentById(id);
	if (!current)
		return;
	FffPresident updated = *current;
	updated.photoZoom = editor.zoom();
	updated.photoX = editor.panX();
	updated.photoY = editor.panY();
	m_session->updatePresident(updated);
	refreshTable();
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
	m_session->updatePresident(updated);
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
