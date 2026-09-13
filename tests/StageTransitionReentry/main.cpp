#include <obs.h>
#include <cstdio>
#include <cstdlib>

static unsigned destroyed = 0;
static void check(bool ok, const char *message)
{
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
static void check_source(obs_source_t *transition, obs_transition_target slot, obs_source_t *expected)
{
    obs_source_t *actual = obs_transition_get_source(transition, slot);
    check(actual == expected, "transition child must be the intended scene");
    obs_source_release(actual);
}
int main()
{
    check(obs_startup("en-US", nullptr, nullptr), "OBS startup");
    obs_source_info type{};
    type.id = "pulse_reentry_fixture";
    type.type = OBS_SOURCE_TYPE_TRANSITION;
    type.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_CUSTOM_DRAW;
    type.get_name = [](void *) { return "Fixed-duration stage fixture"; };
    type.create = [](obs_data_t *, obs_source_t *source) -> void * {
        obs_transition_enable_fixed(source, true, 60000);
        return new int(1);
    };
    type.destroy = [](void *data) { delete static_cast<int *>(data); ++destroyed; };
    type.video_render = [](void *, gs_effect_t *) {};
    type.audio_render = [](void *, uint64_t *, obs_source_audio_mix *, uint32_t, size_t, size_t) { return false; };
    obs_register_source(&type);
    obs_scene_t *scenes[] = {obs_scene_create_private("A"), obs_scene_create_private("B"), obs_scene_create_private("C")};
    // Independent portrait/program and provider routes, with fixed-duration
    // transitions like stingers. No network, media files or user settings.
    for (unsigned route = 0; route < 7; ++route) {
        obs_source_t *transition = obs_source_create_private(type.id, "cached stinger", nullptr);
        check(transition != nullptr, "create transition");
        obs_transition_set_size(transition, route % 2 ? 1080 : 1920, route % 2 ? 1920 : 1080);
        obs_transition_set(transition, obs_scene_get_source(scenes[0]));
        for (unsigned i = 0; i < 300; ++i) {
            obs_source_t *previous = obs_scene_get_source(scenes[i % 3]);
            obs_source_t *next = obs_scene_get_source(scenes[(i + 1) % 3]);
            // Exact legacy frontend sequence when the current canvas source
            // is the same cached stinger: must remain safe in private builds.
            obs_transition_set(transition, transition);
            check_source(transition, OBS_TRANSITION_SOURCE_A, obs_scene_get_source(scenes[i ? (i - 1) % 3 : 0]));
            check(obs_transition_start(transition, OBS_TRANSITION_MODE_AUTO, 0, next), "restart during stinger");
            check_source(transition, OBS_TRANSITION_SOURCE_A, previous);
            check_source(transition, OBS_TRANSITION_SOURCE_B, next);
            check(!obs_transition_start(transition, OBS_TRANSITION_MODE_AUTO, 0, transition), "reject self destination");
            check_source(transition, OBS_TRANSITION_SOURCE_B, next);
        }
        obs_source_t *last = obs_scene_get_source(scenes[0]);
        obs_transition_set(transition, last);
        check(!obs_transition_is_active(transition), "cut cancels transition");
        check_source(transition, OBS_TRANSITION_SOURCE_A, last);
        check(!obs_transition_start(transition, OBS_TRANSITION_MODE_AUTO, 0, last), "same scene is a no-op");
        obs_source_release(transition);
    }
    for (auto scene : scenes) obs_scene_release(scene);
    obs_wait_for_destroy_queue();
    check(destroyed == 7, "transitions released without self-reference leaks");
    obs_shutdown();
    std::puts("PASS: 2,100 mid-transition stage changes across 7 routes; cuts, self-target rejection and clean destruction.");
}
