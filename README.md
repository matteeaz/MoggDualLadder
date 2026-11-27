# MoggDualLadder
A modded take on a dual ladder-filter-circuit.

(!!) Copy VST3 and/or AU component to the dedicated directory. (/Library/Audio/Plug-Ins/...)

PLUGIN WON'T OPEN UNTIL YOU REMOVE QUARANTINE FLAG IN TERMINAL.

(!!) Type these lines in to Terminal and confirm with password.

```bash
   sudo xattr -cr "/Library/Audio/Plug-Ins/Components/Mogg Dual Ladder.component" && sudo codesign --force --deep --sign - "/Library/Audio/Plug-Ins/Components/Mogg Dual Ladder.component"
   ```
```bash
   sudo xattr -cr "/Library/Audio/Plug-Ins/VST3/Mogg Dual Ladder.vst3" && sudo codesign --force --deep --sign - "/Library/Audio/Plug-Ins/VST3/Mogg Dual Ladder.vst3"
   ```

Have fun! ^^
