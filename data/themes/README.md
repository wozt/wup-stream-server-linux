# WUP Stream themes (JSON)

Each `*.json` file is discovered at startup. Use the dropdown's refresh button to
load changed/new files without restarting or interrupting streaming.

Use `~/.config/wup-stream-serveur/themes/` for personal themes; they override
bundled themes when their `id` is the same (lowercase `a-z`, digits, `_`, `-`).
You can copy any bundled theme JSON to that folder and edit its `name`, `id`,
`colors` (GTK UI), and `gamepad` (Cairo visualizer). All colors are `#RRGGBB`.

UI color keys: background, panel, entry, border, text, muted, accent,
button, hover, active, negative.
GamePad keys: background, shell, screen, border, text, muted, accent, negative.

A malformed JSON file is ignored and a warning is printed to stderr.
