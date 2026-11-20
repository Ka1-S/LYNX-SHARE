#!/usr/bin/env python3
"""fetch_vcsl.py - downloads the CC0 reference notes LYNX Model is calibrated on (round 190ni) and writes their manifest.

  fetch_vcsl.py <tree.json> <LICENSE> <out-dir> <commit>

<tree.json>: https://api.github.com/repos/sgossner/VCSL/git/trees/<commit>?recursive=1 ; <LICENSE>: the repository's
LICENSE at that commit (read it: CC0 1.0 Universal). Fetches the six mallet folders below at the pinned commit from
raw.githubusercontent.com into <out-dir>/<set>/, checks each file's git blob SHA-1 against the tree (a file that does
not match fails the run) and writes <out-dir>/manifest.json: per file its path, size, blob SHA-1 and SHA-256, plus the
commit, the licence and the licence file's SHA-256. calibrate.py copies those records into each table's provenance.
Keep the downloads out of the repository: only the fitted parameters ship.
"""
import hashlib, json, os, sys, urllib.parse, urllib.request
tree, licence, out, commit = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4]
licence_text = open(licence, 'rb').read()
if b'CC0 1.0 Universal' not in licence_text:
    raise SystemExit('the LICENSE is not CC0 1.0: stop (the data policy is CC0 only)')
SETS = {
    'marimba': 'Idiophones/Struck Idiophones/Marimba/',
    'vibraphone': 'Idiophones/Struck Idiophones/Vibraphone/Soft Mallets/',
    'vibraphone-hard': 'Idiophones/Struck Idiophones/Vibraphone/Hard Mallets/',
    'xylophone': 'Idiophones/Struck Idiophones/Xylophone/Medium Mallets/',
    'xylophone-hard': 'Idiophones/Struck Idiophones/Xylophone/Hard Mallets/',
    'glockenspiel': 'Idiophones/Struck Idiophones/Glockenspiel/',
    'tubularbells': 'Idiophones/Struck Idiophones/Tubular Bells 2/',
    'kalimba': 'Idiophones/Plucked Idiophones/Kalimba, Tanzania/',
}
t = json.load(open(tree))
manifest = []
for name, prefix in SETS.items():
    d = os.path.join(out, name)
    os.makedirs(d, exist_ok=True)
    for e in t['tree']:
        p = e['path']
        if e['type'] != 'blob' or not p.startswith(prefix) or '/' in p[len(prefix):] or not p.lower().endswith('.wav'):
            continue
        dst = os.path.join(d, p.split('/')[-1])
        if not os.path.exists(dst) or os.path.getsize(dst) != e['size']:
            url = 'https://raw.githubusercontent.com/sgossner/VCSL/%s/%s' % (commit, urllib.parse.quote(p))
            data = urllib.request.urlopen(url, timeout=120).read()
            open(dst, 'wb').write(data)
        data = open(dst, 'rb').read()
        blob = hashlib.sha1(b'blob %d\0' % len(data) + data).hexdigest()
        ok = blob == e['sha']
        if not ok:
            raise SystemExit('blob SHA-1 mismatch: %s' % p)
        manifest.append({'set': name, 'path': p, 'bytes': len(data), 'git_blob_sha1': e['sha'], 'verified': ok,
                         'sha256': hashlib.sha256(data).hexdigest()})
        print(name, ok, p.split('/')[-1], flush=True)
json.dump({'source': 'https://github.com/sgossner/VCSL', 'commit': commit, 'licence': 'CC0 1.0 Universal (repository LICENSE)',
           'licence_sha256': hashlib.sha256(licence_text).hexdigest(),
           'licence_check': 'Repository LICENSE at commit %s: Creative Commons CC0 1.0 Universal; README: "This collection is under a '
                            'Creative Commons 0 license"; no other licence or notice file in the instrument folders' % commit,
           'files': manifest}, open(os.path.join(out, 'manifest.json'), 'w'), indent=1)
print('files', len(manifest), 'all verified', all(m['verified'] for m in manifest))
