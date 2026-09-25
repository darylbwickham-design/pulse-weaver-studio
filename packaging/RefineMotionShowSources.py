"""Carry the regular show's shared sources into the isolated Motion stages.

Inputs are copies in this task's backup directory. This script only writes the
two explicit output paths; it never edits the regular Pulse Weaver profile.
"""

import argparse
import copy
import json
from pathlib import Path


LANDSCAPE_REFERENCES = {
    "PW Starting": ["stream starting with 3d cam"],
    "PW Intermission": ["stand by with 3d cam", "ending"],
    "PW Hangout": ["chatting wiht face cam", "chatting wiht 3d cam"],
    "PW Gameplay": ["fortnite wiht 3d cam", "Palworld"],
    "PW Celebration": ["shoutout", "joy shoutout"],
}
PORTRAIT_REFERENCES = {
    "PW Vert Starting": ["STarting"],
    "PW Vert Intermission": ["VERT BRB"],
    "PW Vert Hangout": ["Vertical Scene"],
    "PW Vert Gameplay": ["Vertical Scene"],
    "PW Vert Celebration": ["Vertical Scene"],
}
STAGE_PAIRS = {
    main: main.replace("PW ", "PW Vert ", 1) for main in LANDSCAPE_REFERENCES
}
LANDSCAPE_OMIT = {"chat 3", "FullScreen Printer"}
STARTING_OMIT = {"Game Capture"}
CELEBRATION_SHARED = {
    "Sticker Party", "spotifysound", "spotify", "lumia", "firstmessage",
    "duel", "logo", "Stick o vision", "hfx", "follower", "Challenges",
    "screw you", "TTS TEST", "solocast", "whodatpalLB", "whodatpal", "caption",
}
PORTRAIT_SHARED = {"Mic/Aux", "Desktop Audio", "spotifysound", "lumia"}
PORTRAIT_EFFECTS = {"emotes", "whodatpal", "caption"}
HIDDEN_ADDITIONS = {"desk cam", "Browser", "palworl"}
BOTTOM_SOURCES = {
    "Mic/Aux", "Desktop Audio", "Application Audio Capture (BETA)",
    "Application Audio Capture (BETA) 2", "Application Audio Capture (BETA) 3",
    "spotifysound", "lumia", "solocast", "vlc", "vlc capture",
}
POST_CHAT = {"whodatpalalerts", "whodatpalLB", "whodatpal", "caption"}
BASE_VISUAL = {
    "Video Capture Device", "second facecam", "Game Capture", "main display",
    "main screen", "FullScreen Printer", "brb scene", "Stream Ended",
    "lumia startiing", "Shouty", "joyfull", "spiner", "desk cam",
}
RESTORE_REGULAR_GEOMETRY = {
    "follower", "whodatpalalerts", "whodatpalLB", "whodatpal",
    "lumia overlay", "caption", "emotes", "Cheer1",
}


def read_json(path):
    with open(path, encoding="utf-8-sig") as stream:
        return json.load(stream)


def write_json(path, value):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    with open(path, "w", encoding="utf-8") as stream:
        json.dump(value, stream, ensure_ascii=False, indent=4)
        stream.write("\n")


def sources_by_name(document):
    return {source["name"]: source for source in document["sources"]}


def reference_items(regular_sources, names):
    result = {}
    for name in names:
        scene = regular_sources.get(name)
        if scene and scene.get("id") == "scene":
            for item in scene["settings"].get("items", []):
                result.setdefault(item["name"], item)
    return result


def all_reference_items(regular_sources):
    names = [name for entries in LANDSCAPE_REFERENCES.values() for name in entries]
    names += [name for entries in PORTRAIT_REFERENCES.values() for name in entries]
    return reference_items(regular_sources, names)


def full_canvas_chat(item, portrait):
    item.update({"pos": {"x": 0.0, "y": 0.0},
                 "scale": {"x": 1.0 if portrait else 4.0 / 3.0,
                           "y": 1.0 if portrait else 4.0 / 3.0},
                 "bounds_type": 0, "bounds_align": 0, "bounds_crop": False,
                 "bounds": {"x": 0.0, "y": 0.0}, "locked": True,
                 "crop_left": 0, "crop_top": 0, "crop_right": 0,
                 "crop_bottom": 0, "visible": True})


def copy_geometry(destination, reference):
    for key in ("pos", "scale", "bounds", "rot", "align", "bounds_type",
                "bounds_align", "bounds_crop", "crop_left", "crop_top",
                "crop_right", "crop_bottom"):
        if key in reference:
            destination[key] = copy.deepcopy(reference[key])


def transform(item, order):
    position = item.get("pos", {})
    scale = item.get("scale", {})
    bounds = item.get("bounds", {})
    return {
        "alignment": item.get("align", 5),
        "boundsAlignment": item.get("bounds_align", 0),
        "boundsHeight": bounds.get("y", 0),
        "boundsType": item.get("bounds_type", 0),
        "boundsWidth": bounds.get("x", 0),
        "cropBottom": item.get("crop_bottom", 0),
        "cropLeft": item.get("crop_left", 0),
        "cropRight": item.get("crop_right", 0),
        "cropToBounds": item.get("bounds_crop", False),
        "cropTop": item.get("crop_top", 0),
        "locked": item.get("locked", False),
        "order": order,
        "positionX": position.get("x", 0),
        "positionY": position.get("y", 0),
        "rotation": item.get("rot", 0),
        "scaleX": scale.get("x", 1),
        "scaleY": scale.get("y", 1),
        "visible": item.get("visible", False),
    }


def arrange(items, chat_name):
    name = lambda item: item.get("name", item.get("source"))
    bottom = [item for item in items if name(item) in BOTTOM_SOURCES]
    base = [item for item in items if name(item) in BASE_VISUAL or
            name(item).endswith("backdrop")]
    middle = [item for item in items if name(item) not in BOTTOM_SOURCES and
              name(item) not in BASE_VISUAL and not name(item).endswith("backdrop")
              and name(item) not in POST_CHAT and name(item) != chat_name]
    chat = [item for item in items if name(item) == chat_name]
    post = [item for item in items if name(item) in POST_CHAT]
    return bottom + base + middle + chat + post


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for key in ("regular", "scenes", "actions", "out-scenes", "out-actions"):
        parser.add_argument("--" + key, required=True)
    args = parser.parse_args()
    regular = read_json(args.regular)
    scenes = read_json(args.scenes)
    actions = read_json(args.actions)
    regular_sources = sources_by_name(regular)
    isolated_sources = sources_by_name(scenes)
    fallback = all_reference_items(regular_sources)
    stage_items = {}
    reports = []

    for stage in list(LANDSCAPE_REFERENCES) + list(PORTRAIT_REFERENCES):
        portrait = stage.startswith("PW Vert ")
        refs = (PORTRAIT_REFERENCES if portrait else LANDSCAPE_REFERENCES)[stage]
        templates = reference_items(regular_sources, refs)
        scene = isolated_sources[stage]
        original = scene["settings"]["items"]
        original[:] = [item for item in original if item["name"] != "chat 3"]
        existing = {item["name"] for item in original}
        wanted = {name for name, item in templates.items() if item.get("visible")}
        if portrait:
            wanted |= PORTRAIT_SHARED
            if stage not in ("PW Vert Starting", "PW Vert Intermission"):
                wanted |= PORTRAIT_EFFECTS
            wanted.discard("Chatty")
            wanted.discard("FullScreen Printer")
            wanted.discard("chat 3")
        else:
            wanted -= LANDSCAPE_OMIT
            if stage == "PW Starting":
                wanted -= STARTING_OMIT
            if stage == "PW Celebration":
                wanted |= CELEBRATION_SHARED
        wanted = {name for name in wanted if name in isolated_sources or
                  name in ("Mic/Aux", "Desktop Audio")}
        added = []
        next_id = max([item["id"] for item in original] + [0]) + 1
        for name in sorted(wanted - existing):
            template = templates.get(name) or fallback.get(name)
            if not template:
                raise ValueError(f"No reference scene item for {name!r} in {stage}")
            item = copy.deepcopy(template)
            item["id"] = next_id
            next_id += 1
            item["visible"] = name not in HIDDEN_ADDITIONS
            if portrait and name == "spotifysound":
                item["visible"] = True
            original.append(item)
            added.append(name)
        for item in original:
            name = item["name"]
            if name in ("Chatty", "vert chatty"):
                full_canvas_chat(item, portrait)
            elif not portrait and name in RESTORE_REGULAR_GEOMETRY:
                reference = templates.get(name) or fallback.get(name)
                if reference:
                    copy_geometry(item, reference)
            elif portrait and stage not in ("PW Vert Starting", "PW Vert Intermission") \
                    and name in PORTRAIT_EFFECTS and name in templates:
                copy_geometry(item, templates[name])
                item["visible"] = True
        scene["settings"]["items"] = arrange(original, "vert chatty" if portrait else "Chatty")
        scene["settings"]["id_counter"] = max(
            item["id"] for item in scene["settings"]["items"]) + 1
        stage_items[stage] = {item["id"]: item for item in scene["settings"]["items"]}
        reports.append(f"{stage}: +{len(added)} ({', '.join(added)})")

    refined_actions = []
    for action in actions["actions"]:
        if action.get("name") == "PW Starting · Going live":
            continue
        stage = action.get("stage")
        if stage not in STAGE_PAIRS or action.get("kind") != "layout":
            refined_actions.append(action)
            continue
        action["items"] = [target for target in action["items"]
                           if target.get("source") != "chat 3"]
        updated = []
        for container in (stage, STAGE_PAIRS[stage]):
            portrait = container.startswith("PW Vert ")
            scene = isolated_sources[container]
            by_id = stage_items[container]
            old = [target for target in action["items"]
                   if target.get("container") == container]
            old.sort(key=lambda target: target["transform"].get("order", 0))
            present = {int(target["itemId"]) for target in old}
            for item in scene["settings"]["items"]:
                if item["id"] not in present:
                    old.append({"container": container, "itemId": str(item["id"]),
                                "source": item["name"], "transform": transform(item, 0)})
            old = arrange(old, "vert chatty" if portrait else "Chatty")
            for order, target in enumerate(old):
                item = by_id[int(target["itemId"])]
                target["transform"]["order"] = order
                if target["source"] in ("Chatty", "vert chatty"):
                    target["transform"] = transform(item, order)
                elif not portrait and target["source"] in RESTORE_REGULAR_GEOMETRY:
                    visible = target["transform"].get("visible", False)
                    target["transform"] = transform(item, order)
                    target["transform"]["visible"] = visible
                elif portrait and target["source"] in PORTRAIT_EFFECTS \
                        and container not in ("PW Vert Starting", "PW Vert Intermission"):
                    target["transform"] = transform(item, order)
            updated += old
        action["items"] = updated
        action["summary"] = (f"Switch to {stage}, then move {len(updated)} "
                             f"sources to the saved layout.")
        refined_actions.append(action)
    actions["actions"] = refined_actions
    write_json(args.out_scenes, scenes)
    write_json(args.out_actions, actions)
    print("\n".join(reports))
    print(f"Stage looks: {sum(action.get('kind') == 'layout' and action.get('stage') in STAGE_PAIRS for action in actions['actions'])}")


if __name__ == "__main__":
    main()
