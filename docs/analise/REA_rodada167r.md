# Round 167r — Online Sessions (design only)

LYNX-own feature requested by the user (pt-BR: "quero que fique fácil… pro cara se conectar no meu lynx e gravar a
parte dele, com os plugins dele, sem atrapalhar no projeto… tipo a gente fazer uma sessão online"). Deliverable:
`docs/design/ONLINE_SESSIONS.md`. No product code in this round.

| Reporter's words | FL evidence | LYNX before | LYNX now |
|---|---|---|---|
| online session: host + friend, each records with his own plugins | FL 2026 has no live collaboration (below) | none | design document; nothing implemented |

## FL evidence: no collaboration feature in FL Studio 2026

Static strings check of FL's own binaries (no FL launch, no plugin scan), `C:\Program Files\Image-Line\FL Studio 2026`:

```
for f in FL64.exe FLEngine_x64.dll FLMManaged.dll; do (strings -n 5 "$FL/$f"; strings -n 5 -el "$FL/$f") > $f.str; done
grep -i -E "collab|invite|jam session|online session|multiplayer|co-?produc|peer|webrtc|stun:|turn:|rendezvous|join session"
```

- `FL64.exe` 3 251 strings, `FLEngine_x64.dll` 237 135, `FLMManaged.dll` 79 376.
- `FLEngine_x64.dll`: `collab` 0 hits, `invite` 0, `webrtc` / `stun` / `ice candidate` / `sdp` 0 (2 false positives:
  `IsProcessDPIAware`, `GetProcessDpiAwareness`).
- `FLMManaged.dll`: "invite" / "peer" hits are all Microsoft Graph SDK types (`DriveItemInviteRequest`,
  `InviteParticipantsOperationRequest`, `GetSkypeForBusinessPeerToPeerActivityCounts`…) bundled with the managed
  helper, not FL features.
- FL's network features found instead: FL Cloud (`ToolsFLCloudAccountActionExecute`, `ToolsFLCloudPluginsActionExecute`,
  `ToolsFLCloudMasteringActionExecute`, `ToolsFLCloudDistributionActionExecute`, `UploadSamplesToCloudActionExecute`,
  `FileSaveAndBackupToCloudActionExecute`, `TCloudStorageBackupsFolderNodeHelper`, `ProjectFLCloudBackupBox`),
  `TSoundCloudUploader`, FL Studio Remote / Image-Line Remote (`ILRemoteServer`, "Enable connecting to the FL Studio
  Remote app on iOS and Android").

Conclusion: FL Cloud = sounds, plugins, mastering, distribution and project backups; Remote = a phone controller.
No multi-user project session → Online Sessions are LYNX-own.

## Where I stopped

- Done: design written (`docs/design/ONLINE_SESSIONS.md`), grounded in `AudioEngine::editSong` / `publishSong` /
  undo stacks, `Song.h` entities and index references, `ProjectFile` JSON, `ProjectPackage`, `ExportJob`,
  `TakeRecording`, `PluginHost::findForeign`, `website/api/_common.php`, `PRIVACY.md`, `docs/STORE.md`.
- Next step: the user answers the open questions (§10 of the design); then phase 1 (uids + entity diff/merge +
  stand-ins + Send project / part packages).
