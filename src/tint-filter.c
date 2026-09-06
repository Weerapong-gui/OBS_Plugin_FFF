/*
FFF Tools for OBS - simple color tint filter
Copyright (C) 2026 Student Union MFU <studentunion.developer@gmail.com>
GPL-2.0-or-later
*/

#include <obs-module.h>
#include <graphics/vec4.h>

struct tint_data {
	obs_source_t *context;
	gs_effect_t *effect;
	gs_eparam_t *color_param;
	struct vec4 color;
};

static const char *tint_get_name(void *unused)
{
	UNUSED_PARAMETER(unused);
	return obs_module_text("FFFTint");
}

static void tint_update(void *data, obs_data_t *settings)
{
	struct tint_data *f = data;
	uint32_t c = (uint32_t)obs_data_get_int(settings, "color");
	vec4_from_rgba(&f->color, c);
}

static void *tint_create(obs_data_t *settings, obs_source_t *source)
{
	struct tint_data *f = bzalloc(sizeof(struct tint_data));
	f->context = source;

	char *path = obs_module_file("tint.effect");
	obs_enter_graphics();
	f->effect = gs_effect_create_from_file(path, NULL);
	obs_leave_graphics();
	bfree(path);

	if (!f->effect) {
		bfree(f);
		return NULL;
	}

	f->color_param = gs_effect_get_param_by_name(f->effect, "tint");
	tint_update(f, settings);
	return f;
}

static void tint_destroy(void *data)
{
	struct tint_data *f = data;
	if (!f)
		return;
	obs_enter_graphics();
	gs_effect_destroy(f->effect);
	obs_leave_graphics();
	bfree(f);
}

static void tint_render(void *data, gs_effect_t *effect)
{
	UNUSED_PARAMETER(effect);
	struct tint_data *f = data;

	if (!obs_source_process_filter_begin(f->context, GS_RGBA, OBS_ALLOW_DIRECT_RENDERING))
		return;

	gs_effect_set_vec4(f->color_param, &f->color);
	obs_source_process_filter_end(f->context, f->effect, 0, 0);
}

static obs_properties_t *tint_properties(void *data)
{
	UNUSED_PARAMETER(data);
	obs_properties_t *props = obs_properties_create();
	obs_properties_add_color(props, "color", obs_module_text("Color"));
	return props;
}

static void tint_defaults(obs_data_t *settings)
{
	obs_data_set_default_int(settings, "color", 0xFFFFFFFF);
}

struct obs_source_info fff_tint_filter = {
	.id = "fff_tint_filter",
	.type = OBS_SOURCE_TYPE_FILTER,
	.output_flags = OBS_SOURCE_VIDEO,
	.get_name = tint_get_name,
	.create = tint_create,
	.destroy = tint_destroy,
	.update = tint_update,
	.video_render = tint_render,
	.get_properties = tint_properties,
	.get_defaults = tint_defaults,
};
