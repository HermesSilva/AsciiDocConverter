#include "adc_comum.h"
#include <stdlib.h>
#include <stdio.h>

/* ---------------------------------------------------------------- fatia ---- */

ADC_Fatia adc_fatia_aparar(ADC_Fatia pA) {
    while (pA.tamanho > 0 && adc_eh_espaco(pA.dados[0])) { pA.dados++; pA.tamanho--; }
    while (pA.tamanho > 0 && adc_eh_espaco(pA.dados[pA.tamanho - 1])) pA.tamanho--;
    return pA;
}

ADC_Fatia adc_fatia_aparar_direita(ADC_Fatia pA) {
    while (pA.tamanho > 0 && adc_eh_espaco(pA.dados[pA.tamanho - 1])) pA.tamanho--;
    return pA;
}

int32_t adc_fatia_indice(ADC_Fatia pA, char pC) {
    for (int32_t i = 0; i < pA.tamanho; i++) if (pA.dados[i] == pC) return i;
    return -1;
}

bool adc_fatia_so_espacos(ADC_Fatia pA) {
    for (int32_t i = 0; i < pA.tamanho; i++) if (!adc_eh_espaco(pA.dados[i])) return false;
    return true;
}

int32_t adc_fatia_para_int(ADC_Fatia pA, int32_t pPadrao) {
    pA = adc_fatia_aparar(pA);
    if (pA.tamanho == 0 || pA.tamanho > 9) return pPadrao;
    int32_t v = 0, i = 0; bool neg = false;
    if (pA.dados[0] == '-') { neg = true; i = 1; }
    else if (pA.dados[0] == '+') i = 1;   /* leveloffset=+1, sectnumlevels=+... : o sinal explícito vale */
    if (i >= pA.tamanho) return pPadrao;
    for (; i < pA.tamanho; i++) {
        if (!adc_eh_digito(pA.dados[i])) return pPadrao;
        v = v * 10 + (pA.dados[i] - '0');
    }
    return neg ? -v : v;
}

/* ---------------------------------------------------------------- arena ---- */

struct ADC_BlocoArena {
    ADC_BlocoArena* anterior;
    size_t          usado;
    size_t          capacidade;
    /* dados seguem */
};

void adc_arena_iniciar(ADC_Arena* pArena, size_t pTamanhoBloco) {
    pArena->atual = NULL;
    pArena->tamanho_bloco = pTamanhoBloco < 4096 ? 4096 : pTamanhoBloco;
    pArena->total = 0;
    pArena->sem_memoria = false;
}

static ADC_BlocoArena* adc_arena_novo_bloco(ADC_Arena* pArena, size_t pMinimo) {
    size_t cap = pArena->tamanho_bloco;
    if (pArena->atual && pArena->atual->capacidade * 2 > cap) cap = pArena->atual->capacidade * 2;
    if (cap < pMinimo) cap = pMinimo;
    if (cap > (size_t)64 * 1024 * 1024) cap = cap < pMinimo ? pMinimo : cap; /* só cresce até 64 MB por bloco */
    ADC_BlocoArena* b = (ADC_BlocoArena*)malloc(sizeof(ADC_BlocoArena) + cap);
    if (!b) { pArena->sem_memoria = true; return NULL; }
    b->anterior = pArena->atual;
    b->usado = 0;
    b->capacidade = cap;
    pArena->atual = b;
    return b;
}

void* adc_arena_alocar(ADC_Arena* pArena, size_t pTamanho) {
    if (pTamanho == 0) pTamanho = 1;
    pTamanho = (pTamanho + 7u) & ~(size_t)7u;  /* alinhamento de 8 */
    ADC_BlocoArena* b = pArena->atual;
    if (!b || b->usado + pTamanho > b->capacidade) {
        b = adc_arena_novo_bloco(pArena, pTamanho);
        if (!b) return NULL;
    }
    char* dados = (char*)(b + 1) + b->usado;
    b->usado += pTamanho;
    pArena->total += pTamanho;
    memset(dados, 0, pTamanho);
    return dados;
}

char* adc_arena_copiar(ADC_Arena* pArena, const char* pDados, int32_t pTamanho) {
    char* c = (char*)adc_arena_alocar(pArena, (size_t)pTamanho + 1);
    if (!c) return NULL;
    if (pTamanho > 0) memcpy(c, pDados, (size_t)pTamanho);
    c[pTamanho] = '\0';
    return c;
}

char* adc_arena_cstr(ADC_Arena* pArena, ADC_Fatia pFatia) {
    return adc_arena_copiar(pArena, pFatia.dados, pFatia.tamanho);
}

void adc_arena_liberar(ADC_Arena* pArena) {
    ADC_BlocoArena* b = pArena->atual;
    while (b) { ADC_BlocoArena* ant = b->anterior; free(b); b = ant; }
    pArena->atual = NULL;
    pArena->total = 0;
}

/* --------------------------------------------------------------- buffer ---- */

void adc_buffer_iniciar(ADC_Buffer* pB) {
    pB->dados = NULL; pB->tamanho = 0; pB->capacidade = 0; pB->sem_memoria = false;
}

void adc_buffer_reservar(ADC_Buffer* pB, int32_t pExtra) {
    if (pB->sem_memoria) return;
    int64_t precisa = (int64_t)pB->tamanho + pExtra + 1;
    if (precisa <= pB->capacidade) return;
    int64_t nova = pB->capacidade ? (int64_t)pB->capacidade * 2 : 256;
    while (nova < precisa) nova *= 2;
    if (nova > INT32_MAX) { pB->sem_memoria = true; return; }
    uint8_t* d = (uint8_t*)realloc(pB->dados, (size_t)nova);
    if (!d) { pB->sem_memoria = true; return; }
    pB->dados = d;
    pB->capacidade = (int32_t)nova;
}

void adc_buffer_bytes(ADC_Buffer* pB, const void* pDados, int32_t pTamanho) {
    if (pTamanho <= 0) return;
    adc_buffer_reservar(pB, pTamanho);
    if (pB->sem_memoria) return;
    memcpy(pB->dados + pB->tamanho, pDados, (size_t)pTamanho);
    pB->tamanho += pTamanho;
}

void adc_buffer_fatia(ADC_Buffer* pB, ADC_Fatia pF) { adc_buffer_bytes(pB, pF.dados, pF.tamanho); }
void adc_buffer_texto(ADC_Buffer* pB, const char* pTexto) { adc_buffer_bytes(pB, pTexto, (int32_t)strlen(pTexto)); }

void adc_buffer_char(ADC_Buffer* pB, char pC) {
    adc_buffer_reservar(pB, 1);
    if (pB->sem_memoria) return;
    pB->dados[pB->tamanho++] = (uint8_t)pC;
}

void adc_buffer_int(ADC_Buffer* pB, int64_t pValor) {
    char tmp[24];
    int n = snprintf(tmp, sizeof tmp, "%lld", (long long)pValor);
    adc_buffer_bytes(pB, tmp, n);
}

void adc_buffer_decimal(ADC_Buffer* pB, double pValor, int pCasas) {
    char tmp[64];
    int n = snprintf(tmp, sizeof tmp, "%.*f", pCasas, pValor);
    /* remove zeros à direita e o ponto solto: "12.500" -> "12.5", "3.000" -> "3" */
    if (pCasas > 0) {
        while (n > 0 && tmp[n - 1] == '0') n--;
        if (n > 0 && tmp[n - 1] == '.') n--;
    }
    if (n == 2 && tmp[0] == '-' && tmp[1] == '0') { tmp[0] = '0'; n = 1; }
    adc_buffer_bytes(pB, tmp, n);
}

void adc_buffer_repetir(ADC_Buffer* pB, char pC, int32_t pVezes) {
    for (int32_t i = 0; i < pVezes; i++) adc_buffer_char(pB, pC);
}

void adc_buffer_truncar(ADC_Buffer* pB, int32_t pTamanho) {
    if (pTamanho >= 0 && pTamanho < pB->tamanho) pB->tamanho = pTamanho;
}

void adc_buffer_liberar(ADC_Buffer* pB) {
    free(pB->dados);
    adc_buffer_iniciar(pB);
}

uint8_t* adc_buffer_entregar(ADC_Buffer* pB, int32_t* pTamanho) {
    adc_buffer_reservar(pB, 1);
    if (pB->sem_memoria) { *pTamanho = 0; return NULL; }
    if (!pB->dados) {
        pB->dados = (uint8_t*)malloc(1);
        if (!pB->dados) { *pTamanho = 0; return NULL; }
    }
    pB->dados[pB->tamanho] = 0;
    uint8_t* d = pB->dados;
    *pTamanho = pB->tamanho;
    adc_buffer_iniciar(pB);
    return d;
}

/* ----------------------------------------------------------------- utf8 ---- */

int32_t adc_utf8_tamanho(unsigned char pByte) {
    if (pByte < 0x80) return 1;
    if ((pByte & 0xE0) == 0xC0) return 2;
    if ((pByte & 0xF0) == 0xE0) return 3;
    if ((pByte & 0xF8) == 0xF0) return 4;
    return 1;
}

int32_t adc_utf8_validar(const uint8_t* pDados, int32_t pTamanho) {
    int32_t i = 0;
    while (i < pTamanho) {
        uint8_t b = pDados[i];
        if (b < 0x80) { i++; continue; }
        int32_t n; uint32_t cp, min;
        if ((b & 0xE0) == 0xC0) { n = 2; cp = b & 0x1F; min = 0x80; }
        else if ((b & 0xF0) == 0xE0) { n = 3; cp = b & 0x0F; min = 0x800; }
        else if ((b & 0xF8) == 0xF0) { n = 4; cp = b & 0x07; min = 0x10000; }
        else return i;
        if (i + n > pTamanho) return i;
        for (int32_t k = 1; k < n; k++) {
            uint8_t c = pDados[i + k];
            if ((c & 0xC0) != 0x80) return i;
            cp = (cp << 6) | (c & 0x3F);
        }
        if (cp < min || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return i;
        i += n;
    }
    return -1;
}

uint32_t adc_utf8_decodificar(const char* pDados, int32_t pTamanho, int32_t* pPos) {
    int32_t i = *pPos;
    unsigned char b = (unsigned char)pDados[i];
    int32_t n = adc_utf8_tamanho(b);
    if (n == 1 || i + n > pTamanho) { *pPos = i + 1; return b; }
    uint32_t cp = (n == 2) ? (b & 0x1F) : (n == 3) ? (b & 0x0F) : (b & 0x07);
    for (int32_t k = 1; k < n; k++) cp = (cp << 6) | ((unsigned char)pDados[i + k] & 0x3F);
    *pPos = i + n;
    return cp;
}

int32_t adc_utf8_codificar(uint32_t pCp, char* pSaida) {
    if (pCp < 0x80) { pSaida[0] = (char)pCp; return 1; }
    if (pCp < 0x800) { pSaida[0] = (char)(0xC0 | (pCp >> 6)); pSaida[1] = (char)(0x80 | (pCp & 0x3F)); return 2; }
    if (pCp < 0x10000) {
        pSaida[0] = (char)(0xE0 | (pCp >> 12)); pSaida[1] = (char)(0x80 | ((pCp >> 6) & 0x3F));
        pSaida[2] = (char)(0x80 | (pCp & 0x3F)); return 3;
    }
    pSaida[0] = (char)(0xF0 | (pCp >> 18)); pSaida[1] = (char)(0x80 | ((pCp >> 12) & 0x3F));
    pSaida[2] = (char)(0x80 | ((pCp >> 6) & 0x3F)); pSaida[3] = (char)(0x80 | (pCp & 0x3F)); return 4;
}

int32_t adc_utf8_contar(ADC_Fatia pF) {
    int32_t n = 0;
    for (int32_t i = 0; i < pF.tamanho; i++) if (((unsigned char)pF.dados[i] & 0xC0) != 0x80) n++;
    return n;
}

static bool adc_cp_alfa_digito(uint32_t pCp) {
    if (pCp < 0x80) return adc_eh_alnum((char)pCp);
    /* Latin-1 Supplement: letras de U+00C0 a U+00FF, exceto × (D7) e ÷ (F7); símbolos abaixo de C0 não */
    if (pCp >= 0xC0 && pCp <= 0xFF) return pCp != 0xD7 && pCp != 0xF7;
    if (pCp >= 0xAA && pCp <= 0xBA) return pCp == 0xAA || pCp == 0xB5 || pCp == 0xBA;
    if (pCp < 0xC0) return false;
    /* Pontuação geral e símbolos: não são letras */
    if (pCp >= 0x2000 && pCp <= 0x2BFF) return false;
    if (pCp >= 0x3000 && pCp <= 0x303F) return false;
    if (pCp >= 0xFF00 && pCp <= 0xFF0F) return false;
    return true;
}

bool adc_utf8_alfa_digito_em(const char* pDados, int32_t pTamanho, int32_t pPos) {
    if (pPos < 0 || pPos >= pTamanho) return false;
    int32_t p = pPos;
    return adc_cp_alfa_digito(adc_utf8_decodificar(pDados, pTamanho, &p));
}

bool adc_utf8_alfa_digito_antes(const char* pDados, int32_t pPos) {
    if (pPos <= 0) return false;
    int32_t i = pPos - 1;
    while (i > 0 && ((unsigned char)pDados[i] & 0xC0) == 0x80 && pPos - i < 4) i--;
    int32_t p = i;
    return adc_cp_alfa_digito(adc_utf8_decodificar(pDados, pPos, &p));
}
