"""Migrate a resource pack zip to another Minecraft version.

What it does, going up or down:
  - pack.mcmeta gets the target's pack format (and no format ranges or overlays);
  - overlays that apply to the target version are merged into the pack itself;
  - files Minecraft renamed between the two versions are renamed (short_grass/grass,
    turtle_scute/scute, the 1.21.2 armour and elytra textures), and models that point
    at them are rewritten;
  - going below 1.21.4, item model definitions (assets/*/items/) become plain item models.

What it can't do is reported as notes, e.g. the GUI, which is a set of sprites since
1.20.2 and a few big sheets before that: those parts fall back to the default look.

The zip is read and written entry by entry, so big packs don't have to fit in memory.
"""
import json
import re
import shutil
import zipfile

# First Minecraft version of each resource pack format.
VERSION_FORMATS = [
    ("1.13", 4), ("1.15", 5), ("1.16.2", 6), ("1.17", 7), ("1.18", 8), ("1.19", 9), ("1.19.3", 12),
    ("1.19.4", 13), ("1.20", 15), ("1.20.2", 18), ("1.20.3", 22), ("1.20.5", 32), ("1.21", 34),
    ("1.21.2", 42), ("1.21.4", 46), ("1.21.5", 55), ("1.21.6", 63), ("1.21.7", 64), ("1.21.9", 69),
]
OVERLAYS_FROM = 18        # 1.20.2: pack overlays
GUI_SPRITES_FROM = 18     # 1.20.2: the GUI is cut into sprites
EQUIPMENT_FROM = 42       # 1.21.2: armour textures under entity/equipment
ITEM_DEFINITIONS_FROM = 46  # 1.21.4: assets/<ns>/items/<id>.json
MIN_MAX_FORMAT_FROM = 65  # 1.21.9: min_format/max_format in pack.mcmeta


def version_key(version):
    return tuple(int(p) if p.isdigit() else 0 for p in version.split("."))


def format_for(minecraft):
    """The resource pack format of a Minecraft version, e.g. "1.20.1" -> 15."""
    fmt = None
    for first, number in VERSION_FORMATS:
        if version_key(first) <= version_key(minecraft):
            fmt = number
    return fmt


def _renames():
    """(format that introduced it, old path, new path), relative to assets/minecraft/."""
    renames = [
        (22, "textures/block/grass.png", "textures/block/short_grass.png"),
        (22, "models/block/grass.json", "models/block/short_grass.json"),
        (22, "models/item/grass.json", "models/item/short_grass.json"),
        (22, "blockstates/grass.json", "blockstates/short_grass.json"),
        (32, "textures/item/scute.png", "textures/item/turtle_scute.png"),
        (32, "models/item/scute.json", "models/item/turtle_scute.json"),
        (EQUIPMENT_FROM, "textures/entity/elytra.png", "textures/entity/equipment/wings/elytra.png"),
        (EQUIPMENT_FROM, "textures/models/armor/leather_layer_1_overlay.png",
         "textures/entity/equipment/humanoid/leather_overlay.png"),
        (EQUIPMENT_FROM, "textures/models/armor/leather_layer_2_overlay.png",
         "textures/entity/equipment/humanoid_leggings/leather_overlay.png"),
    ]
    for old, new in (("leather", "leather"), ("chainmail", "chainmail"), ("iron", "iron"), ("gold", "gold"),
                     ("diamond", "diamond"), ("netherite", "netherite"), ("turtle", "turtle_scute")):
        renames.append((EQUIPMENT_FROM, f"textures/models/armor/{old}_layer_1.png",
                        f"textures/entity/equipment/humanoid/{new}.png"))
        renames.append((EQUIPMENT_FROM, f"textures/models/armor/{old}_layer_2.png",
                        f"textures/entity/equipment/humanoid_leggings/{new}.png"))
    return renames


RENAMES = _renames()


def rename_map(source, target):
    """Path renames (relative to assets/minecraft/) to go from format source to format target."""
    mapping = {}
    for introduced, old, new in RENAMES:
        if source < introduced <= target:
            mapping[old] = new
        elif target < introduced <= source:
            mapping[new] = old
    # Animation/metadata files travel with their texture.
    mapping.update({f"{a}.mcmeta": f"{b}.mcmeta" for a, b in list(mapping.items()) if a.endswith(".png")})
    return mapping


def _resource_id(path):
    """textures/block/grass.png -> block/grass; models/item/x.json -> item/x; else None."""
    for prefix, ext in (("textures/", ".png"), ("models/", ".json")):
        if path.startswith(prefix) and path.endswith(ext):
            return path[len(prefix):-len(ext)]
    return None


def reference_rewrites(mapping):
    """Regexes that rewrite references in model/blockstate JSON to renamed textures and models."""
    rewrites = []
    for old, new in mapping.items():
        old_id, new_id = _resource_id(old), _resource_id(new)
        if old_id and new_id and old_id != new_id:
            rewrites.append((re.compile(r'"((?:minecraft:)?)' + re.escape(old_id) + r'"'), r'"\g<1>' + new_id + '"'))
    return rewrites


def _overlay_applies(formats, fmt):
    """Whether an overlay's "formats" (a number, [min, max] or {min_inclusive, max_inclusive}) cover fmt."""
    if isinstance(formats, int):
        return formats == fmt
    if isinstance(formats, list) and len(formats) == 2:
        return formats[0] <= fmt <= formats[1]
    if isinstance(formats, dict):
        return formats.get("min_inclusive", 0) <= fmt <= formats.get("max_inclusive", 10 ** 6)
    return False


def _pack_format(pack):
    fmt = pack.get("pack_format", pack.get("min_format"))
    if isinstance(fmt, list):   # 1.21.9 allows [major, minor]
        fmt = fmt[0]
    return int(fmt) if fmt is not None else None


def supports(source_zip, fmt):
    """Whether a resource pack zip is made for format fmt (its pack_format, or the range it declares)."""
    with zipfile.ZipFile(source_zip) as z:
        metas = sorted((n for n in z.namelist() if n.rsplit("/", 1)[-1] == "pack.mcmeta"), key=len)
        if not metas:
            raise NotAResourcePack("no pack.mcmeta in it")
        pack = json.loads(z.read(metas[0]).decode("utf-8-sig")).get("pack", {})
    ranges = pack.get("supported_formats")
    if ranges is not None:
        return _overlay_applies(ranges, fmt)
    if "min_format" in pack and "max_format" in pack:
        low, high = (v[0] if isinstance(v, list) else v for v in (pack["min_format"], pack["max_format"]))
        return int(low) <= fmt <= int(high)
    return _pack_format(pack) == fmt


class NotAResourcePack(ValueError):
    pass


def migrate(source_zip, target_zip, target_format, progress=lambda fraction: None):
    """Write a copy of source_zip for resource pack format target_format to target_zip.

    Returns (source format, notes) where notes say what couldn't be converted.
    """
    notes = []
    with zipfile.ZipFile(source_zip) as zin:
        files = [i for i in zin.infolist() if not i.is_dir()]
        metas = sorted((i.filename for i in files if i.filename.rsplit("/", 1)[-1] == "pack.mcmeta"), key=len)
        if not metas:
            raise NotAResourcePack("no pack.mcmeta in it")
        root = metas[0][:-len("pack.mcmeta")]     # packs zipped with their folder around them
        meta = json.loads(zin.read(metas[0]).decode("utf-8-sig"))
        pack = meta.get("pack", {})
        source_format = _pack_format(pack)
        if source_format is None:
            raise NotAResourcePack("pack.mcmeta has no pack_format")

        # Overlays: their directories, and those that apply to the target, in order (later wins).
        entries = (meta.get("overlays") or {}).get("entries", [])
        overlay_dirs = {e.get("directory", "") for e in entries if e.get("directory")}
        applying = [e["directory"] for e in entries if e.get("directory") and _overlay_applies(e.get("formats"), target_format)]

        mapping = rename_map(source_format, target_format)
        rewrites = reference_rewrites(mapping)

        # Plan: output name -> (zip entry, priority); overlays beat the base, files already at
        # their output name beat renamed ones.
        plan = {}
        for info in files:
            if not info.filename.startswith(root):
                continue
            rel = info.filename[len(root):]
            if rel == "pack.mcmeta":
                continue
            top = rel.split("/", 1)[0]
            if top in overlay_dirs:
                if top not in applying or "/" not in rel:
                    continue
                rel = rel.split("/", 1)[1]
                priority = 10 + applying.index(top)
            else:
                priority = 0
            out = rel
            if rel.startswith("assets/minecraft/") and rel[len("assets/minecraft/"):] in mapping:
                out = "assets/minecraft/" + mapping[rel[len("assets/minecraft/"):]]
            rank = (priority, out == rel)
            if out not in plan or rank > plan[out][1]:
                plan[out] = (info, rank)

        # Below 1.21.4: plain item models for item definitions that just name a model.
        generated = {}
        if target_format < ITEM_DEFINITIONS_FROM <= source_format:
            skipped = 0
            for out, (info, _) in plan.items():
                m = re.fullmatch(r"assets/([^/]+)/items/(.+)\.json", out)
                if not m:
                    continue
                model_path = f"assets/{m.group(1)}/models/item/{m.group(2)}.json"
                if model_path in plan:
                    continue
                try:
                    model = json.loads(zin.read(info).decode("utf-8-sig")).get("model", {})
                except ValueError:
                    model = {}
                if model.get("type", "").removeprefix("minecraft:") == "model" and model.get("model"):
                    generated[model_path] = json.dumps({"parent": model["model"]}, indent=2)
                else:
                    skipped += 1
            if skipped:
                notes.append(f"{skipped} item definition(s) choose between models (by state, count or custom data); "
                             "before 1.21.4 those items use their default model.")

        if target_format < GUI_SPRITES_FROM <= source_format and any(o.startswith("assets/minecraft/textures/gui/sprites/") for o in plan):
            notes.append("The GUI is made of single sprites since 1.20.2; older versions use a few big sheets "
                         "(widgets.png, icons.png), so the pack's GUI parts show the default look.")
        if source_format < GUI_SPRITES_FROM <= target_format and any(
                o in plan for o in ("assets/minecraft/textures/gui/widgets.png", "assets/minecraft/textures/gui/icons.png")):
            notes.append("The pack's GUI sheets (widgets.png, icons.png) aren't cut into the sprites 1.20.2+ "
                         "uses; those parts show the default look.")
        if (min(source_format, target_format) < EQUIPMENT_FROM <= max(source_format, target_format)
                and any(o.startswith("assets/minecraft/shaders/core/") for o in plan)):
            notes.append("Core shaders changed in 1.21.2 and were copied unchanged; they may not work.")
        if source_format == target_format:
            notes.append("It was already made for this version; only the overlays and pack.mcmeta were tidied up.")

        # pack.mcmeta for the target.
        pack = {k: v for k, v in pack.items() if k not in ("supported_formats", "min_format", "max_format")}
        pack["pack_format"] = target_format
        if target_format >= MIN_MAX_FORMAT_FROM:
            pack["min_format"] = pack["max_format"] = target_format
        meta = {k: v for k, v in meta.items() if k != "overlays"}
        meta["pack"] = pack

        total = max(1, len(plan) + len(generated))
        done = 0
        with zipfile.ZipFile(target_zip, "w", zipfile.ZIP_DEFLATED, compresslevel=6) as zout:
            zout.writestr("pack.mcmeta", json.dumps(meta, indent=2, ensure_ascii=False))
            for out in sorted(plan):
                info = plan[out][0]
                compress = zipfile.ZIP_STORED if out.lower().endswith((".png", ".ogg")) else zipfile.ZIP_DEFLATED
                if rewrites and out.endswith(".json") and re.match(r"assets/[^/]+/(models|blockstates|items)/", out):
                    text = zin.read(info).decode("utf-8-sig", errors="replace")
                    for pattern, replacement in rewrites:
                        text = pattern.sub(replacement, text)
                    zout.writestr(zipfile.ZipInfo(out, info.date_time), text, compress_type=compress)
                else:
                    target_info = zipfile.ZipInfo(out, info.date_time)
                    target_info.compress_type = compress
                    with zin.open(info) as src, zout.open(target_info, "w", force_zip64=True) as dst:
                        shutil.copyfileobj(src, dst, 1024 * 1024)
                done += 1
                if done % 200 == 0:
                    progress(done / total)
            for out, text in generated.items():
                zout.writestr(out, text)
    progress(1.0)
    return source_format, notes
