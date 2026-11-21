# libxmp-lite in LYNX SHARE

- Version: libxmp-lite 4.7.4, from the official release `https://github.com/libxmp/libxmp/releases/download/libxmp-4.7.4/libxmp-lite-4.7.4.tar.gz`
  (sha256 `d4b6108b6e8c7ecc106403bc4bab28fb46686867a7b5247567371ad557634c12`), retrieved 2026-11-20 (round 190nw).
- What is here: the release's `src/` (with `src/loaders/`: the MOD, XM, S3M and IT loaders) and `include/`, and its `README`
  (the licence is in it; `LICENSE` is a copy of that section). The build files and the Makefiles were left out; LYNX builds
  the sources itself (CMakeLists.txt, scripts/build-local.sh, scripts/build-windows-zig.sh) with `LIBXMP_CORE_PLAYER`
  and `LIBXMP_STATIC`.
- LYNX's addition (no libxmp file is modified): `src/lynx_xmp_extra.c` / `.h`, read-only accessors of the module
  data libxmp keeps in its internal context (a sample's c5spd, the exact frame time, the sustain loop; round 190ob:
  whether a channel's sample has played to its end; round 190om: the pitch, sample and position a channel's mixer voice plays, and the player's mode),
  used by `src/core/ModuleImport.cpp`.
- Licence: MIT (`LICENSE`), the whole library since libxmp 4.6.1; `src/loaders/itsex.c` (the IT sample decompressor) is
  public domain by Olivier Lapicque; `src/md5.c` is public domain. Compatible with LYNX's AGPL-3.0.
