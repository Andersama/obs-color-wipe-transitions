#include <obs-module.h>
#include <graphics/image-file.h>
#include <util/dstr.h>

OBS_DECLARE_MODULE()

OBS_MODULE_USE_DEFAULT_LOCALE("obs-color-wipe-transitions", "en-US")

#define S_INV                   "animated_wipe_invert"
#define S_TRANSITION            "animated_wipe_transition"
#define S_COLOR_A               "animated_wipe_color_target_a"
#define S_COLOR_B               "animated_wipe_color_target_b"

#define T_TRANSITION            obs_module_text("AnimatedWipe.Transition")
#define T_INV                   obs_module_text("AnimatedWipe.Invert")
#define T_COLOR_A               obs_module_text("AnimatedWipe.ColorTargetA")
#define T_COLOR_B               obs_module_text("AnimatedWipe.ColorTargetB")

struct animated_wipe_info {
	obs_source_t *source;

	gs_effect_t *effect;
	gs_eparam_t *ep_a_tex;
	gs_eparam_t *ep_b_tex;
	gs_eparam_t *ep_c_tex;
	//gs_eparam_t *ep_progress;
	gs_eparam_t *ep_invert;
	gs_eparam_t *ep_color_target_a;
	gs_eparam_t *ep_color_target_b;

	gs_image_file_t animated_image;
	
	bool invert;

	obs_data_t *wipes_list;
};

static const char *animated_wipe_get_name(void *type_data)
{
	UNUSED_PARAMETER(type_data);
	return obs_module_text("AnimatedWipeTransition");
}

static void animated_wipe_update(void *data, obs_data_t *settings)
{
	struct animated_wipe_info *awipe = data;

	const char *name = obs_data_get_string(settings, S_TRANSITION);
	awipe->invert = obs_data_get_bool(settings, S_INV);
	
	struct vec4 color_target_a;
	struct vec4 color_target_b;

	vec4_from_rgba(&color_target_a, ((unsigned int)obs_data_get_int(settings, S_COLOR_A)));
	gs_effect_set_vec4(awipe->ep_color_target_a, &color_target_a);

	vec4_from_rgba(&color_target_b, ((unsigned int)obs_data_get_int(settings, S_COLOR_B)));
	gs_effect_set_vec4(awipe->ep_color_target_b, &color_target_b);

	struct dstr path = {0};

	dstr_copy(&path, "animated_wipes/");
	dstr_cat(&path, name);

	char *file = obs_module_file(path.array);

	obs_enter_graphics();
	gs_image_file_free(&awipe->animated_image);
	obs_leave_graphics();

	gs_image_file_init(&awipe->animated_image, file);

	obs_enter_graphics();
	gs_image_file_init_texture(&awipe->animated_image);
	obs_leave_graphics();

	bfree(file);
	dstr_free(&path);

	UNUSED_PARAMETER(settings);
}

static void animated_wipe_get_list(void *data)
{
	struct animated_wipe_info *awipe = data;

	char *path = obs_module_file("animated_wipes/wipes.json");

	awipe->wipes_list = obs_data_create_from_json_file(path);

	bfree(path);
}

static void *animated_wipe_create(obs_data_t *settings, obs_source_t *source)
{
	struct animated_wipe_info *awipe;
	gs_effect_t *effect;
	char *file = obs_module_file("animated_wipe_transition.effect");
	char *errors = NULL;

	obs_enter_graphics();
	effect = gs_effect_create_from_file(file, errors);
	obs_leave_graphics();

	if (!effect) {
		blog(LOG_ERROR, "Could not open animated_wipe_transition.effect from: \n%s", (file == NULL || strlen(file) == 0 ? "" : file));
		blog(LOG_ERROR, "Unable to create effect.Errors returned from parser : \n%s", (errors == NULL || strlen(errors) == 0 ? "(None)" : errors));
		return NULL;
	}

	bfree(file);

	awipe = bzalloc(sizeof(*awipe));

	awipe->effect      = effect;
	awipe->ep_a_tex    = gs_effect_get_param_by_name(effect, "a_tex");
	awipe->ep_b_tex    = gs_effect_get_param_by_name(effect, "b_tex");
	awipe->ep_c_tex    = gs_effect_get_param_by_name(effect, "c_tex");	
	awipe->ep_color_target_a = gs_effect_get_param_by_name(effect, "color_target_a");
	awipe->ep_color_target_b = gs_effect_get_param_by_name(effect, "color_target_b");
	awipe->ep_invert   = gs_effect_get_param_by_name(effect, "invert");
	awipe->source      = source;

	animated_wipe_get_list(awipe);

	animated_wipe_update(awipe, settings);

	return awipe;
}

static void animated_wipe_destroy(void *data)
{
	struct animated_wipe_info *awipe = data;

	obs_enter_graphics();
	gs_image_file_free(&awipe->animated_image);
	obs_leave_graphics();

	obs_data_release(awipe->wipes_list);

	bfree(awipe);
}

static obs_properties_t *animated_wipe_properties(void *data)
{
	obs_properties_t *props = obs_properties_create();
	struct animated_wipe_info *awipe = data;

	obs_property_t *p;

	p = obs_properties_add_list(props, S_TRANSITION, T_TRANSITION,
		OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	
	obs_data_item_t *item = obs_data_first(awipe->wipes_list);

	for (; item != NULL; obs_data_item_next(&item)) {
		const char *name = obs_data_item_get_name(item);
		const char *path = obs_data_item_get_string(item);
		obs_property_list_add_string(p, obs_module_text(name), path);
	}

	obs_properties_add_color(props, S_COLOR_A, T_COLOR_A);
	obs_properties_add_color(props, S_COLOR_B, T_COLOR_B);
	obs_properties_add_bool(props, S_INV, T_INV);

	return props;
}

static void animated_wipe_defaults(obs_data_t *settings)
{
	obs_data_set_default_string(settings, S_TRANSITION, "linear-h.gif");
	obs_data_set_default_bool(settings, S_INV, false);
	obs_data_set_default_int(settings, S_COLOR_A, 0xffffffff);
	obs_data_set_default_int(settings, S_COLOR_B, 0xff000000);
}

bool gs_image_file_smooth(gs_image_file_t *image, double percentage)
{
	int loops;

	if (!image->is_animated_gif || !image->loaded)
		return false;

	loops = image->gif.loop_count;
	if (loops >= 0xFFFF)
		loops = 0;
	//(image->gif.frame_count) * loops * percentage
	if (!loops || image->cur_loop < loops) {
		uint64_t total_frames = (image->gif.frame_count - 1) * (loops + 1);
		double step = 1.0f / total_frames;
		int new_frame = (int32_t)floor(percentage / step);

		if (new_frame != image->cur_frame) {
			//decode_new_frame(image, new_frame);
			image->cur_frame = new_frame;
			return true;
		}
	}

	return false;
}

static void animated_wipe_callback(void *data, gs_texture_t *a, gs_texture_t *b,
			       float t, uint32_t cx, uint32_t cy)
{
	struct animated_wipe_info *awipe = data;
	//t ranges from 0 (just started) to 1 (transition complete)
	gs_effect_set_texture(awipe->ep_a_tex, a);
	gs_effect_set_texture(awipe->ep_b_tex, b);
	//image->gif.frame_count
	//gs_image_file_tick(awipe->animated_image,);
	if (awipe->invert) {
		gs_image_file_smooth(&awipe->animated_image, 1.0f - t);
	}
	else {
		gs_image_file_smooth(&awipe->animated_image, t);
	}
	gs_image_file_update_texture(&awipe->animated_image);
	gs_effect_set_texture(awipe->ep_c_tex, awipe->animated_image.texture);

	gs_effect_set_bool(awipe->ep_invert, awipe->invert);

	while (gs_effect_loop(awipe->effect, "AnimatedWipe"))
		gs_draw_sprite(NULL, 0, cx, cy);
}

void animated_wipe_video_render(void *data, gs_effect_t *effect)
{
	struct animated_wipe_info *awipe = data;
	obs_transition_video_render(awipe->source, animated_wipe_callback);
	UNUSED_PARAMETER(effect);
}

static float mix_a(void *data, float t)
{
	UNUSED_PARAMETER(data);
	return 1.0f - t;
}

static float mix_b(void *data, float t)
{
	UNUSED_PARAMETER(data);
	return t;
}

bool animated_wipe_audio_render(void *data, uint64_t *ts_out,
		struct obs_source_audio_mix *audio, uint32_t mixers,
		size_t channels, size_t sample_rate)
{
	struct animated_wipe_info *awipe = data;
	return obs_transition_audio_render(awipe->source, ts_out, audio, mixers,
				channels, sample_rate, mix_a, mix_b);
}

struct obs_source_info obs_color_animated_wipe_transition = {
	.id                             = "obs-color-wipe-transitions",
	.type                           = OBS_SOURCE_TYPE_TRANSITION,
	.get_name                       = animated_wipe_get_name,
	.create                         = animated_wipe_create,
	.destroy                        = animated_wipe_destroy,
	.update	                        = animated_wipe_update,
	.video_render                   = animated_wipe_video_render,
	.audio_render                   = animated_wipe_audio_render,
	.get_properties                 = animated_wipe_properties,
	.get_defaults                   = animated_wipe_defaults
};

void obs_module_load(){
	obs_register_source(&obs_color_animated_wipe_transition);
	return true;
}