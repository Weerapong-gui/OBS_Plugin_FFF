// What a test can see and do to the in-memory libobs hotkey system.
#pragma once

#include <obs.h>

#include <QString>

void fffHotkeyStubsReset();
// Hotkeys still registered; unregistering one does not renumber the rest.
int fffHotkeyCount();
QString fffHotkeyName(obs_hotkey_id id);
QString fffHotkeyDescription(obs_hotkey_id id);
obs_hotkey_id fffHotkeyIdFor(const QString &name);
bool fffHotkeyLive(obs_hotkey_id id);
// Stands in for the key combination OBS would hold. 0 is unbound.
int fffHotkeyBinding(obs_hotkey_id id);
void fffHotkeySetBinding(obs_hotkey_id id, int binding);
// Calls the hotkey's own callback, as libobs' hotkey thread would.
void fffHotkeyPress(obs_hotkey_id id, bool pressed);
