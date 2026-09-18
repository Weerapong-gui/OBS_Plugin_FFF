#include "obs-stubs.h"

#include <obs-module.h>

#include <QDir>
#include <QFileInfo>
#include <cstdlib>
#include <cstring>

QString fffTestConfigPath;
QString fffTestDataPath;

extern "C" {
obs_module_t *obs_current_module(void)
{
	return nullptr;
}
char *obs_module_get_config_path(obs_module_t *, const char *file)
{
	return strdup(QDir(fffTestConfigPath).filePath(QString::fromUtf8(file)).toUtf8().constData());
}
char *obs_find_module_file(obs_module_t *, const char *file)
{
	if (fffTestDataPath.isEmpty())
		return nullptr;
	const QString path = QDir(fffTestDataPath).filePath(QString::fromUtf8(file));
	// The real one answers only for files that exist, and seeding relies on
	// that to tell a build carrying a bundle from one that is not.
	if (!QFileInfo::exists(path))
		return nullptr;
	return strdup(path.toUtf8().constData());
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
