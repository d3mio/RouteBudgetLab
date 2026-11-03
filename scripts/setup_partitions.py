#!/usr/bin/env python3
import os
import zipfile
import json

MAP_ZIP = "data/download/mapf-map.zip"
SCEN_ZIP = "data/download/mapf-scen-random.zip"

PARTITIONS = {
    "training": [
        "warehouse-10-20-10-2-1",
        "random-32-32-20",
        "maze-32-32-2",
        "empty-32-32"
    ],
    "validation": [
        "warehouse-10-20-10-2-2",
        "random-64-64-10"
    ],
    "test": [
        "warehouse-20-40-10-2-1",
        "random-64-64-20",
        "room-64-64-8",
        "ost003d"
    ]
}

def setup_partitions():
    manifest = {"partitions": {}}

    with zipfile.ZipFile(MAP_ZIP, 'r') as map_zf, zipfile.ZipFile(SCEN_ZIP, 'r') as scen_zf:
        scen_namelist = scen_zf.namelist()
        map_namelist = map_zf.namelist()

        for part_name, map_keys in PARTITIONS.items():
            maps_dir = f"data/{part_name}/maps"
            scens_dir = f"data/{part_name}/scenarios"
            os.makedirs(maps_dir, exist_ok=True)
            os.makedirs(scens_dir, exist_ok=True)

            part_manifest = {"maps": {}, "scenarios": {}}

            for key in map_keys:
                map_filename = f"{key}.map"
                # Extract map
                if map_filename in map_namelist:
                    map_zf.extract(map_filename, maps_dir)
                    part_manifest["maps"][map_filename] = {
                        "path": f"{maps_dir}/{map_filename}"
                    }
                else:
                    raise FileNotFoundError(f"Map {map_filename} not found in {MAP_ZIP}")

                # Extract all 25 random scenarios for this map
                for i in range(1, 26):
                    scen_filename = f"{key}-random-{i}.scen"
                    # zip might store under scen-random/
                    scen_zip_entry = f"scen-random/{scen_filename}"
                    if scen_zip_entry in scen_namelist:
                        # Extract and move to scens_dir
                        data = scen_zf.read(scen_zip_entry)
                        target_path = f"{scens_dir}/{scen_filename}"
                        with open(target_path, "wb") as f_out:
                            f_out.write(data)
                        part_manifest["scenarios"][scen_filename] = {
                            "path": target_path,
                            "map": map_filename
                        }
                    else:
                        raise FileNotFoundError(f"Scenario {scen_zip_entry} not found in {SCEN_ZIP}")

            manifest["partitions"][part_name] = part_manifest
            print(f"Partition {part_name}: {len(part_manifest['maps'])} maps, {len(part_manifest['scenarios'])} scenarios extracted.")

    os.makedirs("data", exist_ok=True)
    with open("data/dataset_manifest.json", "w") as f:
        json.dump(manifest, f, indent=2)
    print("Manifest written to data/dataset_manifest.json")

if __name__ == "__main__":
    setup_partitions()
