/*
 * Mensagens do processador: avisos e erros com linha e coluna, em pt-BR. Nunca se perdem — vão ao
 * chamador como JSON no sucesso e na falha (IC-2).
 */
#ifndef ADC_MENSAGENS_H
#define ADC_MENSAGENS_H

#include "adc_comum.h"

typedef enum ADC_Nivel { ADC_NIVEL_AVISO = 1, ADC_NIVEL_ERRO = 2 } ADC_Nivel;

typedef struct ADC_Mensagem {
    ADC_Nivel            nivel;
    int32_t              linha;      /* 0 = sem posição */
    int32_t              coluna;
    const char*          texto;      /* na arena */
    struct ADC_Mensagem* proxima;
} ADC_Mensagem;

typedef struct ADC_Mensagens {
    ADC_Arena*    arena;
    ADC_Mensagem* primeira;
    ADC_Mensagem* ultima;
    int32_t       quantidade;
    int32_t       erros;
    int32_t       limite;       /* para de acumular além disto (e registra um aviso final) */
    bool          estourou;
} ADC_Mensagens;

void adc_msgs_iniciar(ADC_Mensagens* pM, ADC_Arena* pArena);
/* printf-like; pLinha/pColuna 0 quando não houver posição. */
void adc_msgs_adicionar(ADC_Mensagens* pM, ADC_Nivel pNivel, int32_t pLinha, int32_t pColuna, const char* pFormato, ...);
/* Serializa como array JSON (texto UTF-8 com '\0' extra). */
void adc_msgs_para_json(const ADC_Mensagens* pM, ADC_Buffer* pSaida);

#endif
