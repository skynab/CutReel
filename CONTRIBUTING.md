# Contributing to CutReel

## The licence your contribution is under

**Contributions are accepted under the Apache License 2.0** — the same licence
as the rest of this repository's source. By opening a pull request you are
offering your change under those terms.

That is worth one paragraph of explanation, because the binaries CutReel
releases are GPL-3.0 and the two facts together look contradictory. They are
not. The repository's source is Apache-2.0; the *binary* picks up GPL-3.0
because it links x264, x265 and a GPL build of FFmpeg. Apache-2.0 is one-way
compatible with GPL-3.0, which is what makes that arrangement legal, and it is
why inbound contributions have to be Apache-2.0 rather than GPL: an Apache-2.0
patch can go into the GPL-3.0 binary, but a GPL-3.0 patch could not go back into
the Apache-2.0 source. `THIRD-PARTY.md` sets out the whole position.

**You keep the copyright in what you write.** This is a licence you grant, not
an assignment. Nobody is asking you to sign your work over.

### Signing off

Certify the grant with a `Signed-off-by` line in each commit, which
`git commit -s` adds for you:

```
Signed-off-by: Your Name <you@example.com>
```

This is the [Developer Certificate of Origin](https://developercertificate.org)
1.1: it says you wrote the change, or have the right to submit it under
Apache-2.0. It is one line and no paperwork, and it is what lets the project
keep its licensing options open — including a commercially-licensed x264 build
later, which becomes impossible to arrange the moment the source has
contributions the project cannot relicense.

If a change is not yours to give — code from another project, or work your
employer owns — say so in the pull request rather than signing it off. There is
usually a way to take it; there is no way to take it back.

## Before you open a pull request

CI runs all of this, so running it first is the difference between one round
trip and three.

```
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
```

- **Warnings are errors.** A build that is quiet has passed.
- **`clang-format` is a CI gate**, pinned to version 22.1.8. Run
  `clang-format -i` on every file you touched; a rename that lengthens an
  identifier can push a line past the 100-column limit in a file you never
  opened.
- **The GUI suite needs a display and a working QRhi.**
  `QT_QPA_PLATFORM=offscreen` is not one — that plugin has no QRhi and a
  `QRhiWidget` on it never draws. On a headless Linux box use `xvfb-run`, which
  is what CI does.
- **Media fixtures are generated, not committed.** Run `./testdata/generate.sh`
  once; without it the media tests skip rather than fail, which is a quieter
  failure than it sounds.

## What the code expects of you

The layout is in the README and the reasoning is in `docs/PLAN.md`; the
architecture decisions that are already settled are in `docs/adr/`, and a pull
request that reverses one is welcome but should say so and say why.

Two conventions carry more weight than the rest:

- **`core/` links neither Qt nor FFmpeg**, and that is not an accident of how it
  grew. It is what makes the edit engine testable in CI, scriptable, and
  renderable without a window server. A change that needs a Qt type in `core/`
  is a change that needs an interface in `core/` and an implementation in
  `platform/`.
- **Commands are the only write path into the model.** If something mutates a
  `Project` outside a `Command`, undo does not cover it, and the operation that
  escaped undo is the one somebody loses an afternoon to.

Comments here tend to explain *why* rather than *what*, especially where the
obvious approach was tried and abandoned. Matching that is more useful than
matching the formatting, which `clang-format` handles anyway.

## Reporting a bug

Say what you did, what happened, and what you expected. For anything involving
playback or export, `cutreel-probe <file>` on the media is worth pasting: a
surprising number of playback reports are a file that is not what it looks like.

Platform matters more than usual in this program. A playback bug that reproduces
on one machine and not another is often a difference in the audio device's
sample rate rather than a difference in the code, and `AudioSink` logs a line at
device open when the device and the sequence disagree. Include it.
