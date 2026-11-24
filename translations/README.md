# Translation files

`wfview TCI Digi` uses Qt Linguist (`.ts` source catalogs and compiled `.qm`
runtime catalogs).

## Current catalog

- `wfview_tci_digi_ja_JP.ts` — Japanese, by **JG3HLX** (partly done: about 210 of
  the 1,025 texts so far).
- English is the source language and is used whenever a translated entry or
  catalog is unavailable.

The starter catalog contains the simple `tr("...")` strings found in the C++
source snapshot. Some multi-part C++ strings and strings assembled dynamically
may not be in this initial extraction. Run `lupdate` before translating or
releasing so Qt can reconcile the catalog with the complete source.

## Update the Japanese catalog

From the repository root:

```sh
lupdate wfview-tci-digi.pro -ts translations/wfview_tci_digi_ja_JP.ts
```

Open the `.ts` file in Qt Linguist, complete entries marked unfinished, and
save it.

## Compile and deploy

Nothing to do by hand: `wfview-tci-digi.pro` lists every catalog in
`TRANSLATIONS`, and qmake's `lrelease` / `embed_translations` features compile
them at build time and build the `.qm` files into the executable (resource
prefix `:/i18n`). The Windows zip and the Linux AppImage therefore carry every
language. `lrelease` comes with Qt (Qt Linguist tools); without it the build
still works, in English only, with a qmake warning.

`wfview-tci-digi --list-languages` prints the languages a build contains.

To try a catalog without rebuilding, compile it yourself and put the `.qm` in a
`translations` folder beside the executable; it takes priority over the
built-in one of the same name:

```sh
mkdir -p build/translations
lrelease translations/wfview_tci_digi_ja_JP.ts \
  -qm build/translations/wfview_tci_digi_ja_JP.qm
```

Windows PowerShell:

```powershell
New-Item -ItemType Directory -Force build\translations | Out-Null
lrelease translations\wfview_tci_digi_ja_JP.ts `
  -qm build\translations\wfview_tci_digi_ja_JP.qm
```

`tools/update-translations.sh` / `.ps1` run `lupdate` and then do the same for
every catalog.

## Add another language

1. Copy the Japanese `.ts` file and change its `language` attribute and name,
   for example `wfview_tci_digi_de_DE.ts`.
2. Add the new file to `TRANSLATIONS` in `wfview-tci-digi.pro`.
3. Run `lupdate`, translate it in Qt Linguist, and rebuild: the new language is
   built in and appears in Options > Language.

The language menu lists every built-in catalog and every
`wfview_tci_digi_*.qm` in `translations/` beside the executable. The
OS-language option uses the system locale (for example `ja_JP`) and falls back
to the language-only catalog (for example `ja`) if a region-specific catalog
is not present.

## Runtime behavior

- `system`: follow the operating-system locale (default).
- `en`: use the English source strings.
- Any other value: load the matching `.qm` catalog (beside the executable
  first, then built in).
- Qt's own texts (dialog buttons such as Yes / No) use Qt's `qtbase_<lang>.qm`
  when it is found beside the executable or in Qt's translations folder;
  otherwise they stay in English.
- A language change is saved in QSettings and takes effect after restarting
  the application. The running UI is deliberately not retranslated in place,
  to avoid disrupting active modem and TCI state.

## Translators

Thank you to everyone who translates wfview TCI Digi. Translators are credited
here, in Help > About and in the release notes.

- Japanese (日本語): JG3HLX
