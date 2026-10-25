#include "adc_inlines.h"
#include "adc_attrlist.h"
#include <stdio.h>
#include <stdlib.h>

/* ------------------------------------------------------------- origem ---- */

ADC_Origem adc_origem_linha(ADC_Segmento* pSeg, int32_t pLinha, int32_t pColuna, int32_t pArquivo, const int32_t* pMapa, int32_t pTamanhoOriginal) {
    pSeg->inicio = 0; pSeg->linha = pLinha; pSeg->coluna = pColuna; pSeg->arquivo = pArquivo;
    ADC_Origem o; o.segmentos = pSeg; o.quantidade = 1; o.mapa = pMapa; o.tamanho_original = pTamanhoOriginal;
    return o;
}

ADC_Posicao adc_origem_posicao(const ADC_Origem* pO, ADC_Fatia pTextoOriginal, int32_t pDesl, int32_t* pArquivo) {
    ADC_Posicao pos; pos.linha = 0; pos.coluna = 0;
    if (pArquivo) *pArquivo = 0;
    if (!pO || pO->quantidade == 0) return pos;
    int32_t s = 0;
    while (s + 1 < pO->quantidade && pO->segmentos[s + 1].inicio <= pDesl) s++;
    const ADC_Segmento* seg = &pO->segmentos[s];
    pos.linha = seg->linha;
    /* coluna em caracteres: conta codepoints entre o início do segmento e o deslocamento */
    int32_t bytes = pDesl - seg->inicio;
    int32_t chars = 0;
    const char* d = pTextoOriginal.dados + seg->inicio;
    int32_t lim = pDesl <= pTextoOriginal.tamanho ? bytes : pTextoOriginal.tamanho - seg->inicio;
    for (int32_t i = 0; i < lim; i++) if (((unsigned char)d[i] & 0xC0) != 0x80) chars++;
    pos.coluna = seg->coluna + chars;
    if (pArquivo) *pArquivo = seg->arquivo;
    return pos;
}

/* ------------------------------------------------------------- parser ---- */

typedef struct Parser {
    ADC_Contexto*     ctx;
    const char*       d;        /* texto (pré-processado) */
    int32_t           n;
    ADC_Subs          subs;
    int32_t           profundidade;
    ADC_Fatia         texto_original;
    const ADC_Origem* origem;
    bool              so_marcas;   /* ADC_SUBS_VERBATIM_MARCAS: só o grupo quotes do Asciidoctor */
} Parser;

ADC_Inline* adc_inline_texto(ADC_Arena* pArena, ADC_Fatia pValor) {
    ADC_Inline* i = ADC_NOVO(pArena, ADC_Inline);
    if (!i) return NULL;
    i->tipo = ADC_IN_TEXTO; i->valor = pValor;
    return i;
}

static ADC_Inline* novo(Parser* P, ADC_TipoInline pTipo, int32_t pIni, int32_t pFim) {
    ADC_Inline* i = ADC_NOVO(P->ctx->arena, ADC_Inline);
    if (!i) return NULL;
    i->tipo = pTipo; i->desl_inicio = pIni; i->desl_fim = pFim;
    return i;
}

static ADC_Inline* texto(Parser* P, int32_t pIni, int32_t pFim) {
    ADC_Inline* t = novo(P, ADC_IN_TEXTO, pIni, pFim);
    if (t) t->valor = adc_fatia(P->d + pIni, pFim - pIni);
    return t;
}

static ADC_Inline* charref(Parser* P, const char* pEntidade, int32_t pIni, int32_t pFim) {
    ADC_Inline* c = novo(P, ADC_IN_CHARREF, pIni, pFim);
    if (c) c->valor = adc_fatia_cstr(pEntidade);
    return c;
}

typedef struct Lista { ADC_Inline* primeiro; ADC_Inline* ultimo; } Lista;

static void anexar(Lista* L, ADC_Inline* pI) {
    if (!pI) return;
    pI->proximo = NULL;
    if (L->ultimo) L->ultimo->proximo = pI; else L->primeiro = pI;
    L->ultimo = pI;
}

/* texto acumulado: [ini, fim) pendente */
typedef struct Acum { int32_t ini; int32_t fim; bool ativo; } Acum;

static void descarregar(Parser* P, Lista* L, Acum* A) {
    if (A->ativo && A->fim > A->ini) anexar(L, texto(P, A->ini, A->fim));
    A->ativo = false;
}

static void acumular(Acum* A, int32_t pIni, int32_t pFim) {
    if (!A->ativo) { A->ini = pIni; A->fim = pFim; A->ativo = true; }
    else if (A->fim == pIni) A->fim = pFim;
    else { /* não contíguo: o chamador deve ter descarregado */ A->ini = pIni; A->fim = pFim; }
}

ADC_INLINE bool em(Parser* P, int32_t p, char c) { return p >= 0 && p < P->n && P->d[p] == c; }
ADC_INLINE bool espaco_em(Parser* P, int32_t p) { return p >= 0 && p < P->n && (P->d[p] == ' ' || P->d[p] == '\t' || P->d[p] == '\n'); }
ADC_INLINE bool alfa_d_em(Parser* P, int32_t p) { return adc_utf8_alfa_digito_em(P->d, P->n, p); }
ADC_INLINE bool alfa_d_antes(Parser* P, int32_t p) { return adc_utf8_alfa_digito_antes(P->d, p); }
ADC_INLINE bool comeca_em(Parser* P, int32_t p, const char* s) { size_t n = strlen(s); return p + (int32_t)n <= P->n && memcmp(P->d + p, s, n) == 0; }

static ADC_Inline* parsear_faixa(Parser* P, int32_t pIni, int32_t pFim);

/* Procura ']' que fecha um '[' aberto em pAbre, respeitando aninhamento e escapes. -1 se não houver. */
static int32_t achar_fecha_colchete(Parser* P, int32_t pAbre, int32_t pLimite) {
    int32_t prof = 0;
    for (int32_t i = pAbre; i < pLimite; i++) {
        char c = P->d[i];
        if (c == '\\' && i + 1 < pLimite) { i++; continue; }
        if (c == '[') prof++;
        else if (c == ']') { prof--; if (prof == 0) return i; }
        else if (c == '\n') { /* attrlist inline não cruza linha? Asciidoctor permite; mantemos */ }
    }
    return -1;
}

/* Texto de link (macro link: ou URL[...]): como no Asciidoctor, só há atributos se o texto contém '='; aí a attrlist
   é parseada inteira e o rótulo é o 1º posicional (vazio = usa o alvo). Sem '=', o texto todo — com vírgulas — é o
   rótulo ("Understanding UEFI, Secure Boot, and TPM"). */
static ADC_Fatia separar_texto_link(ADC_Arena* pArena, ADC_Fatia pTxt, ADC_Atributo** pAtributos) {
    *pAtributos = NULL;
    if (adc_fatia_indice(pTxt, '=') < 0) return pTxt;
    *pAtributos = adc_attrlist_parsear(pArena, pTxt, false, NULL);
    bool a; ADC_Fatia rot = adc_atributo_obter(*pAtributos, "$1", &a);
    return a ? rot : adc_fatia(NULL, 0);
}

/* Passthrough que começa em q (pass:[...], pass:q[...], +++...+++, ++...++, +...+ constrained): devolve o índice
   logo após o fim, ou -1. O Asciidoctor extrai os passthroughs ANTES das marcas de formatação, então o conteúdo
   deles nunca fecha um *, _ ou ` de fora (ex.: *`pass:[*]`*). */
static int32_t fim_passthrough(Parser* P, int32_t q, int32_t pFim) {
    const char* d = P->d;
    if (q + 5 < pFim && memcmp(d + q, "pass:", 5) == 0 && !(q > 0 && (adc_eh_alnum(d[q - 1]) || d[q - 1] == '\\'))) {
        int32_t k = q + 5;
        while (k < pFim && (adc_eh_alfa(d[k]) || d[k] == ',')) k++;
        if (k < pFim && d[k] == '[') { int32_t f = achar_fecha_colchete(P, k, pFim); if (f > 0) return f + 1; }
        return -1;
    }
    if (d[q] != '+' || (q > 0 && d[q - 1] == '\\')) return -1;
    if (q + 2 < pFim && d[q + 1] == '+' && d[q + 2] == '+') {
        for (int32_t k = q + 3; k + 2 < pFim; k++) if (d[k] == '+' && d[k + 1] == '+' && d[k + 2] == '+') return k + 3;
        return -1;
    }
    if (q + 1 < pFim && d[q + 1] == '+') {
        for (int32_t k = q + 2; k + 1 < pFim; k++) if (d[k] == '+' && d[k + 1] == '+') return k + 2;
        return -1;
    }
    /* constrained: não colado a letra/dígito antes, conteúdo sem espaço nas pontas, fecho não seguido de letra/dígito */
    if (q > 0 && adc_eh_alnum(d[q - 1])) return -1;
    if (q + 1 >= pFim || espaco_em(P, q + 1)) return -1;
    for (int32_t k = q + 1; k < pFim; k++) {
        if (d[k] == '\n') return -1;
        if (d[k] == '+' && !espaco_em(P, k - 1) && k > q + 1 && !alfa_d_em(P, k + 1)) return k + 1;
    }
    return -1;
}

/* Procura marca de fechamento constrained a partir de pDe (exclusive do conteúdo que começa em pDe). */
static int32_t achar_fecho_constrained(Parser* P, char pMarca, int32_t pDe, int32_t pFim) {
    for (int32_t q = pDe; q < pFim; q++) {
        /* o próprio passthrough +...+ procura o seu '+' de fecho aqui: nele não se salta passthrough */
        if (pMarca != '+') { int32_t fp = fim_passthrough(P, q, pFim); if (fp > q) { q = fp - 1; continue; } }
        char c = P->d[q];
        if (c == pMarca) {
            /* salta um par unconstrained interno da mesma marca: **...** */
            if (q + 1 < pFim && P->d[q + 1] == pMarca && q > pDe) {
                /* se este '**' abre um unconstrained interno, pula até o seu fecho */
                int32_t f = -1;
                for (int32_t r = q + 2; r + 1 < pFim; r++) { if (P->d[r] == pMarca && P->d[r + 1] == pMarca) { f = r; break; } }
                if (f > 0) { q = f + 1; continue; }
            }
            if (q == pDe) continue;                 /* conteúdo vazio */
            if (espaco_em(P, q - 1)) continue;      /* espaço antes da marca de fecho */
            if (alfa_d_em(P, q + 1)) continue;      /* seguida de letra/dígito */
            return q;
        }
    }
    return -1;
}

static int32_t achar_fecho_unconstrained(Parser* P, char pMarca, int32_t pDe, int32_t pFim) {
    for (int32_t q = pDe; q + 1 < pFim; q++) {
        if (pMarca != '+') { int32_t fp = fim_passthrough(P, q, pFim); if (fp > q) { q = fp - 1; continue; } }
        if (P->d[q] == pMarca && P->d[q + 1] == pMarca && q > pDe) return q;
    }
    return -1;
}

static ADC_VarianteSpan variante_de(char c) {
    switch (c) { case '*': return ADC_SPAN_STRONG; case '_': return ADC_SPAN_EMPHASIS; case '`': return ADC_SPAN_CODE; case '#': return ADC_SPAN_MARK; case '^': return ADC_SPAN_SUPERSCRIPT; case '~': return ADC_SPAN_SUBSCRIPT; default: return ADC_SPAN_STRONG; }
}

/* attrlist inline de span: [.role#id] imediatamente antes da marca. Devolve o nó se casou. */
static ADC_Inline* tentar_span(Parser* P, int32_t p, int32_t pFim, int32_t* pConsumido, ADC_Metadados* pMetaPrefixo, int32_t pIniPrefixo) {
    char m = P->d[p];
    if (!(m == '*' || m == '_' || m == '`' || m == '#' || m == '^' || m == '~')) return NULL;
    int32_t ini_no = pMetaPrefixo ? pIniPrefixo : p;
    /* unconstrained: marca dupla (não para ^ e ~, que são marcas simples sem espaços dentro) */
    if ((m == '*' || m == '_' || m == '`' || m == '#') && em(P, p + 1, m)) {
        int32_t f = achar_fecho_unconstrained(P, m, p + 2, pFim);
        if (f > 0) {
            ADC_Inline* s = novo(P, ADC_IN_SPAN, ini_no, f + 2);
            if (!s) return NULL;
            s->variante_span = variante_de(m); s->unconstrained = true;
            if (m == '`' && em(P, p + 2, '+') && em(P, f - 1, '+') && f - 1 > p + 2) {
                s->filhos = texto(P, p + 3, f - 1); /* ``+literal+`` */
            } else s->filhos = parsear_faixa(P, p + 2, f);
            if (pMetaPrefixo) { s->id = pMetaPrefixo->id; s->papeis = pMetaPrefixo->papeis; }
            *pConsumido = f + 2;
            return s;
        }
        if (m != '`') { /* '**' sem fecho: pode ainda ser '*' constrained seguido de '*'? deixamos como texto */ }
    }
    if (m == '^' || m == '~') {
        /* ^sup^ / ~sub~: conteúdo sem espaços */
        int32_t f = -1;
        for (int32_t q = p + 1; q < pFim; q++) { char c = P->d[q]; if (c == ' ' || c == '\n' || c == '\t') break; if (c == m) { f = q; break; } }
        if (f > p + 1) {
            ADC_Inline* s = novo(P, ADC_IN_SPAN, ini_no, f + 1);
            if (!s) return NULL;
            s->variante_span = variante_de(m); s->unconstrained = true;
            s->filhos = parsear_faixa(P, p + 1, f);
            *pConsumido = f + 1;
            return s;
        }
        return NULL;
    }
    /* constrained: não precedido de alpha_d (salvo com prefixo [..]), não seguido de espaço */
    if (!pMetaPrefixo && alfa_d_antes(P, p)) return NULL;
    /* Asciidoctor (QUOTE_SUBS): a marca constrained também não abre depois de ';', ':' ou '}' — e o ` nem depois
       de aspas ou crase, o # nem depois de '&' (arn:aws:iam::*:role/x-* fica literal) */
    if (!pMetaPrefixo && p > 0) {
        char ant = P->d[p - 1];
        /* '<', '>' e '&' contam como ';': no Asciidoctor eles já viraram &lt; &gt; &amp; quando as marcas são lidas */
        if (ant == ';' || ant == ':' || ant == '}' || ant == '<' || ant == '>' || ant == '&') return NULL;
        if (m == '`' && (ant == '"' || ant == '\'' || ant == '`')) return NULL;
        if (m == '#' && ant == '&') return NULL;
    }
    if (espaco_em(P, p + 1) || p + 1 >= pFim) return NULL;
    int32_t f = achar_fecho_constrained(P, m, p + 1, pFim);
    if (f < 0) return NULL;
    ADC_Inline* s = novo(P, ADC_IN_SPAN, ini_no, f + 1);
    if (!s) return NULL;
    s->variante_span = variante_de(m); s->unconstrained = false;
    if (m == '`' && em(P, p + 1, '+') && em(P, f - 1, '+') && f - 1 > p + 1) s->filhos = texto(P, p + 2, f - 1);
    else s->filhos = parsear_faixa(P, p + 1, f);
    if (pMetaPrefixo) { s->id = pMetaPrefixo->id; s->papeis = pMetaPrefixo->papeis; }
    *pConsumido = f + 1;
    return s;
}

/* URL: esquema://... até espaço ou pontuação final. Devolve fim (exclusivo) do alvo. */
static int32_t fim_url(Parser* P, int32_t p, int32_t pFim) {
    int32_t q = p;
    while (q < pFim) {
        char c = P->d[q];
        if (c == ' ' || c == '\t' || c == '\n' || c == '[' || c == ']' || c == '<' || c == '>' || c == '"' || c == '\'' || c == '{' || c == '}' || c == '|' || c == '^' || c == '`') break;
        q++;
    }
    /* pontuação final não faz parte do link */
    while (q > p) {
        char c = P->d[q - 1];
        if (c == '.' || c == ',' || c == ';' || c == ':' || c == '!' || c == '?' || c == ')' || c == '*' || c == '_') q--;
        else break;
    }
    return q;
}

static bool eh_esquema_url(Parser* P, int32_t p, int32_t* pTamEsquema) {
    static const char* esquemas[] = { "https://", "http://", "ftp://", "irc://", "mailto:" };
    for (size_t i = 0; i < 5; i++) if (comeca_em(P, p, esquemas[i])) { *pTamEsquema = (int32_t)strlen(esquemas[i]); return true; }
    return false;
}

/* Lê um macro inline "nome:alvo[attrs]" começando em p (nome já identificado). Preenche alvo e attrs
 * (fatias) e devolve o fim (exclusivo) ou -1. */
static int32_t ler_macro(Parser* P, int32_t p, int32_t pTamNome, int32_t pFim, ADC_Fatia* pAlvo, ADC_Fatia* pAttrs, bool pAlvoObrigatorio) {
    int32_t q = p + pTamNome; /* após "nome:" */
    int32_t ini_alvo = q;
    while (q < pFim && P->d[q] != '[' && P->d[q] != ' ' && P->d[q] != '\n' && P->d[q] != '\t') q++;
    if (q >= pFim || P->d[q] != '[') return -1;
    if (pAlvoObrigatorio && q == ini_alvo) return -1;
    int32_t f = achar_fecha_colchete(P, q, pFim);
    if (f < 0) return -1;
    *pAlvo = adc_fatia(P->d + ini_alvo, q - ini_alvo);
    *pAttrs = adc_fatia(P->d + q + 1, f - q - 1);
    return f + 1;
}

/* Remove escapes "\]" de um texto de attrlist inline. */
static ADC_Fatia desescapar_colchete(Parser* P, ADC_Fatia pF) {
    bool tem = false;
    for (int32_t i = 0; i + 1 < pF.tamanho; i++) if (pF.dados[i] == '\\' && pF.dados[i + 1] == ']') { tem = true; break; }
    if (!tem) return pF;
    char* s = (char*)adc_arena_alocar(P->ctx->arena, (size_t)pF.tamanho + 1);
    if (!s) return pF;
    int32_t o = 0;
    for (int32_t i = 0; i < pF.tamanho; i++) { if (pF.dados[i] == '\\' && i + 1 < pF.tamanho && pF.dados[i + 1] == ']') continue; s[o++] = pF.dados[i]; }
    return adc_fatia(s, o);
}

/* Parseia um sub-texto (attrs de macro etc.) em inlines, mantendo as localizações (é fatia do mesmo buffer). */
static ADC_Inline* parsear_sub(Parser* P, ADC_Fatia pF) {
    if (pF.dados >= P->d && pF.dados + pF.tamanho <= P->d + P->n) {
        int32_t ini = (int32_t)(pF.dados - P->d);
        return parsear_faixa(P, ini, ini + pF.tamanho);
    }
    Parser sub = *P; sub.d = pF.dados; sub.n = pF.tamanho; sub.origem = NULL;
    return parsear_faixa(&sub, 0, pF.tamanho);
}

static ADC_Inline* texto_de_fatia(Parser* P, ADC_Fatia pF) {
    ADC_Inline* t = adc_inline_texto(P->ctx->arena, pF);
    if (t && pF.dados >= P->d && pF.dados + pF.tamanho <= P->d + P->n) { t->desl_inicio = (int32_t)(pF.dados - P->d); t->desl_fim = t->desl_inicio + pF.tamanho; }
    return t;
}

/* Separa "a > b > c" ou "a,b,c" em itens TEXTO. */
static ADC_Inline* dividir_itens(Parser* P, ADC_Fatia pF, const char* pSep) {
    Lista L = { NULL, NULL };
    int32_t n = (int32_t)strlen(pSep);
    int32_t ini = 0;
    for (int32_t i = 0; i <= pF.tamanho; i++) {
        bool fim = i == pF.tamanho;
        bool sep = !fim && i + n <= pF.tamanho && memcmp(pF.dados + i, pSep, (size_t)n) == 0;
        if (fim || sep) {
            ADC_Fatia item = adc_fatia_aparar(adc_fatia(pF.dados + ini, i - ini));
            if (item.tamanho > 0) anexar(&L, texto_de_fatia(P, item));
            ini = i + n; if (sep) i += n - 1;
        }
    }
    return L.primeiro;
}

/* pLiteral: o texto do link é a própria URL/e-mail e NÃO se re-parseia (senão recursão infinita). */
static ADC_Inline* criar_ref(Parser* P, ADC_VarianteRef pVar, ADC_Fatia pAlvo, ADC_Fatia pTexto, int32_t pIni, int32_t pFim, bool pLiteral) {
    ADC_Inline* r = novo(P, ADC_IN_REF, pIni, pFim);
    if (!r) return NULL;
    r->variante_ref = pVar; r->alvo = pAlvo;
    if (pTexto.tamanho > 0) r->filhos = pLiteral ? texto_de_fatia(P, pTexto) : parsear_sub(P, pTexto);
    return r;
}

/* Macros e formas abreviadas que começam em p. Devolve o nó e o fim consumido, ou NULL. */
static ADC_Inline* tentar_macro(Parser* P, int32_t p, int32_t pFim, int32_t* pConsumido) {
    char c = P->d[p];
    ADC_Arena* arena = P->ctx->arena;
    /* <<alvo,texto>> */
    if (c == '<' && em(P, p + 1, '<')) {
        int32_t f = -1;
        for (int32_t q = p + 2; q + 1 < pFim; q++) { if (P->d[q] == '>' && P->d[q + 1] == '>') { f = q; break; } if (P->d[q] == '\n') break; }
        if (f > p + 2) {
            ADC_Fatia dentro = adc_fatia(P->d + p + 2, f - p - 2);
            int32_t v = adc_fatia_indice(dentro, ',');
            ADC_Fatia alvo = v >= 0 ? adc_fatia(dentro.dados, v) : dentro;
            ADC_Fatia txt = v >= 0 ? adc_fatia_aparar(adc_fatia(dentro.dados + v + 1, dentro.tamanho - v - 1)) : adc_fatia(NULL, 0);
            alvo = adc_fatia_aparar(alvo);
            ADC_Inline* r = criar_ref(P, ADC_REF_XREF, alvo, txt, p, f + 2, false);
            *pConsumido = f + 2;
            return r;
        }
        return NULL;
    }
    /* [[id]] / [[id,reftext]] âncora inline; [#id] também */
    if (c == '[' && em(P, p + 1, '[')) {
        int32_t f = -1;
        for (int32_t q = p + 2; q + 1 < pFim; q++) { if (P->d[q] == ']' && P->d[q + 1] == ']') { f = q; break; } if (P->d[q] == '\n') break; }
        if (f > p + 2) {
            ADC_Fatia dentro = adc_fatia(P->d + p + 2, f - p - 2);
            int32_t v = adc_fatia_indice(dentro, ',');
            ADC_Inline* a = novo(P, ADC_IN_ANCORA, p, f + 2);
            if (!a) return NULL;
            a->id = adc_fatia_aparar(v >= 0 ? adc_fatia(dentro.dados, v) : dentro);
            if (v >= 0) a->filhos = parsear_sub(P, adc_fatia_aparar(adc_fatia(dentro.dados + v + 1, dentro.tamanho - v - 1)));
            adc_ctx_registrar_id(P->ctx, a->id, a->filhos, NULL);
            *pConsumido = f + 2;
            return a;
        }
        return NULL;
    }
    if (c == '[' && em(P, p + 1, '#')) {
        int32_t f = achar_fecha_colchete(P, p, pFim);
        if (f > p + 2) {
            ADC_Fatia dentro = adc_fatia(P->d + p + 2, f - p - 2);
            bool ok = dentro.tamanho > 0;
            for (int32_t i = 0; i < dentro.tamanho; i++) if (adc_eh_espaco(dentro.dados[i]) || dentro.dados[i] == '.' ) { ok = false; break; }
            /* [#id]*texto* é prefixo de span; [#id] sozinho (seguido de não-marca) é âncora */
            if (ok) {
                char prox = f + 1 < pFim ? P->d[f + 1] : 0;
                if (!(prox == '*' || prox == '_' || prox == '`' || prox == '#' || prox == '^' || prox == '~')) {
                    ADC_Inline* a = novo(P, ADC_IN_ANCORA, p, f + 1);
                    if (!a) return NULL;
                    a->id = dentro;
                    adc_ctx_registrar_id(P->ctx, a->id, NULL, NULL);
                    *pConsumido = f + 1;
                    return a;
                }
            }
        }
    }
    /* URL direta: https://... [texto] */
    int32_t tam_esq;
    if ((c == 'h' || c == 'f' || c == 'i' || c == 'm') && eh_esquema_url(P, p, &tam_esq) && !alfa_d_antes(P, p)) {
        int32_t f = fim_url(P, p, pFim);
        if (f > p + tam_esq) {
            ADC_Fatia alvo = adc_fatia(P->d + p, f - p);
            ADC_Fatia txt = adc_fatia(NULL, 0);
            int32_t fim_total = f;
            bool externo = false;
            if (em(P, f, '[')) {
                int32_t fc = achar_fecha_colchete(P, f, pFim);
                if (fc > 0) {
                    txt = adc_fatia(P->d + f + 1, fc - f - 1);
                    fim_total = fc + 1;
                }
            }
            ADC_Inline* r;
            if (comeca_em(P, p, "mailto:")) {
                ADC_Fatia email = adc_fatia(alvo.dados + 7, alvo.tamanho - 7);
                r = criar_ref(P, ADC_REF_LINK, alvo, txt.tamanho ? txt : email, p, fim_total, txt.tamanho == 0);
            } else {
                /* texto com ^ final abre em nova janela; atributos (window=...) só se houver '=' */
                if (txt.tamanho > 0) {
                    ADC_Atributo* at_link = NULL;
                    ADC_Fatia rotulo = separar_texto_link(arena, txt, &at_link);
                    if (rotulo.tamanho > 0 && rotulo.dados[rotulo.tamanho - 1] == '^') { externo = true; rotulo.tamanho--; }
                    r = criar_ref(P, ADC_REF_LINK, alvo, rotulo.tamanho ? rotulo : alvo, p, fim_total, rotulo.tamanho == 0);
                    if (r) r->atributos = at_link;
                    if (r && !externo) { bool a; ADC_Fatia w = adc_atributo_obter(r->atributos, "window", &a); externo = a && adc_fatia_igual(w, "_blank"); }
                } else r = criar_ref(P, ADC_REF_LINK, alvo, alvo, p, fim_total, true);
            }
            if (r) r->alvo_externo = externo;
            *pConsumido = fim_total;
            return r;
        }
    }
    /* macros nome:alvo[attrs] */
    if (adc_eh_alfa(c) && !alfa_d_antes(P, p)) {
        static const struct { const char* nome; bool alvo_obrigatorio; } macros[] = {
            { "link:", true }, { "xref:", true }, { "image:", true }, { "footnote:", false }, { "footnoteref:", true },
            { "kbd:", false }, { "btn:", false }, { "menu:", true }, { "icon:", true }, { "pass:", false },
            { "anchor:", true }, { "indexterm:", false }, { "indexterm2:", false }, { "stem:", false },
            { "latexmath:", false }, { "asciimath:", false }
        };
        bool experimental = adc_atribs_definido(&P->ctx->atribs, "experimental");
        for (size_t m = 0; m < sizeof macros / sizeof macros[0]; m++) {
            if (!comeca_em(P, p, macros[m].nome)) continue;
            if (!experimental && (m == 5 || m == 6 || m == 7)) continue; /* kbd, btn, menu exigem :experimental: */
            int32_t tn = (int32_t)strlen(macros[m].nome);
            ADC_Fatia alvo, attrs;
            int32_t f = ler_macro(P, p, tn, pFim, &alvo, &attrs, macros[m].alvo_obrigatorio);
            if (f < 0) continue;
            const char* nome = macros[m].nome;
            ADC_Inline* r = NULL;
            if (strcmp(nome, "link:") == 0) {
                ADC_Atributo* at_link = NULL;
                ADC_Fatia rotulo = separar_texto_link(arena, attrs, &at_link);
                bool externo = false;
                if (rotulo.tamanho > 0 && rotulo.dados[rotulo.tamanho - 1] == '^') { externo = true; rotulo.tamanho--; }
                r = criar_ref(P, ADC_REF_LINK, alvo, rotulo.tamanho ? rotulo : alvo, p, f, rotulo.tamanho == 0);
                if (r) r->atributos = at_link;
                if (r && !externo) { bool a; ADC_Fatia w = adc_atributo_obter(r->atributos, "window", &a); externo = a && adc_fatia_igual(w, "_blank"); }
                if (r) r->alvo_externo = externo;
            } else if (strcmp(nome, "xref:") == 0) {
                r = criar_ref(P, ADC_REF_XREF, alvo, attrs, p, f, false);
            } else if (strcmp(nome, "image:") == 0) {
                r = novo(P, ADC_IN_IMAGEM, p, f);
                if (r) {
                    r->alvo = alvo;
                    r->atributos = adc_attrlist_parsear(arena, attrs, false, NULL);
                    static const char* const pos[] = { "alt", "width", "height", NULL };
                    adc_attrlist_mapear_posicionais(arena, &r->atributos, pos);
                    bool a; adc_atributo_obter(r->atributos, "alt", &a);
                    if (!a) {
                        /* alt padrão: nome do arquivo sem extensão */
                        int32_t ult = alvo.tamanho; for (int32_t k = alvo.tamanho - 1; k >= 0; k--) { if (alvo.dados[k] == '.') { ult = k; break; } if (alvo.dados[k] == '/') break; }
                        int32_t ini = 0; for (int32_t k = 0; k < alvo.tamanho; k++) if (alvo.dados[k] == '/') ini = k + 1;
                        adc_atributo_definir(arena, &r->atributos, adc_fatia_cstr("alt"), adc_fatia(alvo.dados + ini, ult - ini));
                    }
                }
            } else if (strcmp(nome, "footnote:") == 0 || strcmp(nome, "footnoteref:") == 0) {
                r = novo(P, ADC_IN_NOTA, p, f);
                if (r) {
                    ADC_Fatia id = alvo; ADC_Fatia txt = attrs;
                    if (strcmp(nome, "footnoteref:") == 0) { /* footnoteref:id[texto] (legado) */ }
                    ADC_NotaRodape* existente = adc_ctx_achar_nota(P->ctx, id);
                    if (existente) { r->numero = existente->numero; r->id = id; r->filhos = existente->texto; r->valor = adc_fatia_cstr("ref"); }
                    else {
                        r->filhos = parsear_sub(P, desescapar_colchete(P, txt));
                        r->id = id;
                        r->numero = (P->subs == ADC_SUBS_TITULO) ? 0 : adc_ctx_registrar_nota(P->ctx, id, r->filhos);
                    }
                }
            } else if (strcmp(nome, "kbd:") == 0) {
                r = novo(P, ADC_IN_TECLA, p, f);
                if (r) {
                    ADC_Fatia a = attrs;
                    /* "Ctrl+Shift+T" ou "Ctrl,T"; o último '+' solto é tecla */
                    Lista L = { NULL, NULL };
                    int32_t ini = 0;
                    for (int32_t i = 0; i <= a.tamanho; i++) {
                        bool fim = i == a.tamanho;
                        bool sep = !fim && (a.dados[i] == '+' || a.dados[i] == ',') && i + 1 < a.tamanho && i > ini;
                        if (fim || sep) {
                            ADC_Fatia item = adc_fatia_aparar(adc_fatia(a.dados + ini, i - ini));
                            if (item.tamanho > 0) anexar(&L, texto_de_fatia(P, item));
                            ini = i + 1;
                        }
                    }
                    r->filhos = L.primeiro;
                }
            } else if (strcmp(nome, "btn:") == 0) {
                r = novo(P, ADC_IN_BOTAO, p, f);
                if (r) r->filhos = texto_de_fatia(P, adc_fatia_aparar(attrs));
            } else if (strcmp(nome, "menu:") == 0) {
                r = novo(P, ADC_IN_MENU, p, f);
                if (r) {
                    Lista L = { NULL, NULL };
                    anexar(&L, texto_de_fatia(P, alvo));
                    ADC_Inline* itens = dividir_itens(P, attrs, adc_fatia_indice(attrs, '>') >= 0 ? ">" : ",");
                    if (itens) { L.ultimo->proximo = itens; }
                    r->filhos = L.primeiro;
                }
            } else if (strcmp(nome, "icon:") == 0) {
                r = novo(P, ADC_IN_ICONE, p, f);
                if (r) { r->alvo = alvo; r->atributos = adc_attrlist_parsear(arena, attrs, false, NULL); }
            } else if (strcmp(nome, "pass:") == 0) {
                /* pass:[texto] cru; pass:q[texto] aplica quotes (parse normal); pass:a[] só atributos */
                if (alvo.tamanho > 0 && adc_fatia_indice(alvo, 'q') >= 0) {
                    r = novo(P, ADC_IN_SPAN, p, f); /* contêiner neutro: usamos filhos sem variante */
                    if (r) { r->tipo = ADC_IN_RAW; r->valor = adc_fatia(NULL, 0); r->filhos = parsear_sub(P, attrs); }
                } else {
                    r = novo(P, ADC_IN_RAW, p, f);
                    if (r) r->valor = attrs;
                }
            } else if (strcmp(nome, "anchor:") == 0) {
                r = novo(P, ADC_IN_ANCORA, p, f);
                if (r) { r->id = alvo; if (attrs.tamanho) r->filhos = parsear_sub(P, attrs); adc_ctx_registrar_id(P->ctx, alvo, r->filhos, NULL); }
            } else if (strcmp(nome, "indexterm:") == 0) {
                r = novo(P, ADC_IN_INDICE, p, f); /* oculto: sem filhos visíveis */
                if (r) r->valor = attrs;
            } else if (strcmp(nome, "indexterm2:") == 0) {
                r = novo(P, ADC_IN_INDICE, p, f);
                if (r) { r->valor = attrs; r->filhos = texto_de_fatia(P, attrs); }
            } else { /* stem, latexmath, asciimath: texto cru com marcador */
                r = novo(P, ADC_IN_RAW, p, f);
                if (r) { r->valor = attrs; r->alvo = adc_fatia(nome, tn - 1); }
            }
            if (!r) return NULL;
            *pConsumido = f;
            return r;
        }
    }
    /* (((termo))) oculto e ((termo)) visível */
    if (c == '(' && em(P, p + 1, '(')) {
        bool triplo = em(P, p + 2, '(');
        const char* fecho = triplo ? ")))" : "))";
        int32_t ini = p + (triplo ? 3 : 2);
        int32_t f = -1;
        for (int32_t q = ini; q + (triplo ? 3 : 2) <= pFim; q++) { if (memcmp(P->d + q, fecho, triplo ? 3 : 2) == 0) { f = q; break; } if (P->d[q] == '\n') break; }
        if (f > ini) {
            ADC_Inline* r = novo(P, ADC_IN_INDICE, p, f + (triplo ? 3 : 2));
            if (!r) return NULL;
            r->valor = adc_fatia(P->d + ini, f - ini);
            if (!triplo) r->filhos = parsear_sub(P, r->valor);
            *pConsumido = f + (triplo ? 3 : 2);
            return r;
        }
    }
    /* e-mail solto: nome@dominio.tld */
    if (adc_eh_alnum(c) && !alfa_d_antes(P, p)) {
        int32_t q = p;
        while (q < pFim && (adc_eh_alnum(P->d[q]) || P->d[q] == '.' || P->d[q] == '_' || P->d[q] == '-' || P->d[q] == '+' || P->d[q] == '%')) q++;
        if (q < pFim && P->d[q] == '@' && q > p) {
            int32_t r0 = q + 1, r = r0; int32_t pontos = 0;
            while (r < pFim && (adc_eh_alnum(P->d[r]) || P->d[r] == '.' || P->d[r] == '-')) { if (P->d[r] == '.') pontos++; r++; }
            while (r > r0 && (P->d[r - 1] == '.' || P->d[r - 1] == '-')) { if (P->d[r - 1] == '.') pontos--; r--; }
            if (pontos >= 1 && r > r0 + 2) {
                ADC_Fatia email = adc_fatia(P->d + p, r - p);
                char* alvo = (char*)adc_arena_alocar(arena, (size_t)email.tamanho + 8);
                if (!alvo) return NULL;
                memcpy(alvo, "mailto:", 7); memcpy(alvo + 7, email.dados, (size_t)email.tamanho);
                ADC_Inline* rr = criar_ref(P, ADC_REF_LINK, adc_fatia(alvo, email.tamanho + 7), email, p, r, true);
                *pConsumido = r;
                return rr;
            }
        }
    }
    return NULL;
}

/* Substituições tipográficas (replacements) começando em p. */
static ADC_Inline* tentar_replacement(Parser* P, int32_t p, int32_t pFim, int32_t* pConsumido) {
    char c = P->d[p];
    if (c == '(') {
        if (comeca_em(P, p, "(C)")) { *pConsumido = p + 3; return charref(P, "#169", p, p + 3); }
        if (comeca_em(P, p, "(R)")) { *pConsumido = p + 3; return charref(P, "#174", p, p + 3); }
        if (comeca_em(P, p, "(TM)")) { *pConsumido = p + 4; return charref(P, "#8482", p, p + 4); }
        return NULL;
    }
    if (c == '-' && em(P, p + 1, '-') && !em(P, p + 2, '-')) {
        /* " -- " entre espaços → thin space + em dash + thin space ; "a--b" entre letras → em dash */
        /* Asciidoctor: (?: |\n|^)--(?: |\n|$), com ^ e $ por linha: o espaço/quebra de cada lado é absorvido */
        bool antes_ok = p == 0 || P->d[p - 1] == ' ' || P->d[p - 1] == '\n';
        bool depois_fim = p + 2 >= pFim;
        bool depois_ok = depois_fim || em(P, p + 2, ' ') || em(P, p + 2, '\n');
        if (antes_ok && depois_ok) {
            int32_t ini = p > 0 ? p - 1 : p;
            ADC_Inline* r = novo(P, ADC_IN_RAW, ini, depois_fim ? p + 2 : p + 3);
            if (r) r->valor = adc_fatia_cstr("&#8201;&#8212;&#8201;");
            /* o espaço anterior já foi acumulado como texto pelo chamador: devolvemos só o traço com os
               thin spaces e o chamador recua o acumulador em 1 (ver uso) */
            *pConsumido = depois_fim ? p + 2 : p + 3;
            return r;
        }
        if (alfa_d_antes(P, p) && alfa_d_em(P, p + 2)) { *pConsumido = p + 2; ADC_Inline* r = novo(P, ADC_IN_RAW, p, p + 2); if (r) r->valor = adc_fatia_cstr("&#8212;&#8203;"); return r; }
        if (p + 2 == pFim || em(P, p + 2, '\n')) { if (alfa_d_antes(P, p)) { *pConsumido = p + 2; return charref(P, "#8212", p, p + 2); } }
        return NULL;
    }
    if (c == '.' && comeca_em(P, p, "...")) { *pConsumido = p + 3; ADC_Inline* r = novo(P, ADC_IN_RAW, p, p + 3); if (r) r->valor = adc_fatia_cstr("&#8230;&#8203;"); return r; }
    if (c == '-' && em(P, p + 1, '>')) { *pConsumido = p + 2; return charref(P, "#8594", p, p + 2); }
    if (c == '=' && em(P, p + 1, '>')) { *pConsumido = p + 2; return charref(P, "#8658", p, p + 2); }
    if (c == '<' && em(P, p + 1, '-') && !em(P, p + 2, '-')) { *pConsumido = p + 2; return charref(P, "#8592", p, p + 2); }
    if (c == '<' && em(P, p + 1, '=') && !em(P, p + 2, '=')) { *pConsumido = p + 2; return charref(P, "#8656", p, p + 2); }
    if (c == '\'' && alfa_d_antes(P, p) && alfa_d_em(P, p + 1)) { *pConsumido = p + 1; return charref(P, "#8217", p, p + 1); }
    return NULL;
}

/* Aspas tipográficas "`texto`" e '`texto`': preenche pSaida com abertura, conteúdo e fecho. */
static bool tentar_aspas(Parser* P, int32_t p, int32_t pFim, int32_t* pConsumido, Lista* pSaida) {
    char c = P->d[p];
    if ((c != '"' && c != '\'') || !em(P, p + 1, '`')) return false;
    if (espaco_em(P, p + 2)) return false;
    int32_t f = -1;
    for (int32_t q = p + 2; q + 1 < pFim; q++) { if (P->d[q] == '`' && P->d[q + 1] == c && !espaco_em(P, q - 1)) { f = q; break; } }
    if (f < 0) return false;
    pSaida->primeiro = pSaida->ultimo = NULL;
    anexar(pSaida, charref(P, c == '"' ? "#8220" : "#8216", p, p + 2));
    ADC_Inline* dentro = parsear_faixa(P, p + 2, f);
    while (dentro) { ADC_Inline* prox = dentro->proximo; anexar(pSaida, dentro); dentro = prox; }
    anexar(pSaida, charref(P, c == '"' ? "#8221" : "#8217", f, f + 2));
    *pConsumido = f + 2;
    return true;
}

/* Testa, sem consumir, se em p começa uma marcação que casaria (span, macro, replacement, passthrough,
 * charref, aspas, referência de atributo). Devolve o nó casado (descartado pelo chamador) e o fim. */
static ADC_Inline* tentar_escapavel(Parser* P, int32_t p, int32_t pFim, int32_t* pFimConsumido) {
    char c = P->d[p];
    int32_t consumido = 0;
    ADC_Inline* no = NULL;
    if (c == '*' || c == '_' || c == '`' || c == '#' || c == '^' || c == '~') no = tentar_span(P, p, pFim, &consumido, NULL, 0);
    /* \https://... : a barra escapa o link automático (o Asciidoctor o reconhece mesmo com <...> na URL, porque
       os caracteres especiais já viraram entidades) — sai a URL como texto, com o [...] que a seguir */
    if (!no && adc_eh_alfa(c)) {
        static const char* const esquemas[] = { "https://", "http://", "file://", "ftp://", "irc://" };
        for (int32_t k = 0; k < 5 && !no; k++) {
            if (!comeca_em(P, p, esquemas[k])) continue;
            int32_t q = p + (int32_t)strlen(esquemas[k]);
            while (q < pFim && !espaco_em(P, q) && P->d[q] != '[' && P->d[q] != ']') q++;
            if (q > p + (int32_t)strlen(esquemas[k])) {
                if (em(P, q, '[')) { int32_t fc = achar_fecha_colchete(P, q, pFim); if (fc > 0) q = fc + 1; }
                consumido = q; no = novo(P, ADC_IN_RAW, p, q);
            }
        }
    }
    if (!no && (c == '<' || c == '[' || c == '(' || adc_eh_alnum(c))) no = tentar_macro(P, p, pFim, &consumido);
    if (!no && (c == '(' || c == '-' || c == '.' || c == '=' || c == '<' || c == '\'')) no = tentar_replacement(P, p, pFim, &consumido);
    if (!no && c == '+' && comeca_em(P, p, "+++")) { for (int32_t q = p + 3; q + 2 < pFim; q++) if (P->d[q] == '+' && P->d[q + 1] == '+' && P->d[q + 2] == '+') { consumido = q + 3; no = novo(P, ADC_IN_RAW, p, consumido); break; } }
    if (!no && c == '+' && em(P, p + 1, '+')) { int32_t f = achar_fecho_unconstrained(P, '+', p + 2, pFim); if (f > 0) { consumido = f + 2; no = novo(P, ADC_IN_RAW, p, consumido); } }
    if (!no && c == '+' && !espaco_em(P, p + 1)) { int32_t f = achar_fecho_constrained(P, '+', p + 1, pFim); if (f > 0) { consumido = f + 1; no = novo(P, ADC_IN_RAW, p, consumido); } }
    if (!no && c == '&') { int32_t q = p + 1; if (em(P, q, '#')) q++; while (q < pFim && adc_eh_alnum(P->d[q])) q++; if (q > p + 1 && em(P, q, ';')) { consumido = q + 1; no = novo(P, ADC_IN_RAW, p, consumido); } }
    if (!no && (c == '"' || c == '\'') && em(P, p + 1, '`')) { Lista sub; if (tentar_aspas(P, p, pFim, &consumido, &sub)) no = sub.primeiro; }
    if (!no && c == '{') { int32_t q = p + 1; while (q < pFim && (adc_eh_alnum(P->d[q]) || P->d[q] == '-' || P->d[q] == '_')) q++; if (q > p + 1 && em(P, q, '}')) { consumido = q + 1; no = novo(P, ADC_IN_RAW, p, consumido); } }
    if (no) *pFimConsumido = consumido;
    return no;
}

static ADC_Inline* parsear_faixa(Parser* P, int32_t pIni, int32_t pFim) {
    Lista L = { NULL, NULL };
    Acum A = { 0, 0, false };
    if (++P->profundidade > 64) {
        P->profundidade--;
        adc_msgs_adicionar(&P->ctx->msgs, ADC_NIVEL_AVISO, 0, 0, "Marcação inline aninhada além de 64 níveis; o restante foi tratado como texto.");
        return texto(P, pIni, pFim);
    }
    int32_t p = pIni;
    while (p < pFim) {
        char c = P->d[p];
        int32_t consumido = 0;
        ADC_Inline* no = NULL;

        if (P->so_marcas) {
            /* grupo quotes apenas: \ escapa uma marca que casaria; marcas e aspas tipográficas; o resto é texto */
            if (c == '\\' && p + 1 < pFim) {
                char m = P->d[p + 1];
                if (m == '*' || m == '_' || m == '`' || m == '#' || m == '^' || m == '~') {
                    int32_t salvo_prof = P->profundidade;
                    ADC_Inline* t = tentar_span(P, p + 1, pFim, &consumido, NULL, 0);
                    P->profundidade = salvo_prof;
                    if (t) { descarregar(P, &L, &A); anexar(&L, texto(P, p + 1, consumido)); p = consumido; continue; }
                }
            }
            if (c == '*' || c == '_' || c == '`' || c == '#' || c == '^' || c == '~') {
                no = tentar_span(P, p, pFim, &consumido, NULL, 0);
                if (no) { descarregar(P, &L, &A); anexar(&L, no); p = consumido; continue; }
            }
            if (c == '"' || c == '\'') {
                Lista sub;
                if (tentar_aspas(P, p, pFim, &consumido, &sub)) {
                    descarregar(P, &L, &A);
                    ADC_Inline* i = sub.primeiro;
                    while (i) { ADC_Inline* prox = i->proximo; anexar(&L, i); i = prox; }
                    p = consumido; continue;
                }
            }
            acumular(&A, p, p + adc_utf8_tamanho((unsigned char)c));
            p += adc_utf8_tamanho((unsigned char)c);
            continue;
        }

        /* escape contextual (como o Asciidoctor): a barra só escapa uma marcação que CASARIA aqui;
           nesse caso o trecho casado sai como texto literal, sem a barra. Senão a barra é texto. */
        if (c == '\\' && p + 1 < pFim) {
            int32_t fim_esc = -1;
            int32_t salvo_profundidade = P->profundidade;
            int32_t salvo_notas = P->ctx->quantidade_notas;
            ADC_Inline* teste = tentar_escapavel(P, p + 1, pFim, &fim_esc);
            P->profundidade = salvo_profundidade;
            if (teste && fim_esc > p + 1) {
                /* desfaz efeitos colaterais do teste (nota registrada) */
                if (P->ctx->quantidade_notas != salvo_notas) { P->ctx->quantidade_notas = salvo_notas; ADC_NotaRodape* n = P->ctx->notas; ADC_NotaRodape* ant = NULL; while (n && n->numero <= salvo_notas) { ant = n; n = n->proxima; } if (ant) { ant->proxima = NULL; P->ctx->ultima_nota = ant; } else { P->ctx->notas = NULL; P->ctx->ultima_nota = NULL; } }
                descarregar(P, &L, &A);
                anexar(&L, texto(P, p + 1, fim_esc));
                p = fim_esc;
                continue;
            }
        }
        /* passthroughs */
        if (c == '+' && comeca_em(P, p, "+++")) {
            int32_t f = -1;
            for (int32_t q = p + 3; q + 2 < pFim; q++) if (P->d[q] == '+' && P->d[q + 1] == '+' && P->d[q + 2] == '+') { f = q; break; }
            if (f > 0) { descarregar(P, &L, &A); no = novo(P, ADC_IN_RAW, p, f + 3); if (no) no->valor = adc_fatia(P->d + p + 3, f - p - 3); anexar(&L, no); p = f + 3; continue; }
        }
        if (c == '+' && em(P, p + 1, '+') && !em(P, p + 2, '+')) {
            int32_t f = achar_fecho_unconstrained(P, '+', p + 2, pFim);
            if (f > 0) { descarregar(P, &L, &A); anexar(&L, texto(P, p + 2, f)); p = f + 2; continue; }
        }
        if (c == '+' && !alfa_d_antes(P, p) && !espaco_em(P, p + 1) && p + 1 < pFim && !em(P, p + 1, '+')) {
            int32_t f = achar_fecho_constrained(P, '+', p + 1, pFim);
            if (f > 0) { descarregar(P, &L, &A); anexar(&L, texto(P, p + 1, f)); p = f + 1; continue; }
        }
        if (c == '$' && em(P, p + 1, '$')) {
            int32_t f = achar_fecho_unconstrained(P, '$', p + 2, pFim);
            if (f > 0) { descarregar(P, &L, &A); no = novo(P, ADC_IN_RAW, p, f + 2); if (no) no->valor = adc_fatia(P->d + p + 2, f - p - 2); anexar(&L, no); p = f + 2; continue; }
        }
        /* quebra de linha forçada: " +" no fim da LINHA do texto inteiro (HardLineBreakRx), não do trecho corrente:
           em *Add a filter +* o '+' fecha o negrito, não a linha */
        if (c == '+' && p > pIni && P->d[p - 1] == ' ' && (p + 1 == P->n || em(P, p + 1, '\n'))) {
            /* remove o espaço do acumulador */
            if (A.ativo && A.fim == p) A.fim = p - 1;
            descarregar(P, &L, &A);
            no = novo(P, ADC_IN_QUEBRA_LINHA, p, p + 1);
            anexar(&L, no);
            p = p + 1;
            if (em(P, p, '\n')) { /* a quebra absorve o '\n' */ p++; }
            continue;
        }
        if (c == '\n' && P->ctx->hardbreaks) {
            descarregar(P, &L, &A);
            anexar(&L, novo(P, ADC_IN_QUEBRA_LINHA, p, p + 1));
            p++;
            continue;
        }
        /* prefixo [attrs] de span (parse_quoted_text_attributes do Asciidoctor): [.role#id] em atalhos, ou
           [papel] — qualquer outro texto (até a 1ª vírgula) é o papel: [x-]`texto` → <code class="x-"> */
        if (c == '[' && !em(P, p + 1, '[')) {
            int32_t f = achar_fecha_colchete(P, p, pFim);
            if (f > p + 1 && f + 1 < pFim) {
                char m = P->d[f + 1];
                ADC_Fatia attrs_x = adc_fatia(P->d + p + 1, f - p - 1);
                bool x_menos = adc_fatia_igual(attrs_x, "x-") || adc_fatia_termina(attrs_x, " x-");
                if (x_menos && (m == '`' || m == '+') && !espaco_em(P, f + 2)) {
                    /* [x-]`texto` (InlinePassRx, "old behavior"): monoespaçado; com ` o conteúdo é literal; o papel é o
                       que vem antes de " x-" */
                    int32_t g = -1;
                    for (int32_t q = f + 2; q < pFim; q++) { if (P->d[q] == '\n') break; if (P->d[q] == m && q > f + 2 && !espaco_em(P, q - 1) && !alfa_d_em(P, q + 1)) { g = q; break; } }
                    if (g > 0) {
                        ADC_Inline* s = novo(P, ADC_IN_SPAN, p, g + 1);
                        if (s) {
                            s->variante_span = ADC_SPAN_CODE; s->unconstrained = false;
                            s->filhos = m == '`' ? texto(P, f + 2, g) : parsear_faixa(P, f + 2, g);
                            if (attrs_x.tamanho > 3) { ADC_Fatia papel = adc_fatia_aparar(adc_fatia(attrs_x.dados, attrs_x.tamanho - 3)); if (papel.tamanho) adc_lista_fatias_adicionar(P->ctx->arena, &s->papeis, papel); }
                            descarregar(P, &L, &A); anexar(&L, s); p = g + 1; continue;
                        }
                    }
                }
                if (m == '*' || m == '_' || m == '`' || m == '#' || m == '^' || m == '~') {
                    ADC_Metadados meta; memset(&meta, 0, sizeof meta);
                    ADC_Fatia conteudo = adc_fatia(P->d + p + 1, f - p - 1);
                    int32_t v = adc_fatia_indice(conteudo, ',');
                    if (v >= 0) conteudo.tamanho = v;
                    conteudo = adc_fatia_aparar(conteudo);
                    if (conteudo.tamanho > 0 && (conteudo.dados[0] == '.' || conteudo.dados[0] == '#')) adc_attrlist_atalhos(P->ctx->arena, conteudo, &meta);
                    else if (conteudo.tamanho > 0) adc_lista_fatias_adicionar(P->ctx->arena, &meta.papeis, conteudo);
                    ADC_Inline* s = tentar_span(P, f + 1, pFim, &consumido, &meta, p);
                    if (s) { descarregar(P, &L, &A); anexar(&L, s); p = consumido; continue; }
                }
            }
        }
        /* spans */
        if (c == '*' || c == '_' || c == '`' || c == '#' || c == '^' || c == '~') {
            no = tentar_span(P, p, pFim, &consumido, NULL, 0);
            if (no) { descarregar(P, &L, &A); anexar(&L, no); p = consumido; continue; }
        }
        /* macros e referências */
        if (c == '<' || c == '[' || c == '(' || adc_eh_alnum(c)) {
            no = tentar_macro(P, p, pFim, &consumido);
            if (no) { descarregar(P, &L, &A); anexar(&L, no); p = consumido; continue; }
        }
        /* referência de caractere &nome; / &#123; */
        if (c == '&') {
            int32_t q = p + 1;
            if (em(P, q, '#')) { q++; if (em(P, q, 'x') || em(P, q, 'X')) q++; int32_t ini = q; while (q < pFim && (adc_eh_alnum(P->d[q]))) q++; if (q > ini && em(P, q, ';')) { descarregar(P, &L, &A); no = novo(P, ADC_IN_CHARREF, p, q + 1); if (no) no->valor = adc_fatia(P->d + p + 1, q - p - 1); anexar(&L, no); p = q + 1; continue; } }
            else { int32_t ini = q; while (q < pFim && adc_eh_alnum(P->d[q])) q++; if (q > ini && q - ini <= 32 && em(P, q, ';')) { descarregar(P, &L, &A); no = novo(P, ADC_IN_CHARREF, p, q + 1); if (no) no->valor = adc_fatia(P->d + p + 1, q - p - 1); anexar(&L, no); p = q + 1; continue; } }
        }
        /* aspas tipográficas */
        if (c == '"' || c == '\'') {
            Lista sub;
            if (tentar_aspas(P, p, pFim, &consumido, &sub)) {
                descarregar(P, &L, &A);
                ADC_Inline* i = sub.primeiro;
                while (i) { ADC_Inline* prox = i->proximo; anexar(&L, i); i = prox; }
                p = consumido; continue;
            }
        }
        /* replacements */
        if (c == '(' || c == '-' || c == '.' || c == '=' || c == '<' || c == '\'') {
            no = tentar_replacement(P, p, pFim, &consumido);
            if (no) {
                if (no->tipo == ADC_IN_RAW && no->desl_inicio == p - 1) { if (A.ativo && A.fim == p) A.fim = p - 1; }
                descarregar(P, &L, &A); anexar(&L, no); p = consumido; continue;
            }
        }
        /* texto comum */
        acumular(&A, p, p + adc_utf8_tamanho((unsigned char)c));
        p += adc_utf8_tamanho((unsigned char)c);
    }
    descarregar(P, &L, &A);
    P->profundidade--;
    return L.primeiro;
}

/* Verbatim: um texto só, com callouts <n> / <.> em fim de linha separados como nós CHARREF "callout". */
/* Trecho de texto verbatim: com subs="+quotes", passa pelo grupo quotes; senão é um texto só. */
static void anexar_trecho_verbatim(Parser* P, Lista* L, int32_t pIni, int32_t pFim) {
    if (pFim <= pIni) return;
    if (!P->so_marcas) { anexar(L, texto(P, pIni, pFim)); return; }
    ADC_Inline* i = parsear_faixa(P, pIni, pFim);
    while (i) { ADC_Inline* prox = i->proximo; anexar(L, i); i = prox; }
}

static ADC_Inline* parsear_verbatim(Parser* P) {
    Lista L = { NULL, NULL };
    int32_t ini = 0, p = 0;
    while (p < P->n) {
        /* fim de linha */
        int32_t f = p; while (f < P->n && P->d[f] != '\n') f++;
        /* procura callouts no fim da linha: " <1>", "<2>", "<.>" (vários permitidos) */
        int32_t q = f;
        int32_t primeiro_callout = -1;
        for (;;) {
            int32_t r = q;
            while (r > p && P->d[r - 1] == ' ') r--;
            if (r > p + 2 && P->d[r - 1] == '>') {
                int32_t s = r - 2; while (s > p && (adc_eh_digito(P->d[s]) || P->d[s] == '.')) s--;
                if (P->d[s] == '<' && s < r - 2 && (s == p || P->d[s - 1] == ' ' || P->d[s - 1] == '-' || P->d[s - 1] == '/' || P->d[s - 1] == '#' || P->d[s - 1] == '!' || P->d[s - 1] == ';')) {
                    /* "// <1>" ou "# <1>": o comentário também sai no Asciidoctor? Não — fica. Só removemos o <n>. */
                    primeiro_callout = s; q = s; continue;
                }
            }
            break;
        }
        if (primeiro_callout >= 0) {
            int32_t fim_texto = primeiro_callout; while (fim_texto > p && P->d[fim_texto - 1] == ' ') fim_texto--;
            anexar_trecho_verbatim(P, &L, ini, fim_texto);
            /* cada callout vira um nó */
            int32_t s = primeiro_callout;
            while (s < f) {
                if (P->d[s] == '<') {
                    int32_t e = s + 1; while (e < f && P->d[e] != '>') e++;
                    ADC_Inline* c = novo(P, ADC_IN_CHARREF, s, e + 1);
                    if (c) {
                        c->valor = adc_fatia_cstr("callout");
                        ADC_Fatia num = adc_fatia(P->d + s + 1, e - s - 1);
                        c->numero = adc_fatia_igual(num, ".") ? ++P->ctx->contador_callout : adc_fatia_para_int(num, 0);
                        anexar(&L, c);
                    }
                    s = e + 1;
                } else s++;
            }
            ini = f; /* o '\n' entra no próximo texto */
        }
        p = f + 1;
    }
    anexar_trecho_verbatim(P, &L, ini, P->n);
    return L.primeiro;
}

/* Funde nós de texto adjacentes (o ASG espera o parágrafo inteiro em um só nó). */
static ADC_Inline* fundir_textos(Parser* P, ADC_Inline* pLista) {
    for (ADC_Inline* i = pLista; i; i = i->proximo) {
        /* kbd e menu têm por filhos uma LISTA de itens (teclas, passos), não texto corrido: não se fundem */
        if (i->filhos && i->tipo != ADC_IN_TECLA && i->tipo != ADC_IN_MENU) i->filhos = fundir_textos(P, i->filhos);
        while (i->tipo == ADC_IN_TEXTO && i->proximo && i->proximo->tipo == ADC_IN_TEXTO) {
            ADC_Inline* j = i->proximo;
            if (i->valor.dados + i->valor.tamanho == j->valor.dados) {
                i->valor.tamanho += j->valor.tamanho;
            } else {
                char* c = (char*)adc_arena_alocar(P->ctx->arena, (size_t)i->valor.tamanho + (size_t)j->valor.tamanho + 1);
                if (!c) break;
                memcpy(c, i->valor.dados, (size_t)i->valor.tamanho);
                memcpy(c + i->valor.tamanho, j->valor.dados, (size_t)j->valor.tamanho);
                i->valor = adc_fatia(c, i->valor.tamanho + j->valor.tamanho);
            }
            i->desl_fim = j->desl_fim;
            i->proximo = j->proximo;
        }
    }
    return pLista;
}

static void fixar_locais(Parser* P, ADC_Inline* pLista) {
    for (ADC_Inline* i = pLista; i; i = i->proximo) {
        if (P->origem && i->desl_fim > i->desl_inicio) {
            int32_t oi = i->desl_inicio, of = i->desl_fim - 1;
            if (P->origem->mapa) { oi = P->origem->mapa[oi]; of = P->origem->mapa[of]; }
            int32_t arq;
            i->local.inicio = adc_origem_posicao(P->origem, P->texto_original, oi, &arq);
            i->local.fim = adc_origem_posicao(P->origem, P->texto_original, of, NULL);
            i->local.arquivo = arq;
            /* fim: coluna do último caractere (inclusiva) — adc_origem_posicao devolve a coluna do byte 'of' */
        }
        if (i->filhos) fixar_locais(P, i->filhos);
    }
}

ADC_Inline* adc_inlines_parsear(ADC_Contexto* pC, ADC_Fatia pTexto, const ADC_Origem* pOrigem, ADC_Subs pSubs) {
    Parser P; memset(&P, 0, sizeof P);
    P.ctx = pC; P.d = pTexto.dados; P.n = pTexto.tamanho; P.subs = pSubs; P.origem = pOrigem;
    if (pOrigem && pOrigem->mapa) P.texto_original = adc_fatia(NULL, pOrigem->tamanho_original);
    else P.texto_original = pTexto;
    /* o texto original para contagem de colunas: com mapa, precisamos dos bytes originais; quem chama
       passa pTexto já resolvido. Para contar colunas usamos o próprio texto resolvido quando não há
       mapa; com mapa, o chamador garante que 'tamanho_original' e os segmentos se referem ao original —
       e para a contagem de caracteres usamos o pré-processado como aproximação quando original não está
       disponível. */
    if (P.texto_original.dados == NULL) P.texto_original = pTexto;
    ADC_Inline* r;
    if (pSubs == ADC_SUBS_NENHUMA) {
        r = novo(&P, ADC_IN_RAW, 0, P.n);
        if (r) r->valor = pTexto;
    } else if (pSubs == ADC_SUBS_VERBATIM || pSubs == ADC_SUBS_VERBATIM_MARCAS) {
        P.so_marcas = pSubs == ADC_SUBS_VERBATIM_MARCAS;
        r = parsear_verbatim(&P);
    } else {
        r = parsear_faixa(&P, 0, P.n);
        r = fundir_textos(&P, r);
    }
    fixar_locais(&P, r);
    return r;
}
