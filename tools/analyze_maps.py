import collections, json, os, sys
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from cofcoop.bsp import list_maps, load_map_entities

maps_dir = sys.argv[1]
out_path = sys.argv[2]
maps = list_maps(maps_dir)
names = set(maps)
report = {}
link_kinds = collections.Counter()
spawn_kinds = collections.Counter()
errors = {}
for name, path in sorted(maps.items()):
    try:
        ents = load_map_entities(path)
    except Exception as e:
        errors[name] = str(e)
        continue
    ws = ents[0] if ents else {}
    links, spawns, classes = [], collections.Counter(), collections.Counter()
    for e in ents:
        cn = e.get("classname", "")
        classes[cn] += 1
        if cn.startswith("info_player") or cn.startswith("cof_coop_spawn"):
            spawns[cn] += 1
            spawn_kinds[cn] += 1
        for k, v in e.items():
            lv = v.strip().lower()
            if k != "classname" and lv in names and lv != name:
                links.append({"class": cn, "key": k, "map": lv,
                              "landmark": e.get("landmark", ""),
                              "targetname": e.get("targetname", "")})
                link_kinds[(cn, k)] += 1
    report[name] = {
        "worldspawn": {k: v for k, v in ws.items() if k in ("iuser1", "message", "skyname", "chaptertitle", "startdark", "gametitle")},
        "links": links, "spawns": dict(spawns),
        "cof_classes": {c: n for c, n in classes.items() if c.startswith(("cof_", "inter_door", "trigger_coop", "func_coop", "info_player", "trigger_changelevel", "game_player"))},
    }
json.dump({"maps": report, "errors": errors}, open(out_path, "w"), indent=1)
print("maps:", len(report), "errors:", errors)
print("link kinds:", link_kinds.most_common(30))
print("spawn kinds:", spawn_kinds.most_common())
print("lobby maps (iuser1):", [n for n, r in report.items() if r["worldspawn"].get("iuser1")])
