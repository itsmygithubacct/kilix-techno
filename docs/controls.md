# Controls

Keyboard and mouse controls of the Kilix Techno editor. This text was moved
from the README; the [README](../README.md) is the short entry point.

## Controls

- The grid groups steps into four-step beats. Filled height represents
  velocity, the bright top edge represents probability, and dots identify
  extra ratchets. Instrument families retain distinct colors while the
  playhead and edit cursor use separate outlines.
- Left/right move the step cursor; up/down choose the editable field; `Tab`
  changes the selected track. The instrument field selects any native or
  curated voice. A missing optional voice plays its corresponding native synth
  fallback.
- `Enter` toggles a step; `Space` starts or pauses transport; `.` stops;
  `Ctrl+Space` restarts; `T` sets tap tempo.
- `[` and `]` select tracks; `-` and `+` edit the focused value.
- `M` toggles mute; `Shift+M` or `S` toggles solo.
- `Ctrl+Z` and `Ctrl+Y` undo and redo; `U` and `R` are compact aliases.
- `Ctrl+S` saves; `Ctrl+E` starts or cancels stereo WAV export; `F1` or `?`
  opens help.
- `1`, `2`, `3`, and `4` switch between the step grid, piano roll,
  arrangement, and mixer. Piano-roll tiles support left-drag note painting,
  right-drag erasing, and Shift-drag range selection in both UI modes.
- Shift+arrows extends a rectangular selection. `Ctrl+C`, `Ctrl+X`, and
  `Ctrl+V` copy, cut, and paste it. `Alt+F/C/I/M` fills, clears, inverts, or
  mirrors it; Alt+Left/Right rotates it.
- `Ctrl+P` turns the selected track into a named linked pattern; `Ctrl+D`
  creates an independent pattern copy. `Alt+A` appends the current scene to
  the arrangement and Alt+Enter launches the arrangement.
- `,` and `>` choose a scene; `L` launches it at the project's selected
  step, beat, or four-beat boundary.
- `Q` requests quit and asks again when the project has unsaved changes.
- Left-click toggles a grid step; keep dragging to paint that resulting on/off
  state across every tile crossed. Left-click also selects tracks and scenes.
- Right-click a step or track label for track options and categorized instrument
  lists. Press `/` in that menu to search; Space previews and `F` toggles a
  favorite. Selecting an uninstalled instrument offers its one-action lazy
  install while retaining an audible native fallback. Keep right-dragging a
  grid instead to erase every crossed tile. Right-click a scene to launch it.
- The wheel edits the focused value. Middle-click is intentionally ignored.

The status line always reports the selected audio backend or the usable
no-audio state and the installed-bank count. Project editing and export do not
require an audio sink.
Kitty graphics are optional: a failed pixel-session start restores the
terminal before the bounded cell editor starts.
