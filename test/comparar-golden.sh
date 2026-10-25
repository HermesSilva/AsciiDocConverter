#!/usr/bin/env bash
# Converte cada test/casos/*.adoc com o adc_cli e compara com test/golden/*.html (Asciidoctor 2.0.x).
# Uso: test/comparar-golden.sh [caminho do adc_cli]   (padrão: obj/dev-linux/adc_cli)
set -uo pipefail
RAIZ="$(cd "$(dirname "$0")/.." && pwd)"
CLI="${1:-$RAIZ/obj/dev-linux/adc_cli}"
pass=0; fail=0
mkdir -p "$RAIZ/obj/golden-obtido"
for adoc in "$RAIZ"/test/casos/*.adoc; do
  nome=$(basename "$adoc" .adoc)
  esperado="$RAIZ/test/golden/$nome.html"
  obtido="$RAIZ/obj/golden-obtido/$nome.html"
  [ -f "$esperado" ] || { echo "SEM-GOLDEN $nome"; continue; }
  "$CLI" "$adoc" html "$obtido" 2>"$RAIZ/obj/golden-obtido/$nome.msgs" || true
  if diff -q <(sed -e 's/[[:space:]]*$//' "$esperado") <(sed -e 's/[[:space:]]*$//' "$obtido"; echo) >/dev/null 2>&1 || diff -q "$esperado" "$obtido" >/dev/null 2>&1; then
    pass=$((pass+1)); echo "PASS $nome"
  else
    fail=$((fail+1)); echo "FAIL $nome"
    diff -u "$esperado" "$obtido" | head -${ADC_DIFF_LINHAS:-30}
  fi
done
echo "pass=$pass fail=$fail"
[ "$fail" -eq 0 ]
