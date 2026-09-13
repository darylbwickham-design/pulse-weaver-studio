#include <obs.h>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>

static std::atomic<unsigned> destroyed{0}, rendered{0};
static void *create(obs_data_t *, obs_source_t *) { return new int(1); }
static void destroy(void *data) { delete static_cast<int *>(data); ++destroyed; }
static bool render(void *, uint64_t *, obs_source_audio_mix *, uint32_t, size_t, size_t)
{
    ++rendered;
    return false;
}
static void select_duplication_source(obs_source_t *source)
{
    calldata_t data;
    calldata_init(&data);
    calldata_set_ptr(&data, "source", source);
    signal_handler_signal(obs_get_signal_handler(), "deduplication_changed", &data);
    calldata_free(&data);
}
int main()
{
    for (unsigned cycle = 0; cycle < 5; ++cycle) {
        if (!obs_startup("en-US", nullptr, nullptr)) return 1;
        obs_audio_info info{};
        info.samples_per_sec = 48000;
        info.speakers = SPEAKERS_STEREO;
        if (!obs_reset_audio(&info)) return 2;
        obs_source_info type{};
        type.id = "pulse_audio_lifetime_fixture";
        type.type = OBS_SOURCE_TYPE_INPUT;
        type.output_flags = OBS_SOURCE_AUDIO;
        type.get_name = [](void *) { return "Silent lifetime fixture"; };
        type.create = create;
        type.destroy = destroy;
        type.audio_render = render;
        obs_register_source(&type);
        obs_source_t *survivor = obs_source_create_private(type.id, "survivor", nullptr);
        if (!survivor) return 3;
        const unsigned before = rendered.load();
        for (unsigned i = 0; i < 300; ++i) {
            obs_source_t *retiring = obs_source_create_private(type.id, "retiring", nullptr);
            if (!retiring) return 4;
            select_duplication_source(retiring);
            obs_source_release(retiring);
            // Exercise source destruction while the independent audio callback
            // continues. Deliberately omit device-disconnect notifications.
            obs_wait_for_destroy_queue();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            if (i % 3 == 0) select_duplication_source(nullptr);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(60));
        if (rendered.load() <= before) return 5;
        obs_source_release(survivor);
        // The current render pass may legitimately retain the last source
        // until its next buffer finishes; wait for that bounded handoff.
        for (unsigned wait = 0; wait < 100 && destroyed.load() != (cycle + 1) * 301; ++wait) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            obs_wait_for_destroy_queue();
        }
        // The weak reference must not keep any capture or scene source alive.
        if (destroyed.load() != (cycle + 1) * 301) return 6;
        obs_shutdown();
    }
    std::printf("PASS: 1,500 concurrent source retirements and 5 audio shutdown cycles; %u audio renders.\n", rendered.load());
    return 0;
}
