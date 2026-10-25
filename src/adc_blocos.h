/*
 * Parser de blocos: identifica a forma estrutural de cada bloco pelas linhas (SDR-3), delimita o bloco,
 * parseia os filhos (quando o modelo de conteúdo permite) e só então aplica o estilo e o parse inline.
 */
#ifndef ADC_BLOCOS_H
#define ADC_BLOCOS_H

#include "adc_contexto.h"
#include "adc_inlines.h"

/* Escopo de parse: até onde este nível vai. */
typedef struct ADC_Escopo {
    ADC_Fatia  fechamento;       /* linha delimitadora que encerra o pai (vazia = EOF)             */
    int32_t    nivel_secao;      /* nível da seção corrente (0 = documento)                        */
    bool       permite_secao;    /* só documento e seção                                           */
    bool       em_item_lista;    /* blocos anexados a item: '+' e marcadores interrompem           */
    ADC_Fatia  marcador_lista;   /* marcador da lista corrente (para detectar irmão/aninhado)       */
    bool       em_tabela;        /* célula AsciiDoc: '|' (separador) interrompe                     */
    char       separador_tabela;
    bool       buffer_item;      /* parse sobre o buffer de um item de lista (read_lines_for_list_item):
                                    linha vazia não encerra; parágrafo sem vazia antes para em linha de lista */
} ADC_Escopo;

/* Parseia blocos até o fim do escopo, anexando em pPai. */
void adc_blocos_parsear(ADC_Contexto* pC, ADC_Bloco* pPai, const ADC_Escopo* pEscopo);

/* Parseia exatamente um bloco (com seus metadados pendentes) — usado pelas listas. NULL se nada. */
ADC_Bloco* adc_blocos_parsear_um(ADC_Contexto* pC, ADC_Bloco* pPai, const ADC_Escopo* pEscopo, ADC_Metadados* pMeta);

/* Parseia o texto de uma célula AsciiDoc (ou de qualquer sub-documento) como blocos filhos de pPai. */
void adc_blocos_parsear_texto(ADC_Contexto* pC, ADC_Bloco* pPai, ADC_Fatia pTexto, int32_t pLinhaBase, int32_t pArquivo);

/* Tabelas (adc_tabelas.c). */
ADC_Bloco* adc_tabela_parsear(ADC_Contexto* pC, ADC_Bloco* pPai, ADC_Metadados* pMeta, ADC_Fatia pDelimitador, const ADC_Linha* pAbertura);

/* Utilitários compartilhados. */
bool adc_linha_eh_delimitador(ADC_Fatia pLinha, ADC_TipoBloco* pTipo, char* pChar);
bool adc_linha_eh_atributo_bloco(ADC_Fatia pLinha);
bool adc_linha_eh_item_lista(ADC_Fatia pLinha, ADC_Fatia* pMarcador, ADC_VarianteLista* pVariante, int32_t* pIniTexto);
bool adc_linha_eh_item_dlist(ADC_Fatia pLinha, ADC_Fatia* pTermo, ADC_Fatia* pMarcador, int32_t* pIniDesc);
bool adc_linha_eh_entrada_atributo(ADC_Fatia pLinha, ADC_Fatia* pNome, ADC_Fatia* pValor, bool* pRemover);
ADC_Inline* adc_parsear_texto_inline(ADC_Contexto* pC, ADC_Fatia pTexto, int32_t pLinha, int32_t pColuna, int32_t pArquivo, ADC_Subs pSubs);
void adc_aplicar_metadados(ADC_Contexto* pC, ADC_Bloco* pBloco, ADC_Metadados* pMeta);
/* Lê uma linha de atributos de bloco ([...] ou [[id]]) acumulando em pMeta. */
void adc_ler_linha_atributos(ADC_Contexto* pC, ADC_Metadados* pMeta, const ADC_Linha* pL);
void adc_definir_atributo_documento(ADC_Contexto* pC, ADC_Fatia pNome, ADC_Fatia pValor, bool pRemover, int32_t pLinha);

#endif
