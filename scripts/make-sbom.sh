#!/usr/bin/env bash
# Writes an SPDX 2.3 JSON software bill of materials of LYNX SHARE's third-party components (round 190nr, 0.1.31).
#
#   scripts/make-sbom.sh <version label, e.g. 0.1.31-alpha> <output .spdx.json> [licenses folder]
#
# Sources: the component table of THIRD_PARTY.md (name, link, version, licence) and the licence texts the packages
# carry in licenses/ (scripts/collect-licenses.sh; run into a temporary folder when no folder is given). The SPDX
# licence expression, version and third_party/ folder of each row come from the COMPONENTS table below, keyed by the
# row's first link text: a THIRD_PARTY.md row without an entry there, or a licences folder entry that no component
# claims, stops the script, so the SBOM can't silently fall behind THIRD_PARTY.md. Plain bash + python3.
set -euo pipefail
LABEL="${1:?usage: scripts/make-sbom.sh <version label> <output .spdx.json> [licenses folder]}"
OUT="${2:?usage: scripts/make-sbom.sh <version label> <output .spdx.json> [licenses folder]}"
LIC="${3:-}"
case "$OUT" in /*) ;; *) OUT="$PWD/$OUT" ;; esac
case "$LIC" in /*|"") ;; *) LIC="$PWD/$LIC" ;; esac
cd "$(dirname "$0")/.."
TMP=""
if [ -z "$LIC" ]; then
    TMP="$(mktemp -d)"; trap 'rm -rf "$TMP"' EXIT
    scripts/collect-licenses.sh "$TMP/licenses" > /dev/null
    LIC="$TMP/licenses"
fi
COMMIT="$(git rev-parse HEAD 2>/dev/null || echo unknown)"
python3 - "$LABEL" "$OUT" "$LIC" "$COMMIT" <<'PY'
import datetime, json, os, re, sys, uuid

label, out, lic_dir, commit = sys.argv[1:5]

# key (first link text of the THIRD_PARTY.md row, or the row's plain text start) ->
#   version (None: the one in the key), SPDX licence LYNX uses it under (concluded), declared licence,
#   third_party/ folders whose licence files belong to it, shipped (False: reference only, not in the SBOM).
NOA = "NOASSERTION"
COMPONENTS = {
    "JUCE 8": dict(version="8.0.4", concluded="AGPL-3.0-only", declared="AGPL-3.0-only OR LicenseRef-JUCE-Commercial",
                   folders=[]),
    "CLAP": dict(version="1.2.2", concluded="MIT", folders=["clap"]),
    "WavPack": dict(version="5.7.0", concluded="BSD-3-Clause", folders=["wavpack"]),
    "libogg 1.3.5 and libvorbis 1.3.7": dict(split=[("libogg", "1.3.5"), ("libvorbis", "1.3.7")],
                                             concluded="BSD-3-Clause", folders=["vorbis"]),
    "LAME 3.100": dict(concluded="LGPL-2.0-or-later", folders=["lame"]),
    "TinySoundFont 0.9": dict(concluded="MIT", folders=["tinysoundfont"]),
    "File-format references": dict(shipped=False),
    "stb_vorbis 1.22": dict(concluded="MIT", declared="MIT OR Unlicense", folders=["stb_vorbis"]),
    "minimp3": dict(version="ea99364f61c14656440e8d77e9c233ccf3124633", concluded="CC0-1.0", folders=["minimp3"]),
    "Signalsmith Stretch 1.4": dict(concluded="MIT", folders=["signalsmith"]),
    "Signalsmith Linear 0.6.6": dict(concluded="MIT", folders=["signalsmith"]),
    "pocketpy 2.2.0": dict(concluded="MIT", folders=["pocketpy"]),
    "Monocypher 4.0.2": dict(concluded="BSD-2-Clause", declared="BSD-2-Clause OR CC0-1.0", folders=["monocypher"]),
    "Faust Libraries": dict(version="1.6.2", name="Faust Libraries reverbs.lib (re.valhallaSupermassive, ported)",
                            concluded="MIT", folders=[]),
    "PipeWire 1.6.2": dict(name="PipeWire headers", concluded="MIT", folders=["pipewire"]),
    "Ableton Link 4.1.1": dict(concluded="GPL-2.0-or-later", folders=["link"], exclude=["link-asio"]),
    "Asio 1.38.2": dict(concluded="BSL-1.0", folders=["link-asio"]),
    "VST 3 SDK": dict(version="3.7.12 (bundled with JUCE 8.0.4)", concluded="GPL-3.0-only",
                      declared="GPL-3.0-only OR LicenseRef-Steinberg-Proprietary", folders=[], extra=["GPL-3.0.txt"]),
    "ARA SDK 2.1.0": dict(concluded="Apache-2.0", folders=["ara"]),
    "ASIO SDK 2.3.4": dict(concluded="GPL-3.0-only", declared="GPL-3.0-only OR LicenseRef-Steinberg-Proprietary",
                           folders=["asiosdk"], extra=["GPL-3.0.txt"]),
    "ONNX Runtime 1.23.2": dict(concluded="MIT", folders=["onnxruntime"]),
    "Basic Pitch": dict(version="fa5997a", concluded="Apache-2.0", folders=["basic-pitch"]),
    "PitchNet": dict(version="121b330", name="PitchNet polyphonic engine", concluded="AGPL-3.0-only",
                     folders=["pitchnet-poly"]),
    "Libraries bundled inside JUCE's modules": dict(split_libs=[
        ("HarfBuzz", "MIT", "https://github.com/harfbuzz/harfbuzz"),
        ("SheenBidi", "Apache-2.0", "https://github.com/Tehreer/SheenBidi"),
        ("FLAC", "BSD-3-Clause", "https://xiph.org/flac/"),
        ("Ogg/Vorbis (JUCE copy)", "BSD-3-Clause", "https://xiph.org/vorbis/"),
        ("libpng", "Libpng", "http://www.libpng.org/pub/png/libpng.html"),
        ("IJG libjpeg", "IJG", "https://ijg.org"),
        ("zlib", "Zlib", "https://zlib.net")]),
    "sse2neon": dict(version=NOA, concluded="MIT", folders=["sse2neon"]),
    # round 190qd (0.1.31): the tracker module reader, the JSFX host and the bundled JSFX pack
    "libxmp-lite 4.7.4": dict(concluded="MIT", folders=["libxmp-lite"]),
    "ysfx 0.0.47": dict(name="ysfx (with Cockos WDL / EEL2, the EEL2 GAS port and dr_libs)",
                        concluded="Apache-2.0 AND Zlib AND (Unlicense OR MIT-0)", folders=["ysfx"]),
    "chokehold JSFX": dict(version="f6d175d8", concluded="MIT", folders=["jsfx-chokehold"]),
    "Open Sans": dict(concluded="OFL-1.1", folders=[], extra=["open-sans-OFL.txt"]),
    "Open Sans Condensed": dict(concluded="OFL-1.1", folders=[], extra=["open-sans-OFL.txt"]),
}
# Components that have a licence file in licenses/ but no row of their own in THIRD_PARTY.md's table.
EXTRA_ROWS = {}
LICENSE_REFS = {
    "LicenseRef-JUCE-Commercial": ("JUCE commercial licence",
        "JUCE 8 is also offered under a commercial licence by Raw Material Software; LYNX uses JUCE under the AGPL-3.0."),
    "LicenseRef-Steinberg-Proprietary": ("Steinberg proprietary SDK licence",
        "Steinberg offers the VST 3 and ASIO SDKs under a proprietary licence or the GPLv3; LYNX takes the GPLv3 option."),
}

rows = []
for line in open("THIRD_PARTY.md", encoding="utf-8"):
    if not line.startswith("| ") or line.startswith("| Component") or line.startswith("|---"):
        continue
    cells = [c.strip() for c in line.strip().strip("|").split(" | ")]
    rows.append(cells)
if not rows:
    sys.exit("make-sbom: no component table in THIRD_PARTY.md")

def spdx_id(name):
    return "SPDXRef-" + re.sub(r"[^A-Za-z0-9.-]+", "-", name).strip("-")

packages, claimed, errors = [], set(), []
lic_files = sorted(f for f in os.listdir(lic_dir) if f != "README.txt")

def files_for(spec):
    names = []
    for folder in spec.get("folders", []):
        for f in lic_files:
            if f.startswith(folder + "-") and not any(f.startswith(x + "-") for x in spec.get("exclude", [])):
                names.append(f)
    names += [f for f in spec.get("extra", []) if f in lic_files]
    claimed.update(names)
    return names

def add(name, version, url, concluded, declared, used_for, licence_files):
    comment = "Used for: " + re.sub(r"\s+", " ", re.sub(r"[`*]", "", used_for)).strip()
    if licence_files:
        comment += ". Licence texts in the package's licenses folder: " + ", ".join(licence_files)
    packages.append({
        "name": name, "SPDXID": spdx_id(name), "versionInfo": version or NOA,
        "downloadLocation": url or NOA, "filesAnalyzed": False,
        "licenseConcluded": concluded, "licenseDeclared": declared or concluded,
        "copyrightText": NOA, "supplier": NOA, "comment": comment,
    })

for cells in rows:
    first, used_for = cells[0], cells[1] if len(cells) > 1 else ""
    links = re.findall(r"\[([^\]]+)\]\(([^)]+)\)", first)
    key = links[0][0] if links else first
    spec = COMPONENTS.get(key) or next((v for k, v in COMPONENTS.items() if first.startswith(k)), None)
    if spec is None:
        errors.append(f"THIRD_PARTY.md row '{first[:60]}' has no entry in scripts/make-sbom.sh COMPONENTS")
        continue
    if spec.get("shipped") is False:
        continue
    if "split_libs" in spec:
        for name, lic, url in spec["split_libs"]:
            add(name, "bundled with JUCE 8.0.4", url, lic, None, used_for, [])
        continue
    # one row can name several projects ([Signalsmith Stretch 1.4](..) and [Signalsmith Linear 0.6.6](..), Link + Asio)
    for text, url in (links or [(key, None)]):
        spec2 = COMPONENTS.get(text)
        if spec2 is None:
            if text == key:
                spec2 = spec
            else:
                continue          # a link inside the description (a source library, a dataset): not a component
        if "split" in spec2:
            files = files_for(spec2)
            for name, ver in spec2["split"]:
                add(name, ver, url, spec2["concluded"], spec2.get("declared"), used_for,
                    [f for f in files if ("copying." + name) in f.lower()] or files)
            continue
        m = re.match(r"^(.*?)\s+(\d+(?:\.\d+)+)$", text)
        name = spec2.get("name") or (m.group(1) if m else text)
        version = spec2.get("version") or (m.group(2) if m else None)
        add(name, version, url, spec2["concluded"], spec2.get("declared"), used_for, files_for(spec2))

for key, spec in EXTRA_ROWS.items():
    add(key, spec["version"], spec["url"], spec["concluded"], None, spec["used_for"], files_for(spec))

for f in lic_files:
    if f not in claimed:
        errors.append(f"licences folder file {f} belongs to no SBOM component (add it to THIRD_PARTY.md / make-sbom.sh)")
if errors:
    sys.exit("make-sbom:\n  " + "\n  ".join(errors))

ids = [p["SPDXID"] for p in packages]
if len(ids) != len(set(ids)):
    sys.exit("make-sbom: duplicate SPDX ids: " + ", ".join(sorted({i for i in ids if ids.count(i) > 1})))

root = {
    "name": "LYNX SHARE", "SPDXID": "SPDXRef-LYNX-SHARE", "versionInfo": label,
    "downloadLocation": f"git+https://github.com/Ka1-S/LYNX-SHARE@{commit}" if commit != "unknown"
                        else "https://github.com/Ka1-S/LYNX-SHARE",
    "homepage": "https://lynx-share.com", "filesAnalyzed": False,
    "licenseConcluded": "AGPL-3.0-only", "licenseDeclared": "AGPL-3.0-only",
    "copyrightText": NOA, "supplier": "Person: Ka1-S",
    "primaryPackagePurpose": "APPLICATION",
}
used_refs = sorted({r for p in packages for r in re.findall(r"LicenseRef-[A-Za-z0-9.-]+", p["licenseDeclared"])})
doc = {
    "spdxVersion": "SPDX-2.3",
    "dataLicense": "CC0-1.0",
    "SPDXID": "SPDXRef-DOCUMENT",
    "name": f"LYNX-SHARE-{label}",
    "documentNamespace": f"https://github.com/Ka1-S/LYNX-SHARE/releases/v{label}/sbom-{uuid.uuid4()}",
    "creationInfo": {
        "created": datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
        "creators": ["Tool: LYNX-SHARE scripts/make-sbom.sh", "Organization: LYNX SHARE"],
        "comment": "Third-party components of LYNX SHARE, from THIRD_PARTY.md and the packages' licenses folder.",
    },
    "documentDescribes": ["SPDXRef-LYNX-SHARE"],
    "packages": [root] + packages,
    "relationships": [{"spdxElementId": "SPDXRef-DOCUMENT", "relationshipType": "DESCRIBES",
                       "relatedSpdxElement": "SPDXRef-LYNX-SHARE"}] +
                     [{"spdxElementId": "SPDXRef-LYNX-SHARE", "relationshipType": "CONTAINS",
                       "relatedSpdxElement": p["SPDXID"]} for p in packages],
}
if used_refs:
    doc["hasExtractedLicensingInfos"] = [{"licenseId": r, "name": LICENSE_REFS[r][0], "extractedText": LICENSE_REFS[r][1]}
                                         for r in used_refs]
with open(out, "w", encoding="utf-8") as fh:
    json.dump(doc, fh, indent=2, ensure_ascii=False)
    fh.write("\n")
print(f"make-sbom: {len(packages)} components -> {out}")
PY
