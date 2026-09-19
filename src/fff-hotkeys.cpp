/*
FFF Tools for OBS - OBS hotkeys for the live bar's buttons
Copyright (C) 2026 Student Union MFU <studentunion.developer@gmail.com>
GPL-2.0-or-later
*/

#include "fff-hotkeys.h"
#include "fff-dock-live.h"

#include <obs-module.h>

#include <QDir>
#include <QMetaObject>
#include <QPointer>

namespace {

// The names are the keys the bindings are filed under, so they are part of the
// saved format: renaming one loses whatever the operator had bound to it. The
// descriptions are what Settings > Hotkeys shows, in Thai like the rest of the
// operator's side of this plugin.
struct Definition {
	FffAction action;
	const char *name;
	const char *description;
};

constexpr Definition kDefinitions[] = {
	{FffAction::ToggleShowStatus, "fff.toggle_show_status", "FFF Flag Board: สลับ Show Status ขึ้น/ลงจอ"},
	{FffAction::ToggleBottomBar, "fff.toggle_bottom_bar", "FFF Flag Board: สลับ BOTTOM BAR ขึ้น/ลงจอ"},
	{FffAction::HideScreen, "fff.hide_screen", "FFF Flag Board: ซ่อนจอ"},
	{FffAction::NewRound, "fff.new_round", "FFF Flag Board: เริ่มรอบใหม่ (ไม่ถามยืนยัน)"},
};

} // namespace

FffHotkeys::FffHotkeys(FffLivePanel *live, const QString &configDir, QObject *parent)
	: QObject(parent),
	  m_live(live),
	  m_configDir(configDir)
{
	for (size_t index = 0; index < m_entries.size(); ++index) {
		const Definition &definition = kDefinitions[index];
		Entry &entry = m_entries[index];
		entry = {this, definition.action, definition.name, OBS_INVALID_HOTKEY_ID};
		entry.id = obs_hotkey_register_frontend(definition.name, definition.description, onHotkey, &entry);
	}
	loadBindings();
}

FffHotkeys::~FffHotkeys()
{
	// OBS closing is the one moment a binding set in Settings would otherwise
	// be lost, so write once more before letting go of the ids.
	saveBindings();
	for (Entry &entry : m_entries) {
		if (entry.id == OBS_INVALID_HOTKEY_ID)
			continue;
		// This takes libobs' hotkey lock, so a callback already dispatching
		// finishes before the entry it was handed goes away.
		obs_hotkey_unregister(entry.id);
		entry.id = OBS_INVALID_HOTKEY_ID;
	}
}

QString FffHotkeys::bindingsPath() const
{
	return QDir(m_configDir).filePath(QStringLiteral("hotkeys.json"));
}

void FffHotkeys::onHotkey(void *data, obs_hotkey_id, obs_hotkey_t *, bool pressed)
{
	// libobs calls this on the key going down and again on it coming up. Only
	// the press is the operator asking for something.
	if (!pressed)
		return;

	const Entry *entry = static_cast<const Entry *>(data);
	// This runs on libobs' own hotkey thread, so the work has to reach the Qt
	// thread before it touches the session or the dock. Queued either way: a
	// save to disk does not belong inside a hotkey dispatch even when the
	// frontend happens to be routing callbacks to the UI thread already.
	QMetaObject::invokeMethod(
		entry->owner,
		[owner = QPointer<FffHotkeys>(entry->owner), action = entry->action]() {
			if (owner)
				owner->run(action);
		},
		Qt::QueuedConnection);
}

void FffHotkeys::run(FffAction action)
{
	if (!m_live)
		return;
	switch (action) {
	case FffAction::ToggleShowStatus:
		m_live->toggleMode(QStringLiteral("scoreboard"));
		return;
	case FffAction::ToggleBottomBar:
		m_live->toggleMode(QStringLiteral("bottomBar"));
		return;
	case FffAction::HideScreen:
		m_live->hideScreen();
		return;
	case FffAction::NewRound:
		// No dialog: the operator's hands are on the keyboard, and a
		// confirmation they cannot see is worse than none.
		m_live->startNewRound(false);
		return;
	}
}

void FffHotkeys::saveBindings() const
{
	obs_data_t *root = obs_data_create();
	for (const Entry &entry : m_entries) {
		if (entry.id == OBS_INVALID_HOTKEY_ID)
			continue;
		obs_data_array_t *bindings = obs_hotkey_save(entry.id);
		obs_data_set_array(root, entry.name, bindings);
		obs_data_array_release(bindings);
	}
	obs_data_save_json(root, bindingsPath().toUtf8().constData());
	obs_data_release(root);
}

void FffHotkeys::loadBindings()
{
	// No file is the ordinary first run: every hotkey simply stays unbound,
	// which is what OBS shows for one nobody has set yet.
	obs_data_t *root = obs_data_create_from_json_file(bindingsPath().toUtf8().constData());
	if (!root)
		return;
	for (const Entry &entry : m_entries) {
		if (entry.id == OBS_INVALID_HOTKEY_ID)
			continue;
		obs_data_array_t *bindings = obs_data_get_array(root, entry.name);
		if (!bindings)
			continue;
		obs_hotkey_load(entry.id, bindings);
		obs_data_array_release(bindings);
	}
	obs_data_release(root);
}
