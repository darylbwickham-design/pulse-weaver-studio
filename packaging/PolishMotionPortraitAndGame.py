"""Fill the portrait split looks and use the existing working Game Capture.

The input scene collection already contains the imported originals and the
Motion stages. Only PW scenes and PW looks change.
"""

import argparse
import json
from pathlib import Path


GAME_NAME = "Game Capture"
TOP = (0, 0, 1080, 608)
FOCUS = (0, 608, 1080, 1312)
OFF_CANVAS = (1200, 608, 1080, 1312)
LANDSCAPE_FACECAM = (1338, 48, 520, 292)
LANDSCAPE_PRINTER = (48, 829, 360, 203)
# Keep only the layers used by this show. The imported reference scenes and
# source definitions remain untouched so Restore Original Scenes still works.
SHOW_SOURCES = {
    "Desktop Audio", "Mic/Aux",
    "solocast", "lumia", "spotifysound", "spotify",
    "Game Capture", "main screen", "main display", "Video Capture Device",
    "second facecam", "Chatty", "vert chatty", "caption",
    "lumia overlay", "follower", "Cheer1", "firstmessage",
    "Sticker Party", "logo", "lumia startiing", "brb scene",
    "Stream Ended",
}
UNUSED_VISUALS = {
    "FullScreen Printer", "Game Capture", "main screen",
    "main display", "Video Capture Device", "second facecam", "desk cam",
}
LOOKS = {
    "PW Hangout · Facecam + printer": ("Video Capture Device", "second facecam"),
    "PW Hangout · Printer + facecam": ("second facecam", "Video Capture Device"),
    "PW Hangout · Screen share": ("second facecam", "main display"),
    "PW Gameplay · Gameplay": ("second facecam", GAME_NAME),
    "PW Gameplay · Screen activity": ("second facecam", "main screen"),
    "PW Gameplay · Printer activity": ("second facecam", "Video Capture Device"),
}


def read(path):
    with open(path, encoding="utf-8-sig") as stream:
        return json.load(stream)


def write(path, value):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    with open(path, "w", encoding="utf-8") as stream:
        json.dump(value, stream, ensure_ascii=False, indent=4)
        stream.write("\n")


def fill_region(target, region, visible, fit_width=False):
    x, y, width, height = region
    transform = target["transform"]
    transform.update({
        "alignment": 5, "boundsAlignment": 0,
        "boundsHeight": height, "boundsWidth": width,
        "boundsType": 4 if fit_width else 3,
        "cropToBounds": not fit_width,
        "cropLeft": 0, "cropRight": 0, "cropTop": 0, "cropBottom": 0,
        "positionX": x, "positionY": y, "rotation": 0,
        "scaleX": 1, "scaleY": 1, "visible": visible,
    })


def apply_to_scene_item(item, transform):
    item.update({
        "pos": {"x": transform["positionX"], "y": transform["positionY"]},
        "scale": {"x": transform["scaleX"], "y": transform["scaleY"]},
        "bounds": {"x": transform["boundsWidth"], "y": transform["boundsHeight"]},
        "rot": transform["rotation"], "align": transform["alignment"],
        "bounds_type": transform["boundsType"],
        "bounds_align": transform["boundsAlignment"],
        "bounds_crop": transform["cropToBounds"],
        "crop_left": transform["cropLeft"],
        "crop_right": transform["cropRight"],
        "crop_top": transform["cropTop"],
        "crop_bottom": transform["cropBottom"],
        "visible": transform["visible"],
    })


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("scenes", "actions", "out-scenes", "out-actions"):
        parser.add_argument("--" + name, required=True)
    args = parser.parse_args()
    scenes, actions = read(args.scenes), read(args.actions)
    # Legacy sample actions target the imported original scenes, include retired
    # browser overlays, and clutter Lumia's picker. Their scenes stay protected.
    actions["actions"] = [action for action in actions["actions"]
                          if (action.get("stage") or "").startswith("PW ")]
    stages = {source["name"]: source for source in scenes["sources"]
              if source["id"] == "scene" and source["name"].startswith("PW ")}
    if not any(source["name"] == GAME_NAME for source in scenes["sources"]):
        raise ValueError("The imported working Game Capture source is missing")

    removed = set()
    for stage in stages.values():
        items = stage["settings"]["items"]
        kept = [item for item in items if item["name"] in SHOW_SOURCES
                or item["name"].startswith("PW ")]
        removed.update(item["name"] for item in items if item not in kept)
        stage["settings"]["items"] = kept
    for action in actions["actions"]:
        if action.get("kind") == "layout" and action.get("stage", "").startswith("PW "):
            action["items"] = [target for target in action["items"]
                               if target["source"] in SHOW_SOURCES
                               or target["source"].startswith("PW ")]

    gameplay = next(action for action in actions["actions"]
                    if action.get("name") == "PW Gameplay · Gameplay")
    gameplay_targets = {target["source"]: target for target in gameplay["items"]
                        if target["container"] == "PW Gameplay"}
    for source, region in (("second facecam", LANDSCAPE_FACECAM),
                           ("Video Capture Device", LANDSCAPE_PRINTER)):
        if source not in gameplay_targets:
            raise ValueError(f"Missing landscape Gameplay source: {source}")
        fill_region(gameplay_targets[source], region, True)
    gameplay_scene_items = {item["id"]: item for item in
                            stages["PW Gameplay"]["settings"]["items"]}
    for source in ("second facecam", "Video Capture Device"):
        target = gameplay_targets[source]
        apply_to_scene_item(gameplay_scene_items[int(target["itemId"])],
                            target["transform"])

    first_looks = {}
    polished = []
    for action in actions["actions"]:
        if action.get("kind") != "layout" or not action.get("stage", "").startswith("PW "):
            continue
        pair = LOOKS.get(action["name"])
        if pair is None:
            continue
        top_source, focus_source = pair
        portrait = action["stage"].replace("PW ", "PW Vert ", 1)
        targets = [target for target in action["items"] if target["container"] == portrait]
        by_source = {target["source"]: target for target in targets}
        if top_source not in by_source or focus_source not in by_source:
            raise ValueError(f"Missing portrait focus source in {action['name']}")
        for source in UNUSED_VISUALS & by_source.keys():
            fill_region(by_source[source], OFF_CANVAS, False)
        fill_region(by_source[top_source], TOP, True, fit_width=True)
        fill_region(by_source[focus_source], FOCUS, True)
        first_looks.setdefault(portrait, targets)
        polished.append(action["name"])

    for portrait, targets in first_looks.items():
        by_id = {item["id"]: item for item in stages[portrait]["settings"]["items"]}
        for target in targets:
            if target["source"] in UNUSED_VISUALS:
                apply_to_scene_item(by_id[int(target["itemId"])], target["transform"])
    write(args.out_scenes, scenes)
    write(args.out_actions, actions)
    print("Gameplay source:", GAME_NAME)
    print("Removed unused stage layers:", ", ".join(sorted(removed)))
    print("Portrait looks polished:", ", ".join(polished))


if __name__ == "__main__":
    main()
