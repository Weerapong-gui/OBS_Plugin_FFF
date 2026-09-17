/*
FFF Tools for OBS - colours and confirmations shared by the dock panels
Copyright (C) 2026 Student Union MFU <studentunion.developer@gmail.com>
GPL-2.0-or-later
*/

#pragma once

#include <QMessageBox>
#include <QPushButton>
#include <QString>
#include <QWidget>

#include <functional>
#include <utility>

// The web pages use the same values (data/web/app.css), so a state reads as
// the same colour in the dock and on the monitor.
namespace FffColor {
constexpr const char *kRed = "#e23c3c";
constexpr const char *kGreen = "#21b04a";
constexpr const char *kAmber = "#ffb02e";
constexpr const char *kGrey = "#6d7580";
constexpr const char *kMuted = "#9aa4b0";
constexpr const char *kError = "#ff8075";
} // namespace FffColor

inline QString fffSaveErrorText()
{
	return QStringLiteral("บันทึกไม่สำเร็จ คืนค่าก่อนแก้ไขแล้ว กรุณาตรวจสอบพื้นที่และสิทธิ์เขียนไฟล์");
}

// The live bar's buttons sit side by side, so a checked one ("on air") has to
// be unmistakable against its sibling rather than a faint default check
// mark; non-checkable buttons just get the same bordered rounded rectangle.
inline QString fffModeButtonStyle()
{
	return QStringLiteral(
		"QPushButton { border: 1px solid #3a4452; border-radius: 6px; padding: 8px 12px; font-weight: 700; }"
		"QPushButton:hover { border-color: #4d9dff; }"
		"QPushButton:checked { background: #4d9dff; color: #0e1116; border-color: #4d9dff; }"
		"QPushButton:focus { border-color: #4d9dff; }");
}

inline std::function<bool(const QString &title)> &fffConfirmOverride()
{
	static std::function<bool(const QString &title)> hook;
	return hook;
}

// Tests answer confirmations here instead of through a modal loop.
inline void fffSetConfirmOverride(std::function<bool(const QString &title)> hook)
{
	fffConfirmOverride() = std::move(hook);
}

// Cancel is both the default and the Escape button, so a stray Enter never
// destroys anything.
inline bool fffConfirm(QWidget *parent, const QString &title, const QString &text, const QString &acceptText)
{
	if (fffConfirmOverride())
		return fffConfirmOverride()(title);

	QMessageBox box(QMessageBox::Warning, title, text, QMessageBox::NoButton, parent);
	QPushButton *accept = box.addButton(acceptText, QMessageBox::AcceptRole);
	QPushButton *cancel = box.addButton(QStringLiteral("ยกเลิก"), QMessageBox::RejectRole);
	box.setDefaultButton(cancel);
	box.setEscapeButton(cancel);
	box.exec();
	return box.clickedButton() == accept;
}
