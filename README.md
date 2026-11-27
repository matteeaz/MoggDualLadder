# MoggDualLadder
A modernized take on a dual ladder-filter-circuit.

(!!) Copy VST3 and/or AU component to the dedicated directory. (/Library/Audio/Plug-Ins/...)

Plugin won't open until you remove the qurantine flag.

(!!) Type in to Terminal:

  sudo xattr -cr "/Library/Audio/Plug-Ins/VST3/Mogg Dual Ladder.vst3"
  sudo codesign --force --deep --sign - "/Library/Audio/Plug-Ins/VST3/Mogg Dual Ladder.vst3"

confirm with password, and

  sudo xattr -cr "/Library/Audio/Plug-Ins/Components/Mogg Dual Ladder.component"
  sudo codesign --force --deep --sign - "/Library/Audio/Plug-Ins/Components/Mogg Dual Ladder.component"

Have fun! ^^
