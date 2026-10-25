#!/usr/bin/env bash
# Compilação de desenvolvimento no Linux (WSL ou nativo): .so, adc_cli e adc_tck em obj/dev-linux/.
# Debug (padrão) liga ASan+UBSan; "release" compila -O2. Uso: ./compilar-dev.sh [debug|release]
set -euo pipefail
RAIZ="$(cd "$(dirname "$0")" && pwd)"
MODO="${1:-debug}"
OBJ="$RAIZ/obj/dev-linux"
mkdir -p "$OBJ"
CC="${CC:-gcc}"
FONTES=$(find "$RAIZ/src" -name '*.c' | sort)
TERCEIROS="$RAIZ/terceiros/miniz.c"
PY="${PYTHON:-python3}"
"$PY" -I "$RAIZ/ferramentas/gerar_fontes.py" "$RAIZ/obj/fontes/adc_fontes_embutidas.c"
FONTES_C="$RAIZ/obj/fontes/adc_fontes_embutidas.c"
COMUNS="-std=c11 -Wall -Wextra -Werror -Wno-unused-parameter -Wno-unused-function -Wno-missing-field-initializers -I$RAIZ/include -I$RAIZ/src -I$RAIZ/src/pdf -D_GNU_SOURCE"
if [ "$MODO" = "release" ]; then
  OPT="-O2 -DNDEBUG"
else
  OPT="-O0 -g -fsanitize=address,undefined -fno-omit-frame-pointer"
fi
echo "==> .so"
$CC $COMUNS $OPT -fPIC -DADC_EXPORTS -shared -Wl,--no-undefined -o "$OBJ/AsciiDocConverter.so" $FONTES $TERCEIROS $FONTES_C -lm
echo "==> adc_cli"
$CC $COMUNS $OPT -DADC_ESTATICO -o "$OBJ/adc_cli" "$RAIZ/test/adc_cli.c" $FONTES $TERCEIROS $FONTES_C -lm
echo "==> adc_tck"
$CC $COMUNS $OPT -DADC_ESTATICO -o "$OBJ/adc_tck" "$RAIZ/test/tck/adc_tck.c" $FONTES $TERCEIROS $FONTES_C -lm
echo "OK: $OBJ"
