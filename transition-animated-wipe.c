#include <obs-module.h>
#include <graphics/graphics.h>
#include <graphics/image-file.h>
#include <util/dstr.h>

OBS_DECLARE_MODULE()

OBS_MODULE_USE_DEFAULT_LOCALE("obs-color-wipe-transitions", "en-US")

#define S_INV                   "animated_wipe_invert"
#define S_COLOR_A               "animated_wipe_color_target_a"
#define S_COLOR_B               "animated_wipe_color_target_b"

#define T_TRANSITION            obs_module_text("AnimatedWipe.Transition")
#define T_INV                   obs_module_text("AnimatedWipe.Invert")
#define T_COLOR_A               obs_module_text("AnimatedWipe.ColorTargetA")
#define T_COLOR_B               obs_module_text("AnimatedWipe.ColorTargetB")

enum fade_style {
	FADE_STYLE_FADE_OUT_FADE_IN,
	FADE_STYLE_CROSS_FADE
};

struct animated_wipe_info {
	obs_source_t *source;

	obs_source_t *media_source;
	gs_texrender_t *texrender;

	uint64_t duration_ns;
	uint64_t duration_frames;

	int monitoring_type;

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
	bool transitioning;

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

	const char *path = obs_data_get_string(settings, "path");

	obs_data_t *media_settings = obs_data_create();
	obs_data_set_string(media_settings, "local_file", path);

	obs_source_release(awipe->media_source);
	awipe->media_source = obs_source_create_private("ffmpeg_source", NULL,
		media_settings);
	obs_data_release(media_settings);

	awipe->invert = obs_data_get_bool(settings, S_INV);
	
	struct vec4 color_target_a;
	struct vec4 color_target_b;

	vec4_from_rgba(&color_target_a, ((unsigned int)obs_data_get_int(settings, S_COLOR_A)));
	gs_effect_set_vec4(awipe->ep_color_target_a, &color_target_a);

	vec4_from_rgba(&color_target_b, ((unsigned int)obs_data_get_int(settings, S_COLOR_B)));
	gs_effect_set_vec4(awipe->ep_color_target_b, &color_target_b);

	awipe->monitoring_type = (int)obs_data_get_int(settings, "audio_monitoring");
	obs_source_set_monitoring_type(awipe->media_source, awipe->monitoring_type);

	UNUSED_PARAMETER(settings);
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

	awipe->texrender = gs_texrender_create(GS_RGBA, GS_ZS_NONE);

	animated_wipe_update(awipe, settings);

	return awipe;
}

static void animated_wipe_destroy(void *data)
{
	struct animated_wipe_info *awipe = data;

	obs_enter_graphics();
	//gs_image_file_free(&awipe->animated_image);
	gs_texrender_destroy(awipe->texrender);
	obs_leave_graphics();

	obs_data_release(awipe->wipes_list);

	bfree(awipe);
}

#define FILE_FILTER \
	"Video Files (*.mp4 *.ts *.mov *.wmv *.flv *.mkv *.avi *.gif *.webm);;"

static obs_properties_t *animated_wipe_properties(void *data)
{
	obs_properties_t *props = obs_properties_create();
	struct animated_wipe_info *awipe = data;

	obs_property_t *p;

	obs_properties_add_path(props, "path",
		obs_module_text("VideoFile"),
		OBS_PATH_FILE,
		FILE_FILTER, NULL);

	obs_properties_add_color(props, S_COLOR_A, T_COLOR_A);
	obs_properties_add_color(props, S_COLOR_B, T_COLOR_B);
	obs_properties_add_bool(props, S_INV, T_INV);

	obs_property_t *monitor_list = obs_properties_add_list(props,
		"audio_monitoring", obs_module_text("AudioMonitoring"),
		OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(monitor_list,
		obs_module_text("AudioMonitoring.None"),
		OBS_MONITORING_TYPE_NONE);
	obs_property_list_add_int(monitor_list,
		obs_module_text("AudioMonitoring.MonitorOnly"),
		OBS_MONITORING_TYPE_MONITOR_ONLY);
	obs_property_list_add_int(monitor_list,
		obs_module_text("AudioMonitoring.Both"),
		OBS_MONITORING_TYPE_MONITOR_AND_OUTPUT);

	return props;
}

static void animated_wipe_defaults(obs_data_t *settings)
{
	obs_data_set_default_bool(settings, S_INV, false);
	obs_data_set_default_int(settings, S_COLOR_A, 0xffffffff);
	obs_data_set_default_int(settings, S_COLOR_B, 0xff000000);
}

static void animated_wipe_callback(void *data, gs_texture_t *a, gs_texture_t *b,
			       float t, uint32_t cx, uint32_t cy)
{
	struct animated_wipe_info *awipe = (struct animated_wipe_info *)data;

	float source_cx = (float)obs_source_get_width(awipe->source);
	float source_cy = (float)obs_source_get_height(awipe->source);

	uint32_t media_cx = obs_source_get_width(awipe->media_source);
	uint32_t media_cy = obs_source_get_height(awipe->media_source);

	if (!media_cx || !media_cy)
		return;

	float scale_x = source_cx / (float)media_cx;
	float scale_y = source_cy / (float)media_cy;

	//t ranges from 0 (just started) to 1 (transition complete)
	gs_effect_set_texture(awipe->ep_a_tex, a);
	gs_effect_set_texture(awipe->ep_b_tex, b);

	gs_texrender_reset(awipe->texrender);
	if (gs_texrender_begin(awipe->texrender, media_cx, media_cy)) {
		struct vec4 clear_color;
		vec4_zero(&clear_color);

		gs_matrix_scale3f(scale_x, scale_y, 1.0f);
		obs_source_video_render(awipe->media_source);

		gs_texrender_end(awipe->texrender);
	} else {
		return;
	}
	gs_texture_t *tex = gs_texrender_get_texture(awipe->texrender);

	gs_effect_set_texture(awipe->ep_c_tex, tex);

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
	uint64_t ts = 0;

	if (!obs_source_audio_pending(awipe->media_source)) {
		ts = obs_source_get_audio_timestamp(awipe->media_source);
		if (!ts)
			return false;
	}

	bool success = obs_transition_audio_render(awipe->source, ts_out, audio, mixers,
				channels, sample_rate, mix_a, mix_b);
	if (!ts)
		return success;

	if (!*ts_out || ts < *ts_out)
		*ts_out = ts;

	struct obs_source_audio_mix child_audio;
	obs_source_get_audio_mix(awipe->media_source, &child_audio);

	for (size_t mix = 0; mix < MAX_AUDIO_MIXES; mix++) {
		if ((mixers & (1 << mix)) == 0)
			continue;

		for (size_t ch = 0; ch < channels; ch++) {
			register float *out = audio->output[mix].data[ch];
			register float *in = child_audio.output[mix].data[ch];
			register float *end = in + AUDIO_OUTPUT_FRAMES;

			while (in < end)
				*(out++) += *(in++);
		}
	}

	return true;
}

static void animated_wipe_transition_start(void *data)
{
	struct animated_wipe_info *awipe = data;

	if (awipe->media_source) {
		calldata_t cd = { 0 };

		proc_handler_t *ph =
			obs_source_get_proc_handler(awipe->media_source);

		if (awipe->transitioning) {
			proc_handler_call(ph, "restart", &cd);
			return;
		}

		proc_handler_call(ph, "get_duration", &cd);
		proc_handler_call(ph, "get_nb_frames", &cd);
		awipe->duration_ns = (uint64_t)calldata_int(&cd, "duration");
		awipe->duration_frames = (uint64_t)calldata_int(&cd, "num_frames");

		obs_transition_enable_fixed(awipe->source, true,
			(uint32_t)(awipe->duration_ns / 1000000));

		calldata_free(&cd);

		obs_source_add_active_child(awipe->source, awipe->media_source);
	}

	awipe->transitioning = true;
}

static void animated_wipe_transition_stop(void *data)
{
	struct animated_wipe_info *awipe = data;

	if (awipe->media_source)
		obs_source_remove_active_child(awipe->source, awipe->media_source);

	awipe->transitioning = false;
}

static void animated_wipe_enum_active_sources(void *data,
	obs_source_enum_proc_t enum_callback, void *param)
{
	struct animated_wipe_info *awipe = data;
	if (awipe->media_source && awipe->transitioning)
		enum_callback(awipe->source, awipe->media_source, param);
}

static void animated_wipe_enum_all_sources(void *data,
	obs_source_enum_proc_t enum_callback, void *param)
{
	struct animated_wipe_info *awipe = data;
	if (awipe->media_source)
		enum_callback(awipe->source, awipe->media_source, param);
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
	.get_defaults                   = animated_wipe_defaults,
	.enum_active_sources            = animated_wipe_enum_active_sources,
	.enum_all_sources               = animated_wipe_enum_all_sources,
	.transition_start               = animated_wipe_transition_start,
	.transition_stop                = animated_wipe_transition_stop
};

void obs_module_load(){
	obs_register_source(&obs_color_animated_wipe_transition);
	return true;
}