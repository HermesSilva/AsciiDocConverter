/*
 * Parser inline: transforma texto (já pré-processado quanto a {atributos}) em nós inline (SDR-5).
 * Descida recursiva com escolha ordenada; a saída de cada regra são nós, não texto.
 */
#ifndef ADC_INLINES_H
#define ADC_INLINES_H

#include "adc_contexto.h"

/* Um segmento do texto de origem: do byte 'inicio' (no texto passado ao parser) até o início do
 * próximo segmento, os bytes vêm da linha 'linha' do arquivo 'arquivo', a partir da coluna 'coluna'. */
typedef struct ADC_Segmento {
    int32_t inicio;
    int32_t linha;
    int32_t coluna;
    int32_t arquivo;
} ADC_Segmento;

typedef struct ADC_Origem {
    const ADC_Segmento* segmentos;   /* ordenados por 'inicio'; o 1º tem inicio 0                      */
    int32_t             quantidade;
    const int32_t*      mapa;        /* opcional: byte do texto pré-processado → byte do texto original */
    int32_t             tamanho_original;
} ADC_Origem;

typedef enum ADC_Subs {
    ADC_SUBS_NORMAL = 1,   /* tudo                                                                     */
    ADC_SUBS_VERBATIM,     /* só texto (special chars na renderização) + callouts <n>                   */
    ADC_SUBS_NENHUMA,      /* passthrough: um nó RAW                                                   */
    ADC_SUBS_TITULO,       /* como normal, sem notas de rodapé novas (títulos)                         */
    ADC_SUBS_VERBATIM_MARCAS /* verbatim + quotes (subs="+quotes"): callouts e só as marcas *, _, `, #,
                                ^, ~ e aspas tipográficas — sem macros, passthroughs nem replacements      */
} ADC_Subs;

/* Parseia o texto. pOrigem pode ser NULL (então as localizações ficam zeradas). */
ADC_Inline* adc_inlines_parsear(ADC_Contexto* pC, ADC_Fatia pTexto, const ADC_Origem* pOrigem, ADC_Subs pSubs);

/* Monta uma ADC_Origem de uma só linha (col inicial 1) — para títulos, células, etc. */
ADC_Origem adc_origem_linha(ADC_Segmento* pSeg, int32_t pLinha, int32_t pColuna, int32_t pArquivo, const int32_t* pMapa, int32_t pTamanhoOriginal);

/* Converte um deslocamento de byte em posição (linha, coluna em caracteres). */
ADC_Posicao adc_origem_posicao(const ADC_Origem* pO, ADC_Fatia pTextoOriginal, int32_t pDeslocamentoOriginal, int32_t* pArquivo);

/* Nó de texto simples na arena. */
ADC_Inline* adc_inline_texto(ADC_Arena* pArena, ADC_Fatia pValor);

#endif
