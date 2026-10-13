#define NOMINMAX
#include <windows.h>
#include <obs.h>
#include <obs-module.h>
#include <util/platform.h>
#include <media-io/audio-io.h>
#include <media-io/video-io.h>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <mutex>
#include <string>
#include <vector>

static std::atomic<double> audio_peak{0};
static std::atomic<uint64_t> first_audio{0};
static std::mutex frame_mutex;
static std::vector<uint8_t> frame;
static std::atomic<uint32_t> frames{0};
static constexpr uint32_t W = 640, H = 360;
static void check(bool ok, const char *message) { if (!ok) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(2); } }
static void log_output(int level, const char *format, va_list args, void *) { if (level <= LOG_INFO) { vfprintf(stderr, format, args); fputc('\n', stderr); } }
static void capture_audio(void *, size_t, audio_data *data)
{
  auto *samples = reinterpret_cast<float *>(data->data[0]);
  double peak = 0;
  for (size_t i = 0; i < data->frames; ++i) peak = std::max(peak, double(std::abs(samples[i])));
  if (peak > .0001 && first_audio == 0) first_audio = os_gettime_ns();
  double old = audio_peak;
  while (peak > old && !audio_peak.compare_exchange_weak(old, peak)) {}
}
static void capture_video(void *, video_data *data)
{
  std::lock_guard<std::mutex> lock(frame_mutex);
  frame.resize(W * H * 4);
  for (uint32_t y = 0; y < H; ++y) std::memcpy(frame.data() + y * W * 4, data->data[0] + y * data->linesize[0], W * 4);
  ++frames;
}
static double brightness()
{
  std::lock_guard<std::mutex> lock(frame_mutex);
  double sum = 0;
  for (size_t i = 0; i + 3 < frame.size(); i += 4) sum += frame[i] + frame[i+1] + frame[i+2];
  return sum / (W * H * 3);
}
static void snapshot(const std::string &name)
{
  std::lock_guard<std::mutex> lock(frame_mutex);
  std::ofstream out(name, std::ios::binary);
  out << "P6\n" << W << " " << H << "\n255\n";
  for (size_t i = 0; i + 3 < frame.size(); i += 4) out.write(reinterpret_cast<const char *>(frame.data() + i), 3);
}
static void draw(void *, gs_effect_t *)
{
  auto *effect = obs_get_base_effect(OBS_EFFECT_SOLID);
  vec4 color{.12f,.55f,.9f,1};
  gs_effect_set_vec4(gs_effect_get_param_by_name(effect, "color"), &color);
  while (gs_effect_loop(effect, "Solid")) {
    gs_matrix_push(); gs_matrix_translate3f(245,130,0); gs_draw_sprite(nullptr,0,150,200); gs_matrix_pop();
    gs_matrix_push(); gs_matrix_translate3f(265,65,0); gs_draw_sprite(nullptr,0,110,90); gs_matrix_pop();
  }
}
static void load_module(const std::string &dll, const std::string &data)
{
  obs_module_t *module = nullptr;
  check(obs_open_module(&module, dll.c_str(), data.c_str()) == MODULE_SUCCESS, dll.c_str());
  check(obs_init_module(module), "module initialization");
}
static void setcue(obs_source_t *filter, const char *cue)
{
  static int64_t nonce = 100;
  auto *s = obs_source_get_settings(filter);
  obs_data_set_string(s, "cue", cue); obs_data_set_int(s, "cue_id", ++nonce);
  obs_source_update(filter, s); obs_data_release(s);
}
static double elapsed(obs_source_t *filter)
{
  calldata_t args; calldata_init(&args);
  check(proc_handler_call(obs_source_get_proc_handler(filter), "teleport_status", &args), "filter status procedure");
  double t = calldata_float(&args, "elapsed"); calldata_free(&args); return t;
}
static void tone(const std::string &path, double hz)
{
  uint32_t rate = 48000, count = 38400, bytes = count * 2;
  uint16_t format = 1, channels = 1, align = 2, bits = 16;
  std::ofstream out(path, std::ios::binary);
  auto put = [&](const auto &value) { out.write(reinterpret_cast<const char *>(&value), sizeof(value)); };
  out.write("RIFF",4); uint32_t size = bytes + 36; put(size); out.write("WAVEfmt ",8); uint32_t fmt = 16; put(fmt);
  put(format); put(channels); put(rate); uint32_t speed = rate * 2; put(speed); put(align); put(bits);
  out.write("data",4); put(bytes);
  for (uint32_t i = 0; i < count; ++i) { int16_t v = int16_t(std::sin(i * hz * 6.283185307179586 / rate) * 16000); put(v); }
}
int main(int argc, char **argv)
{
  check(argc == 4, "usage: test app-root native-plugin-root results-dir");
  std::string app = argv[1], native = argv[2], results = argv[3];
  check(SetDllDirectoryA((app + "/bin/64bit").c_str()), "runtime dependency search directory");
  base_set_log_handler(log_output, nullptr);
  check(obs_startup("en-US", nullptr, nullptr), "libobs startup");
  obs_add_data_path((app + "/data/libobs/").c_str());
  obs_audio_info ai{}; ai.samples_per_sec = 48000; ai.speakers = SPEAKERS_STEREO;
  check(obs_reset_audio(&ai), "audio startup");
  obs_video_info vi{};
  std::string graphics = app + "/bin/64bit/libobs-d3d11.dll";
  vi.graphics_module = graphics.c_str(); vi.fps_num = 60; vi.fps_den = 1;
  vi.base_width = W; vi.base_height = H; vi.output_width = W; vi.output_height = H;
  vi.output_format = VIDEO_FORMAT_RGBA; vi.colorspace = VIDEO_CS_709; vi.range = VIDEO_RANGE_FULL; vi.gpu_conversion = false;
  check(obs_reset_video(&vi) == OBS_VIDEO_SUCCESS, "D3D11 GPU startup");
  load_module(app + "/obs-plugins/64bit/obs-ffmpeg.dll", app + "/data/obs-plugins/obs-ffmpeg");
  load_module(native + "/obs-plugins/64bit/altron-teleport.dll", native + "/data/obs-plugins/altron-teleport");
  obs_post_load_modules();
  obs_source_info synthetic{};
  synthetic.id = "altron_native_test_camera"; synthetic.type = OBS_SOURCE_TYPE_INPUT;
  synthetic.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_CUSTOM_DRAW;
  synthetic.get_name = [](void *) { return "Synthetic test cutout"; };
  synthetic.create = [](obs_data_t *, obs_source_t *) -> void * { return reinterpret_cast<void *>(1); };
  synthetic.destroy = [](void *) {};
  synthetic.get_width = [](void *) { return W; }; synthetic.get_height = [](void *) { return H; };
  synthetic.video_render = draw;
  obs_register_source(&synthetic);
  auto *camera = obs_source_create(synthetic.id, "Native test camera", nullptr, nullptr);
  auto *camera2 = obs_source_create(synthetic.id, "Independent test camera", nullptr, nullptr);
  auto *filter = obs_source_create("pw_altron_teleport_filter", "Native teleport", nullptr, nullptr);
  auto *filter2 = obs_source_create("pw_altron_teleport_filter", "Independent teleport", nullptr, nullptr);
  check(camera && filter && camera2 && filter2, "native sources created and GPU shader compiled");
  obs_source_filter_add(camera, filter); obs_source_filter_add(camera2, filter2);
  tone(results + "/arrival.wav", 440); tone(results + "/departure.wav", 880);
  auto *sound_settings = obs_data_create();
  obs_data_set_string(sound_settings, "arrival_file", (results + "/arrival.wav").c_str());
  obs_data_set_string(sound_settings, "departure_file", (results + "/departure.wav").c_str());
  obs_data_set_int(sound_settings, "arrival_delay", 350);
  auto *sound = obs_source_create("pw_altron_teleport_audio", "Altron Teleport Audio", sound_settings, nullptr);
  obs_data_release(sound_settings); check(sound, "native sound source created");
  auto *scene = obs_scene_create_private("Isolated alpha test"); obs_scene_add(scene, camera); obs_scene_add(scene, sound);
  obs_set_output_source(0, obs_scene_get_source(scene));
  audio_convert_info audio_info{}; audio_info.format = AUDIO_FORMAT_FLOAT_PLANAR; audio_info.samples_per_sec = 48000; audio_info.speakers = SPEAKERS_STEREO;
  check(audio_output_connect(obs_get_audio(), 0, &audio_info, capture_audio, nullptr), "stream audio callback connected");
  obs_add_raw_video_callback(nullptr, capture_video, nullptr);
  os_sleep_ms(700); check(frames > 5, "GPU video frames produced");
  double baseline = brightness(); check(baseline > 10, "test camera visible"); snapshot(results + "/native-idle.ppm");
  audio_peak = 0; first_audio = 0; setcue(filter, "out"); os_sleep_ms(1200);
  check(elapsed(filter) > .8, "departure timer advances"); check(elapsed(filter2) == -1, "independent filter does not trigger");
  check(audio_peak > .1, "departure sound reaches stream audio"); snapshot(results + "/native-departure.ppm");
  double departure_peak = audio_peak; os_sleep_ms(1700); check(elapsed(filter) >= 2.69, "departure holds hidden");
  check(brightness() < baseline * .08, "actor disappears after departure"); snapshot(results + "/native-hidden.ppm");
  audio_peak = 0; first_audio = 0; auto start = os_gettime_ns(); setcue(filter, "in"); os_sleep_ms(180);
  check(audio_peak < .0001, "arrival sound delay does not play early"); os_sleep_ms(700);
  check(audio_peak > .1, "arrival sound reaches stream audio");
  check(first_audio > start + 250000000, "arrival delay respected"); snapshot(results + "/native-arrival.ppm");
  os_sleep_ms(2100); check(elapsed(filter) == -1, "arrival returns live camera"); check(brightness() > baseline * .8, "actor visible after arrival");
  audio_peak = 0; setcue(filter, "in"); os_sleep_ms(70); setcue(filter, "reset"); os_sleep_ms(650);
  check(audio_peak < .0001, "reset cancels queued audio");
  auto *settings = obs_source_get_settings(sound); obs_data_set_double(settings, "volume", 35); obs_source_update(sound, settings); obs_data_release(settings);
  os_sleep_ms(150); audio_peak = 0; setcue(filter, "out"); os_sleep_ms(900);
  check(audio_peak > departure_peak * .4 && audio_peak < departure_peak * .6, "sound volume applies once");
  setcue(filter, "reset"); os_sleep_ms(150); audio_peak = 0;
  settings = obs_source_get_settings(sound); obs_data_set_string(settings, "departure_file", (results + "/missing.wav").c_str()); obs_source_update(sound, settings); obs_data_release(settings);
  os_sleep_ms(150); setcue(filter, "out"); os_sleep_ms(550);
  check(audio_peak < .0001 && elapsed(filter) > .3, "missing sound does not block teleport");
  setcue(filter, "reset");
  std::printf("PASS: real libobs/GPU frames, native out/in/hold/reset, independent cameras, stream audio, delay, cancellation, volume and missing-file fallback.\n");
  obs_remove_raw_video_callback(capture_video, nullptr); audio_output_disconnect(obs_get_audio(), 0, capture_audio, nullptr);
  obs_set_output_source(0, nullptr); obs_scene_release(scene);
  obs_source_release(sound); obs_source_release(camera); obs_source_release(camera2); obs_source_release(filter); obs_source_release(filter2);
  os_sleep_ms(150); // Let the render/audio passes release their last source references.
  obs_wait_for_destroy_queue();
  size_t remaining = 0;
  obs_enum_all_sources([](void *p, obs_source_t *) { ++*static_cast<size_t *>(p); return true; }, &remaining);
  check(remaining == 0, "all native test sources are released before unloading modules");
  std::fflush(stdout);
  obs_shutdown(); return 0;
}

