#!/usr/bin/env bash
# Compila a biblioteca nativa AsciiDocConverter para Linux, nas arquiteturas amd64 (x64) e arm64.
# Executado DENTRO de um contêiner Debian pelo Build-AsciiDocConverter.ps1 — o diretório do projeto é
# montado em /src e os binários saem em /src/Dist. Também roda num Linux nativo (WSL): RAIZ=$(pwd) bash build-linux.sh
#
# Nomes de saída (um por sistema operacional e arquitetura):
#   AsciiDocConverter.linux-x64.so
#   AsciiDocConverter.linux-arm64.so

set -euo pipefail

RAIZ="${RAIZ:-/src}"
DIST="$RAIZ/Dist"
mkdir -p "$DIST"

if [ -f /etc/debian_version ] && [ "$(id -u)" = "0" ] && ! command -v aarch64-linux-gnu-gcc >/dev/null 2>&1; then
  export DEBIAN_FRONTEND=noninteractive
  apt-get update -qq
  apt-get install -y -qq --no-install-recommends build-essential crossbuild-essential-arm64 python3
fi

# fontes embutidas (zlib) — Python 3 só com biblioteca padrão
python3 -I "$RAIZ/ferramentas/gerar_fontes.py" "$RAIZ/obj/fontes/adc_fontes_embutidas.c"

FONTES=$(find "$RAIZ/src" -name '*.c' | sort)
TERCEIROS="$RAIZ/terceiros/miniz.c $RAIZ/obj/fontes/adc_fontes_embutidas.c"
FLAGS="-O2 -DNDEBUG -std=c11 -fPIC -fvisibility=hidden -Wall -Wextra -Werror -Wno-unused-parameter -Wno-unused-function -Wno-missing-field-initializers -I$RAIZ/include -I$RAIZ/src -I$RAIZ/src/pdf -D_GNU_SOURCE -DADC_EXPORTS"

compilar() {
  local rid="$1" cc="$2" strip="$3" nm="$4"
  local obj="/tmp/adc-obj/$rid"
  local saida="$DIST/AsciiDocConverter.$rid.so"
  echo "==> $rid ($cc)"
  rm -rf "$obj"; mkdir -p "$obj"
  local objs=""
  for f in $FONTES $TERCEIROS; do
    local o="$obj/$(echo "$f" | tr '/' '_').o"
    "$cc" $FLAGS -c "$f" -o "$o"
    objs="$objs $o"
  done
  # --no-undefined: símbolo não resolvido vira erro de link, não falha silenciosa em runtime.
  "$cc" -shared -Wl,--no-undefined -o "$saida" $objs -lm
  "$strip" --strip-unneeded "$saida"
  # Guarda: os três símbolos exportados devem existir; sem eles o P/Invoke falha só em produção.
  for simbolo in ADC_Converter ADC_Liberar ADC_Versao; do
    "$nm" -D --defined-only "$saida" | grep -qw "$simbolo" || { echo "ERRO: símbolo '$simbolo' ausente em $saida" >&2; exit 1; }
  done
  rm -rf "$obj"
  echo "    $saida"
}

# ARQUITETURAS permite compilar só uma (ex.: num WSL sem o cross-compiler): ARQUITETURAS=linux-x64
for rid in ${ARQUITETURAS:-linux-x64 linux-arm64}; do
  case "$rid" in
    linux-x64)   compilar "linux-x64"   "gcc"                   "strip"                   "nm" ;;
    linux-arm64) compilar "linux-arm64" "aarch64-linux-gnu-gcc" "aarch64-linux-gnu-strip" "aarch64-linux-gnu-nm" ;;
    *) echo "ERRO: arquitetura desconhecida '$rid'" >&2; exit 1 ;;
  esac
done

echo "Concluído."
