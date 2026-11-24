#!/usr/bin/env sh
set -eu
LUPDATE="${LUPDATE:-lupdate}"
LRELEASE="${LRELEASE:-lrelease}"
OUT="${1:-build}"
mkdir -p translations "$OUT/translations"

# Update every catalog listed by the project, then compile all available TS files.
"$LUPDATE" wfview-tci-digi.pro
for ts in translations/*.ts; do
    [ -f "$ts" ] || continue
    name=$(basename "$ts" .ts)
    "$LRELEASE" "$ts" -qm "$OUT/translations/$name.qm"
done
printf 'Compiled translation catalogs into: %s/translations\n' "$OUT"
