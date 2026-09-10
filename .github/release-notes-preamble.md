### Installing

- **macOS** — `CutReel-<version>-Darwin-<arch>.dmg`. Open it and drag **cutreel.app** onto **Applications**. See the note below before the first launch.
- **Linux** — `cutreel_<version>_amd64.deb`. `sudo apt install ./cutreel_<version>_amd64.deb`.
- **Windows** — `CutReel-<version>-Windows-AMD64.exe`. Run it; it installs to Program Files and adds a Start Menu entry.

### macOS: the first launch is blocked, and the message is wrong

CutReel is not signed with an Apple Developer ID and is not notarised, so macOS
refuses to start it the first time and says:

> "cutreel" is damaged and can't be opened. You should move it to the Trash.

It is not damaged. macOS flags anything a browser downloads with
`com.apple.quarantine`, and Gatekeeper will not launch a quarantined
application it cannot trace to a paid developer account — this is the wording
it reaches for, and it is the same wording a genuinely corrupt download gets.

After dragging the app to `/Applications`, clear the flag on it once:

```
xattr -dr com.apple.quarantine /Applications/cutreel.app
```

That affects only this bundle and leaves Gatekeeper switched on for everything
else. Each new download needs the command again. The dialog goes away for good
only with a Developer ID signature and notarisation.

---
