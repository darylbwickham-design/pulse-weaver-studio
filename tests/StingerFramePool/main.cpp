#include "../../engine/obs-studio/shared/media-playback/media-playback/uniform-alpha-pool.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

static unsigned checks;
static void check(bool ok, const char *message)
{
	if (!ok) {
		std::fprintf(stderr, "FAIL: %s\n", message);
		std::exit(1);
	}
	++checks;
}

struct Source {
	obs_source_frame frame{};
	std::vector<uint8_t> storage[4];
	Source(uint32_t width, uint32_t height, uint8_t alpha, uint32_t padding = 0)
	{
		frame.format = VIDEO_FORMAT_I40A;
		frame.width = width;
		frame.height = height;
		frame.timestamp = 1234567890123ULL;
		frame.full_range = false;
		frame.flip = true;
		frame.flags = OBS_SOURCE_FRAME_LINEAR_ALPHA;
		frame.trc = VIDEO_TRC_PQ;
		frame.max_luminance = 1200;
		for (size_t i = 0; i < 16; ++i)
			frame.color_matrix[i] = float(i) * 0.0625f;
		for (size_t i = 0; i < 3; ++i) {
			frame.color_range_min[i] = float(i) * 0.125f;
			frame.color_range_max[i] = 1.0f - float(i) * 0.125f;
		}
		const uint32_t widths[4] = {width, width / 2 + width % 2, width / 2 + width % 2, width};
		const uint32_t heights[4] = {height, height / 2 + height % 2, height / 2 + height % 2, height};
		for (size_t plane = 0; plane < 4; ++plane) {
			frame.linesize[plane] = widths[plane] + padding;
			storage[plane].resize(size_t(frame.linesize[plane]) * heights[plane]);
			frame.data[plane] = storage[plane].data();
			for (uint32_t y = 0; y < heights[plane]; ++y) {
				uint8_t *row = frame.data[plane] + size_t(y) * frame.linesize[plane];
				for (uint32_t x = 0; x < widths[plane]; ++x)
					row[x] = plane == 3 ? alpha : uint8_t(x * 17 + y * 13 + plane * 41);
				/* Padding changes on every row; it must never defeat pooling. */
				for (uint32_t x = widths[plane]; x < frame.linesize[plane]; ++x)
					row[x] = uint8_t(y + x + plane + 97);
			}
		}
	}
};

static void sameFrame(const obs_source_frame &actual, const obs_source_frame &expected)
{
	check(actual.format == expected.format && actual.width == expected.width && actual.height == expected.height,
	      "format and dimensions unchanged");
	check(actual.timestamp == expected.timestamp && actual.flip == expected.flip && actual.flags == expected.flags &&
		      actual.trc == expected.trc && actual.full_range == expected.full_range &&
		      actual.max_luminance == expected.max_luminance,
	      "timing, alpha, transfer function and luminance metadata unchanged");
	check(!std::memcmp(actual.color_matrix, expected.color_matrix, sizeof(actual.color_matrix)), "matrix unchanged");
	if (!expected.full_range) {
		check(!std::memcmp(actual.color_range_min, expected.color_range_min, sizeof(actual.color_range_min)),
		      "range minimum unchanged");
		check(!std::memcmp(actual.color_range_max, expected.color_range_max, sizeof(actual.color_range_max)),
		      "range maximum unchanged");
	}
	const uint32_t widths[4] = {expected.width, expected.width / 2 + expected.width % 2,
				    expected.width / 2 + expected.width % 2, expected.width};
	const uint32_t heights[4] = {expected.height, expected.height / 2 + expected.height % 2,
				     expected.height / 2 + expected.height % 2, expected.height};
	for (size_t plane = 0; plane < 4; ++plane) {
		for (uint32_t y = 0; y < heights[plane]; ++y)
			check(!std::memcmp(actual.data[plane] + size_t(y) * actual.linesize[plane],
					   expected.data[plane] + size_t(y) * expected.linesize[plane], widths[plane]),
			      "every visible Y/U/V/A byte unchanged");
	}
}

static void checkPlaybackCopy(const obs_source_frame &pooled, const obs_source_frame &expected)
{
	obs_source_frame playback{};
	obs_source_frame_init(&playback, pooled.format, pooled.width, pooled.height);
	obs_source_frame_copy(&playback, &pooled);
	sameFrame(playback, expected);
	obs_source_frame_free(&playback);
}

static void expectFallback(mp_uniform_alpha_pool &pool, const obs_source_frame &source)
{
	const auto before = mp_uniform_alpha_pool_stats(&pool);
	obs_source_frame destination{};
	destination.timestamp = 77;
	check(!mp_uniform_alpha_copy_frame(&pool, &destination, &source), "unsupported frame uses original allocation");
	check(destination.timestamp == 77 && !destination.data[0], "fallback leaves destination untouched");
	const auto after = mp_uniform_alpha_pool_stats(&pool);
	check(before.frames == after.frames && before.logical_bytes == after.logical_bytes &&
		      before.unique_bytes == after.unique_bytes && before.planes == after.planes,
	      "fallback leaves pool untouched");
}

int main()
{
	const long baseline = bnum_allocs();
	mp_uniform_alpha_pool pool{};
	std::vector<obs_source_frame> frames;
	for (uint8_t alpha : {uint8_t(0), uint8_t(255), uint8_t(127)}) {
		Source source(5, 3, alpha, 7);
		obs_source_frame first{}, second{};
		check(mp_uniform_alpha_copy_frame(&pool, &first, &source.frame), "odd padded I40A frame pools");
		sameFrame(first, source.frame);
		checkPlaybackCopy(first, source.frame);
		source.frame.timestamp += 987654;
		source.frame.full_range = true;
		check(mp_uniform_alpha_copy_frame(&pool, &second, &source.frame), "second matching alpha pools");
		sameFrame(second, source.frame);
		checkPlaybackCopy(second, source.frame);
		check(first.data[3] == second.data[3], "equal alpha and dimensions share one plane");
		check(first.data[0] != second.data[0], "frames retain independent generic-free-compatible YUV");
		check(uintptr_t(first.data[0]) % base_get_alignment() == 0 &&
			      uintptr_t(first.data[3]) % base_get_alignment() == 0,
		      "YUV and shared alpha allocations keep OBS alignment");
		frames.push_back(first);
		frames.push_back(second);
	}
	check(frames[0].data[3] != frames[2].data[3] && frames[2].data[3] != frames[4].data[3],
	      "different uniform alpha values never alias");
	Source otherSize(3, 5, 0, 1);
	obs_source_frame other{};
	check(mp_uniform_alpha_copy_frame(&pool, &other, &otherSize.frame), "different dimensions pool separately");
	check(other.data[3] != frames[0].data[3], "equal byte count with different dimensions has separate ownership");
	sameFrame(other, otherSize.frame);
	frames.push_back(other);
	Source single(1, 1, 64);
	obs_source_frame one{};
	check(mp_uniform_alpha_copy_frame(&pool, &one, &single.frame), "single pixel and no padding supported");
	sameFrame(one, single.frame);
	frames.push_back(one);
	const auto stats = mp_uniform_alpha_pool_stats(&pool);
	check(stats.frames == 8 && stats.planes == 5 && stats.logical_bytes == 106 && stats.unique_bytes == 61,
	      "statistics count unique visible alpha buffers rather than unrelated pointer offsets");

	Source nonuniform(5, 3, 255, 9);
	nonuniform.frame.data[3][size_t(2) * nonuniform.frame.linesize[3] + 4] = 254;
	expectFallback(pool, nonuniform.frame);
	obs_source_frame original{};
	obs_source_frame_init(&original, nonuniform.frame.format, nonuniform.frame.width, nonuniform.frame.height);
	obs_source_frame_copy(&original, &nonuniform.frame);
	sameFrame(original, nonuniform.frame);
	obs_source_frame_free(&original);
	Source firstRowDifference(5, 3, 0);
	firstRowDifference.frame.data[3][3] = 1;
	expectFallback(pool, firstRowDifference.frame);
	Source unsupported(5, 3, 255);
	unsupported.frame.format = VIDEO_FORMAT_I420;
	expectFallback(pool, unsupported.frame);
	for (int invalid = 0; invalid < 6; ++invalid) {
		obs_source_frame input = single.frame;
		if (invalid == 0) input.data[3] = nullptr;
		if (invalid == 1) input.linesize[3] = 0;
		if (invalid == 2) input.width = 0;
		if (invalid == 3) input.height = 0;
		if (invalid == 4) input.width = input.height = UINT32_MAX;
		if (invalid == 5) {
			input.width = input.height = UINT32_MAX - 1;
			for (auto &line : input.linesize) line = UINT32_MAX;
		}
		expectFallback(pool, input);
	}
	/* Publication transfers ownership by value; readers retain only frame
	 * views. This mirrors mp_shared_cache without adding playback work. */
	mp_uniform_alpha_pool published = pool;
	std::memset(&pool, 0, sizeof(pool));
	mp_uniform_alpha_pool_free(&pool);
	checkPlaybackCopy(frames[0], Source(5, 3, 0, 7).frame);
	for (auto &frame : frames)
		obs_source_frame_free(&frame);
	mp_uniform_alpha_pool_free(&published);
	mp_uniform_alpha_pool_free(&published);
	check(bnum_allocs() == baseline, "private/partial pool and published owner release every OBS allocation once");
	std::printf("PASS: %u lossless uniform-alpha pool checks; unchanged raw playback and allocation cleanup.\n", checks);
}
