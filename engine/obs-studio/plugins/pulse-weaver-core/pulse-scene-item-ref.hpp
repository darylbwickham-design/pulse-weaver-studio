#pragma once

#include <obs.hpp>
#include <cstring>

namespace PulseRuntimeSafety {

inline OBSSceneItem findSceneItemById(obs_scene_t *scene, int64_t id)
{
    struct Search { int64_t id; OBSSceneItem result; } search{id, {}};
    obs_scene_enum_items(scene, [](obs_scene_t *, obs_sceneitem_t *item, void *data) {
        auto &search = *static_cast<Search *>(data);
        if (obs_sceneitem_get_id(item) == search.id)
            search.result = item;
        return !search.result;
    }, &search);
    return search.result;
}

// Take the reference inside enumeration, while libobs holds the scene lock
// and a temporary reference to the item. Raw lookup results are borrowed.
inline OBSSceneItem findSceneItem(obs_scene_t *scene, const char *name)
{
    struct Search { const char *name; OBSSceneItem result; } search{name, {}};
    obs_scene_enum_items(scene, [](obs_scene_t *, obs_sceneitem_t *item, void *data) {
        auto &search = *static_cast<Search *>(data);
        if (std::strcmp(obs_source_get_name(obs_sceneitem_get_source(item)), search.name) == 0)
            search.result = item;
        else if (obs_sceneitem_is_group(item))
            search.result = findSceneItem(obs_sceneitem_group_get_scene(item), search.name);
        return !search.result;
    }, &search);
    return search.result;
}

} // namespace PulseRuntimeSafety
