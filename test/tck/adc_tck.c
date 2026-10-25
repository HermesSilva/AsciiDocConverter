/*
 * Adapter do AsciiDoc TCK: lê {"contents","path","type"} do stdin e escreve o ASG em JSON no stdout.
 * Compilado junto dos fontes da biblioteca (ADC_ESTATICO) para alcançar o parse só de inlines.
 */
#include "../../src/adc_documento.h"
#include "../../src/adc_render.h"
#include "../../src/adc_json.h"
#include <stdio.h>
#include <stdlib.h>

int main(void) {
    ADC_Buffer entrada; adc_buffer_iniciar(&entrada);
    char tmp[65536]; size_t n;
    while ((n = fread(tmp, 1, sizeof tmp, stdin)) > 0) adc_buffer_bytes(&entrada, tmp, (int32_t)n);
    ADC_Arena arena; adc_arena_iniciar(&arena, 64 * 1024);
    const char* erro = NULL; int32_t pos = 0;
    ADC_Json* j = adc_json_ler(&arena, adc_buffer_fatia_de(&entrada), &erro, &pos);
    if (!j) { fprintf(stderr, "entrada do TCK nao e JSON valido: %s (pos %d)\n", erro ? erro : "?", pos); return 1; }
    ADC_Fatia contents = adc_json_texto(j, "contents", "");
    ADC_Fatia tipo = adc_json_texto(j, "type", "block");
    ADC_Fatia path = adc_json_texto(j, "path", "");
    ADC_Contexto* ctx = ADC_NOVO(&arena, ADC_Contexto);
    const char* erro_op = NULL;
    if (!ctx || !adc_doc_iniciar(ctx, &arena, NULL, NULL, NULL, &erro_op)) { fprintf(stderr, "falha ao iniciar\n"); return 1; }
    ADC_Buffer saida; adc_buffer_iniciar(&saida);
    if (adc_fatia_igual(tipo, "inline")) {
        ADC_Inline* in = adc_doc_parsear_inlines(ctx, contents);
        adc_asg_escrever_inlines(ctx, in, &saida);
    } else {
        adc_doc_parsear(ctx, contents, path);
        adc_asg_escrever_documento(ctx, &saida);
    }
    fwrite(saida.dados, 1, (size_t)saida.tamanho, stdout);
    fputc('\n', stdout);
    for (const ADC_Mensagem* m = ctx->msgs.primeira; m; m = m->proxima) fprintf(stderr, "%s [%d:%d] %s\n", m->nivel == ADC_NIVEL_ERRO ? "erro" : "aviso", m->linha, m->coluna, m->texto);
    adc_buffer_liberar(&saida); adc_buffer_liberar(&entrada);
    adc_arena_liberar(&arena);
    return 0;
}
