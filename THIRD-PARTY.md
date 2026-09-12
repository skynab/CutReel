# Third-party software in CutReel

CutReel is built from other people's work as well as its own. This file says
whose, under what terms, and where to get it. It is the attribution notice that
ships with every binary, and it is also the honest answer to "what am I actually
installing?"

## The licence position, in short

**CutReel's own source code is Apache-2.0.** That is the licence on this
repository, it is the licence contributions are accepted under, and it does not
change.

**The binaries — the `.dmg`, the `.deb`, the installer `.exe` — are distributed
under GPL-3.0-or-later.** They have to be. They link x264, x265 and a
GPL-enabled build of FFmpeg, whose authors allow that only if the combined work
is offered under the GPL, and GPL-3.0 is the version that every component here
can agree on:

- Apache-2.0 is incompatible with GPL-2.0 but compatible with GPL-3.0.
- Qt 6 is LGPL-3.0, which cannot be combined into a GPL-2.0-only work.
- x264, x265 and FFmpeg are all *GPL-2.0-or-later*, and the "or later" is what
  lets the combined work be GPL-3.0.

So the source stays permissive and the binary is copyleft. This is a normal
arrangement, not a contradiction: the Apache-2.0 grant on our code is what makes
the GPL-3.0 binary possible, and anyone who wants CutReel's code under Apache-2.0
terms can take it from the repository without touching the GPL components at all.

## Getting the source

GPL-3.0 §6(d) requires that anyone given a binary can get the complete
corresponding source for it. Every release satisfies this from the same page it
is downloaded from:

- **CutReel itself** — <https://github.com/skynab/CutReel>, at the tag the
  release was built from.
- **The dependencies** — each project's own source, at the versions listed
  below. Where a build pins them, the pin is in `vcpkg.json` and the CI
  workflow in `.github/workflows/ci.yml`.

## What ships inside the binaries

Versions are those resolved for the Windows build, which pins its dependencies
through vcpkg. macOS and Linux take FFmpeg, SDL2 and Qt from Homebrew and the
distribution respectively, so their versions are whatever those ship.

| Component | Version | Licence | What it does here |
|---|---|---|---|
| [FFmpeg](https://ffmpeg.org) (GPL build) | 9.0.1 | GPL-2.0-or-later | Decoding, encoding, muxing, resampling, scaling |
| [x264](https://www.videolan.org/developers/x264.html) | 0.165.3222 | GPL-2.0-or-later | H.264 encoding, and every proxy |
| [x265](https://www.x265.org) | 4.3 | GPL-2.0-or-later | HEVC encoding |
| [Qt](https://www.qt.io) (Widgets, Gui, ShaderTools) | 6.9.3 | LGPL-3.0-only | The window, and QRhi under the GPU compositor |
| [SDL2](https://www.libsdl.org) | 2.32.10 | Zlib | The audio output device |
| [nlohmann/json](https://github.com/nlohmann/json) | 3.12.0 | MIT | Reading and writing `.cutreel` project files |
| [zlib](https://zlib.net) | 1.3.2 | Zlib | What PNG is made of, inside FFmpeg |

### Attribution

- **FFmpeg** — Copyright (c) the FFmpeg developers. FFmpeg is LGPL-2.1-or-later
  by default; CutReel builds it with `--enable-gpl`, which makes the result
  GPL-2.0-or-later. That flag is asked for deliberately — see the comment in
  `vcpkg.json` — because x264 and x265 require it.
- **x264** — Copyright (c) VideoLAN and the x264 project authors. A commercial
  licence is available from VideoLAN for anyone who needs one.
- **x265** — Copyright (c) MulticoreWare, Inc. A commercial licence is available
  from MulticoreWare.
- **Qt** — Copyright (c) The Qt Company Ltd and other contributors. Used under
  LGPL-3.0. Qt is linked dynamically; because the whole work is conveyed under
  GPL-3.0 with complete source, LGPL-3.0 §4's relinking requirement is satisfied
  by that source rather than by a separate object-file offer.
- **SDL2** — Copyright (C) 1997-2025 Sam Lantinga.
- **nlohmann/json** — Copyright (c) 2013-2025 Niels Lohmann.
- **zlib** — Copyright (C) 1995-2026 Jean-loup Gailly and Mark Adler.

Each project's own full licence text is the authoritative one, and each ships it
in its own `COPYING` or `LICENSE` file alongside the source linked above. The
full texts are not yet reproduced inside the installed packages; doing so is
part of the packaging work this notice is the first half of.

## Used to build and test, but not shipped

These are not part of any binary handed to anyone, so they impose no obligation
on a release. They are listed because "what does this repository depend on" is a
different and equally reasonable question.

| Component | Licence | What it does here |
|---|---|---|
| [Catch2](https://github.com/catchorg/Catch2) | BSL-1.0 | The test framework, all three suites |
| [vcpkg](https://github.com/microsoft/vcpkg) | MIT | Dependency acquisition on Windows |
| [pkgconf](https://github.com/pkgconf/pkgconf) | ISC | Finding FFmpeg and SDL2 at configure time |
| [WiX Toolset](https://wixtoolset.org) | MS-RL | Building the Windows installer |

## Patents

Copyright licensing and patent licensing are different things, and the GPL
settles only the first. H.264 and HEVC are covered by patent pools (MPEG LA and
Access Advance respectively) whose terms are not affected by anything in this
file. Distributing an encoder for either format may carry obligations
independent of the source licence. This is stated here so that it is not
mistaken for something the licence position above resolves; it is not legal
advice, and anyone redistributing CutReel commercially should take their own.
