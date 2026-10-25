/*
 * Parser de attrlist (SDR-2): o texto entre '[' e ']' de uma linha de atributos de bloco ou de um macro.
 * Entrada já com referências {atributo} resolvidas pelo chamador (modo atributos do pré-processador).
 *
 * Produz a lista nome → valor: posicionais em "$1", "$2", ...; nomeados pelo nome; role e opts
 * acumulam. Se pAtalhos for true, o primeiro posicional é lido como atalhos:
 *     [idname,reftext]stylename#idname.role1.role2%opt1%opt2
 * e vira style/id/reftext/role/opts (a forma [[id,reftext]] da âncora de bloco é tratada pelo chamador).
 */
#ifndef ADC_ATTRLIST_H
#define ADC_ATTRLIST_H

#include "adc_comum.h"
#include "adc_ast.h"

/* Parseia a attrlist em uma nova lista. pMeta (opcional) recebe id/estilo/papéis/opções/título/reftext
 * já consolidados — acumulando sobre o que já tinha (várias linhas de atributo para o mesmo bloco). */
ADC_Atributo* adc_attrlist_parsear(ADC_Arena* pArena, ADC_Fatia pTexto, bool pAtalhos, ADC_Metadados* pMeta);

/* Funde pNovos em pMeta->atributos (último vence; role/opts acumulam; posicional vazio não substitui). */
void adc_attrlist_fundir(ADC_Arena* pArena, ADC_Metadados* pMeta, ADC_Atributo* pNovos);

/* Lê os atalhos de um valor (style#id.role%opt). Devolve false se o texto não os obedece (então o valor
 * é guardado como veio). */
bool adc_attrlist_atalhos(ADC_Arena* pArena, ADC_Fatia pValor, ADC_Metadados* pMeta);

/* Mapeia posicionais para nomes (SDR-6): pNomes é um array terminado em NULL ("alt","width","height"). */
void adc_attrlist_mapear_posicionais(ADC_Arena* pArena, ADC_Atributo** pLista, const char* const* pNomes);

#endif
