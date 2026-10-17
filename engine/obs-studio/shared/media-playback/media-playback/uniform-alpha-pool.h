#pragma once

#include <obs.h>
#include <media-io/video-frame.h>
#include <util/darray.h>
#include <stdint.h>
#include <string.h>

/* Preload-only storage. Playback still reads ordinary immutable frame planes.
 * Each frame owns its YUV data[0]; this pool owns the separately stored alpha. */
struct mp_uniform_alpha_plane {
	uint32_t width, height;
	uint8_t value;
	uint8_t *data;
};

struct mp_uniform_alpha_stats {
	uint64_t frames;
	uint64_t logical_bytes;
	uint64_t unique_bytes;
	size_t planes;
};

struct mp_uniform_alpha_pool {
	DARRAY(struct mp_uniform_alpha_plane) planes;
	uint64_t frames;
	uint64_t logical_bytes;
	uint64_t unique_bytes;
};

static inline struct mp_uniform_alpha_stats mp_uniform_alpha_pool_stats(const struct mp_uniform_alpha_pool *pool)
{
	struct mp_uniform_alpha_stats stats = {pool->frames, pool->logical_bytes, pool->unique_bytes, pool->planes.num};
	return stats;
}

static inline void mp_uniform_alpha_pool_free(struct mp_uniform_alpha_pool *pool)
{
	for (size_t i = 0; i < pool->planes.num; ++i)
		bfree(pool->planes.array[i].data);
	da_free(pool->planes);
	memset(pool, 0, sizeof(*pool));
}

static inline bool mp_uniform_alpha_source_layout_valid(const struct obs_source_frame *frame)
{
	if (frame->format != VIDEO_FORMAT_I40A || !frame->width || !frame->height || frame->width == UINT32_MAX ||
	    frame->height == UINT32_MAX)
		return false;
	const uint32_t half_width = frame->width / 2 + frame->width % 2;
	const uint32_t half_height = frame->height / 2 + frame->height % 2;
	const uint32_t widths[4] = {frame->width, half_width, half_width, frame->width};
	const uint32_t heights[4] = {frame->height, half_height, half_height, frame->height};
	const int alignment = base_get_alignment();
	if (alignment <= 0)
		return false;
	size_t allocation_size = 0;
	for (size_t plane = 0; plane < 4; ++plane) {
		if (!frame->data[plane] || !widths[plane] || !heights[plane] || frame->linesize[plane] < widths[plane] ||
		    heights[plane] > SIZE_MAX / widths[plane])
			return false;
		/* Validate both the source's last visible row and the frame helper's
		 * aligned contiguous allocation before touching any source pixels. */
		if ((size_t)(heights[plane] - 1) > (SIZE_MAX - widths[plane]) / frame->linesize[plane])
			return false;
		const size_t bytes = (size_t)widths[plane] * heights[plane];
		const size_t padding = ((size_t)alignment - bytes % (size_t)alignment) % (size_t)alignment;
		if (bytes > SIZE_MAX - padding || allocation_size > SIZE_MAX - (bytes + padding))
			return false;
		allocation_size += bytes + padding;
	}
	return true;
}

/* Returns false without changing dst or pool when the original full-frame
 * allocation should be used. Only visible bytes determine a uniform plane;
 * decoder row padding is neither compared nor interpreted as image content. */
static inline bool mp_uniform_alpha_copy_frame(struct mp_uniform_alpha_pool *pool, struct obs_source_frame *dst,
					     const struct obs_source_frame *src)
{
	if (!mp_uniform_alpha_source_layout_valid(src))
		return false;
	const uint8_t *alpha = src->data[3];
	const uint8_t value = alpha[0];
	for (uint32_t x = 1; x < src->width; ++x)
		if (alpha[x] != value)
			return false;
	for (uint32_t y = 1; y < src->height; ++y)
		if (memcmp(alpha, alpha + (size_t)y * src->linesize[3], src->width) != 0)
			return false;
	const size_t bytes = (size_t)src->width * src->height;
	if (pool->frames == UINT64_MAX || pool->logical_bytes > UINT64_MAX - bytes)
		return false;
	uint8_t *shared_alpha = NULL;
	for (size_t i = 0; i < pool->planes.num; ++i) {
		const struct mp_uniform_alpha_plane *plane = &pool->planes.array[i];
		if (plane->width == src->width && plane->height == src->height && plane->value == value) {
			shared_alpha = plane->data;
			break;
		}
	}
	if (!shared_alpha) {
		struct mp_uniform_alpha_plane plane = {src->width, src->height, value, (uint8_t *)bmalloc(bytes)};
		memset(plane.data, value, bytes);
		da_push_back(pool->planes, &plane);
		pool->unique_bytes += bytes;
		shared_alpha = plane.data;
	}
	/* I40A's first three planes are exactly I420. Use the existing OBS copy
	 * contract to retain all frame metadata and pixels, without copying into
	 * the shared alpha plane. Generic frame-free still owns only data[0]. */
	struct obs_source_frame yuv = *src;
	yuv.format = VIDEO_FORMAT_I420;
	obs_source_frame_init(dst, VIDEO_FORMAT_I420, src->width, src->height);
	obs_source_frame_copy(dst, &yuv);
	dst->format = VIDEO_FORMAT_I40A;
	dst->data[3] = shared_alpha;
	dst->linesize[3] = src->width;
	++pool->frames;
	pool->logical_bytes += bytes;
	return true;
}
