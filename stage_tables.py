"""Prepare an append-only Unruned Oath overlay for a Reign of the Warlock mod."""
import argparse
import csv
import io
from itertools import combinations
import json
from pathlib import Path
import re

SET = "The Unruned Oath"
AURA = "Unruned Oath"
KEY = "UnrunedOath"
STATE = "unruned_oath"
TABLE_KEYS = {
    "sets": "index",
    "setitems": "index",
    "skills": "skill",
    "states": "state",
    "skilldesc": "skilldesc",
}
DATA_FORMAT = 1
MAX_STATS = 32
RING_LEVEL = 75
REQUIRED_LEVELS = {
    "Dawn": 40, "Dusk": 45, "Eclipse": 65, "Zenith": 55, "Midnight": 60, "Horizon": 50,
}
RING_ROLLS = {
    "Dawn": (("hp", 25, 35), ("heal-kill", 5, 5), ("lifesteal", 5, 5)),
    "Dusk": (("mana", 25, 35), ("mana-kill", 5, 5), ("regen-mana", 15, 20)),
    "Eclipse": (("dmg-min", 5, 10), ("deadly", 5, 10), ("crush", 5, 10)),
    "Zenith": (("att", 100, 150), ("dex", 10, 15), ("block1", 15, 15)),
    "Midnight": (("res-all", 15, 20), ("red-dmg", 5, 5), ("red-mag", 5, 5)),
    "Horizon": (("move1", 10, 15), ("mag%", 15, 20)),
}

# Grouped properties write each underlying stat and get one tooltip line.
PAIR_STATS = {
    "skills": (("item_allskills",), None),
    "cast": (("item_fastercastrate",), None),
    "attack": (("item_fasterattackrate",), None),
    "run": (("item_fastermovevelocity",), None),
    "damage": (("item_mindamage_percent", "item_maxdamage_percent"), "strModEnhancedDamage"),
    "attack_rating": (("tohit",), None),
    "life": (("maxhp",), None),
    "resists": (("fireresist", "coldresist", "lightresist", "poisonresist"), "strModAllResistances"),
    "hit_recovery": (("item_fastergethitrate",), None),
    "life_leech": (("lifedrainmindam",), None),
    "mana_leech": (("manadrainmindam",), None),
    "mana": (("maxmana",), None),
    "mana_regen": (("manarecoverybonus",), None),
    "max_mana": (("item_maxmana_percent",), None),
    "fire": (("passive_fire_mastery",), None),
    "cold": (("passive_cold_mastery",), None),
    "lightning": (("passive_ltng_mastery",), None),
    "poison": (("passive_pois_mastery",), None),
    "pierce": (("item_pierce",), None),
    "cannot_freeze": (("item_cannotbefrozen",), None),
    "magic_find": (("item_magicbonus",), None),
    "block_rate": (("item_fasterblockrate",), None),
    "block_chance": (("toblock",), None),
    "strength": (("strength",), None),
    "dexterity": (("dexterity",), None),
}
PAIRS = {
    ("Dawn", "Dusk"): dict(skills=4, cast=20, attack=20, run=20),
    ("Dawn", "Eclipse"): dict(skills=3, damage=60, attack=30, life_leech=8),
    ("Dawn", "Zenith"): dict(skills=3, damage=100, attack=20, attack_rating=300),
    ("Dawn", "Midnight"): dict(skills=3, life=60, resists=20, hit_recovery=30, life_leech=6),
    ("Dawn", "Horizon"): dict(skills=3, run=35, life_leech=6, mana_leech=6),
    ("Dusk", "Eclipse"): dict(skills=3, cast=40, mana=40, mana_regen=30),
    ("Dusk", "Zenith"): dict(skills=3, cast=25, fire=15, cold=15, lightning=15, poison=15, mana=40),
    ("Dusk", "Midnight"): dict(skills=3, cast=20, max_mana=40, hit_recovery=25),
    ("Dusk", "Horizon"): dict(skills=3, cast=25, run=40, mana_regen=50),
    ("Eclipse", "Zenith"): dict(skills=3, attack=25, attack_rating=250, pierce=25),
    ("Eclipse", "Midnight"): dict(skills=3, attack=30, hit_recovery=30, cannot_freeze=1),
    ("Eclipse", "Horizon"): dict(skills=3, attack=25, run=30, magic_find=60),
    ("Zenith", "Midnight"): dict(skills=3, block_rate=30, block_chance=10, strength=15, dexterity=15),
    ("Zenith", "Horizon"): dict(skills=3, cast=25, attack=25, attack_rating=250),
    ("Midnight", "Horizon"): dict(skills=3, run=25, hit_recovery=25, magic_find=80),
}
LOCALES = ("enUS", "zhTW", "deDE", "esES", "frFR", "itIT", "koKR", "plPL",
           "esMX", "jaJP", "ptBR", "ruRU", "zhCN")

SET_NAMES = ("The Unruned Oath", "無符文之誓", "Der Schwur ohne Runen",
             "El juramento sin runas", "Le Serment sans runes", "Il giuramento senza rune",
             "룬 없는 맹세", "Przysięga bez run", "El juramento sin runas",
             "ルーンなき誓い", "O Juramento sem Runas", "Безрунная клятва", "无符文之誓")
DAWN_NAMES = ("Oath of Dawn", "黎明之誓", "Schwur der Morgendämmerung",
              "Juramento del alba", "Serment de l’aube", "Giuramento dell’alba",
              "새벽의 맹세", "Przysięga świtu", "Juramento del alba",
              "暁の誓い", "Juramento da Alvorada", "Клятва рассвета", "黎明之誓")
DUSK_NAMES = ("Oath of Dusk", "黃昏之誓", "Schwur der Abenddämmerung",
              "Juramento del ocaso", "Serment du crépuscule", "Giuramento del crepuscolo",
              "황혼의 맹세", "Przysięga zmierzchu", "Juramento del ocaso",
              "黄昏の誓い", "Juramento do Crepúsculo", "Клятва сумерек", "黄昏之誓")
RING_NAMES = {
    "Dawn": DAWN_NAMES,
    "Dusk": DUSK_NAMES,
    "Eclipse": (
        "Oath of Eclipse", "蝕之誓", "Schwur der Finsternis", "Juramento del eclipse",
        "Serment de l’éclipse", "Giuramento dell’eclissi", "일식의 맹세", "Przysięga zaćmienia",
        "Juramento del eclipse", "蝕の誓い", "Juramento do Eclipse", "Клятва затмения", "蚀之誓",
    ),
    "Zenith": (
        "Oath of Zenith", "天頂之誓", "Schwur des Zenits", "Juramento del cenit",
        "Serment du zénith", "Giuramento dello zenit", "천정의 맹세", "Przysięga zenitu",
        "Juramento del cenit", "天頂の誓い", "Juramento do Zênite", "Клятва зенита", "天顶之誓",
    ),
    "Midnight": (
        "Oath of Midnight", "午夜之誓", "Schwur der Mitternacht", "Juramento de la medianoche",
        "Serment de minuit", "Giuramento della mezzanotte", "한밤의 맹세", "Przysięga północy",
        "Juramento de la medianoche", "真夜中の誓い", "Juramento da Meia-Noite", "Клятва полуночи", "午夜之誓",
    ),
    "Horizon": (
        "Oath of Horizon", "地平線之誓", "Schwur des Horizonts", "Juramento del horizonte",
        "Serment de l’horizon", "Giuramento dell’orizzonte", "지평선의 맹세", "Przysięga horyzontu",
        "Juramento del horizonte", "地平線の誓い", "Juramento do Horizonte", "Клятва горизонта", "地平线之誓",
    ),
}


class Table:
    def __init__(self, path, key_column):
        self.path = path
        self.raw = path.read_bytes()
        self.newline = "\r\n" if b"\r\n" in self.raw else "\n"
        parsed = list(csv.reader(io.StringIO(self.raw.decode("utf-8-sig")),
                                 delimiter="\t", quoting=csv.QUOTE_NONE))
        if not parsed or not parsed[0] or "" in parsed[0] or len(set(parsed[0])) != len(parsed[0]):
            raise ValueError(f"{path.name} has an empty or duplicate column name")
        self.header, *self.rows = parsed
        if key_column not in self.header:
            raise ValueError(f"{path.name} is missing the {key_column} column")
        self.key_index = self.header.index(key_column)
        self.added = []

    def key(self, row):
        return row[self.key_index] if self.key_index < len(row) else ""

    def next_id(self, column):
        i = self.header.index(column)
        return max(int(r[i]) for r in self.rows if len(r) > i and r[i].isdecimal()) + 1

    def count(self):
        return sum(self.key(row) not in ("", "Expansion")
                   for row in self.rows) + len(self.added)

    def add(self, **fields):
        if "*Creator" in self.header:
            fields.setdefault("*Creator", "MadMike")
        missing = sorted(name for name, value in fields.items()
                         if name != "*Creator" and str(value) and name not in self.header)
        if missing:
            raise ValueError(f"{self.path.name} is missing populated columns {', '.join(missing)}")
        row = [str(fields.get(col, "")) for col in self.header]
        key = self.key(row)
        if not key:
            raise ValueError(f"{self.path.name} needs a {self.header[self.key_index]} for the new row")
        if any(self.key(r) == key for r in self.rows + self.added):
            raise ValueError(f"{key} already exists in {self.path.name}")
        self.added.append(row)

    def write(self, path):
        if not self.raw.endswith(b"\n"):
            raise ValueError(f"{self.path.name} does not end in a newline")
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(self.raw + "".join("\t".join(row) + self.newline
                                           for row in self.added).encode("utf-8"))


def add_strings(source, dest, entries):
    raw = source.read_bytes()
    ending = re.search(rb"\]\s*$", raw)
    if not ending:
        raise ValueError(f"Missing array end in {source}")
    ids = {int(x) for x in re.findall(rb'"id"\s*:\s*(\d+)', raw)}
    items = []
    for string_id, key, translations in entries:
        if re.search(rb'"Key"\s*:\s*"' + re.escape(key.encode()) + rb'"', raw):
            raise ValueError(f"Already exists: {key}")
        if string_id in ids:
            raise ValueError(f"String id {string_id} already exists")
        ids.add(string_id)
        items.append(dict(id=string_id, Key=key, **dict(zip(LOCALES, translations, strict=True))))
    nl = b"\r\n" if b"\r\n" in raw else b"\n"
    prefix = raw[:ending.start()].rstrip()
    if not prefix.endswith(b"}") and not prefix.endswith(b"},"):
        raise ValueError(f"Unexpected array tail in {source}")
    additional = json.dumps(items, indent=2, ensure_ascii=False)[2:-2].encode()
    dest.parent.mkdir(parents=True, exist_ok=True)
    dest.write_bytes(prefix + (b"" if prefix.endswith(b",") else b",") + nl
                     + additional.replace(b"\n", nl) + nl + raw[ending.start():])


def build_pairs(stat_table, item_modifiers):
    with stat_table.open(encoding="utf-8-sig", newline="") as source:
        stats = {row["Stat"]: row for row in csv.DictReader(
            source, delimiter="\t", quoting=csv.QUOTE_NONE)}
    labels = {row["Key"]: row for row in json.loads(item_modifiers.read_text(encoding="utf-8-sig"))}
    ring_pairs = list(combinations(RING_NAMES, 2))
    if set(PAIRS) != set(ring_pairs):
        raise ValueError("Expected exactly one bonus for each pair of rings")
    stat_names = [name for names, _ in PAIR_STATS.values() for name in names]
    if len(set(stat_names)) != len(stat_names) or len(stat_names) > MAX_STATS:
        raise ValueError("Duplicate or too many aura stats")
    ordered_stats = sorted((stats[name] for name in stat_names), key=lambda row: int(row["*ID"]))
    if len({stat["*ID"] for stat in ordered_stats}) != len(ordered_stats):
        raise ValueError("Duplicate aura stat ID")
    definitions = [(stat["*ID"], stat["ValShift"] or 0, stat["Stat"]) for stat in ordered_stats]
    pair_values, pair_strings = [], []
    for first, second in ring_pairs:
        bonus = PAIRS[first, second]
        key = KEY + "Pair" + first + second
        lines = [[f"{a} + {b}"] for a, b in zip(RING_NAMES[first], RING_NAMES[second], strict=True)]
        values = {}
        for prop, amount in bonus.items():
            names, label_key = PAIR_STATS[prop]
            label = labels[label_key or stats[names[0]]["descstrpos"]]
            for locale, text in zip(LOCALES, lines, strict=True):
                text.append(label[locale] if prop == "cannot_freeze" else label[locale] % amount)
            for name in names:
                stat = stats[name]
                bits = int(stat["Send Bits"] or 0)
                shift = int(stat["ValShift"] or 0)
                if not 0 <= shift < 31:
                    raise ValueError(f"{name}: unsupported stat shift")
                raw = amount << shift
                value_bits = min(bits - (stat["Signed"] == "1"), 31)
                if (not 0 <= int(stat["*ID"]) < 511 or int(stat["Send Param Bits"] or 0)
                        or value_bits <= 0 or not 0 < raw < (1 << value_bits)):
                    raise ValueError(f"{name}: value does not fit the aura packet")
                values[name] = raw
        pair_values.extend(values.get(stat["Stat"], 0) for stat in ordered_stats)
        pair_strings.append((key, tuple("\n".join(text) for text in lines)))
    return definitions, pair_values, pair_strings


def add_oath_rows(tables, ring_ids, skill_id, state_id, valid_properties=None):
    tables["sets"].add(index=SET, name=SET, version=100, PCode2a="aura", PParam2a=AURA,
                       PMin2a=1, PMax2a=1, **{"*eol": 0})
    for ring, rolls in RING_ROLLS.items():
        name = RING_NAMES[ring][0]
        props = {}
        for i, (prop, low, high) in enumerate(rolls, 1):
            if (valid_properties is not None and prop not in valid_properties) or not 0 < low <= high:
                raise ValueError(f"{name}: invalid property {prop}")
            props.update({f"prop{i}": prop, f"min{i}": low, f"max{i}": high})
        # The native Ctrl-hover renderer skips set items hidden from the Chronicle.
        tables["setitems"].add(index=name, set=SET, spawnable=1,
                              item="rin", rarity=1, lvl=RING_LEVEL, invtransform="lgld",
                              **{"*ID": ring_ids[ring], "*ItemName": "Ring", "lvl req": REQUIRED_LEVELS[ring],
                                 "add func": 0, "cost mult": 5, "cost add": 100, "*eol": 0}, **props)
    tables["states"].add(state=STATE, aura=1, hide=1, **{"*ID": state_id, "*eol": 0})
    # Generic self aura. It owns the temporary list but does not define any
    # modifier itself, so the plugin can explicitly remove its bonus with zero.
    tables["skills"].add(skill=AURA, skilldesc=STATE, srvdofunc=65, aurafilter=1,
                        aurastate=STATE, aurarangecalc=0, range="none", monanim="xx",
                        aura=1, perdelay=10, InTown=1, InGame=1,
                        **{"*Id": skill_id, "*eol": 0})
    tables["skilldesc"].add(skilldesc=STATE,
                           **{"str name": KEY, "str short": KEY + "Effect",
                              "str long": KEY + "Effect", "str alt": KEY, "*eol": 0})


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=Path, help="Unpatched mod directory containing data/")
    parser.add_argument("stage", type=Path, help="Separate output directory for the merged files")
    args = parser.parse_args()
    if args.source.resolve() == args.stage.resolve():
        parser.error("source and stage must be separate directories")
    excel = Path("data/global/excel")
    strings = Path("data/local/lng/strings")
    tables = {name: Table(args.source / excel / (name + ".txt"), key)
              for name, key in TABLE_KEYS.items()}
    ring_id = tables["setitems"].next_id("*ID")
    state_id = tables["states"].next_id("*ID")
    skill_id = tables["skills"].next_id("*Id")
    # The item service indexes the compiled SetItems rows, and the engine
    # indexes stat and skill records. Refuse to assume those IDs on sparse data.
    for table, next_id in (("setitems", ring_id), ("skills", skill_id), ("states", state_id)):
        if tables[table].count() != next_id:
            raise ValueError(f"{table}: IDs and physical rows differ")
    ring_ids = {name: ring_id + i for i, name in enumerate(RING_NAMES)}
    property_table = Table(args.source / excel / "properties.txt", "code")
    valid_properties = {property_table.key(row) for row in property_table.rows}
    add_oath_rows(tables, ring_ids, skill_id, state_id, valid_properties)
    for name, table in tables.items():
        table.write(args.stage / excel / (name + ".txt"))
    used_ids = {int(value) for path in (args.source / strings).glob("*.json")
                for value in re.findall(rb'"id"\s*:\s*(\d+)', path.read_bytes())}
    definitions, pair_values, pair_strings = build_pairs(
        args.source / excel / "itemstatcost.txt", args.source / strings / "item-modifiers.json")
    # Numeric installation data uses a game string so it travels with the
    # compiled tables, in a loose data directory or a packaged mod.
    data = [DATA_FORMAT, ring_id, skill_id, state_id, RING_LEVEL, len(definitions)]
    data.extend(field for stat in definitions for field in stat)
    data.extend(pair_values)
    install_data = " ".join(map(str, data))
    string_id = next(n for n in range(63000, 64996)
                     if not used_ids.intersection(range(n, n + 5)))
    # Keep the five IDs assigned by the two-ring version.
    extra_ids = (i for i in range(string_id + 5, 65000) if i not in used_ids)
    names = list(RING_NAMES.values())
    add_strings(args.source / strings / "item-names.json", args.stage / strings / "item-names.json",
                [(string_id, SET, SET_NAMES),
                 (string_id + 1, names[0][0], names[0]),
                 (string_id + 2, names[1][0], names[1]),
                 *((next(extra_ids), name[0], name) for name in names[2:])])
    add_strings(args.source / strings / "skills.json", args.stage / strings / "skills.json",
                [(string_id + 3, KEY, SET_NAMES), (string_id + 4, KEY + "Effect", SET_NAMES),
                 *((next(extra_ids), key, translations) for key, translations in pair_strings),
                 (next(extra_ids), KEY + "InstallData", (install_data,) * len(LOCALES))])
    print(f"SetItems {ring_id}–{max(ring_ids.values())}, {len(PAIRS)} pairs, skill {skill_id}, state {state_id}")


if __name__ == "__main__":
    main()
