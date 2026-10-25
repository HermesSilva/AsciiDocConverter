/*
 * Fontes embutidas no binário (geradas por ferramentas/gerar_fontes.py em obj/fontes/adc_fontes_embutidas.c):
 * Liberation 2.1.5, comprimidas com zlib; descomprimidas na arena do documento quando usadas.
 */
#ifndef ADC_FONTES_H
#define ADC_FONTES_H

#include <stdint.h>

typedef struct ADC_FonteEmbutida {
    const char*     identificador;   /* "serif_regular", "sans_negrito", "mono_italico", ...      */
    const char*     nome_pdf;        /* "LiberationSerif-Regular"                                  */
    const uint8_t*  dados_zlib;
    int32_t         tamanho_zlib;
    int32_t         tamanho_ttf;
} ADC_FonteEmbutida;

extern const ADC_FonteEmbutida adc_fontes_embutidas[];
extern const int32_t adc_fontes_embutidas_quantidade;

#endif
