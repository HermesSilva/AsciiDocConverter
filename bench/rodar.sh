#!/usr/bin/env bash
# Compila o benchmark em -O2 e roda sobre bench/corpus/*.adoc. Também mede o Asciidoctor.js 3.0.4 (Node)
# no mesmo corpus quando ADC_GOLDEN_MODULOS aponta para a pasta node_modules com @asciidoctor/core.
# Uso: bench/rodar.sh [repeticoes]
set -euo pipefail
RAIZ="$(cd "$(dirname "$0")/.." && pwd)"
REP="${1:-21}"
mkdir -p "$RAIZ/obj/bench"
python3 -I "$RAIZ/ferramentas/gerar_fontes.py" "$RAIZ/obj/fontes/adc_fontes_embutidas.c" >/dev/null
gcc -std=c11 -O2 -DNDEBUG -DADC_ESTATICO -I"$RAIZ/include" -I"$RAIZ/src" -I"$RAIZ/src/pdf" -D_GNU_SOURCE \
    -o "$RAIZ/obj/bench/bench" "$RAIZ/bench/bench.c" $(find "$RAIZ/src" -name '*.c') "$RAIZ/terceiros/miniz.c" "$RAIZ/obj/fontes/adc_fontes_embutidas.c" -lm
echo "maquina: $(uname -m) $(grep -m1 'model name' /proc/cpuinfo 2>/dev/null | cut -d: -f2 | sed 's/^ //')"
for f in "$RAIZ"/bench/corpus/*.adoc; do
  "$RAIZ/obj/bench/bench" "$f" "$REP"
  if [ -n "${ADC_GOLDEN_MODULOS:-}" ] && command -v node >/dev/null; then
    node "$RAIZ/bench/asciidoctorjs.js" "$f" "$REP" "$ADC_GOLDEN_MODULOS"
  fi
  echo
done
