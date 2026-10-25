/*
 * Alvo de fuzzing (libFuzzer): ADC_Converter com cada formato. Compilar com clang:
 *   clang -std=c11 -g -O1 -fsanitize=fuzzer,address,undefined -DADC_ESTATICO -D_GNU_SOURCE -Iinclude -Isrc -Isrc/pdf \
 *         test/fuzz/fuzz_converter.c $(find src -name '*.c') terceiros/miniz.c obj/fontes/adc_fontes_embutidas.c -lm -o obj/fuzz
 *   obj/fuzz -max_len=4096 -timeout=10 test/fuzz/corpus
 * O byte 0 da entrada escolhe o formato; o restante é o documento.
 */
#include "../../include/adc.h"
#include <stdint.h>
#include <stddef.h>

int LLVMFuzzerTestOneInput(const uint8_t* pDados, size_t pTamanho) {
    if (pTamanho < 1 || pTamanho > 1 << 20) return 0;
    int32_t formato = 1 + (pDados[0] % 4);   /* HTML5, Markdown, PDF, ASG */
    uint8_t* saida = NULL; int32_t tam = 0; char* msgs = NULL;
    ADC_Converter(pDados + 1, (int32_t)pTamanho - 1, formato, NULL, NULL, NULL, &saida, &tam, &msgs);
    ADC_Liberar(saida); ADC_Liberar(msgs);
    return 0;
}
