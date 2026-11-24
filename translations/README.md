# Translation files

`wfview TCI Digi` uses Qt Linguist (`.ts` source catalogs and compiled `.qm`
runtime catalogs).

## Current catalog

- `wfview_tci_digi_ja_JP.ts` — Japanese starter translation.
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

The executable expects compiled catalogs in a `translations` directory beside
the executable:

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

For a build in another directory, copy the `.qm` file to
`<directory-containing-wfview-tci-digi.exe>/translations/`.

## Add another language

1. Copy the Japanese `.ts` file and change its `language` attribute and name,
   for example `wfview_tci_digi_de_DE.ts`.
2. Add the new file to `TRANSLATIONS` in `wfview-tci-digi.pro`.
3. Run `lupdate`, translate it in Qt Linguist, then compile it with `lrelease`.
4. Place the resulting `.qm` beside the executable in `translations/`.

The language menu discovers `wfview_tci_digi_*.qm` files automatically. The
OS-language option uses the system locale (for example `ja_JP`) and falls back
to the language-only catalog (for example `ja`) if a region-specific catalog
is not present.

## Runtime behavior

- `system`: follow the operating-system locale (default).
- `en`: use the English source strings.
- Any other value: load the matching `.qm` catalog.
- A language change is saved in QSettings and takes effect after restarting
  the application. The running UI is deliberately not retranslated in place,
  to avoid disrupting active modem and TCI state.
