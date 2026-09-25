"""Fill the portrait split looks and use the existing working Game Capture.

The input scene collection already contains the imported originals and the
Motion stages. Only PW scenes and PW looks change.
"""

import argparse
import copy
import json
from pathlib import Path
import uuid


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
    "Stream Ended", "PW Ending Credits",
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
    reference = item.get("scale_ref", {})
    half_height = float(reference.get("y", 0)) / 2
    half_width = float(reference.get("x", 0)) / 2
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
    # OBS scene files also carry coordinates relative to canvas height. If
    # these are left at their imported values, OBS reconstructs stale geometry
    # on startup even though the absolute fields above look correct in JSON.
    if half_height > 0:
        item["pos_rel"] = {
            "x": (transform["positionX"] - half_width) / half_height,
            "y": (transform["positionY"] - half_height) / half_height,
        }
        item["bounds_rel"] = {
            "x": transform["boundsWidth"] / half_height,
            "y": transform["boundsHeight"] / half_height,
        }
        item["scale_rel"] = {
            "x": transform["scaleX"], "y": transform["scaleY"],
        }


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

    # Reuse the original Lumia ending overlay twice in portrait: the title
    # occupies the top third and its live credits panel fills the lower area.
    # Both references are confined to this new PW scene; originals stay intact.
    credit_name = "PW Ending Credits"
    ended_source = next(source for source in scenes["sources"]
                        if source["name"] == "Stream Ended")
    credit_source = next((source for source in scenes["sources"]
                          if source["name"] == credit_name), None)
    if credit_source is None:
        credit_source = copy.deepcopy(ended_source)
        credit_source["name"] = credit_name
        credit_source["uuid"] = str(uuid.uuid5(uuid.NAMESPACE_URL,
                                               "pulseweaver-motion-preview/ending-credits"))
        scenes["sources"].append(credit_source)
    portrait_items = stages["PW Vert Intermission"]["settings"]["items"]
    credit_item = next((item for item in portrait_items
                        if item["name"] == credit_name), None)
    if credit_item is None:
        credit_item = copy.deepcopy(next(item for item in portrait_items
                                         if item["name"] == "Stream Ended"))
        credit_item["name"] = credit_name
        credit_item["source_uuid"] = credit_source["uuid"]
        credit_item["id"] = max(item["id"] for item in portrait_items) + 1
        credit_item["visible"] = False
        portrait_items.append(credit_item)
    for action in actions["actions"]:
        if action.get("stage") != "PW Intermission":
            continue
        if any(target["source"] == credit_name for target in action["items"]):
            continue
        title = next(target for target in action["items"]
                     if target["container"] == "PW Vert Intermission"
                     and target["source"] == "Stream Ended")
        credit_target = copy.deepcopy(title)
        credit_target["source"] = credit_name
        credit_target["itemId"] = str(credit_item["id"])
        credit_target["transform"]["visible"] = False
        action["items"].append(credit_target)

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

    # The physical printer feed may not have the same aspect ratio as the
    # canvas. Fill both Hangout regions so a swap never letterboxes a camera.
    for name in ("PW Hangout · Facecam + printer",
                 "PW Hangout · Printer + facecam"):
        look = next(action for action in actions["actions"]
                    if action.get("name") == name)
        for target in look["items"]:
            if target["container"] != "PW Hangout" or target["source"] not in (
                    "second facecam", "Video Capture Device"):
                continue
            target["transform"]["boundsType"] = 3
            target["transform"]["cropToBounds"] = True
            if name == "PW Hangout · Facecam + printer":
                scene_items = {item["id"]: item for item in
                               stages["PW Hangout"]["settings"]["items"]}
                apply_to_scene_item(scene_items[int(target["itemId"])],
                                    target["transform"])

    # A full-size camera under BRB makes the live printer the backdrop.
    # Ending keeps its original full-bleed artwork in landscape.
    for action in actions["actions"]:
        if action.get("stage") != "PW Intermission":
            continue
        name = action["name"]
        for target in action["items"]:
            source, container = target["source"], target["container"]
            portrait = container == "PW Vert Intermission"
            if source == "Video Capture Device":
                fill_region(target, (0, 0, 1080, 1920) if portrait
                            else (0, 0, 1920, 1080),
                            not name.endswith("Ending"))
            if name.endswith("BRB") and source == "brb scene":
                fill_region(target, (0, 0, 1080, 1280) if portrait
                            else (96, 54, 1728, 972), True)
                # Portrait scales by height and lets the 16:9 artwork hang
                # beyond the sides. Its central title stays large and legible.
            if name.endswith("Ending") and portrait:
                if source == "Stream Ended":
                    fill_region(target, (0, 0, 1080, 608), True)
                    target["transform"].update(cropLeft=300, cropRight=560,
                                               cropTop=260, cropBottom=240)
                elif source == credit_name:
                    # The panel sits at the right edge of Lumia's 16:9 art.
                    # Widen its portrait crop and offset it so the panel
                    # occupies the centre of the lower two thirds.
                    fill_region(target, (-270, 608, 1350, 1312), True)
                    target["transform"].update(cropLeft=1740, cropRight=0,
                                               cropTop=260, cropBottom=250)
            elif name.endswith("Ending") and source == "Stream Ended":
                fill_region(target, (0, 0, 1920, 1080), True)

        # OBS order zero is the back. Keep the print under the art and the
        # credits above it for all three Intermission looks.
        for container in ("PW Intermission", "PW Vert Intermission"):
            targets = [target for target in action["items"]
                       if target["container"] == container]
            by_name = {target["source"]: target for target in targets}
            original = sorted(targets, key=lambda target: target["transform"]["order"])
            stack = [target for target in original
                     if target["source"] not in ("Video Capture Device", credit_name)]
            camera = by_name["Video Capture Device"]
            art_index = next(index for index, target in enumerate(stack)
                             if target["source"] == "brb scene")
            stack.insert(art_index, camera)
            if container == "PW Vert Intermission":
                credit = by_name[credit_name]
                ended_index = next(index for index, target in enumerate(stack)
                                   if target["source"] == "Stream Ended")
                stack.insert(ended_index + 1, credit)
            for order, target in enumerate(stack):
                target["transform"]["order"] = order

    # PW Intermission starts in its BRB look before any cue is recalled.
    brb = next(action for action in actions["actions"]
               if action.get("name") == "PW Intermission · BRB")
    for container in ("PW Intermission", "PW Vert Intermission"):
        by_id = {item["id"]: item for item in stages[container]["settings"]["items"]}
        for target in brb["items"]:
            if target["container"] == container and target["source"] in (
                    "Video Capture Device", "brb scene", credit_name):
                apply_to_scene_item(by_id[int(target["itemId"])],
                                    target["transform"])
        brb_order = {int(target["itemId"]): target["transform"]["order"]
                     for target in brb["items"] if target["container"] == container}
        stages[container]["settings"]["items"].sort(
            key=lambda item: brb_order.get(item["id"], len(brb_order)))

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
    # The standard Starting facecam crop is aimed at the pixel board. Enlarge
    # that portrait inset while keeping its lower-right anchor; landscape and
    # every other look retain their existing transforms.
    starting = next(action for action in actions["actions"]
                    if action.get("name") == "PW Starting · Starting")
    pixel_board = next(target for target in starting["items"]
                       if target["container"] == "PW Vert Starting"
                       and target["source"] == "second facecam")
    pixel_board["transform"].update(
        positionX=524, positionY=-371,
        scaleX=0.8187204003334045, scaleY=0.8185389041900635,
        cropLeft=381, cropTop=167, cropRight=491, cropBottom=0)
    starting_stack = sorted(
        (target for target in starting["items"]
         if target["container"] == "PW Vert Starting" and target is not pixel_board),
        key=lambda target: target["transform"]["order"])
    printer_index = next(index for index, target in enumerate(starting_stack)
                         if target["source"] == "Video Capture Device"
                         and target["transform"]["visible"])
    starting_stack.insert(printer_index, pixel_board)
    for order, target in enumerate(starting_stack):
        target["transform"]["order"] = order
    # The portable app may reopen directly on any PW Stage. Seed each new
    # scene with its first saved look so the first programme frame is complete,
    # including the portrait focus region. Only PW scenes are changed.
    defaults = {
        "PW Starting": "PW Starting · Starting",
        "PW Intermission": "PW Intermission · BRB",
        "PW Hangout": "PW Hangout · Facecam + printer",
        "PW Gameplay": "PW Gameplay · Gameplay",
        "PW Celebration": "PW Celebration · Raid welcome",
    }
    for stage_name, look_name in defaults.items():
        look = next(action for action in actions["actions"]
                    if action.get("name") == look_name)
        for container in (stage_name, stage_name.replace("PW ", "PW Vert ", 1)):
            scene = stages[container]
            by_id = {item["id"]: item for item in scene["settings"]["items"]}
            targets = [target for target in look["items"]
                       if target["container"] == container]
            for target in targets:
                item = by_id.get(int(target["itemId"]))
                if item is None:
                    raise ValueError(f"Missing item {target['itemId']} in {container}")
                apply_to_scene_item(item, target["transform"])
            positions = {int(target["itemId"]): target["transform"]["order"]
                         for target in targets}
            scene["settings"]["items"].sort(
                key=lambda item: positions.get(item["id"], len(positions)))
    write(args.out_scenes, scenes)
    write(args.out_actions, actions)
    print("Gameplay source:", GAME_NAME)
    print("Removed unused stage layers:", ", ".join(sorted(removed)))
    print("Portrait looks polished:", ", ".join(polished))


if __name__ == "__main__":
    main()
