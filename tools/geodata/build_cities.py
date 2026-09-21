#!/usr/bin/env python3
#
# SPDX-FileCopyrightText: 2026 One Small Step Apps Ltd
#
# SPDX-License-Identifier: MIT
#
"""Build assets/geodata/cities.tsv, the offline city list behind the Location
search (ADS-B / AIS home position).

Source: GeoNames cities15000 (every city with >= 15,000 people), licensed
CC BY 4.0 (https://www.geonames.org/). One line per city, most populous first,
so a search can stop at the first matches:

    name<TAB>country<TAB>lat<TAB>lon

`name` is GeoNames' ASCII name (renders with the app fonts), lat/lon rounded to
3 decimals (~100 m, far below the map precision).

    python3 tools/geodata/build_cities.py            # download + build
"""
import argparse
import io
import sys
import urllib.request
import zipfile
from datetime import date

URL = "https://download.geonames.org/export/dump/cities15000.zip"


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--out", default="assets/geodata/cities.tsv")
    ap.add_argument("--url", default=URL)
    args = ap.parse_args()

    with urllib.request.urlopen(args.url, timeout=120) as r:
        data = r.read()
    with zipfile.ZipFile(io.BytesIO(data)) as z:
        text = z.read("cities15000.txt").decode("utf-8")

    rows = []
    for line in text.splitlines():
        f = line.split("\t")
        if len(f) < 15:
            continue
        name = f[2].strip() or f[1].strip()
        if not name or "\t" in name:
            continue
        rows.append((int(f[14] or 0), name, f[8], float(f[4]), float(f[5])))
    rows.sort(key=lambda r: -r[0])

    with open(args.out, "w", encoding="ascii", errors="ignore", newline="\n") as out:
        out.write(f"# GeoNames cities15000, CC BY 4.0, geonames.org, built {date.today()}\n")
        for _, name, cc, lat, lon in rows:
            out.write(f"{name}\t{cc}\t{lat:.3f}\t{lon:.3f}\n")
    print(f"wrote {args.out}: {len(rows)} cities", file=sys.stderr)


if __name__ == "__main__":
    main()
