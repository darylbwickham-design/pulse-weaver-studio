#pragma once

#include <obs.hpp>

#include <QHash>

#include <vector>

/* Stage video exclusions require a private scene copy so hiding an item for one
 * destination cannot hide it everywhere.  The copy is intentionally isolated,
 * but its item transforms still need to follow Camera edits while it is live. */
class PulseOutputSceneTransformSync {
	struct ItemIdentity {
		obs_source_t *source = nullptr;
		int64_t id = 0;
	};

	OBSSource sourceScene;
	OBSSource outputScene;
	QHash<int64_t, int64_t> outputItemIds;
	OBSSignal transformSignal;

	static std::vector<ItemIdentity> SceneItems(obs_scene_t *scene)
	{
		std::vector<ItemIdentity> items;
		if (scene)
			obs_scene_enum_items(scene, [](obs_scene_t *, obs_sceneitem_t *item, void *opaque) {
				auto *items = static_cast<std::vector<ItemIdentity> *>(opaque);
				items->push_back({obs_sceneitem_get_source(item), obs_sceneitem_get_id(item)});
				return true;
			}, &items);
		return items;
	}

	static void SourceItemTransformed(void *opaque, calldata_t *params)
	{
		auto *sync = static_cast<PulseOutputSceneTransformSync *>(opaque);
		auto *sourceItem = static_cast<obs_sceneitem_t *>(calldata_ptr(params, "item"));
		if (!sync || !sourceItem)
			return;

		const auto found = sync->outputItemIds.constFind(obs_sceneitem_get_id(sourceItem));
		if (found == sync->outputItemIds.constEnd())
			return;
		obs_scene_t *scene = obs_scene_from_source(sync->outputScene);
		obs_sceneitem_t *outputItem = scene ? obs_scene_find_sceneitem_by_id(scene, found.value()) : nullptr;
		if (!outputItem)
			return;

		obs_transform_info transform = {};
		obs_sceneitem_crop crop = {};
		obs_sceneitem_get_info2(sourceItem, &transform);
		obs_sceneitem_get_crop(sourceItem, &crop);
		obs_sceneitem_set_info2(outputItem, &transform);
		obs_sceneitem_set_crop(outputItem, &crop);
	}

public:
	PulseOutputSceneTransformSync(obs_scene_t *source, obs_scene_t *output)
		: sourceScene(source ? obs_scene_get_source(source) : nullptr),
		  outputScene(output ? obs_scene_get_source(output) : nullptr)
	{
		const auto sourceItems = SceneItems(source);
		const auto outputItems = SceneItems(output);
		std::vector<bool> used(outputItems.size(), false);
		for (const ItemIdentity &sourceItem : sourceItems) {
			for (size_t index = 0; index < outputItems.size(); ++index) {
				/* A referenced duplicate uses the same child sources. Matching the
				 * occurrence as well as the pointer handles a source added twice. */
				if (!used[index] && outputItems[index].source == sourceItem.source) {
					outputItemIds.insert(sourceItem.id, outputItems[index].id);
					used[index] = true;
					break;
				}
			}
		}
		if (sourceScene && outputScene)
			transformSignal.Connect(obs_source_get_signal_handler(sourceScene), "item_transform",
						&SourceItemTransformed, this);
	}

	PulseOutputSceneTransformSync(const PulseOutputSceneTransformSync &) = delete;
	PulseOutputSceneTransformSync &operator=(const PulseOutputSceneTransformSync &) = delete;
};
