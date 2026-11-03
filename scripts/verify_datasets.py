#!/usr/bin/env python3
"""
Verify Moving AI maps and scenarios across partitions (FR-01, DR-01).
Validates:
- Header syntax and dimension consistency
- Traversability of start and goal locations on the associated map
- Unique start and goal positions among the first 100 agents
- Coordinate bounds
- Partition isolation
"""
import sys, os, json

def parse_map(map_path):
    with open(map_path, 'r') as f:
        lines = [line.strip() for line in f if line.strip()]
    assert lines[0].startswith("type"), f"Missing type header in {map_path}"
    h_parts = lines[1].split()
    w_parts = lines[2].split()
    assert h_parts[0] == "height" and w_parts[0] == "width"
    height = int(h_parts[1])
    width = int(w_parts[1])
    assert lines[3] == "map", f"Missing map header in {map_path}"
    grid = lines[4:]
    assert len(grid) == height, f"Grid height mismatch in {map_path}: got {len(grid)}, expected {height}"
    for r, row in enumerate(grid):
        assert len(row) == width, f"Row {r} width mismatch in {map_path}: got {len(row)}, expected {width}"
    return width, height, grid

def is_traversable(grid, x, y):
    # MovingAI: '.' is passable, '@' obstacle, 'T' tree/water, 'O' out of bounds, etc.
    # '.' and 'G' and 'S' are considered traversable.
    c = grid[y][x]
    return c in ('.', 'G', 'S')

def verify_dataset(manifest_path="data/dataset_manifest.json"):
    with open(manifest_path, 'r') as f:
        manifest = json.load(f)

    total_scenarios_checked = 0
    total_maps_checked = 0

    for part_name, part_data in manifest["partitions"].items():
        print(f"Verifying partition: {part_name}...")
        maps = {}
        for map_file, m_info in part_data["maps"].items():
            w, h, grid = parse_map(m_info["path"])
            maps[map_file] = (w, h, grid)
            total_maps_checked += 1

        for scen_file, s_info in part_data["scenarios"].items():
            scen_path = s_info["path"]
            with open(scen_path, 'r') as f:
                lines = [line.strip() for line in f if line.strip()]
            assert lines[0].startswith("version"), f"Missing version in {scen_path}"
            records = lines[1:]
            assert len(records) >= 100, f"Scenario {scen_file} has fewer than 100 agent specifications ({len(records)})"

            # Check up to 100 agents (benchmark primary evaluation range: 50, 100)
            seen_starts = set()
            seen_goals = set()

            for idx, line in enumerate(records[:100]):
                parts = line.split('\t')
                if len(parts) == 1:
                    parts = line.split()
                assert len(parts) == 9, f"Invalid record format in {scen_path} line {idx+2}: {line}"
                map_name = parts[1]
                assert map_name in maps, f"Scenario {scen_file} references map {map_name} not in partition {part_name}"
                w, h, grid = maps[map_name]
                scen_w = int(parts[2])
                scen_h = int(parts[3])
                assert scen_w == w and scen_h == h, f"Dimension mismatch in {scen_file} line {idx+2}"

                sx, sy = int(parts[4]), int(parts[5])
                gx, gy = int(parts[6]), int(parts[7])

                # Check bounds
                assert 0 <= sx < w and 0 <= sy < h, f"Start out of bounds in {scen_file} agent {idx}: ({sx},{sy})"
                assert 0 <= gx < w and 0 <= gy < h, f"Goal out of bounds in {scen_file} agent {idx}: ({gx},{gy})"

                # Check obstacle traversability
                assert is_traversable(grid, sx, sy), f"Start blocked in {scen_file} agent {idx}: ({sx},{sy}) = '{grid[sy][sx]}'"
                assert is_traversable(grid, gx, gy), f"Goal blocked in {scen_file} agent {idx}: ({gx},{gy}) = '{grid[gy][gx]}'"

                # Check duplicate start/goal
                assert (sx, sy) not in seen_starts, f"Duplicate start in {scen_file} agent {idx}: ({sx},{sy})"
                assert (gx, gy) not in seen_goals, f"Duplicate goal in {scen_file} agent {idx}: ({gx},{gy})"
                seen_starts.add((sx, sy))
                seen_goals.add((gx, gy))

            total_scenarios_checked += 1

    print(f"Verification SUCCESSFUL: {total_maps_checked} maps and {total_scenarios_checked} scenarios verified.")
    return 0

if __name__ == "__main__":
    sys.exit(verify_dataset())
