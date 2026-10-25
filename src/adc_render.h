/*
 * Renderizadores: cada um nasce da AST, nunca de outro formato (IC-1).
 */
#ifndef ADC_RENDER_H
#define ADC_RENDER_H

#include "adc_contexto.h"

/* ASG JSON (esquema draft-01). */
void adc_asg_escrever_documento(ADC_Contexto* pC, ADC_Buffer* pSaida);
void adc_asg_escrever_inlines(ADC_Contexto* pC, const ADC_Inline* pInlines, ADC_Buffer* pSaida);

/* HTML5 (estrutura do Asciidoctor). */
void adc_html5_escrever(ADC_Contexto* pC, ADC_Buffer* pSaida);
/* Escapa &, <, > (os caracteres especiais do HTML). */
void adc_html_escapar(ADC_Buffer* pB, ADC_Fatia pTexto);
/* Escapa também aspas (para atributos). */
void adc_html_escapar_atributo(ADC_Buffer* pB, ADC_Fatia pTexto);

/* Markdown (GFM). */
void adc_markdown_escrever(ADC_Contexto* pC, ADC_Buffer* pSaida);

/* PDF (escritor próprio). Devolve false em falha (mensagem já registrada). */
bool adc_pdf_escrever(ADC_Contexto* pC, ADC_Buffer* pSaida);

/* Utilidades compartilhadas pelos renderizadores. */
/* Numeração de seção "1.2.3" escrita em pB (sectnums). */
void adc_render_numero_secao(ADC_Buffer* pB, const int32_t* pContadores, int32_t pNivel);
/* Legenda "Figura 1. " etc. — devolve true se escreveu. */
bool adc_render_legenda(ADC_Contexto* pC, const ADC_Bloco* pB, ADC_Buffer* pSaida, const char* pAtributo, int32_t* pContador);
/* Sumário ligado no cabeçalho (:toc: lido até o fim do cabeçalho, ou pela API)? pValor recebe o valor (pode ser NULL). */
static inline bool adc_render_toc(const ADC_Contexto* pC, ADC_Fatia* pValor) { if (pValor) *pValor = pC->toc_valor; return pC->toc_ativo; }
/* Texto do alvo de um xref (reftext/título registrado), ou NULL. */
const ADC_Inline* adc_render_texto_xref(ADC_Contexto* pC, ADC_Fatia pAlvo);/* Rótulo da admonição conforme o atributo <variante>-caption. */
ADC_Fatia adc_render_rotulo_admonicao(ADC_Contexto* pC, ADC_VarianteAdm pV);

#endif
