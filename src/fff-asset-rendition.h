/*
FFF Tools for OBS - shrunk copies of artwork for the 1920x1080 stream
Copyright (C) 2026 Student Union MFU <studentunion.developer@gmail.com>
GPL-2.0-or-later
*/

#pragma once

#include <QSize>
#include <QString>

/*
 * Artwork arrives at whatever size the designer exported. A 4500x8000 card
 * shown 250px tall still costs the browser a full 144 MB decode the first
 * time it is drawn, which froze the first reveal for half a second. The
 * server therefore hands pages a copy no larger than the stream itself; the
 * imported original is never touched.
 */
namespace FffAssetRendition {

// The largest box any asset can fill on the 1920x1080 Browser Source.
constexpr int kMaxWidth = 1920;
constexpr int kMaxHeight = 1080;

// `source` scaled down to fit the stream, keeping its aspect ratio. Sizes
// that already fit, and invalid sizes, come back unchanged.
QSize fittedSize(const QSize &source);
// Reads only the PNG header, so it is cheap enough for the UI thread.
bool needsRendition(const QString &path);
// "<dir>/renditions/<file name>" next to the original.
QString renditionPath(const QString &sourcePath);
// Decodes, shrinks and writes a PNG atomically. Slow for large artwork: call
// it off the UI thread.
bool writeRendition(const QString &sourcePath, const QString &targetPath);
// Appended to asset URLs. It changes whenever the bounds do, so a browser
// cache still holding full-size bytes under the old URL is never reused.
QString urlTag();

} // namespace FffAssetRendition
