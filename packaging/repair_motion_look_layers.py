"""Repair inconsistent saved layer order in the isolated Motion Preview show.

The protected OBS scene snapshots are not touched. Run against the task's
portable profile while Pulse Weaver is closed; a dated backup is written first.
"""

import argparse
import json
import os
import shutil
from datetime import datetime
from pathlib import Path


def find_action(actions, name):
    matches = [action for action in actions if action.get("name") == name]
    if len(matches) != 1 or matches[0].get("collection") != "og scenes (OBS Import)":
        raise ValueError(f"Expected one imported show look: {name}")
    return matches[0]


def layer_map(action, container):
    rows = [item for item in action["items"] if item.get("container") == container]
    names = [item["source"] for item in rows]
    if len(names) != len(set(names)):
        raise ValueError(f"Duplicate sources in {action['name']} / {container}")
    return {item["source"]: item for item in rows}


def repair(profile):
    data = json.loads(profile.read_text(encoding="utf-8"))
    actions = data["actions"]
    changes = []
    for stage, reference_name, looks, container in (
        ("Intermission", "Ending", ("BRB", "Printer break"), "PW Vert Intermission"),
        ("Celebration", "Raid welcome", ("Shoutout", "Thank you"), "PW Vert Celebration"),
    ):
        reference = layer_map(find_action(actions, f"PW {stage} · {reference_name}"), container)
        for look_name in looks:
            action = find_action(actions, f"PW {stage} · {look_name}")
            layers = layer_map(action, container)
            if set(layers) != set(reference):
                raise ValueError(f"Source set changed in {action['name']}; no changes were written")
            for source, item in layers.items():
                desired_order = reference[source]["transform"]["order"]
                transform = item["transform"]
                if transform["order"] != desired_order:
                    transform["order"] = desired_order
                    changes.append(f"{look_name}: {source} layer")
            if stage == "Intermission" and look_name == "BRB":
                # Full-canvas event graphics stay available while BRB is on air.
                overlay = layers["lumia overlay"]["transform"]
                if not overlay["visible"]:
                    overlay["visible"] = True
                    changes.append("BRB: Lumia overlay visible")
            if stage == "Celebration" and look_name == "Thank you":
                for source in ("Cheer1", "emotes", "lumia overlay"):
                    transform = layers[source]["transform"]
                    if not transform["visible"]:
                        transform["visible"] = True
                        changes.append(f"Thank you: {source} visible")
    if not changes:
        return changes
    backup = profile.with_name(profile.stem + "-before-layer-repair-" + datetime.now().strftime("%Y%m%d-%H%M%S") + profile.suffix)
    shutil.copy2(profile, backup)
    temporary = profile.with_suffix(profile.suffix + ".tmp")
    temporary.write_text(json.dumps(data, ensure_ascii=False, indent=4) + "\n", encoding="utf-8")
    os.replace(temporary, profile)
    print(f"Backup: {backup}")
    return changes


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("profile", type=Path)
    args = parser.parse_args()
    for change in repair(args.profile):
        print(change)
