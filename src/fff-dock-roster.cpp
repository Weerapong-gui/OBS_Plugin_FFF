/*
FFF Tools for OBS - dock roster tab
Copyright (C) 2026 Student Union MFU <studentunion.developer@gmail.com>
GPL-2.0-or-later
*/

#include "fff-dock-roster.h"
#include "fff-dock-ui.h"
#include "fff-session.h"

#include <QAction>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QLabel>
#include <QList>
#include <QMenu>
#include <QPair>
#include <QPushButton>
#include <QTableWidget>
#include <QToolButton>
#include <QVBoxLayout>

namespace {

enum Column { ColName = 0, ColSchool = 1, ColPin = 2, ColCard = 3, ColBottomBar = 4, ColLogo = 5 };

constexpr int kVisibleRows = 6;

struct AssetMenu {
	QString kind;
	QString choose;
	QString clear;
};

QString mark(const QString &asset)
{
	return asset.isEmpty() ? QStringLiteral("—") : QStringLiteral("✓");
}

} // namespace

FffRosterTab::FffRosterTab(FffSession *session, QWidget *parent) : QWidget(parent), m_session(session)
{
	auto *layout = new QVBoxLayout(this);

	m_table = new QTableWidget(0, 6, this);
	m_table->setHorizontalHeaderLabels({QStringLiteral("ชื่อนายก"), QStringLiteral("สำนักวิชา"), QStringLiteral("PIN"),
					    QStringLiteral("การ์ด"), QStringLiteral("BAR"), QStringLiteral("โลโก้")});
	m_table->horizontalHeader()->setSectionResizeMode(ColName, QHeaderView::Stretch);
	m_table->horizontalHeader()->setSectionResizeMode(ColSchool, QHeaderView::Stretch);
	for (int column : {ColPin, ColCard, ColBottomBar, ColLogo})
		m_table->horizontalHeader()->setSectionResizeMode(column, QHeaderView::ResizeToContents);
	m_table->verticalHeader()->setVisible(false);
	m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
	m_table->setSelectionMode(QAbstractItemView::SingleSelection);
	m_table->setMinimumHeight(m_table->verticalHeader()->defaultSectionSize() * kVisibleRows +
				  m_table->horizontalHeader()->sizeHint().height() + 4);
	layout->addWidget(m_table, 1);

	const QList<AssetMenu> assets = {
		{QStringLiteral("card"), QStringLiteral("เลือกการ์ด PNG (สำรอง)…"), QStringLiteral("ลบการ์ด PNG")},
		{QStringLiteral("bottomBar"), QStringLiteral("เลือก BOTTOM BAR PNG…"),
		 QStringLiteral("ลบ BOTTOM BAR PNG")},
		{QStringLiteral("logo"), QStringLiteral("เลือกโลโก้กลาง PNG…"), QStringLiteral("ลบโลโก้กลาง PNG")},
	};

	m_add = new QPushButton(QStringLiteral("+ เพิ่ม"), this);
	m_png = new QToolButton(this);
	m_png->setText(QStringLiteral("PNG"));
	m_png->setPopupMode(QToolButton::InstantPopup);
	auto *pngMenu = new QMenu(m_png);
	for (const AssetMenu &asset : assets) {
		QAction *choose = pngMenu->addAction(asset.choose);
		m_choose.insert(asset.kind, choose);
		const QString kind = asset.kind;
		connect(choose, &QAction::triggered, this, [this, kind]() { chooseAsset(kind); });
	}
	// Removing sits apart from choosing so a slip of the pointer lands on the
	// harmless half of the menu.
	pngMenu->addSeparator();
	for (const AssetMenu &asset : assets) {
		QAction *clear = pngMenu->addAction(asset.clear);
		m_clear.insert(asset.kind, clear);
		const QString kind = asset.kind;
		connect(clear, &QAction::triggered, this, [this, kind]() { applyAsset(kind, QString()); });
	}
	m_png->setMenu(pngMenu);

	m_more = new QToolButton(this);
	m_more->setText(QStringLiteral("⋯"));
	m_more->setPopupMode(QToolButton::InstantPopup);
	auto *moreMenu = new QMenu(m_more);
	m_regeneratePin = moreMenu->addAction(QStringLiteral("สุ่ม PIN ใหม่…"));
	moreMenu->addSeparator();
	m_remove = moreMenu->addAction(QStringLiteral("ลบนายก…"));
	m_more->setMenu(moreMenu);

	auto *buttons = new QHBoxLayout();
	m_add->setMinimumHeight(32);
	m_png->setMinimumHeight(32);
	m_more->setMinimumHeight(32);
	buttons->addWidget(m_add);
	buttons->addWidget(m_png);
	buttons->addWidget(m_more);
	buttons->addStretch();
	layout->addLayout(buttons);

	m_hint = new QLabel(QStringLiteral("เลือกนายกในตารางเพื่อจัดการ PNG และ PIN"), this);
	m_hint->setWordWrap(true);
	m_hint->setStyleSheet(QStringLiteral("color:%1;").arg(QLatin1String(FffColor::kMuted)));
	layout->addWidget(m_hint);

	connect(m_add, &QPushButton::clicked, this, [this]() { addPresident(); });
	connect(m_regeneratePin, &QAction::triggered, this, [this]() { regeneratePin(); });
	connect(m_remove, &QAction::triggered, this, [this]() { removeSelected(); });
	connect(m_table, &QTableWidget::itemSelectionChanged, this, [this]() { updateActions(); });
	connect(m_table, &QTableWidget::itemChanged, this, [this](QTableWidgetItem *item) {
		if (m_updating || !item || (item->column() != ColName && item->column() != ColSchool))
			return;
		QTableWidgetItem *key = m_table->item(item->row(), ColName);
		const FffPresident *existing = key ? m_session->presidentById(key->data(Qt::UserRole).toString())
						   : nullptr;
		if (!existing)
			return;
		FffPresident updated = *existing;
		if (item->column() == ColName)
			updated.name = item->text();
		else
			updated.school = item->text();
		if (!m_session->updatePresident(updated)) {
			emit errorRaised(fffSaveErrorText());
			refresh();
		}
	});

	refresh();
}

void FffRosterTab::refresh()
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

		const QList<QPair<int, QString>> fixed = {{ColPin, president.pin},
							  {ColCard, mark(president.card)},
							  {ColBottomBar, mark(president.bottomBar)},
							  {ColLogo, mark(president.logo)}};
		for (const auto &cell : fixed) {
			auto *item = new QTableWidgetItem(cell.second);
			item->setFlags(item->flags() & ~Qt::ItemIsEditable);
			if (cell.first != ColPin)
				item->setTextAlignment(Qt::AlignCenter);
			m_table->setItem(row, cell.first, item);
		}
		++row;
	}
	m_updating = false;
	selectPresident(selectedId);
}

QString FffRosterTab::selectedPresidentId() const
{
	const QModelIndexList rows = m_table->selectionModel()->selectedRows();
	if (rows.isEmpty())
		return QString();
	const QTableWidgetItem *item = m_table->item(rows.first().row(), ColName);
	return item ? item->data(Qt::UserRole).toString() : QString();
}

void FffRosterTab::selectPresident(const QString &id)
{
	for (int row = 0; !id.isEmpty() && row < m_table->rowCount(); ++row) {
		const QTableWidgetItem *item = m_table->item(row, ColName);
		if (item && item->data(Qt::UserRole).toString() == id) {
			m_table->selectRow(row);
			updateActions();
			return;
		}
	}
	m_table->clearSelection();
	m_table->setCurrentItem(nullptr);
	updateActions();
}

void FffRosterTab::updateActions()
{
	const FffPresident *president = m_session->presidentById(selectedPresidentId());
	const bool selected = president != nullptr;
	const QString hint = selected ? QString() : QStringLiteral("เลือกนายกในตารางก่อน");
	m_png->setEnabled(selected);
	m_png->setToolTip(hint);
	m_more->setEnabled(selected);
	m_more->setToolTip(hint);
	m_hint->setVisible(!selected);
	m_clear.value(QStringLiteral("card"))->setEnabled(selected && !president->card.isEmpty());
	m_clear.value(QStringLiteral("bottomBar"))->setEnabled(selected && !president->bottomBar.isEmpty());
	m_clear.value(QStringLiteral("logo"))->setEnabled(selected && !president->logo.isEmpty());
}

void FffRosterTab::addPresident()
{
	FffPresident president;
	president.id = FffSession::newId();
	president.name = QStringLiteral("นายกคนใหม่");
	president.school = QStringLiteral("สำนักวิชา");
	president.pin = m_session->uniquePin();
	if (!m_session->addPresident(president))
		emit errorRaised(fffSaveErrorText());
	refresh();
	selectPresident(president.id);
}

void FffRosterTab::chooseAsset(const QString &kind)
{
	const QString id = selectedPresidentId();
	if (id.isEmpty())
		return;
	const QString title = kind == QLatin1String("bottomBar") ? QStringLiteral("เลือก BOTTOM BAR PNG")
			      : kind == QLatin1String("logo")    ? QStringLiteral("เลือกโลโก้กลาง PNG")
								 : QStringLiteral("เลือกการ์ด PNG สำรอง");
	const QString file = QFileDialog::getOpenFileName(this, title, QString(), QStringLiteral("การ์ด PNG (*.png)"));
	if (file.isEmpty())
		return;
	const QString stored = m_session->importAsset(file, id, kind);
	if (stored.isEmpty()) {
		emit errorRaised(QStringLiteral("นำเข้า PNG ไม่สำเร็จ รูปเดิมยังอยู่"));
		return;
	}
	applyAsset(kind, stored);
}

void FffRosterTab::applyAsset(const QString &kind, const QString &stored)
{
	// The PNG itself stays in the cards directory, so clearing by mistake is
	// undone by choosing the same file again.
	const FffPresident *existing = m_session->presidentById(selectedPresidentId());
	if (!existing)
		return;
	FffPresident updated = *existing;
	if (kind == QLatin1String("bottomBar"))
		updated.bottomBar = stored;
	else if (kind == QLatin1String("logo"))
		updated.logo = stored;
	else
		updated.card = stored;
	if (!m_session->updatePresident(updated))
		emit errorRaised(fffSaveErrorText());
	refresh();
}

void FffRosterTab::regeneratePin()
{
	const QString id = selectedPresidentId();
	const FffPresident *existing = m_session->presidentById(id);
	if (!existing)
		return;
	const QString text =
		QStringLiteral("PIN เดิมของ %1 จะใช้เข้าสู่ระบบใหม่ไม่ได้\nมือถือที่เข้าอยู่แล้วยังใช้ต่อได้").arg(existing->name);
	if (!fffConfirm(this, QStringLiteral("สุ่ม PIN ใหม่"), text, QStringLiteral("สุ่ม PIN ใหม่")))
		return;
	// The dialog ran an event loop; look the president up again.
	existing = m_session->presidentById(id);
	if (!existing)
		return;
	FffPresident updated = *existing;
	updated.pin = m_session->uniquePin();
	if (!m_session->updatePresident(updated))
		emit errorRaised(fffSaveErrorText());
	refresh();
}

void FffRosterTab::removeSelected()
{
	const QString id = selectedPresidentId();
	const FffPresident *existing = m_session->presidentById(id);
	if (!existing)
		return;
	const QString text = QStringLiteral("ลบ %1 (%2) ออกจากรายชื่อ?").arg(existing->name, existing->school);
	if (!fffConfirm(this, QStringLiteral("ลบนายก"), text, QStringLiteral("ลบนายก")))
		return;
	if (!m_session->removePresident(id))
		emit errorRaised(fffSaveErrorText());
	refresh();
}
