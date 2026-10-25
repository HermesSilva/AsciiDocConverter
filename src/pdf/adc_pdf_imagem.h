/*
 * Imagens para o PDF: PNG decodificado pela casa (filtros, paleta, alpha, Adam7) sobre o inflate do miniz;
 * JPEG passa cru (DCTDecode) lendo só o cabeçalho.
 */
#ifndef ADC_PDF_IMAGEM_H
#define ADC_PDF_IMAGEM_H

#include "../adc_comum.h"

typedef struct ADC_ImagemDecodificada {
    int32_t   largura, altura;
    int32_t   componentes;     /* 1 = cinza, 3 = RGB, 4 = CMYK (só JPEG)                          */
    uint8_t*  pixels;          /* componentes × largura × altura, 8 bits (malloc) — ou o JPEG cru */
    int32_t   tamanho_pixels;
    uint8_t*  alpha;           /* largura × altura, 8 bits (malloc) ou NULL                        */
    int32_t   tamanho_alpha;
    bool      dct;             /* true: pixels é o arquivo JPEG inteiro                           */
    bool      cmyk_invertido;  /* JPEG Adobe (APP14): CMYK invertido                               */
    const char* erro;          /* texto estático pt-BR em falha                                    */
} ADC_ImagemDecodificada;

/* Detecta o formato pelos bytes mágicos e decodifica. false em falha (pErro preenchido). */
bool adc_imagem_decodificar(const uint8_t* pDados, int32_t pTamanho, ADC_ImagemDecodificada* pSaida);
void adc_imagem_liberar(ADC_ImagemDecodificada* pI);

#endif
