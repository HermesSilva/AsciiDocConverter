/*
 * Infraestrutura comum: tipos básicos, fatia (ponteiro + tamanho), arena, buffer de saída, UTF-8.
 * Tudo aqui é independente do AsciiDoc.
 */
#ifndef ADC_COMUM_H
#define ADC_COMUM_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#if defined(_MSC_VER)
#  define ADC_INLINE static __inline
#else
#  define ADC_INLINE static inline
#endif

/* ---------------------------------------------------------------- fatia ---- */

/* Fatia de texto: aponta para memória de outro (a entrada ou a arena); nunca é dona. */
typedef struct ADC_Fatia {
    const char* dados;
    int32_t     tamanho;
} ADC_Fatia;

ADC_INLINE ADC_Fatia adc_fatia(const char* pDados, int32_t pTamanho) {
    ADC_Fatia f; f.dados = pDados; f.tamanho = pTamanho; return f;
}
ADC_INLINE ADC_Fatia adc_fatia_cstr(const char* pTexto) {
    return adc_fatia(pTexto, pTexto ? (int32_t)strlen(pTexto) : 0);
}
ADC_INLINE bool adc_fatia_igual(ADC_Fatia pA, const char* pB) {
    size_t n = strlen(pB);
    return (size_t)pA.tamanho == n && memcmp(pA.dados, pB, n) == 0;
}
ADC_INLINE bool adc_fatia_igual_f(ADC_Fatia pA, ADC_Fatia pB) {
    return pA.tamanho == pB.tamanho && memcmp(pA.dados, pB.dados, (size_t)pA.tamanho) == 0;
}
ADC_INLINE bool adc_fatia_comeca(ADC_Fatia pA, const char* pPrefixo) {
    size_t n = strlen(pPrefixo);
    return (size_t)pA.tamanho >= n && memcmp(pA.dados, pPrefixo, n) == 0;
}
ADC_INLINE bool adc_fatia_termina(ADC_Fatia pA, const char* pSufixo) {
    size_t n = strlen(pSufixo);
    return (size_t)pA.tamanho >= n && memcmp(pA.dados + pA.tamanho - n, pSufixo, n) == 0;
}
ADC_Fatia adc_fatia_aparar(ADC_Fatia pA);          /* remove espaços e tabs nas duas pontas  */
ADC_Fatia adc_fatia_aparar_direita(ADC_Fatia pA);
int32_t   adc_fatia_indice(ADC_Fatia pA, char pC);  /* -1 se não houver                       */
bool      adc_fatia_so_espacos(ADC_Fatia pA);
int32_t   adc_fatia_para_int(ADC_Fatia pA, int32_t pPadrao);

ADC_INLINE bool adc_eh_espaco(char pC) { return pC == ' ' || pC == '\t'; }
ADC_INLINE bool adc_eh_digito(char pC) { return pC >= '0' && pC <= '9'; }
ADC_INLINE bool adc_eh_alfa(char pC) { return (pC >= 'a' && pC <= 'z') || (pC >= 'A' && pC <= 'Z'); }
ADC_INLINE bool adc_eh_alnum(char pC) { return adc_eh_alfa(pC) || adc_eh_digito(pC); }
ADC_INLINE char adc_minusculo(char pC) { return (pC >= 'A' && pC <= 'Z') ? (char)(pC + 32) : pC; }

/* ---------------------------------------------------------------- arena ---- */

/* Um documento = uma arena = um free. Blocos de tamanho crescente; nunca realoca o que já entregou. */
typedef struct ADC_BlocoArena ADC_BlocoArena;
typedef struct ADC_Arena {
    ADC_BlocoArena* atual;
    size_t          tamanho_bloco;
    size_t          total;          /* bytes pedidos (para limites e diagnóstico) */
    bool            sem_memoria;    /* ficou true no primeiro malloc que falhou   */
} ADC_Arena;

void   adc_arena_iniciar(ADC_Arena* pArena, size_t pTamanhoBloco);
void*  adc_arena_alocar(ADC_Arena* pArena, size_t pTamanho);          /* zera a memória */
char*  adc_arena_copiar(ADC_Arena* pArena, const char* pDados, int32_t pTamanho); /* + '\0' */
char*  adc_arena_cstr(ADC_Arena* pArena, ADC_Fatia pFatia);
void   adc_arena_liberar(ADC_Arena* pArena);

#define ADC_NOVO(pArena, Tipo) ((Tipo*)adc_arena_alocar((pArena), sizeof(Tipo)))
#define ADC_NOVOS(pArena, Tipo, pN) ((Tipo*)adc_arena_alocar((pArena), sizeof(Tipo) * (size_t)(pN)))

/* --------------------------------------------------------------- buffer ---- */

/* Buffer dinâmico de bytes (saída dos renderizadores e strings em construção). Usa malloc/realloc:
 * a saída é devolvida ao chamador e liberada por ADC_Liberar, fora da arena. */
typedef struct ADC_Buffer {
    uint8_t* dados;
    int32_t  tamanho;
    int32_t  capacidade;
    bool     sem_memoria;
} ADC_Buffer;

void adc_buffer_iniciar(ADC_Buffer* pB);
void adc_buffer_reservar(ADC_Buffer* pB, int32_t pExtra);
void adc_buffer_bytes(ADC_Buffer* pB, const void* pDados, int32_t pTamanho);
void adc_buffer_fatia(ADC_Buffer* pB, ADC_Fatia pF);
void adc_buffer_texto(ADC_Buffer* pB, const char* pTexto);
void adc_buffer_char(ADC_Buffer* pB, char pC);
void adc_buffer_int(ADC_Buffer* pB, int64_t pValor);
void adc_buffer_decimal(ADC_Buffer* pB, double pValor, int pCasas);  /* ponto decimal, sem expoente */
void adc_buffer_repetir(ADC_Buffer* pB, char pC, int32_t pVezes);
void adc_buffer_truncar(ADC_Buffer* pB, int32_t pTamanho);
void adc_buffer_liberar(ADC_Buffer* pB);
/* Entrega a propriedade dos dados (com '\0' extra ao fim) e zera o buffer. */
uint8_t* adc_buffer_entregar(ADC_Buffer* pB, int32_t* pTamanho);
ADC_INLINE ADC_Fatia adc_buffer_fatia_de(const ADC_Buffer* pB) {
    return adc_fatia((const char*)pB->dados, pB->tamanho);
}

/* --------------------------------------------------------------- base64 ---- */

/* Escreve os bytes em base64 padrão (com '='), sem quebras de linha. */
void adc_base64_escrever(ADC_Buffer* pB, const uint8_t* pDados, int32_t pTamanho);

/* ----------------------------------------------------------------- utf8 ---- */

/* Valida UTF-8 estrito (sem sobrelongos, sem substitutos, ≤ U+10FFFF). Devolve -1 se válido, ou o
 * deslocamento do primeiro byte inválido. */
int32_t adc_utf8_validar(const uint8_t* pDados, int32_t pTamanho);
/* Decodifica um codepoint em pPos; avança *pPos. Entrada já validada. */
uint32_t adc_utf8_decodificar(const char* pDados, int32_t pTamanho, int32_t* pPos);
/* Codifica um codepoint; devolve o número de bytes escritos (1 a 4) em pSaida[4]. */
int32_t adc_utf8_codificar(uint32_t pCodepoint, char* pSaida);
/* Tamanho em bytes do codepoint que começa em pByte (1..4), ou 1 se byte inválido. */
int32_t adc_utf8_tamanho(unsigned char pByte);
/* Número de codepoints da fatia. */
int32_t adc_utf8_contar(ADC_Fatia pF);
/* Letra ou dígito Unicode (aproximação: ASCII alnum, ou qualquer codepoint ≥ 0x80 que seja letra
 * nos blocos Latin-1/Latin Extended/Greek/Cyrillic; outros blocos ≥ 0x80 contam como letra). É o
 * "alpha\d" da especificação para as marcas constrained. */
bool adc_utf8_alfa_digito_antes(const char* pDados, int32_t pPos);   /* codepoint que termina em pPos-1 */
bool adc_utf8_alfa_digito_em(const char* pDados, int32_t pTamanho, int32_t pPos);

#endif /* ADC_COMUM_H */
