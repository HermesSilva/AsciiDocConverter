/*
 * Mensagens do processador: avisos e erros com linha e coluna. Nunca se perdem — vão ao chamador como JSON no
 * sucesso e na falha (IC-2).
 *
 * Idioma (DAD-10, 2026-10-31): o texto sai em INGLÊS por padrão e em pt-BR quando a opção "idioma" vale "pt-BR".
 * Os formatos são escritos em pt-BR nos pontos de chamada (como sempre foram) e traduzidos pela tabela de
 * adc_mensagens_en.c; o que não tem tradução sai em pt-BR (nunca em branco).
 *
 * Arquivo (DAD-11): cada mensagem lembra o arquivo que o leitor estava lendo quando ela nasceu (o principal ou um
 * include). O JSON só o emite quando `com_arquivo` está ligado — a entrada WebAssembly liga; a ABI nativa não.
 */
#ifndef ADC_MENSAGENS_H
#define ADC_MENSAGENS_H

#include "adc_comum.h"

typedef enum ADC_Nivel { ADC_NIVEL_AVISO = 1, ADC_NIVEL_ERRO = 2 } ADC_Nivel;

typedef enum ADC_Idioma { ADC_IDIOMA_EN = 0, ADC_IDIOMA_PT_BR = 1 } ADC_Idioma;

typedef struct ADC_Mensagem {
    ADC_Nivel            nivel;
    int32_t              linha;      /* 0 = sem posição */
    int32_t              coluna;
    const char*          texto;      /* na arena */
    const char*          arquivo;    /* caminho do arquivo lido quando a mensagem nasceu; NULL/"" = principal */
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
    ADC_Idioma    idioma;
    const char*   arquivo_corrente;   /* mantido pelo leitor (include em leitura) */
    bool          com_arquivo;        /* o JSON leva "arquivo" */
} ADC_Mensagens;

void adc_msgs_iniciar(ADC_Mensagens* pM, ADC_Arena* pArena);
/* printf-like; pLinha/pColuna 0 quando não houver posição. O formato é o texto pt-BR; a tradução é automática. */
void adc_msgs_adicionar(ADC_Mensagens* pM, ADC_Nivel pNivel, int32_t pLinha, int32_t pColuna, const char* pFormato, ...);
/* Traduz uma frase pt-BR conhecida para o idioma corrente (devolve a própria frase quando não há tradução). Serve
 * aos argumentos %s que são frases fixas (motivo de falha de imagem, degradação do Markdown...). */
const char* adc_msgs_tr(const ADC_Mensagens* pM, const char* pFrasePT);
/* Serializa como array JSON (texto UTF-8 com '\0' extra). */
void adc_msgs_para_json(const ADC_Mensagens* pM, ADC_Buffer* pSaida);
/* "en" | "pt-BR" (qualquer caixa; "pt" conta como pt-BR). Devolve false se desconhecido. */
bool adc_idioma_ler(ADC_Fatia pTexto, ADC_Idioma* pIdioma);

#endif
