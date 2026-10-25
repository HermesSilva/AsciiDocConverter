/*
 * HTML5 na estrutura do conversor html5 do Asciidoctor 2.0.x (o golden de test/golden é gerado por ele).
 */
#include "../adc_render.h"
#include "../adc_recursos.h"

typedef struct H {
    ADC_Contexto* ctx;
    ADC_Buffer*   b;
    int32_t       contadores[8];      /* numeração de seções                          */
    bool          sectnums;
    int32_t       cont_figura, cont_tabela, cont_exemplo, cont_listing;
    bool          icons_font;
    bool          sectanchors, sectlinks;  /* âncora e link no título da seção (Asciidoctor) */
    bool          autonomo;                /* documento completo (<html>) ou só o corpo              */
    bool          icons_imagem;            /* :icons: definido com valor diferente de font → <img>   */
} H;

/* <img src="{iconsdir}/{nome}.{icontype}" alt="..."> — ícone de imagem do Asciidoctor (icon_uri). */
static void icone_img(H* h, const char* pNome, int32_t pNumero, ADC_Fatia pAlt) {
    adc_buffer_texto(h->b, "<img src=\"");
    adc_html_escapar_atributo(h->b, adc_atribs_valor_ou(&h->ctx->atribs, "iconsdir", "./images/icons"));
    adc_buffer_char(h->b, '/'); adc_buffer_texto(h->b, pNome);
    if (pNumero >= 0) adc_buffer_int(h->b, pNumero);
    adc_buffer_char(h->b, '.'); adc_html_escapar_atributo(h->b, adc_atribs_valor_ou(&h->ctx->atribs, "icontype", "png"));
    adc_buffer_texto(h->b, "\" alt=\"");
    if (pNumero >= 0) adc_buffer_int(h->b, pNumero); else adc_html_escapar_atributo(h->b, pAlt);
    adc_buffer_texto(h->b, "\">");
}

static void inlines(H* h, const ADC_Inline* pI);
static void inline_um(H* h, const ADC_Inline* i);
static void blocos(H* h, const ADC_Bloco* pPrimeiro);

static void esc(H* h, ADC_Fatia t) { adc_html_escapar(h->b, t); }
static void txt(H* h, const char* t) { adc_buffer_texto(h->b, t); }

static void id_e_classe(H* h, const ADC_Bloco* b, const char* pClasse) {
    if (b->meta.id.tamanho) { txt(h, " id=\""); adc_html_escapar_atributo(h->b, b->meta.id); txt(h, "\""); }
    txt(h, " class=\""); txt(h, pClasse);
    for (int32_t k = 0; k < b->meta.papeis.quantidade; k++) { txt(h, " "); esc(h, b->meta.papeis.itens[k]); }
    txt(h, "\"");
}

static void titulo_div(H* h, const ADC_Bloco* b) {
    if (!b->titulo) return;
    txt(h, "<div class=\"title\">"); inlines(h, b->titulo); txt(h, "</div>\n");
}

static void texto_cru_escapado(H* h, const ADC_Inline* pI) {
    /* verbatim: textos escapados; callouts viram <b class="conum">(n)</b> */
    for (const ADC_Inline* i = pI; i; i = i->proximo) {
        if (i->tipo == ADC_IN_CHARREF && adc_fatia_igual(i->valor, "callout")) {
            if (h->icons_font) { txt(h, "<i class=\"conum\" data-value=\""); adc_buffer_int(h->b, i->numero); txt(h, "\"></i><b>("); adc_buffer_int(h->b, i->numero); txt(h, ")</b>"); }
            else if (h->icons_imagem) icone_img(h, "callouts/", i->numero, adc_fatia(NULL, 0));
            else { txt(h, "<b class=\"conum\">("); adc_buffer_int(h->b, i->numero); txt(h, ")</b>"); }
        } else if (i->tipo == ADC_IN_TEXTO) {
            bool antes_de_callout = i->proximo && i->proximo->tipo == ADC_IN_CHARREF && adc_fatia_igual(i->proximo->valor, "callout");
            ADC_Fatia v = i->valor;
            if (antes_de_callout && (h->icons_font || h->icons_imagem)) {
                /* com ícones o Asciidoctor descarta o comentário de linha (guard: //, #, --, ;;) antes do callout */
                if (v.tamanho >= 2 && (memcmp(v.dados + v.tamanho - 2, "//", 2) == 0 || memcmp(v.dados + v.tamanho - 2, "--", 2) == 0 || memcmp(v.dados + v.tamanho - 2, ";;", 2) == 0)) v.tamanho -= 2;
                else if (v.tamanho >= 1 && v.dados[v.tamanho - 1] == '#') v.tamanho -= 1;
                else { esc(h, v); if (v.tamanho && v.dados[v.tamanho - 1] != '\n') txt(h, " "); continue; }
                esc(h, v);
                continue;
            }
            /* o espaço antes do callout é preservado pelo Asciidoctor; o texto já vem sem o espaço final */
            esc(h, v);
            if (antes_de_callout && v.tamanho && v.dados[v.tamanho - 1] != '\n') txt(h, " ");
        } else if (i->tipo == ADC_IN_RAW) adc_buffer_fatia(h->b, i->valor);
        else inline_um(h, i);   /* só este nó: o laço já percorre os irmãos */
    }
}

static void inline_um(H* h, const ADC_Inline* i) {
    switch (i->tipo) {
        case ADC_IN_TEXTO: esc(h, i->valor); break;
        case ADC_IN_CHARREF:
            if (adc_fatia_igual(i->valor, "callout")) { txt(h, "<b class=\"conum\">("); adc_buffer_int(h->b, i->numero); txt(h, ")</b>"); }
            else { txt(h, "&"); adc_buffer_fatia(h->b, i->valor); txt(h, ";"); }
            break;
        case ADC_IN_RAW:
            if (i->valor.dados == NULL && i->filhos) inlines(h, i->filhos);
            else if (adc_fatia_igual(i->alvo, "latexmath")) { txt(h, "\\("); esc(h, i->valor); txt(h, "\\)"); }
            else if (adc_fatia_igual(i->alvo, "stem") || adc_fatia_igual(i->alvo, "asciimath")) { txt(h, "\\$"); esc(h, i->valor); txt(h, "\\$"); }
            else adc_buffer_fatia(h->b, i->valor);
            break;
        case ADC_IN_SPAN: {
            const char* tag = i->variante_span == ADC_SPAN_STRONG ? "strong" : i->variante_span == ADC_SPAN_EMPHASIS ? "em" : i->variante_span == ADC_SPAN_CODE ? "code" : i->variante_span == ADC_SPAN_MARK ? "mark" : i->variante_span == ADC_SPAN_SUPERSCRIPT ? "sup" : "sub";
            bool span_role = i->variante_span == ADC_SPAN_MARK && i->papeis.quantidade > 0;
            if (span_role) tag = "span";
            txt(h, "<"); txt(h, tag);
            if (i->id.tamanho) { txt(h, " id=\""); adc_html_escapar_atributo(h->b, i->id); txt(h, "\""); }
            if (i->papeis.quantidade) { txt(h, " class=\""); for (int32_t k = 0; k < i->papeis.quantidade; k++) { if (k) txt(h, " "); esc(h, i->papeis.itens[k]); } txt(h, "\""); }
            txt(h, ">"); inlines(h, i->filhos); txt(h, "</"); txt(h, tag); txt(h, ">");
            break;
        }
        case ADC_IN_REF:
            if (i->variante_ref == ADC_REF_LINK) {
                bool bare = i->filhos && i->filhos->tipo == ADC_IN_TEXTO && !i->filhos->proximo && adc_fatia_igual_f(i->filhos->valor, i->alvo);
                txt(h, "<a href=\""); adc_html_escapar_atributo(h->b, i->alvo); txt(h, "\"");
                {
                    /* um só atributo class: "bare" e o role juntos (Asciidoctor: class="bare include") */
                    bool a; ADC_Fatia role = adc_atributo_obter(i->atributos, "role", &a);
                    if (bare || a) {
                        txt(h, " class=\"");
                        if (bare) txt(h, "bare");
                        if (a) { if (bare) txt(h, " "); esc(h, role); }
                        txt(h, "\"");
                    }
                }
                { bool a; ADC_Fatia t = adc_atributo_obter(i->atributos, "title", &a); if (a) { txt(h, " title=\""); adc_html_escapar_atributo(h->b, t); txt(h, "\""); } }
                if (i->alvo_externo) txt(h, " target=\"_blank\" rel=\"noopener\"");
                else { bool a; ADC_Fatia w = adc_atributo_obter(i->atributos, "window", &a); if (a) { txt(h, " target=\""); esc(h, w); txt(h, "\""); } }
                txt(h, ">"); inlines(h, i->filhos); txt(h, "</a>");
            } else {
                ADC_Fatia alvo = i->alvo;
                /* xref:arquivo.adoc#id[] → href arquivo.html#id ; <<id>> → #id */
                int32_t hash = adc_fatia_indice(alvo, '#');
                txt(h, "<a href=\"");
                if (hash >= 0) {
                    ADC_Fatia arq = adc_fatia(alvo.dados, hash);
                    if (adc_fatia_termina(arq, ".adoc")) { adc_html_escapar_atributo(h->b, adc_fatia(arq.dados, arq.tamanho - 5)); txt(h, ".html"); } else adc_html_escapar_atributo(h->b, arq);
                    txt(h, "#"); adc_html_escapar_atributo(h->b, adc_fatia(alvo.dados + hash + 1, alvo.tamanho - hash - 1));
                } else if (adc_fatia_termina(alvo, ".adoc")) { adc_html_escapar_atributo(h->b, adc_fatia(alvo.dados, alvo.tamanho - 5)); txt(h, ".html"); }
                else { txt(h, "#"); adc_html_escapar_atributo(h->b, alvo); }
                txt(h, "\">");
                if (i->filhos) inlines(h, i->filhos);
                else {
                    ADC_Fatia id = hash >= 0 ? adc_fatia(alvo.dados + hash + 1, alvo.tamanho - hash - 1) : alvo;
                    const ADC_Inline* t = adc_render_texto_xref(h->ctx, id);
                    if (t) inlines(h, t);
                    else { txt(h, "["); esc(h, id); txt(h, "]"); }
                }
                txt(h, "</a>");
            }
            break;
        case ADC_IN_IMAGEM: {
            bool a;
            ADC_Fatia alt = adc_atributo_obter(i->atributos, "alt", &a);
            ADC_Fatia link = adc_atributo_obter(i->atributos, "link", &a); bool tem_link = a;
            ADC_Fatia role = adc_atributo_obter(i->atributos, "role", &a); bool tem_role = a;
            txt(h, "<span class=\"image"); if (tem_role) { txt(h, " "); esc(h, role); } txt(h, "\">");
            if (tem_link) { txt(h, "<a class=\"image\" href=\""); adc_html_escapar_atributo(h->b, link); txt(h, "\">"); }
            txt(h, "<img src=\""); { ADC_Fatia dir; if (adc_atribs_obter_cstr(&h->ctx->atribs, "imagesdir", &dir) && dir.tamanho && !adc_fatia_comeca(i->alvo, "http") && !adc_fatia_comeca(i->alvo, "/") ) { adc_html_escapar_atributo(h->b, dir); txt(h, "/"); } }
            adc_html_escapar_atributo(h->b, i->alvo); txt(h, "\" alt=\""); adc_html_escapar_atributo(h->b, alt); txt(h, "\"");
            ADC_Fatia w = adc_atributo_obter(i->atributos, "width", &a); if (a) { txt(h, " width=\""); esc(h, w); txt(h, "\""); }
            ADC_Fatia ht = adc_atributo_obter(i->atributos, "height", &a); if (a) { txt(h, " height=\""); esc(h, ht); txt(h, "\""); }
            ADC_Fatia ti = adc_atributo_obter(i->atributos, "title", &a); if (a) { txt(h, " title=\""); adc_html_escapar_atributo(h->b, ti); txt(h, "\""); }
            txt(h, ">");
            if (tem_link) txt(h, "</a>");
            txt(h, "</span>");
            break;
        }
        case ADC_IN_NOTA:
            if (i->numero > 0) {
                bool ref = adc_fatia_igual(i->valor, "ref");
                if (ref) { txt(h, "<sup class=\"footnoteref\">[<a class=\"footnote\" href=\"#_footnotedef_"); adc_buffer_int(h->b, i->numero); txt(h, "\" title=\"View footnote.\">"); adc_buffer_int(h->b, i->numero); txt(h, "</a>]</sup>"); }
                else {
                    txt(h, "<sup class=\"footnote\"");
                    if (i->id.tamanho) { txt(h, " id=\"_footnote_"); adc_html_escapar_atributo(h->b, i->id); txt(h, "\""); }
                    txt(h, ">[<a id=\"_footnoteref_"); adc_buffer_int(h->b, i->numero); txt(h, "\" class=\"footnote\" href=\"#_footnotedef_"); adc_buffer_int(h->b, i->numero); txt(h, "\" title=\"View footnote.\">"); adc_buffer_int(h->b, i->numero); txt(h, "</a>]</sup>");
                }
            } else inlines(h, i->filhos);
            break;
        case ADC_IN_QUEBRA_LINHA: txt(h, i->proximo ? "<br>\n" : "<br>"); break;   /* no fim do texto, sem a quebra (Asciidoctor) */
        case ADC_IN_ANCORA: txt(h, "<a id=\""); adc_html_escapar_atributo(h->b, i->id); txt(h, "\"></a>"); break;
        case ADC_IN_TECLA: {
            int32_t n = 0; for (const ADC_Inline* k = i->filhos; k; k = k->proximo) n++;
            if (n > 1) txt(h, "<span class=\"keyseq\">");
            for (const ADC_Inline* k = i->filhos; k; k = k->proximo) { if (k != i->filhos) txt(h, "+"); txt(h, "<kbd>"); esc(h, k->valor); txt(h, "</kbd>"); }
            if (n > 1) txt(h, "</span>");
            break;
        }
        case ADC_IN_BOTAO: txt(h, "<b class=\"button\">"); inlines(h, i->filhos); txt(h, "</b>"); break;
        case ADC_IN_MENU: {
            int32_t n = 0; for (const ADC_Inline* k = i->filhos; k; k = k->proximo) n++;
            if (n <= 1) { txt(h, "<b class=\"menuref\">"); if (i->filhos) esc(h, i->filhos->valor); txt(h, "</b>"); break; }
            txt(h, "<span class=\"menuseq\"><b class=\"menu\">"); esc(h, i->filhos->valor); txt(h, "</b>");
            int32_t idx = 0;
            for (const ADC_Inline* k = i->filhos->proximo; k; k = k->proximo, idx++) {
                txt(h, "&#160;<b class=\"caret\">&#8250;</b> <b class=\""); txt(h, k->proximo ? "submenu" : "menuitem"); txt(h, "\">"); esc(h, k->valor); txt(h, "</b>");
            }
            txt(h, "</span>");
            break;
        }
        case ADC_IN_ICONE: {
            bool a; ADC_Fatia alt = adc_atributo_obter(i->atributos, "alt", &a);
            if (h->icons_font) { txt(h, "<span class=\"icon\"><i class=\"fa fa-"); esc(h, i->alvo); txt(h, "\"></i></span>"); }
            else { txt(h, "<span class=\"icon\">["); if (a) esc(h, alt); else esc(h, i->alvo); txt(h, "&#93;</span>"); }
            break;
        }
        case ADC_IN_INDICE: if (i->filhos) inlines(h, i->filhos); break;
        default: break;
    }
}

static void inlines(H* h, const ADC_Inline* pI) {
    for (const ADC_Inline* i = pI; i; i = i->proximo) inline_um(h, i);
}

/* ------------------------------------------------------------------ blocos ---- */

static void abrir_div(H* h, const ADC_Bloco* b, const char* pClasse) { txt(h, "<div"); id_e_classe(h, b, pClasse); txt(h, ">\n"); }

static void paragrafo(H* h, const ADC_Bloco* b) {
    abrir_div(h, b, "paragraph");
    titulo_div(h, b);
    txt(h, "<p>"); inlines(h, b->inlines); txt(h, "</p>\n</div>\n");
}

static void listing(H* h, const ADC_Bloco* b) {
    abrir_div(h, b, b->tipo == ADC_BL_LISTING ? "listingblock" : "literalblock");
    if (b->titulo) {
        txt(h, "<div class=\"title\">");
        if (b->tipo == ADC_BL_LISTING) adc_render_legenda(h->ctx, b, h->b, "listing-caption", &h->cont_listing);
        inlines(h, b->titulo); txt(h, "</div>\n");
    }
    txt(h, "<div class=\"content\">\n");
    ADC_Fatia lang = b->linguagem;
    bool source = b->tipo == ADC_BL_LISTING && (adc_fatia_igual(b->meta.estilo, "source") || lang.tamanho > 0);
    if (source) {
        txt(h, "<pre class=\"highlight\"><code");
        if (lang.tamanho) { txt(h, " class=\"language-"); esc(h, lang); txt(h, "\" data-lang=\""); esc(h, lang); txt(h, "\""); }
        txt(h, ">");
        texto_cru_escapado(h, b->inlines);
        txt(h, "</code></pre>\n");
    } else {
        txt(h, "<pre>"); texto_cru_escapado(h, b->inlines); txt(h, "</pre>\n");
    }
    txt(h, "</div>\n</div>\n");
}

static void admonicao(H* h, const ADC_Bloco* b) {
    char classe[64]; ADC_Fatia v = adc_variante_adm_nome(b->variante_adm);
    snprintf(classe, sizeof classe, "admonitionblock %.*s", v.tamanho, v.dados);
    abrir_div(h, b, classe);
    txt(h, "<table>\n<tr>\n<td class=\"icon\">\n");
    /* rótulo (textlabel): caption= do bloco ou o <variante>-caption do documento */
    ADC_Fatia rotulo; { bool a; rotulo = adc_atributo_obter(b->meta.atributos, "caption", &a); if (!a) rotulo = adc_render_rotulo_admonicao(h->ctx, b->variante_adm); }
    /* Asciidoctor: icons=font → <i>; icons com outro valor (até vazio) → <img {iconsdir}/{nome}.{icontype}>; sem icons → texto */
    if (h->icons_font) { txt(h, "<i class=\"fa icon-"); adc_buffer_fatia(h->b, v); txt(h, "\" title=\""); adc_html_escapar_atributo(h->b, rotulo); txt(h, "\"></i>\n"); }
    else if (h->icons_imagem) {
        char nome[32]; snprintf(nome, sizeof nome, "%.*s", v.tamanho, v.dados);
        icone_img(h, nome, -1, rotulo); txt(h, "\n");
    }
    else { txt(h, "<div class=\"title\">"); esc(h, rotulo); txt(h, "</div>\n"); }
    txt(h, "</td>\n<td class=\"content\">\n");
    titulo_div(h, b);
    if (b->forma == ADC_FORMA_PARAGRAFO && b->filhos && b->filhos->tipo == ADC_BL_PARAGRAFO && !b->filhos->proximo) { inlines(h, b->filhos->inlines); txt(h, "\n"); }
    else blocos(h, b->filhos);
    txt(h, "</td>\n</tr>\n</table>\n</div>\n");
}

static void conteudo_composto(H* h, const ADC_Bloco* b) {
    /* bloco nascido de um parágrafo com estilo: o texto sai direto, sem o <div class="paragraph"> */
    if (b->forma == ADC_FORMA_PARAGRAFO && b->filhos && b->filhos->tipo == ADC_BL_PARAGRAFO && !b->filhos->proximo && !b->filhos->titulo) { inlines(h, b->filhos->inlines); txt(h, "\n"); }
    else blocos(h, b->filhos);
}

static void composto(H* h, const ADC_Bloco* b, const char* pClasse, const char* pLegenda, int32_t* pContador) {
    abrir_div(h, b, pClasse);
    if (b->tipo == ADC_BL_SIDEBAR) {
        txt(h, "<div class=\"content\">\n"); titulo_div(h, b); conteudo_composto(h, b); txt(h, "</div>\n</div>\n");
        return;
    }
    if (b->titulo) { txt(h, "<div class=\"title\">"); if (pLegenda) adc_render_legenda(h->ctx, b, h->b, pLegenda, pContador); inlines(h, b->titulo); txt(h, "</div>\n"); }
    txt(h, "<div class=\"content\">\n"); conteudo_composto(h, b); txt(h, "</div>\n</div>\n");
}

static void atribuicao(H* h, const ADC_Bloco* b) {
    if (!b->atribuicao.tamanho && !b->citacao.tamanho) return;
    txt(h, "<div class=\"attribution\">\n");
    if (b->atribuicao.tamanho) { txt(h, "&#8212; "); esc(h, b->atribuicao); }
    if (b->citacao.tamanho) { if (b->atribuicao.tamanho) txt(h, "<br>\n"); txt(h, "<cite>"); esc(h, b->citacao); txt(h, "</cite>"); }
    txt(h, "\n</div>\n");
}

static void quote(H* h, const ADC_Bloco* b) {
    abrir_div(h, b, adc_fatia_igual(b->meta.estilo, "abstract") ? "quoteblock abstract" : "quoteblock");
    titulo_div(h, b);
    txt(h, "<blockquote>\n");
    if (b->forma == ADC_FORMA_PARAGRAFO && b->filhos && b->filhos->tipo == ADC_BL_PARAGRAFO && !b->filhos->proximo) { inlines(h, b->filhos->inlines); txt(h, "\n"); }
    else blocos(h, b->filhos);
    txt(h, "</blockquote>\n");
    atribuicao(h, b);
    txt(h, "</div>\n");
}

static void verse(H* h, const ADC_Bloco* b) {
    abrir_div(h, b, "verseblock");
    titulo_div(h, b);
    txt(h, "<pre class=\"content\">"); inlines(h, b->inlines); txt(h, "</pre>\n");
    atribuicao(h, b);
    txt(h, "</div>\n");
}

static const char* estilo_ol(const ADC_Bloco* b) {
    ADC_Fatia m = b->marcador;
    if (adc_fatia_igual(b->meta.estilo, "loweralpha") || adc_fatia_igual(b->meta.estilo, "upperalpha") || adc_fatia_igual(b->meta.estilo, "lowerroman") || adc_fatia_igual(b->meta.estilo, "upperroman") || adc_fatia_igual(b->meta.estilo, "arabic") || adc_fatia_igual(b->meta.estilo, "decimal")) {
        static char tmp[16]; snprintf(tmp, sizeof tmp, "%.*s", b->meta.estilo.tamanho, b->meta.estilo.dados); return tmp;
    }
    if (m.tamanho >= 2 && adc_eh_alfa(m.dados[0])) {
        if (m.dados[m.tamanho - 1] == ')' ) return (m.dados[0] >= 'A' && m.dados[0] <= 'Z') ? "upperroman" : "lowerroman";
        return (m.dados[0] >= 'A' && m.dados[0] <= 'Z') ? "upperalpha" : "loweralpha";
    }
    /* "." = arabic; ".." = loweralpha; "..." = lowerroman; "...." = upperalpha; "....." = upperroman */
    if (m.tamanho >= 1 && m.dados[0] == '.') { switch (m.tamanho) { case 1: return "arabic"; case 2: return "loweralpha"; case 3: return "lowerroman"; case 4: return "upperalpha"; default: return "upperroman"; } }
    return "arabic";
}

static void item_corpo(H* h, const ADC_Bloco* item) {
    txt(h, "<p>"); inlines(h, item->principal); txt(h, "</p>\n");
    blocos(h, item->filhos);
}

static void lista(H* h, const ADC_Bloco* b) {
    if (b->variante_lista == ADC_LISTA_CALLOUT) {
        abrir_div(h, b, "colist arabic");
        titulo_div(h, b);
        if (h->icons_font || h->icons_imagem) {
            txt(h, "<table>\n");
            for (const ADC_Bloco* it = b->filhos; it; it = it->proximo) {
                txt(h, "<tr>\n<td>");
                if (h->icons_font) { txt(h, "<i class=\"conum\" data-value=\""); adc_buffer_int(h->b, it->numero); txt(h, "\"></i><b>"); adc_buffer_int(h->b, it->numero); txt(h, "</b>"); }
                else icone_img(h, "callouts/", it->numero, adc_fatia(NULL, 0));
                txt(h, "</td>\n<td>"); inlines(h, it->principal); blocos(h, it->filhos); txt(h, "</td>\n</tr>\n");
            }
            txt(h, "</table>\n</div>\n");
        } else {
            txt(h, "<ol>\n");
            for (const ADC_Bloco* it = b->filhos; it; it = it->proximo) { txt(h, "<li>\n"); item_corpo(h, it); txt(h, "</li>\n"); }
            txt(h, "</ol>\n</div>\n");
        }
        return;
    }
    if (b->variante_lista == ADC_LISTA_ORDERED) {
        const char* est = estilo_ol(b);
        char classe[64]; snprintf(classe, sizeof classe, "olist %s", est);
        abrir_div(h, b, classe);
        titulo_div(h, b);
        txt(h, "<ol class=\""); txt(h, est); txt(h, "\"");
        if (strcmp(est, "loweralpha") == 0) txt(h, " type=\"a\""); else if (strcmp(est, "upperalpha") == 0) txt(h, " type=\"A\""); else if (strcmp(est, "lowerroman") == 0) txt(h, " type=\"i\""); else if (strcmp(est, "upperroman") == 0) txt(h, " type=\"I\"");
        if (b->numero > 1) { txt(h, " start=\""); adc_buffer_int(h->b, b->numero); txt(h, "\""); }
        if (adc_lista_fatias_contem(&b->meta.opcoes, "reversed")) txt(h, " reversed");
        txt(h, ">\n");
        for (const ADC_Bloco* it = b->filhos; it; it = it->proximo) { txt(h, "<li>\n"); item_corpo(h, it); txt(h, "</li>\n"); }
        txt(h, "</ol>\n</div>\n");
        return;
    }
    const char* classe = "ulist";
    char tmp[64];
    if (adc_fatia_igual(b->meta.estilo, "checklist") || (b->filhos && b->filhos->principal && b->filhos->principal->tipo == ADC_IN_TEXTO && (adc_fatia_comeca(b->filhos->principal->valor, "[ ] ") || adc_fatia_comeca(b->filhos->principal->valor, "[x] ") || adc_fatia_comeca(b->filhos->principal->valor, "[*] ")))) { classe = "ulist checklist"; }
    if (b->meta.estilo.tamanho && !adc_fatia_igual(b->meta.estilo, "checklist")) { snprintf(tmp, sizeof tmp, "ulist %.*s", b->meta.estilo.tamanho, b->meta.estilo.dados); classe = tmp; }
    abrir_div(h, b, classe);
    titulo_div(h, b);
    txt(h, "<ul");
    if (strncmp(classe, "ulist checklist", 15) == 0) txt(h, " class=\"checklist\"");
    else if (b->meta.estilo.tamanho) { txt(h, " class=\""); esc(h, b->meta.estilo); txt(h, "\""); }
    txt(h, ">\n");
    for (const ADC_Bloco* it = b->filhos; it; it = it->proximo) {
        txt(h, "<li>\n<p>");
        /* checklist: [ ] / [x] */
        if (it->principal && it->principal->tipo == ADC_IN_TEXTO && it->principal->valor.tamanho >= 4 && it->principal->valor.dados[0] == '[' && it->principal->valor.dados[2] == ']' && it->principal->valor.dados[3] == ' ') {
            bool marcado = it->principal->valor.dados[1] != ' ';
            txt(h, marcado ? "&#10003; " : "&#10063; ");
            ADC_Inline copia = *it->principal; copia.valor = adc_fatia(copia.valor.dados + 4, copia.valor.tamanho - 4);
            inline_um(h, &copia);
            inlines(h, it->principal->proximo);
        } else inlines(h, it->principal);
        txt(h, "</p>\n");
        blocos(h, it->filhos);
        txt(h, "</li>\n");
    }
    txt(h, "</ul>\n</div>\n");
}

static void dlist(H* h, const ADC_Bloco* b) {
    if (adc_fatia_igual(b->meta.estilo, "horizontal")) {
        abrir_div(h, b, "hdlist");
        titulo_div(h, b);
        txt(h, "<table>\n");
        for (const ADC_Bloco* it = b->filhos; it; it = it->proximo) {
            txt(h, "<tr>\n<td class=\"hdlist1\">\n");
            for (int32_t k = 0; k < it->quantidade_termos; k++) { inlines(h, it->termos[k]); txt(h, k + 1 < it->quantidade_termos ? "\n<br>\n" : "\n"); }
            txt(h, "</td>\n<td class=\"hdlist2\">\n");
            if (it->principal) { txt(h, "<p>"); inlines(h, it->principal); txt(h, "</p>\n"); }
            blocos(h, it->filhos);
            txt(h, "</td>\n</tr>\n");
        }
        txt(h, "</table>\n</div>\n");
        return;
    }
    if (adc_fatia_igual(b->meta.estilo, "qanda")) {
        abrir_div(h, b, "qlist qanda");
        titulo_div(h, b);
        txt(h, "<ol>\n");
        for (const ADC_Bloco* it = b->filhos; it; it = it->proximo) {
            txt(h, "<li>\n");
            for (int32_t k = 0; k < it->quantidade_termos; k++) { txt(h, "<p><em>"); inlines(h, it->termos[k]); txt(h, "</em></p>\n"); }
            if (it->principal) { txt(h, "<p>"); inlines(h, it->principal); txt(h, "</p>\n"); }
            blocos(h, it->filhos);
            txt(h, "</li>\n");
        }
        txt(h, "</ol>\n</div>\n");
        return;
    }
    abrir_div(h, b, "dlist");
    titulo_div(h, b);
    txt(h, "<dl>\n");
    for (const ADC_Bloco* it = b->filhos; it; it = it->proximo) {
        for (int32_t k = 0; k < it->quantidade_termos; k++) { txt(h, "<dt class=\"hdlist1\">"); inlines(h, it->termos[k]); txt(h, "</dt>\n"); }
        if (it->principal || it->filhos) {
            txt(h, "<dd>\n");
            if (it->principal) { txt(h, "<p>"); inlines(h, it->principal); txt(h, "</p>\n"); }
            blocos(h, it->filhos);
            txt(h, "</dd>\n");
        }
    }
    txt(h, "</dl>\n</div>\n");
}

static void tabela(H* h, const ADC_Bloco* b) {
    const ADC_Tabela* T = b->tabela;
    if (!T) return;
    ADC_Fatia frame = T->molduras.tamanho ? T->molduras : adc_fatia_cstr("all");
    if (adc_fatia_igual(frame, "topbot")) frame = adc_fatia_cstr("ends");
    ADC_Fatia grid = T->grade.tamanho ? T->grade : adc_fatia_cstr("all");
    txt(h, "<table");
    if (b->meta.id.tamanho) { txt(h, " id=\""); adc_html_escapar_atributo(h->b, b->meta.id); txt(h, "\""); }
    txt(h, " class=\"tableblock frame-"); esc(h, frame); txt(h, " grid-"); esc(h, grid);
    if (T->autowidth) txt(h, " fit-content");
    else if (T->largura_percentual > 0 && T->largura_percentual < 100) { txt(h, "\" style=\"width: "); adc_buffer_int(h->b, T->largura_percentual); txt(h, "%;"); }
    else txt(h, " stretch");
    for (int32_t k = 0; k < b->meta.papeis.quantidade; k++) { txt(h, " "); esc(h, b->meta.papeis.itens[k]); }
    txt(h, "\">\n");
    if (b->titulo) { txt(h, "<caption class=\"title\">"); adc_render_legenda(h->ctx, b, h->b, "table-caption", &h->cont_tabela); inlines(h, b->titulo); txt(h, "</caption>\n"); }
    txt(h, "<colgroup>\n");
    int32_t total = 0; bool pct = false;
    for (int32_t k = 0; k < T->quantidade_colunas; k++) { total += T->colunas[k].largura; if (T->colunas[k].percentual) pct = true; }
    double soma = 0;
    for (int32_t k = 0; k < T->quantidade_colunas; k++) {
        if (T->autowidth || T->colunas[k].automatica) { txt(h, "<col>\n"); continue; }
        double w;
        if (pct) w = T->colunas[k].largura;
        else if (k + 1 == T->quantidade_colunas) w = 100.0 - soma;
        else { w = (double)T->colunas[k].largura * 100.0 / (double)(total ? total : 1); w = ((double)(int64_t)(w * 10000.0)) / 10000.0; }
        soma += w;
        txt(h, "<col style=\"width: "); adc_buffer_decimal(h->b, w, 4); txt(h, "%;\">\n");
    }
    txt(h, "</colgroup>\n");
    bool em_thead = false, em_tbody = false, em_tfoot = false;
    for (const ADC_LinhaTabela* l = T->linhas; l; l = l->proxima) {
        if (l->cabecalho) { if (!em_thead) { txt(h, "<thead>\n"); em_thead = true; } }
        else if (l->rodape) { if (em_thead) { txt(h, "</thead>\n"); em_thead = false; } if (em_tbody) { txt(h, "</tbody>\n"); em_tbody = false; } if (!em_tfoot) { txt(h, "<tfoot>\n"); em_tfoot = true; } }
        else { if (em_thead) { txt(h, "</thead>\n"); em_thead = false; } if (!em_tbody) { txt(h, "<tbody>\n"); em_tbody = true; } }
        txt(h, "<tr>\n");
        for (const ADC_Bloco* c = l->celulas; c; c = c->proximo) {
            bool th = l->cabecalho || c->estilo_celula == 'h';
            txt(h, th ? "<th class=\"tableblock" : "<td class=\"tableblock");
            txt(h, " halign-"); txt(h, c->alinha_h == ADC_ALINHA_CENTRO ? "center" : c->alinha_h == ADC_ALINHA_DIREITA ? "right" : "left");
            txt(h, " valign-"); txt(h, c->alinha_v == ADC_ALINHA_MEIO ? "middle" : c->alinha_v == ADC_ALINHA_BASE ? "bottom" : "top");
            txt(h, "\"");
            if (c->colspan > 1) { txt(h, " colspan=\""); adc_buffer_int(h->b, c->colspan); txt(h, "\""); }
            if (c->rowspan > 1) { txt(h, " rowspan=\""); adc_buffer_int(h->b, c->rowspan); txt(h, "\""); }
            txt(h, ">");
            if (l->cabecalho) { inlines(h, c->inlines); for (const ADC_Bloco* p = c->filhos; p; p = p->proximo) inlines(h, p->inlines); }
            else if (c->estilo_celula == 'a') { txt(h, "<div class=\"content\">"); blocos(h, c->filhos); while (h->b->tamanho > 0 && h->b->dados[h->b->tamanho - 1] == '\n') h->b->tamanho--; txt(h, "</div>"); }
            else if (c->estilo_celula == 'l') { txt(h, "<div class=\"literal\"><pre>"); for (const ADC_Inline* i = c->inlines; i; i = i->proximo) if (i->tipo == ADC_IN_TEXTO) esc(h, i->valor); txt(h, "</pre></div>"); }
            else {
                if (c->filhos) { for (const ADC_Bloco* p = c->filhos; p; p = p->proximo) { txt(h, "<p class=\"tableblock\">"); inlines(h, p->inlines); txt(h, "</p>"); if (p->proximo) txt(h, "\n"); } }
                else if (c->inlines) { txt(h, "<p class=\"tableblock\">"); inlines(h, c->inlines); txt(h, "</p>"); }
            }
            txt(h, th ? "</th>\n" : "</td>\n");
        }
        txt(h, "</tr>\n");
    }
    if (em_thead) txt(h, "</thead>\n");
    if (em_tbody) txt(h, "</tbody>\n");
    if (em_tfoot) txt(h, "</tfoot>\n");
    txt(h, "</table>\n");
}

static void imagem_bloco(H* h, const ADC_Bloco* b) {
    bool a;
    ADC_Fatia alt = adc_atributo_obter(b->meta.atributos, "alt", &a);
    ADC_Fatia link = adc_atributo_obter(b->meta.atributos, "link", &a); bool tem_link = a;
    ADC_Fatia alinh = adc_atributo_obter(b->meta.atributos, "align", &a); bool tem_alinh = a;
    ADC_Fatia flo = adc_atributo_obter(b->meta.atributos, "float", &a); bool tem_float = a;
    txt(h, "<div");
    if (b->meta.id.tamanho) { txt(h, " id=\""); adc_html_escapar_atributo(h->b, b->meta.id); txt(h, "\""); }
    txt(h, " class=\"imageblock");
    if (tem_alinh) { txt(h, " text-"); esc(h, alinh); }
    if (tem_float) { txt(h, " "); esc(h, flo); }
    for (int32_t k = 0; k < b->meta.papeis.quantidade; k++) { txt(h, " "); esc(h, b->meta.papeis.itens[k]); }
    txt(h, "\">\n<div class=\"content\">\n");
    if (tem_link) { txt(h, "<a class=\"image\" href=\""); adc_html_escapar_atributo(h->b, link); txt(h, "\">"); }
    txt(h, "<img src=\"");
    { ADC_Fatia dir; if (adc_atribs_obter_cstr(&h->ctx->atribs, "imagesdir", &dir) && dir.tamanho && !adc_fatia_comeca(b->alvo, "http") && !adc_fatia_comeca(b->alvo, "/")) { adc_html_escapar_atributo(h->b, dir); txt(h, "/"); } }
    adc_html_escapar_atributo(h->b, b->alvo); txt(h, "\" alt=\""); adc_html_escapar_atributo(h->b, alt); txt(h, "\"");
    ADC_Fatia w = adc_atributo_obter(b->meta.atributos, "width", &a); if (a) { txt(h, " width=\""); esc(h, w); txt(h, "\""); }
    ADC_Fatia ht = adc_atributo_obter(b->meta.atributos, "height", &a); if (a) { txt(h, " height=\""); esc(h, ht); txt(h, "\""); }
    ADC_Fatia ti = adc_atributo_obter(b->meta.atributos, "title", &a); if (a && false) { txt(h, " title=\""); adc_html_escapar_atributo(h->b, ti); txt(h, "\""); }
    txt(h, ">");
    if (tem_link) txt(h, "</a>");
    txt(h, "\n</div>\n");
    if (b->titulo) { txt(h, "<div class=\"title\">"); adc_render_legenda(h->ctx, b, h->b, "figure-caption", &h->cont_figura); inlines(h, b->titulo); txt(h, "</div>\n"); }
    txt(h, "</div>\n");
}

static void toc(H* h);
static void toc_macro(H* h);

static void secao(H* h, const ADC_Bloco* b) {
    if (b->nivel <= 0) {
        /* nível 0 (parte): <h1 id class="sect0 papel">título</h1> e o conteúdo logo depois, sem div (Asciidoctor) */
        txt(h, "<h1");
        if (b->meta.id.tamanho) { txt(h, " id=\""); adc_html_escapar_atributo(h->b, b->meta.id); txt(h, "\""); }
        txt(h, " class=\"sect0");
        for (int32_t k = 0; k < b->meta.papeis.quantidade; k++) { txt(h, " "); esc(h, b->meta.papeis.itens[k]); }
        txt(h, "\">");
        if (h->sectanchors && b->meta.id.tamanho) { txt(h, "<a class=\"anchor\" href=\"#"); adc_html_escapar_atributo(h->b, b->meta.id); txt(h, "\"></a>"); }
        if (h->sectlinks && b->meta.id.tamanho) { txt(h, "<a class=\"link\" href=\"#"); adc_html_escapar_atributo(h->b, b->meta.id); txt(h, "\">"); }
        inlines(h, b->titulo);
        if (h->sectlinks && b->meta.id.tamanho) txt(h, "</a>");
        txt(h, "</h1>\n");
        blocos(h, b->filhos);
        return;
    }
    int32_t nivel = b->nivel < 1 ? 1 : b->nivel;
    if (nivel > 5) nivel = 5;
    if (nivel < 8) { h->contadores[nivel]++; for (int32_t k = nivel + 1; k < 8; k++) h->contadores[k] = 0; }
    char tag[8]; snprintf(tag, sizeof tag, "h%d", nivel + 1);
    txt(h, "<div class=\"sect"); adc_buffer_int(h->b, nivel);
    for (int32_t k = 0; k < b->meta.papeis.quantidade; k++) { txt(h, " "); esc(h, b->meta.papeis.itens[k]); }
    txt(h, "\">\n<"); txt(h, tag);
    if (b->meta.id.tamanho) { txt(h, " id=\""); adc_html_escapar_atributo(h->b, b->meta.id); txt(h, "\""); }
    txt(h, ">");
    /* Asciidoctor: <a class="anchor" href="#id"></a> antes do título e, com sectlinks, o título (com o número) vira link */
    bool com_ancora = h->sectanchors && b->meta.id.tamanho > 0;
    bool com_link = h->sectlinks && b->meta.id.tamanho > 0;
    if (com_ancora) { txt(h, "<a class=\"anchor\" href=\"#"); adc_html_escapar_atributo(h->b, b->meta.id); txt(h, "\"></a>"); }
    if (com_link) { txt(h, "<a class=\"link\" href=\"#"); adc_html_escapar_atributo(h->b, b->meta.id); txt(h, "\">"); }
    ADC_Fatia sectnumlevels = adc_atribs_valor_ou(&h->ctx->atribs, "sectnumlevels", "3");
    if (h->sectnums && nivel <= adc_fatia_para_int(sectnumlevels, 3) && !adc_fatia_igual(b->meta.estilo, "appendix") && !adc_fatia_igual(b->meta.estilo, "preface") && !adc_fatia_igual(b->meta.estilo, "bibliography") && !adc_fatia_igual(b->meta.estilo, "glossary") && !adc_fatia_igual(b->meta.estilo, "index") && !adc_fatia_igual(b->meta.estilo, "colophon") && !adc_fatia_igual(b->meta.estilo, "dedication") && !adc_fatia_igual(b->meta.estilo, "abstract")) { adc_render_numero_secao(h->b, h->contadores, nivel); txt(h, " "); }
    inlines(h, b->titulo);
    if (com_link) txt(h, "</a>");
    txt(h, "</"); txt(h, tag); txt(h, ">\n");
    if (nivel == 1) { txt(h, "<div class=\"sectionbody\">\n"); if (!b->filhos) txt(h, "\n"); blocos(h, b->filhos); txt(h, "</div>\n"); }
    else blocos(h, b->filhos);
    txt(h, "</div>\n");
}

static void bloco_conteudo(H* h, const ADC_Bloco* b);

/* Os ícones valem no ponto do documento em que o bloco nasceu (playback_attributes do Asciidoctor). */
static void bloco(H* h, const ADC_Bloco* b) {
    bool f = h->icons_font, im = h->icons_imagem;
    h->icons_font = b->icons_estado == 1; h->icons_imagem = b->icons_estado == 2;
    bloco_conteudo(h, b);
    h->icons_font = f; h->icons_imagem = im;
}

static void bloco_conteudo(H* h, const ADC_Bloco* b) {
    switch (b->tipo) {
        case ADC_BL_SECAO: secao(h, b); break;
        case ADC_BL_TITULO: {
            int32_t nivel = b->nivel < 1 ? 1 : b->nivel; if (nivel > 5) nivel = 5;
            char tag[8]; snprintf(tag, sizeof tag, "h%d", nivel + 1);
            txt(h, "<"); txt(h, tag);
            if (b->meta.id.tamanho) { txt(h, " id=\""); adc_html_escapar_atributo(h->b, b->meta.id); txt(h, "\""); }
            txt(h, " class=\"discrete"); for (int32_t k = 0; k < b->meta.papeis.quantidade; k++) { txt(h, " "); esc(h, b->meta.papeis.itens[k]); } txt(h, "\">");
            inlines(h, b->titulo); txt(h, "</"); txt(h, tag); txt(h, ">\n");
            break;
        }
        case ADC_BL_PARAGRAFO: paragrafo(h, b); break;
        case ADC_BL_LISTING: case ADC_BL_LITERAL: listing(h, b); break;
        case ADC_BL_PASS: adc_buffer_fatia(h->b, b->texto_cru); txt(h, "\n"); break;
        case ADC_BL_STEM: abrir_div(h, b, "stemblock"); titulo_div(h, b); txt(h, "<div class=\"content\">\n\\$"); esc(h, b->texto_cru); txt(h, "\\$\n</div>\n</div>\n"); break;
        case ADC_BL_VERSE: verse(h, b); break;
        case ADC_BL_ADMONICAO: admonicao(h, b); break;
        case ADC_BL_EXEMPLO:
            if (adc_lista_fatias_contem(&b->meta.opcoes, "collapsible")) {
                /* [%collapsible] (Asciidoctor convert_example): <details>, sem número de legenda; "Details" sem título */
                txt(h, "<details");
                if (b->meta.id.tamanho) { txt(h, " id=\""); adc_html_escapar_atributo(h->b, b->meta.id); txt(h, "\""); }
                if (b->meta.papeis.quantidade) { txt(h, " class=\""); for (int32_t k = 0; k < b->meta.papeis.quantidade; k++) { if (k) txt(h, " "); esc(h, b->meta.papeis.itens[k]); } txt(h, "\""); }
                if (adc_lista_fatias_contem(&b->meta.opcoes, "open")) txt(h, " open");
                txt(h, ">\n<summary class=\"title\">");
                if (b->titulo) inlines(h, b->titulo); else txt(h, "Details");
                txt(h, "</summary>\n<div class=\"content\">\n");
                blocos(h, b->filhos);
                txt(h, "</div>\n</details>\n");
            } else composto(h, b, "exampleblock", "example-caption", &h->cont_exemplo);
            break;
        case ADC_BL_SIDEBAR: composto(h, b, "sidebarblock", NULL, NULL); break;
        case ADC_BL_OPEN:
            if (adc_fatia_igual(b->meta.estilo, "abstract")) quote(h, b);
            else if (adc_fatia_igual(b->meta.estilo, "partintro")) composto(h, b, "openblock partintro", NULL, NULL);
            else composto(h, b, "openblock", NULL, NULL);
            break;
        case ADC_BL_QUOTE: quote(h, b); break;
        case ADC_BL_LISTA: lista(h, b); break;
        case ADC_BL_DLIST: dlist(h, b); break;
        case ADC_BL_TABELA: tabela(h, b); break;
        case ADC_BL_QUEBRA: txt(h, b->variante_quebra == ADC_QUEBRA_PAGE ? "<div style=\"page-break-after: always;\"></div>\n" : "<hr>\n"); break;
        case ADC_BL_MACRO:
            if (adc_fatia_igual(b->nome_macro, "image")) imagem_bloco(h, b);
            else if (adc_fatia_igual(b->nome_macro, "toc")) toc_macro(h);
            else if (adc_fatia_igual(b->nome_macro, "video")) {
                abrir_div(h, b, "videoblock"); titulo_div(h, b); txt(h, "<div class=\"content\">\n<video src=\""); adc_html_escapar_atributo(h->b, b->alvo); txt(h, "\"");
                bool a; ADC_Fatia w = adc_atributo_obter(b->meta.atributos, "width", &a); if (a) { txt(h, " width=\""); esc(h, w); txt(h, "\""); }
                ADC_Fatia ht = adc_atributo_obter(b->meta.atributos, "height", &a); if (a) { txt(h, " height=\""); esc(h, ht); txt(h, "\""); }
                ADC_Fatia po = adc_atributo_obter(b->meta.atributos, "poster", &a); if (a) { txt(h, " poster=\""); adc_html_escapar_atributo(h->b, po); txt(h, "\""); }
                txt(h, " controls>\nYour browser does not support the video tag.\n</video>\n</div>\n</div>\n");
            } else if (adc_fatia_igual(b->nome_macro, "audio")) {
                abrir_div(h, b, "audioblock"); titulo_div(h, b); txt(h, "<div class=\"content\">\n<audio src=\""); adc_html_escapar_atributo(h->b, b->alvo); txt(h, "\" controls>\nYour browser does not support the audio tag.\n</audio>\n</div>\n</div>\n");
            }
            break;
        default: break;
    }
}

static void blocos(H* h, const ADC_Bloco* pPrimeiro) {
    for (const ADC_Bloco* b = pPrimeiro; b; b = b->proximo) bloco(h, b);
}

/* --------------------------------------------------------------------- toc ---- */

static void toc_nivel(H* h, const ADC_Bloco* pPrimeiro, int32_t pNivelMax, int32_t* pContadores) {
    bool algum = false;
    for (const ADC_Bloco* b = pPrimeiro; b; b = b->proximo) if (b->tipo == ADC_BL_SECAO && b->nivel <= pNivelMax) { algum = true; break; }
    if (!algum) return;
    int32_t nivel = 0; for (const ADC_Bloco* b = pPrimeiro; b; b = b->proximo) if (b->tipo == ADC_BL_SECAO) { nivel = b->nivel; break; }
    txt(h, "<ul class=\"sectlevel"); adc_buffer_int(h->b, nivel); txt(h, "\">\n");
    for (const ADC_Bloco* b = pPrimeiro; b; b = b->proximo) {
        if (b->tipo != ADC_BL_SECAO || b->nivel > pNivelMax) continue;
        int32_t n = b->nivel < 1 ? 1 : b->nivel; if (n > 7) n = 7;
        pContadores[n]++; for (int32_t k = n + 1; k < 8; k++) pContadores[k] = 0;
        txt(h, "<li><a href=\"#"); adc_html_escapar_atributo(h->b, b->meta.id); txt(h, "\">");
        if (h->sectnums && n <= adc_fatia_para_int(adc_atribs_valor_ou(&h->ctx->atribs, "sectnumlevels", "3"), 3)) { adc_render_numero_secao(h->b, pContadores, n); txt(h, " "); }
        inlines(h, b->titulo); txt(h, "</a>");
        bool tem_sub = false; for (const ADC_Bloco* f = b->filhos; f; f = f->proximo) if (f->tipo == ADC_BL_SECAO && f->nivel <= pNivelMax) { tem_sub = true; break; }
        if (tem_sub) { txt(h, "\n"); toc_nivel(h, b->filhos, pNivelMax, pContadores); }
        txt(h, "</li>\n");
    }
    txt(h, "</ul>\n");
}

/* Posição lateral do sumário: ":toc: left|right" ou ":toc-position: left|right" (com :toc:). Devolve a fatia vazia se não houver. */
static ADC_Fatia toc_lado(const ADC_Contexto* pC) {
    ADC_Fatia vazia = { 0 };
    ADC_Fatia pos;
    if (!adc_render_toc(pC, &pos)) return vazia;
    if (adc_fatia_igual(pos, "left") || adc_fatia_igual(pos, "right")) return pos;
    if (adc_atribs_obter_cstr(&pC->atribs, "toc-position", &pos) && (adc_fatia_igual(pos, "left") || adc_fatia_igual(pos, "right"))) return pos;
    return vazia;
}

static void toc_corpo(H* h, bool pMacro);
static void toc(H* h) { toc_corpo(h, false); }

/* toc::[] (Asciidoctor convert_toc): só com :toc: macro (ou toc-placement=macro) e se o documento tem seções;
   senão, o comentário "toc disabled". O título do sumário via macro leva class="title". */
static void toc_macro(H* h) {
    const ADC_TabelaAtributos* at = &h->ctx->atribs;
    ADC_Fatia tv;
    bool macro = adc_render_toc(h->ctx, &tv) && (adc_fatia_igual(tv, "macro") || adc_fatia_igual(adc_atribs_valor_ou(at, "toc-placement", ""), "macro"));
    bool tem_secao = false;
    for (const ADC_Bloco* s = h->ctx->documento ? h->ctx->documento->filhos : NULL; s; s = s->proximo) if (s->tipo == ADC_BL_SECAO) { tem_secao = true; break; }
    if (!macro || !tem_secao) { txt(h, "<!-- toc disabled -->\n"); return; }
    toc_corpo(h, true);
}

static void toc_corpo(H* h, bool pMacro) {
    if (!h->ctx->documento) return;
    int32_t niveis = adc_fatia_para_int(h->ctx->toc_niveis.dados ? h->ctx->toc_niveis : adc_fatia_cstr("2"), 2);   /* do cabeçalho */
    /* Asciidoctor: a classe é "toc2" quando o sumário fica ao lado, só no documento completo (no fragmento fica "toc"); toc-class sobrepõe */
    ADC_Fatia classe = adc_atribs_valor_ou(&h->ctx->atribs, "toc-class", (h->autonomo && toc_lado(h->ctx).tamanho) ? "toc2" : "toc");
    txt(h, "<div id=\"toc\" class=\""); adc_html_escapar_atributo(h->b, classe); txt(h, pMacro ? "\">\n<div id=\"toctitle\" class=\"title\">" : "\">\n<div id=\"toctitle\">"); esc(h, h->ctx->toc_titulo.dados ? h->ctx->toc_titulo : adc_atribs_valor_ou(&h->ctx->atribs, "toc-title", "Table of Contents"));   /* do cabeçalho */ txt(h, "</div>\n");
    int32_t cont[8] = { 0 };
    toc_nivel(h, h->ctx->documento->filhos, niveis, cont);
    txt(h, "</div>\n");
}

/* ---------------------------------------------------------------- rodapé ---- */

static void notas(H* h) {
    if (!h->ctx->notas) return;
    txt(h, "<div id=\"footnotes\">\n<hr>\n");
    for (const ADC_NotaRodape* n = h->ctx->notas; n; n = n->proxima) {
        txt(h, "<div class=\"footnote\" id=\"_footnotedef_"); adc_buffer_int(h->b, n->numero); txt(h, "\">\n<a href=\"#_footnoteref_"); adc_buffer_int(h->b, n->numero); txt(h, "\">"); adc_buffer_int(h->b, n->numero); txt(h, "</a>. ");
        inlines(h, n->texto);
        txt(h, "\n</div>\n");
    }
    txt(h, "</div>\n");
}

/* ------------------------------------------------------- documento completo ---- */

/* Minúsculas ASCII e Latim-1 (À–Þ, exceto ×), escapadas: o Asciidoctor faz downcase do version-label nos detalhes. */
static void minusculas(H* h, ADC_Fatia t) {
    ADC_Buffer tmp; adc_buffer_iniciar(&tmp);
    for (int32_t i = 0; i < t.tamanho; i++) {
        unsigned char c = (unsigned char)t.dados[i];
        if (c >= 'A' && c <= 'Z') { adc_buffer_char(&tmp, (char)(c + 32)); continue; }
        if (c == 0xC3 && i + 1 < t.tamanho) {
            unsigned char d = (unsigned char)t.dados[i + 1];
            if (d >= 0x80 && d <= 0x9E && d != 0x97) { adc_buffer_char(&tmp, (char)c); adc_buffer_char(&tmp, (char)(d + 0x20)); i++; continue; }
        }
        adc_buffer_char(&tmp, (char)c);
    }
    esc(h, adc_buffer_fatia_de(&tmp));
    adc_buffer_liberar(&tmp);
}

static void meta_se(H* h, const char* pNome, const char* pAtributo) {
    ADC_Fatia v;
    if (adc_atribs_obter_cstr(&h->ctx->atribs, pAtributo, &v) && v.tamanho) { txt(h, "<meta name=\""); txt(h, pNome); txt(h, "\" content=\""); adc_html_escapar_atributo(h->b, v); txt(h, "\">\n"); }
}

/* <!DOCTYPE> até </head>, na ordem e com as regras do conversor html5 do Asciidoctor 2.0.x. */
static void cabecalho_html(H* h, ADC_Fatia pTitulo) {
    const ADC_TabelaAtributos* at = &h->ctx->atribs;
    ADC_Fatia v;
    txt(h, "<!DOCTYPE html>\n<html");
    if (!adc_atribs_definido(at, "nolang")) { txt(h, " lang=\""); adc_html_escapar_atributo(h->b, adc_atribs_valor_ou(at, "lang", "pt-BR")); txt(h, "\""); }
    txt(h, ">\n<head>\n<meta charset=\"UTF-8\">\n<meta http-equiv=\"X-UA-Compatible\" content=\"IE=edge\">\n<meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">\n<meta name=\"generator\" content=\"AsciiDocConverter\">\n");
    meta_se(h, "app-name", "app-name"); meta_se(h, "description", "description"); meta_se(h, "keywords", "keywords");
    meta_se(h, "author", "author"); meta_se(h, "copyright", "copyright");
    txt(h, "<title>"); esc(h, pTitulo); txt(h, "</title>\n");
    /* Fontes web do Google: o Asciidoctor as liga por padrão; aqui só quando o documento (ou as opções) define :webfonts:,
     * porque a biblioteca roda dentro de um SaaS e não dispara pedido externo sem ordem (ver docs/fidelidade.md). */
    if (adc_atribs_obter_cstr(at, "webfonts", &v)) {
        txt(h, "<link rel=\"stylesheet\" href=\"https://fonts.googleapis.com/css?family=");
        if (v.tamanho) adc_html_escapar_atributo(h->b, v);
        else txt(h, "Open+Sans:300,300italic,400,400italic,600,600italic%7CNoto+Serif:400,400italic,700,700italic%7CDroid+Sans+Mono:400,700");
        txt(h, "\">\n");
    }
    if (adc_atribs_definido(at, "linkcss")) {
        txt(h, "<link rel=\"stylesheet\" href=\""); adc_html_escapar_atributo(h->b, adc_atribs_valor_ou(at, "stylesdir", "."));
        txt(h, "/"); adc_html_escapar_atributo(h->b, adc_atribs_valor_ou(at, "stylesheet", "asciidoctor.css")); txt(h, "\">\n");
    } else {
        if (adc_atribs_obter_cstr(at, "stylesheet", &v) && v.tamanho)
            adc_msgs_adicionar(&h->ctx->msgs, ADC_NIVEL_AVISO, 0, 0, "HTML5: a folha de estilo personalizada '%.*s' (:stylesheet:) não foi lida, porque a biblioteca não acessa o disco; a folha padrão do Asciidoctor foi embutida no lugar. Para referenciá-la por URL, defina :linkcss:.", (int)v.tamanho, v.dados);
        txt(h, "<style>\n"); adc_buffer_bytes(h->b, adc_css_asciidoctor, adc_css_asciidoctor_tamanho); txt(h, "</style>\n");
    }
    if (h->icons_font) {
        txt(h, "<link rel=\"stylesheet\" href=\"");
        if (adc_atribs_obter_cstr(at, "iconfont-cdn", &v) && v.tamanho) adc_html_escapar_atributo(h->b, v);
        else if (adc_atribs_obter_cstr(at, "iconfont-name", &v) && v.tamanho) { adc_html_escapar_atributo(h->b, adc_atribs_valor_ou(at, "stylesdir", ".")); txt(h, "/"); adc_html_escapar_atributo(h->b, v); txt(h, ".css"); }
        else txt(h, "https://cdnjs.cloudflare.com/ajax/libs/font-awesome/4.7.0/css/font-awesome.min.css");
        txt(h, "\">\n");
    }
    if (adc_atribs_definido(at, "stem"))
        adc_msgs_adicionar(&h->ctx->msgs, ADC_NIVEL_AVISO, 0, 0, "HTML5: :stem: está definido, mas esta versão não injeta o MathJax no <head>; as expressões saem como texto.");
    if (adc_atribs_obter_cstr(at, "source-highlighter", &v) && v.tamanho)
        adc_msgs_adicionar(&h->ctx->msgs, ADC_NIVEL_AVISO, 0, 0, "HTML5: :source-highlighter: %.*s não é suportado nesta versão; o código sai sem realce.", (int)v.tamanho, v.dados);
    if (adc_atribs_definido(at, "docinfo"))
        adc_msgs_adicionar(&h->ctx->msgs, ADC_NIVEL_AVISO, 0, 0, "HTML5: :docinfo: não é suportado (exigiria ler docinfo*.html do disco, e a biblioteca não acessa o disco); o cabeçalho e o rodapé saíram sem o conteúdo extra.");
    txt(h, "</head>\n");
}

void adc_html5_escrever(ADC_Contexto* pC, ADC_Buffer* pSaida) {
    H h; memset(&h, 0, sizeof h);
    h.ctx = pC; h.b = pSaida;
    h.sectnums = adc_atribs_definido(&pC->atribs, "sectnums") || adc_atribs_definido(&pC->atribs, "numbered");
    /* <head> e o que não está em bloco usam :icons: do fim do cabeçalho (o Asciidoctor restaura os atributos do
       cabeçalho antes de converter); cada bloco usa o do seu ponto (ver bloco()) */
    h.icons_font = pC->icons_cabecalho == 1;
    h.icons_imagem = pC->icons_cabecalho == 2;
    const ADC_Bloco* doc = pC->documento;
    bool autonomo = pC->opcoes.autonomo;
    h.autonomo = autonomo;
    h.sectanchors = adc_atribs_definido(&pC->atribs, "sectanchors");
    h.sectlinks = adc_atribs_definido(&pC->atribs, "sectlinks");
    /* título do documento: o cabeçalho; sem ele, a primeira seção (como o Asciidoctor); sem seção, untitled-label */
    ADC_Fatia titulo_puro = adc_atribs_valor_ou(&pC->atribs, "untitled-label", "Untitled");
    if (pC->cabecalho.titulo) titulo_puro = adc_inlines_texto_puro(pC->arena, pC->cabecalho.titulo);
    else for (const ADC_Bloco* s = doc ? doc->filhos : NULL; s; s = s->proximo) if (s->tipo == ADC_BL_SECAO && s->titulo) { titulo_puro = adc_inlines_texto_puro(pC->arena, s->titulo); break; }
    ADC_Fatia toc_pos;
    bool tem_toc = adc_render_toc(pC, &toc_pos);
    bool toc_auto = tem_toc && (toc_pos.tamanho == 0 || adc_fatia_igual(toc_pos, "auto") || adc_fatia_igual(toc_pos, "left") || adc_fatia_igual(toc_pos, "right"));
    bool toc_preamble = tem_toc && adc_fatia_igual(toc_pos, "preamble");
    if (autonomo) {
        cabecalho_html(&h, titulo_puro);
        txt(&h, "<body");
        if (doc && doc->meta.id.tamanho) { txt(&h, " id=\""); adc_html_escapar_atributo(h.b, doc->meta.id); txt(&h, "\""); }
        txt(&h, " class=\"article");
        { ADC_Fatia lado = toc_lado(pC); if (lado.tamanho) { txt(&h, " toc2 toc-"); esc(&h, lado); } }
        txt(&h, "\">\n");
        if (!adc_atribs_definido(&pC->atribs, "noheader")) {
            txt(&h, "<div id=\"header\">\n");
            if (pC->cabecalho.presente) {
                if (!adc_atribs_definido(&pC->atribs, "notitle")) { txt(&h, "<h1>"); inlines(&h, pC->cabecalho.titulo); txt(&h, "</h1>\n"); }
                if (pC->cabecalho.autores || pC->cabecalho.revisao_numero.tamanho || pC->cabecalho.revisao_data.tamanho) {
                    txt(&h, "<div class=\"details\">\n");
                    int32_t idx = 1;
                    for (const ADC_Autor* a = pC->cabecalho.autores; a; a = a->proximo, idx++) {
                        txt(&h, "<span id=\"author"); if (idx > 1) adc_buffer_int(h.b, idx); txt(&h, "\" class=\"author\">"); esc(&h, a->nome_completo); txt(&h, "</span><br>\n");
                        if (a->email.tamanho) { txt(&h, "<span id=\"email"); if (idx > 1) adc_buffer_int(h.b, idx); txt(&h, "\" class=\"email\"><a href=\"mailto:"); adc_html_escapar_atributo(h.b, a->email); txt(&h, "\">"); esc(&h, a->email); txt(&h, "</a></span><br>\n"); }
                    }
                    /* Asciidoctor escreve o version-label em minúsculas nos detalhes ("versão 1.0,") e como está no rodapé */
                    if (pC->cabecalho.revisao_numero.tamanho) { txt(&h, "<span id=\"revnumber\">"); minusculas(&h, adc_atribs_valor_ou(&pC->atribs, "version-label", "Version")); txt(&h, " "); esc(&h, pC->cabecalho.revisao_numero); txt(&h, pC->cabecalho.revisao_data.tamanho ? ",</span>\n" : "</span>\n"); }
                    if (pC->cabecalho.revisao_data.tamanho) { txt(&h, "<span id=\"revdate\">"); esc(&h, pC->cabecalho.revisao_data); txt(&h, "</span>\n"); }
                    if (pC->cabecalho.revisao_observacao.tamanho) { txt(&h, "<br><span id=\"revremark\">"); esc(&h, pC->cabecalho.revisao_observacao); txt(&h, "</span>\n"); }
                    txt(&h, "</div>\n");
                }
            }
            if (toc_auto) toc(&h);
            txt(&h, "</div>\n");
        }
        txt(&h, "<div id=\"content\">\n");
    } else {
        if (pC->cabecalho.presente && adc_atribs_definido(&pC->atribs, "showtitle")) { txt(&h, "<h1>"); inlines(&h, pC->cabecalho.titulo); txt(&h, "</h1>\n"); }
        if (toc_auto) toc(&h);
    }
    /* preâmbulo: blocos antes da primeira seção quando há seções */
    bool tem_secao = false; for (const ADC_Bloco* b = doc ? doc->filhos : NULL; b; b = b->proximo) if (b->tipo == ADC_BL_SECAO) { tem_secao = true; break; }
    const ADC_Bloco* b = doc ? doc->filhos : NULL;
    if (tem_secao && b && b->tipo != ADC_BL_SECAO && pC->cabecalho.presente) {
        txt(&h, "<div id=\"preamble\">\n<div class=\"sectionbody\">\n");
        while (b && b->tipo != ADC_BL_SECAO) { bloco(&h, b); b = b->proximo; }
        if (toc_preamble) toc(&h);
        txt(&h, "</div>\n</div>\n");
    }
    for (; b; b = b->proximo) bloco(&h, b);
    if (autonomo) txt(&h, "</div>\n");   /* as notas de rodapé ficam fora de #content, como no Asciidoctor */
    notas(&h);
    if (autonomo) {
        if (!adc_atribs_definido(&pC->atribs, "nofooter")) {
            txt(&h, "<div id=\"footer\">\n<div id=\"footer-text\">\n");
            if (pC->cabecalho.revisao_numero.tamanho) { esc(&h, adc_atribs_valor_ou(&pC->atribs, "version-label", "Version")); txt(&h, " "); esc(&h, pC->cabecalho.revisao_numero); txt(&h, "<br>\n"); }
            /* A biblioteca não tem relógio nem lê o disco: a data só sai se o chamador definir docdatetime (e sem :reproducible:) */
            ADC_Fatia quando;
            if (!adc_atribs_definido(&pC->atribs, "reproducible") && adc_atribs_obter_cstr(&pC->atribs, "docdatetime", &quando) && quando.tamanho) {
                esc(&h, adc_atribs_valor_ou(&pC->atribs, "last-update-label", "Last updated")); txt(&h, " "); esc(&h, quando); txt(&h, "\n");
            }
            txt(&h, "</div>\n</div>\n");
        }
        txt(&h, "</body>\n</html>\n");
    }
}
