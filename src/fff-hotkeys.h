/*
FFF Tools for OBS - OBS hotkeys for the live bar's buttons
Copyright (C) 2026 Student Union MFU <studentunion.developer@gmail.com>
GPL-2.0-or-later
*/

#pragma once

#include <obs.h>

#include <QObject>
#include <QString>

#include <array>

class FffLivePanel;

// One per button on the live bar that changes what the stream shows.
enum class FffAction { ToggleShowStatus, ToggleBottomBar, HideScreen, NewRound };

/*
 * The live bar's actions, reachable from OBS's own hotkey system. Registering
 * them is all it takes for them to appear in Settings > Hotkeys; OBS does not
 * keep the bindings of a hotkey a plugin registered, so saving and restoring
 * them is this class's other job.
 *
 * Every action runs through FffLivePanel, so a key press reaches exactly the
 * code a click does, with the same error line and the same refresh - the one
 * difference being that "start a new round" from a key never opens the
 * confirmation the button opens.
 */
class FffHotkeys : public QObject {
public:
	FffHotkeys(FffLivePanel *live, const QString &configDir, QObject *parent = nullptr);
	~FffHotkeys() override;

	// Writes the current bindings to hotkeys.json. The dock calls this
	// whenever OBS saves, so a key set in Settings is kept without waiting
	// for OBS to close; the destructor calls it once more as a backstop.
	void saveBindings() const;

	// Runs one action on this thread. The hotkey callback queues it here;
	// the tests call it directly.
	void run(FffAction action);

private:
	// Registration hands libobs a pointer that stays valid for the hotkey's
	// whole life, so these entries live in the object and never move.
	struct Entry {
		FffHotkeys *owner;
		FffAction action;
		const char *name;
		obs_hotkey_id id;
	};

	static void onHotkey(void *data, obs_hotkey_id id, obs_hotkey_t *hotkey, bool pressed);
	void loadBindings();
	QString bindingsPath() const;

	FffLivePanel *m_live = nullptr;
	QString m_configDir;
	std::array<Entry, 4> m_entries;
};
