/*
 * Markdown (GFM) direto da AST. Política de degradação (DAD-3, padrão adotado em 2026-10-24):
 * GFM como alvo; o que o GFM não expressa vira HTML embutido quando "markdown.htmlEmbutido" (padrão)
 * ou texto plano quando desligado; cada degradação gera um aviso em pMensagens (nunca muda).
 */
#include "../adc_render.h"

typedef struct M {
    ADC_Contexto* ctx;
    ADC_Buffer*   b;
    int32_t       indent;          /* espaços de recuo corrente (listas)                    */
    bool          html;
    int32_t       contadores[8];
    bool          sectnums;
    bool          em_tabela;
    int32_t       avisos;
} M;

static void inlines(M* m, const ADC_Inline* pI);
static void blocos(M* m, const ADC_Bloco* pPrimeiro, bool pPrimeiroBloco);

static void txt(M* m, const char* t) { adc_buffer_texto(m->b, t); }
static void recuo(M* m) { adc_buffer_repetir(m->b, ' ', m->indent); }
/* Registra uma degradação: pFrase é a frase completa, no pretérito, sem "Markdown:" na frente. */
static void degradar(M* m, const ADC_Bloco* b, const char* pFrase) {
    m->avisos++;
    adc_msgs_adicionar(&m->ctx->msgs, ADC_NIVEL_AVISO, b ? b->local.inicio.linha : 0, b ? b->local.inicio.coluna : 0, "Markdown: %s", adc_msgs_tr(&m->ctx->msgs, pFrase));
}

static void degradar_sem_equivalente(M* m, const ADC_Bloco* b, const char* pQue) {
    char frase[256];
    const ADC_Mensagens* ms = &m->ctx->msgs;
    snprintf(frase, sizeof frase, adc_msgs_tr(ms, "%s não tem equivalente em GFM e foi %s."), adc_msgs_tr(ms, pQue), adc_msgs_tr(ms, m->html ? "emitido como HTML embutido" : "reduzido a texto"));
    degradar(m, b, frase);
}

/* Escapa caracteres com significado em Markdown dentro de texto corrido. */
static void esc_texto(M* m, ADC_Fatia t) {
    for (int32_t i = 0; i < t.tamanho; i++) {
        char c = t.dados[i];
        if (c == '*' || c == '_' || c == '`' || c == '[' || c == ']' || c == '\\' || c == '<' || c == '>' ) { adc_buffer_char(m->b, '\\'); adc_buffer_char(m->b, c); }
        else if (c == '|' && m->em_tabela) { adc_buffer_texto(m->b, "\\|"); }
        else if (c == '#' && (i == 0 || t.dados[i - 1] == '\n')) { adc_buffer_texto(m->b, "\\#"); }
        else if (c == '\n' && m->em_tabela) { adc_buffer_texto(m->b, " "); }
        else if (c == '\n') { adc_buffer_char(m->b, '\n'); recuo(m); }
        else adc_buffer_char(m->b, c);
    }
}

static void charref(M* m, ADC_Fatia v) {
    /* entidades numéricas viram o caractere */
    if (v.tamanho > 1 && v.dados[0] == '#') {
        uint32_t cp = 0; bool hex = v.dados[1] == 'x' || v.dados[1] == 'X';
        for (int32_t k = hex ? 2 : 1; k < v.tamanho; k++) { char c = v.dados[k]; int d = (c >= '0' && c <= '9') ? c - '0' : (c >= 'a' && c <= 'f') ? c - 'a' + 10 : (c >= 'A' && c <= 'F') ? c - 'A' + 10 : -1; if (d < 0) break; cp = hex ? cp * 16 + (uint32_t)d : cp * 10 + (uint32_t)d; }
        if (cp == 0x2009) return; /* thin space: omite */
        char tmp[4]; int32_t n = adc_utf8_codificar(cp ? cp : '?', tmp); adc_buffer_bytes(m->b, tmp, n);
        return;
    }
    if (adc_fatia_igual(v, "amp")) adc_buffer_char(m->b, '&');
    else if (adc_fatia_igual(v, "lt")) adc_buffer_texto(m->b, "\\<");
    else if (adc_fatia_igual(v, "gt")) adc_buffer_texto(m->b, "\\>");
    else if (adc_fatia_igual(v, "nbsp")) adc_buffer_texto(m->b, "\xC2\xA0");
    else { adc_buffer_char(m->b, '&'); adc_buffer_fatia(m->b, v); adc_buffer_char(m->b, ';'); }
}

static void inline_um(M* m, const ADC_Inline* i) {
    switch (i->tipo) {
        case ADC_IN_TEXTO: esc_texto(m, i->valor); break;
        case ADC_IN_CHARREF: if (adc_fatia_igual(i->valor, "callout")) { txt(m, " <"); adc_buffer_int(m->b, i->numero); txt(m, ">"); } else charref(m, i->valor); break;
        case ADC_IN_RAW:
            if (i->valor.dados == NULL && i->filhos) inlines(m, i->filhos);
            else if (adc_fatia_igual(i->alvo, "stem") || adc_fatia_igual(i->alvo, "latexmath") || adc_fatia_igual(i->alvo, "asciimath")) { txt(m, "$"); adc_buffer_fatia(m->b, i->valor); txt(m, "$"); }
            else if (adc_fatia_igual(i->valor, "&#8201;&#8212;&#8201;")) txt(m, " \xE2\x80\x94 ");   /* travessão com espaços finos → " — " */
            else adc_buffer_fatia(m->b, i->valor);
            break;
        case ADC_IN_SPAN:
            switch (i->variante_span) {
                case ADC_SPAN_STRONG: txt(m, "**"); inlines(m, i->filhos); txt(m, "**"); break;
                case ADC_SPAN_EMPHASIS: txt(m, "*"); inlines(m, i->filhos); txt(m, "*"); break;
                case ADC_SPAN_CODE: {
                    /* conteúdo literal: usa o texto puro; se tiver crase, usa crase dupla */
                    ADC_Fatia puro = adc_inlines_texto_puro(m->ctx->arena, i->filhos);
                    bool crase = adc_fatia_indice(puro, '`') >= 0;
                    txt(m, crase ? "`` " : "`"); adc_buffer_fatia(m->b, puro); txt(m, crase ? " ``" : "`");
                    break;
                }
                case ADC_SPAN_MARK: if (m->html) { txt(m, "<mark>"); inlines(m, i->filhos); txt(m, "</mark>"); } else { txt(m, "=="); inlines(m, i->filhos); txt(m, "=="); } break;
                case ADC_SPAN_SUPERSCRIPT: if (m->html) { txt(m, "<sup>"); inlines(m, i->filhos); txt(m, "</sup>"); } else { txt(m, "^"); inlines(m, i->filhos); txt(m, "^"); } break;
                case ADC_SPAN_SUBSCRIPT: if (m->html) { txt(m, "<sub>"); inlines(m, i->filhos); txt(m, "</sub>"); } else { txt(m, "~"); inlines(m, i->filhos); txt(m, "~"); } break;
            }
            break;
        case ADC_IN_REF:
            if (i->variante_ref == ADC_REF_LINK) {
                bool bare = i->filhos && i->filhos->tipo == ADC_IN_TEXTO && !i->filhos->proximo && adc_fatia_igual_f(i->filhos->valor, i->alvo);
                if (bare && !adc_fatia_comeca(i->alvo, "mailto:")) { txt(m, "<"); adc_buffer_fatia(m->b, i->alvo); txt(m, ">"); }
                else { txt(m, "["); inlines(m, i->filhos); txt(m, "]("); adc_buffer_fatia(m->b, i->alvo); txt(m, ")"); }
            } else {
                ADC_Fatia alvo = i->alvo; int32_t hash = adc_fatia_indice(alvo, '#');
                ADC_Fatia id = hash >= 0 ? adc_fatia(alvo.dados + hash + 1, alvo.tamanho - hash - 1) : alvo;
                txt(m, "[");
                if (i->filhos) inlines(m, i->filhos);
                else { const ADC_Inline* t = adc_render_texto_xref(m->ctx, id); if (t) inlines(m, t); else esc_texto(m, id); }
                txt(m, "](");
                if (hash >= 0) { ADC_Fatia arq = adc_fatia(alvo.dados, hash); if (adc_fatia_termina(arq, ".adoc")) { adc_buffer_bytes(m->b, arq.dados, arq.tamanho - 5); txt(m, ".md"); } else adc_buffer_fatia(m->b, arq); txt(m, "#"); adc_buffer_fatia(m->b, id); }
                else if (adc_fatia_termina(alvo, ".adoc")) { adc_buffer_bytes(m->b, alvo.dados, alvo.tamanho - 5); txt(m, ".md"); }
                else { txt(m, "#"); adc_buffer_fatia(m->b, id); }
                txt(m, ")");
            }
            break;
        case ADC_IN_IMAGEM: { bool a; ADC_Fatia alt = adc_atributo_obter(i->atributos, "alt", &a); txt(m, "!["); adc_buffer_fatia(m->b, alt); txt(m, "]("); adc_buffer_fatia(m->b, i->alvo); txt(m, ")"); break; }
        case ADC_IN_NOTA: if (i->numero > 0) { txt(m, "[^"); adc_buffer_int(m->b, i->numero); txt(m, "]"); } break;
        case ADC_IN_QUEBRA_LINHA: txt(m, "  \n"); recuo(m); break;
        case ADC_IN_ANCORA: if (m->html) { txt(m, "<a id=\""); adc_buffer_fatia(m->b, i->id); txt(m, "\"></a>"); } break;
        case ADC_IN_TECLA: for (const ADC_Inline* k = i->filhos; k; k = k->proximo) { if (k != i->filhos) txt(m, "+"); if (m->html) { txt(m, "<kbd>"); adc_buffer_fatia(m->b, k->valor); txt(m, "</kbd>"); } else { txt(m, "`"); adc_buffer_fatia(m->b, k->valor); txt(m, "`"); } } break;
        case ADC_IN_BOTAO: txt(m, "**"); inlines(m, i->filhos); txt(m, "**"); break;
        case ADC_IN_MENU: for (const ADC_Inline* k = i->filhos; k; k = k->proximo) { if (k != i->filhos) txt(m, " › "); txt(m, "**"); esc_texto(m, k->valor); txt(m, "**"); } break;
        case ADC_IN_ICONE: txt(m, "["); adc_buffer_fatia(m->b, i->alvo); txt(m, "]"); break;
        case ADC_IN_INDICE: if (i->filhos) inlines(m, i->filhos); break;
        default: break;
    }
}

static void inlines(M* m, const ADC_Inline* pI) { for (const ADC_Inline* i = pI; i; i = i->proximo) inline_um(m, i); }

/* ----------------------------------------------------------------- blocos ---- */

static void separar(M* m) { if (m->b->tamanho > 0 && !(m->b->tamanho >= 2 && m->b->dados[m->b->tamanho - 1] == '\n' && m->b->dados[m->b->tamanho - 2] == '\n')) { if (m->b->dados[m->b->tamanho - 1] != '\n') txt(m, "\n"); txt(m, "\n"); } }

static void titulo_de_bloco(M* m, const ADC_Bloco* b, const char* pLegenda, int32_t* pContador) {
    if (!b->titulo) return;
    recuo(m); txt(m, "**"); if (pLegenda) adc_render_legenda(m->ctx, b, m->b, pLegenda, pContador); inlines(m, b->titulo); txt(m, "**\n\n");
}

static void verbatim(M* m, const ADC_Bloco* b) {
    titulo_de_bloco(m, b, b->tipo == ADC_BL_LISTING ? "listing-caption" : NULL, NULL);
    recuo(m); txt(m, "```"); if (b->linguagem.tamanho) adc_buffer_fatia(m->b, b->linguagem); txt(m, "\n");
    /* linhas do texto cru, cada uma recuada */
    ADC_Fatia t = b->texto_cru; int32_t ini = 0;
    for (int32_t i = 0; i <= t.tamanho; i++) {
        if (i == t.tamanho || t.dados[i] == '\n') { recuo(m); adc_buffer_bytes(m->b, t.dados + ini, i - ini); txt(m, "\n"); ini = i + 1; }
    }
    /* callouts: lista das anotações sai do próprio bloco de lista; aqui nada */
    recuo(m); txt(m, "```\n");
}

static void citacao(M* m, const ADC_Bloco* b, const char* pPrefixo) {
    /* renderiza os filhos num buffer e prefixa cada linha com "> " */
    ADC_Buffer sub; adc_buffer_iniciar(&sub);
    M mm = *m; mm.b = &sub; mm.indent = 0;
    if (pPrefixo) { adc_buffer_texto(&sub, pPrefixo); adc_buffer_texto(&sub, "\n"); }
    if (b->titulo) { adc_buffer_texto(&sub, "**"); inlines(&mm, b->titulo); adc_buffer_texto(&sub, "**\n\n"); }
    blocos(&mm, b->filhos, true);
    if (b->tipo == ADC_BL_VERSE || (b->tipo == ADC_BL_QUOTE && !b->filhos && b->inlines)) { inlines(&mm, b->inlines); adc_buffer_texto(&sub, "\n"); }
    if (b->atribuicao.tamanho || b->citacao.tamanho) { adc_buffer_texto(&sub, "\n— "); if (b->atribuicao.tamanho) adc_buffer_fatia(&sub, b->atribuicao); if (b->citacao.tamanho) { if (b->atribuicao.tamanho) adc_buffer_texto(&sub, ", "); adc_buffer_texto(&sub, "*"); adc_buffer_fatia(&sub, b->citacao); adc_buffer_texto(&sub, "*"); } adc_buffer_texto(&sub, "\n"); }
    m->avisos = mm.avisos;
    ADC_Fatia t = adc_buffer_fatia_de(&sub);
    while (t.tamanho > 0 && t.dados[t.tamanho - 1] == '\n') t.tamanho--;
    int32_t ini = 0;
    for (int32_t i = 0; i <= t.tamanho; i++) {
        if (i == t.tamanho || t.dados[i] == '\n') { recuo(m); txt(m, i - ini > 0 ? "> " : ">"); adc_buffer_bytes(m->b, t.dados + ini, i - ini); txt(m, "\n"); ini = i + 1; }
    }
    adc_buffer_liberar(&sub);
}

static void lista(M* m, const ADC_Bloco* b) {
    titulo_de_bloco(m, b, NULL, NULL);
    int32_t n = b->numero > 0 ? b->numero : 1;
    for (const ADC_Bloco* it = b->filhos; it; it = it->proximo) {
        recuo(m);
        int32_t largura;
        if (b->variante_lista == ADC_LISTA_ORDERED) { char tmp[16]; largura = snprintf(tmp, sizeof tmp, "%d. ", n++); adc_buffer_texto(m->b, tmp); }
        else if (b->variante_lista == ADC_LISTA_CALLOUT) { char tmp[16]; largura = snprintf(tmp, sizeof tmp, "%d. ", it->numero); adc_buffer_texto(m->b, tmp); }
        else {
            if (it->principal && it->principal->tipo == ADC_IN_TEXTO && it->principal->valor.tamanho >= 4 && it->principal->valor.dados[0] == '[' && it->principal->valor.dados[2] == ']' && it->principal->valor.dados[3] == ' ') {
                txt(m, it->principal->valor.dados[1] == ' ' ? "- [ ] " : "- [x] "); largura = 2;
                ADC_Inline copia = *it->principal; copia.valor = adc_fatia(copia.valor.dados + 4, copia.valor.tamanho - 4);
                int32_t salvo = m->indent; m->indent += largura; inline_um(m, &copia); inlines(m, it->principal->proximo); m->indent = salvo;
                txt(m, "\n");
                if (it->filhos) { m->indent += largura; txt(m, "\n"); blocos(m, it->filhos, true); m->indent -= largura; }
                continue;
            }
            txt(m, "- "); largura = 2;
        }
        int32_t salvo = m->indent; m->indent += largura;
        inlines(m, it->principal);
        txt(m, "\n");
        if (it->filhos) {
            /* blocos anexados: lista aninhada direto; outros com linha vazia antes */
            if (it->filhos->tipo != ADC_BL_LISTA && it->filhos->tipo != ADC_BL_DLIST) txt(m, "\n");
            blocos(m, it->filhos, true);
        }
        m->indent = salvo;
    }
    if (m->indent == 0) txt(m, "\n");
}

static void dlist(M* m, const ADC_Bloco* b) {
    titulo_de_bloco(m, b, NULL, NULL);
    for (const ADC_Bloco* it = b->filhos; it; it = it->proximo) {
        recuo(m); txt(m, "- ");
        for (int32_t k = 0; k < it->quantidade_termos; k++) { if (k) txt(m, ", "); txt(m, "**"); inlines(m, it->termos[k]); txt(m, "**"); }
        if (it->principal) { txt(m, ": "); int32_t salvo = m->indent; m->indent += 2; inlines(m, it->principal); m->indent = salvo; }
        txt(m, "\n");
        if (it->filhos) { int32_t salvo = m->indent; m->indent += 2; if (it->filhos->tipo != ADC_BL_LISTA && it->filhos->tipo != ADC_BL_DLIST) txt(m, "\n"); blocos(m, it->filhos, true); m->indent = salvo; }
    }
    if (m->indent == 0) txt(m, "\n");
}

static void celula(M* m, const ADC_Bloco* c) {
    bool antes = m->em_tabela; m->em_tabela = true;
    if (c->filhos) {
        bool primeiro = true;
        for (const ADC_Bloco* p = c->filhos; p; p = p->proximo) {
            if (!primeiro) txt(m, m->html ? "<br>" : " ");
            primeiro = false;
            if (p->tipo == ADC_BL_PARAGRAFO) inlines(m, p->inlines);
            else { ADC_Buffer sub; adc_buffer_iniciar(&sub); M mm = *m; mm.b = &sub; mm.indent = 0; blocos(&mm, p, true); ADC_Fatia t = adc_buffer_fatia_de(&sub); for (int32_t i = 0; i < t.tamanho; i++) adc_buffer_char(m->b, t.dados[i] == '\n' ? ' ' : t.dados[i]); adc_buffer_liberar(&sub); }
        }
    } else inlines(m, c->inlines);
    m->em_tabela = antes;
}

static void tabela(M* m, const ADC_Bloco* b) {
    const ADC_Tabela* T = b->tabela; if (!T) return;
    titulo_de_bloco(m, b, "table-caption", &m->ctx->contador_tabela);
    bool spans = false;
    for (const ADC_LinhaTabela* l = T->linhas; l; l = l->proxima) for (const ADC_Bloco* c = l->celulas; c; c = c->proximo) if (c->colspan > 1 || c->rowspan > 1) spans = true;
    if (spans) degradar(m, b, "a tabela tem células mescladas (colspan/rowspan); o GFM não as representa e a mescla foi perdida.");
    const ADC_LinhaTabela* l = T->linhas;
    int32_t ncol = T->quantidade_colunas;
    /* cabeçalho: a primeira linha se for header; senão uma linha vazia (GFM exige cabeçalho) */
    recuo(m); txt(m, "|");
    if (l && l->cabecalho) { for (const ADC_Bloco* c = l->celulas; c; c = c->proximo) { txt(m, " "); celula(m, c); txt(m, " |"); for (int32_t k = 1; k < c->colspan; k++) txt(m, " |"); } l = l->proxima; }
    else { for (int32_t k = 0; k < ncol; k++) txt(m, "   |"); degradar(m, b, "a tabela não tem linha de cabeçalho; o GFM exige uma e foi inserida vazia."); }
    txt(m, "\n"); recuo(m); txt(m, "|");
    for (int32_t k = 0; k < ncol; k++) {
        ADC_Alinhamento a = T->colunas ? T->colunas[k].horizontal : ADC_ALINHA_PADRAO;
        txt(m, a == ADC_ALINHA_CENTRO ? " :---: |" : a == ADC_ALINHA_DIREITA ? " ---: |" : a == ADC_ALINHA_ESQUERDA ? " :--- |" : " --- |");
    }
    txt(m, "\n");
    for (; l; l = l->proxima) {
        recuo(m); txt(m, "|");
        int32_t escritas = 0;
        for (const ADC_Bloco* c = l->celulas; c; c = c->proximo) { txt(m, " "); celula(m, c); txt(m, " |"); escritas += c->colspan; for (int32_t k = 1; k < c->colspan; k++) txt(m, " |"); }
        for (; escritas < ncol; escritas++) txt(m, "  |");
        txt(m, "\n");
    }
    txt(m, "\n");
}

static void admonicao(M* m, const ADC_Bloco* b) {
    const char* tag = b->variante_adm == ADC_ADM_NOTE ? "> [!NOTE]" : b->variante_adm == ADC_ADM_TIP ? "> [!TIP]" : b->variante_adm == ADC_ADM_IMPORTANT ? "> [!IMPORTANT]" : b->variante_adm == ADC_ADM_WARNING ? "> [!WARNING]" : "> [!CAUTION]";
    /* GFM alerts: "> [!NOTE]" e as linhas do conteúdo prefixadas */
    ADC_Buffer sub; adc_buffer_iniciar(&sub);
    M mm = *m; mm.b = &sub; mm.indent = 0;
    if (b->titulo) { adc_buffer_texto(&sub, "**"); inlines(&mm, b->titulo); adc_buffer_texto(&sub, "**\n\n"); }
    blocos(&mm, b->filhos, true);
    m->avisos = mm.avisos;
    ADC_Fatia t = adc_buffer_fatia_de(&sub);
    while (t.tamanho > 0 && t.dados[t.tamanho - 1] == '\n') t.tamanho--;
    recuo(m); txt(m, tag); txt(m, "\n");
    int32_t ini = 0;
    for (int32_t i = 0; i <= t.tamanho; i++) if (i == t.tamanho || t.dados[i] == '\n') { recuo(m); txt(m, i - ini > 0 ? "> " : ">"); adc_buffer_bytes(m->b, t.dados + ini, i - ini); txt(m, "\n"); ini = i + 1; }
    adc_buffer_liberar(&sub);
    txt(m, "\n");
}

static void secao(M* m, const ADC_Bloco* b) {
    int32_t nivel = b->nivel < 1 ? 1 : b->nivel; if (nivel > 5) nivel = 5;
    m->contadores[nivel]++; for (int32_t k = nivel + 1; k < 8; k++) m->contadores[k] = 0;
    separar(m);
    adc_buffer_repetir(m->b, '#', nivel + 1); txt(m, " ");
    if (m->sectnums) { adc_render_numero_secao(m->b, m->contadores, nivel); txt(m, " "); }
    inlines(m, b->titulo);
    if (b->meta.id.tamanho && m->html) { /* âncora explícita para xref */ txt(m, " <a id=\""); adc_buffer_fatia(m->b, b->meta.id); txt(m, "\"></a>"); }
    txt(m, "\n\n");
    blocos(m, b->filhos, true);
}

static void bloco(M* m, const ADC_Bloco* b, bool pPrimeiro) {
    if (!pPrimeiro && b->tipo != ADC_BL_SECAO) { if (m->b->tamanho > 0 && m->b->dados[m->b->tamanho - 1] != '\n') txt(m, "\n"); if (!(m->b->tamanho >= 2 && m->b->dados[m->b->tamanho - 2] == '\n')) txt(m, "\n"); }
    switch (b->tipo) {
        case ADC_BL_SECAO: secao(m, b); return;
        case ADC_BL_TITULO: { int32_t n = b->nivel < 1 ? 1 : b->nivel; if (n > 5) n = 5; recuo(m); adc_buffer_repetir(m->b, '#', n + 1); txt(m, " "); inlines(m, b->titulo); txt(m, "\n\n"); return; }
        case ADC_BL_PARAGRAFO: titulo_de_bloco(m, b, NULL, NULL); recuo(m); inlines(m, b->inlines); txt(m, "\n"); return;
        case ADC_BL_LISTING: case ADC_BL_LITERAL: verbatim(m, b); return;
        case ADC_BL_PASS: if (m->html) { adc_buffer_fatia(m->b, b->texto_cru); txt(m, "\n"); } else degradar(m, b, "bloco passthrough"); return;
        case ADC_BL_STEM: recuo(m); txt(m, "$$\n"); adc_buffer_fatia(m->b, b->texto_cru); txt(m, "\n$$\n"); return;
        case ADC_BL_VERSE: citacao(m, b, NULL); return;
        case ADC_BL_ADMONICAO: admonicao(m, b); return;
        case ADC_BL_EXEMPLO: { if (b->titulo) { ADC_Buffer t; adc_buffer_iniciar(&t); M mm = *m; mm.b = &t; adc_buffer_texto(&t, "**"); adc_render_legenda(m->ctx, b, &t, "example-caption", &m->ctx->contador_exemplo); inlines(&mm, b->titulo); adc_buffer_texto(&t, "**"); ADC_Bloco copia = *b; copia.titulo = NULL; char* p = adc_arena_copiar(m->ctx->arena, (const char*)t.dados, t.tamanho); adc_buffer_liberar(&t); citacao(m, &copia, p); } else citacao(m, b, NULL); return; }
        case ADC_BL_SIDEBAR: citacao(m, b, NULL); return;
        case ADC_BL_OPEN: if (adc_fatia_igual(b->meta.estilo, "abstract")) { citacao(m, b, NULL); return; } titulo_de_bloco(m, b, NULL, NULL); blocos(m, b->filhos, true); return;
        case ADC_BL_QUOTE: citacao(m, b, NULL); return;
        case ADC_BL_LISTA: lista(m, b); return;
        case ADC_BL_DLIST: dlist(m, b); return;
        case ADC_BL_TABELA: tabela(m, b); return;
        case ADC_BL_QUEBRA:
            if (b->variante_quebra == ADC_QUEBRA_THEMATIC) { recuo(m); txt(m, "---\n"); }
            else if (m->html) { recuo(m); txt(m, "<div style=\"page-break-after: always;\"></div>\n"); }
            else degradar_sem_equivalente(m, b, "a quebra de página");
            return;
        case ADC_BL_MACRO:
            if (adc_fatia_igual(b->nome_macro, "image")) {
                bool a; ADC_Fatia alt = adc_atributo_obter(b->meta.atributos, "alt", &a);
                recuo(m); txt(m, "!["); adc_buffer_fatia(m->b, alt); txt(m, "]("); adc_buffer_fatia(m->b, b->alvo); txt(m, ")\n");
                if (b->titulo) { recuo(m); txt(m, "\n"); recuo(m); txt(m, "*"); adc_render_legenda(m->ctx, b, m->b, "figure-caption", &m->ctx->contador_imagem); inlines(m, b->titulo); txt(m, "*\n"); }
            } else if (adc_fatia_igual(b->nome_macro, "toc")) {
                /* GFM não tem sumário automático; omitido com aviso — só quando o macro estaria ativo (:toc: macro),
                   porque sem isso nem o Asciidoctor o renderiza */
                const ADC_TabelaAtributos* at = &m->ctx->atribs;
                ADC_Fatia tv;
                if (adc_render_toc(m->ctx, &tv) && (adc_fatia_igual(tv, "macro") || adc_fatia_igual(adc_atribs_valor_ou(at, "toc-placement", ""), "macro")))
                    degradar_sem_equivalente(m, b, "o sumário (toc::[])");
            }
            else { recuo(m); txt(m, "["); adc_buffer_fatia(m->b, b->nome_macro); txt(m, "]("); adc_buffer_fatia(m->b, b->alvo); txt(m, ")\n"); }
            return;
        default: return;
    }
}

static void blocos(M* m, const ADC_Bloco* pPrimeiro, bool pPrimeiroBloco) {
    bool primeiro = pPrimeiroBloco;
    for (const ADC_Bloco* b = pPrimeiro; b; b = b->proximo) { bloco(m, b, primeiro); primeiro = false; }
}

void adc_markdown_escrever(ADC_Contexto* pC, ADC_Buffer* pSaida) {
    M m; memset(&m, 0, sizeof m);
    m.ctx = pC; m.b = pSaida; m.html = pC->opcoes.md_html_embutido;
    m.sectnums = adc_atribs_definido(&pC->atribs, "sectnums") || adc_atribs_definido(&pC->atribs, "numbered");
    if (pC->cabecalho.presente) {
        txt(&m, "# "); inlines(&m, pC->cabecalho.titulo); txt(&m, "\n");
        if (pC->cabecalho.autores) { txt(&m, "\n"); for (const ADC_Autor* a = pC->cabecalho.autores; a; a = a->proximo) { if (a != pC->cabecalho.autores) txt(&m, "; "); esc_texto(&m, a->nome_completo); if (a->email.tamanho) { txt(&m, " <"); adc_buffer_fatia(m.b, a->email); txt(&m, ">"); } } txt(&m, "\n"); }
        if (pC->cabecalho.revisao_numero.tamanho || pC->cabecalho.revisao_data.tamanho) { txt(&m, "\n"); if (pC->cabecalho.revisao_numero.tamanho) { esc_texto(&m, adc_atribs_valor_ou(&pC->atribs, "version-label", "Version")); txt(&m, " "); adc_buffer_fatia(m.b, pC->cabecalho.revisao_numero); } if (pC->cabecalho.revisao_data.tamanho) { if (pC->cabecalho.revisao_numero.tamanho) txt(&m, ", "); adc_buffer_fatia(m.b, pC->cabecalho.revisao_data); } txt(&m, "\n"); }
        txt(&m, "\n");
    }
    blocos(&m, pC->documento ? pC->documento->filhos : NULL, true);
    if (pC->notas) {
        txt(&m, "\n");
        for (const ADC_NotaRodape* n = pC->notas; n; n = n->proxima) { txt(&m, "[^"); adc_buffer_int(m.b, n->numero); txt(&m, "]: "); inlines(&m, n->texto); txt(&m, "\n"); }
    }
    /* garante um só '\n' final */
    while (pSaida->tamanho >= 2 && pSaida->dados[pSaida->tamanho - 1] == '\n' && pSaida->dados[pSaida->tamanho - 2] == '\n') pSaida->tamanho--;
}
