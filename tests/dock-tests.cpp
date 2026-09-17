// Dock panels driven offscreen against a real session and server.
#include "fff-dock-live.h"
#include "fff-dock-roster.h"
#include "fff-dock-ui.h"
#include "fff-http-server.h"
#include "fff-session.h"
#include "obs-stubs.h"

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QStringList>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QToolButton>

static void check(bool ok, const char *message)
{
	if (!ok)
		qFatal("%s", message);
}

// Every confirmation a panel asked for, answered with `answer`.
static QStringList asked;
static bool answer = false;

static FffPresident president(const QString &id, const QString &name, const QString &pin)
{
	FffPresident value;
	value.id = id;
	value.name = name;
	value.school = QStringLiteral("สำนัก") + name;
	value.pin = pin;
	return value;
}

// A plain file where the config directory should be makes every save fail.
static QString blockedConfig()
{
	const QString path = QDir(fffTestConfigPath).filePath(QStringLiteral("blocked"));
	QFile file(path);
	check(file.open(QIODevice::WriteOnly), "create blocked config");
	return path;
}

static void testLivePanel(FffSession &session, FffHttpServer &server)
{
	FffLivePanel panel(&session, &server);
	check(session.phase() == FffPhase::Collecting, "tests start off air");
	check(panel.banner()->text() == QStringLiteral("○ จอว่าง") &&
		      panel.banner()->property("fffState").toString() == QLatin1String("blank"),
	      "a blank board reads as blank");
	check(!panel.hideButton()->isEnabled(), "nothing to hide while blank");
	check(panel.summary()->text() == QStringLiteral("รอบ 1 · โหวตแล้ว 0/2") &&
		      !panel.summary()->property("fffReady").toBool(),
	      "summary counts votes");

	check(session.setVote(QStringLiteral("a"), FffVote::Green) && session.setVote(QStringLiteral("b"), FffVote::Red),
	      "everyone votes");
	check(panel.summary()->text() == QStringLiteral("รอบ 1 · ✓ ครบ 2/2 พร้อมขึ้นจอ") &&
		      panel.summary()->property("fffReady").toBool(),
	      "a full vote invites the reveal");

	panel.bottomBarButton()->click();
	check(session.phase() == FffPhase::Revealed && session.displayMode() == QLatin1String("bottomBar"),
	      "BOTTOM BAR goes on air");
	check(panel.banner()->text() == QStringLiteral("● ออกอากาศ · BOTTOM BAR") &&
		      panel.banner()->property("fffState").toString() == QLatin1String("onAir"),
	      "the banner names the aired mode");
	check(panel.bottomBarButton()->isChecked() && !panel.scoreboardButton()->isChecked(),
	      "the aired button is checked");
	check(!panel.summary()->property("fffReady").toBool(), "the summary stops inviting once on air");

	panel.bottomBarButton()->click();
	check(session.phase() == FffPhase::Revealed && panel.bottomBarButton()->isChecked(),
	      "pressing the aired mode again keeps it on air");

	panel.scoreboardButton()->click();
	check(session.displayMode() == QLatin1String("scoreboard") && panel.scoreboardButton()->isChecked() &&
		      !panel.bottomBarButton()->isChecked(),
	      "the other mode switches directly");

	panel.hideButton()->click();
	check(session.phase() == FffPhase::Collecting && !panel.hideButton()->isEnabled() &&
		      panel.banner()->property("fffState").toString() == QLatin1String("blank"),
	      "hide blanks the stream");

	asked.clear();
	answer = false;
	panel.newRoundButton()->click();
	check(asked == QStringList{QStringLiteral("เริ่มรอบใหม่")} && session.round() == 1 && session.votedCount() == 2,
	      "a declined new round keeps the votes");
	answer = true;
	panel.newRoundButton()->click();
	check(session.round() == 2 && session.votedCount() == 0, "a confirmed new round clears the votes");

	server.stop();
	check(panel.banner()->property("fffState").toString() == QLatin1String("serverOff") &&
		      panel.banner()->text().startsWith(QStringLiteral("⚠ เซิร์ฟเวอร์ปิด")),
	      "a stopped server outranks the board state");
	QString error;
	check(server.start(0, &error), "restart the server");
	check(panel.banner()->property("fffState").toString() == QLatin1String("blank"),
	      "the banner recovers with the server");

	const QString config = fffTestConfigPath;
	fffTestConfigPath = blockedConfig();
	panel.bottomBarButton()->click();
	check(session.phase() == FffPhase::Collecting && !panel.error()->isHidden() &&
		      panel.error()->text() == fffSaveErrorText(),
	      "a failed show surfaces in the panel");
	fffTestConfigPath = config;
	check(session.setVote(QStringLiteral("a"), FffVote::Green) && panel.error()->isHidden(),
	      "the next successful change clears the error");

	FffLiveTab live(&session);
	check(live.votes()->count() == 2 && live.votes()->item(0)->text() == QStringLiteral("● เขียว · หนึ่ง") &&
		      live.votes()->item(1)->text() == QStringLiteral("○ ยังไม่กด · สอง"),
	      "the vote list names colour and president");
	live.logo()->setCurrentIndex(live.logo()->findData(QStringLiteral("b")));
	check(session.logoPresidentId() == QLatin1String("b"), "the logo choice reaches the session");
	check(session.setVote(QStringLiteral("b"), FffVote::Red) &&
		      live.logo()->currentData().toString() == QLatin1String("b"),
	      "a vote keeps the logo choice");
}

static void testRosterTab(FffSession &session)
{
	FffRosterTab roster(&session);
	QStringList errors;
	QObject::connect(&roster, &FffRosterTab::errorRaised, [&](const QString &message) { errors.append(message); });

	check(roster.table()->rowCount() == 2 && roster.table()->columnCount() == 6,
	      "the roster lists everyone in six columns");
	check(roster.table()->item(0, 3)->text() == QStringLiteral("—"), "missing artwork reads as a dash");
	check(!roster.pngButton()->isEnabled() && !roster.moreButton()->isEnabled() &&
		      roster.pngButton()->toolTip() == QStringLiteral("เลือกนายกในตารางก่อน"),
	      "row actions wait for a selection and say so");

	roster.selectPresident(QStringLiteral("a"));
	check(roster.selectedPresidentId() == QLatin1String("a") && roster.pngButton()->isEnabled() &&
		      roster.moreButton()->isEnabled(),
	      "selecting a row enables its actions");
	check(roster.chooseAction(QStringLiteral("bottomBar"))->isEnabled() &&
		      !roster.clearAction(QStringLiteral("bottomBar"))->isEnabled(),
	      "nothing to clear before artwork exists");

	FffPresident withBar = *session.presidentById(QStringLiteral("a"));
	withBar.bottomBar = QStringLiteral("bottomBar-test.png");
	check(session.updatePresident(withBar), "give the president a BOTTOM BAR");
	roster.refresh();
	check(roster.selectedPresidentId() == QLatin1String("a") &&
		      roster.clearAction(QStringLiteral("bottomBar"))->isEnabled() &&
		      roster.table()->item(0, 4)->text() == QStringLiteral("✓"),
	      "existing artwork shows a tick and can be cleared");
	roster.clearAction(QStringLiteral("bottomBar"))->trigger();
	check(session.presidentById(QStringLiteral("a"))->bottomBar.isEmpty() &&
		      roster.table()->item(0, 4)->text() == QStringLiteral("—"),
	      "clearing artwork needs no confirmation");

	const QString pinBefore = session.presidentById(QStringLiteral("a"))->pin;
	asked.clear();
	answer = false;
	roster.regeneratePinAction()->trigger();
	check(asked == QStringList{QStringLiteral("สุ่ม PIN ใหม่")} &&
		      session.presidentById(QStringLiteral("a"))->pin == pinBefore,
	      "a declined PIN change keeps the PIN");
	answer = true;
	roster.regeneratePinAction()->trigger();
	check(session.presidentById(QStringLiteral("a"))->pin != pinBefore, "a confirmed PIN change replaces it");

	asked.clear();
	answer = false;
	roster.removeAction()->trigger();
	check(asked == QStringList{QStringLiteral("ลบนายก")} && session.presidents().size() == 2,
	      "a declined removal keeps the president");

	roster.addButton()->click();
	const QString added = session.presidents().last().id;
	check(session.presidents().size() == 3 && roster.table()->rowCount() == 3 &&
		      roster.selectedPresidentId() == added,
	      "add creates and selects a president");
	answer = true;
	roster.removeAction()->trigger();
	check(session.presidents().size() == 2 && roster.table()->rowCount() == 2 && !session.presidentById(added),
	      "a confirmed removal deletes the president");

	roster.table()->item(1, 0)->setText(QStringLiteral("สองใหม่"));
	check(session.presidentById(QStringLiteral("b"))->name == QStringLiteral("สองใหม่"), "editing a name saves it");
	check(errors.isEmpty(), "no roster errors on the happy path");
}

int main(int argc, char **argv)
{
	QApplication app(argc, argv);
	QTemporaryDir temp;
	check(temp.isValid(), "temporary config");
	fffTestConfigPath = temp.path();
	fffSetConfirmOverride([](const QString &title) {
		asked.append(title);
		return answer;
	});

	FffSession session;
	check(session.addPresident(president(QStringLiteral("a"), QStringLiteral("หนึ่ง"), QStringLiteral("111111"))) &&
		      session.addPresident(president(QStringLiteral("b"), QStringLiteral("สอง"), QStringLiteral("222222"))),
	      "roster for the dock tests");
	FffHttpServer server(&session);
	QString error;
	check(server.start(0, &error), "HTTP starts");

	testLivePanel(session, server);
	testRosterTab(session);
	qInfo("PASS: dock live panel and roster tab");
	return 0;
}
