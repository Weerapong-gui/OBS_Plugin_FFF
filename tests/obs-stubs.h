// OBS host functions the plugin sources call, backed by a temporary directory.
#pragma once

#include <QString>

// Directory obs_module_get_config_path() resolves into. Tests point it at a
// QTemporaryDir, or at a plain file to make every save fail.
extern QString fffTestConfigPath;
