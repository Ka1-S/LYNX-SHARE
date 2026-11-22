# Third-party components

LYNX is distributed under the [GNU AGPL-3.0](LICENSE). It uses the components below, each under its own license:

The full licence texts and NOTICE files travel with every package in the `licenses` folder next to the program (made by
`scripts/collect-licenses.sh` from the files named below).

| Component | Used for | License |
|---|---|---|
| [JUCE 8](https://juce.com) | UI, audio, VST3 plugin hosting | AGPLv3 (or a commercial JUCE license) |
| [CLAP](https://github.com/free-audio/clap) (headers in `third_party/clap`) | CLAP plugin hosting | MIT |
| [WavPack](https://www.wavpack.com) (decoder in `third_party/wavpack`) | reading `.wv` samples | 3-clause BSD (`third_party/wavpack/COPYING`) |
| [libogg 1.3.5 and libvorbis 1.3.7](https://xiph.org/vorbis/) (decoder in `third_party/vorbis`; `src/mdct.c` modified, see the "LYNX modification" comment) | decoding Ogg Vorbis samples the way FL Studio does | 3-clause BSD (`third_party/vorbis/COPYING.libogg`, `third_party/vorbis/COPYING.libvorbis`) |
| [LAME 3.100](https://lame.sourceforge.io) (`libmp3lame` encoder sources in `third_party/lame`, unmodified; `src/config.h` written for LYNX's build) | MP3 export | LGPL-2.0-or-later (`third_party/lame/COPYING`) |
| [TinySoundFont 0.9](https://github.com/schellingb/TinySoundFont) (`third_party/tinysoundfont/tsf.h`; LYNX modification: reverb / chorus send generators kept, see the header comment) | parsing SoundFont 2 / 3 files for the LYNX Soundfont instrument (LYNX plays the voices itself) | MIT (`third_party/tinysoundfont/LICENSE`) |
| File-format references for other samplers' instruments (round 190ii; **no code from these projects is compiled in or shipped**; LYNX's readers in `src/core/GigFile.cpp`, `src/core/Ncw.cpp`, `src/core/KontaktFile.cpp` are its own): [libgig 4.4.1](https://www.linuxsampler.org/libgig/) by Christian Schoenebeck (the GigaStudio `.gig` layout on top of the MMA's DLS Level 2 specification), [ncw](https://github.com/monomadic/ncw) `FORMAT.md` (Native Instruments' NCW sample compression), [ConvertWithMoss](https://github.com/git-moss/ConvertWithMoss) by Jürgen Moßgraber (telling Kontakt instrument versions and protected libraries apart) | importing `.gig` instruments, decoding `.ncw` samples, recognising `.nki` files (protected Kontakt libraries are never decrypted) | libgig: GPL-2.0-or-later (its source headers), compatible with the AGPL-3.0 through the GPLv3 terms its "or any later version" allows; ncw: MIT OR Apache-2.0; ConvertWithMoss: LGPL-3.0 |
| [stb_vorbis 1.22](https://github.com/nothings/stb) (`third_party/stb_vorbis/stb_vorbis.c`, unmodified) | decoding the Ogg Vorbis samples of SoundFont 3 files | MIT (option of its MIT / public domain licence, `third_party/stb_vorbis/LICENSE`) |
| [minimp3](https://github.com/lieff/minimp3) (`minimp3.h`, `minimp3_ex.h` in `third_party/minimp3`; one marked "LYNX" change in `minimp3.h`: damaged granules are silenced, see its README) | decoding `.mp3` samples and audio clips (gapless) | CC0 1.0 public domain dedication (`third_party/minimp3/LICENSE`) |
| [sse2neon](https://github.com/DLTcollab/sse2neon) (`third_party/sse2neon/sse2neon.h`, unmodified) | the SSE intrinsics of LYNX's DSP (`src/core/Simd.h`) on ARM64 builds (Apple silicon) | MIT (`third_party/sse2neon/LICENSE`) |
| [Signalsmith Stretch 1.4](https://github.com/Signalsmith-Audio/signalsmith-stretch) and [Signalsmith Linear 0.6.6](https://github.com/Signalsmith-Audio/linear) (`third_party/signalsmith`, unmodified) | pitch-preserving time-stretch (tempo-following samples, FL Studio stretch modes, Newtone, the audio editor) | MIT (`third_party/signalsmith/LICENSE-stretch.txt`, `LICENSE-linear.txt`) |
| [pocketpy 2.2.0](https://github.com/pocketpy/pocketpy) (the single-file `pocketpy.c` / `pocketpy.h` amalgamation in `third_party/pocketpy`; LYNX's changes are marked "LYNX": an interrupt flag for scripts that run too long, eager generator expressions, line continuation after a backslash) | the embedded Python that runs FL Studio's VFX Script note scripts (built without its `os` / file modules) | MIT (`third_party/pocketpy/LICENSE`) |
| [libxmp-lite 4.7.4](https://github.com/libxmp/libxmp) by Claudio Matsuoka, Hipolito Carraro Jr and the libxmp contributors (`third_party/libxmp-lite`: the release's `src` and `include`, unmodified; LYNX's read-only accessors added in `src/lynx_xmp_extra.c` / `.h`; details in `third_party/libxmp-lite/README-LYNX.md`) | reading ProTracker .mod, FastTracker 2 .xm, Scream Tracker 3 .s3m and Impulse Tracker .it modules for File › Open / Import (`src/core/ModuleImport.cpp`): their patterns, instruments and samples, and the song's order, speed and tempo as libxmp plays it | MIT (`third_party/libxmp-lite/LICENSE`); its IT sample decompressor (`src/loaders/itsex.c`) and `src/md5.c` are public domain |
| [ysfx 0.0.47](https://github.com/JoepVanlier/ysfx) by Jean Pierre Cimalando and Joep Vanlier (the library's `include` and `sources` in `third_party/ysfx`, commit 5c3452fe; its JUCE plugin, @gfx drawing (LICE / SWELL), tests and tools are left out; one LYNX change, marked "LYNX" and listed in `third_party/ysfx/README-LYNX.md`: a host filter for the files scripts may read) with what it bundles: Cockos' [WDL](https://www.cockos.com/wdl/) EEL2 compiler and FFT (`thirdparty/WDL`, unmodified), the GAS port of EEL2's x86-64 code generator (`sources/eel2-gas`, unmodified) and [dr_libs](https://github.com/mackron/dr_libs) `dr_wav.h` / `dr_flac.h` (unmodified) | running JSFX effect and instrument scripts (REAPER's text-based plugin format) in mixer slots and on channels (`src/engine/JsfxFormat.cpp`, round 190pj, issue #241) | ysfx: Apache-2.0 (`third_party/ysfx/LICENSE`); WDL / EEL2: zlib-style Cockos WDL licence (`third_party/ysfx/thirdparty/WDL/LICENSE.txt`), its Bison-made parser `eel2/y.tab.c` GPL-2.0-or-later with the Bison special exception (in the file); eel2-gas: zlib (`third_party/ysfx/sources/eel2-gas/LICENSE.txt`); dr_libs: public domain (Unlicense) or MIT-0, at the user's choice (`third_party/ysfx/thirdparty/dr_libs/LICENSE`) |
| [chokehold JSFX](https://github.com/chkhld/jsfx) by chokehold (commit f6d175d8; the 31 scripts without an @gfx display, unmodified, in `resources/jsfx/chokehold`; embedded in every build by `tools/make_jsfx_pack.py` and written to the settings folder's "JSFX pack") | the bundled JSFX pack: clippers, dynamics, EQ, delay, lo-fi, stereo and utility effects, a test signal generator and a MIDI chord trigger | MIT (`resources/jsfx/chokehold/LICENSE.txt`, also `resources/licenses/jsfx-chokehold-LICENSE.txt`) |
| [Monocypher 4.0.2](https://monocypher.org) (`third_party/monocypher/monocypher.c` / `.h`, unmodified) | the end-to-end encryption of live sessions: Argon2id (the invite code's keys), BLAKE2b, XChaCha20-Poly1305 | 2-clause BSD or CC0 1.0, at the user's choice (`third_party/monocypher/LICENCE.md`) |
| [Faust Libraries](https://github.com/grame-cncm/faustlibraries) `reverbs.lib` 1.6.2: the function `re.valhallaSupermassive` by David Braun, ported to C++ in `src/core/NebulaCloud.h` / `.cpp` (no Faust code or runtime is shipped; LYNX's changes: block processing, glided controls, single-precision signal path) | the six "cloud" networks of the Nebula effect (its Measured model) | MIT (declared for that function in the file; notice below) |
| [PipeWire 1.6.2](https://gitlab.freedesktop.org/pipewire/pipewire) headers (the `pipewire-0.3` / `spa-0.2` headers LYNX includes, in `third_party/pipewire/include`, unmodified) | LYNX's native PipeWire audio driver on Linux; `libpipewire-0.3.so.0` itself is loaded from the system at run time, never shipped | MIT (`third_party/pipewire/LICENSE`) |
| [Ableton Link 4.1.1](https://github.com/Ableton/link) by Ableton AG (headers in `third_party/link/include`, unmodified) with the standalone [Asio 1.38.2](https://think-async.com/Asio/) it bundles (`third_party/link/asio/include`, unmodified) | Ableton Link: tempo, beat and phase shared with other apps and devices on the network (`src/engine/AbletonLink.cpp`) | Link: GPL-2.0-or-later (`third_party/link/LICENSE.md`, `GNU-GPL-v2.0.md`), used under the GPLv3 terms its "or any later version" allows, which are compatible with the AGPL-3.0; Asio: Boost Software License 1.0 (`third_party/link/asio/LICENSE_1_0.txt`) |
| [VST 3 SDK](https://github.com/steinbergmedia/vst3sdk) (the copy bundled with JUCE 8.0.4) | VST3 plugin hosting | GPLv3 option of Steinberg's dual licence (`VST3_SDK/LICENSE.txt` in JUCE) |
| [ARA SDK 2.1.0](https://github.com/Celemony/ARA_SDK) by Celemony Software GmbH (`ARA_API` headers and the `ARA_Library` Debug / Dispatch / Utilities sources in `third_party/ara`, unmodified; tag `releases/2.1.0`, ARA_API f0af6d7, ARA_Library c3c580d) | hosting ARA 2 plugins (Melodyne, RipX, VocAlign, SpectraLayers…) on audio clips (`src/engine/AraHost.cpp`, through JUCE's ARA hosting classes) | Apache-2.0 (`third_party/ara/ARA_API/LICENSE.txt`, `third_party/ara/ARA_Library/LICENSE.txt`, `third_party/ara/NOTICE.txt`) |
| [ASIO SDK 2.3.4](https://www.steinberg.net/developers/) (interface headers in `third_party/asiosdk`) | ASIO audio drivers on Windows | GPLv3 option of Steinberg's dual licence (`third_party/asiosdk/LICENSE.txt`) |
| [ONNX Runtime 1.23.2](https://github.com/microsoft/onnxruntime) (C API headers in `third_party/onnxruntime`, unmodified; Microsoft's release library ships next to LYNX and is loaded at run time) | running the stem separation model of "Extract stems from sample" and the Basic Pitch model of "Convert to MIDI" | MIT (`third_party/onnxruntime/LICENSE`; Microsoft's notices for the code inside the library: `third_party/onnxruntime/ThirdPartyNotices.txt`, from the 1.23.2 release) |
| [Basic Pitch](https://github.com/spotify/basic-pitch) by Spotify (the ICASSP 2022 model `nmp.onnx` from commit fa5997a, unmodified, compiled in as `third_party/basic-pitch/nmp.onnx.inc`; its note-creation rules and pitch-bend reading (`note_creation.py` output_to_notes_polyphonic, get_pitch_bends) ported to C++ in `src/core/AudioToMidi.cpp`) | "Convert to MIDI": melody and harmony of audio clips, and "Convert to MIDI (polyphonic)..." (round 190lj) | Apache-2.0, code and model weights (`third_party/basic-pitch/LICENSE`, `NOTICE`; text below) |
| [PitchNet](https://github.com/SessionLoops/PitchNet)'s polyphonic engine by Session Loops and contributors (a derivative of [HachiTune](https://github.com/KCKT0112/HachiTune) by the HachiTune contributors): `Source/Audio/Polyphonic` of commit 121b330 (2026-11-17) in `third_party/pitchnet-poly`, without its ONNX Runtime wrapper (`BasicPitchEvidence`); LYNX modifications marked "LYNX modification (round 190lk / 190lo)" and listed in `third_party/pitchnet-poly/README-LYNX.md` (masks built on demand, `PolyLazy.cpp` added; slow re-attacks; overlap-aware moves). Its note-filter trees (`PolyPurifierModel.h`, `PolyGuidedPurifierModel.h`) were trained by PitchNet on features of recordings from [Salamander Grand Piano V3](https://github.com/sfzinstruments/SalamanderGrandPiano) by Alexander Holm (CC BY 3.0) and [VSCO 2 Community Edition](https://github.com/sgossner/VSCO-2-CE) by Versilian Studios and Ivy Audio (CC0 1.0); no audio of those libraries is included | "Edit notes (polyphonic)..." (round 190lk): finding the notes of chords and polyphonic audio clips and rendering per-note edits (`src/core/PolyEdit.cpp`, `src/ui/PolyEditor.cpp`) | AGPL-3.0 (`third_party/pitchnet-poly/LICENSE`); the trees' source recordings: CC BY 3.0 (Salamander, attribution above) and CC0 1.0 (VSCO 2 CE) |
| Libraries bundled inside JUCE's modules | text shaping, image and audio codecs, zip | HarfBuzz (MIT), SheenBidi (Apache-2.0), FLAC (BSD-3-Clause), Ogg/Vorbis (BSD-3-Clause), libpng (libpng/zlib licence), IJG libjpeg (IJG licence), zlib (zlib licence) |
| [Open Sans](https://github.com/googlefonts/opensans) (`resources/fonts`, unmodified; Linux builds only) | the interface font on Linux | SIL Open Font License 1.1 (`resources/fonts/OFL.txt`, full text below) |
| [Open Sans Condensed](https://github.com/googlefonts/opensans) SemiBold and Bold (the same Open Sans project's static `OpenSans-CondensedSemiBold.ttf` / `OpenSans-CondensedBold.ttf`, `resources/fonts`, unmodified; embedded in every build as base64 in `src/ui/CondensedFontData.h` by `tools/make_font_data.py`) | Settings › General › "Condensed labels (FL style)" (off by default) | SIL Open Font License 1.1 (`resources/fonts/OFL.txt`, the same licence and copyright line as Open Sans, full text below) |

The HT-Demucs model is not part of LYNX: on first use of "Extract stems from sample" LYNX shows its source and licence
and downloads it only if you agree. The Demucs code is MIT; its authors state that the licence of the trained weights
is not clear. On Windows, ONNX Runtime uses the Microsoft Visual C++ Redistributable already on the computer; LYNX does
not ship it.

M4A (AAC) export uses the AAC encoder of the operating system: Media Foundation's on Windows, AudioToolbox's on macOS.
LYNX ships no AAC encoder (no FDK-AAC, no FFmpeg): the codec and its patent licensing come with the system that provides
it. Where there is no system encoder (Linux, Windows "N" editions without the Media Feature Pack), M4A is not offered.

Every component is available under an OSI-approved open-source licence, and LYNX uses each of them under that
licence; no proprietary or closed-source code is compiled into or shipped with LYNX. Where Steinberg offers a
proprietary licence as an alternative (VST 3 SDK, ASIO SDK), LYNX takes the GPLv3 option, which is compatible with
the AGPL-3.0. The release binaries are built from this repository by
[`.github/workflows/release.yml`](.github/workflows/release.yml); the build downloads JUCE 8.0.4 and Zig 0.14.1
from their official sources and checks their SHA-256.

VST and ASIO are trademarks of Steinberg Media Technologies GmbH. Ableton and Ableton Link are trademarks of Ableton AG;
LYNX is not affiliated with Ableton. FL Studio is a registered trademark of
Image-Line Software; LYNX is not affiliated with Image-Line.

## Basic Pitch (Apache-2.0)

LYNX compiles in Basic Pitch's ONNX model (`basic_pitch/saved_models/icassp_2022/nmp.onnx`, 230 444 bytes, SHA-256
2c3c1d144bfa61ad236e92e169c13535c880469a12a047d4e73451f2c059a0ec) and ports its note creation (`note_creation.py`) to
C++. The repository publishes the code and the trained weights under the Apache License 2.0
(https://www.apache.org/licenses/LICENSE-2.0; full text in `third_party/basic-pitch/LICENSE`). Its NOTICE file:

```
Basic Pitch
Copyright 2022 Spotify AB

This product includes software developed at
Spotify AB (http://www.spotify.com/).
```

(The rest of Basic Pitch's NOTICE names Python libraries its own package uses — librosa, mir_eval, numpy, pretty-midi,
resampy, scipy, tensorflow — and test audio from the Vocadito dataset; none of them is part of LYNX.)

## ARA SDK (Apache-2.0)

LYNX includes the `ARA_API` headers and the `ARA_Library` Debug / Dispatch / Utilities sources of Celemony's ARA Audio
Random Access SDK 2.1.0, unmodified, under the Apache License 2.0 (https://www.apache.org/licenses/LICENSE-2.0; full
text in `third_party/ara/ARA_API/LICENSE.txt` and `third_party/ara/ARA_Library/LICENSE.txt`). The parts of the SDK's
NOTICE file that apply to what LYNX includes (`third_party/ara/NOTICE.txt` has all of it; the SDK's examples, its
bundled cpp-base64, pugixml, ICST AudioFile, VST3 SDK subset, CoreAudio Utility Classes and JUCE_ARA fork are not part of
LYNX):

```
This module bundles the ARA API, ARA Library and ARA Examples
by Celemony Software GmbH as ARA Audio Random Access SDK,
available under the Apache License 2.0.

THIS SOFTWARE IS PROVIDED BY CELEMONY SOFTWARE GMBH AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF NON-INFRINGEMENT, MERCHANTABILITY AND FITNESS FOR A
PARTICULAR PURPOSE ARE DISCLAIMED.
IN NO EVENT SHALL CELEMONY SOFTWARE GMBH AND/OR CONTRIBUTORS BE LIABLE FOR
ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```

ARA is a trademark of Celemony Software GmbH; LYNX is not affiliated with Celemony. Melodyne is a trademark of
Celemony Software GmbH.

## Faust Libraries `re.valhallaSupermassive` (MIT)

`src/core/NebulaCloud.h` and `src/core/NebulaCloud.cpp` are a C++ port of the function `valhallaSupermassive` of
`reverbs.lib` (version 1.6.2) in https://github.com/grame-cncm/faustlibraries — a model of six delay / reverb networks
(feedback delay networks and cascades of matrix all-passes). The library file declares, for this function,
`author "David Braun"` and `license "MIT"`; the other functions of that file, under other licences, are not used.

```
MIT License

Copyright (c) 2026 David Braun

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

(The library names the author and the licence but carries no copyright line for this function; the line above is
written from that declaration and the year of the contribution.)

## sse2neon (MIT)

`third_party/sse2neon/sse2neon.h` v1.8.0 — https://github.com/DLTcollab/sse2neon. Translates the SSE intrinsics of the
exact FL engines to NEON in the Apple Silicon (arm64) build. Copyright (c) 2015-2024 SSE2NEON Contributors; MIT
licence in `third_party/sse2neon/LICENSE`.

## Open Sans: SIL Open Font License 1.1

```
Copyright 2020 The Open Sans Project Authors (https://github.com/googlefonts/opensans)

-----------------------------------------------------------
SIL OPEN FONT LICENSE Version 1.1 - 26 February 2007
-----------------------------------------------------------

PREAMBLE
The goals of the Open Font License (OFL) are to stimulate worldwide
development of collaborative font projects, to support the font
creation efforts of academic and linguistic communities, and to
provide a free and open framework in which fonts may be shared and
improved in partnership with others.

The OFL allows the licensed fonts to be used, studied, modified and
redistributed freely as long as they are not sold by themselves. The
fonts, including any derivative works, can be bundled, embedded,
redistributed and/or sold with any software provided that any reserved
names are not used by derivative works. The fonts and derivatives,
however, cannot be released under any other type of license. The
requirement for fonts to remain under this license does not apply to
any document created using the fonts or their derivatives.

DEFINITIONS
"Font Software" refers to the set of files released by the Copyright
Holder(s) under this license and clearly marked as such. This may
include source files, build scripts and documentation.

"Reserved Font Name" refers to any names specified as such after the
copyright statement(s).

"Original Version" refers to the collection of Font Software
components as distributed by the Copyright Holder(s).

"Modified Version" refers to any derivative made by adding to,
deleting, or substituting -- in part or in whole -- any of the
components of the Original Version, by changing formats or by porting
the Font Software to a new environment.

"Author" refers to any designer, engineer, programmer, technical
writer or other person who contributed to the Font Software.

PERMISSION & CONDITIONS
Permission is hereby granted, free of charge, to any person obtaining
a copy of the Font Software, to use, study, copy, merge, embed,
modify, redistribute, and sell modified and unmodified copies of the
Font Software, subject to the following conditions:

1) Neither the Font Software nor any of its individual components, in
Original or Modified Versions, may be sold by itself.

2) Original or Modified Versions of the Font Software may be bundled,
redistributed and/or sold with any software, provided that each copy
contains the above copyright notice and this license. These can be
included either as stand-alone text files, human-readable headers or
in the appropriate machine-readable metadata fields within text or
binary files as long as those fields can be easily viewed by the user.

3) No Modified Version of the Font Software may use the Reserved Font
Name(s) unless explicit written permission is granted by the
corresponding Copyright Holder. This restriction only applies to the
primary font name as presented to the users.

4) The name(s) of the Copyright Holder(s) or the Author(s) of the Font
Software shall not be used to promote, endorse or advertise any
Modified Version, except to acknowledge the contribution(s) of the
Copyright Holder(s) and the Author(s) or with their explicit written
permission.

5) The Font Software, modified or unmodified, in part or in whole,
must be distributed entirely under this license, and must not be
distributed under any other license. The requirement for fonts to
remain under this license does not apply to any document created using
the Font Software.

TERMINATION
This license becomes null and void if any of the above conditions are
not met.

DISCLAIMER
THE FONT SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO ANY WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT
OF COPYRIGHT, PATENT, TRADEMARK, OR OTHER RIGHT. IN NO EVENT SHALL THE
COPYRIGHT HOLDER BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
INCLUDING ANY GENERAL, SPECIAL, INDIRECT, INCIDENTAL, OR CONSEQUENTIAL
DAMAGES, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
FROM, OUT OF THE USE OR INABILITY TO USE THE FONT SOFTWARE OR FROM
OTHER DEALINGS IN THE FONT SOFTWARE.
```
