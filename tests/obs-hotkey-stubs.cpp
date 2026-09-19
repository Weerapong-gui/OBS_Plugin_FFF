// The slice of libobs that FffHotkeys talks to, kept in memory so the real
// registration, dispatch and save/restore can be driven without OBS running.
//
// obs_data here is only what this plugin asks of it: a flat map of name to
// binding array, written and read back as JSON. A binding array is opaque to
// the plugin, so it is opaque here too - each one is a number that identifies
// which hotkey libobs produced it for, which is exactly what a check needs to
// see that the right bindings came back to the right hotkey.
#include "obs-hotkey-stubs.h"

#include <obs-module.h>

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>

#include <map>
#include <vector>

namespace {

struct Registered {
	QString name;
	QString description;
	obs_hotkey_func func = nullptr;
	void *data = nullptr;
	// The binding this hotkey currently holds, the way OBS would hold what
	// the operator typed in Settings. 0 means unbound.
	int binding = 0;
	bool live = true;
};

std::vector<Registered> &registry()
{
	static std::vector<Registered> value;
	return value;
}

// obs_data_array_t stands in for a binding list; obs_data_t for the one object
// the plugin builds around them.
struct StubArray {
	int binding = 0;
	int refs = 1;
};

struct StubData {
	std::map<QString, int> bindings;
	int refs = 1;
};

} // namespace

void fffHotkeyStubsReset()
{
	registry().clear();
}

int fffHotkeyCount()
{
	int count = 0;
	for (const Registered &entry : registry()) {
		if (entry.live)
			++count;
	}
	return count;
}

QString fffHotkeyName(obs_hotkey_id id)
{
	return id < registry().size() ? registry()[id].name : QString();
}

QString fffHotkeyDescription(obs_hotkey_id id)
{
	return id < registry().size() ? registry()[id].description : QString();
}

obs_hotkey_id fffHotkeyIdFor(const QString &name)
{
	for (size_t index = 0; index < registry().size(); ++index) {
		if (registry()[index].live && registry()[index].name == name)
			return index;
	}
	return OBS_INVALID_HOTKEY_ID;
}

bool fffHotkeyLive(obs_hotkey_id id)
{
	return id < registry().size() && registry()[id].live;
}

int fffHotkeyBinding(obs_hotkey_id id)
{
	return id < registry().size() ? registry()[id].binding : 0;
}

void fffHotkeySetBinding(obs_hotkey_id id, int binding)
{
	if (id < registry().size())
		registry()[id].binding = binding;
}

void fffHotkeyPress(obs_hotkey_id id, bool pressed)
{
	if (id >= registry().size() || !registry()[id].live)
		return;
	const Registered &entry = registry()[id];
	entry.func(entry.data, id, nullptr, pressed);
}

extern "C" {

obs_hotkey_id obs_hotkey_register_frontend(const char *name, const char *description, obs_hotkey_func func, void *data)
{
	Registered entry;
	entry.name = QString::fromUtf8(name);
	entry.description = QString::fromUtf8(description);
	entry.func = func;
	entry.data = data;
	registry().push_back(entry);
	return registry().size() - 1;
}

void obs_hotkey_unregister(obs_hotkey_id id)
{
	if (id < registry().size())
		registry()[id].live = false;
}

obs_data_array_t *obs_hotkey_save(obs_hotkey_id id)
{
	auto *array = new StubArray();
	array->binding = id < registry().size() ? registry()[id].binding : 0;
	return reinterpret_cast<obs_data_array_t *>(array);
}

void obs_hotkey_load(obs_hotkey_id id, obs_data_array_t *data)
{
	if (id >= registry().size() || !data)
		return;
	registry()[id].binding = reinterpret_cast<StubArray *>(data)->binding;
}

obs_data_t *obs_data_create(void)
{
	return reinterpret_cast<obs_data_t *>(new StubData());
}

obs_data_t *obs_data_create_from_json_file(const char *json_file)
{
	QFile file(QString::fromUtf8(json_file));
	if (!file.open(QIODevice::ReadOnly))
		return nullptr;
	const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
	auto *value = new StubData();
	for (auto it = root.begin(); it != root.end(); ++it)
		value->bindings.insert({it.key(), it.value().toInt()});
	return reinterpret_cast<obs_data_t *>(value);
}

bool obs_data_save_json(obs_data_t *data, const char *file)
{
	QJsonObject root;
	for (const auto &binding : reinterpret_cast<StubData *>(data)->bindings)
		root.insert(binding.first, binding.second);
	QFile out(QString::fromUtf8(file));
	if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate))
		return false;
	out.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
	return true;
}

void obs_data_set_array(obs_data_t *data, const char *name, obs_data_array_t *array)
{
	reinterpret_cast<StubData *>(data)->bindings[QString::fromUtf8(name)] =
		array ? reinterpret_cast<StubArray *>(array)->binding : 0;
}

obs_data_array_t *obs_data_get_array(obs_data_t *data, const char *name)
{
	auto *value = reinterpret_cast<StubData *>(data);
	const auto found = value->bindings.find(QString::fromUtf8(name));
	if (found == value->bindings.end())
		return nullptr;
	auto *array = new StubArray();
	array->binding = found->second;
	return reinterpret_cast<obs_data_array_t *>(array);
}

void obs_data_array_release(obs_data_array_t *array)
{
	delete reinterpret_cast<StubArray *>(array);
}

void obs_data_release(obs_data_t *data)
{
	delete reinterpret_cast<StubData *>(data);
}
}
