/*
FFF Tools for OBS - flag board session state
Copyright (C) 2026 Student Union MFU <studentunion.developer@gmail.com>
GPL-2.0-or-later
*/

#include "fff-session.h"

#include "fff-asset-rendition.h"
#include "fff-monitor-access.h"

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
#include <QRegularExpression>
#include <QSaveFile>
#include <QUuid>

#include <algorithm>
#include <cmath>

// One box of a template: a position and a size in the piece's own pixels, with
// an optional opacity. Shared by the card, logo and count templates so a box
// can never mean one thing in one panel and something else in another.
bool FffSession::validTemplateBox(const QJsonObject &layer)
{
	if (layer.contains(QStringLiteral("opacity"))) {
		const auto opacity = layer.value(QStringLiteral("opacity"));
		if (!opacity.isDouble() || !std::isfinite(opacity.toDouble()) || opacity.toDouble() < 0 ||
		    opacity.toDouble() > 1)
			return false;
	}
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
	return true;
}

bool FffSession::validCardTemplate(const QJsonObject &value)
{
	for (const QString &name : {QStringLiteral("image"), QStringLiteral("result")}) {
		if (!validTemplateBox(value.value(name).toObject()))
			return false;
	}
	const auto order = value.value(QStringLiteral("order")).toArray();
	return order.size() == 2 && ((order.at(0).toString() == QLatin1String("result") &&
				      order.at(1).toString() == QLatin1String("image")) ||
				     (order.at(0).toString() == QLatin1String("image") &&
				      order.at(1).toString() == QLatin1String("result")));
}

bool FffSession::validLogoTemplate(const QJsonObject &value)
{
	return validTemplateBox(value.value(QStringLiteral("image")).toObject());
}

// The operator picks a font that is installed on this machine, so the family
// name travels as text. Empty means the page's own stack. Everything that could
// break out of a CSS font-family value is refused, Thai and other non-ASCII
// family names are not.
bool FffSession::validFontFamily(const QString &family)
{
	if (family.size() > 120)
		return false;
	for (const QChar character : family) {
		if (character.unicode() < 0x20 || QStringLiteral("\"';{}<>\\/()").contains(character))
			return false;
	}
	return true;
}

bool FffSession::validHexColor(const QString &color)
{
	static const QRegularExpression pattern(QStringLiteral("^#[0-9a-fA-F]{6}$"));
	return pattern.match(color).hasMatch();
}

bool FffSession::validCountTemplate(const QJsonObject &value)
{
	if (!validTemplateBox(value.value(QStringLiteral("value")).toObject()))
		return false;
	const auto family = value.value(QStringLiteral("fontFamily"));
	if (!family.isString() || !validFontFamily(family.toString()))
		return false;
	const auto colors = value.value(QStringLiteral("colors")).toObject();
	for (const QString &vote : {QStringLiteral("red"), QStringLiteral("green")}) {
		if (!validHexColor(colors.value(vote).toString()))
			return false;
	}
	// The stylesheet used to pin the weight at 700, which is why the operator's
	// choice never reached the screen. It belongs to the template now.
	const auto weight = value.value(QStringLiteral("fontWeight"));
	if (!weight.isDouble() || !std::isfinite(weight.toDouble()) || weight.toDouble() < 100 ||
	    weight.toDouble() > 900)
		return false;
	const auto size = value.value(QStringLiteral("fontSize"));
	return size.isDouble() && std::isfinite(size.toDouble()) && size.toDouble() >= 24 && size.toDouble() <= 400;
}

bool FffSession::setCardTemplate(const QJsonObject &value, const QString &mode)
{
	if (!validMode(mode))
		return false;
	auto &m_cardTemplate = mode == QLatin1String("bottomBar") ? m_bottomTemplate : this->m_cardTemplate;
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

// The centre logo and the two flag counters are Bottom Bar furniture with no
// counterpart on the scoreboard, so each keeps its own template instead of
// borrowing the card one.
bool FffSession::setBottomTemplate(const QString &piece, const QJsonObject &value)
{
	const bool logo = piece == QLatin1String("logo");
	if (!logo && piece != QLatin1String("count"))
		return false;
	if (!(logo ? validLogoTemplate(value) : validCountTemplate(value)))
		return false;
	auto &target = logo ? m_bottomLogoTemplate : m_bottomCountTemplate;
	const auto previous = target;
	target = value;
	if (!save()) {
		target = previous;
		return false;
	}
	emit changed();
	return true;
}

// Every piece the Bottom Bar and the scoreboard can position or stack. Keeping
// the one list here stops the HTTP validation and the session loader from
// disagreeing about what exists.
bool FffSession::validPieceTarget(const QString &mode, const QString &target) const
{
	const bool bottom = mode == QLatin1String("bottomBar");
	if (target == (bottom ? QLatin1String("logo") : QLatin1String("heading")))
		return true;
	if (bottom && (target == QLatin1String("cover") || target == QLatin1String("count:red") ||
		       target == QLatin1String("count:green")))
		return true;
	return target.startsWith(QLatin1String("card:")) && indexOf(target.mid(5)) >= 0;
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

QString FffSession::statusName(FffStatus status)
{
	switch (status) {
	case FffStatus::Qualified:
		return QStringLiteral("qualified");
	case FffStatus::Unqualified:
		return QStringLiteral("unqualified");
	default:
		return QStringLiteral("waiting");
	}
}

FffStatus FffSession::statusFromName(const QString &name)
{
	if (name == QLatin1String("qualified"))
		return FffStatus::Qualified;
	if (name == QLatin1String("unqualified"))
		return FffStatus::Unqualified;
	return FffStatus::Waiting;
}

// statusFromName falls back to waiting, so callers that must reject a typo ask
// this first rather than silently storing the wrong status.
bool FffSession::validStatusName(const QString &name)
{
	return name == QLatin1String("waiting") || name == QLatin1String("unqualified") ||
	       name == QLatin1String("qualified");
}

QString FffSession::statusUrl(const FffPresident &president) const
{
	const QString url = assetUrl(president, statusName(president.status));
	return url.isEmpty() ? cardUrl(president) : url;
}

bool FffSession::setStatus(const QString &presidentId, FffStatus status)
{
	const int index = indexOf(presidentId);
	if (index < 0)
		return false;
	const FffStatus previous = m_presidents[index].status;
	if (previous == status)
		return true;
	m_presidents[index].status = status;
	if (!save()) {
		m_presidents[index].status = previous;
		emit saveFailed();
		return false;
	}
	emit changed();
	return true;
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

bool FffSession::addPresident(const FffPresident &president)
{
	const auto previous_m_presidents = m_presidents;
	const auto previous_m_pieceLayers = m_pieceLayers;
	const auto previousBottomLayers = m_bottomLayers;
	if (president.id.isEmpty() || indexOf(president.id) >= 0)
		return false;
	m_presidents.append(president);
	if (!m_pieceLayers.isEmpty()) {
		int topLayer = 0;
		for (auto it = m_pieceLayers.cbegin(); it != m_pieceLayers.cend(); ++it)
			topLayer = std::max(topLayer, it.value());
		m_pieceLayers.insert(QStringLiteral("card:") + president.id, topLayer + 1);
	}
	if (!m_bottomLayers.isEmpty()) {
		int topLayer = 0;
		for (auto it = m_bottomLayers.cbegin(); it != m_bottomLayers.cend(); ++it)
			topLayer = std::max(topLayer, it.value());
		m_bottomLayers.insert(QStringLiteral("card:") + president.id, topLayer + 1);
	}
	if (!save()) {
		m_presidents = previous_m_presidents;
		m_pieceLayers = previous_m_pieceLayers;
		m_bottomLayers = previousBottomLayers;
		emit saveFailed();
		return false;
	}
	emit changed();
	return true;
}

bool FffSession::updatePresident(const FffPresident &president)
{
	const auto previous_m_presidents = m_presidents;
	const int index = indexOf(president.id);
	if (index < 0)
		return false;
	m_presidents[index] = president;
	if (!save()) {
		m_presidents = previous_m_presidents;
		emit saveFailed();
		return false;
	}
	emit changed();
	return true;
}

bool FffSession::removePresident(const QString &id)
{
	const auto previous_m_presidents = m_presidents;
	const auto previous_m_votes = m_votes;
	const auto previous_m_pieceLayouts = m_pieceLayouts;
	const auto previous_m_pieceLayers = m_pieceLayers;
	const auto previous_m_bottomPieces = m_bottomPieces;
	const auto previous_m_bottomLayers = m_bottomLayers;
	const auto previous_m_logoPresidentId = m_logoPresidentId;
	const int index = indexOf(id);
	if (index < 0)
		return false;

	m_presidents.removeAt(index);
	m_votes.remove(id);
	m_pieceLayouts.remove(QStringLiteral("card:") + id);
	m_pieceLayers.remove(QStringLiteral("card:") + id);
	m_bottomPieces.remove(QStringLiteral("card:") + id);
	m_bottomLayers.remove(QStringLiteral("card:") + id);
	if (m_logoPresidentId == id)
		m_logoPresidentId.clear();
	if (!save()) {
		m_presidents = previous_m_presidents;
		m_votes = previous_m_votes;
		m_pieceLayouts = previous_m_pieceLayouts;
		m_pieceLayers = previous_m_pieceLayers;
		m_bottomPieces = previous_m_bottomPieces;
		m_bottomLayers = previous_m_bottomLayers;
		m_logoPresidentId = previous_m_logoPresidentId;
		emit saveFailed();
		return false;
	}
	emit changed();
	return true;
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
	const auto previous_m_votes = m_votes;
	if (indexOf(presidentId) < 0)
		return false;

	if (vote == FffVote::None)
		m_votes.remove(presidentId);
	else
		m_votes.insert(presidentId, vote);

	if (!save()) {
		m_votes = previous_m_votes;
		emit saveFailed();
		return false;
	}
	emit changed();
	return true;
}

bool FffSession::forceReveal()
{
	const auto previous_m_phase = m_phase;
	if (m_phase == FffPhase::Revealed)
		return true;
	m_phase = FffPhase::Revealed;
	if (!save()) {
		m_phase = previous_m_phase;
		emit saveFailed();
		return false;
	}
	emit changed();
	return true;
}

bool FffSession::clearRound()
{
	const auto previous_m_votes = m_votes;
	const auto previous_m_phase = m_phase;
	const auto previous_m_round = m_round;
	m_votes.clear();
	m_phase = FffPhase::Collecting;
	++m_round;
	if (!save()) {
		m_votes = previous_m_votes;
		m_phase = previous_m_phase;
		m_round = previous_m_round;
		emit saveFailed();
		return false;
	}
	emit changed();
	return true;
}

bool FffSession::setPort(quint16 port)
{
	const auto previous_m_port = m_port;
	if (m_port == port)
		return true;
	m_port = port;
	if (!save()) {
		m_port = previous_m_port;
		emit saveFailed();
		return false;
	}
	emit changed();
	return true;
}

bool FffSession::setMonitorLanEnabled(bool enabled)
{
	if (m_monitorLanEnabled == enabled && (!enabled || !m_monitorKey.isEmpty()))
		return true;
	const auto previousEnabled = m_monitorLanEnabled;
	const auto previousKey = m_monitorKey;
	m_monitorLanEnabled = enabled;
	if (enabled && m_monitorKey.isEmpty())
		m_monitorKey = FffMonitorAccess::generateKey();
	if (!save()) {
		m_monitorLanEnabled = previousEnabled;
		m_monitorKey = previousKey;
		emit saveFailed();
		return false;
	}
	emit changed();
	emit monitorAccessChanged();
	return true;
}

bool FffSession::regenerateMonitorKey()
{
	const auto previousKey = m_monitorKey;
	m_monitorKey = FffMonitorAccess::generateKey();
	if (!save()) {
		m_monitorKey = previousKey;
		emit saveFailed();
		return false;
	}
	emit changed();
	emit monitorAccessChanged();
	return true;
}

bool FffSession::setLayout(const FffLayout &layout, const QString &mode)
{
	if (!validMode(mode))
		return false;
	auto &m_layout = mode == QLatin1String("bottomBar") ? m_bottomLayout : this->m_layout;
	const auto previous_m_layout = m_layout;
	m_layout.x = qBound(0.0, layout.x, 1.0);
	m_layout.y = qBound(0.0, layout.y, 1.0);
	m_layout.scale = qBound(0.5, layout.scale, 2.0);
	if (!save()) {
		m_layout = previous_m_layout;
		emit saveFailed();
		return false;
	}
	emit changed();
	return true;
}

bool FffSession::setPieceLayout(const QString &target, const FffLayout *layout, const QString &mode)
{
	if (!validMode(mode))
		return false;
	auto &m_pieceLayouts = mode == QLatin1String("bottomBar") ? m_bottomPieces : this->m_pieceLayouts;
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

bool FffSession::resetLayouts(const QString &mode)
{
	if (!validMode(mode))
		return false;
	auto &m_pieceLayouts = mode == QLatin1String("bottomBar") ? m_bottomPieces : this->m_pieceLayouts;
	auto &m_pieceLayers = mode == QLatin1String("bottomBar") ? m_bottomLayers : this->m_pieceLayers;
	auto &m_layout = mode == QLatin1String("bottomBar") ? m_bottomLayout : this->m_layout;
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

bool FffSession::movePieceLayer(const QString &target, const QString &action, const QString &mode)
{
	if (!validMode(mode))
		return false;
	auto &m_pieceLayers = mode == QLatin1String("bottomBar") ? m_bottomLayers : this->m_pieceLayers;
	struct LayeredPiece {
		QString target;
		int layer = 0;
		int naturalOrder = 0;
	};

	QVector<LayeredPiece> pieces;
	const QString special = mode == QLatin1String("bottomBar") ? QStringLiteral("logo") : QStringLiteral("heading");
	pieces.append({special, m_pieceLayers.value(special, 0), 0});
	for (int index = 0; index < m_presidents.size(); ++index) {
		const QString key = QStringLiteral("card:") + m_presidents[index].id;
		pieces.append({key, m_pieceLayers.value(key, index + 1), index + 1});
	}

	if (mode == QLatin1String("bottomBar")) {
		int natural = m_presidents.size() + 1;
		for (const QString &key :
		     {QStringLiteral("count:red"), QStringLiteral("count:green"), QStringLiteral("cover")}) {
			pieces.append({key, m_pieceLayers.value(key, natural), natural});
			++natural;
		}
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

QString FffSession::assetPath(const FffPresident &president, const QString &kind) const
{
	const QString name = kind == QLatin1String("card")          ? president.card
			     : kind == QLatin1String("bottomBar")   ? president.bottomBar
			     : kind == QLatin1String("logo")        ? president.logo
			     : kind == QLatin1String("logo2")       ? president.logo2
			     : kind == QLatin1String("qualified")   ? president.qualified
			     : kind == QLatin1String("unqualified") ? president.unqualified
			     : kind == QLatin1String("waiting")     ? president.waiting
								    : QString();
	if (name.isEmpty() || QFileInfo(name).fileName() != name)
		return QString();
	return QDir(cardsDir()).filePath(name);
}

QString FffSession::assetUrl(const FffPresident &president, const QString &kind) const
{
	const QString path = assetPath(president, kind);
	if (path.isEmpty() || !QFileInfo::exists(path))
		return QString();
	return QStringLiteral("/api/%1/%2?v=%3&%4")
		.arg(kind, president.id, QFileInfo(path).fileName(), FffAssetRendition::urlTag());
}

QString FffSession::coverPath() const
{
	if (m_cover.isEmpty() || QFileInfo(m_cover).fileName() != m_cover)
		return QString();
	return QDir(cardsDir()).filePath(m_cover);
}

QString FffSession::coverUrl() const
{
	const QString path = coverPath();
	return path.isEmpty() || !QFileInfo::exists(path)
		       ? QString()
		       : QStringLiteral("/api/cover?v=%1&%2").arg(m_cover, FffAssetRendition::urlTag());
}

QStringList FffSession::assetPaths() const
{
	QStringList paths;
	for (const FffPresident &president : m_presidents) {
		for (const QString &kind : {QStringLiteral("card"), QStringLiteral("bottomBar"), QStringLiteral("logo"),
					    QStringLiteral("logo2"), QStringLiteral("qualified"),
					    QStringLiteral("unqualified"), QStringLiteral("waiting")}) {
			const QString path = assetPath(president, kind);
			if (!path.isEmpty())
				paths.append(path);
		}
	}
	const QString cover = coverPath();
	if (!cover.isEmpty())
		paths.append(cover);
	return paths;
}

bool FffSession::setCover(const QString &fileName)
{
	if (!fileName.isEmpty() &&
	    (QFileInfo(fileName).fileName() != fileName || !QFileInfo::exists(QDir(cardsDir()).filePath(fileName))))
		return false;
	const auto previous = m_cover;
	m_cover = fileName;
	if (!save()) {
		m_cover = previous;
		emit saveFailed();
		return false;
	}
	emit changed();
	return true;
}

QString FffSession::cardPath(const FffPresident &president) const
{
	return assetPath(president, QStringLiteral("card"));
}
QString FffSession::cardUrl(const FffPresident &president) const
{
	return assetUrl(president, QStringLiteral("card"));
}
QString FffSession::importCard(const QString &path, const QString &id)
{
	return importAsset(path, id, QStringLiteral("card"));
}

QString FffSession::storeAsset(const QByteArray &png, const QString &kind)
{
	static const QStringList kinds = {QStringLiteral("card"),        QStringLiteral("bottomBar"),
					  QStringLiteral("logo"),        QStringLiteral("logo2"),
					  QStringLiteral("cover"),       QStringLiteral("qualified"),
					  QStringLiteral("unqualified"), QStringLiteral("waiting")};
	if (!kinds.contains(kind))
		return QString();
	// Only real PNGs get in, whether they arrive from the file picker or over
	// the wire: the overlay depends on transparency and nothing else is served.
	if (!png.startsWith(QByteArray::fromHex("89504e470d0a1a0a")))
		return QString();
	const QString name = kind + QStringLiteral("-") + QUuid::createUuid().toString(QUuid::WithoutBraces) +
			     QStringLiteral(".png");
	QSaveFile target(QDir(cardsDir()).filePath(name));
	if (!target.open(QIODevice::WriteOnly) || target.write(png) != png.size() || !target.commit())
		return QString();
	return name;
}

QString FffSession::importAsset(const QString &sourcePath, const QString &presidentId, const QString &kind)
{
	QFile source(sourcePath);
	if (!source.open(QIODevice::ReadOnly))
		return QString();
	Q_UNUSED(presidentId);
	return storeAsset(source.readAll(), kind);
}

bool FffSession::validMode(const QString &mode)
{
	return mode == QLatin1String("scoreboard") || mode == QLatin1String("bottomBar");
}
bool FffSession::showMode(const QString &mode)
{
	if (!validMode(mode))
		return false;
	const auto oldMode = m_displayMode;
	const auto oldPhase = m_phase;
	m_displayMode = mode;
	m_phase = FffPhase::Revealed;
	if (!save()) {
		m_displayMode = oldMode;
		m_phase = oldPhase;
		emit saveFailed();
		return false;
	}
	emit changed();
	return true;
}
bool FffSession::hideDisplay()
{
	if (m_phase == FffPhase::Collecting)
		return true;
	const auto oldPhase = m_phase;
	m_phase = FffPhase::Collecting;
	// Votes, round and the remembered mode all stay: this hides the board,
	// it does not end the round.
	if (!save()) {
		m_phase = oldPhase;
		emit saveFailed();
		return false;
	}
	emit changed();
	return true;
}
bool FffSession::setLogoPresident(const QString &id)
{
	if (!id.isEmpty() && indexOf(id) < 0)
		return false;
	const auto previous = m_logoPresidentId;
	m_logoPresidentId = id;
	if (!save()) {
		m_logoPresidentId = previous;
		emit saveFailed();
		return false;
	}
	emit changed();
	return true;
}

bool FffSession::setLogoRound(int round)
{
	if (round != 1 && round != 2)
		return false;
	if (m_logoRound == round)
		return true;
	const auto previous = m_logoRound;
	m_logoRound = round;
	if (!save()) {
		m_logoRound = previous;
		emit saveFailed();
		return false;
	}
	emit changed();
	return true;
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
		president.bottomBar = entry.value(QStringLiteral("bottomBar")).toString();
		president.logo = entry.value(QStringLiteral("logo")).toString();
		president.logo2 = entry.value(QStringLiteral("logo2")).toString();
		president.qualified = entry.value(QStringLiteral("qualified")).toString();
		president.unqualified = entry.value(QStringLiteral("unqualified")).toString();
		president.waiting = entry.value(QStringLiteral("waiting")).toString();
		president.status = statusFromName(entry.value(QStringLiteral("status")).toString());
		president.pin = entry.value(QStringLiteral("pin")).toString();
		if (!president.id.isEmpty())
			m_presidents.append(president);
	}

	m_round = root.value(QStringLiteral("round")).toInt(1);
	m_phase = root.value(QStringLiteral("phase")).toString() == QLatin1String("revealed") ? FffPhase::Revealed
											      : FffPhase::Collecting;

	const int port = root.value(QStringLiteral("port")).toInt(9779);
	m_port = (port > 0 && port <= 65535) ? static_cast<quint16>(port) : 9779;

	// A key that is not one we minted switches LAN access off rather than
	// letting a hand-edited file open the monitor.
	static const QRegularExpression keyPattern(QStringLiteral("^[0-9a-f]{32}$"));
	m_monitorKey = root.value(QStringLiteral("monitorKey")).toString();
	if (!keyPattern.match(m_monitorKey).hasMatch())
		m_monitorKey.clear();
	m_monitorLanEnabled = root.value(QStringLiteral("monitorLanEnabled")).toBool(false) && !m_monitorKey.isEmpty();

	m_displayMode = root.value(QStringLiteral("displayMode")).toString(QStringLiteral("scoreboard"));
	if (!validMode(m_displayMode))
		m_displayMode = QStringLiteral("scoreboard");
	const auto bottom = root.value(QStringLiteral("bottomBar")).toObject();
	m_cover = bottom.value(QStringLiteral("cover")).toString();
	if (QFileInfo(m_cover).fileName() != m_cover)
		m_cover.clear();
	m_logoRound = bottom.value(QStringLiteral("logoRound")).toInt(1);
	if (m_logoRound != 1 && m_logoRound != 2)
		m_logoRound = 1;
	m_logoPresidentId = bottom.value(QStringLiteral("logoPresidentId")).toString();
	if (indexOf(m_logoPresidentId) < 0)
		m_logoPresidentId.clear();
	m_bottomTemplate = bottom.value(QStringLiteral("cardTemplate")).toObject();
	if (!validCardTemplate(m_bottomTemplate))
		m_bottomTemplate = QJsonObject();
	m_bottomLogoTemplate = bottom.value(QStringLiteral("logoTemplate")).toObject();
	if (!validLogoTemplate(m_bottomLogoTemplate))
		m_bottomLogoTemplate = QJsonObject();
	m_bottomCountTemplate = bottom.value(QStringLiteral("countTemplate")).toObject();
	// Counters used to name one of five built-in font keys and took their colour
	// from the stylesheet. Carry those sessions forward on the defaults rather
	// than dropping a template the operator already positioned.
	if (m_bottomCountTemplate.contains(QStringLiteral("font"))) {
		m_bottomCountTemplate.remove(QStringLiteral("font"));
		m_bottomCountTemplate.insert(QStringLiteral("fontFamily"), QString());
	}
	if (!m_bottomCountTemplate.isEmpty()) {
		// 700 is what the stylesheet forced before the weight was selectable, so
		// an older template keeps the face it has always been drawn with.
		if (!m_bottomCountTemplate.value(QStringLiteral("fontWeight")).isDouble())
			m_bottomCountTemplate.insert(QStringLiteral("fontWeight"), 700);
		auto colors = m_bottomCountTemplate.value(QStringLiteral("colors")).toObject();
		if (!validHexColor(colors.value(QStringLiteral("red")).toString()))
			colors.insert(QStringLiteral("red"), QStringLiteral("#e23c3c"));
		if (!validHexColor(colors.value(QStringLiteral("green")).toString()))
			colors.insert(QStringLiteral("green"), QStringLiteral("#21b04a"));
		m_bottomCountTemplate.insert(QStringLiteral("colors"), colors);
	}
	if (!validCountTemplate(m_bottomCountTemplate))
		m_bottomCountTemplate = QJsonObject();
	for (bool isBottom : {false, true}) {
		const auto modeRoot = isBottom ? bottom : root;
		const QString mode = isBottom ? QStringLiteral("bottomBar") : QStringLiteral("scoreboard");
		auto &m_layout = isBottom ? m_bottomLayout : this->m_layout;
		auto &m_pieceLayouts = isBottom ? m_bottomPieces : this->m_pieceLayouts;
		auto &m_pieceLayers = isBottom ? m_bottomLayers : this->m_pieceLayers;
		const QJsonObject layout = modeRoot.value(QStringLiteral("layout")).toObject();
		m_layout.x = qBound(0.0, layout.value(QStringLiteral("x")).toDouble(0.5), 1.0);
		m_layout.y = qBound(0.0, layout.value(QStringLiteral("y")).toDouble(0.5), 1.0);
		m_layout.scale = qBound(0.5, layout.value(QStringLiteral("scale")).toDouble(1.0), 2.0);

		m_pieceLayouts.clear();
		const QJsonObject pieces = modeRoot.value(QStringLiteral("pieces")).toObject();
		for (auto it = pieces.begin(); it != pieces.end(); ++it) {
			if (!validPieceTarget(mode, it.key()))
				continue;
			const QJsonObject value = it.value().toObject();
			FffLayout piece;
			piece.x = qBound(0.0, value.value(QStringLiteral("x")).toDouble(0.5), 1.0);
			piece.y = qBound(0.0, value.value(QStringLiteral("y")).toDouble(0.5), 1.0);
			piece.scale = qBound(0.5, value.value(QStringLiteral("scale")).toDouble(1.0), 2.0);
			piece.scaleX = qBound(0.5, value.value(QStringLiteral("scaleX")).toDouble(piece.scale), 2.0);
			piece.scaleY = qBound(0.5, value.value(QStringLiteral("scaleY")).toDouble(piece.scale), 2.0);
			piece.resultScaleX =
				qBound(0.5, value.value(QStringLiteral("resultScaleX")).toDouble(1.0), 2.0);
			piece.resultScaleY =
				qBound(0.5, value.value(QStringLiteral("resultScaleY")).toDouble(1.0), 2.0);
			piece.imageOpacity =
				value.contains(QStringLiteral("imageOpacity"))
					? qBound(0.0, value.value(QStringLiteral("imageOpacity")).toDouble(1), 1.0)
					: -1;
			piece.resultOpacity =
				value.contains(QStringLiteral("resultOpacity"))
					? qBound(0.0, value.value(QStringLiteral("resultOpacity")).toDouble(1), 1.0)
					: -1;
			m_pieceLayouts.insert(it.key(), piece);
		}

		m_pieceLayers.clear();
		const QJsonObject layers = modeRoot.value(QStringLiteral("layers")).toObject();
		for (auto it = layers.begin(); it != layers.end(); ++it) {
			if (!it.value().isDouble() || !validPieceTarget(mode, it.key()))
				continue;
			m_pieceLayers.insert(it.key(), qBound(-10000, it.value().toInt(), 10000));
		}
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
		entry.insert(QStringLiteral("bottomBar"), president.bottomBar);
		entry.insert(QStringLiteral("logo"), president.logo);
		entry.insert(QStringLiteral("logo2"), president.logo2);
		entry.insert(QStringLiteral("qualified"), president.qualified);
		entry.insert(QStringLiteral("unqualified"), president.unqualified);
		entry.insert(QStringLiteral("waiting"), president.waiting);
		entry.insert(QStringLiteral("status"), statusName(president.status));
		presidents.append(entry);
	}

	QJsonObject votes;
	for (auto it = m_votes.begin(); it != m_votes.end(); ++it)
		votes.insert(it.key(), voteName(it.value()));

	QJsonObject root;
	root.insert(QStringLiteral("displayMode"), m_displayMode);
	root.insert(QStringLiteral("bottomBar"), bottomBarJson());
	root.insert(QStringLiteral("version"), 5);
	if (!m_cardTemplate.isEmpty())
		root.insert(QStringLiteral("cardTemplate"), m_cardTemplate);
	root.insert(QStringLiteral("port"), static_cast<int>(m_port));
	root.insert(QStringLiteral("monitorLanEnabled"), m_monitorLanEnabled);
	root.insert(QStringLiteral("monitorKey"), m_monitorKey);
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
		if (it.value().imageOpacity >= 0)
			piece.insert(QStringLiteral("imageOpacity"), it.value().imageOpacity);
		if (it.value().resultOpacity >= 0)
			piece.insert(QStringLiteral("resultOpacity"), it.value().resultOpacity);
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
		entry.insert(QStringLiteral("bottomBarUrl"), assetUrl(president, QStringLiteral("bottomBar")));
		// The round on air picks the centre logo; both are named so the
		// overlay can fetch the other round before the operator switches.
		const QString logoRound1 = assetUrl(president, QStringLiteral("logo"));
		const QString logoRound2 = assetUrl(president, QStringLiteral("logo2"));
		entry.insert(QStringLiteral("logoUrl"), m_logoRound == 2 ? logoRound2 : logoRound1);
		entry.insert(QStringLiteral("logoRound1Url"), logoRound1);
		entry.insert(QStringLiteral("logoRound2Url"), logoRound2);
		entry.insert(QStringLiteral("status"), statusName(president.status));
		entry.insert(QStringLiteral("statusUrl"), statusUrl(president));
		// Per-status URLs so the monitor can say which statuses already have
		// artwork without guessing from the one that happens to be showing.
		for (const QString &kind :
		     {QStringLiteral("qualified"), QStringLiteral("unqualified"), QStringLiteral("waiting")})
			entry.insert(kind + QStringLiteral("Url"), assetUrl(president, kind));
		entry.insert(QStringLiteral("vote"), voteName(voteOf(president.id)));
		presidents.append(entry);
	}

	QJsonObject root;
	root.insert(QStringLiteral("displayMode"), m_displayMode);
	root.insert(QStringLiteral("bottomBar"), bottomBarJson());
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
		if (it.value().imageOpacity >= 0)
			piece.insert(QStringLiteral("imageOpacity"), it.value().imageOpacity);
		if (it.value().resultOpacity >= 0)
			piece.insert(QStringLiteral("resultOpacity"), it.value().resultOpacity);
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

QJsonObject FffSession::bottomBarJson() const
{
	QJsonObject root;
	root.insert(QStringLiteral("logoPresidentId"), m_logoPresidentId);
	root.insert(QStringLiteral("logoRound"), m_logoRound);
	root.insert(QStringLiteral("cover"), m_cover);
	root.insert(QStringLiteral("coverUrl"), coverUrl());
	QJsonObject layout;
	if (!m_bottomTemplate.isEmpty())
		root.insert(QStringLiteral("cardTemplate"), m_bottomTemplate);
	if (!m_bottomLogoTemplate.isEmpty())
		root.insert(QStringLiteral("logoTemplate"), m_bottomLogoTemplate);
	if (!m_bottomCountTemplate.isEmpty())
		root.insert(QStringLiteral("countTemplate"), m_bottomCountTemplate);
	layout.insert(QStringLiteral("x"), m_bottomLayout.x);
	layout.insert(QStringLiteral("y"), m_bottomLayout.y);
	layout.insert(QStringLiteral("scale"), m_bottomLayout.scale);
	root.insert(QStringLiteral("layout"), layout);
	QJsonObject pieces;
	for (auto it = m_bottomPieces.begin(); it != m_bottomPieces.end(); ++it) {
		QJsonObject piece;
		piece.insert(QStringLiteral("x"), it.value().x);
		piece.insert(QStringLiteral("y"), it.value().y);
		piece.insert(QStringLiteral("scale"), it.value().scale);
		piece.insert(QStringLiteral("scaleX"), it.value().scaleX);
		piece.insert(QStringLiteral("scaleY"), it.value().scaleY);
		piece.insert(QStringLiteral("resultScaleX"), it.value().resultScaleX);
		piece.insert(QStringLiteral("resultScaleY"), it.value().resultScaleY);
		if (it.value().imageOpacity >= 0)
			piece.insert(QStringLiteral("imageOpacity"), it.value().imageOpacity);
		if (it.value().resultOpacity >= 0)
			piece.insert(QStringLiteral("resultOpacity"), it.value().resultOpacity);
		pieces.insert(it.key(), piece);
	}
	root.insert(QStringLiteral("pieces"), pieces);
	QJsonObject layers;
	for (auto it = m_bottomLayers.cbegin(); it != m_bottomLayers.cend(); ++it)
		layers.insert(it.key(), it.value());
	root.insert(QStringLiteral("layers"), layers);

	return root;
}
