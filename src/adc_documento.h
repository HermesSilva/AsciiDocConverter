/*
 * Montagem do documento: cabeçalho (título, autores, revisão, entradas de atributo), corpo e os
 * atributos intrínsecos. Entrada: texto UTF-8 válido. Saída: ADC_Contexto preenchido (documento, msgs).
 */
#ifndef ADC_DOCUMENTO_H
#define ADC_DOCUMENTO_H

#include "adc_contexto.h"

/* Prepara o contexto: arena, atributos (com os da API), leitor, limites. */
bool adc_doc_iniciar(ADC_Contexto* pC, ADC_Arena* pArena, const ADC_Json* pOpcoes, ADC_ResolverRecurso pResolver, void* pContexto, const char** pErroOpcoes);
/* Parseia o documento inteiro. */
void adc_doc_parsear(ADC_Contexto* pC, ADC_Fatia pTexto, ADC_Fatia pCaminho);
/* Só inlines (adapter do TCK): devolve a lista. */
ADC_Inline* adc_doc_parsear_inlines(ADC_Contexto* pC, ADC_Fatia pTexto);

#endif
