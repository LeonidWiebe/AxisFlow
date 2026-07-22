# Translating AxisFlow

AxisFlow follows FreeCAD's translation policy: the workbench shows its interface
in **FreeCAD's own language** (Preferences → General → Language). There is no
separate language switch inside AxisFlow — set FreeCAD's language and AxisFlow
follows it, including on first start. Translations use Qt's system: `.ts` source
files (XML, editable in Qt Linguist) compiled to `.qm` binaries that FreeCAD
loads at runtime.

All translatable strings are grouped under the context **`AxisFlow`** (the
settings dialog) plus a few Qt contexts for the C++ side (the panel and each
command). You don't need to worry about contexts when translating — Qt Linguist
shows them for you.

## Files

Everything lives in `translations/`:

- `AxisFlow.ts` — the **base source** (English). Do not translate this one; it is
  regenerated from the code. Copy it to start a new language.
- `AxisFlow_<lang>.ts` — one per language (e.g. `AxisFlow_de.ts`,
  `AxisFlow_fr.ts`, `AxisFlow_zh-CN.ts`). `<lang>` is the FreeCAD/Qt locale code.
- `AxisFlow_<lang>.qm` — the compiled file FreeCAD actually loads. Generated from
  the `.ts`; ship it in releases.

A worked set is included: `AxisFlow_de/fr/hu/zh-CN/ko/hi/sr/ru.ts` and their `.qm`
(German, French, Hungarian, Chinese Simplified, Korean, Hindi, Serbian, Russian). These
are initial machine-assisted translations — corrections from native speakers are
very welcome.

## Add or update a translation

1. **Start a language**: copy `translations/AxisFlow.ts` to
   `translations/AxisFlow_<lang>.ts` and set the language, e.g. open it in
   **Qt Linguist** (`linguist`) which asks for the target language, or edit the
   header to `<TS version="2.1" language="de_DE">`.
2. **Translate** every entry in Qt Linguist (or any text editor: fill each
   `<translation>…</translation>`). Strings shown to the user only — leave
   technical/console text alone.
3. **Compile** to `.qm`:
   ```
   lrelease translations/AxisFlow_<lang>.ts
   ```
   or just run `python update_translations.py --compile` to build every language.
4. **Test**: set FreeCAD to that language (Preferences → General → Language),
   restart FreeCAD, switch to AxisFlow. The UI should appear translated. (Some
   parts only change after a restart — that's normal in FreeCAD.)
5. Send a pull request with the `.ts` (and `.qm`) so it ships for everyone.

## Regenerating the base source (maintainers)

When UI strings change in the code, refresh `AxisFlow.ts`:

```
python update_translations.py        # extract strings + compile all .qm
```

It runs the FreeCAD-standard pipeline (`pylupdate`/`lupdate` → `lconvert` →
`lrelease`). Needs Qt Linguist tools on PATH: on Debian/Ubuntu
`pyqt5-dev-tools` + `qttools5-dev-tools`; on Qt6, `pyside6-lupdate` /
`pyside6-lrelease`. To then pull the new strings into an existing translation,
re-run `lupdate` on that language file (it keeps your existing translations and
adds the new ones as “unfinished”), or use a platform like
[Crowdin](https://crowdin.com/project/freecad-addons).

## Marking new strings in code (developers)

- **Python**: define `translate = FreeCAD.Qt.translate` (the name must be exactly
  `translate`), then wrap UI text: `translate("AxisFlow", "My text")`. For text
  stored in data and translated later, mark the literal with
  `QT_TRANSLATE_NOOP("AxisFlow", "My text")` and translate the variable at
  display time.
- **C++**: use `tr("My text")` in a `QObject`, or `QT_TR_NOOP("My text")` for
  command `sMenuText`/`sToolTipText` (FreeCAD translates those with the command's
  class name as context).
- Pass **string literals**, never variables, to the extractor functions —
  `lupdate` reads text, it does not run code.
- Do not hide a literal behind your own helper. `translate("AxisFlow", "Close")`
  is extracted; `_tr("Close")` is **not**, even if `_tr` merely calls `translate`
  — the extractor matches the function *name*. A helper is fine for text that was
  already marked with `QT_TRANSLATE_NOOP` elsewhere (that is how the option
  labels work), but never for a literal that appears only there. This is a silent
  failure: the string simply never reaches the `.ts` file, so after adding UI
  text always check that the message count in `AxisFlow.ts` grew by what you
  expect.

See the FreeCAD wiki for the full policy:
<https://wiki.freecad.org/Translating_an_external_workbench>.
