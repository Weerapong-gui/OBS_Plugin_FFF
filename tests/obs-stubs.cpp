#include "obs-stubs.h"

#include <obs-module.h>

#include <QDir>
#include <cstdlib>
#include <cstring>

QString fffTestConfigPath;

extern "C" {
obs_module_t *obs_current_module(void)
{
	return nullptr;
}
char *obs_module_get_config_path(obs_module_t *, const char *file)
{
	return strdup(QDir(fffTestConfigPath).filePath(QString::fromUtf8(file)).toUtf8().constData());
}
char *obs_find_module_file(obs_module_t *, const char *)
{
	return nullptr;
}
void bfree(void *ptr)
{
	free(ptr);
}
int os_mkdirs(const char *path)
{
	return QDir().mkpath(QString::fromUtf8(path)) ? 0 : -1;
}
void obs_log(int, const char *, ...) {}
}
