# WUP Stream language packs

English is compiled into the C application; no `en.json` is required.

Each JSON file has `id` (lowercase ASCII language code), `name` (native language
name) and a `translations` object mapping *original English interface strings*
to UTF-8 text. Missing keys fall back to English. Example:

```json
{
  "id": "nl",
  "name": "Nederlands",
  "translations": {"Start stream": "Stream starten", "Quality": "Kwaliteit"}
}
```

Bundled language packs: `data/languages/*.json`
User overrides and additional languages: `~/.config/wup-stream-serveur/languages/*.json`
Use the language drop-down and refresh button to apply changes without restarting.
Translation keys are original English strings, not localized labels.
GTK3 uses the system's installed fonts; some languages need Noto CJK fonts.
