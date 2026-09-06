/*
FFF Tools for OBS - photo framing dialog
Copyright (C) 2026 Student Union MFU <studentunion.developer@gmail.com>
GPL-2.0-or-later
*/

#include "fff-photo-editor.h"

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QSlider>
#include <QVBoxLayout>
#include <QWheelEvent>

namespace {

constexpr int kPreviewSize = 260;
constexpr double kMinZoom = 1.0;
constexpr double kMaxZoom = 4.0;

}

FffPhotoPreview::FffPhotoPreview(QWidget *parent) : QWidget(parent)
{
	setFixedSize(kPreviewSize, kPreviewSize);
	setCursor(Qt::OpenHandCursor);
}

void FffPhotoPreview::setPixmap(const QPixmap &pixmap)
{
	m_pixmap = pixmap;
	update();
}

void FffPhotoPreview::setFraming(double zoom, double x, double y)
{
	m_zoom = qBound(kMinZoom, zoom, kMaxZoom);
	m_x = x;
	m_y = y;
	clampPan();
	update();
}

double FffPhotoPreview::panLimit() const
{
	return (m_zoom - 1.0) / 2.0 * 100.0;
}

void FffPhotoPreview::clampPan()
{
	const double limit = panLimit();
	m_x = qBound(-limit, m_x, limit);
	m_y = qBound(-limit, m_y, limit);
}

void FffPhotoPreview::paintEvent(QPaintEvent *event)
{
	Q_UNUSED(event)

	QPainter painter(this);
	painter.setRenderHint(QPainter::Antialiasing);
	painter.setRenderHint(QPainter::SmoothPixmapTransform);

	const double w = width();
	const double h = height();

	QPainterPath circle;
	circle.addEllipse(0, 0, w, h);
	painter.setClipPath(circle);
	painter.fillRect(rect(), QColor(0x22, 0x29, 0x32));

	if (m_pixmap.isNull())
		return;

	// object-fit: cover, then object-position: center
	const double cover = qMax(w / m_pixmap.width(), h / m_pixmap.height());
	const double dw = m_pixmap.width() * cover;
	const double dh = m_pixmap.height() * cover;
	const double cx = (w - dw) / 2.0;
	const double cy = (h - dh) / 2.0;

	// transform: translate(x%, y%) scale(zoom), origin at the centre
	painter.translate(w * m_x / 100.0, h * m_y / 100.0);
	painter.translate(w / 2.0, h / 2.0);
	painter.scale(m_zoom, m_zoom);
	painter.translate(-w / 2.0, -h / 2.0);

	painter.drawPixmap(QRectF(cx, cy, dw, dh), m_pixmap, m_pixmap.rect());
}

void FffPhotoPreview::mousePressEvent(QMouseEvent *event)
{
	m_drag = event->pos();
	setCursor(Qt::ClosedHandCursor);
}

void FffPhotoPreview::mouseMoveEvent(QMouseEvent *event)
{
	if (!(event->buttons() & Qt::LeftButton)) {
		setCursor(Qt::OpenHandCursor);
		return;
	}

	const QPoint delta = event->pos() - m_drag;
	m_drag = event->pos();

	m_x += delta.x() * 100.0 / width();
	m_y += delta.y() * 100.0 / height();
	clampPan();
	update();
	if (onChanged)
		onChanged();
}

void FffPhotoPreview::wheelEvent(QWheelEvent *event)
{
	const double steps = event->angleDelta().y() / 120.0;
	if (qFuzzyIsNull(steps))
		return;

	m_zoom = qBound(kMinZoom, m_zoom + steps * 0.1, kMaxZoom);
	clampPan();
	update();
	if (onChanged)
		onChanged();
	event->accept();
}

FffPhotoEditor::FffPhotoEditor(const QString &imagePath, double zoom, double x, double y, QWidget *parent)
	: QDialog(parent)
{
	setWindowTitle(QStringLiteral("ปรับรูปนายก"));

	auto *root = new QVBoxLayout(this);

	auto *hint = new QLabel(QStringLiteral("ลากในวงกลมเพื่อเลื่อนรูป · หมุนล้อเมาส์หรือใช้แถบเลื่อนเพื่อซูม"), this);
	hint->setWordWrap(true);
	root->addWidget(hint);

	m_preview = new FffPhotoPreview(this);
	m_preview->setPixmap(QPixmap(imagePath));
	m_preview->setFraming(zoom, x, y);
	root->addWidget(m_preview, 0, Qt::AlignHCenter);

	auto *zoomRow = new QHBoxLayout();
	zoomRow->addWidget(new QLabel(QStringLiteral("ซูม"), this));
	m_zoomSlider = new QSlider(Qt::Horizontal, this);
	m_zoomSlider->setRange(static_cast<int>(kMinZoom * 100), static_cast<int>(kMaxZoom * 100));
	m_zoomSlider->setValue(static_cast<int>(m_preview->zoom() * 100));
	zoomRow->addWidget(m_zoomSlider);
	auto *resetButton = new QPushButton(QStringLiteral("รีเซ็ต"), this);
	zoomRow->addWidget(resetButton);
	root->addLayout(zoomRow);

	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
	root->addWidget(buttons);

	connect(m_zoomSlider, &QSlider::valueChanged, this, [this](int value) {
		m_preview->setFraming(value / 100.0, m_preview->panX(), m_preview->panY());
	});
	connect(resetButton, &QPushButton::clicked, this, [this]() {
		m_zoomSlider->setValue(100);
		m_preview->setFraming(1.0, 0.0, 0.0);
	});
	// Wheel zooming has to drag the slider along with it.
	m_preview->onChanged = [this]() {
		const int value = static_cast<int>(m_preview->zoom() * 100);
		if (m_zoomSlider->value() != value) {
			QSignalBlocker blocker(m_zoomSlider);
			m_zoomSlider->setValue(value);
		}
	};
	connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

double FffPhotoEditor::zoom() const
{
	return m_preview->zoom();
}

double FffPhotoEditor::panX() const
{
	return m_preview->panX();
}

double FffPhotoEditor::panY() const
{
	return m_preview->panY();
}
