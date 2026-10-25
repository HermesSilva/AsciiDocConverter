/*
 * Leitor de linhas com o pré-processador de linha: include:: (por callback), ifdef/ifndef/ifeval/endif.
 * Entrega linhas já sem CR e sem o '\n'; mantém uma janela de antevisão para o parser de blocos.
 * Nunca lê disco (IC-3).
 */
#ifndef ADC_LEITOR_H
#define ADC_LEITOR_H

#include "adc_comum.h"
#include "adc_ast.h"
#include "adc_atributos.h"
#include "adc_mensagens.h"
#include "../include/adc.h"

typedef struct ADC_Linha {
    ADC_Fatia  texto;      /* sem o terminador de linha                                        */
    int32_t    numero;     /* 1-based no arquivo de origem                                     */
    int32_t    arquivo;    /* índice em ADC_Leitor.arquivos (0 = documento principal)           */
    int32_t    deslocamento; /* posição do 1º byte no buffer do arquivo (para localização)     */
} ADC_Linha;

typedef struct ADC_Fonte {
    const char* dados;
    int32_t     tamanho;
    int32_t     pos;
    int32_t     linha;
    int32_t     arquivo;
    int32_t     nivel_offset;   /* leveloffset aplicado ao entrar neste include                 */
    int32_t     profundidade_condicional_ao_entrar;
    /* include com leveloffset: o Asciidoctor (push_include) cerca as linhas com ":leveloffset:" e uma linha vazia
       antes e depois — as vazias separam blocos na fronteira do include. Servidas como linhas sintéticas. */
    bool        vazia_antes, vazia_depois;
} ADC_Fonte;

#define ADC_LEITOR_MAX_FONTES 16
#define ADC_LEITOR_MAX_CONDICIONAIS 32
#define ADC_LEITOR_ANTEVISAO 64

typedef struct ADC_Leitor {
    ADC_Arena*            arena;
    ADC_TabelaAtributos*  atributos;
    ADC_Mensagens*        msgs;
    ADC_ResolverRecurso   resolver;
    void*                 contexto_resolver;
    ADC_Fonte             pilha[ADC_LEITOR_MAX_FONTES];
    int32_t               topo;                     /* -1 = sem fonte                           */
    ADC_Arquivo*          arquivos;                 /* lista: índice → caminho                   */
    int32_t               quantidade_arquivos;
    int32_t               includes_feitos;
    int32_t               includes_maximo;
    /* condicionais: pilha de "estou pulando?" */
    bool                  condicional[ADC_LEITOR_MAX_CONDICIONAIS];
    int32_t               profundidade_condicional;
    /* antevisão (fila circular) */
    ADC_Linha             fila[ADC_LEITOR_ANTEVISAO];
    int32_t               fila_inicio;
    int32_t               fila_tamanho;
    bool                  fim;
    int32_t               leveloffset;              /* soma dos leveloffset dos includes ativos  */
    /* modo vetor: linhas JÁ pré-processadas (o buffer de um item de lista, como o Reader do Asciidoctor sobre
       as linhas de read_lines_for_list_item). Servidas como estão, sem diretivas e sem limite de antevisão. */
    const ADC_Linha*      prontas;
    int32_t               n_prontas;
    int32_t               i_pronta;
} ADC_Leitor;

/* Abre um leitor sobre linhas já pré-processadas (número de linha e arquivo preservados). */
void adc_leitor_abrir_linhas(ADC_Leitor* pL, const ADC_Linha* pLinhas, int32_t pN);

void adc_leitor_iniciar(ADC_Leitor* pL, ADC_Arena* pArena, ADC_TabelaAtributos* pAtributos, ADC_Mensagens* pMsgs,
                        ADC_ResolverRecurso pResolver, void* pContexto, int32_t pIncludesMaximo);
void adc_leitor_abrir(ADC_Leitor* pL, ADC_Fatia pTexto, ADC_Fatia pCaminho);

/* Linha pN à frente (0 = a próxima a ser lida), ou NULL no fim. Ponteiro válido até o próximo avanço. */
const ADC_Linha* adc_leitor_espiar(ADC_Leitor* pL, int32_t pN);
/* Lê e consome a próxima linha; false no fim. */
bool adc_leitor_ler(ADC_Leitor* pL, ADC_Linha* pLinha);
/* Consome pN linhas. */
void adc_leitor_pular(ADC_Leitor* pL, int32_t pN);
bool adc_leitor_fim(ADC_Leitor* pL);
/* Caminho do arquivo de índice pIndice (vazio para o principal sem nome). */
ADC_Fatia adc_leitor_caminho(const ADC_Leitor* pL, int32_t pIndice);

#endif
