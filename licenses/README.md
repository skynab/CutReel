# Licence texts

The full text of every licence CutReel's binaries are distributed under or
depend on. `../THIRD-PARTY.md` is the index: it says which component is under
which of these, who holds the copyright, and why the combined work is GPL-3.0.
This directory is only the texts themselves.

| File | Applies to |
|---|---|
| `GPL-3.0.txt` | **The licence the CutReel binaries are conveyed under.** Also Qt's LGPL-3.0 supplements it |
| `LGPL-3.0.txt` | Qt 6 — read together with `GPL-3.0.txt`, which it modifies |
| `GPL-2.0.txt` | The version x264, x265 and GPL-enabled FFmpeg name. All three are "or later", which is what allows the GPL-3.0 above |
| `FFmpeg.md` | FFmpeg's own statement of which of its parts are under what |
| `SDL2.txt` | SDL2 (Zlib licence, with its copyright notice) |
| `nlohmann-json.txt` | nlohmann/json (MIT, with its copyright notice) |
| `zlib.txt` | zlib (Zlib licence, with its copyright notice) |

CutReel's own source code is Apache-2.0; that text is `../LICENSE`, not here,
because it is the licence of this repository rather than of something vendored
into it.

Every file here is a verbatim copy of the text the upstream project ships. They
are copied in rather than fetched at build time so that a package can be built,
and its licence obligations checked, without a network.
