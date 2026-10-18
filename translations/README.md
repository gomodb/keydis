# Translations

`keydis_zh_CN.ts` is the Simplified Chinese catalog. Regenerate it after UI
string changes:

```bash
/usr/lib/qt6/bin/lupdate -recursive src -ts translations/keydis_zh_CN.ts
```

The CMake build compiles it with `qt_add_lrelease` and embeds the `.qm` in the
application resource at `:/i18n/`. New strings must be translated in the `.ts`
file; `lupdate` preserves existing translations.

Language selection follows `Language::resolve()`: the `language` key in
`~/.config/keydisrc` (`system`, `zh_CN`, `en`) or the system locale.
