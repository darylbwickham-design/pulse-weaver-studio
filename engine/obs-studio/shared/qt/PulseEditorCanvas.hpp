#pragma once

#include <obs.hpp>
#include <cstring>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace PulseEditor {
inline bool IsHud(obs_source_t *source)
{
	OBSCanvasAutoRelease canvas = source ? obs_source_get_canvas(source) : nullptr;
	return canvas && std::strcmp(obs_canvas_get_name(canvas), "Pulse Weaver HUD") == 0;
}

inline bool IsPortrait(obs_source_t *source)
{
	obs_canvas_t *canvas = source ? obs_source_get_canvas(source) : nullptr;
	const bool result = canvas && std::strcmp(obs_canvas_get_name(canvas), "Pulse Weaver Vertical") == 0;
	obs_canvas_release(canvas);
	return result;
}

/* Scene ownership determines output routing, but a nested helper may be used
 * exclusively by the other canvas, or shared by both. The Camera tabs follow
 * those actual scene/group references without moving or duplicating sources. */
inline bool IsAvailableInEditor(obs_source_t *source, bool portrait)
{
	if (!source || obs_source_removed(source) || !obs_group_or_scene_from_source(source) || IsHud(source))
		return false;
	struct Graph {
		std::vector<OBSSource> sources;
		std::unordered_map<obs_source_t *, size_t> index;
		std::vector<std::vector<size_t>> children;
		std::vector<size_t> incoming;
		std::vector<unsigned> usage;
		std::vector<bool> main;
		std::vector<bool> vertical;
	} graph;
	obs_enum_all_sources([](void *opaque, obs_source_t *candidate) {
		auto &graph = *static_cast<Graph *>(opaque);
		if (!obs_source_removed(candidate) && obs_group_or_scene_from_source(candidate)) {
			graph.index.emplace(candidate, graph.sources.size());
			graph.sources.emplace_back(candidate);
		}
		return true;
	}, &graph);
	const auto target = graph.index.find(source);
	if (target == graph.index.end())
		return false;
	graph.children.resize(graph.sources.size());
	graph.incoming.resize(graph.sources.size());
	graph.usage.resize(graph.sources.size());
	graph.main.resize(graph.sources.size());
	graph.vertical.resize(graph.sources.size());
	for (size_t parent = 0; parent < graph.sources.size(); ++parent) {
		obs_source_t *candidate = graph.sources[parent];
		OBSCanvasAutoRelease canvas = obs_source_get_canvas(candidate);
		graph.main[parent] = canvas && (obs_canvas_get_flags(canvas) & MAIN);
		graph.vertical[parent] = obs_scene_from_source(candidate) && IsPortrait(candidate);
		struct Edges { Graph &graph; size_t parent; } edges{graph, parent};
		obs_scene_enum_items(obs_group_or_scene_from_source(candidate),
			[](obs_scene_t *, obs_sceneitem_t *item, void *opaque) {
				auto &edges = *static_cast<Edges *>(opaque);
				const auto child = edges.graph.index.find(obs_sceneitem_get_source(item));
				if (child != edges.graph.index.end()) {
					edges.graph.children[edges.parent].push_back(child->second);
					++edges.graph.incoming[child->second];
				}
				return true;
			}, &edges);
	}
	constexpr unsigned landscapeUsage = 1, portraitUsage = 2;
	for (size_t i = 0; i < graph.sources.size(); ++i)
		graph.usage[i] = graph.vertical[i] ? portraitUsage :
			(graph.main[i] && !graph.incoming[i] ? landscapeUsage : 0);
	/* A fixed point also terminates for malformed imported reference cycles.
 * Visibility is deliberately ignored: a saved Look can enable a hidden item. */
	bool changed;
	do {
		changed = false;
		for (size_t parent = 0; parent < graph.sources.size(); ++parent)
			for (size_t child : graph.children[parent]) {
				if (graph.vertical[child])
					continue;
				const unsigned next = graph.usage[child] | graph.usage[parent];
				changed |= next != graph.usage[child];
				graph.usage[child] = next;
			}
	} while (changed);
	unsigned usage = graph.usage[target->second];
	if (!usage && graph.main[target->second])
		usage = landscapeUsage; // Unreferenced/imported helpers remain reachable.
	return (usage & (portrait ? portraitUsage : landscapeUsage)) != 0;
}

inline bool VideoInfo(bool portrait, obs_video_info *info, obs_source_t *selected = nullptr)
{
	if (!info)
		return false;
	bool found = false;
	if (portrait) {
		obs_canvas_t *canvas = obs_get_canvas_by_name("Pulse Weaver Vertical");
		found = canvas && obs_canvas_get_video_info(canvas, info);
		obs_canvas_release(canvas);
	}
	if (!found)
		found = obs_get_video_info(info);
	if (found && selected && !obs_source_removed(selected) && obs_scene_from_source(selected)) {
		OBSDataAutoRelease settings = obs_source_get_settings(selected);
		if (obs_data_get_bool(settings, "custom_size")) {
			const uint32_t width = obs_source_get_width(selected), height = obs_source_get_height(selected);
			if (width && height) {
				info->base_width = width;
				info->base_height = height;
			}
		}
	}
	return found;
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
