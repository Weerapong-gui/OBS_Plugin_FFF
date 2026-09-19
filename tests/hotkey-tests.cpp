// The OBS hotkeys for the live bar, driven against an in-memory libobs.
#include "fff-dock-live.h"
#include "fff-dock-ui.h"
#include "fff-hotkeys.h"
#include "fff-http-server.h"
#include "fff-session.h"
#include "obs-hotkey-stubs.h"
#include "obs-stubs.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>
#include <QTemporaryDir>

static void check(bool ok, const char *message)
{
	if (!ok)
		qFatal("%s", message);
}

// Every confirmation a panel asked for. A hotkey must never add to it.
static QStringList asked;

// The callback only queues the work, so the action lands once the event loop
// has run - exactly as it does inside OBS.
static void settle()
{
	QCoreApplication::processEvents();
}

static FffPresident president(const QString &id, const QString &pin)
{
	FffPresident value;
	value.id = id;
	value.name = QStringLiteral("นายก") + id;
	value.pin = pin;
	return value;
}

static QJsonObject savedBindings()
{
	QFile file(QDir(fffTestConfigPath).filePath(QStringLiteral("hotkeys.json")));
	check(file.open(QIODevice::ReadOnly), "hotkeys.json exists");
	return QJsonDocument::fromJson(file.readAll()).object();
}

int main(int argc, char **argv)
{
	QApplication app(argc, argv);
	QTemporaryDir temp;
	check(temp.isValid(), "temporary config");
	fffTestConfigPath = temp.path();
	fffSetConfirmOverride([](const QString &title) {
		asked.append(title);
		return true;
	});

	FffSession session;
	check(session.addPresident(president(QStringLiteral("a"), QStringLiteral("111111"))) &&
		      session.addPresident(president(QStringLiteral("b"), QStringLiteral("222222"))),
	      "roster for the hotkey tests");
	FffHttpServer server(&session);
	QString error;
	check(server.start(0, &error), "HTTP starts");
	FffLivePanel panel(&session, &server);

	const QStringList names{QStringLiteral("fff.toggle_show_status"), QStringLiteral("fff.toggle_bottom_bar"),
				QStringLiteral("fff.hide_screen"), QStringLiteral("fff.new_round")};

	{
		FffHotkeys hotkeys(&panel, session.configDir());

		// Registering is the whole of appearing in Settings > Hotkeys, so the
		// names and the words the operator reads there are the check.
		check(fffHotkeyCount() == 4, "four hotkeys are registered");
		for (const QString &name : names)
			check(fffHotkeyIdFor(name) != OBS_INVALID_HOTKEY_ID, "every action has a hotkey");
		check(fffHotkeyDescription(fffHotkeyIdFor(QStringLiteral("fff.new_round"))) ==
			      QStringLiteral("FFF Flag Board: เริ่มรอบใหม่ (ไม่ถามยืนยัน)"),
		      "the description says the round goes without a question");

		// Releasing a key must not repeat what pressing it did.
		const obs_hotkey_id bottomBar = fffHotkeyIdFor(QStringLiteral("fff.toggle_bottom_bar"));
		fffHotkeyPress(bottomBar, false);
		settle();
		check(session.phase() == FffPhase::Collecting, "a key coming up does nothing");

		fffHotkeyPress(bottomBar, true);
		check(session.phase() == FffPhase::Collecting, "the action waits for the Qt thread");
		settle();
		check(session.phase() == FffPhase::Revealed && session.displayMode() == QLatin1String("bottomBar") &&
			      panel.bottomBarButton()->isChecked(),
		      "the press puts BOTTOM BAR on air and the dock follows");
		fffHotkeyPress(bottomBar, true);
		fffHotkeyPress(bottomBar, false);
		settle();
		check(session.phase() == FffPhase::Collecting, "pressing it again takes it back off");

		const obs_hotkey_id showStatus = fffHotkeyIdFor(QStringLiteral("fff.toggle_show_status"));
		fffHotkeyPress(showStatus, true);
		settle();
		check(session.phase() == FffPhase::Revealed && session.displayMode() == QLatin1String("scoreboard"),
		      "the other mode has its own key");

		fffHotkeyPress(fffHotkeyIdFor(QStringLiteral("fff.hide_screen")), true);
		settle();
		check(session.phase() == FffPhase::Collecting, "the hide key blanks the stream");

		check(session.setVote(QStringLiteral("a"), FffVote::Green), "a vote to clear");
		asked.clear();
		fffHotkeyPress(fffHotkeyIdFor(QStringLiteral("fff.new_round")), true);
		settle();
		check(asked.isEmpty() && session.round() == 2 && session.votedCount() == 0,
		      "the new round key clears the round without asking");

		// What the operator types in Settings > Hotkeys lands on the hotkey
		// itself; the plugin's job is only to write it down and put it back.
		for (int index = 0; index < names.size(); ++index)
			fffHotkeySetBinding(fffHotkeyIdFor(names.at(index)), index + 1);
		hotkeys.saveBindings();
		const QJsonObject saved = savedBindings();
		check(saved.size() == 4, "every hotkey is written");
		for (int index = 0; index < names.size(); ++index)
			check(saved.value(names.at(index)).toInt() == index + 1,
			      "each binding is filed under its own name");
	}

	// Leaving scope saves once more and hands the ids back.
	for (const QString &name : names)
		check(fffHotkeyIdFor(name) == OBS_INVALID_HOTKEY_ID, "every hotkey is unregistered on the way out");
	check(fffHotkeyCount() == 0, "nothing is left registered");

	{
		// A fresh OBS: new ids, and the bindings from disk on the right ones.
		FffHotkeys reopened(&panel, session.configDir());
		for (int index = 0; index < names.size(); ++index) {
			const obs_hotkey_id id = fffHotkeyIdFor(names.at(index));
			check(id != OBS_INVALID_HOTKEY_ID, "the hotkey is registered again");
			check(fffHotkeyBinding(id) == index + 1, "its saved binding came back to it");
		}
		// The keys still work after a restore, not just carry a binding.
		fffHotkeyPress(fffHotkeyIdFor(QStringLiteral("fff.toggle_bottom_bar")), true);
		settle();
		check(session.phase() == FffPhase::Revealed && session.displayMode() == QLatin1String("bottomBar"),
		      "a restored hotkey still acts");
		reopened.run(FffAction::HideScreen);
		check(session.phase() == FffPhase::Collecting, "run() reaches the panel directly too");
	}

	{
		// First run on a machine with no hotkeys.json: unbound, not broken.
		fffTestConfigPath = QDir(temp.path()).filePath(QStringLiteral("empty"));
		check(QDir().mkpath(fffTestConfigPath), "a config directory with no hotkeys.json");
		FffHotkeys fresh(&panel, fffTestConfigPath);
		for (const QString &name : names)
			check(fffHotkeyBinding(fffHotkeyIdFor(name)) == 0, "a missing file leaves every key unbound");
		fffHotkeyPress(fffHotkeyIdFor(QStringLiteral("fff.new_round")), true);
		settle();
		check(session.round() == 3, "and the keys work all the same");
	}

	qInfo("PASS: hotkey registration, press/release, dispatch and saved bindings");
	return 0;
}
