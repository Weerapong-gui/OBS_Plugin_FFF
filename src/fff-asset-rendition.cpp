/*
FFF Tools for OBS - shrunk copies of artwork for the 1920x1080 stream
Copyright (C) 2026 Student Union MFU <studentunion.developer@gmail.com>
GPL-2.0-or-later
*/

#include "fff-asset-rendition.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QSaveFile>
#include <QtEndian>

#include <limits>

namespace FffAssetRendition {

QSize fittedSize(const QSize &source)
{
	if (!source.isValid() || (source.width() <= kMaxWidth && source.height() <= kMaxHeight))
		return source;
	return source.scaled(kMaxWidth, kMaxHeight, Qt::KeepAspectRatio);
}

// Width and height straight from the IHDR chunk. Imports are PNG only, and a
// decoder handed a truncated file would complain on stderr for nothing.
static QSize pngSize(const QString &path)
{
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly))
		return QSize();
	const QByteArray head = file.read(24);
	if (head.size() < 24 || !head.startsWith(QByteArray::fromHex("89504e470d0a1a0a")) ||
	    head.mid(12, 4) != QByteArrayLiteral("IHDR"))
		return QSize();
	const quint32 width = qFromBigEndian<quint32>(head.constData() + 16);
	const quint32 height = qFromBigEndian<quint32>(head.constData() + 20);
	const quint32 limit = quint32(std::numeric_limits<int>::max());
	return width > limit || height > limit ? QSize() : QSize(int(width), int(height));
}

bool needsRendition(const QString &path)
{
	const QSize size = pngSize(path);
	return size.isValid() && fittedSize(size) != size;
}

QString renditionPath(const QString &sourcePath)
{
	const QFileInfo source(sourcePath);
	return source.dir().filePath(QStringLiteral("renditions/") + source.fileName());
}

bool writeRendition(const QString &sourcePath, const QString &targetPath)
{
	QImageReader reader(sourcePath);
	const QSize size = reader.size();
	if (!size.isValid())
		return false;
	// Formats that cannot decode at a smaller size are read in full and then
	// scaled smoothly by Qt, which averages the pixels a thumbnail drops.
	reader.setScaledSize(fittedSize(size));
	const QImage image = reader.read();
	if (image.isNull() || !QDir().mkpath(QFileInfo(targetPath).absolutePath()))
		return false;

	QSaveFile file(targetPath);
	return file.open(QIODevice::WriteOnly) && image.save(&file, "PNG") && file.commit();
}

QString urlTag()
{
	return QStringLiteral("fit=%1x%2").arg(kMaxWidth).arg(kMaxHeight);
}

} // namespace FffAssetRendition
