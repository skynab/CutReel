---
name: bump-version
description: Change CutReel's version number (e.g. "bump to 0.8.0", "set the version to 0.7.2", "cut a patch release"). Covers the single source of truth in the root CMakeLists.txt, the comments that quote the current version, and what must be reconfigured afterwards.
---

# Changing the version number

## The one place that matters

The version lives in exactly one spot, the `project()` call at the top of the
root `CMakeLists.txt`:

```cmake
project(CutReel
    VERSION 0.7.1
    ...
```

Change that and everything real follows from it at configure time:

- `cmake/Version.h.in` → generated `zaro/Version.h` (`kVersion`,
  `kVersionMajor/Minor/Patch`), used by the About box (`app/About.cpp`), the
  preview window title (`app/PreviewWindow.cpp`) and the CLI tools'
  `--version` (`tools/Cli.h`).
- `cmake/Packaging.cmake` → `CPACK_PACKAGE_VERSION`, the package file name,
  and the macOS DMG volume name.

Do **not** hardcode a version anywhere else. If new code needs it, include
`<zaro/Version.h>` or use `${PROJECT_VERSION}` in CMake.

## Steps

1. Find the current version: read line ~4 of `CMakeLists.txt`.
2. Edit the `VERSION` in `project(CutReel ...)`. Use plain `MAJOR.MINOR.PATCH`
   — CMake rejects suffixes like `-beta`, and `Version.h.in` expects exactly
   three numeric parts.
3. Update the comments that quote the current version as an example, so they
   don't go stale. Grep for the old string to catch them all:

   ```bash
   git grep -n -F "<old version>" -- ':!build' ':!testdata'
   ```

   As of this writing the hits are:
   - `cmake/Packaging.cmake` — `CutReel-X.Y.Z-Darwin-arm64` and
     `cutreel_X.Y.Z_amd64.deb` in comments
   - `.github/workflows/release.yml` — the `git tag vX.Y.Z` example at the top

   Ignore numeric literals that merely look similar (`0.7` in tests, shaders,
   `testdata/generate.sh`, and `kVersion = "1.9"` in `FinalCutXml.cpp`, which is
   the FCPXML format version, not ours).
4. `vcpkg.json` has its own `"version"` field that has **not** tracked the
   project version (it was `0.1.0` at 0.7.x). Leave it alone unless the user
   asks to sync it; mention it in your summary.
5. Existing build trees keep the old version baked into the generated
   `Version.h` until CMake reconfigures. Rebuilding normally triggers that
   because `CMakeLists.txt` changed; if a stale version shows up, reconfigure
   explicitly (on Windows see the `run-cutreel-windows` skill for the build
   tree and shell setup).

## Things that are not part of a bump

- **Tagging / releasing.** Pushing a `vX.Y.Z` tag triggers
  `.github/workflows/release.yml` and builds a draft GitHub release. Only do
  that when the user explicitly asks — it's outward-facing.
- **Committing.** Don't commit unless asked. If asked, keep the bump in its own
  commit, separate from unrelated work in the tree.
