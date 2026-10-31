/*
 * PDF direto da AST (IC-1), sobre o escritor próprio (IC-4). Motor de leiaute: páginas e margens, quebra de
 * linha e de página, parágrafos, títulos, listas, código, admonições, tabelas, imagens, cabeçalho/rodapé,
 * sumário (duas passagens), notas de rodapé, links e marcadores. Tema padrão da casa com sobrescrita por
 * JSON em "pdf" (DAD-8).
 */
#include "../adc_render.h"
#include "../pdf/adc_pdf_escritor.h"
#include "../pdf/adc_pdf_imagem.h"
#include "../pdf/adc_fontes.h"
#include "../../terceiros/miniz.h"
#include <math.h>

/* ------------------------------------------------------------------- tema ---- */

typedef struct Cor { double r, g, b; } Cor;

typedef struct Tema {
    double pagina_w, pagina_h;
    double margem_sup, margem_dir, margem_inf, margem_esq;
    double fonte_base;            /* pt */
    double entrelinha;            /* fator */
    const char* familia_corpo;    /* serif | sans | mono */
    const char* familia_titulos;
    const char* familia_codigo;
    double tam_titulo[7];         /* nível 0 (documento) .. 6 */
    Cor texto, titulo, link, codigo_fundo, codigo_borda, borda, cinza, cabecalho_tabela, sidebar_fundo;
    Cor adm[6];                   /* cor da barra por variante (1..5) */
    bool rodape;
    double espaco_bloco;          /* pt entre blocos */
    double recuo_lista;
    double tam_codigo;
} Tema;

static Cor cor(double r, double g, double b) { Cor c = { r / 255.0, g / 255.0, b / 255.0 }; return c; }

static Cor cor_de_texto(ADC_Fatia pT, Cor pPadrao) {
    /* "#RRGGBB" ou "RRGGBB" */
    if (pT.tamanho == 7 && pT.dados[0] == '#') { pT.dados++; pT.tamanho--; }
    if (pT.tamanho != 6) return pPadrao;
    int v[6];
    for (int i = 0; i < 6; i++) { char c = pT.dados[i]; v[i] = (c >= '0' && c <= '9') ? c - '0' : (c >= 'a' && c <= 'f') ? c - 'a' + 10 : (c >= 'A' && c <= 'F') ? c - 'A' + 10 : -1; if (v[i] < 0) return pPadrao; }
    return cor(v[0] * 16 + v[1], v[2] * 16 + v[3], v[4] * 16 + v[5]);
}

static void tema_padrao(Tema* T) {
    memset(T, 0, sizeof *T);
    T->pagina_w = 595.28; T->pagina_h = 841.89;              /* A4 */
    T->margem_sup = 56; T->margem_dir = 56; T->margem_inf = 56; T->margem_esq = 56;
    T->fonte_base = 10.5; T->entrelinha = 1.4;
    T->familia_corpo = "serif"; T->familia_titulos = "sans"; T->familia_codigo = "mono";
    T->tam_titulo[0] = 26; T->tam_titulo[1] = 20; T->tam_titulo[2] = 16; T->tam_titulo[3] = 13.5; T->tam_titulo[4] = 12; T->tam_titulo[5] = 11; T->tam_titulo[6] = 10.5;
    T->texto = cor(51, 51, 51); T->titulo = cor(186, 57, 37); T->link = cor(37, 87, 160);
    T->codigo_fundo = cor(247, 247, 248); T->codigo_borda = cor(220, 220, 220); T->borda = cor(200, 200, 200);
    T->cinza = cor(120, 120, 120); T->cabecalho_tabela = cor(240, 240, 240); T->sidebar_fundo = cor(243, 243, 242);
    T->adm[ADC_ADM_NOTE] = cor(25, 64, 124); T->adm[ADC_ADM_TIP] = cor(17, 128, 77); T->adm[ADC_ADM_IMPORTANT] = cor(191, 54, 12);
    T->adm[ADC_ADM_WARNING] = cor(191, 120, 0); T->adm[ADC_ADM_CAUTION] = cor(160, 30, 30);
    T->rodape = true; T->espaco_bloco = 8; T->recuo_lista = 18; T->tam_codigo = 9;
}

static void tema_aplicar_json(Tema* T, const ADC_Json* pJ) {
    if (!pJ || pJ->tipo != ADC_JSON_OBJETO) return;
    ADC_Fatia pagina = adc_json_texto(pJ, "pagina", "");
    if (adc_fatia_igual(pagina, "Letter") || adc_fatia_igual(pagina, "letter")) { T->pagina_w = 612; T->pagina_h = 792; }
    else if (adc_fatia_igual(pagina, "A5")) { T->pagina_w = 419.53; T->pagina_h = 595.28; }
    const ADC_Json* m = adc_json_membro(pJ, "margens");
    if (m && m->tipo == ADC_JSON_ARRAY && m->quantidade == 4) { const ADC_Json* v = m->filhos; double* alvo[4] = { &T->margem_sup, &T->margem_dir, &T->margem_inf, &T->margem_esq }; for (int i = 0; i < 4 && v; i++, v = v->proximo) if (v->tipo == ADC_JSON_NUMERO && v->numero >= 0 && v->numero < 300) *alvo[i] = v->numero; }
    double fb = adc_json_numero(pJ, "fonteBase", T->fonte_base); if (fb >= 6 && fb <= 24) T->fonte_base = fb;
    double el = adc_json_numero(pJ, "entrelinha", T->entrelinha); if (el >= 1.0 && el <= 2.5) T->entrelinha = el;
    ADC_Fatia f;
    f = adc_json_texto(pJ, "familiaCorpo", ""); if (adc_fatia_igual(f, "sans")) T->familia_corpo = "sans"; else if (adc_fatia_igual(f, "serif")) T->familia_corpo = "serif"; else if (adc_fatia_igual(f, "mono")) T->familia_corpo = "mono";
    f = adc_json_texto(pJ, "familiaTitulos", ""); if (adc_fatia_igual(f, "sans")) T->familia_titulos = "sans"; else if (adc_fatia_igual(f, "serif")) T->familia_titulos = "serif"; else if (adc_fatia_igual(f, "mono")) T->familia_titulos = "mono";
    f = adc_json_texto(pJ, "familiaCodigo", ""); if (adc_fatia_igual(f, "sans")) T->familia_codigo = "sans"; else if (adc_fatia_igual(f, "serif")) T->familia_codigo = "serif"; else if (adc_fatia_igual(f, "mono")) T->familia_codigo = "mono";
    T->rodape = adc_json_bool(pJ, "rodape", T->rodape);
    const ADC_Json* cores = adc_json_membro(pJ, "cores");
    if (cores) {
        T->texto = cor_de_texto(adc_json_texto(cores, "texto", ""), T->texto);
        T->titulo = cor_de_texto(adc_json_texto(cores, "titulo", ""), T->titulo);
        T->link = cor_de_texto(adc_json_texto(cores, "link", ""), T->link);
        T->codigo_fundo = cor_de_texto(adc_json_texto(cores, "codigoFundo", ""), T->codigo_fundo);
    }
}

/* ----------------------------------------------------------------- fontes ---- */

typedef struct R R;

typedef struct FonteCache { const char* familia; bool negrito, italico; int32_t indice; ADC_PdfFonte* fonte; } FonteCache;
typedef struct TocItem { const ADC_Bloco* secao; int32_t pagina; double y; } TocItem;

struct R {
    ADC_Contexto*  ctx;
    ADC_PdfDoc     doc;
    Tema           T;
    FonteCache     fontes[12];
    int32_t        n_fontes;
    /* fluxo */
    int32_t        pagina;          /* índice da página corrente */
    double         y;               /* cursor: distância do topo da página (pt) */
    double         x_esq, largura;  /* área corrente */
    bool           medir;           /* só mede (não emite) */
    bool           sem_paginacao;   /* células: cresce sem quebrar página */
    double         altura_medida;   /* acumulada em modo medir */
    int32_t        passagem;        /* 1 ou 2 (sumário) */
    bool           avisar;          /* emitir avisos de imagem (só na passagem final, para não duplicar) */
    bool           suprimir_espaco; /* o próximo espaço entre blocos não se aplica (1º filho de um contêiner) */
    /* sumário */
    TocItem*       toc_itens; int32_t n_toc, cap_toc;
    int32_t        paginas_toc;     /* páginas que o sumário ocupa (passagem 2) */
    int32_t        contadores[8];
    bool           sectnums;
    int32_t        nivel_lista;
    bool           falhou;
    int32_t        notas_renderizadas;
    int32_t        cont_figura, cont_tabela, cont_exemplo;
    ADC_Fatia      titulo_doc;
};

static bool inflar_fonte(R* r, const ADC_FonteEmbutida* pE, const uint8_t** pDados) {
    uint8_t* d = (uint8_t*)adc_arena_alocar(r->ctx->arena, (size_t)pE->tamanho_ttf);
    if (!d) return false;
    mz_ulong fora = (mz_ulong)pE->tamanho_ttf;
    if (mz_uncompress(d, &fora, pE->dados_zlib, (mz_ulong)pE->tamanho_zlib) != MZ_OK || (int32_t)fora != pE->tamanho_ttf) return false;
    *pDados = d;
    return true;
}

static int32_t fonte(R* r, const char* pFamilia, bool pNegrito, bool pItalico) {
    for (int32_t i = 0; i < r->n_fontes; i++) if (strcmp(r->fontes[i].familia, pFamilia) == 0 && r->fontes[i].negrito == pNegrito && r->fontes[i].italico == pItalico) return r->fontes[i].indice;
    char ident[64]; snprintf(ident, sizeof ident, "%s_%s", pFamilia, pNegrito && pItalico ? "negrito_italico" : pNegrito ? "negrito" : pItalico ? "italico" : "regular");
    const ADC_FonteEmbutida* e = NULL;
    for (int32_t i = 0; i < adc_fontes_embutidas_quantidade; i++) if (strcmp(adc_fontes_embutidas[i].identificador, ident) == 0) { e = &adc_fontes_embutidas[i]; break; }
    if (!e) return r->n_fontes ? r->fontes[0].indice : -1;
    const uint8_t* dados;
    if (!inflar_fonte(r, e, &dados)) { adc_msgs_adicionar(&r->ctx->msgs, ADC_NIVEL_ERRO, 0, 0, "PDF: não foi possível descomprimir a fonte embutida %s.", e->nome_pdf); r->falhou = true; return -1; }
    ADC_PdfFonte* f = ADC_NOVO(r->ctx->arena, ADC_PdfFonte);
    if (!f || !adc_pdf_fonte_carregar(f, r->ctx->arena, dados, e->tamanho_ttf, e->nome_pdf)) { adc_msgs_adicionar(&r->ctx->msgs, ADC_NIVEL_ERRO, 0, 0, "PDF: a fonte embutida %s não pôde ser lida.", e->nome_pdf); r->falhou = true; return -1; }
    int32_t idx = adc_pdf_doc_fonte(&r->doc, f);
    if (idx < 0 || r->n_fontes >= 12) return -1;
    r->fontes[r->n_fontes].familia = pFamilia; r->fontes[r->n_fontes].negrito = pNegrito; r->fontes[r->n_fontes].italico = pItalico; r->fontes[r->n_fontes].indice = idx; r->fontes[r->n_fontes].fonte = f;
    r->n_fontes++;
    return idx;
}

static ADC_PdfFonte* fonte_obj(R* r, int32_t pIndice) { for (int32_t i = 0; i < r->n_fontes; i++) if (r->fontes[i].indice == pIndice) return r->fontes[i].fonte; return NULL; }

/* ------------------------------------------------------------------ fluxo ---- */

static double limite_inferior(R* r) { return r->T.pagina_h - r->T.margem_inf; }
static double area_largura(R* r) { return r->T.pagina_w - r->T.margem_esq - r->T.margem_dir; }
static ADC_PdfPagina* pagina(R* r) { return adc_pdf_doc_pagina(&r->doc, r->pagina); }
/* y do PDF (origem embaixo) para um cursor "distância do topo" */
static double ypdf(R* r, double pTopo) { return r->T.pagina_h - pTopo; }

static void nova_pagina(R* r) {
    if (r->medir || r->sem_paginacao) { /* em medição/célula não há páginas: só cresce */ return; }
    r->pagina = adc_pdf_doc_nova_pagina(&r->doc, r->T.pagina_w, r->T.pagina_h);
    r->y = r->T.margem_sup;
}

/* Garante pAltura disponível; quebra a página se preciso. Devolve true se quebrou. */
static bool garantir(R* r, double pAltura) {
    if (r->sem_paginacao) return false;
    if (r->y + pAltura > limite_inferior(r) && r->y > r->T.margem_sup + 1) { nova_pagina(r); return true; }
    return false;
}

static void avancar(R* r, double pAltura) {
    r->y += pAltura;
    if (r->medir || r->sem_paginacao) r->altura_medida += pAltura;
}

/* ------------------------------------------------------------------- runs ---- */

typedef struct Estilo { const char* familia; bool negrito, italico, mono; double tam; Cor cor; ADC_Fatia uri; ADC_Fatia destino; double desloc; bool sup; } Estilo;

typedef struct Atomo {
    ADC_Fatia   texto;      /* palavra (sem espaço) ou " " */
    int32_t     fonte;
    double      tam;
    double      largura;
    Cor         cor;
    ADC_Fatia   uri, destino;
    double      desloc;     /* sup/sub */
    bool        quebra;     /* quebra de linha forçada */
    bool        espaco;
    bool        fundo;      /* código inline: fundo cinza */
    int32_t     imagem;     /* ≥0: imagem inline (índice no doc) */
    double      img_w, img_h;
    struct Atomo* proximo;
} Atomo;

typedef struct Atomos { Atomo* primeiro; Atomo* ultimo; int32_t n; } Atomos;

static void atomo_anexar(Atomos* A, Atomo* a) { a->proximo = NULL; if (A->ultimo) A->ultimo->proximo = a; else A->primeiro = a; A->ultimo = a; A->n++; }

static Atomo* novo_atomo(R* r, Atomos* A, ADC_Fatia pTexto, const Estilo* E, bool pEspaco) {
    Atomo* a = ADC_NOVO(r->ctx->arena, Atomo);
    if (!a) return NULL;
    a->texto = pTexto; a->fonte = fonte(r, E->mono ? r->T.familia_codigo : E->familia, E->negrito, E->italico);
    a->tam = E->tam; a->cor = E->cor; a->uri = E->uri; a->destino = E->destino; a->desloc = E->desloc; a->espaco = pEspaco; a->fundo = E->mono; a->imagem = -1;
    ADC_PdfFonte* f = fonte_obj(r, a->fonte);
    a->largura = f ? adc_pdf_fonte_largura(f, pTexto, E->tam, false) : 0;
    atomo_anexar(A, a);
    return a;
}

/* Divide texto em palavras e espaços (quebras de linha do fonte viram espaço). */
static void texto_em_atomos(R* r, Atomos* A, ADC_Fatia pT, const Estilo* E) {
    int32_t i = 0;
    while (i < pT.tamanho) {
        if (pT.dados[i] == ' ' || pT.dados[i] == '\n' || pT.dados[i] == '\t') {
            while (i < pT.tamanho && (pT.dados[i] == ' ' || pT.dados[i] == '\n' || pT.dados[i] == '\t')) i++;
            novo_atomo(r, A, adc_fatia_cstr(" "), E, true);
            continue;
        }
        int32_t ini = i;
        while (i < pT.tamanho && pT.dados[i] != ' ' && pT.dados[i] != '\n' && pT.dados[i] != '\t') i++;
        novo_atomo(r, A, adc_fatia(pT.dados + ini, i - ini), E, false);
    }
}

static ADC_Fatia charref_para_texto(R* r, ADC_Fatia v) {
    uint32_t cp = 0;
    if (v.tamanho > 1 && v.dados[0] == '#') {
        bool hex = v.dados[1] == 'x' || v.dados[1] == 'X';
        for (int32_t k = hex ? 2 : 1; k < v.tamanho; k++) { char c = v.dados[k]; int d = (c >= '0' && c <= '9') ? c - '0' : (c >= 'a' && c <= 'f') ? c - 'a' + 10 : (c >= 'A' && c <= 'F') ? c - 'A' + 10 : -1; if (d < 0) break; cp = hex ? cp * 16 + (uint32_t)d : cp * 10 + (uint32_t)d; }
    } else if (adc_fatia_igual(v, "amp")) cp = '&'; else if (adc_fatia_igual(v, "lt")) cp = '<'; else if (adc_fatia_igual(v, "gt")) cp = '>';
    else if (adc_fatia_igual(v, "quot")) cp = '"'; else if (adc_fatia_igual(v, "nbsp")) cp = 0xA0; else if (adc_fatia_igual(v, "copy")) cp = 0xA9; else if (adc_fatia_igual(v, "reg")) cp = 0xAE;
    else if (adc_fatia_igual(v, "trade")) cp = 0x2122; else if (adc_fatia_igual(v, "hellip")) cp = 0x2026; else if (adc_fatia_igual(v, "mdash")) cp = 0x2014; else if (adc_fatia_igual(v, "ndash")) cp = 0x2013;
    if (cp == 0x200B || cp == 0x2060) return adc_fatia("", 0);  /* largura zero: ignora */
    if (cp == 0x2009) cp = ' ';
    if (!cp) return adc_fatia("", 0);
    char* c = (char*)adc_arena_alocar(r->ctx->arena, 5);
    if (!c) return adc_fatia("", 0);
    int32_t n = adc_utf8_codificar(cp, c);
    return adc_fatia(c, n);
}

/* Entidades embutidas em RAW (&#8212;) → caracteres. */
static ADC_Fatia raw_para_texto(R* r, ADC_Fatia v) {
    if (adc_fatia_indice(v, '&') < 0) return v;
    ADC_Buffer b; adc_buffer_iniciar(&b);
    for (int32_t i = 0; i < v.tamanho; i++) {
        if (v.dados[i] == '&') {
            int32_t f = i + 1; while (f < v.tamanho && v.dados[f] != ';' && f - i < 12) f++;
            if (f < v.tamanho && v.dados[f] == ';') { ADC_Fatia t = charref_para_texto(r, adc_fatia(v.dados + i + 1, f - i - 1)); adc_buffer_fatia(&b, t); i = f; continue; }
        }
        if (v.dados[i] == '<') { int32_t f = i; while (f < v.tamanho && v.dados[f] != '>') f++; i = f; continue; } /* remove tags */
        adc_buffer_char(&b, v.dados[i]);
    }
    int32_t n; uint8_t* d = adc_buffer_entregar(&b, &n);
    char* c = d ? adc_arena_copiar(r->ctx->arena, (const char*)d, n) : NULL; free(d);
    return c ? adc_fatia(c, n) : adc_fatia("", 0);
}

static void inlines_em_atomos(R* r, Atomos* A, const ADC_Inline* pI, Estilo E);

static void inline_em_atomos(R* r, Atomos* A, const ADC_Inline* i, Estilo E) {
    switch (i->tipo) {
        case ADC_IN_TEXTO: texto_em_atomos(r, A, i->valor, &E); break;
        case ADC_IN_CHARREF:
            if (adc_fatia_igual(i->valor, "callout")) { Estilo e2 = E; e2.negrito = true; char tmp[12]; int n = snprintf(tmp, sizeof tmp, "(%d)", i->numero); novo_atomo(r, A, adc_fatia(adc_arena_copiar(r->ctx->arena, tmp, n), n), &e2, false); }
            else { ADC_Fatia t = charref_para_texto(r, i->valor); if (t.tamanho) texto_em_atomos(r, A, t, &E); }
            break;
        case ADC_IN_RAW: if (i->valor.dados == NULL && i->filhos) inlines_em_atomos(r, A, i->filhos, E); else { ADC_Fatia t = raw_para_texto(r, i->valor); texto_em_atomos(r, A, t, &E); } break;
        case ADC_IN_SPAN: {
            Estilo e2 = E;
            switch (i->variante_span) {
                case ADC_SPAN_STRONG: e2.negrito = true; break;
                case ADC_SPAN_EMPHASIS: e2.italico = true; break;
                case ADC_SPAN_CODE: e2.mono = true; e2.tam = E.tam * 0.9; break;
                case ADC_SPAN_MARK: break;
                case ADC_SPAN_SUPERSCRIPT: e2.tam = E.tam * 0.7; e2.desloc = E.tam * 0.4; break;
                case ADC_SPAN_SUBSCRIPT: e2.tam = E.tam * 0.7; e2.desloc = -E.tam * 0.2; break;
            }
            inlines_em_atomos(r, A, i->filhos, e2);
            break;
        }
        case ADC_IN_REF: {
            Estilo e2 = E; e2.cor = r->T.link;
            if (i->variante_ref == ADC_REF_LINK) e2.uri = i->alvo;
            else { ADC_Fatia alvo = i->alvo; int32_t hash = adc_fatia_indice(alvo, '#'); e2.destino = hash >= 0 ? adc_fatia(alvo.dados + hash + 1, alvo.tamanho - hash - 1) : alvo; }
            if (i->filhos) inlines_em_atomos(r, A, i->filhos, e2);
            else {
                const ADC_Inline* t = adc_render_texto_xref(r->ctx, e2.destino);
                if (t) inlines_em_atomos(r, A, t, e2);
                else { ADC_Buffer b; adc_buffer_iniciar(&b); adc_buffer_char(&b, '['); adc_buffer_fatia(&b, e2.destino); adc_buffer_char(&b, ']'); int32_t n; uint8_t* d = adc_buffer_entregar(&b, &n); char* c = d ? adc_arena_copiar(r->ctx->arena, (const char*)d, n) : NULL; free(d); if (c) texto_em_atomos(r, A, adc_fatia(c, n), &e2); }
            }
            break;
        }
        case ADC_IN_IMAGEM: {
            /* imagem inline: tenta carregar; senão o texto alternativo */
            bool a; ADC_Fatia alt = adc_atributo_obter(i->atributos, "alt", &a);
            int32_t lw = 0, lh = 0;
            int32_t idx = adc_pdf_doc_imagem_por_chave(&r->doc, i->alvo, &lw, &lh);
            if (idx < 0 && adc_ctx_tem_resolvedor(r->ctx)) {
                const uint8_t* bytes; int32_t tam;
                if (adc_ctx_resolver_recurso(r->ctx, 2, i->alvo, &bytes, &tam) == 0 && bytes) {
                    ADC_ImagemDecodificada im;
                    if (adc_imagem_decodificar(bytes, tam, &im)) { idx = adc_pdf_doc_imagem(&r->doc, i->alvo, im.largura, im.altura, im.componentes, im.pixels, im.tamanho_pixels, im.alpha, im.tamanho_alpha, im.dct, im.cmyk_invertido); lw = im.largura; lh = im.altura; adc_imagem_liberar(&im); }
                    else adc_msgs_adicionar(&r->ctx->msgs, ADC_NIVEL_AVISO, i->local.inicio.linha, i->local.inicio.coluna, "PDF: imagem inline '%.*s' não pôde ser decodificada (%s); o texto alternativo foi usado.", i->alvo.tamanho, i->alvo.dados, adc_msgs_tr(&r->ctx->msgs, im.erro ? im.erro : "?"));
                }
            }
            if (idx >= 0 && lw > 0 && lh > 0) {
                Atomo* at = ADC_NOVO(r->ctx->arena, Atomo);
                if (at) {
                    ADC_Fatia w = adc_atributo_obter(i->atributos, "width", &a);
                    double larg = a ? adc_fatia_para_int(w, 0) * 0.75 : E.tam * 1.2 * lw / lh;
                    if (larg <= 0) larg = E.tam * 1.2 * lw / lh;
                    at->imagem = idx; at->img_w = larg; at->img_h = larg * lh / lw; at->largura = larg; at->tam = E.tam; at->fonte = fonte(r, E.familia, false, false); at->texto = adc_fatia("", 0);
                    atomo_anexar(A, at);
                }
            } else if (a) texto_em_atomos(r, A, alt, &E);
            break;
        }
        case ADC_IN_NOTA: { Estilo e2 = E; e2.tam = E.tam * 0.7; e2.desloc = E.tam * 0.4; e2.cor = r->T.link; char tmp[12]; int n = snprintf(tmp, sizeof tmp, "[%d]", i->numero); novo_atomo(r, A, adc_fatia(adc_arena_copiar(r->ctx->arena, tmp, n), n), &e2, false); break; }
        case ADC_IN_QUEBRA_LINHA: { Atomo* at = ADC_NOVO(r->ctx->arena, Atomo); if (at) { at->quebra = true; at->imagem = -1; at->tam = E.tam; at->fonte = fonte(r, E.familia, false, false); atomo_anexar(A, at); } break; }
        case ADC_IN_ANCORA: { Atomo* at = ADC_NOVO(r->ctx->arena, Atomo); if (at) { at->imagem = -1; at->tam = E.tam; at->fonte = fonte(r, E.familia, false, false); at->texto = adc_fatia("", 0); at->destino = adc_fatia(NULL, 0); at->uri = adc_fatia(NULL, 0); at->cor = E.cor; /* âncora: registra o destino na posição ao desenhar (usa texto vazio e marca em 'desloc' = -9999) */ at->desloc = -9999; at->largura = 0; at->proximo = NULL; at->espaco = false; at->fundo = false; at->quebra = false; { ADC_Fatia* id = ADC_NOVO(r->ctx->arena, ADC_Fatia); if (id) { *id = i->id; at->texto = *id; } } atomo_anexar(A, at); } break; }
        case ADC_IN_TECLA: { Estilo e2 = E; e2.mono = true; e2.tam = E.tam * 0.9; for (const ADC_Inline* k = i->filhos; k; k = k->proximo) { if (k != i->filhos) novo_atomo(r, A, adc_fatia_cstr("+"), &E, false); novo_atomo(r, A, k->valor, &e2, false); } break; }
        case ADC_IN_BOTAO: { Estilo e2 = E; e2.negrito = true; inlines_em_atomos(r, A, i->filhos, e2); break; }
        case ADC_IN_MENU: { Estilo e2 = E; e2.negrito = true; for (const ADC_Inline* k = i->filhos; k; k = k->proximo) { if (k != i->filhos) texto_em_atomos(r, A, adc_fatia_cstr(" \xE2\x80\xBA "), &E); texto_em_atomos(r, A, k->valor, &e2); } break; }
        case ADC_IN_ICONE: { ADC_Buffer b; adc_buffer_iniciar(&b); adc_buffer_char(&b, '['); adc_buffer_fatia(&b, i->alvo); adc_buffer_char(&b, ']'); int32_t n; uint8_t* d = adc_buffer_entregar(&b, &n); char* c = d ? adc_arena_copiar(r->ctx->arena, (const char*)d, n) : NULL; free(d); if (c) texto_em_atomos(r, A, adc_fatia(c, n), &E); break; }
        case ADC_IN_INDICE: if (i->filhos) inlines_em_atomos(r, A, i->filhos, E); break;
        default: break;
    }
}

static void inlines_em_atomos(R* r, Atomos* A, const ADC_Inline* pI, Estilo E) { for (const ADC_Inline* i = pI; i; i = i->proximo) inline_em_atomos(r, A, i, E); }

static Estilo estilo_corpo(R* r) { Estilo E; memset(&E, 0, sizeof E); E.familia = r->T.familia_corpo; E.tam = r->T.fonte_base; E.cor = r->T.texto; return E; }

/* Desenha uma linha de átomos [a, fim) a partir de x, baseline em pBase (coord. do topo), com altura pAltura. */
static void desenhar_linha(R* r, Atomo* a, Atomo* fim, double x, double pTopo, double pAltura, double pAscMax) {
    if (r->medir) return;
    ADC_PdfPagina* pg = pagina(r);
    if (!pg) return;
    double base = pTopo + pAscMax;   /* baseline a partir do topo */
    for (Atomo* t = a; t && t != fim; t = t->proximo) {
        if (t->quebra) continue;
        if (t->desloc == -9999) { adc_pdf_doc_destino(&r->doc, t->texto, r->pagina, ypdf(r, pTopo)); continue; }
        if (t->imagem >= 0) { adc_pdf_pg_imagem(&r->doc, pg, t->imagem, x, ypdf(r, base), t->img_w, t->img_h); x += t->largura; continue; }
        if (t->fundo && !t->espaco) { adc_pdf_pg_cor_preenchimento(pg, r->T.codigo_fundo.r, r->T.codigo_fundo.g, r->T.codigo_fundo.b); adc_pdf_pg_retangulo(pg, x - 1, ypdf(r, base) - t->tam * 0.25, t->largura + 2, t->tam * 1.1, true, false); }
        if (!t->espaco) {
            adc_pdf_pg_cor_preenchimento(pg, t->cor.r, t->cor.g, t->cor.b);
            adc_pdf_pg_texto(&r->doc, pg, t->fonte, t->tam, x, ypdf(r, base) + t->desloc, t->texto);
            if (t->uri.tamanho || t->destino.tamanho) {
                /* sublinha e anota */
                adc_pdf_pg_cor_traco(pg, t->cor.r, t->cor.g, t->cor.b); adc_pdf_pg_largura_traco(pg, 0.5);
                adc_pdf_pg_linha(pg, x, ypdf(r, base) - 1.5, x + t->largura, ypdf(r, base) - 1.5);
                if (t->uri.tamanho) adc_pdf_pg_link_uri(&r->doc, pg, x, ypdf(r, base) - 3, t->largura, pAltura, t->uri);
                else adc_pdf_pg_link_destino(&r->doc, pg, x, ypdf(r, base) - 3, t->largura, pAltura, t->destino);
            }
        }
        x += t->largura;
    }
}

/* Quebra os átomos em linhas dentro de pLargura, desenhando a partir do cursor. pRecuoPrimeira: recuo da 1ª linha. */
static void fluir_atomos(R* r, Atomos* A, double pLargura, double pRecuoPrimeira, int pAlinhamento) {
    Atomo* a = A->primeiro;
    bool primeira = true;
    /* pula espaços iniciais */
    while (a && a->espaco) a = a->proximo;
    while (a) {
        double disp = pLargura - (primeira ? pRecuoPrimeira : 0);
        double x0 = r->x_esq + (primeira ? pRecuoPrimeira : 0);
        Atomo* fim = a; double w = 0, tam_max = 0, asc_max = 0;
        Atomo* ultimo_espaco = NULL; double w_ate_espaco = 0;
        while (fim) {
            if (fim->quebra) break;
            double wf = fim->largura;
            if (w + wf > disp && fim != a && !fim->espaco) {
                /* quebra na última oportunidade (espaço) ou aqui */
                if (ultimo_espaco) { fim = ultimo_espaco; w = w_ate_espaco; }
                break;
            }
            if (fim->espaco) { ultimo_espaco = fim; w_ate_espaco = w; }
            if (fim->tam > tam_max) tam_max = fim->tam;
            { ADC_PdfFonte* f = fonte_obj(r, fim->fonte); double asc = f ? f->ascender * fim->tam / 1000.0 : fim->tam * 0.8; if (fim->imagem >= 0) asc = fim->img_h; if (asc > asc_max) asc_max = asc; }
            w += wf;
            fim = fim->proximo;
            if (w > disp && fim && fim->espaco) break; /* palavra sozinha maior que a linha: segue */
        }
        if (fim == a && a->quebra) { /* linha vazia por quebra forçada */
            double h = a->tam * r->T.entrelinha; garantir(r, h); avancar(r, h); a = a->proximo; primeira = false; continue;
        }
        if (fim == a) { /* palavra maior que a linha: quebra por caractere */
            ADC_PdfFonte* f = fonte_obj(r, a->fonte);
            int32_t p = 0; int32_t ini = 0; double acum = 0;
            while (p < a->texto.tamanho) {
                int32_t q = p; uint32_t cp = adc_utf8_decodificar(a->texto.dados, a->texto.tamanho, &q);
                double cw = f ? adc_pdf_fonte_avanco(f, adc_pdf_fonte_gid(f, cp)) * a->tam / 1000.0 : a->tam * 0.5;
                if (acum + cw > disp && q - ini > 1 && p > ini) {
                    Atomo pedaco = *a; pedaco.texto = adc_fatia(a->texto.dados + ini, p - ini); pedaco.largura = acum; pedaco.proximo = NULL;
                    double h = a->tam * r->T.entrelinha; double asc = f ? f->ascender * a->tam / 1000.0 : a->tam * 0.8;
                    garantir(r, h); desenhar_linha(r, &pedaco, NULL, x0, r->y, h, asc); avancar(r, h);
                    ini = p; acum = 0; primeira = false; x0 = r->x_esq; disp = pLargura;
                }
                acum += cw; p = q;
            }
            if (ini < a->texto.tamanho) { Atomo resto = *a; resto.texto = adc_fatia(a->texto.dados + ini, a->texto.tamanho - ini); resto.largura = acum; a = &resto; /* continua como átomo normal */
                /* fluimos o resto junto com os próximos: reconstruímos a cadeia temporariamente */
                Atomo* copia = ADC_NOVO(r->ctx->arena, Atomo); if (!copia) return; *copia = resto; copia->proximo = A->primeiro ? NULL : NULL;
                copia->proximo = fim->proximo ? fim->proximo : NULL;
                /* simplificação: desenha o resto sozinho na linha e segue */
                double h = a->tam * r->T.entrelinha; double asc = f ? f->ascender * a->tam / 1000.0 : a->tam * 0.8;
                garantir(r, h);
                /* tenta encaixar os próximos átomos na mesma linha */
                Atomo* seg = fim->proximo; double w2 = acum; Atomo* fim2 = seg; Atomo* ue = NULL; double wue = 0;
                while (fim2 && !fim2->quebra) { if (w2 + fim2->largura > disp && !fim2->espaco) { if (ue) { fim2 = ue; w2 = wue; } break; } if (fim2->espaco) { ue = fim2; wue = w2; } w2 += fim2->largura; fim2 = fim2->proximo; }
                copia->proximo = seg;
                desenhar_linha(r, copia, fim2, x0, r->y, h, asc);
                avancar(r, h);
                a = fim2; while (a && a->espaco) a = a->proximo; primeira = false;
                continue;
            }
            a = fim->proximo; primeira = false;
            continue;
        }
        double h = tam_max * r->T.entrelinha;
        if (asc_max > tam_max * 1.2) h = asc_max + tam_max * 0.4; /* linha com imagem */
        garantir(r, h);
        double x = x0;
        if (pAlinhamento == 1) x = x0 + (disp - w) / 2; else if (pAlinhamento == 2) x = x0 + (disp - w);
        desenhar_linha(r, a, fim, x, r->y, h, asc_max);
        avancar(r, h);
        a = fim;
        if (a && a->quebra) a = a->proximo;
        while (a && a->espaco) a = a->proximo;
        primeira = false;
    }
}

static void paragrafo_inlines(R* r, const ADC_Inline* pI, Estilo E, int pAlinhamento) {
    Atomos A = { NULL, NULL, 0 };
    inlines_em_atomos(r, &A, pI, E);
    if (!A.primeiro) return;
    fluir_atomos(r, &A, r->largura, 0, pAlinhamento);
}

static void texto_simples(R* r, ADC_Fatia pT, Estilo E, int pAlinhamento) {
    Atomos A = { NULL, NULL, 0 };
    texto_em_atomos(r, &A, pT, &E);
    if (!A.primeiro) return;
    fluir_atomos(r, &A, r->largura, 0, pAlinhamento);
}

/* --------------------------------------------------------------- blocos ---- */

static void blocos(R* r, const ADC_Bloco* pPrimeiro);

static void espaco(R* r, double pQuanto) {
    if (r->suprimir_espaco) { r->suprimir_espaco = false; return; }
    if (r->y > r->T.margem_sup + 0.1 || r->sem_paginacao) avancar(r, pQuanto);
}

static void titulo_de_bloco(R* r, const ADC_Bloco* b, const char* pLegenda, int32_t* pContador) {
    if (!b->titulo) return;
    Estilo E = estilo_corpo(r); E.italico = true; E.cor = r->T.titulo; E.tam = r->T.fonte_base * 0.95;
    Atomos A = { NULL, NULL, 0 };
    if (pLegenda) { ADC_Buffer leg; adc_buffer_iniciar(&leg); if (adc_render_legenda(r->ctx, b, &leg, pLegenda, pContador)) { char* c = adc_arena_copiar(r->ctx->arena, (const char*)leg.dados, leg.tamanho); if (c) texto_em_atomos(r, &A, adc_fatia(c, leg.tamanho), &E); } adc_buffer_liberar(&leg); }
    inlines_em_atomos(r, &A, b->titulo, E);
    if (A.primeiro) { fluir_atomos(r, &A, r->largura, 0, 0); avancar(r, 2); }
}

static void registrar_destino_bloco(R* r, const ADC_Bloco* b) {
    if (b->meta.id.tamanho && !r->medir) adc_pdf_doc_destino(&r->doc, b->meta.id, r->pagina, ypdf(r, r->y));
}

static void paragrafo(R* r, const ADC_Bloco* b) {
    titulo_de_bloco(r, b, NULL, NULL);
    registrar_destino_bloco(r, b);
    int alinh = adc_lista_fatias_contem(&b->meta.papeis, "text-center") ? 1 : adc_lista_fatias_contem(&b->meta.papeis, "text-right") ? 2 : 0;
    Estilo E = estilo_corpo(r);
    if (adc_lista_fatias_contem(&b->meta.papeis, "lead")) E.tam = r->T.fonte_base * 1.2;
    paragrafo_inlines(r, b->inlines, E, alinh);
}

/* Bloco de código / literal: fundo cinza, mono, linhas preservadas, quebra por caractere. */
static void codigo(R* r, const ADC_Bloco* b) {
    titulo_de_bloco(r, b, b->tipo == ADC_BL_LISTING ? "listing-caption" : NULL, &r->ctx->contador_listing);
    registrar_destino_bloco(r, b);
    Estilo E = estilo_corpo(r); E.mono = true; E.tam = r->T.tam_codigo; E.familia = r->T.familia_codigo;
    int32_t fnt = fonte(r, r->T.familia_codigo, false, false);
    ADC_PdfFonte* f = fonte_obj(r, fnt);
    double pad = 6, lh = E.tam * 1.35;
    /* linhas (com callouts) a partir dos inlines verbatim */
    typedef struct Linha { Atomos A; struct Linha* prox; } Linha;
    Linha* primeira = NULL; Linha* ult = NULL; int32_t nlin = 0;
    #define NOVA_LINHA() do { Linha* L = ADC_NOVO(r->ctx->arena, Linha); if (L) { L->A.primeiro = L->A.ultimo = NULL; L->A.n = 0; L->prox = NULL; if (ult) ult->prox = L; else primeira = L; ult = L; nlin++; } } while (0)
    NOVA_LINHA();
    for (const ADC_Inline* i = b->inlines; i; i = i->proximo) {
        if (i->tipo == ADC_IN_CHARREF && adc_fatia_igual(i->valor, "callout")) { Estilo e2 = E; e2.negrito = true; char tmp[12]; int n = snprintf(tmp, sizeof tmp, " (%d)", i->numero); if (ult) novo_atomo(r, &ult->A, adc_fatia(adc_arena_copiar(r->ctx->arena, tmp, n), n), &e2, false); continue; }
        ADC_Fatia t = i->tipo == ADC_IN_RAW ? raw_para_texto(r, i->valor) : i->tipo == ADC_IN_TEXTO ? i->valor : adc_inlines_texto_puro(r->ctx->arena, i);
        int32_t ini = 0;
        for (int32_t k = 0; k <= t.tamanho; k++) {
            if (k == t.tamanho || t.dados[k] == '\n') {
                if (k > ini && ult) { /* texto da linha, preservando espaços: cada átomo é a linha inteira */ Atomo* a = ADC_NOVO(r->ctx->arena, Atomo); if (a) { a->texto = adc_fatia(t.dados + ini, k - ini); a->fonte = fnt; a->tam = E.tam; a->cor = E.cor; a->imagem = -1; a->largura = f ? adc_pdf_fonte_largura(f, a->texto, E.tam, false) : 0; atomo_anexar(&ult->A, a); } }
                if (k < t.tamanho) NOVA_LINHA();
                ini = k + 1;
            }
        }
    }
    #undef NOVA_LINHA
    /* remove linha final vazia */
    if (ult && ult->A.n == 0 && nlin > 1) { Linha* L = primeira; while (L->prox != ult) L = L->prox; L->prox = NULL; nlin--; }
    /* desenha linha a linha, com fundo por trecho de página */
    double largura_util = r->largura - 2 * pad;
    bool primeiro_trecho = true;
    double topo_trecho = 0;
    garantir(r, lh + 2 * pad);
    ADC_PdfPagina* pg = pagina(r);
    (void)pg;
    int32_t pagina_trecho = r->pagina;
    topo_trecho = r->y;
    avancar(r, pad);
    for (Linha* L = primeira; L; L = L->prox) {
        /* quebra por caractere para linhas longas */
        double w = 0; for (Atomo* a = L->A.primeiro; a; a = a->proximo) w += a->largura;
        int32_t n_visuais = 1;
        if (w > largura_util && L->A.primeiro) {
            Atomo* a = L->A.primeiro; /* só o primeiro átomo (texto) quebra; callouts ficam no fim */
            int32_t p = 0, ini = 0; double acum = 0;
            while (p < a->texto.tamanho) {
                int32_t q = p; uint32_t cp = adc_utf8_decodificar(a->texto.dados, a->texto.tamanho, &q);
                double cw = f ? adc_pdf_fonte_avanco(f, adc_pdf_fonte_gid(f, cp)) * E.tam / 1000.0 : E.tam * 0.6;
                if (acum + cw > largura_util && p > ini) {
                    if (r->y + lh > limite_inferior(r) && !r->sem_paginacao) {
                        /* fecha o trecho nesta página */
                        if (!r->medir && pagina(r)) { ADC_PdfPagina* pq = adc_pdf_doc_pagina(&r->doc, pagina_trecho); adc_pdf_pg_cor_preenchimento(pq, r->T.codigo_fundo.r, r->T.codigo_fundo.g, r->T.codigo_fundo.b); adc_pdf_pg_cor_traco(pq, r->T.codigo_borda.r, r->T.codigo_borda.g, r->T.codigo_borda.b); adc_pdf_pg_largura_traco(pq, 0.5); /* fundo é desenhado atrás: PDF pinta na ordem — então o fundo aqui cobriria o texto; por isso desenhamos o fundo ANTES de cada trecho (ver abaixo) */ }
                        nova_pagina(r); pagina_trecho = r->pagina; topo_trecho = r->y; primeiro_trecho = false;
                    }
                    if (!r->medir && pagina(r)) { adc_pdf_pg_cor_preenchimento(pagina(r), r->T.codigo_fundo.r, r->T.codigo_fundo.g, r->T.codigo_fundo.b); adc_pdf_pg_retangulo(pagina(r), r->x_esq, ypdf(r, r->y + lh), r->largura, lh, true, false); adc_pdf_pg_cor_preenchimento(pagina(r), E.cor.r, E.cor.g, E.cor.b); adc_pdf_pg_texto(&r->doc, pagina(r), fnt, E.tam, r->x_esq + pad, ypdf(r, r->y + E.tam * 1.0), adc_fatia(a->texto.dados + ini, p - ini)); }
                    avancar(r, lh); n_visuais++;
                    ini = p; acum = 0;
                }
                acum += cw; p = q;
            }
            /* resto da linha + callouts */
            if (r->y + lh > limite_inferior(r) && !r->sem_paginacao) { nova_pagina(r); pagina_trecho = r->pagina; topo_trecho = r->y; primeiro_trecho = false; }
            if (!r->medir && pagina(r)) {
                adc_pdf_pg_cor_preenchimento(pagina(r), r->T.codigo_fundo.r, r->T.codigo_fundo.g, r->T.codigo_fundo.b); adc_pdf_pg_retangulo(pagina(r), r->x_esq, ypdf(r, r->y + lh), r->largura, lh, true, false);
                double x = r->x_esq + pad;
                adc_pdf_pg_cor_preenchimento(pagina(r), E.cor.r, E.cor.g, E.cor.b);
                adc_pdf_pg_texto(&r->doc, pagina(r), fnt, E.tam, x, ypdf(r, r->y + E.tam * 1.0), adc_fatia(a->texto.dados + ini, a->texto.tamanho - ini));
                x += acum;
                for (Atomo* c = a->proximo; c; c = c->proximo) { adc_pdf_pg_texto(&r->doc, pagina(r), c->fonte, c->tam, x, ypdf(r, r->y + E.tam * 1.0), c->texto); x += c->largura; }
            }
            avancar(r, lh);
            continue;
        }
        if (r->y + lh > limite_inferior(r) && !r->sem_paginacao) { nova_pagina(r); pagina_trecho = r->pagina; topo_trecho = r->y; primeiro_trecho = false; }
        if (!r->medir && pagina(r)) {
            adc_pdf_pg_cor_preenchimento(pagina(r), r->T.codigo_fundo.r, r->T.codigo_fundo.g, r->T.codigo_fundo.b);
            adc_pdf_pg_retangulo(pagina(r), r->x_esq, ypdf(r, r->y + lh + (L == primeira ? pad : 0)), r->largura, lh + (L == primeira ? pad : 0), true, false);
            double x = r->x_esq + pad;
            for (Atomo* a = L->A.primeiro; a; a = a->proximo) { adc_pdf_pg_cor_preenchimento(pagina(r), a->cor.r, a->cor.g, a->cor.b); adc_pdf_pg_texto(&r->doc, pagina(r), a->fonte, a->tam, x, ypdf(r, r->y + E.tam * 1.0), a->texto); x += a->largura; }
        }
        avancar(r, lh);
    }
    if (!r->medir && pagina(r)) { adc_pdf_pg_cor_preenchimento(pagina(r), r->T.codigo_fundo.r, r->T.codigo_fundo.g, r->T.codigo_fundo.b); adc_pdf_pg_retangulo(pagina(r), r->x_esq, ypdf(r, r->y + pad), r->largura, pad, true, false); }
    avancar(r, pad);
    (void)primeiro_trecho; (void)topo_trecho;
}

static void admonicao(R* r, const ADC_Bloco* b) {
    registrar_destino_bloco(r, b);
    Cor c = r->T.adm[b->variante_adm];
    double rotulo_w = 78;
    /* mede o conteúdo */
    double x_salvo = r->x_esq, w_salvo = r->largura;
    double inicio = r->y; int32_t pag_inicio = r->pagina;
    garantir(r, r->T.fonte_base * 3);
    inicio = r->y; pag_inicio = r->pagina;
    r->x_esq += rotulo_w; r->largura -= rotulo_w;
    double y_antes = r->y;
    if (!r->medir && pagina(r)) {
        /* rótulo */
        Estilo E = estilo_corpo(r); E.negrito = true; E.tam = r->T.fonte_base * 0.72; E.cor = c; E.familia = r->T.familia_titulos;
        int32_t fnt = fonte(r, E.familia, true, false);
        ADC_Fatia rot = adc_render_rotulo_admonicao(r->ctx, b->variante_adm);
        /* maiúsculas: ASCII e Latin-1 (à..þ → À..Þ, salvo ÷ e ß) */
        char* up = adc_arena_copiar(r->ctx->arena, rot.dados, rot.tamanho);
        if (up) {
            for (int32_t k = 0; k < rot.tamanho; k++) {
                unsigned char c = (unsigned char)up[k];
                if (c >= 'a' && c <= 'z') up[k] = (char)(c - 32);
                else if (c == 0xC3 && k + 1 < rot.tamanho) { unsigned char d = (unsigned char)up[k + 1]; if (d >= 0xA0 && d <= 0xBE && d != 0xB7) up[k + 1] = (char)(d - 0x20); k++; }
            }
            rot = adc_fatia(up, rot.tamanho);
        }
        adc_pdf_pg_cor_preenchimento(pagina(r), c.r, c.g, c.b);
        adc_pdf_pg_texto(&r->doc, pagina(r), fnt, E.tam, x_salvo + 8, ypdf(r, r->y + E.tam * 1.1), rot);
    }
    titulo_de_bloco(r, b, NULL, NULL);
    r->suprimir_espaco = true;
    blocos(r, b->filhos);
    r->suprimir_espaco = false;
    double fim = r->y;
    if (!r->medir && pagina(r)) {
        /* barra à esquerda (na página inicial e, se quebrou, até o fim da inicial e da final — aproximação: só a inicial) */
        ADC_PdfPagina* p0 = adc_pdf_doc_pagina(&r->doc, pag_inicio);
        double y_fim = (r->pagina == pag_inicio) ? fim : limite_inferior(r);
        adc_pdf_pg_cor_preenchimento(p0, c.r, c.g, c.b);
        adc_pdf_pg_retangulo(p0, x_salvo, ypdf(r, y_fim), 3, y_fim - inicio, true, false);
        if (r->pagina != pag_inicio) { adc_pdf_pg_cor_preenchimento(pagina(r), c.r, c.g, c.b); adc_pdf_pg_retangulo(pagina(r), x_salvo, ypdf(r, fim), 3, fim - r->T.margem_sup, true, false); }
    }
    (void)y_antes;
    r->x_esq = x_salvo; r->largura = w_salvo;
}

static void caixa(R* r, const ADC_Bloco* b, bool pFundo, bool pBorda, const char* pLegenda, int32_t* pContador) {
    registrar_destino_bloco(r, b);
    double pad = 8;
    garantir(r, r->T.fonte_base * 3);
    double inicio = r->y; int32_t pag_inicio = r->pagina;
    double x_salvo = r->x_esq, w_salvo = r->largura;
    r->x_esq += pad; r->largura -= 2 * pad;
    avancar(r, pad);
    /* título dentro para sidebar (negrito), fora para exemplo (legenda) */
    if (b->tipo == ADC_BL_SIDEBAR && b->titulo) { Estilo E = estilo_corpo(r); E.negrito = true; E.familia = r->T.familia_titulos; paragrafo_inlines(r, b->titulo, E, 0); avancar(r, 3); }
    else if (b->titulo) { Estilo E = estilo_corpo(r); E.italico = true; E.cor = r->T.titulo; Atomos A = { NULL, NULL, 0 }; ADC_Buffer leg; adc_buffer_iniciar(&leg); if (pLegenda && adc_render_legenda(r->ctx, b, &leg, pLegenda, pContador)) { char* c = adc_arena_copiar(r->ctx->arena, (const char*)leg.dados, leg.tamanho); if (c) texto_em_atomos(r, &A, adc_fatia(c, leg.tamanho), &E); } adc_buffer_liberar(&leg); inlines_em_atomos(r, &A, b->titulo, E); if (A.primeiro) { fluir_atomos(r, &A, r->largura, 0, 0); avancar(r, 3); } }
    r->suprimir_espaco = true;
    blocos(r, b->filhos);
    r->suprimir_espaco = false;
    avancar(r, pad);
    double fim = r->y;
    if (!r->medir && pag_inicio == r->pagina && pagina(r)) {
        /* desenha fundo/borda ATRÁS: como o conteúdo já foi emitido, usamos um retângulo sem preenchimento (borda)
           e, para o fundo, um retângulo claro com a técnica de "pintar antes" não é possível aqui; então o fundo
           da barra lateral é um traço grosso à esquerda e uma borda fina — leitura equivalente. */
        ADC_PdfPagina* p = pagina(r);
        if (pFundo) { adc_pdf_pg_cor_preenchimento(p, r->T.borda.r, r->T.borda.g, r->T.borda.b); adc_pdf_pg_retangulo(p, x_salvo, ypdf(r, fim), 2.5, fim - inicio, true, false); }
        if (pBorda) { adc_pdf_pg_cor_traco(p, r->T.borda.r, r->T.borda.g, r->T.borda.b); adc_pdf_pg_largura_traco(p, 0.75); adc_pdf_pg_retangulo(p, x_salvo, ypdf(r, fim), w_salvo, fim - inicio, false, true); }
    } else if (!r->medir && pagina(r)) {
        ADC_PdfPagina* p0 = adc_pdf_doc_pagina(&r->doc, pag_inicio);
        if (p0 && (pFundo || pBorda)) { adc_pdf_pg_cor_preenchimento(p0, r->T.borda.r, r->T.borda.g, r->T.borda.b); adc_pdf_pg_retangulo(p0, x_salvo, ypdf(r, limite_inferior(r)), 2.5, limite_inferior(r) - inicio, true, false); }
        adc_pdf_pg_cor_preenchimento(pagina(r), r->T.borda.r, r->T.borda.g, r->T.borda.b); adc_pdf_pg_retangulo(pagina(r), x_salvo, ypdf(r, fim), 2.5, fim - r->T.margem_sup, true, false);
    }
    r->x_esq = x_salvo; r->largura = w_salvo;
}

static void citacao(R* r, const ADC_Bloco* b) {
    registrar_destino_bloco(r, b);
    titulo_de_bloco(r, b, NULL, NULL);
    garantir(r, r->T.fonte_base * 2.5);
    double inicio = r->y; int32_t pag_inicio = r->pagina;
    double x_salvo = r->x_esq, w_salvo = r->largura;
    r->x_esq += 14; r->largura -= 14;
    Estilo E = estilo_corpo(r); E.italico = b->tipo != ADC_BL_VERSE; E.cor = r->T.cinza;
    if (b->tipo == ADC_BL_VERSE) {
        /* preserva as linhas */
        ADC_Fatia t = adc_inlines_texto_puro(r->ctx->arena, b->inlines);
        int32_t ini = 0;
        for (int32_t k = 0; k <= t.tamanho; k++) if (k == t.tamanho || t.dados[k] == '\n') { ADC_Fatia l = adc_fatia(t.dados + ini, k - ini); if (l.tamanho) texto_simples(r, l, E, 0); else avancar(r, E.tam * r->T.entrelinha); ini = k + 1; }
    } else if (b->forma == ADC_FORMA_PARAGRAFO && b->filhos && b->filhos->tipo == ADC_BL_PARAGRAFO && !b->filhos->proximo) paragrafo_inlines(r, b->filhos->inlines, E, 0);
    else { r->suprimir_espaco = true; blocos(r, b->filhos); r->suprimir_espaco = false; }
    if (b->atribuicao.tamanho || b->citacao.tamanho) {
        avancar(r, 3);
        ADC_Buffer a; adc_buffer_iniciar(&a); adc_buffer_texto(&a, "\xE2\x80\x94 ");
        if (b->atribuicao.tamanho) adc_buffer_fatia(&a, b->atribuicao);
        if (b->citacao.tamanho) { if (b->atribuicao.tamanho) adc_buffer_texto(&a, ", "); adc_buffer_fatia(&a, b->citacao); }
        char* c = adc_arena_copiar(r->ctx->arena, (const char*)a.dados, a.tamanho); int32_t n = a.tamanho; adc_buffer_liberar(&a);
        Estilo E2 = estilo_corpo(r); E2.tam = r->T.fonte_base * 0.9; E2.cor = r->T.cinza;
        if (c) texto_simples(r, adc_fatia(c, n), E2, 2);
    }
    double fim = r->y;
    if (!r->medir && pagina(r)) {
        ADC_PdfPagina* p0 = adc_pdf_doc_pagina(&r->doc, pag_inicio);
        double yf = pag_inicio == r->pagina ? fim : limite_inferior(r);
        adc_pdf_pg_cor_preenchimento(p0, r->T.borda.r, r->T.borda.g, r->T.borda.b); adc_pdf_pg_retangulo(p0, x_salvo, ypdf(r, yf), 3, yf - inicio, true, false);
    }
    r->x_esq = x_salvo; r->largura = w_salvo;
}

static const char* numero_item(R* r, const ADC_Bloco* lista, int32_t pIndice, char* pBuf, size_t pTam) {
    ADC_Fatia m = lista->marcador;
    int32_t n = (lista->numero > 0 ? lista->numero : 1) + pIndice;
    const char* est = "arabic";
    if (adc_fatia_igual(lista->meta.estilo, "loweralpha") || adc_fatia_igual(lista->meta.estilo, "upperalpha") || adc_fatia_igual(lista->meta.estilo, "lowerroman") || adc_fatia_igual(lista->meta.estilo, "upperroman")) { static char tmp[16]; snprintf(tmp, sizeof tmp, "%.*s", lista->meta.estilo.tamanho, lista->meta.estilo.dados); est = tmp; }
    else if (m.tamanho >= 2 && adc_eh_alfa(m.dados[0])) est = m.dados[m.tamanho - 1] == ')' ? ((m.dados[0] >= 'A' && m.dados[0] <= 'Z') ? "upperroman" : "lowerroman") : ((m.dados[0] >= 'A' && m.dados[0] <= 'Z') ? "upperalpha" : "loweralpha");
    else if (m.tamanho >= 1 && m.dados[0] == '.') { switch (m.tamanho) { case 2: est = "loweralpha"; break; case 3: est = "lowerroman"; break; case 4: est = "upperalpha"; break; case 5: est = "upperroman"; break; default: break; } }
    (void)r;
    if (strcmp(est, "loweralpha") == 0) snprintf(pBuf, pTam, "%c.", 'a' + (n - 1) % 26);
    else if (strcmp(est, "upperalpha") == 0) snprintf(pBuf, pTam, "%c.", 'A' + (n - 1) % 26);
    else if (strcmp(est, "lowerroman") == 0 || strcmp(est, "upperroman") == 0) {
        static const char* rom[] = { "", "i", "ii", "iii", "iv", "v", "vi", "vii", "viii", "ix", "x", "xi", "xii", "xiii", "xiv", "xv", "xvi", "xvii", "xviii", "xix", "xx" };
        const char* s = n >= 1 && n <= 20 ? rom[n] : "?";
        snprintf(pBuf, pTam, "%s.", s);
        if (strcmp(est, "upperroman") == 0) for (char* q = pBuf; *q; q++) if (*q >= 'a' && *q <= 'z') *q = (char)(*q - 32);
    } else snprintf(pBuf, pTam, "%d.", n);
    return pBuf;
}

static void lista(R* r, const ADC_Bloco* b) {
    titulo_de_bloco(r, b, NULL, NULL);
    registrar_destino_bloco(r, b);
    r->nivel_lista++;
    double recuo = r->T.recuo_lista;
    double x_salvo = r->x_esq, w_salvo = r->largura;
    int32_t idx = 0;
    for (const ADC_Bloco* it = b->filhos; it; it = it->proximo, idx++) {
        char marcador[24];
        if (b->variante_lista == ADC_LISTA_ORDERED) numero_item(r, b, idx, marcador, sizeof marcador);
        else if (b->variante_lista == ADC_LISTA_CALLOUT) snprintf(marcador, sizeof marcador, "(%d)", it->numero);
        else {
            if (it->principal && it->principal->tipo == ADC_IN_TEXTO && it->principal->valor.tamanho >= 4 && it->principal->valor.dados[0] == '[' && it->principal->valor.dados[2] == ']' && it->principal->valor.dados[3] == ' ')
                snprintf(marcador, sizeof marcador, "%s", it->principal->valor.dados[1] == ' ' ? "\xE2\x98\x90" : "\xE2\x98\x91");
            else snprintf(marcador, sizeof marcador, "%s", r->nivel_lista % 3 == 1 ? "\xE2\x80\xA2" : r->nivel_lista % 3 == 2 ? "\xE2\x97\xA6" : "\xE2\x96\xAA");
        }
        garantir(r, r->T.fonte_base * r->T.entrelinha);
        double y_item = r->y;
        int32_t pag_item = r->pagina;
        /* marcador */
        Estilo E = estilo_corpo(r);
        bool checklist = marcador[0] == (char)0xE2 && marcador[1] == (char)0x98;
        if (!r->medir && pagina(r)) {
            int32_t fnt = fonte(r, checklist ? r->T.familia_corpo : r->T.familia_corpo, false, false);
            ADC_PdfFonte* f = fonte_obj(r, fnt);
            ADC_Fatia mk = adc_fatia_cstr(marcador);
            /* se a fonte não tem o glifo do marcador, usa alternativas ASCII */
            if (f) { int32_t p = 0; uint32_t cp = adc_utf8_decodificar(mk.dados, mk.tamanho, &p); if (adc_pdf_fonte_gid(f, cp) == 0) mk = adc_fatia_cstr(checklist ? (marcador[2] == (char)0x90 ? "[ ]" : "[x]") : "-"); }
            double mw = f ? adc_pdf_fonte_largura(f, mk, E.tam, false) : 0;
            adc_pdf_pg_cor_preenchimento(pagina(r), E.cor.r, E.cor.g, E.cor.b);
            double asc = f ? f->ascender * E.tam / 1000.0 : E.tam * 0.8;
            adc_pdf_pg_texto(&r->doc, pagina(r), fnt, E.tam, x_salvo + recuo - mw - 5, ypdf(r, y_item + asc), mk);
        }
        r->x_esq = x_salvo + recuo; r->largura = w_salvo - recuo;
        /* principal (sem o "[ ] ") */
        if (checklist && it->principal) { ADC_Inline copia = *it->principal; copia.valor = adc_fatia(copia.valor.dados + 4, copia.valor.tamanho - 4); copia.proximo = it->principal->proximo; paragrafo_inlines(r, &copia, E, 0); }
        else paragrafo_inlines(r, it->principal, E, 0);
        if (it->filhos) { avancar(r, 3); r->suprimir_espaco = true; blocos(r, it->filhos); r->suprimir_espaco = false; }
        else avancar(r, 2);
        (void)pag_item;
    }
    r->x_esq = x_salvo; r->largura = w_salvo;
    r->nivel_lista--;
}

static void dlist(R* r, const ADC_Bloco* b) {
    titulo_de_bloco(r, b, NULL, NULL);
    registrar_destino_bloco(r, b);
    double x_salvo = r->x_esq, w_salvo = r->largura;
    bool horizontal = adc_fatia_igual(b->meta.estilo, "horizontal");
    for (const ADC_Bloco* it = b->filhos; it; it = it->proximo) {
        garantir(r, r->T.fonte_base * r->T.entrelinha * 2);
        Estilo Et = estilo_corpo(r); Et.negrito = true;
        if (horizontal) {
            double col = w_salvo * 0.3;
            double y0 = r->y;
            r->largura = col - 6;
            for (int32_t k = 0; k < it->quantidade_termos; k++) paragrafo_inlines(r, it->termos[k], Et, 0);
            double y_termos = r->y;
            r->y = y0; r->x_esq = x_salvo + col; r->largura = w_salvo - col;
            if (it->principal) paragrafo_inlines(r, it->principal, estilo_corpo(r), 0);
            blocos(r, it->filhos);
            if (r->y < y_termos) r->y = y_termos;
            r->x_esq = x_salvo; r->largura = w_salvo;
        } else {
            for (int32_t k = 0; k < it->quantidade_termos; k++) paragrafo_inlines(r, it->termos[k], Et, 0);
            r->x_esq = x_salvo + r->T.recuo_lista; r->largura = w_salvo - r->T.recuo_lista;
            if (it->principal) paragrafo_inlines(r, it->principal, estilo_corpo(r), 0);
            blocos(r, it->filhos);
            r->x_esq = x_salvo; r->largura = w_salvo;
        }
        avancar(r, 3);
    }
}

/* Renderiza blocos num retângulo (célula) sem paginar; devolve a altura. pDesenhar=false só mede. */
static double blocos_em_caixa(R* r, const ADC_Bloco* pCelula, double x, double y, double w, bool pDesenhar) {
    double sx = r->x_esq, sw = r->largura, sy = r->y, sm = r->altura_medida;
    bool smed = r->medir, ssp = r->sem_paginacao;
    r->x_esq = x; r->largura = w; r->y = y; r->medir = !pDesenhar; r->sem_paginacao = true; r->altura_medida = 0;
    Estilo E = estilo_corpo(r);
    if (pCelula->estilo_celula == 'h') E.negrito = true;
    if (pCelula->estilo_celula == 'e') E.italico = true;
    if (pCelula->estilo_celula == 's') E.negrito = true;
    if (pCelula->estilo_celula == 'm' || pCelula->estilo_celula == 'l') { E.mono = true; E.tam = r->T.tam_codigo; }
    if (pCelula->filhos) {
        if (pCelula->estilo_celula == 'a') { r->suprimir_espaco = true; blocos(r, pCelula->filhos); r->suprimir_espaco = false; }
        else for (const ADC_Bloco* p = pCelula->filhos; p; p = p->proximo) { if (p != pCelula->filhos) avancar(r, 3); paragrafo_inlines(r, p->inlines, E, pCelula->alinha_h == ADC_ALINHA_CENTRO ? 1 : pCelula->alinha_h == ADC_ALINHA_DIREITA ? 2 : 0); }
    } else if (pCelula->estilo_celula == 'l') {
        ADC_Fatia t = pCelula->texto_cru; int32_t ini = 0;
        for (int32_t k = 0; k <= t.tamanho; k++) if (k == t.tamanho || t.dados[k] == '\n') { ADC_Fatia l = adc_fatia(t.dados + ini, k - ini); if (l.tamanho) texto_simples(r, l, E, 0); else avancar(r, E.tam * 1.3); ini = k + 1; }
    } else paragrafo_inlines(r, pCelula->inlines, E, pCelula->alinha_h == ADC_ALINHA_CENTRO ? 1 : pCelula->alinha_h == ADC_ALINHA_DIREITA ? 2 : 0);
    double h = r->y - y;
    r->x_esq = sx; r->largura = sw; r->y = sy; r->medir = smed; r->sem_paginacao = ssp; r->altura_medida = sm;
    return h;
}

static void tabela(R* r, const ADC_Bloco* b) {
    const ADC_Tabela* T = b->tabela; if (!T || T->quantidade_colunas == 0) return;
    titulo_de_bloco(r, b, "table-caption", &r->cont_tabela);
    registrar_destino_bloco(r, b);
    int32_t nc = T->quantidade_colunas;
    double total_w = r->largura * (T->largura_percentual > 0 && T->largura_percentual <= 100 ? T->largura_percentual / 100.0 : 1.0);
    double* larg = (double*)malloc(sizeof(double) * (size_t)nc);
    if (!larg) return;
    { double soma = 0; bool pct = false; for (int32_t k = 0; k < nc; k++) { soma += T->colunas[k].largura > 0 ? T->colunas[k].largura : 1; if (T->colunas[k].percentual) pct = true; }
      for (int32_t k = 0; k < nc; k++) larg[k] = pct ? total_w * T->colunas[k].largura / 100.0 : total_w * (T->colunas[k].largura > 0 ? T->colunas[k].largura : 1) / soma; }
    double pad = 4;
    bool grade_linhas = !adc_fatia_igual(T->grade, "none") && !adc_fatia_igual(T->grade, "cols");
    bool grade_cols = !adc_fatia_igual(T->grade, "none") && !adc_fatia_igual(T->grade, "rows");
    const ADC_LinhaTabela* cab = (T->linhas && T->linhas->cabecalho) ? T->linhas : NULL;
    /* linhas com rowspan: simplificação — rowspan tratado como células normais (altura da própria linha) */
    for (const ADC_LinhaTabela* L = T->linhas; L; L = L->proxima) {
        /* mede a altura da linha */
        double h = 0; int32_t col = 0;
        double* xs = (double*)malloc(sizeof(double) * (size_t)(nc + 1));
        if (!xs) break;
        xs[0] = r->x_esq; for (int32_t k = 0; k < nc; k++) xs[k + 1] = xs[k] + larg[k];
        for (const ADC_Bloco* c = L->celulas; c && col < nc; c = c->proximo) {
            int32_t span = c->colspan < 1 ? 1 : c->colspan; if (col + span > nc) span = nc - col;
            double w = xs[col + span] - xs[col] - 2 * pad;
            double ch = blocos_em_caixa(r, c, xs[col] + pad, 0, w, false) + 2 * pad;
            if (ch > h) h = ch;
            col += span;
        }
        if (h < r->T.fonte_base * 1.4 + 2 * pad) h = r->T.fonte_base * 1.4 + 2 * pad;
        bool quebrou = garantir(r, h);
        if (quebrou && cab && L != cab && !r->medir && pagina(r)) {
            /* repete o cabeçalho */
            double hc = 0; int32_t cc = 0;
            for (const ADC_Bloco* c = cab->celulas; c && cc < nc; c = c->proximo) { int32_t span = c->colspan < 1 ? 1 : c->colspan; if (cc + span > nc) span = nc - cc; double ch = blocos_em_caixa(r, c, xs[cc] + pad, 0, xs[cc + span] - xs[cc] - 2 * pad, false) + 2 * pad; if (ch > hc) hc = ch; cc += span; }
            adc_pdf_pg_cor_preenchimento(pagina(r), r->T.cabecalho_tabela.r, r->T.cabecalho_tabela.g, r->T.cabecalho_tabela.b);
            adc_pdf_pg_retangulo(pagina(r), xs[0], ypdf(r, r->y + hc), xs[nc] - xs[0], hc, true, false);
            cc = 0;
            for (const ADC_Bloco* c = cab->celulas; c && cc < nc; c = c->proximo) { int32_t span = c->colspan < 1 ? 1 : c->colspan; if (cc + span > nc) span = nc - cc; blocos_em_caixa(r, c, xs[cc] + pad, r->y + pad, xs[cc + span] - xs[cc] - 2 * pad, true); if (grade_cols && cc > 0) { adc_pdf_pg_cor_traco(pagina(r), r->T.borda.r, r->T.borda.g, r->T.borda.b); adc_pdf_pg_largura_traco(pagina(r), 0.5); adc_pdf_pg_linha(pagina(r), xs[cc], ypdf(r, r->y), xs[cc], ypdf(r, r->y + hc)); } cc += span; }
            if (grade_linhas) { adc_pdf_pg_cor_traco(pagina(r), r->T.borda.r, r->T.borda.g, r->T.borda.b); adc_pdf_pg_largura_traco(pagina(r), 0.5); adc_pdf_pg_linha(pagina(r), xs[0], ypdf(r, r->y + hc), xs[nc], ypdf(r, r->y + hc)); }
            avancar(r, hc);
        }
        if (!r->medir && pagina(r)) {
            if (L->cabecalho) { adc_pdf_pg_cor_preenchimento(pagina(r), r->T.cabecalho_tabela.r, r->T.cabecalho_tabela.g, r->T.cabecalho_tabela.b); adc_pdf_pg_retangulo(pagina(r), xs[0], ypdf(r, r->y + h), xs[nc] - xs[0], h, true, false); }
            col = 0;
            for (const ADC_Bloco* c = L->celulas; c && col < nc; c = c->proximo) {
                int32_t span = c->colspan < 1 ? 1 : c->colspan; if (col + span > nc) span = nc - col;
                ADC_Bloco cel = *c; if (L->cabecalho) cel.estilo_celula = 'h';
                double w = xs[col + span] - xs[col] - 2 * pad;
                double ch = blocos_em_caixa(r, &cel, xs[col] + pad, 0, w, false) + 2 * pad;
                double y0 = r->y + pad;
                if (c->alinha_v == ADC_ALINHA_MEIO) y0 = r->y + (h - ch) / 2 + pad; else if (c->alinha_v == ADC_ALINHA_BASE) y0 = r->y + h - ch + pad;
                blocos_em_caixa(r, &cel, xs[col] + pad, y0, w, true);
                if (grade_cols && col > 0) { adc_pdf_pg_cor_traco(pagina(r), r->T.borda.r, r->T.borda.g, r->T.borda.b); adc_pdf_pg_largura_traco(pagina(r), 0.5); adc_pdf_pg_linha(pagina(r), xs[col], ypdf(r, r->y), xs[col], ypdf(r, r->y + h)); }
                col += span;
            }
            adc_pdf_pg_cor_traco(pagina(r), r->T.borda.r, r->T.borda.g, r->T.borda.b); adc_pdf_pg_largura_traco(pagina(r), 0.5);
            if (grade_linhas || L == T->linhas) adc_pdf_pg_linha(pagina(r), xs[0], ypdf(r, r->y), xs[nc], ypdf(r, r->y));
            if (!L->proxima || grade_linhas) adc_pdf_pg_linha(pagina(r), xs[0], ypdf(r, r->y + h), xs[nc], ypdf(r, r->y + h));
            if (!adc_fatia_igual(T->molduras, "none") && !adc_fatia_igual(T->molduras, "topbot") && !adc_fatia_igual(T->molduras, "ends")) { adc_pdf_pg_linha(pagina(r), xs[0], ypdf(r, r->y), xs[0], ypdf(r, r->y + h)); adc_pdf_pg_linha(pagina(r), xs[nc], ypdf(r, r->y), xs[nc], ypdf(r, r->y + h)); }
        }
        avancar(r, h);
        free(xs);
    }
    free(larg);
}

static void imagem_bloco(R* r, const ADC_Bloco* b) {
    registrar_destino_bloco(r, b);
    bool a; ADC_Fatia alt = adc_atributo_obter(b->meta.atributos, "alt", &a);
    int32_t lw = 0, lh = 0;
    int32_t idx = adc_pdf_doc_imagem_por_chave(&r->doc, b->alvo, &lw, &lh);
    if (idx < 0 && r->ctx->resolver && !r->medir) {
        const uint8_t* bytes; int32_t tam;
        ADC_Fatia alvo = b->alvo;
        ADC_Fatia dir; ADC_Buffer caminho; adc_buffer_iniciar(&caminho);
        if (adc_atribs_obter_cstr(&r->ctx->atribs, "imagesdir", &dir) && dir.tamanho && !adc_fatia_comeca(alvo, "/") && !adc_fatia_comeca(alvo, "http")) { adc_buffer_fatia(&caminho, dir); adc_buffer_char(&caminho, '/'); adc_buffer_fatia(&caminho, alvo); alvo = adc_fatia((const char*)caminho.dados, caminho.tamanho); }
        if (adc_ctx_resolver_recurso(r->ctx, 2, alvo, &bytes, &tam) == 0 && bytes) {
            ADC_ImagemDecodificada im;
            if (adc_imagem_decodificar(bytes, tam, &im)) { idx = adc_pdf_doc_imagem(&r->doc, b->alvo, im.largura, im.altura, im.componentes, im.pixels, im.tamanho_pixels, im.alpha, im.tamanho_alpha, im.dct, im.cmyk_invertido); lw = im.largura; lh = im.altura; adc_imagem_liberar(&im); }
            else if (r->avisar) adc_msgs_adicionar(&r->ctx->msgs, ADC_NIVEL_AVISO, b->local.inicio.linha, 1, "PDF: a imagem '%.*s' não pôde ser decodificada (%s); o texto alternativo foi usado.", b->alvo.tamanho, b->alvo.dados, adc_msgs_tr(&r->ctx->msgs, im.erro ? im.erro : "?"));
        } else if (r->avisar) adc_msgs_adicionar(&r->ctx->msgs, ADC_NIVEL_AVISO, b->local.inicio.linha, 1, "PDF: a imagem '%.*s' não foi entregue pelo resolvedor de recursos (%s); o texto alternativo foi usado.", b->alvo.tamanho, b->alvo.dados, adc_msgs_tr(&r->ctx->msgs, adc_ctx_tem_resolvedor(r->ctx) ? "negada" : "sem resolvedor"));
        adc_buffer_liberar(&caminho);
    } else if (idx < 0 && !adc_ctx_tem_resolvedor(r->ctx) && !r->medir && r->avisar) adc_msgs_adicionar(&r->ctx->msgs, ADC_NIVEL_AVISO, b->local.inicio.linha, 1, "PDF: a imagem '%.*s' não foi incluída: não há resolvedor de recursos registrado (modo seguro).", b->alvo.tamanho, b->alvo.dados);
    if (idx >= 0 && lw > 0 && lh > 0) {
        ADC_Fatia w = adc_atributo_obter(b->meta.atributos, "width", &a);
        double larg = a ? adc_fatia_para_int(w, 0) * 0.75 : lw * 0.75;
        ADC_Fatia pw = adc_atributo_obter(b->meta.atributos, "pdfwidth", &a);
        if (a && pw.tamanho && pw.dados[pw.tamanho - 1] == '%') larg = r->largura * adc_fatia_para_int(adc_fatia(pw.dados, pw.tamanho - 1), 100) / 100.0;
        if (larg <= 0 || larg > r->largura) larg = r->largura;
        double alt_pt = larg * lh / lw;
        double max_h = limite_inferior(r) - r->T.margem_sup - 20;
        if (alt_pt > max_h) { alt_pt = max_h; larg = alt_pt * lw / lh; }
        garantir(r, alt_pt + 4);
        ADC_Fatia alinh = adc_atributo_obter(b->meta.atributos, "align", &a);
        double x = r->x_esq;
        if ((a && adc_fatia_igual(alinh, "center")) || adc_lista_fatias_contem(&b->meta.papeis, "text-center")) x = r->x_esq + (r->largura - larg) / 2;
        else if (a && adc_fatia_igual(alinh, "right")) x = r->x_esq + r->largura - larg;
        if (!r->medir && pagina(r)) adc_pdf_pg_imagem(&r->doc, pagina(r), idx, x, ypdf(r, r->y + alt_pt), larg, alt_pt);
        avancar(r, alt_pt + 4);
    } else {
        Estilo E = estilo_corpo(r); E.italico = true; E.cor = r->T.cinza;
        ADC_Buffer t; adc_buffer_iniciar(&t); adc_buffer_texto(&t, "["); adc_buffer_fatia(&t, alt); adc_buffer_texto(&t, "]");
        char* c = adc_arena_copiar(r->ctx->arena, (const char*)t.dados, t.tamanho); int32_t n = t.tamanho; adc_buffer_liberar(&t);
        if (c) texto_simples(r, adc_fatia(c, n), E, 1);
    }
    if (b->titulo) { avancar(r, 2); Estilo E = estilo_corpo(r); E.italico = true; E.cor = r->T.titulo; E.tam = r->T.fonte_base * 0.95; Atomos A = { NULL, NULL, 0 }; ADC_Buffer leg; adc_buffer_iniciar(&leg); if (adc_render_legenda(r->ctx, b, &leg, "figure-caption", &r->cont_figura)) { char* c = adc_arena_copiar(r->ctx->arena, (const char*)leg.dados, leg.tamanho); if (c) texto_em_atomos(r, &A, adc_fatia(c, leg.tamanho), &E); } adc_buffer_liberar(&leg); inlines_em_atomos(r, &A, b->titulo, E); if (A.primeiro) fluir_atomos(r, &A, r->largura, 0, 1); }
}

static void secao(R* r, const ADC_Bloco* b) {
    int32_t nivel = b->nivel < 1 ? 1 : b->nivel; if (nivel > 6) nivel = 6;
    r->contadores[nivel]++; for (int32_t k = nivel + 1; k < 8; k++) r->contadores[k] = 0;
    double tam = r->T.tam_titulo[nivel];
    /* mantém com o próximo: exige espaço para o título + 2 linhas */
    espaco(r, r->T.espaco_bloco * (nivel == 1 ? 1.5 : 1.0));
    garantir(r, tam * 1.4 + r->T.fonte_base * r->T.entrelinha * 2.5);
    if (r->passagem == 2 || !adc_render_toc(r->ctx, NULL)) { /* registra posição para o sumário e marcador */ }
    if (r->n_toc == r->cap_toc) { int32_t nova = r->cap_toc ? r->cap_toc * 2 : 64; TocItem* np = (TocItem*)realloc(r->toc_itens, sizeof(TocItem) * (size_t)nova); if (np) { r->toc_itens = np; r->cap_toc = nova; } }
    if (r->n_toc < r->cap_toc) { r->toc_itens[r->n_toc].secao = b; r->toc_itens[r->n_toc].pagina = r->pagina; r->toc_itens[r->n_toc].y = ypdf(r, r->y); r->n_toc++; }
    ADC_Fatia titulo_puro = adc_inlines_texto_puro(r->ctx->arena, b->titulo);
    if (!r->medir) {
        ADC_Buffer t; adc_buffer_iniciar(&t);
        if (r->sectnums && nivel <= adc_fatia_para_int(adc_atribs_valor_ou(&r->ctx->atribs, "sectnumlevels", "3"), 3)) { adc_render_numero_secao(&t, r->contadores, nivel); adc_buffer_char(&t, ' '); }
        adc_buffer_fatia(&t, titulo_puro);
        char* c = adc_arena_copiar(r->ctx->arena, (const char*)t.dados, t.tamanho); int32_t n = t.tamanho; adc_buffer_liberar(&t);
        if (c) adc_pdf_doc_marcador(&r->doc, adc_fatia(c, n), nivel, r->pagina, ypdf(r, r->y));
        if (b->meta.id.tamanho) adc_pdf_doc_destino(&r->doc, b->meta.id, r->pagina, ypdf(r, r->y));
    }
    Estilo E = estilo_corpo(r); E.familia = r->T.familia_titulos; E.negrito = true; E.tam = tam; E.cor = r->T.titulo;
    Atomos A = { NULL, NULL, 0 };
    if (r->sectnums && nivel <= adc_fatia_para_int(adc_atribs_valor_ou(&r->ctx->atribs, "sectnumlevels", "3"), 3)) { ADC_Buffer t; adc_buffer_iniciar(&t); adc_render_numero_secao(&t, r->contadores, nivel); char* c = adc_arena_copiar(r->ctx->arena, (const char*)t.dados, t.tamanho); if (c) texto_em_atomos(r, &A, adc_fatia(c, t.tamanho), &E); adc_buffer_liberar(&t); novo_atomo(r, &A, adc_fatia_cstr(" "), &E, true); }
    inlines_em_atomos(r, &A, b->titulo, E);
    if (A.primeiro) fluir_atomos(r, &A, r->largura, 0, 0);
    if (nivel == 1 && !r->medir && pagina(r)) { adc_pdf_pg_cor_traco(pagina(r), r->T.borda.r, r->T.borda.g, r->T.borda.b); adc_pdf_pg_largura_traco(pagina(r), 0.5); adc_pdf_pg_linha(pagina(r), r->x_esq, ypdf(r, r->y + 1), r->x_esq + r->largura, ypdf(r, r->y + 1)); avancar(r, 2); }
    avancar(r, r->T.espaco_bloco * 0.6);
    blocos(r, b->filhos);
}

static void bloco(R* r, const ADC_Bloco* b) {
    if (r->falhou) return;
    switch (b->tipo) {
        case ADC_BL_SECAO: secao(r, b); return;
        case ADC_BL_TITULO: { int32_t n = b->nivel < 1 ? 1 : b->nivel; if (n > 6) n = 6; espaco(r, r->T.espaco_bloco); garantir(r, r->T.tam_titulo[n] * 2.5); registrar_destino_bloco(r, b); Estilo E = estilo_corpo(r); E.familia = r->T.familia_titulos; E.negrito = true; E.tam = r->T.tam_titulo[n]; E.cor = r->T.titulo; paragrafo_inlines(r, b->titulo, E, 0); avancar(r, r->T.espaco_bloco * 0.5); return; }
        case ADC_BL_PARAGRAFO: espaco(r, r->T.espaco_bloco); paragrafo(r, b); return;
        case ADC_BL_LISTING: case ADC_BL_LITERAL: espaco(r, r->T.espaco_bloco); codigo(r, b); return;
        case ADC_BL_PASS: return; /* HTML cru não tem lugar no PDF */
        case ADC_BL_STEM: espaco(r, r->T.espaco_bloco); { Estilo E = estilo_corpo(r); E.mono = true; texto_simples(r, b->texto_cru, E, 1); } return;
        case ADC_BL_VERSE: case ADC_BL_QUOTE: espaco(r, r->T.espaco_bloco); citacao(r, b); return;
        case ADC_BL_ADMONICAO: espaco(r, r->T.espaco_bloco); admonicao(r, b); return;
        case ADC_BL_EXEMPLO: espaco(r, r->T.espaco_bloco); caixa(r, b, false, true, "example-caption", &r->cont_exemplo); return;
        case ADC_BL_SIDEBAR: espaco(r, r->T.espaco_bloco); caixa(r, b, true, false, NULL, NULL); return;
        case ADC_BL_OPEN: if (adc_fatia_igual(b->meta.estilo, "abstract")) { espaco(r, r->T.espaco_bloco); citacao(r, b); } else { titulo_de_bloco(r, b, NULL, NULL); blocos(r, b->filhos); } return;
        case ADC_BL_LISTA: espaco(r, r->T.espaco_bloco * 0.6); lista(r, b); return;
        case ADC_BL_DLIST: espaco(r, r->T.espaco_bloco * 0.6); dlist(r, b); return;
        case ADC_BL_TABELA: espaco(r, r->T.espaco_bloco); tabela(r, b); return;
        case ADC_BL_QUEBRA:
            if (b->variante_quebra == ADC_QUEBRA_PAGE) { if (!r->sem_paginacao) nova_pagina(r); }
            else { espaco(r, r->T.espaco_bloco); garantir(r, 6); if (!r->medir && pagina(r)) { adc_pdf_pg_cor_traco(pagina(r), r->T.borda.r, r->T.borda.g, r->T.borda.b); adc_pdf_pg_largura_traco(pagina(r), 0.75); adc_pdf_pg_linha(pagina(r), r->x_esq, ypdf(r, r->y + 3), r->x_esq + r->largura, ypdf(r, r->y + 3)); } avancar(r, 6); }
            return;
        case ADC_BL_MACRO:
            if (adc_fatia_igual(b->nome_macro, "image")) { espaco(r, r->T.espaco_bloco); imagem_bloco(r, b); }
            else if (adc_fatia_igual(b->nome_macro, "toc")) { /* o sumário vai no início (duas passagens); toc::[] no meio é ignorado */ }
            else { espaco(r, r->T.espaco_bloco); Estilo E = estilo_corpo(r); E.italico = true; E.cor = r->T.cinza; ADC_Buffer t; adc_buffer_iniciar(&t); adc_buffer_char(&t, '['); adc_buffer_fatia(&t, b->nome_macro); adc_buffer_texto(&t, ": "); adc_buffer_fatia(&t, b->alvo); adc_buffer_char(&t, ']'); char* c = adc_arena_copiar(r->ctx->arena, (const char*)t.dados, t.tamanho); int32_t n = t.tamanho; adc_buffer_liberar(&t); if (c) texto_simples(r, adc_fatia(c, n), E, 0); }
            return;
        default: return;
    }
}

static void blocos(R* r, const ADC_Bloco* pPrimeiro) { for (const ADC_Bloco* b = pPrimeiro; b && !r->falhou; b = b->proximo) bloco(r, b); }

/* --------------------------------------------------------------- sumário ---- */

static void sumario(R* r, int32_t pNiveis, bool pDesenhar, int32_t pDeslocPaginas) {
    Estilo E = estilo_corpo(r); E.familia = r->T.familia_titulos; E.negrito = true; E.tam = r->T.tam_titulo[1]; E.cor = r->T.titulo;
    espaco(r, r->T.espaco_bloco);
    texto_simples(r, adc_atribs_valor_ou(&r->ctx->atribs, "toc-title", "Sumário"), E, 0);
    avancar(r, r->T.espaco_bloco);
    int32_t cont[8] = { 0 };
    for (int32_t i = 0; i < r->n_toc; i++) {
        const ADC_Bloco* s = r->toc_itens[i].secao;
        int32_t n = s->nivel < 1 ? 1 : s->nivel; if (n > 7) n = 7;
        cont[n]++; for (int32_t k = n + 1; k < 8; k++) cont[k] = 0;
        if (n > pNiveis) continue;
        double recuo = (n - 1) * 14;
        Estilo Ei = estilo_corpo(r); Ei.tam = r->T.fonte_base * (n == 1 ? 1.0 : 0.95); Ei.negrito = n == 1;
        ADC_Buffer t; adc_buffer_iniciar(&t);
        if (r->sectnums) { adc_render_numero_secao(&t, cont, n); adc_buffer_char(&t, ' '); }
        adc_buffer_fatia(&t, adc_inlines_texto_puro(r->ctx->arena, s->titulo));
        char* c = adc_arena_copiar(r->ctx->arena, (const char*)t.dados, t.tamanho); int32_t tn = t.tamanho; adc_buffer_liberar(&t);
        if (!c) continue;
        char num[16]; snprintf(num, sizeof num, "%d", r->toc_itens[i].pagina + 1 + pDeslocPaginas);
        int32_t fnt = fonte(r, Ei.familia, Ei.negrito, false);
        ADC_PdfFonte* f = fonte_obj(r, fnt);
        double wn = f ? adc_pdf_fonte_largura(f, adc_fatia_cstr(num), Ei.tam, false) : 20;
        double h = Ei.tam * r->T.entrelinha;
        garantir(r, h);
        if (pDesenhar && !r->medir && pagina(r)) {
            double x = r->x_esq + recuo;
            /* título (recortado na largura disponível) */
            double disp = r->largura - recuo - wn - 12;
            ADC_Fatia tt = adc_fatia(c, tn);
            double wt = f ? adc_pdf_fonte_largura(f, tt, Ei.tam, false) : 0;
            while (wt > disp && tt.tamanho > 1) { tt.tamanho--; while (tt.tamanho > 0 && ((unsigned char)tt.dados[tt.tamanho] & 0xC0) == 0x80) tt.tamanho--; wt = f ? adc_pdf_fonte_largura(f, tt, Ei.tam, false) : 0; }
            double base = ypdf(r, r->y + (f ? f->ascender * Ei.tam / 1000.0 : Ei.tam * 0.8));
            adc_pdf_pg_cor_preenchimento(pagina(r), Ei.cor.r, Ei.cor.g, Ei.cor.b);
            adc_pdf_pg_texto(&r->doc, pagina(r), fnt, Ei.tam, x, base, tt);
            /* pontilhado */
            int32_t fp = fonte(r, r->T.familia_corpo, false, false); ADC_PdfFonte* fpo = fonte_obj(r, fp);
            double wp = fpo ? adc_pdf_fonte_largura(fpo, adc_fatia_cstr(". "), Ei.tam, false) : 3;
            double x0 = x + wt + 4, x1 = r->x_esq + r->largura - wn - 4;
            if (wp > 0 && x1 > x0) { ADC_Buffer pts; adc_buffer_iniciar(&pts); int32_t np = (int32_t)((x1 - x0) / wp); for (int32_t k = 0; k < np; k++) adc_buffer_texto(&pts, ". "); if (np > 0) { adc_pdf_pg_cor_preenchimento(pagina(r), r->T.cinza.r, r->T.cinza.g, r->T.cinza.b); adc_pdf_pg_texto(&r->doc, pagina(r), fp, Ei.tam, x1 - np * wp, base, adc_buffer_fatia_de(&pts)); } adc_buffer_liberar(&pts); }
            adc_pdf_pg_cor_preenchimento(pagina(r), Ei.cor.r, Ei.cor.g, Ei.cor.b);
            adc_pdf_pg_texto(&r->doc, pagina(r), fnt, Ei.tam, r->x_esq + r->largura - wn, base, adc_fatia_cstr(num));
            if (s->meta.id.tamanho) adc_pdf_pg_link_destino(&r->doc, pagina(r), r->x_esq, ypdf(r, r->y + h), r->largura, h, s->meta.id);
        }
        avancar(r, h);
    }
}

/* ---------------------------------------------------------------- rodapé ---- */

static void rodapes(R* r) {
    if (!r->T.rodape) return;
    int32_t fnt = fonte(r, r->T.familia_corpo, false, false);
    ADC_PdfFonte* f = fonte_obj(r, fnt);
    double tam = r->T.fonte_base * 0.8;
    for (int32_t i = 0; i < r->doc.n_paginas; i++) {
        ADC_PdfPagina* p = adc_pdf_doc_pagina(&r->doc, i);
        if (!p) continue;
        char num[32]; snprintf(num, sizeof num, "%d / %d", i + 1, r->doc.n_paginas);
        double y = r->T.margem_inf * 0.55;
        adc_pdf_pg_cor_traco(p, r->T.borda.r, r->T.borda.g, r->T.borda.b); adc_pdf_pg_largura_traco(p, 0.5);
        adc_pdf_pg_linha(p, r->T.margem_esq, y + tam * 1.3, r->T.pagina_w - r->T.margem_dir, y + tam * 1.3);
        adc_pdf_pg_cor_preenchimento(p, r->T.cinza.r, r->T.cinza.g, r->T.cinza.b);
        if (r->titulo_doc.tamanho) {
            ADC_Fatia t = r->titulo_doc; double disp = area_largura(r) * 0.7;
            while (f && adc_pdf_fonte_largura(f, t, tam, false) > disp && t.tamanho > 1) { t.tamanho--; while (t.tamanho > 0 && ((unsigned char)t.dados[t.tamanho] & 0xC0) == 0x80) t.tamanho--; }
            adc_pdf_pg_texto(&r->doc, p, fnt, tam, r->T.margem_esq, y, t);
        }
        double wn = f ? adc_pdf_fonte_largura(f, adc_fatia_cstr(num), tam, false) : 20;
        adc_pdf_pg_texto(&r->doc, p, fnt, tam, r->T.pagina_w - r->T.margem_dir - wn, y, adc_fatia_cstr(num));
    }
}

/* ------------------------------------------------------------ documento ---- */

static void cabecalho_documento(R* r) {
    ADC_Contexto* c = r->ctx;
    if (!c->cabecalho.presente) return;
    Estilo E = estilo_corpo(r); E.familia = r->T.familia_titulos; E.negrito = true; E.tam = r->T.tam_titulo[0]; E.cor = r->T.titulo;
    paragrafo_inlines(r, c->cabecalho.titulo, E, 0);
    avancar(r, 4);
    Estilo Ed = estilo_corpo(r); Ed.cor = r->T.cinza; Ed.tam = r->T.fonte_base * 0.95;
    ADC_Buffer d; adc_buffer_iniciar(&d);
    for (const ADC_Autor* a = c->cabecalho.autores; a; a = a->proximo) { if (a != c->cabecalho.autores) adc_buffer_texto(&d, "; "); adc_buffer_fatia(&d, a->nome_completo); if (a->email.tamanho) { adc_buffer_texto(&d, " <"); adc_buffer_fatia(&d, a->email); adc_buffer_char(&d, '>'); } }
    if (c->cabecalho.revisao_numero.tamanho || c->cabecalho.revisao_data.tamanho) {
        if (d.tamanho) adc_buffer_texto(&d, "\n");
        if (c->cabecalho.revisao_numero.tamanho) { adc_buffer_fatia(&d, adc_atribs_valor_ou(&c->atribs, "version-label", "Versão")); adc_buffer_char(&d, ' '); adc_buffer_fatia(&d, c->cabecalho.revisao_numero); }
        if (c->cabecalho.revisao_data.tamanho) { if (c->cabecalho.revisao_numero.tamanho) adc_buffer_texto(&d, ", "); adc_buffer_fatia(&d, c->cabecalho.revisao_data); }
        if (c->cabecalho.revisao_observacao.tamanho) { adc_buffer_texto(&d, ": "); adc_buffer_fatia(&d, c->cabecalho.revisao_observacao); }
    }
    if (d.tamanho) {
        ADC_Fatia t = adc_fatia((const char*)d.dados, d.tamanho);
        int32_t ini = 0;
        for (int32_t k = 0; k <= t.tamanho; k++) if (k == t.tamanho || t.dados[k] == '\n') { ADC_Fatia l = adc_fatia(t.dados + ini, k - ini); char* cp = adc_arena_copiar(c->arena, l.dados, l.tamanho); if (cp && l.tamanho) texto_simples(r, adc_fatia(cp, l.tamanho), Ed, 0); ini = k + 1; }
    }
    adc_buffer_liberar(&d);
    if (!r->medir && pagina(r)) { avancar(r, 6); adc_pdf_pg_cor_traco(pagina(r), r->T.titulo.r, r->T.titulo.g, r->T.titulo.b); adc_pdf_pg_largura_traco(pagina(r), 1); adc_pdf_pg_linha(pagina(r), r->x_esq, ypdf(r, r->y), r->x_esq + r->largura, ypdf(r, r->y)); }
    avancar(r, r->T.espaco_bloco * 1.5);
}

static void notas(R* r) {
    if (!r->ctx->notas) return;
    espaco(r, r->T.espaco_bloco * 2);
    garantir(r, r->T.fonte_base * 4);
    if (!r->medir && pagina(r)) { adc_pdf_pg_cor_traco(pagina(r), r->T.borda.r, r->T.borda.g, r->T.borda.b); adc_pdf_pg_largura_traco(pagina(r), 0.5); adc_pdf_pg_linha(pagina(r), r->x_esq, ypdf(r, r->y), r->x_esq + r->largura * 0.3, ypdf(r, r->y)); }
    avancar(r, 6);
    Estilo E = estilo_corpo(r); E.tam = r->T.fonte_base * 0.85;
    double x_salvo = r->x_esq, w_salvo = r->largura;
    for (const ADC_NotaRodape* n = r->ctx->notas; n; n = n->proxima) {
        char num[12]; snprintf(num, sizeof num, "%d.", n->numero);
        garantir(r, E.tam * r->T.entrelinha);
        if (!r->medir && pagina(r)) { int32_t fnt = fonte(r, E.familia, false, false); ADC_PdfFonte* f = fonte_obj(r, fnt); adc_pdf_pg_cor_preenchimento(pagina(r), E.cor.r, E.cor.g, E.cor.b); adc_pdf_pg_texto(&r->doc, pagina(r), fnt, E.tam, x_salvo, ypdf(r, r->y + (f ? f->ascender * E.tam / 1000.0 : E.tam * 0.8)), adc_fatia_cstr(num)); }
        r->x_esq = x_salvo + 18; r->largura = w_salvo - 18;
        paragrafo_inlines(r, n->texto, E, 0);
        r->x_esq = x_salvo; r->largura = w_salvo;
        avancar(r, 2);
    }
}

static void renderizar_tudo(R* r, bool pComSumario, int32_t pNiveisToc) {
    adc_pdf_doc_iniciar(&r->doc, r->ctx->arena);
    r->doc.data_definida = r->ctx->opcoes.pdf_data_definida; r->doc.data_criacao = r->ctx->opcoes.pdf_data_criacao;
    r->n_fontes = 0;
    r->pagina = -1; r->n_toc = 0; memset(r->contadores, 0, sizeof r->contadores);
    r->cont_figura = r->cont_tabela = r->cont_exemplo = 0; r->ctx->contador_listing = 0;
    r->x_esq = r->T.margem_esq; r->largura = area_largura(r);
    nova_pagina(r);
    r->doc.titulo = r->titulo_doc;
    { ADC_Fatia a; if (adc_atribs_obter_cstr(&r->ctx->atribs, "author", &a)) r->doc.autor = a; if (adc_atribs_obter_cstr(&r->ctx->atribs, "description", &a)) r->doc.assunto = a; if (adc_atribs_obter_cstr(&r->ctx->atribs, "keywords", &a)) r->doc.palavras_chave = a; }
    cabecalho_documento(r);
    if (pComSumario) {
        /* a 1ª passagem já colheu as posições; o sumário desloca as páginas em r->paginas_toc */
        sumario(r, pNiveisToc, true, r->paginas_toc);
        /* o sumário termina em alguma página; o corpo começa em página nova se o sumário ocupou algo */
        nova_pagina(r);
    }
    blocos(r, r->ctx->documento ? r->ctx->documento->filhos : NULL);
    notas(r);
}

bool adc_pdf_escrever(ADC_Contexto* pC, ADC_Buffer* pSaida) {
    R r; memset(&r, 0, sizeof r);
    r.ctx = pC;
    tema_padrao(&r.T);
    tema_aplicar_json(&r.T, pC->opcoes.pdf_tema);
    r.sectnums = adc_atribs_definido(&pC->atribs, "sectnums") || adc_atribs_definido(&pC->atribs, "numbered");
    r.titulo_doc = pC->cabecalho.titulo ? adc_inlines_texto_puro(pC->arena, pC->cabecalho.titulo) : adc_fatia(NULL, 0);
    bool toc = adc_render_toc(pC, NULL);
    int32_t niveis = adc_fatia_para_int(adc_atribs_valor_ou(&pC->atribs, "toclevels", "2"), 2);
    /* passagem 1: sem sumário, para colher as páginas das seções */
    r.passagem = 1;
    r.avisar = !toc;   /* com sumário haverá 2ª passagem: os avisos saem lá */
    renderizar_tudo(&r, false, niveis);
    if (r.falhou) { adc_pdf_doc_liberar(&r.doc); free(r.toc_itens); return false; }
    if (toc && r.n_toc > 0) {
        /* mede o sumário: quantas páginas ocupa a partir do fim do cabeçalho */
        int32_t paginas_corpo = r.doc.n_paginas;
        /* a 2ª passagem desenha o sumário logo após o cabeçalho e começa o corpo em página nova:
           o deslocamento das páginas do corpo = páginas usadas até o fim do sumário (inclusive) */
        adc_pdf_doc_liberar(&r.doc);
        /* passagem intermediária: só cabeçalho + sumário para contar páginas */
        adc_pdf_doc_iniciar(&r.doc, pC->arena); r.n_fontes = 0; r.pagina = -1; r.x_esq = r.T.margem_esq; r.largura = area_largura(&r);
        nova_pagina(&r);
        cabecalho_documento(&r);
        int32_t salvo_n_toc = r.n_toc;
        sumario(&r, niveis, false, 0);
        r.paginas_toc = r.doc.n_paginas;      /* o corpo começa na página seguinte à última do sumário */
        r.n_toc = salvo_n_toc;
        adc_pdf_doc_liberar(&r.doc);
        /* a 1ª passagem começou o corpo na página 1 (índice 0) junto com o cabeçalho; na 2ª, o corpo começa
           na página índice paginas_toc. O deslocamento dos números das seções é paginas_toc. */
        r.passagem = 2;
        r.avisar = true;
        /* os itens do sumário colhidos na passagem 1 continuam válidos: o corpo não muda de leiaute,
           só desloca em paginas_toc páginas */
        adc_pdf_doc_iniciar(&r.doc, pC->arena);
        r.doc.data_definida = pC->opcoes.pdf_data_definida; r.doc.data_criacao = pC->opcoes.pdf_data_criacao;
        r.n_fontes = 0; r.pagina = -1; memset(r.contadores, 0, sizeof r.contadores);
        r.cont_figura = r.cont_tabela = r.cont_exemplo = 0; pC->contador_listing = 0;
        r.x_esq = r.T.margem_esq; r.largura = area_largura(&r);
        nova_pagina(&r);
        r.doc.titulo = r.titulo_doc;
        { ADC_Fatia a; if (adc_atribs_obter_cstr(&pC->atribs, "author", &a)) r.doc.autor = a; }
        cabecalho_documento(&r);
        sumario(&r, niveis, true, r.paginas_toc);
        nova_pagina(&r);
        r.n_toc = 0;
        blocos(&r, pC->documento ? pC->documento->filhos : NULL);
        notas(&r);
        (void)paginas_corpo;
    }
    if (r.falhou) { adc_pdf_doc_liberar(&r.doc); free(r.toc_itens); return false; }
    rodapes(&r);
    ADC_Buffer saida;
    if (!adc_pdf_doc_finalizar(&r.doc, &saida)) {
        adc_msgs_adicionar(&pC->msgs, ADC_NIVEL_ERRO, 0, 0, "PDF: %s.", adc_msgs_tr(&pC->msgs, r.doc.erro ? r.doc.erro : "falha ao finalizar o arquivo"));
        adc_pdf_doc_liberar(&r.doc); free(r.toc_itens); return false;
    }
    adc_pdf_doc_liberar(&r.doc);
    free(r.toc_itens);
    *pSaida = saida;
    return true;
}
