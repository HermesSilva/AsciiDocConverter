#include "adc_json.h"
#include <stdlib.h>

typedef struct Leitor {
    ADC_Arena*  arena;
    const char* d;
    int32_t     n;
    int32_t     p;
    const char* erro;
    int32_t     profundidade;
} Leitor;

static void pular_espacos(Leitor* L) {
    while (L->p < L->n) {
        char c = L->d[L->p];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') L->p++; else break;
    }
}

static bool falha(Leitor* L, const char* pMsg) { if (!L->erro) L->erro = pMsg; return false; }

static int hex(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static bool ler_texto(Leitor* L, ADC_Fatia* pSaida) {
    if (L->p >= L->n || L->d[L->p] != '"') return falha(L, "esperava uma string entre aspas");
    L->p++;
    ADC_Buffer b; adc_buffer_iniciar(&b);
    while (L->p < L->n) {
        char c = L->d[L->p++];
        if (c == '"') {
            char* copia = adc_arena_copiar(L->arena, (const char*)b.dados, b.tamanho);
            int32_t tam = b.tamanho;
            adc_buffer_liberar(&b);
            if (!copia) return falha(L, "sem memória ao ler string");
            *pSaida = adc_fatia(copia, tam);
            return true;
        }
        if (c == '\\') {
            if (L->p >= L->n) break;
            char e = L->d[L->p++];
            switch (e) {
                case '"': adc_buffer_char(&b, '"'); break;
                case '\\': adc_buffer_char(&b, '\\'); break;
                case '/': adc_buffer_char(&b, '/'); break;
                case 'b': adc_buffer_char(&b, '\b'); break;
                case 'f': adc_buffer_char(&b, '\f'); break;
                case 'n': adc_buffer_char(&b, '\n'); break;
                case 'r': adc_buffer_char(&b, '\r'); break;
                case 't': adc_buffer_char(&b, '\t'); break;
                case 'u': {
                    if (L->p + 4 > L->n) { adc_buffer_liberar(&b); return falha(L, "escape \\u incompleto"); }
                    uint32_t cp = 0;
                    for (int k = 0; k < 4; k++) {
                        int h = hex(L->d[L->p + k]);
                        if (h < 0) { adc_buffer_liberar(&b); return falha(L, "escape \\u com dígito inválido"); }
                        cp = (cp << 4) | (uint32_t)h;
                    }
                    L->p += 4;
                    if (cp >= 0xD800 && cp <= 0xDBFF && L->p + 6 <= L->n && L->d[L->p] == '\\' && L->d[L->p + 1] == 'u') {
                        uint32_t baixo = 0; bool ok = true;
                        for (int k = 0; k < 4; k++) { int h = hex(L->d[L->p + 2 + k]); if (h < 0) { ok = false; break; } baixo = (baixo << 4) | (uint32_t)h; }
                        if (ok && baixo >= 0xDC00 && baixo <= 0xDFFF) {
                            cp = 0x10000 + ((cp - 0xD800) << 10) + (baixo - 0xDC00);
                            L->p += 6;
                        }
                    }
                    char tmp[4]; int32_t nb = adc_utf8_codificar(cp, tmp);
                    adc_buffer_bytes(&b, tmp, nb);
                    break;
                }
                default: adc_buffer_liberar(&b); return falha(L, "escape desconhecido na string");
            }
        } else {
            adc_buffer_char(&b, c);
        }
    }
    adc_buffer_liberar(&b);
    return falha(L, "string sem aspas de fechamento");
}

static ADC_Json* novo(Leitor* L, ADC_JsonTipo pTipo) {
    ADC_Json* j = ADC_NOVO(L->arena, ADC_Json);
    if (j) j->tipo = pTipo;
    return j;
}

static ADC_Json* ler_valor(Leitor* L);

static ADC_Json* ler_objeto(Leitor* L) {
    ADC_Json* obj = novo(L, ADC_JSON_OBJETO);
    if (!obj) { falha(L, "sem memória"); return NULL; }
    L->p++; /* { */
    ADC_Json* ultimo = NULL;
    pular_espacos(L);
    if (L->p < L->n && L->d[L->p] == '}') { L->p++; return obj; }
    for (;;) {
        pular_espacos(L);
        ADC_Fatia chave;
        if (!ler_texto(L, &chave)) return NULL;
        pular_espacos(L);
        if (L->p >= L->n || L->d[L->p] != ':') { falha(L, "esperava ':' após o nome do membro"); return NULL; }
        L->p++;
        ADC_Json* v = ler_valor(L);
        if (!v) return NULL;
        v->chave = chave;
        if (ultimo) ultimo->proximo = v; else obj->filhos = v;
        ultimo = v;
        obj->quantidade++;
        pular_espacos(L);
        if (L->p >= L->n) { falha(L, "objeto sem '}' de fechamento"); return NULL; }
        if (L->d[L->p] == ',') { L->p++; continue; }
        if (L->d[L->p] == '}') { L->p++; return obj; }
        falha(L, "esperava ',' ou '}' no objeto");
        return NULL;
    }
}

static ADC_Json* ler_array(Leitor* L) {
    ADC_Json* arr = novo(L, ADC_JSON_ARRAY);
    if (!arr) { falha(L, "sem memória"); return NULL; }
    L->p++; /* [ */
    ADC_Json* ultimo = NULL;
    pular_espacos(L);
    if (L->p < L->n && L->d[L->p] == ']') { L->p++; return arr; }
    for (;;) {
        ADC_Json* v = ler_valor(L);
        if (!v) return NULL;
        if (ultimo) ultimo->proximo = v; else arr->filhos = v;
        ultimo = v;
        arr->quantidade++;
        pular_espacos(L);
        if (L->p >= L->n) { falha(L, "array sem ']' de fechamento"); return NULL; }
        if (L->d[L->p] == ',') { L->p++; continue; }
        if (L->d[L->p] == ']') { L->p++; return arr; }
        falha(L, "esperava ',' ou ']' no array");
        return NULL;
    }
}

static bool literal(Leitor* L, const char* pPalavra) {
    int32_t n = (int32_t)strlen(pPalavra);
    if (L->p + n <= L->n && memcmp(L->d + L->p, pPalavra, (size_t)n) == 0) { L->p += n; return true; }
    return false;
}

static ADC_Json* ler_valor(Leitor* L) {
    pular_espacos(L);
    if (L->p >= L->n) { falha(L, "fim inesperado do JSON"); return NULL; }
    if (++L->profundidade > 64) { falha(L, "JSON aninhado além de 64 níveis"); return NULL; }
    ADC_Json* r = NULL;
    char c = L->d[L->p];
    if (c == '{') r = ler_objeto(L);
    else if (c == '[') r = ler_array(L);
    else if (c == '"') {
        ADC_Fatia t;
        if (ler_texto(L, &t)) { r = novo(L, ADC_JSON_TEXTO); if (r) r->texto = t; else falha(L, "sem memória"); }
    }
    else if (literal(L, "true")) { r = novo(L, ADC_JSON_BOOL); if (r) r->booleano = true; }
    else if (literal(L, "false")) { r = novo(L, ADC_JSON_BOOL); }
    else if (literal(L, "null")) { r = novo(L, ADC_JSON_NULO); }
    else if (c == '-' || adc_eh_digito(c)) {
        int32_t ini = L->p;
        if (c == '-') L->p++;
        while (L->p < L->n && (adc_eh_digito(L->d[L->p]) || L->d[L->p] == '.' || L->d[L->p] == 'e' || L->d[L->p] == 'E' || L->d[L->p] == '+' || L->d[L->p] == '-')) L->p++;
        char tmp[64]; int32_t n = L->p - ini;
        if (n >= (int32_t)sizeof tmp) { falha(L, "número longo demais"); return NULL; }
        memcpy(tmp, L->d + ini, (size_t)n); tmp[n] = 0;
        r = novo(L, ADC_JSON_NUMERO);
        if (r) r->numero = strtod(tmp, NULL); else falha(L, "sem memória");
    }
    else falha(L, "caractere inesperado no JSON");
    L->profundidade--;
    return r;
}

ADC_Json* adc_json_ler(ADC_Arena* pArena, ADC_Fatia pTexto, const char** pErro, int32_t* pPosErro) {
    Leitor L; L.arena = pArena; L.d = pTexto.dados; L.n = pTexto.tamanho; L.p = 0; L.erro = NULL; L.profundidade = 0;
    ADC_Json* r = ler_valor(&L);
    if (r) {
        pular_espacos(&L);
        if (L.p < L.n) { r = NULL; falha(&L, "conteúdo após o fim do valor JSON"); }
    }
    if (!r) {
        if (pErro) *pErro = L.erro ? L.erro : "JSON inválido";
        if (pPosErro) *pPosErro = L.p;
    }
    return r;
}

const ADC_Json* adc_json_membro(const ADC_Json* pObj, const char* pNome) {
    if (!pObj || pObj->tipo != ADC_JSON_OBJETO) return NULL;
    for (const ADC_Json* f = pObj->filhos; f; f = f->proximo)
        if (adc_fatia_igual(f->chave, pNome)) return f;
    return NULL;
}

ADC_Fatia adc_json_texto(const ADC_Json* pObj, const char* pNome, const char* pPadrao) {
    const ADC_Json* m = adc_json_membro(pObj, pNome);
    if (m && m->tipo == ADC_JSON_TEXTO) return m->texto;
    return adc_fatia_cstr(pPadrao);
}

double adc_json_numero(const ADC_Json* pObj, const char* pNome, double pPadrao) {
    const ADC_Json* m = adc_json_membro(pObj, pNome);
    if (m && m->tipo == ADC_JSON_NUMERO) return m->numero;
    return pPadrao;
}

bool adc_json_bool(const ADC_Json* pObj, const char* pNome, bool pPadrao) {
    const ADC_Json* m = adc_json_membro(pObj, pNome);
    if (m && m->tipo == ADC_JSON_BOOL) return m->booleano;
    return pPadrao;
}

void adc_json_escrever_texto(ADC_Buffer* pB, ADC_Fatia pTexto) {
    adc_buffer_char(pB, '"');
    for (int32_t i = 0; i < pTexto.tamanho; i++) {
        unsigned char c = (unsigned char)pTexto.dados[i];
        switch (c) {
            case '"': adc_buffer_texto(pB, "\\\""); break;
            case '\\': adc_buffer_texto(pB, "\\\\"); break;
            case '\n': adc_buffer_texto(pB, "\\n"); break;
            case '\r': adc_buffer_texto(pB, "\\r"); break;
            case '\t': adc_buffer_texto(pB, "\\t"); break;
            case '\b': adc_buffer_texto(pB, "\\b"); break;
            case '\f': adc_buffer_texto(pB, "\\f"); break;
            default:
                if (c < 0x20) {
                    char tmp[8]; snprintf(tmp, sizeof tmp, "\\u%04x", c);
                    adc_buffer_texto(pB, tmp);
                } else adc_buffer_char(pB, (char)c);
        }
    }
    adc_buffer_char(pB, '"');
}
