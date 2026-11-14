# LYNX Remote Control, version 1

LYNX Remote Control is a local API for editors, controllers and producer agents.
It is independent of any particular client. It uses authenticated IPv4 loopback
TCP and newline-delimited UTF-8 JSON. It does not expose scripting or a network
listener on other interfaces.

Enable **Settings → General → Remote control**. It is off by default, and the
application displays its active status. Disabling it stops the listener and
removes its discovery record. Automated launches can explicitly set
`LYNX_CONTROL_ENABLED=1`; setting a discovery path alone does not enable access.
The ordinary update preference and automatic update check are unchanged.

## Discovery and authentication

The default record is `control/connection.json` inside LYNX's settings folder:

| Platform | Default location |
| --- | --- |
| Windows | `%APPDATA%\LYNX\control\connection.json` |
| macOS | `~/Library/Application Support/LYNX/control/connection.json` |
| Linux | `$XDG_CONFIG_HOME/LYNX/control/connection.json`, normally `~/.config/LYNX/control/connection.json` |

Store-packaged Windows builds use their redirected application-data folder.
`LYNX_CONTROL_DISCOVERY` can select an absolute record path. Test profiles use
`$LYNX_PROFILE_DIR/LYNX/control/connection.json`.

The record contains `protocol: "lynx-control/1"`, `host: "127.0.0.1"`, an ephemeral
`port`, `pid`, `appVersion`, and a random 64-character lower-case hexadecimal
`token`. Treat the token as a credential. Never put it in a command argument,
log, model prompt or request. The record is atomically published, owner-only
(POSIX mode `0600`; a private current-user DACL on Windows), and rotated when the
listener starts. Clients must reject shared, symlink/reparse and incompatible
records. Bind discovery to the expected process when reconnecting.

Each connection performs this handshake before an operation:

1. Client sends `{"type":"hello","protocol":"lynx-control/1","nonce":C}`.
2. Server returns `{"type":"challenge","protocol":"lynx-control/1","nonce":S,"proof":P}`.
3. Client verifies the server proof, then sends `{"type":"auth","proof":Q}`.
4. Server returns `{"type":"ready","protocol":"lynx-control/1"}`.

`C` and `S` are independent random 32-character lower-case hexadecimal nonces.
The key is the token's ASCII text, not its hex-decoded bytes. Proofs are lower-case
HMAC-SHA-256 hex:

```text
P = HMAC(token, "lynx-control/1\nserver\n" + C + "\n" + S)
Q = HMAC(token, "lynx-control/1\nclient\n" + C + "\n" + S)
```

There is no final newline in the signed message. Compare proofs in constant
time. A client must verify `P` before sending `Q` or an operation. Handshake frames
are limited to 1 KiB and depth 8 before JSON parsing. Operation frames are limited
to 1 MiB and depth 64. The listener limits handshake time and validates canonical
UTF-8 and nesting before conversion or recursive parsing. Malformed, overlong,
surrogate and out-of-range encodings are rejected. It does not accept older or
client-specific protocols.

## Requests, results and edit guards

After authentication, send one `{"op":"status","args":{}}` line. The connection
returns one result and closes:

```json
{"ok":true,"result":{"projectId":"…","controlEpoch":"…","revision":42}}
```

Every successful operation includes the current `projectId`, process-lifetime
`controlEpoch` and monotonic `revision`. Keep the complete triple. Persistent
edits and history operations require `expectedProjectId`, `expectedEpoch` and
`expectedRevision` from the observation used to plan them. A newer revision
does not authorize an index copied from an older layout. Read again after a
structural change. Remote changes are refused during recording or a held mouse
gesture; structural, device, document and history operations also require stopped
playback. Each accepted persistent request is one native undo step. A no-op
document transaction creates no step and does not advance the revision.

Errors have this form:

```json
{"ok":false,"error":"…","code":"changed","uncertain":false}
```

Codes include `invalid`, `busy`, `changed`, `limit`, `failed` and `uncertain`.
An uncertain write may have committed. Inspect the project before deciding what
to do; never automatically repeat it. Loss of a write response has the same
ambiguity even when no structured error arrives. Authentication failures disclose
no credential.

## Observe the project

- `api.describe` lists this connection's supported operations, capabilities,
  protocol and frame limits.
- `status` returns bounded project identity, revision, tempo, transport state,
  position, current pattern and collection counts without copying the project.
- `changes {fromRevision,limit?}` returns up to 128 observed revision changes and
  `gap`/`hasMore`. It is a bounded polling feed, not a push subscription or an
  exhaustive edit log. Several UI changes between observations may coalesce.
  A gap requires a fresh project read. Do not infer an undo lineage from revisions.
- `session.inspect` returns a bounded overview. Large projects use `project.read`.
- `pattern.notes`, `plugin.inspect`, `instrument.catalog`, `effect.catalog` and
  `playlist.inspect` provide narrower paginated views. Hosted parameter values
  use normalized `0..1` values. Catalog IDs are opaque exact strings.
- `device.state` returns `deviceStateHash` covering current saved state of
  channels, mixer devices and nested devices. It captures live vendor state and
  requires stopped playback/recording. A vendor GUI may change opaque state
  without a native revision; use document/device hashes in addition to revisions.

Every `pattern.notes` object contains `start`, `length`, `channel`, `key`,
`velocity`, `pan`, `muted`, `finePitch`, `slide`, `porta`, `release`, `modX`,
`modY`, `colour`, `group`, `chance`, `velocityRange`, `chanceGroup` and
`chanceOne`. `pattern.replace_notes` requires all these fields, including
default values, and rejects unsupported fields. Read the complete notes before
replacing them so an edit retains expression it does not intend to change.
`chance` is a probability in `0..1`, `velocityRange` is in `-1..1`,
`chanceGroup` is a nonnegative integer, and `chanceOne` is a boolean. A true
`chanceOne` flag is retained even when its chance group is currently zero.

## Typed pattern operations

The `patterns` capability advertises `pattern.create`, `pattern.clone` and
`pattern.set`. Each requires the normal project/process/revision guards and
stopped playback/recording, and commits one remote-owned native undo step.

- `pattern.create`: optional `index` is an insertion position from zero through
  the pattern count; omit it to append. Optional `name` defaults to the next
  unused `Pattern N`. Optional `lengthTicks` defaults to zero (automatic).
- `pattern.clone`: required `index` names an existing source. The complete copy
  is inserted immediately after it; optional `name` replaces the copy's default
  name. Notes and all expression fields, events, markers, loops and colour are
  retained; the new pattern gets a fresh identity.
- `pattern.set`: required `index` plus `name`, `lengthTicks` or both. Omitted
  metadata is preserved. A no-op does not advance revision or add history.

Names contain 1–256 characters, include non-whitespace text and have no control
characters. `lengthTicks` is an integer
from zero through 1,000,000,000 at PPQ 960; zero follows the notes automatically.
A positive length cannot end before an existing note. Creation/cloning select
the new pattern. Playlist clips, Pattern Blocks across arrangements and channel
Loop Starter references keep their source targets as indices shift. Length
changes resize untrimmed full-pattern clips as in LYNX's UI; trimmed clips keep
their lengths. Results include `index`, `patternId`, `name`, `lengthTicks`,
`effectiveLengthTicks`, `patternCount` and `changed`, plus the common identity
metadata. Session pattern rows expose explicit/effective length and colour.
Refresh after each committed indexed edit. Pattern deletion is not a typed
operation; a valid document transaction can edit the full native model subject
to its reference and identity rules.

## Read a complete project document

`project.read` exposes every field of LYNX's native versioned project format,
including channels, native synthesis data, hosted and nested device state,
patterns/notes/expression, routing, automation and all arrangements. It uses the
same native JSON field schema as `.lynx` projects, with full model numeric
precision instead of the compact numbers used for ordinary saved files.
Hosted binary state and some native engine state remain opaque encoded data;
readable hosted parameter information comes from `plugin.inspect`.

First request:

```json
{"op":"project.read","args":{"limit":32768}}
```

Result fields are `snapshotId`, `documentHash` (SHA-256), `encoding: "base64"`,
`data`, `offset`, `totalBytes`, optional `nextOffset`, and the identity/revision
triple. Decode each `data` chunk to bytes, concatenate them, verify the total size
and hash, then decode UTF-8 JSON. A byte chunk may end within a UTF-8 character.
Do not parse individual chunks or treat a page as the complete project.

Continuation:

```json
{"op":"project.read","args":{"snapshotId":"…","expectedDocumentHash":"…","offset":32768,"limit":32768}}
```

Pages come from an immutable snapshot, never a mixture of reads. Limits are
1–65,536 bytes per page (default 32,768), 64 MiB per complete document, four
retained snapshots and 64 MiB total retained document bytes. New reads can evict
old snapshots. A changed project/revision or an expired/wrong snapshot is refused;
restart the read. Full snapshots require stopped playback and recording and do
not enter history or update project working time.

## Atomically edit project data

`project.edit` accepts a snapshot ID/hash and 1–128 JSON Patch operations. Supported
RFC 6902 operations are `test`, `add`, `replace` and `remove`; paths use RFC 6901
JSON Pointer escaping (`~0` for `~`, `~1` for `/`). It stages every operation on a
copy, validates the complete result, then commits once:

```json
{
  "op":"project.edit",
  "args":{
    "expectedProjectId":"…","expectedEpoch":"…","expectedRevision":42,
    "snapshotId":"…","expectedDocumentHash":"…",
    "patch":[
      {"op":"test","path":"/bpm","value":130},
      {"op":"replace","path":"/bpm","value":142},
      {"op":"replace","path":"/patterns/0/name","value":"Verse"}
    ]
  }
}
```

The hash is checked against a fresh exact snapshot, including live vendor state,
before any undo step is created. The result includes `changed`, `operationCount`,
the resulting `documentHash` and identity/revision. Failed validation leaves the
project and history unchanged. Changed transactions invalidate old snapshots.

The document is a low-level native-format interface. Arrays and references are
literal: when inserting/removing/reordering a collection, update its dependent
indices in the same transaction. The server rejects dangling references and
cyclic mixer routing; it cannot infer the musical intent of valid index changes.
Prefer typed operations for common structural actions because those perform their
native reference remapping.

Unknown fields, invalid types, unsupported values, silently clamped/discarded
content and duplicate identities are refused by a native-format round trip.
Optional properties use their canonical native representation; remove an
optional property to restore a default the writer omits. New entities omit their
`uid` and receive a fresh native ID. Existing IDs must stay unique. Project format,
project lineage/counter and collaboration identity are read-only. Explicitly
unlock a content-locked Playlist row before changing its clips. Transactions are
limited to depth 64, four million JSON nodes, one million items per array, 100,000
channels/patterns/automations, 501 mixer tracks and 500 Playlist rows.

Typed operations include tempo and channel/mixer level changes, catalog-based
instrument insertion/replacement, pattern creation/cloning/name/length changes,
note replacement, device parameter batches,
effect loading/removal and Playlist editing. `api.describe` is authoritative for
the running build; additional typed operations can be added without replacing
the document API. Separate typed requests are separate undo steps. Document
transactions change project data, not settings, filesystem contents, plugin
scans, saving or rendering.

Document fields are explicitly allowlisted, including nested objects and
collections. Replacing a parent object, collection or the root does not bypass
the field policy. Project title, author, genre and comments are editable;
`info.url`, `info.showOnOpen`, creation time and accumulated work time are
read-only. Every changed object must contain only reviewed fields, including
members that were left unchanged; wholly unchanged subtrees are preserved.
New native save-format fields therefore require an API policy review before
their containing object can be edited.

Document edits can reuse only asset references already declared in the project,
with the exact existing spelling. They cannot introduce new sample/zone paths,
native engine data/DLL paths, effect sources, stand-in files, plugin loader IDs
or foreign-plugin locators. This check also covers nested Patcher nodes, bus-head
effects and parallel effect branches, and applies to new entities as well as
existing ones. New UNC, device and relative-path aliases are not normalized into
an allowlisted resource. Import new assets in LYNX first; use the catalog-based
typed operations to load new instruments or effects. The project data/output
folder and each mixer's recording output destination remain unchanged through
document transactions. Asset import and output-path configuration need separate
explicit operations.

Known native encoded state is inspected too: Video Player video paths, Visualizer
image paths, HTML NoteBook page/launch addresses, and decoded native sample/wave
resource references must retain a resource already declared in that same role.
A sample reference cannot become a new page-launch address. VFX Script payloads
may only preserve/reuse an exact pre-existing payload; document edits cannot add
or change script code, including inside Patcher. Hosted `plugin.state` and
`substituteFor.state` are read-only at the existing device identity, including
inside parallel branches, nested Patcher nodes and bus heads. Document edits
cannot supply new nonempty blobs or copy them into another device slot. Channel
UIDs preserve state when channels are reordered; nested devices and effects
without UIDs retain their container-slot identity. Use typed parameter operations
or the plugin editor to change a hosted device, and explicit catalog replacement
when a fresh device instance is intended.

The known native formats above are checked before a commit. Opaque hosted device
state is not a portable parameter schema and remains read-only in `project.edit`.
Restoring existing state through native history or the plugin editor calls the
corresponding device inside LYNX; that device may perform its own filesystem or network I/O. Remote
Control is not a sandbox for plugins or state blobs. Use only trusted clients and
trusted device state even when explicit path fields are restricted.

`transport.play` uses the current Song/Pattern mode. Authenticated
`transport.stop` stops playback/recording even after the project triple has gone
stale. `history.undo` and `history.redo` require current guards; optional
`expectedDeviceStateHash` refuses a restore after unrelated opaque device-state
changes. These are native history operations, not selective per-client undo.
Clients must establish ownership of the intended top step before issuing them.
Device-state refusal checks apply only to these API history calls. Local
undo/redo and History-window jumps retain normal native behavior after device
changes, including while Remote Control is off.

## Independent example client

[`examples/remote_control.py`](../examples/remote_control.py) uses only Python's
standard library. It validates private discovery, verifies the server first and
never retries writes. With remote control enabled:

```sh
python3 examples/remote_control.py status
python3 examples/remote_control.py describe
python3 examples/remote_control.py read --output project.json --metadata snapshot.json
# Write a JSON Patch array to patch.json using the complete observed project.
python3 examples/remote_control.py edit --snapshot snapshot.json --patch patch.json
```

The default command is read-only status. `read` refuses to overwrite output files.
Its metadata file contains the exact edit fence, not authentication material.
`edit` sends that explicitly planned write once; stale or expired fences require
another complete read and a new plan. Use `--discovery /absolute/path` for custom
or Store-packaged locations. Enabling remote control gives authenticated clients
the same project-editing authority as the local producer; keep discovery private.
