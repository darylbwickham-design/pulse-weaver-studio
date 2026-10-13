#include <obs-module.h>
#include <util/platform.h>
#include <algorithm>
#include <atomic>
#include <cstring>
#include <mutex>
#include <string>

OBS_DECLARE_MODULE()
MODULE_EXPORT const char *obs_module_description(void) { return "Altron live-camera teleport and user-supplied arrival/departure audio"; }
MODULE_EXPORT const char *obs_module_name(void) { return "Altron Teleport (alpha)"; }

namespace {
constexpr const char *sound_id = "pw_altron_teleport_audio";
constexpr const char *filter_id = "pw_altron_teleport_filter";
constexpr const char *sound_default = "Altron Teleport Audio";

struct SoundBus;
struct AudioTap { SoundBus *bus; int index; };
struct SoundBus {
  obs_source_t *source;
  obs_source_t *players[2]{};
  std::mutex mutex;
  int selected = -1;
  int queued = -1;
  float remaining = 0;
  bool playing = false;
  float startup_grace = 0;
  int delays[2]{};
  std::string files[2];
  AudioTap taps[2]{};
  std::atomic<int> audible{-1};
  uint64_t audio_clock = 0; // Accessed only by the selected decoder's capture callback.
};

void sound_capture(void *data, obs_source_t *, const audio_data *audio, bool)
{
  auto *tap = static_cast<AudioTap *>(data);
  auto *s = tap->bus;
  if (s->audible.load() != tap->index || !obs_source_active(s->source)) return;
  obs_audio_info info{};
  if (!obs_get_audio_info(&info)) return;
  obs_source_audio output{};
  for (size_t ch = 0; ch < MAX_AV_PLANES; ++ch) output.data[ch] = audio->data[ch];
  output.frames = audio->frames;
  // Different files have different decoder clocks. Rebase each captured packet to
  // the live audio clock so switching/restarting a sound cannot replay stale timestamps.
  const uint64_t now = os_gettime_ns();
  if (!s->audio_clock || now > s->audio_clock + 200000000 || s->audio_clock > now + 200000000) s->audio_clock = now;
  output.timestamp = s->audio_clock;
  s->audio_clock += uint64_t(audio->frames) * 1000000000 / info.samples_per_sec;
  output.samples_per_sec = info.samples_per_sec;
  output.speakers = info.speakers;
  output.format = AUDIO_FORMAT_FLOAT_PLANAR;
  obs_source_output_audio(s->source, &output);
}

void stop_sound(SoundBus *s)
{
  s->queued = -1;
  s->audible = -1;
  s->playing = false;
  s->selected = -1;
  for (int i = 0; i < 2; ++i) if (s->players[i]) {
    obs_source_remove_audio_capture_callback(s->players[i], sound_capture, &s->taps[i]);
    obs_source_media_stop(s->players[i]);
    obs_source_dec_showing(s->players[i]);
    obs_source_release(s->players[i]);
    s->players[i] = nullptr;
  }
}

void sound_cue(void *data, calldata_t *args)
{
  auto *s = static_cast<SoundBus *>(data);
  std::lock_guard<std::mutex> lock(s->mutex);
  stop_sound(s);
  const char *kind = calldata_string(args, "kind");
  int index = kind && std::strcmp(kind, "in") == 0 ? 0 : kind && std::strcmp(kind, "out") == 0 ? 1 : -1;
  if (index < 0 || !obs_source_active(s->source) || s->files[index].empty()) return;
  if (!os_file_exists(s->files[index].c_str())) { blog(LOG_WARNING, "[Altron] %s sound is missing; teleport continues silently", kind); return; }
  s->queued = index;
  s->remaining = s->delays[index] / 1000.0f;
}

void sound_update(void *data, obs_data_t *settings)
{
  auto *s = static_cast<SoundBus *>(data);
  std::lock_guard<std::mutex> lock(s->mutex);
  stop_sound(s);
  const char *keys[] = {"arrival_file", "departure_file"};
  for (int i = 0; i < 2; ++i) {
    s->files[i] = obs_data_get_string(settings, keys[i]);
    s->delays[i] = int(std::clamp(obs_data_get_int(settings, i ? "departure_delay" : "arrival_delay"), int64_t(0), int64_t(1500)));
  }
  // Apply gain once, to the public source, so stream and headphone monitoring match.
  obs_source_set_volume(s->source, float(std::clamp(obs_data_get_double(settings, "volume"), 0.0, 100.0) / 100.0));
  obs_source_set_monitoring_type(s->source, obs_data_get_bool(settings, "monitor") ? OBS_MONITORING_TYPE_MONITOR_AND_OUTPUT : OBS_MONITORING_TYPE_NONE);
}

void *sound_create(obs_data_t *settings, obs_source_t *source)
{
  auto *s = new SoundBus;
  s->source = source;
  proc_handler_add(obs_source_get_proc_handler(source), "void teleport_sound(string kind)", sound_cue, s);
  sound_update(s, settings);
  return s;
}

void sound_destroy(void *data)
{
  auto *s = static_cast<SoundBus *>(data);
  stop_sound(s);
  delete s;
}

void sound_tick(void *data, float seconds)
{
  auto *s = static_cast<SoundBus *>(data);
  std::lock_guard<std::mutex> lock(s->mutex);
  if (!obs_source_active(s->source)) { stop_sound(s); return; }
  if (s->queued >= 0 && (s->remaining -= seconds) <= 0) {
    s->selected = s->queued;
    s->queued = -1;
    const int i = s->selected;
    auto *media = obs_data_create();
    obs_data_set_bool(media, "is_local_file", true);
    obs_data_set_string(media, "local_file", s->files[i].c_str());
    obs_data_set_bool(media, "restart_on_activate", true);
    obs_data_set_bool(media, "close_when_inactive", false);
    obs_data_set_bool(media, "looping", false);
    obs_data_set_bool(media, "log_changes", false);
    s->players[i] = obs_source_create_private("ffmpeg_source", i ? "Altron departure decoder" : "Altron arrival decoder", media);
    obs_data_release(media);
    if (!s->players[i]) { stop_sound(s); return; }
    s->taps[i] = {s, i};
    obs_source_add_audio_capture_callback(s->players[i], sound_capture, &s->taps[i]);
    obs_source_inc_showing(s->players[i]);
    s->playing = true;
    s->audible = s->selected;
    s->startup_grace = .25f; // FFmpeg restart completes asynchronously.
    obs_source_media_restart(s->players[s->selected]);
  }
  if (s->playing && s->selected >= 0) {
    auto state = obs_source_media_get_state(s->players[s->selected]);
    s->startup_grace -= seconds;
    if (s->startup_grace <= 0 && (state == OBS_MEDIA_STATE_ENDED || state == OBS_MEDIA_STATE_ERROR || state == OBS_MEDIA_STATE_STOPPED)) {
      stop_sound(s);
    }
  }
}

void sound_defaults(obs_data_t *s)
{
  obs_data_set_default_double(s, "volume", 70);
  obs_data_set_default_bool(s, "monitor", false);
}

bool sound_test(obs_properties_t *, obs_property_t *property, void *data)
{
  calldata_t args;
  calldata_init(&args);
  calldata_set_string(&args, "kind", std::strcmp(obs_property_name(property), "test_arrival") == 0 ? "in" : "out");
  sound_cue(data, &args);
  calldata_free(&args);
  return false;
}

obs_properties_t *sound_properties(void *)
{
  auto *p = obs_properties_create();
  const char *extensions = "Audio (*.wav *.mp3 *.ogg *.flac *.m4a);;All files (*.*)";
  obs_properties_add_path(p, "arrival_file", "Arrival sound (optional)", OBS_PATH_FILE, extensions, nullptr);
  obs_properties_add_path(p, "departure_file", "Departure sound (optional)", OBS_PATH_FILE, extensions, nullptr);
  obs_properties_add_float_slider(p, "volume", "Sound volume (%)", 0, 100, 1);
  obs_properties_add_int(p, "arrival_delay", "Arrival sound delay (ms)", 0, 1500, 10);
  obs_properties_add_int(p, "departure_delay", "Departure sound delay (ms)", 0, 1500, 10);
  obs_properties_add_bool(p, "monitor", "Also hear through the app's audio monitoring device");
  obs_properties_add_button(p, "test_arrival", "Test arrival sound (show scene must be live)", sound_test);
  obs_properties_add_button(p, "test_departure", "Test departure sound (show scene must be live)", sound_test);
  return p;
}

struct Teleport {
  obs_source_t *source;
  gs_effect_t *effect{};
  std::mutex mutex;
  float elapsed = -1, preview = -1, intensity = 1.3f, cx = .54f, cy = .63f;
  int direction = 0;
  int64_t nonce = 0;
  std::string audio_source = sound_default;
};

void filter_status(void *data, calldata_t *args)
{
  auto *t = static_cast<Teleport *>(data);
  std::lock_guard<std::mutex> lock(t->mutex);
  calldata_set_float(args, "elapsed", t->elapsed);
  calldata_set_bool(args, "running", t->direction != 0);
}

void notify_audio(Teleport *t, const char *kind)
{
  if (t->audio_source.empty()) return;
  auto *audio = obs_get_source_by_name(t->audio_source.c_str());
  if (!audio) return;
  if (std::strcmp(obs_source_get_unversioned_id(audio), sound_id) == 0) {
    calldata_t args;
    calldata_init(&args);
    calldata_set_string(&args, "kind", kind);
    proc_handler_call(obs_source_get_proc_handler(audio), "teleport_sound", &args);
    calldata_free(&args);
  }
  obs_source_release(audio);
}

void cue(Teleport *t, const char *kind)
{
  if (!kind) return;
  if (!std::strcmp(kind, "out")) { t->elapsed = 0; t->direction = 1; }
  else if (!std::strcmp(kind, "in")) { t->elapsed = 2.60f; t->direction = -1; }
  else if (!std::strcmp(kind, "hide")) { t->elapsed = 2.7f; t->direction = 0; }
  else if (!std::strcmp(kind, "reset")) { t->elapsed = -1; t->direction = 0; }
  else return;
  t->preview = -1;
  notify_audio(t, kind);
}

void filter_update(void *data, obs_data_t *settings)
{
  auto *t = static_cast<Teleport *>(data);
  std::lock_guard<std::mutex> lock(t->mutex);
  t->intensity = float(std::clamp(obs_data_get_double(settings, "intensity"), .2, 2.0));
  t->cx = float(obs_data_get_double(settings, "centre_x"));
  t->cy = float(obs_data_get_double(settings, "centre_y"));
  t->preview = float(obs_data_get_double(settings, "preview_time"));
  t->audio_source = obs_data_get_string(settings, "audio_source");
  int64_t nonce = obs_data_get_int(settings, "cue_id");
  if (nonce != t->nonce) { t->nonce = nonce; cue(t, obs_data_get_string(settings, "cue")); }
}

void *filter_create(obs_data_t *settings, obs_source_t *source)
{
  auto *t = new Teleport;
  t->source = source;
  t->nonce = obs_data_get_int(settings, "cue_id"); // A saved cue is never replayed on startup.
  char *file = obs_module_file("white-teleport.effect");
  obs_enter_graphics();
  t->effect = file ? gs_effect_create_from_file(file, nullptr) : nullptr;
  obs_leave_graphics();
  bfree(file);
  if (!t->effect) { delete t; return nullptr; }
  filter_update(t, settings);
  proc_handler_add(obs_source_get_proc_handler(source), "void teleport_status(out float elapsed, out bool running)", filter_status, t);
  return t;
}

void filter_destroy(void *data)
{
  auto *t = static_cast<Teleport *>(data);
  notify_audio(t, "reset");
  obs_enter_graphics();
  gs_effect_destroy(t->effect);
  obs_leave_graphics();
  delete t;
}

void filter_defaults(obs_data_t *s)
{
  obs_data_set_default_double(s, "intensity", 1.3);
  obs_data_set_default_double(s, "centre_x", .54);
  obs_data_set_default_double(s, "centre_y", .63);
  obs_data_set_default_double(s, "preview_time", -1);
  obs_data_set_default_string(s, "audio_source", sound_default);
}

bool filter_test(obs_properties_t *, obs_property_t *p, void *data)
{
  auto *t = static_cast<Teleport *>(data);
  std::lock_guard<std::mutex> lock(t->mutex);
  cue(t, obs_property_name(p));
  return false;
}

obs_properties_t *filter_properties(void *)
{
  auto *p = obs_properties_create();
  obs_properties_add_float_slider(p, "intensity", "Energy strength", .2, 2, .1);
  obs_properties_add_float_slider(p, "centre_x", "Energy centre X", 0, 1, .01);
  obs_properties_add_float_slider(p, "centre_y", "Energy centre Y", 0, 1, .01);
  obs_properties_add_text(p, "audio_source", "Teleport audio source name (blank = silent)", OBS_TEXT_DEFAULT);
  obs_properties_add_float(p, "preview_time", "Frozen preview (-1 = live)", -1, 3, .05);
  obs_properties_add_button(p, "out", "Test departure", filter_test);
  obs_properties_add_button(p, "in", "Test arrival", filter_test);
  obs_properties_add_button(p, "reset", "Reset / cancel sound", filter_test);
  return p;
}

void filter_tick(void *data, float seconds)
{
  auto *t = static_cast<Teleport *>(data);
  std::lock_guard<std::mutex> lock(t->mutex);
  if (!obs_source_enabled(t->source)) { if (t->direction) notify_audio(t, "reset"); t->elapsed = -1; t->direction = 0; return; }
  if (t->direction) {
    t->elapsed += seconds * t->direction;
    if (t->direction > 0 && t->elapsed >= 2.7f) { t->elapsed = 2.7f; t->direction = 0; }
    else if (t->direction < 0 && t->elapsed <= 0) { t->elapsed = -1; t->direction = 0; }
  }
}

void filter_render(void *data, gs_effect_t *)
{
  auto *t = static_cast<Teleport *>(data);
  std::lock_guard<std::mutex> lock(t->mutex);
  auto *parent = obs_filter_get_parent(t->source);
  auto width = parent ? obs_source_get_base_width(parent) : 0;
  auto height = parent ? obs_source_get_base_height(parent) : 0;
  if (!width || !height || !obs_source_process_filter_begin(t->source, GS_RGBA, OBS_NO_DIRECT_RENDERING)) return;
  auto set = [&](const char *name, float value) { gs_effect_set_float(gs_effect_get_param_by_name(t->effect, name), value); };
  set("elapsed", t->preview >= 0 ? t->preview : t->elapsed);
  gs_effect_set_int(gs_effect_get_param_by_name(t->effect, "role"), 0);
  set("intensity", t->intensity); set("centre_x", t->cx); set("centre_y", t->cy);
  set("pixel_x", 1.0f / width); set("pixel_y", 1.0f / height);
  obs_source_process_filter_end(t->source, t->effect, width, height);
}
} // namespace

bool obs_module_load(void)
{
  obs_source_info filter{};
  filter.id = filter_id; filter.type = OBS_SOURCE_TYPE_FILTER; filter.output_flags = OBS_SOURCE_VIDEO;
  filter.get_name = [](void *) { return "Altron Teleport (native alpha)"; };
  filter.create = filter_create; filter.destroy = filter_destroy; filter.update = filter_update;
  filter.get_defaults = filter_defaults; filter.get_properties = filter_properties;
  filter.video_tick = filter_tick; filter.video_render = filter_render;
  obs_register_source(&filter);
  obs_source_info sound{};
  sound.id = sound_id; sound.type = OBS_SOURCE_TYPE_INPUT; sound.output_flags = OBS_SOURCE_AUDIO;
  sound.get_name = [](void *) { return "Altron Teleport Audio (native alpha)"; };
  sound.create = sound_create; sound.destroy = sound_destroy; sound.update = sound_update;
  sound.get_defaults = sound_defaults; sound.get_properties = sound_properties;
  sound.video_tick = sound_tick;
  obs_register_source(&sound);
  blog(LOG_INFO, "[Altron] Native teleport alpha 0.1.0 loaded (no Lua required)");
  return true;
}
