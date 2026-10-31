#include "adc_comum.h"

static const char ALFABETO[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

void adc_base64_escrever(ADC_Buffer* pB, const uint8_t* pDados, int32_t pTamanho) {
    if (pTamanho <= 0) return;
    adc_buffer_reservar(pB, ((pTamanho + 2) / 3) * 4);
    int32_t i = 0;
    for (; i + 2 < pTamanho; i += 3) {
        uint32_t v = ((uint32_t)pDados[i] << 16) | ((uint32_t)pDados[i + 1] << 8) | (uint32_t)pDados[i + 2];
        adc_buffer_char(pB, ALFABETO[(v >> 18) & 63]); adc_buffer_char(pB, ALFABETO[(v >> 12) & 63]);
        adc_buffer_char(pB, ALFABETO[(v >> 6) & 63]); adc_buffer_char(pB, ALFABETO[v & 63]);
    }
    if (i < pTamanho) {
        uint32_t v = (uint32_t)pDados[i] << 16;
        if (i + 1 < pTamanho) v |= (uint32_t)pDados[i + 1] << 8;
        adc_buffer_char(pB, ALFABETO[(v >> 18) & 63]); adc_buffer_char(pB, ALFABETO[(v >> 12) & 63]);
        adc_buffer_char(pB, i + 1 < pTamanho ? ALFABETO[(v >> 6) & 63] : '=');
        adc_buffer_char(pB, '=');
    }
}
