#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <memory>
#include <filesystem>
#include <fstream>
#include <string>
extern "C" {
#include "media-playback.h"
#include "cache.h"
#include <media-io/video-frame.h>
}

static int checks;
static void check(bool value, const char *description)
{
    if (!value) { std::fprintf(stderr, "FAIL: %s\n", description); std::exit(1); }
    ++checks; std::printf("PASS: %s\n", description);
}
template<class F> static bool waitFor(F predicate)
{
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(25);
    while (!predicate()) {
        if (std::chrono::steady_clock::now() > end) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return true;
}
struct Player {
    mp_cache_t cache{};
    std::atomic<bool> ready{false};
    std::atomic<int> video{0}, audio{0};
    Player(const char *path, bool share = true, int speed = 100) {
        mp_media_info info{};
        info.path = path; info.speed = speed; info.is_local_file = true;
        info.full_decode = true; info.share_cache = share; info.request_preload = true;
        info.opaque = this;
        info.v_preload_cb = [](void *p, obs_source_frame *) { static_cast<Player *>(p)->ready.store(true); };
        info.v_cb = [](void *p, obs_source_frame *) { ++static_cast<Player *>(p)->video; };
        info.a_cb = [](void *p, obs_source_audio *) { ++static_cast<Player *>(p)->audio; };
        check(mp_cache_init(&cache, &info), "Open preload player");
        mp_cache_preload_frame(&cache);
    }
    ~Player() { mp_cache_free(&cache); }
    void awaitReady() { check(waitFor([&] { return ready.load(); }), "Preload completes"); }
};

int main(int argc, char **argv)
{
    if (argc != 2) return 2;
    const long allocationsBefore = bnum_allocs();
    auto first = std::make_unique<Player>(argv[1]);
    auto second = std::make_unique<Player>(argv[1]);
    auto third = std::make_unique<Player>(argv[1]);
    auto fourth = std::make_unique<Player>(argv[1]);
    auto fifth = std::make_unique<Player>(argv[1]);
    for (auto *p : {first.get(), second.get(), third.get(), fourth.get(), fifth.get()}) p->awaitReady();
    check(first->cache.video_frames.num > 0, "Video frames are decoded");
    for (auto *p : {second.get(), third.get(), fourth.get(), fifth.get()}) {
        check(p->cache.video_frames.array == first->cache.video_frames.array, "Five players share one video allocation");
        check(p->cache.audio_segments.array == first->cache.audio_segments.array, "Audio data is shared when present");
        check(p->cache.final_v_duration == first->cache.final_v_duration, "Frame timing is preserved");
    }
    std::printf("CACHE: %zu video frames, %zu audio segments; five players, one decoded allocation.\n",
        first->cache.video_frames.num, first->cache.audio_segments.num);
    uint64_t decodedBytes = 0;
    for (size_t i = 0; i < first->cache.video_frames.num; ++i) {
        const auto &frame = first->cache.video_frames.array[i];
        size_t last = 0;
        for (size_t plane = 1; plane < MAX_AV_PLANES; ++plane) if (frame.data[plane]) last = plane;
        const bool halfHeight = last > 0 && (frame.format == VIDEO_FORMAT_I420 || frame.format == VIDEO_FORMAT_I010 ||
            frame.format == VIDEO_FORMAT_NV12 || frame.format == VIDEO_FORMAT_P010);
        const auto height = halfHeight ? (frame.height + 1) / 2 : frame.height;
        decodedBytes += (frame.data[last] - frame.data[0]) + uint64_t(height) * frame.linesize[last];
    }
    std::printf("VIDEO MEMORY: %.1f MiB per decoded copy; five private copies %.1f MiB; shared %.1f MiB; saving %.1f MiB.\n",
        decodedBytes/1048576.0, decodedBytes*5/1048576.0, decodedBytes/1048576.0, decodedBytes*4/1048576.0);
    const auto originalTimestamp = first->cache.video_frames.array[0].timestamp;
    mp_cache_play(&first->cache, true); mp_cache_play(&second->cache, true);
    check(waitFor([&] { return first->video >= 3 && second->video >= 3; }), "Players deliver video independently");
    if (first->cache.has_audio)
        check(waitFor([&] { return first->audio >= 3 && second->audio >= 3; }), "Shared audio is delivered by independent players");
    mp_cache_play_pause(&first->cache, true);
    const int before = second->video.load();
    check(waitFor([&] { return second->video > before + 3; }), "Pausing one player does not pause another");
    mp_cache_seek(&second->cache, 0);
    check(waitFor([&] { return second->video > before + 6; }), "Seek resumes cached playback");
    check(second->cache.video_frames.array[0].timestamp == originalTimestamp, "Playback does not mutate shared timestamps");
    first.reset();
    const int after = second->video.load();
    check(waitFor([&] { return second->video > after + 3; }), "Destroying original owner preserves remaining playback");
    mp_cache_stop(&second->cache);
    second.reset(); third.reset(); fourth.reset(); fifth.reset();
    {
        Player normal(argv[1], false); normal.awaitReady();
        Player shared(argv[1]); shared.awaitReady();
        check(normal.cache.video_frames.array != shared.cache.video_frames.array, "Opt-out player keeps private cache");
        Player differentSpeed(argv[1], true, 80); differentSpeed.awaitReady();
        check(differentSpeed.cache.video_frames.array != shared.cache.video_frames.array, "Different decode settings do not share data");
    }
    { Player reopened(argv[1]); reopened.awaitReady();
      check(reopened.cache.video_frames.num > 0, "File reopens after all owners are released"); }
    check(bnum_allocs() == allocationsBefore, "Last owner releases every OBS cache allocation");
    const auto temporary = std::filesystem::temp_directory_path() / ("PulseWeaver-stinger-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".webm");
    std::filesystem::copy_file(argv[1], temporary);
    const auto tempPath = temporary.u8string();
    {
        Player oldFile(tempPath.c_str()); oldFile.awaitReady();
        { std::ofstream changed(temporary, std::ios::binary | std::ios::app); changed.put('\0'); }
        Player changedFile(tempPath.c_str()); changedFile.awaitReady();
        check(oldFile.cache.video_frames.array != changedFile.cache.video_frames.array, "Changed file identity gets a new decoded cache");
        const auto writeTime = std::filesystem::last_write_time(temporary);
        { std::fstream changed(temporary, std::ios::binary | std::ios::in | std::ios::out); changed.seekp(-1, std::ios::end); changed.put('\1'); }
        std::filesystem::last_write_time(temporary, writeTime);
        Player changedContent(tempPath.c_str()); changedContent.awaitReady();
        check(changedFile.cache.video_frames.array != changedContent.cache.video_frames.array, "Same size and timestamp with different content gets a new cache");
    }
    std::filesystem::remove(temporary);
    check(bnum_allocs() == allocationsBefore, "Changed-file caches also release all allocations");
    std::printf("PASS: %d real media sharing and playback checks.\n", checks);
}
