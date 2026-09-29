#pragma once

#include <obs.hpp>

#include <QHash>
#include <QSet>

#include <vector>

/* Stage video exclusions require a private scene copy so hiding an item for one
 * destination cannot hide it everywhere.  The copy is intentionally isolated,
 * but its item transforms still need to follow Camera edits while it is live. */
class PulseOutputSceneTransformSync {
	struct ItemIdentity {
		OBSSource source;
		int64_t id = 0;
	};

	OBSSource sourceScene;
	OBSSource outputScene;
	QHash<int64_t, int64_t> outputItemIds;
	QSet<QString> excludedSources;
	QSet<QString> excludedSourceUuids;
	OBSSignal transformSignal;
	OBSSignal reorderSignal;
	OBSSignal removeSignal;
	bool tickRegistered = false;

	static OBSSceneItem SceneItemById(obs_scene_t *scene, int64_t id)
	{
		struct Search { int64_t id; OBSSceneItem item; } search{id, {}};
		if (scene)
			obs_scene_enum_items(scene, [](obs_scene_t *, obs_sceneitem_t *item, void *opaque) {
				auto &search = *static_cast<Search *>(opaque);
				if (obs_sceneitem_get_id(item) == search.id)
					search.item = item;
				return !search.item;
			}, &search);
		return search.item;
	}

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

	void SynchronizeItem(obs_sceneitem_t *sourceItem)
	{
		if (!sourceItem)
			return;

		const auto found = outputItemIds.constFind(obs_sceneitem_get_id(sourceItem));
		if (found == outputItemIds.constEnd())
			return;
		obs_scene_t *scene = obs_scene_from_source(outputScene);
		OBSSceneItem outputItem = SceneItemById(scene, found.value());
		if (!outputItem)
			return;

		obs_transform_info sourceTransform = {};
		obs_transform_info outputTransform = {};
		obs_sceneitem_crop sourceCrop = {};
		obs_sceneitem_crop outputCrop = {};
		obs_sceneitem_get_info2(sourceItem, &sourceTransform);
		obs_sceneitem_get_info2(outputItem, &outputTransform);
		obs_sceneitem_get_crop(sourceItem, &sourceCrop);
		obs_sceneitem_get_crop(outputItem, &outputCrop);

		const bool transformChanged = sourceTransform.pos.x != outputTransform.pos.x ||
			sourceTransform.pos.y != outputTransform.pos.y || sourceTransform.rot != outputTransform.rot ||
			sourceTransform.scale.x != outputTransform.scale.x ||
			sourceTransform.scale.y != outputTransform.scale.y ||
			sourceTransform.alignment != outputTransform.alignment ||
			sourceTransform.bounds_type != outputTransform.bounds_type ||
			sourceTransform.bounds_alignment != outputTransform.bounds_alignment ||
			sourceTransform.bounds.x != outputTransform.bounds.x ||
			sourceTransform.bounds.y != outputTransform.bounds.y ||
			sourceTransform.crop_to_bounds != outputTransform.crop_to_bounds;
		const bool cropChanged = sourceCrop.left != outputCrop.left || sourceCrop.top != outputCrop.top ||
			sourceCrop.right != outputCrop.right || sourceCrop.bottom != outputCrop.bottom;
		if (transformChanged)
			obs_sceneitem_set_info2(outputItem, &sourceTransform);
		if (cropChanged)
			obs_sceneitem_set_crop(outputItem, &sourceCrop);
		obs_source_t *itemSource = obs_sceneitem_get_source(sourceItem);
		const QString sourceName = itemSource ? QString::fromUtf8(obs_source_get_name(itemSource)) : QString();
		const QString sourceUuid = itemSource ? QString::fromUtf8(obs_source_get_uuid(itemSource)) : QString();
		const bool visible = obs_sceneitem_visible(sourceItem) && !excludedSources.contains(sourceName) &&
			!excludedSourceUuids.contains(sourceUuid);
		if (obs_sceneitem_visible(outputItem) != visible)
			obs_sceneitem_set_visible(outputItem, visible);
	}

	void SynchronizeAll()
	{
		obs_scene_t *scene = obs_scene_from_source(sourceScene);
		if (!scene)
			return;
		obs_scene_enum_items(scene, [](obs_scene_t *, obs_sceneitem_t *item, void *opaque) {
			static_cast<PulseOutputSceneTransformSync *>(opaque)->SynchronizeItem(item);
			return true;
		}, this);
	}

	void SynchronizeOrder()
	{
		obs_scene_t *source = obs_scene_from_source(sourceScene);
		obs_scene_t *output = obs_scene_from_source(outputScene);
		if (!source || !output)
			return;
		for (const ItemIdentity &identity : SceneItems(source)) {
			const auto mapped = outputItemIds.constFind(identity.id);
			if (mapped == outputItemIds.constEnd())
				continue;
			OBSSceneItem item = SceneItemById(output, mapped.value());
			OBSSceneItem original = SceneItemById(source, identity.id);
			if (item && original &&
			    obs_sceneitem_get_order_position(item) != obs_sceneitem_get_order_position(original))
				obs_sceneitem_set_order_position(item, obs_sceneitem_get_order_position(original));
		}
	}

	static void SourceReordered(void *opaque, calldata_t *)
	{
		auto *sync = static_cast<PulseOutputSceneTransformSync *>(opaque);
		if (sync)
			sync->SynchronizeOrder();
	}

	static void SourceItemTransformed(void *opaque, calldata_t *params)
	{
		auto *sync = static_cast<PulseOutputSceneTransformSync *>(opaque);
		if (sync)
			sync->SynchronizeItem(static_cast<obs_sceneitem_t *>(calldata_ptr(params, "item")));
	}

	static void SourceItemRemoved(void *opaque, calldata_t *params)
	{
		auto *sync = static_cast<PulseOutputSceneTransformSync *>(opaque);
		auto *sourceItem = static_cast<obs_sceneitem_t *>(calldata_ptr(params, "item"));
		if (!sync || !sourceItem)
			return;
		const auto found = sync->outputItemIds.constFind(obs_sceneitem_get_id(sourceItem));
		if (found == sync->outputItemIds.constEnd())
			return;
		obs_scene_t *output = obs_scene_from_source(sync->outputScene);
		OBSSceneItem item = SceneItemById(output, found.value());
		if (item)
			obs_sceneitem_remove(item);
	}

	static void VideoTick(void *opaque, float)
	{
		auto *sync = static_cast<PulseOutputSceneTransformSync *>(opaque);
		if (sync)
			sync->SynchronizeAll();
	}

public:
	PulseOutputSceneTransformSync(obs_scene_t *source, obs_scene_t *output, QSet<QString> excluded = {})
		: sourceScene(source ? obs_scene_get_source(source) : nullptr),
		  outputScene(output ? obs_scene_get_source(output) : nullptr), excludedSources(std::move(excluded))
	{
		const auto sourceItems = SceneItems(source);
		const auto outputItems = SceneItems(output);
		std::vector<bool> used(outputItems.size(), false);
		for (const ItemIdentity &sourceItem : sourceItems) {
			if (excludedSources.contains(QString::fromUtf8(obs_source_get_name(sourceItem.source))))
				excludedSourceUuids.insert(QString::fromUtf8(obs_source_get_uuid(sourceItem.source)));
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
		if (sourceScene && outputScene)
			reorderSignal.Connect(obs_source_get_signal_handler(sourceScene), "reorder", &SourceReordered, this);
		if (sourceScene && outputScene)
			removeSignal.Connect(obs_source_get_signal_handler(sourceScene), "item_remove", &SourceItemRemoved, this);
		/* Some animation filters update scene-item transforms during their video
		 * tick without producing an item_transform signal on every frame.  Keep a
		 * frame-paced reconciliation as a fallback so the live destination copy
		 * follows Move-style animation as well as direct Camera edits. */
		if (sourceScene && outputScene) {
			obs_add_tick_callback(&VideoTick, this);
			tickRegistered = true;
		}
	}
	~PulseOutputSceneTransformSync()
	{
		if (tickRegistered)
			obs_remove_tick_callback(&VideoTick, this);
	}

	void Synchronize() { SynchronizeAll(); }

	PulseOutputSceneTransformSync(const PulseOutputSceneTransformSync &) = delete;
	PulseOutputSceneTransformSync &operator=(const PulseOutputSceneTransformSync &) = delete;
};
