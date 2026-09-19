#pragma once

#include <obs.hpp>
#include <cstring>
#include <mutex>

namespace PulseEditor {
inline bool IsPortrait(obs_source_t *source)
{
	obs_canvas_t *canvas = source ? obs_source_get_canvas(source) : nullptr;
	const bool result = canvas && std::strcmp(obs_canvas_get_name(canvas), "Pulse Weaver Vertical") == 0;
	obs_canvas_release(canvas);
	return result;
}

inline bool VideoInfo(bool portrait, obs_video_info *info)
{
	if (portrait) {
		obs_canvas_t *canvas = obs_get_canvas_by_name("Pulse Weaver Vertical");
		const bool found = canvas && obs_canvas_get_video_info(canvas, info);
		obs_canvas_release(canvas);
		if (found)
			return true;
	}
	return obs_get_video_info(info);
}

/* An editor selection owns a preview reference, never a programme channel.
 * The renderer takes a strong scene reference under the same lock used when
 * switching scenes, so scene deletion cannot race a draw callback. */
class Selection {
	mutable std::mutex mutex;
	OBSScene scene;
	bool showing = false;

public:
	~Selection() { Clear(); }
	OBSScene Get() const
	{
		std::lock_guard<std::mutex> lock(mutex);
		return scene;
	}
	void Select(obs_scene_t *next)
	{
		std::lock_guard<std::mutex> lock(mutex);
		if (scene == next)
			return;
		if (showing && next)
			obs_source_inc_showing(obs_scene_get_source(next));
		if (showing && scene)
			obs_source_dec_showing(obs_scene_get_source(scene));
		scene = next;
	}
	void SetShowing(bool enabled)
	{
		std::lock_guard<std::mutex> lock(mutex);
		if (showing == enabled)
			return;
		if (scene) {
			if (enabled)
				obs_source_inc_showing(obs_scene_get_source(scene));
			else
				obs_source_dec_showing(obs_scene_get_source(scene));
		}
		showing = enabled;
	}
	void Clear()
	{
		SetShowing(false);
		Select(nullptr);
	}
};
} // namespace PulseEditor
