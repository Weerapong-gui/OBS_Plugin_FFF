/*
FFF Tools for OBS - photo framing dialog
Copyright (C) 2026 Student Union MFU <studentunion.developer@gmail.com>
GPL-2.0-or-later
*/

#pragma once

#include <functional>

#include <QDialog>
#include <QPixmap>
#include <QPoint>
#include <QWidget>

class QSlider;

/*
 * Round preview that paints the picture exactly the way the overlay CSS does:
 *   object-fit: cover; transform: translate(x%, y%) scale(zoom)
 * Drag to pan, wheel to zoom.
 */
class FffPhotoPreview : public QWidget {
public:
	explicit FffPhotoPreview(QWidget *parent = nullptr);

	void setPixmap(const QPixmap &pixmap);
	void setFraming(double zoom, double x, double y);

	double zoom() const { return m_zoom; }
	double panX() const { return m_x; }
	double panY() const { return m_y; }

	// Pan only has room to move once the picture is zoomed in.
	double panLimit() const;

	std::function<void()> onChanged;

protected:
	void paintEvent(QPaintEvent *event) override;
	void mousePressEvent(QMouseEvent *event) override;
	void mouseMoveEvent(QMouseEvent *event) override;
	void wheelEvent(QWheelEvent *event) override;

private:
	void clampPan();

	QPixmap m_pixmap;
	double m_zoom = 1.0;
	double m_x = 0.0;
	double m_y = 0.0;
	QPoint m_drag;
};

class FffPhotoEditor : public QDialog {
public:
	FffPhotoEditor(const QString &imagePath, double zoom, double x, double y, QWidget *parent = nullptr);

	double zoom() const;
	double panX() const;
	double panY() const;

private:
	FffPhotoPreview *m_preview = nullptr;
	QSlider *m_zoomSlider = nullptr;
};
