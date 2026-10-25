/*
 * JSON mínimo: leitor para as opções (pOpcoesJSON, tema do PDF) e escrita escapada para a saída
 * (ASG, mensagens). Sem dependências. O leitor materializa a árvore na arena.
 */
#ifndef ADC_JSON_H
#define ADC_JSON_H

#include "adc_comum.h"

typedef enum ADC_JsonTipo {
    ADC_JSON_NULO, ADC_JSON_BOOL, ADC_JSON_NUMERO, ADC_JSON_TEXTO, ADC_JSON_ARRAY, ADC_JSON_OBJETO
} ADC_JsonTipo;

typedef struct ADC_Json ADC_Json;
struct ADC_Json {
    ADC_JsonTipo tipo;
    bool         booleano;
    double       numero;
    ADC_Fatia    texto;       /* já sem aspas e sem escapes (copiado para a arena)            */
    ADC_Fatia    chave;       /* nome, quando este nó é membro de um objeto                   */
    ADC_Json*    filhos;      /* lista ligada dos membros (objeto) ou itens (array)           */
    ADC_Json*    proximo;
    int32_t      quantidade;
};

/* Lê o JSON inteiro. Devolve NULL e preenche *pErro (texto estático, pt-BR) e *pPosErro em falha. */
ADC_Json* adc_json_ler(ADC_Arena* pArena, ADC_Fatia pTexto, const char** pErro, int32_t* pPosErro);

/* Procura um membro pelo nome (só em objeto); NULL se ausente. */
const ADC_Json* adc_json_membro(const ADC_Json* pObj, const char* pNome);
/* Atalhos com padrão. */
ADC_Fatia adc_json_texto(const ADC_Json* pObj, const char* pNome, const char* pPadrao);
double    adc_json_numero(const ADC_Json* pObj, const char* pNome, double pPadrao);
bool      adc_json_bool(const ADC_Json* pObj, const char* pNome, bool pPadrao);

/* Escreve uma string JSON com aspas e escapes (UTF-8 passa cru; controles viram \uXXXX). */
void adc_json_escrever_texto(ADC_Buffer* pB, ADC_Fatia pTexto);

#endif
