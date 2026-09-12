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

#include <algorithm>
#include <cmath>

bool FffSession::validCardTemplate(const QJsonObject &value)
{
	for (const QString &name : {QStringLiteral("image"), QStringLiteral("result")}) {
		const auto layer = value.value(name).toObject();
		for (const QString &key :
		     {QStringLiteral("x"), QStringLiteral("y"), QStringLiteral("width"), QStringLiteral("height")}) {
			const auto number = layer.value(key);
			if (!number.isDouble() || !std::isfinite(number.toDouble()))
				return false;
			const double n = number.toDouble();
			if (key == QLatin1String("x") || key == QLatin1String("y")) {
				if (n < -2000 || n > 2000)
					return false;
			} else if (n < 1 || n > 2000)
				return false;
		}
	}
	const auto order = value.value(QStringLiteral("order")).toArray();
	return order.size() == 2 && ((order.at(0).toString() == QLatin1String("result") &&
				      order.at(1).toString() == QLatin1String("image")) ||
				     (order.at(0).toString() == QLatin1String("image") &&
				      order.at(1).toString() == QLatin1String("result")));
}

bool FffSession::setCardTemplate(const QJsonObject &value)
{
	if (!validCardTemplate(value))
		return false;
	const auto previous = m_cardTemplate;
	m_cardTemplate = value;
	if (!save()) {
		m_cardTemplate = previous;
		return false;
	}
	emit changed();
	return true;
}

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
	if (!m_pieceLayers.isEmpty()) {
		int topLayer = 0;
		for (auto it = m_pieceLayers.cbegin(); it != m_pieceLayers.cend(); ++it)
			topLayer = std::max(topLayer, it.value());
		m_pieceLayers.insert(QStringLiteral("card:") + president.id, topLayer + 1);
	}
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

	const QString card = cardPath(m_presidents[index]);
	if (!card.isEmpty())
		QFile::remove(card);

	m_presidents.removeAt(index);
	m_votes.remove(id);
	m_pieceLayouts.remove(QStringLiteral("card:") + id);
	m_pieceLayers.remove(QStringLiteral("card:") + id);
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

	save();
	emit changed();
	return true;
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

void FffSession::setLayout(const FffLayout &layout)
{
	m_layout.x = qBound(0.0, layout.x, 1.0);
	m_layout.y = qBound(0.0, layout.y, 1.0);
	m_layout.scale = qBound(0.5, layout.scale, 2.0);
	save();
	emit changed();
}

bool FffSession::setPieceLayout(const QString &target, const FffLayout *layout)
{
	const auto previous = m_pieceLayouts;
	if (layout)
		m_pieceLayouts.insert(target, *layout);
	else
		m_pieceLayouts.remove(target);
	if (!save()) {
		m_pieceLayouts = previous;
		return false;
	}
	emit changed();
	return true;
}

bool FffSession::resetLayouts()
{
	const auto previous = m_pieceLayouts;
	const auto previousLayers = m_pieceLayers;
	const FffLayout previousLayout = m_layout;
	m_pieceLayouts.clear();
	m_pieceLayers.clear();
	m_layout = FffLayout();
	if (!save()) {
		m_pieceLayouts = previous;
		m_pieceLayers = previousLayers;
		m_layout = previousLayout;
		return false;
	}
	emit changed();
	return true;
}

bool FffSession::movePieceLayer(const QString &target, const QString &action)
{
	struct LayeredPiece {
		QString target;
		int layer = 0;
		int naturalOrder = 0;
	};

	QVector<LayeredPiece> pieces;
	pieces.append({QStringLiteral("heading"), m_pieceLayers.value(QStringLiteral("heading"), 0), 0});
	for (int index = 0; index < m_presidents.size(); ++index) {
		const QString key = QStringLiteral("card:") + m_presidents[index].id;
		pieces.append({key, m_pieceLayers.value(key, index + 1), index + 1});
	}

	int current = -1;
	for (int index = 0; index < pieces.size(); ++index) {
		if (pieces[index].target == target) {
			current = index;
			break;
		}
	}
	if (current < 0)
		return false;

	std::stable_sort(pieces.begin(), pieces.end(), [](const LayeredPiece &left, const LayeredPiece &right) {
		return left.layer == right.layer ? left.naturalOrder < right.naturalOrder : left.layer < right.layer;
	});
	for (int index = 0; index < pieces.size(); ++index) {
		if (pieces[index].target == target) {
			current = index;
			break;
		}
	}

	if (action == QLatin1String("front")) {
		std::rotate(pieces.begin() + current, pieces.begin() + current + 1, pieces.end());
	} else if (action == QLatin1String("forward")) {
		if (current + 1 < pieces.size())
			std::swap(pieces[current], pieces[current + 1]);
	} else if (action == QLatin1String("backward")) {
		if (current > 0)
			std::swap(pieces[current], pieces[current - 1]);
	} else if (action == QLatin1String("back")) {
		std::rotate(pieces.begin(), pieces.begin() + current, pieces.begin() + current + 1);
	} else {
		return false;
	}

	const auto previous = m_pieceLayers;
	m_pieceLayers.clear();
	for (int index = 0; index < pieces.size(); ++index)
		m_pieceLayers.insert(pieces[index].target, index);
	if (!save()) {
		m_pieceLayers = previous;
		return false;
	}
	emit changed();
	return true;
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

QString FffSession::cardsDir() const
{
	const QString dir = QDir(configDir()).filePath(QStringLiteral("cards"));
	os_mkdirs(dir.toUtf8().constData());
	return dir;
}

QString FffSession::cardPath(const FffPresident &president) const
{
	if (president.card.isEmpty())
		return QString();
	return QDir(cardsDir()).filePath(president.card);
}

QString FffSession::cardUrl(const FffPresident &president) const
{
	const QString path = cardPath(president);
	if (path.isEmpty())
		return QString();

	// The mtime lets the renderer cache a card while still picking up a
	// replacement made during an event.
	const QFileInfo info(path);
	if (!info.exists())
		return QString();
	return QStringLiteral("/api/card/%1?v=%2").arg(president.id).arg(info.lastModified().toSecsSinceEpoch());
}

QString FffSession::importCard(const QString &sourcePath, const QString &presidentId)
{
	const QFileInfo source(sourcePath);
	if (!source.exists() || source.suffix().compare(QLatin1String("png"), Qt::CaseInsensitive) != 0)
		return QString();

	// Same president, new card: replace the old file without keeping copies.
	const int index = indexOf(presidentId);
	if (index >= 0) {
		const QString previous = cardPath(m_presidents[index]);
		if (!previous.isEmpty())
			QFile::remove(previous);
	}

	const QString name = presidentId + QStringLiteral(".png");
	const QString target = QDir(cardsDir()).filePath(name);
	QFile::remove(target);
	if (!QFile::copy(sourcePath, target)) {
		obs_log(LOG_WARNING, "could not copy card to %s", target.toUtf8().constData());
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
	const auto cardTemplate = root.value(QStringLiteral("cardTemplate")).toObject();
	m_cardTemplate = validCardTemplate(cardTemplate) ? cardTemplate : QJsonObject();

	m_presidents.clear();
	const QJsonArray presidents = root.value(QStringLiteral("presidents")).toArray();
	for (qsizetype i = 0; i < presidents.size(); ++i) {
		const QJsonObject entry = presidents.at(i).toObject();
		FffPresident president;
		president.id = entry.value(QStringLiteral("id")).toString();
		president.name = entry.value(QStringLiteral("name")).toString();
		president.school = entry.value(QStringLiteral("school")).toString();
		// Old "photo" assets were person portraits, not finished card art.
		// Deliberately leave them unused instead of stretching them as cards.
		president.card = entry.value(QStringLiteral("card")).toString();
		president.pin = entry.value(QStringLiteral("pin")).toString();
		if (!president.id.isEmpty())
			m_presidents.append(president);
	}

	m_round = root.value(QStringLiteral("round")).toInt(1);
	m_phase = root.value(QStringLiteral("phase")).toString() == QLatin1String("revealed") ? FffPhase::Revealed
											      : FffPhase::Collecting;

	const int port = root.value(QStringLiteral("port")).toInt(9779);
	m_port = (port > 0 && port <= 65535) ? static_cast<quint16>(port) : 9779;

	const QJsonObject layout = root.value(QStringLiteral("layout")).toObject();
	m_layout.x = qBound(0.0, layout.value(QStringLiteral("x")).toDouble(0.5), 1.0);
	m_layout.y = qBound(0.0, layout.value(QStringLiteral("y")).toDouble(0.5), 1.0);
	m_layout.scale = qBound(0.5, layout.value(QStringLiteral("scale")).toDouble(1.0), 2.0);

	m_pieceLayouts.clear();
	const QJsonObject pieces = root.value(QStringLiteral("pieces")).toObject();
	for (auto it = pieces.begin(); it != pieces.end(); ++it) {
		if (it.key() != QLatin1String("heading") &&
		    (!it.key().startsWith(QLatin1String("card:")) || indexOf(it.key().mid(5)) < 0))
			continue;
		const QJsonObject value = it.value().toObject();
		FffLayout piece;
		piece.x = qBound(0.0, value.value(QStringLiteral("x")).toDouble(0.5), 1.0);
		piece.y = qBound(0.0, value.value(QStringLiteral("y")).toDouble(0.5), 1.0);
		piece.scale = qBound(0.5, value.value(QStringLiteral("scale")).toDouble(1.0), 2.0);
		piece.scaleX = qBound(0.5, value.value(QStringLiteral("scaleX")).toDouble(piece.scale), 2.0);
		piece.scaleY = qBound(0.5, value.value(QStringLiteral("scaleY")).toDouble(piece.scale), 2.0);
		piece.resultScaleX = qBound(0.5, value.value(QStringLiteral("resultScaleX")).toDouble(1.0), 2.0);
		piece.resultScaleY = qBound(0.5, value.value(QStringLiteral("resultScaleY")).toDouble(1.0), 2.0);
		m_pieceLayouts.insert(it.key(), piece);
	}

	m_pieceLayers.clear();
	const QJsonObject layers = root.value(QStringLiteral("layers")).toObject();
	for (auto it = layers.begin(); it != layers.end(); ++it) {
		if (!it.value().isDouble())
			continue;
		if (it.key() != QLatin1String("heading") &&
		    (!it.key().startsWith(QLatin1String("card:")) || indexOf(it.key().mid(5)) < 0))
			continue;
		m_pieceLayers.insert(it.key(), qBound(-10000, it.value().toInt(), 10000));
	}

	m_votes.clear();
	const QJsonObject votes = root.value(QStringLiteral("votes")).toObject();
	for (auto it = votes.begin(); it != votes.end(); ++it) {
		const FffVote vote = voteFromName(it.value().toString());
		if (vote != FffVote::None && indexOf(it.key()) >= 0)
			m_votes.insert(it.key(), vote);
	}

	emit changed();
}

bool FffSession::save() const
{
	QJsonArray presidents;
	for (const FffPresident &president : m_presidents) {
		QJsonObject entry;
		entry.insert(QStringLiteral("id"), president.id);
		entry.insert(QStringLiteral("name"), president.name);
		entry.insert(QStringLiteral("school"), president.school);
		entry.insert(QStringLiteral("card"), president.card);
		entry.insert(QStringLiteral("pin"), president.pin);
		presidents.append(entry);
	}

	QJsonObject votes;
	for (auto it = m_votes.begin(); it != m_votes.end(); ++it)
		votes.insert(it.key(), voteName(it.value()));

	QJsonObject root;
	root.insert(QStringLiteral("version"), 3);
	if (!m_cardTemplate.isEmpty())
		root.insert(QStringLiteral("cardTemplate"), m_cardTemplate);
	root.insert(QStringLiteral("port"), static_cast<int>(m_port));
	root.insert(QStringLiteral("round"), m_round);
	root.insert(QStringLiteral("phase"),
		    m_phase == FffPhase::Revealed ? QStringLiteral("revealed") : QStringLiteral("collecting"));
	QJsonObject layout;
	layout.insert(QStringLiteral("x"), m_layout.x);
	layout.insert(QStringLiteral("y"), m_layout.y);
	layout.insert(QStringLiteral("scale"), m_layout.scale);

	root.insert(QStringLiteral("layout"), layout);
	QJsonObject pieces;
	for (auto it = m_pieceLayouts.begin(); it != m_pieceLayouts.end(); ++it) {
		QJsonObject piece;
		piece.insert(QStringLiteral("x"), it.value().x);
		piece.insert(QStringLiteral("y"), it.value().y);
		piece.insert(QStringLiteral("scale"), it.value().scale);
		piece.insert(QStringLiteral("scaleX"), it.value().scaleX);
		piece.insert(QStringLiteral("scaleY"), it.value().scaleY);
		piece.insert(QStringLiteral("resultScaleX"), it.value().resultScaleX);
		piece.insert(QStringLiteral("resultScaleY"), it.value().resultScaleY);
		pieces.insert(it.key(), piece);
	}
	root.insert(QStringLiteral("pieces"), pieces);
	QJsonObject layers;
	for (auto it = m_pieceLayers.cbegin(); it != m_pieceLayers.cend(); ++it)
		layers.insert(it.key(), it.value());
	root.insert(QStringLiteral("layers"), layers);
	root.insert(QStringLiteral("presidents"), presidents);
	root.insert(QStringLiteral("votes"), votes);

	const QString path = QDir(configDir()).filePath(QStringLiteral("session.json"));
	QSaveFile file(path);
	if (!file.open(QIODevice::WriteOnly)) {
		obs_log(LOG_WARNING, "could not write %s", path.toUtf8().constData());
		return false;
	}
	const QByteArray data = QJsonDocument(root).toJson(QJsonDocument::Indented);
	if (file.write(data) != data.size() || !file.commit()) {
		obs_log(LOG_WARNING, "could not commit %s", path.toUtf8().constData());
		return false;
	}
	return true;
}

QByteArray FffSession::overlayStateJson() const
{
	QJsonArray presidents;
	for (const FffPresident &president : m_presidents) {
		QJsonObject entry;
		entry.insert(QStringLiteral("id"), president.id);
		entry.insert(QStringLiteral("name"), president.name);
		entry.insert(QStringLiteral("school"), president.school);
		entry.insert(QStringLiteral("cardUrl"), cardUrl(president));
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

	QJsonObject layout;
	if (!m_cardTemplate.isEmpty())
		root.insert(QStringLiteral("cardTemplate"), m_cardTemplate);
	layout.insert(QStringLiteral("x"), m_layout.x);
	layout.insert(QStringLiteral("y"), m_layout.y);
	layout.insert(QStringLiteral("scale"), m_layout.scale);
	root.insert(QStringLiteral("layout"), layout);
	QJsonObject pieces;
	for (auto it = m_pieceLayouts.begin(); it != m_pieceLayouts.end(); ++it) {
		QJsonObject piece;
		piece.insert(QStringLiteral("x"), it.value().x);
		piece.insert(QStringLiteral("y"), it.value().y);
		piece.insert(QStringLiteral("scale"), it.value().scale);
		piece.insert(QStringLiteral("scaleX"), it.value().scaleX);
		piece.insert(QStringLiteral("scaleY"), it.value().scaleY);
		piece.insert(QStringLiteral("resultScaleX"), it.value().resultScaleX);
		piece.insert(QStringLiteral("resultScaleY"), it.value().resultScaleY);
		pieces.insert(it.key(), piece);
	}
	root.insert(QStringLiteral("pieces"), pieces);
	QJsonObject layers;
	for (auto it = m_pieceLayers.cbegin(); it != m_pieceLayers.cend(); ++it)
		layers.insert(it.key(), it.value());
	root.insert(QStringLiteral("layers"), layers);

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
		you.insert(QStringLiteral("vote"), voteName(voteOf(president->id)));
		root.insert(QStringLiteral("you"), you);
	}
	return QJsonDocument(root).toJson(QJsonDocument::Compact);
}
