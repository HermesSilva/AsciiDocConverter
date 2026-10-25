/*
 * Fonte TrueType para o PDF: leitura (stb_truetype) de métricas e cmap, marcação dos glifos usados e
 * geração do subconjunto (tabelas head/hhea/maxp/hmtx/loca/glyf, glifos não usados zerados — os GIDs
 * originais ficam estáveis, por isso o PDF usa Identity-H + CIDToGIDMap /Identity).
 */
#ifndef ADC_PDF_FONTE_H
#define ADC_PDF_FONTE_H

#include "../adc_comum.h"

#define ADC_PDF_MAX_FONTES 16

typedef struct ADC_PdfFonte {
    const uint8_t* dados;            /* TTF completo (memória da casa, embutida no binário)       */
    int32_t        tamanho;
    const char*    nome_pdf;         /* BaseFont, ex.: "LiberationSans-Bold"                       */
    void*          info;             /* stbtt_fontinfo*                                            */
    int32_t        unidades_por_em;
    int32_t        n_glifos;
    double         ascender, descender, altura_linha;   /* em unidades/1000 (escala PDF)           */
    double         cap_height, x_height;
    double         bbox[4];
    uint8_t*       usado;            /* bitmap de glifos usados                                   */
    int32_t        objeto;           /* Type0 no PDF (preenchido ao escrever)                      */
    bool           negrito, italico, mono;
    ADC_Arena*     arena;
} ADC_PdfFonte;

/* Carrega a fonte (não copia os bytes). false se o TTF não é reconhecido. */
bool    adc_pdf_fonte_carregar(ADC_PdfFonte* pF, ADC_Arena* pArena, const uint8_t* pDados, int32_t pTamanho, const char* pNomePdf);
/* GID do codepoint (0 = .notdef). */
int32_t adc_pdf_fonte_gid(const ADC_PdfFonte* pF, uint32_t pCodepoint);
/* Avanço do glifo em unidades/1000. */
double  adc_pdf_fonte_avanco(const ADC_PdfFonte* pF, int32_t pGid);
/* Largura de um texto UTF-8 em pontos para o tamanho dado (sem kerning). Marca glifos se pMarcar. */
double  adc_pdf_fonte_largura(ADC_PdfFonte* pF, ADC_Fatia pTextoUTF8, double pTamanho, bool pMarcar);
/* Marca um glifo como usado (e seus componentes compostos, ao subconjuntar). */
void    adc_pdf_fonte_marcar(ADC_PdfFonte* pF, int32_t pGid);
/* Gera o subconjunto TTF em pSaida (malloc; chamador libera). false em falha. */
bool    adc_pdf_fonte_subconjunto(ADC_PdfFonte* pF, ADC_Buffer* pSaida);
/* Escreve o array /W (larguras dos glifos usados) no formato do PDF. */
void    adc_pdf_fonte_escrever_W(ADC_PdfFonte* pF, ADC_Buffer* pB);
/* Escreve o CMap ToUnicode (bfchar) dos glifos usados. */
void    adc_pdf_fonte_escrever_tounicode(ADC_PdfFonte* pF, ADC_Buffer* pB);
void    adc_pdf_fonte_liberar(ADC_PdfFonte* pF);

#endif
