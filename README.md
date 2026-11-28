# Dirty Mogger

A modded take on a dual filter-circuit.

(!!) Copy VST3 and/or AU component to the dedicated directory. (/Library/Audio/Plug-Ins/...)

PLUGIN WON'T OPEN UNTIL YOU REMOVE QUARANTINE FLAG IN TERMINAL.

(!!) Type these lines in to Terminal and confirm with password.

```bash
sudo xattr -cr "/Library/Audio/Plug-Ins/Components/Dirty Mogger.component" && sudo codesign --force --deep --sign - "/Library/Audio/Plug-Ins/Components/Dirty Mogger.component"
```

```bash
sudo xattr -cr "/Library/Audio/Plug-Ins/VST3/Dirty Mogger.vst3" && sudo codesign --force --deep --sign - "/Library/Audio/Plug-Ins/VST3/Dirty Mogger.vst3"
```

Have fun! ^^
