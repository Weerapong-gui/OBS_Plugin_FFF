/*
FFF Tools for OBS - flag board session state
Copyright (C) 2026 Student Union MFU <studentunion.developer@gmail.com>
GPL-2.0-or-later
*/

#include "fff-session.h"

#include <obs-module.h>
#include <plugin-support.h>
#include <util/platform.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDateTime>
#include <QRandomGenerator>
#include <QSaveFile>
#include <QUuid>

FffSession::FffSession(QObject *parent) : QObject(parent) {}

QString FffSession::newId()
{
	return QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);
}

QString FffSession::voteName(FffVote vote)
{
	switch (vote) {
	case FffVote::Red:
		return QStringLiteral("red");
	case FffVote::Green:
		return QStringLiteral("green");
	default:
		return QStringLiteral("none");
	}
}

FffVote FffSession::voteFromName(const QString &name)
{
	if (name == QLatin1String("red"))
		return FffVote::Red;
	if (name == QLatin1String("green"))
		return FffVote::Green;
	return FffVote::None;
}

int FffSession::indexOf(const QString &id) const
{
	for (int i = 0; i < m_presidents.size(); ++i) {
		if (m_presidents[i].id == id)
			return i;
	}
	return -1;
}

const FffPresident *FffSession::presidentById(const QString &id) const
{
	const int index = indexOf(id);
	return index < 0 ? nullptr : &m_presidents[index];
}

const FffPresident *FffSession::presidentByPin(const QString &pin) const
{
	if (pin.isEmpty())
		return nullptr;
	for (const FffPresident &president : m_presidents) {
		if (president.pin == pin)
			return &president;
	}
	return nullptr;
}

QString FffSession::uniquePin() const
{
	for (int attempt = 0; attempt < 1000; ++attempt) {
		const QString pin = QString::number(QRandomGenerator::system()->bounded(100000, 1000000));
		if (!presidentByPin(pin))
			return pin;
	}
	return QString::number(QRandomGenerator::system()->bounded(100000, 1000000));
}

void FffSession::addPresident(const FffPresident &president)
{
	m_presidents.append(president);
	save();
	emit changed();
}

void FffSession::updatePresident(const FffPresident &president)
{
	const int index = indexOf(president.id);
	if (index < 0)
		return;
	m_presidents[index] = president;
	save();
	emit changed();
}

void FffSession::removePresident(const QString &id)
{
	const int index = indexOf(id);
	if (index < 0)
		return;

	const QString photo = photoPath(m_presidents[index]);
	if (!photo.isEmpty())
		QFile::remove(photo);

	m_presidents.removeAt(index);
	m_votes.remove(id);
	maybeAutoReveal();
	save();
	emit changed();
}

FffVote FffSession::voteOf(const QString &id) const
{
	return m_votes.value(id, FffVote::None);
}

int FffSession::votedCount() const
{
	int count = 0;
	for (const FffPresident &president : m_presidents) {
		if (m_votes.value(president.id, FffVote::None) != FffVote::None)
			++count;
	}
	return count;
}

bool FffSession::setVote(const QString &presidentId, FffVote vote)
{
	if (indexOf(presidentId) < 0)
		return false;

	if (vote == FffVote::None)
		m_votes.remove(presidentId);
	else
		m_votes.insert(presidentId, vote);

	maybeAutoReveal();
	save();
	emit changed();
	return true;
}

void FffSession::maybeAutoReveal()
{
	if (m_phase != FffPhase::Collecting)
		return;
	if (m_presidents.isEmpty())
		return;
	if (votedCount() == m_presidents.size())
		m_phase = FffPhase::Revealed;
}

void FffSession::forceReveal()
{
	if (m_phase == FffPhase::Revealed)
		return;
	m_phase = FffPhase::Revealed;
	save();
	emit changed();
}

void FffSession::clearRound()
{
	m_votes.clear();
	m_phase = FffPhase::Collecting;
	++m_round;
	save();
	emit changed();
}

void FffSession::setPort(quint16 port)
{
	if (m_port == port)
		return;
	m_port = port;
	save();
	emit changed();
}

QString FffSession::configDir() const
{
	char *path = obs_module_config_path("");
	const QString dir = QString::fromUtf8(path ? path : "");
	bfree(path);

	if (!dir.isEmpty())
		os_mkdirs(dir.toUtf8().constData());
	return dir;
}

QString FffSession::photosDir() const
{
	const QString dir = QDir(configDir()).filePath(QStringLiteral("photos"));
	os_mkdirs(dir.toUtf8().constData());
	return dir;
}

QString FffSession::photoPath(const FffPresident &president) const
{
	if (president.photo.isEmpty())
		return QString();
	return QDir(photosDir()).filePath(president.photo);
}

QString FffSession::photoUrl(const FffPresident &president) const
{
	const QString path = photoPath(president);
	if (path.isEmpty())
		return QString();

	// The mtime in the query string lets the overlay cache faces for a while
	// and still pick up a picture swapped mid-event.
	const QFileInfo info(path);
	if (!info.exists())
		return QString();
	return QStringLiteral("/api/photo/%1?v=%2").arg(president.id).arg(info.lastModified().toSecsSinceEpoch());
}

QString FffSession::importPhoto(const QString &sourcePath, const QString &presidentId)
{
	const QFileInfo source(sourcePath);
	if (!source.exists())
		return QString();

	QString suffix = source.suffix().toLower();
	if (suffix.isEmpty())
		suffix = QStringLiteral("png");

	// Same president, new picture: drop whatever was there before so a
	// changed extension does not leave an orphan behind.
	const int index = indexOf(presidentId);
	if (index >= 0) {
		const QString previous = photoPath(m_presidents[index]);
		if (!previous.isEmpty())
			QFile::remove(previous);
	}

	const QString name = presidentId + QLatin1Char('.') + suffix;
	const QString target = QDir(photosDir()).filePath(name);
	QFile::remove(target);
	if (!QFile::copy(sourcePath, target)) {
		obs_log(LOG_WARNING, "could not copy photo to %s", target.toUtf8().constData());
		return QString();
	}
	return name;
}

void FffSession::load()
{
	const QString path = QDir(configDir()).filePath(QStringLiteral("session.json"));
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly))
		return;

	const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
	file.close();
	if (!document.isObject())
		return;

	const QJsonObject root = document.object();

	m_presidents.clear();
	const QJsonArray presidents = root.value(QStringLiteral("presidents")).toArray();
	for (qsizetype i = 0; i < presidents.size(); ++i) {
		const QJsonObject entry = presidents.at(i).toObject();
		FffPresident president;
		president.id = entry.value(QStringLiteral("id")).toString();
		president.name = entry.value(QStringLiteral("name")).toString();
		president.school = entry.value(QStringLiteral("school")).toString();
		president.photo = entry.value(QStringLiteral("photo")).toString();
		president.pin = entry.value(QStringLiteral("pin")).toString();
		if (!president.id.isEmpty())
			m_presidents.append(president);
	}

	m_round = root.value(QStringLiteral("round")).toInt(1);
	m_phase = root.value(QStringLiteral("phase")).toString() == QLatin1String("revealed") ? FffPhase::Revealed
											     : FffPhase::Collecting;

	const int port = root.value(QStringLiteral("port")).toInt(9779);
	m_port = (port > 0 && port <= 65535) ? static_cast<quint16>(port) : 9779;

	m_votes.clear();
	const QJsonObject votes = root.value(QStringLiteral("votes")).toObject();
	for (auto it = votes.begin(); it != votes.end(); ++it) {
		const FffVote vote = voteFromName(it.value().toString());
		if (vote != FffVote::None && indexOf(it.key()) >= 0)
			m_votes.insert(it.key(), vote);
	}

	emit changed();
}

void FffSession::save() const
{
	QJsonArray presidents;
	for (const FffPresident &president : m_presidents) {
		QJsonObject entry;
		entry.insert(QStringLiteral("id"), president.id);
		entry.insert(QStringLiteral("name"), president.name);
		entry.insert(QStringLiteral("school"), president.school);
		entry.insert(QStringLiteral("photo"), president.photo);
		entry.insert(QStringLiteral("pin"), president.pin);
		presidents.append(entry);
	}

	QJsonObject votes;
	for (auto it = m_votes.begin(); it != m_votes.end(); ++it)
		votes.insert(it.key(), voteName(it.value()));

	QJsonObject root;
	root.insert(QStringLiteral("version"), 1);
	root.insert(QStringLiteral("port"), static_cast<int>(m_port));
	root.insert(QStringLiteral("round"), m_round);
	root.insert(QStringLiteral("phase"),
		    m_phase == FffPhase::Revealed ? QStringLiteral("revealed") : QStringLiteral("collecting"));
	root.insert(QStringLiteral("presidents"), presidents);
	root.insert(QStringLiteral("votes"), votes);

	const QString path = QDir(configDir()).filePath(QStringLiteral("session.json"));
	QSaveFile file(path);
	if (!file.open(QIODevice::WriteOnly)) {
		obs_log(LOG_WARNING, "could not write %s", path.toUtf8().constData());
		return;
	}
	file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
	if (!file.commit())
		obs_log(LOG_WARNING, "could not commit %s", path.toUtf8().constData());
}

QByteArray FffSession::overlayStateJson() const
{
	QJsonArray presidents;
	for (const FffPresident &president : m_presidents) {
		QJsonObject entry;
		entry.insert(QStringLiteral("id"), president.id);
		entry.insert(QStringLiteral("name"), president.name);
		entry.insert(QStringLiteral("school"), president.school);
		entry.insert(QStringLiteral("photoUrl"), photoUrl(president));
		entry.insert(QStringLiteral("vote"), voteName(voteOf(president.id)));
		presidents.append(entry);
	}

	QJsonObject root;
	root.insert(QStringLiteral("phase"),
		    m_phase == FffPhase::Revealed ? QStringLiteral("revealed") : QStringLiteral("collecting"));
	root.insert(QStringLiteral("round"), m_round);
	root.insert(QStringLiteral("total"), m_presidents.size());
	root.insert(QStringLiteral("voted"), votedCount());
	root.insert(QStringLiteral("presidents"), presidents);
	return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

QByteArray FffSession::phoneStateJson(const QString &presidentId) const
{
	QJsonObject root;
	root.insert(QStringLiteral("phase"),
		    m_phase == FffPhase::Revealed ? QStringLiteral("revealed") : QStringLiteral("collecting"));
	root.insert(QStringLiteral("round"), m_round);
	root.insert(QStringLiteral("total"), m_presidents.size());
	root.insert(QStringLiteral("voted"), votedCount());

	// Only ever the caller's own president: nobody on a phone gets to peek
	// at the other flags before the reveal.
	const FffPresident *president = presidentById(presidentId);
	if (president) {
		QJsonObject you;
		you.insert(QStringLiteral("id"), president->id);
		you.insert(QStringLiteral("name"), president->name);
		you.insert(QStringLiteral("school"), president->school);
		you.insert(QStringLiteral("photoUrl"), photoUrl(*president));
		you.insert(QStringLiteral("vote"), voteName(voteOf(president->id)));
		root.insert(QStringLiteral("you"), you);
	}
	return QJsonDocument(root).toJson(QJsonDocument::Compact);
}
