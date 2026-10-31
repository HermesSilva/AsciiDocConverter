#!/usr/bin/env bash
# Compila a AsciiDocConverter para WebAssembly (wasm32-wasi, modelo "reactor") com o wasi-sdk:
#   Dist/AsciiDocConverter.wasm
# É o MESMO fonte C da .dll/.so mais a entrada wasm/adc_wasm.c (fora de src/, que os builds nativos compilam por
# curinga — DAD-9). Roda em qualquer Linux/WSL/macOS com o wasi-sdk (https://github.com/WebAssembly/wasi-sdk):
#   WASI_SDK_PATH=/opt/wasi-sdk bash wasm/build-wasm.sh
# Sem WASI_SDK_PATH procura em ~/wasi-sdk e /opt/wasi-sdk. Python 3 (só biblioteca padrão) gera as fontes embutidas.
set -euo pipefail

RAIZ="$(cd "$(dirname "$0")/.." && pwd)"
SDK="${WASI_SDK_PATH:-}"
for candidato in "$SDK" "$HOME/wasi-sdk" /opt/wasi-sdk; do
  if [ -n "$candidato" ] && [ -x "$candidato/bin/clang" ]; then SDK="$candidato"; break; fi
done
if [ -z "$SDK" ] || [ ! -x "$SDK/bin/clang" ]; then
  echo "ERRO: wasi-sdk não encontrado. Defina WASI_SDK_PATH ou instale em ~/wasi-sdk (https://github.com/WebAssembly/wasi-sdk/releases)." >&2
  exit 1
fi
SYSROOT="$SDK/share/wasi-sysroot"
CC="$SDK/bin/clang"
OBJ="$RAIZ/obj/wasm"
DIST="$RAIZ/Dist"
mkdir -p "$OBJ" "$DIST"

echo "==> fontes embutidas"
python3 -I "$RAIZ/ferramentas/gerar_fontes.py" "$RAIZ/obj/fontes/adc_fontes_embutidas.c"

FONTES=$(find "$RAIZ/src" -name '*.c' | sort)
EXTRAS="$RAIZ/terceiros/miniz.c $RAIZ/obj/fontes/adc_fontes_embutidas.c $RAIZ/wasm/adc_wasm.c"
# -Werror vale como nos builds nativos; os -Wno- extras cobrem o que o clang acusa e o gcc não.
FLAGS="--target=wasm32-wasi --sysroot=$SYSROOT -O2 -DNDEBUG -std=c11 -fvisibility=hidden -Wall -Wextra -Werror \
  -Wno-unused-parameter -Wno-unused-function -Wno-missing-field-initializers -Wno-unused-but-set-variable \
  -Wno-unknown-warning-option -Wno-unknown-pragmas \
  -I$RAIZ/include -I$RAIZ/src -I$RAIZ/src/pdf -D_GNU_SOURCE -DADC_ESTATICO -DMINIZ_NO_STDIO -DMINIZ_NO_TIME"

echo "==> compilando ($CC)"
OBJS=""
for f in $FONTES $EXTRAS; do
  o="$OBJ/$(echo "${f#$RAIZ/}" | tr '/' '_').o"
  $CC $FLAGS -c "$f" -o "$o"
  OBJS="$OBJS $o"
done

SAIDA="$DIST/AsciiDocConverter.wasm"
echo "==> ligando $SAIDA"
# reactor: sem main; _initialize é exportado e chamado pelo hospedeiro. Pilha de 4 MiB: o leiaute do PDF recursa fundo.
$CC --target=wasm32-wasi --sysroot="$SYSROOT" -mexec-model=reactor -O2 \
  -Wl,--strip-all -Wl,-z,stack-size=4194304 -Wl,--initial-memory=16777216 \
  -o "$SAIDA" $OBJS

# Guarda: as exportações do contrato precisam existir (sem elas o hospedeiro falha só em tempo de execução).
if command -v "$SDK/bin/wasm-ld" >/dev/null 2>&1 && command -v python3 >/dev/null 2>&1; then
  python3 - "$SAIDA" <<'PY'
import sys
dados = open(sys.argv[1], 'rb').read()
esperadas = [b'adc_wasm_alloc', b'adc_wasm_free', b'adc_wasm_converter', b'adc_wasm_saida', b'adc_wasm_saida_tamanho', b'adc_wasm_mensagens', b'adc_wasm_limpar', b'adc_wasm_versao', b'adc_wasm_resolvido', b'_initialize', b'memory']
faltam = [e.decode() for e in esperadas if e not in dados]
if faltam:
    print('ERRO: exportações ausentes no .wasm: ' + ', '.join(faltam), file=sys.stderr); sys.exit(1)
print('    exportações conferidas (%d bytes)' % len(dados))
PY
fi
echo "Concluído: $SAIDA"
