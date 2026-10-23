# Wired selected-source view

The GUI Guider export is stored under `GUI_Guider_Generated` and is exposed to the
common `SelectedAudioSrcCtrl` through `SelectedAudioSrcWired_View.c`.

The generated Wired screen functions have a `Wired` prefix so they can be linked
beside the Bluetooth GUI Guider export, which uses the same original screen name.
Roboto Bold 9 and 12 are shared with the Bluetooth export and compiled only once.
