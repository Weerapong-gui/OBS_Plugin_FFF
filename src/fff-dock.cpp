/*
FFF Tools for OBS - control dock
Copyright (C) 2026 Student Union MFU <studentunion.developer@gmail.com>
GPL-2.0-or-later
*/

#include <obs-module.h>
#include <obs-frontend-api.h>

#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>
#include <QWidget>

static void switch_to_scene(const QString &name)
{
	obs_source_t *scene = obs_get_source_by_name(name.toUtf8().constData());
	if (!scene)
		return;
	obs_frontend_set_current_scene(scene);
	obs_source_release(scene);
}

extern "C" void fff_register_dock(void)
{
	QWidget *dock = new QWidget();
	auto *layout = new QVBoxLayout(dock);

	auto *title = new QLabel(QStringLiteral("Student Union FFF"));
	layout->addWidget(title);

	auto *sceneName = new QLineEdit();
	sceneName->setPlaceholderText(QStringLiteral("Scene name"));
	layout->addWidget(sceneName);

	auto *btn = new QPushButton(QStringLiteral("Switch to scene"));
	QObject::connect(btn, &QPushButton::clicked, [sceneName]() {
		const QString name = sceneName->text().trimmed();
		if (!name.isEmpty())
			switch_to_scene(name);
	});
	layout->addWidget(btn);

	auto *status = new QLabel();
	auto *checkBtn = new QPushButton(QStringLiteral("Streaming status"));
	QObject::connect(checkBtn, &QPushButton::clicked, [status]() {
		status->setText(obs_frontend_streaming_active()
					? QStringLiteral("LIVE")
					: QStringLiteral("offline"));
	});
	layout->addWidget(checkBtn);
	layout->addWidget(status);

	layout->addStretch();

	obs_frontend_add_dock_by_id("fff_dock", "FFF Control", dock);
}
