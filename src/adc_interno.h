/*
 * Entrada INTERNA do conversor (2026-10-31): o que ADC_Converter (ABI pública, congelada — DAD-9) e a entrada
 * WebAssembly (wasm/adc_wasm.c) têm em comum. Os "extras" são o que só o wasm liga; com pExtras == NULL o
 * comportamento é exatamente o de ADC_Converter.
 */
#ifndef ADC_INTERNO_H
#define ADC_INTERNO_H

#include "../include/adc.h"
#include "adc_leitor.h"

typedef struct ADC_Extras {
    ADC_ResolverRecurso2 resolver2;          /* resolvedor com origem e caminho resolvido (DAD-11)      */
    void*                contexto_resolver2;
    bool                 permitir_docbook;   /* DocBook 5 só por aqui: a nativa segue devolvendo ADC_ERRO_FORMATO (DAD-9) */
    bool                 com_arquivo;        /* mensagens levam "arquivo"                               */
} ADC_Extras;

int32_t adc_converter_interno(const uint8_t* pEntradaUTF8, int32_t pTamanho, int32_t pFormato,
                              const char* pOpcoesJSON,
                              ADC_ResolverRecurso pResolver, void* pContextoResolver,
                              const ADC_Extras* pExtras,
                              uint8_t** pSaida, int32_t* pTamanhoSaida, char** pMensagens);

#endif
