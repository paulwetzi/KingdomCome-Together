# Copyright (C) 2026 the Kingdom Come: Together contributors (AUTHORS). SPDX-License-Identifier: GPL-3.0-only
# GPLv3 section 7 additional terms: NOTICE. This project's own code only; Kingdom Come: Deliverance II and its
# content belong to Warhorse Studios and PLAION. Unofficial, free, not affiliated with or endorsed by them.
"""Lists female souls that suit a fixed figure, read from the game's own Data/Tables.pak.

Usage: python tools/Find-FixedSoulCandidates.py "<game>/Data/Tables.pak"

A soul qualifies when it is a numbered settlement woman (t???_woman_N), has crime role 1 (no authority), no voice
group, VIP class 0, a generic voice character (not a named quest figure) and no soldier, guard or enemy faction.
These are the rules docs/WO-83-findings.md used for the male roster. Nothing from the game is written to disk.
"""
import re
import sys
import xml.etree.ElementTree as ET
import zipfile

NAME = re.compile(r"^t[a-z]{3}_woman_\d+$")
BAD_FACTION = re.compile(r"soldier|guard|enem|bandit|militia", re.I)


def rows(data):
    return [el.attrib for el in ET.fromstring(data).iter() if el.attrib]


def main(path):
    with zipfile.ZipFile(path) as pak:
        names = {n.lower(): n for n in pak.namelist()}
        classes = names.get("libs/tables/rpg/social_class.xml")
        if classes is None:
            raise SystemExit("social_class.xml is not in this pak: pass the game's Data/Tables.pak")
        crime_role = {r["social_class_id"]: r.get("soul_crime_role_id")
                      for r in rows(pak.read(classes)) if "social_class_id" in r}
        class_name = {r["social_class_id"]: r.get("social_class_name", "?")
                      for r in rows(pak.read(classes)) if "social_class_id" in r}
        found = []
        for low, real in sorted(names.items()):
            if not re.fullmatch(r"libs/tables/rpg/soul__.*\.xml", low):
                continue
            for r in rows(pak.read(real)):
                soul = r.get("soul_name", "")
                if not NAME.match(soul):
                    continue
                cid = r.get("social_class_id", "")
                if (crime_role.get(cid) == "1" and not r.get("voice_group_name")
                        and r.get("soul_vip_class_id", "0") == "0"
                        and r.get("skald_character_name", "").startswith("char_GENERIC_")
                        and not BAD_FACTION.search(r.get("factionName", ""))):
                    found.append((soul, r.get("soul_id", ""), class_name.get(cid, "?"), r.get("factionName", "")))
    for soul, guid, cls, faction in sorted(found):
        print(f"{soul:18} {guid}  {cls:14} {faction}")
    print(f"{len(found)} candidate(s)")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit(__doc__)
    main(sys.argv[1])
