/*
 * DocBook 5 direto da AST (IC-1), na estrutura do conversor docbook5 do Asciidoctor 2.0.x: <article> (ou <book>
 * com :doctype: book) no namespace DocBook, <info> com título, autores e revisão, seções aninhadas com xml:id,
 * <simpara>/<formalpara>, listas, <variablelist>, admonições, <example>/<sidebar>/<blockquote>, tabelas CALS,
 * <figure>/<mediaobject>, notas de rodapé, callouts (<co>/<calloutlist>) e os PIs do Asciidoctor para o que o DocBook
 * não tem (<?asciidoc-hr?>, <?asciidoc-pagebreak?>, <?asciidoc-toc?>, <?asciidoc-br?>).
 *
 * Alcançável só pela entrada WebAssembly (DAD-9): a ABI nativa segue devolvendo ADC_ERRO_FORMATO.
 */
#include "../adc_render.h"

typedef struct D {
    ADC_Contexto* ctx;
    ADC_Buffer*   b;
    int32_t       listing_corrente;     /* numeração dos <co xml:id="CO<listing>-<n>"/>              */
    int32_t       listing_do_callout;   /* o listing a que a próxima <calloutlist> se refere           */
    int32_t       profundidade_secao;
} D;

static void inlines(D* d, const ADC_Inline* pI);
static void blocos(D* d, const ADC_Bloco* pPrimeiro);

void adc_xml_escapar(ADC_Buffer* pB, ADC_Fatia pT) {
    for (int32_t i = 0; i < pT.tamanho; i++) {
        char c = pT.dados[i];
        if (c == '&') adc_buffer_texto(pB, "&amp;");
        else if (c == '<') adc_buffer_texto(pB, "&lt;");
        else if (c == '>') adc_buffer_texto(pB, "&gt;");
        else if (c == '"') adc_buffer_texto(pB, "&quot;");
        else adc_buffer_char(pB, c);
    }
}

static void esc(D* d, ADC_Fatia t) { adc_xml_escapar(d->b, t); }
static void txt(D* d, const char* t) { adc_buffer_texto(d->b, t); }

/* xml:id e role (papéis) de um bloco. */
static void id_e_papeis(D* d, const ADC_Bloco* b) {
    if (b->meta.id.tamanho) { txt(d, " xml:id=\""); esc(d, b->meta.id); txt(d, "\""); }
    if (b->meta.papeis.quantidade) {
        txt(d, " role=\"");
        for (int32_t k = 0; k < b->meta.papeis.quantidade; k++) { if (k) txt(d, " "); esc(d, b->meta.papeis.itens[k]); }
        txt(d, "\"");
    }
}

static void titulo_se(D* d, const ADC_Bloco* b) {
    if (!b->titulo) return;
    txt(d, "<title>"); inlines(d, b->titulo); txt(d, "</title>\n");
}

/* Entidade nomeada → referência numérica (XML não conhece &nbsp; e companhia). */
static void charref(D* d, ADC_Fatia v) {
    static const struct { const char* nome; const char* numero; } MAPA[] = {
        { "nbsp", "160" }, { "zwsp", "8203" }, { "wj", "8288" }, { "deg", "176" }, { "plus", "43" }, { "brvbar", "166" },
        { "vbar", "124" }, { "lsquo", "8216" }, { "rsquo", "8217" }, { "ldquo", "8220" }, { "rdquo", "8221" },
        { "hellip", "8230" }, { "mdash", "8212" }, { "ndash", "8211" }, { "laquo", "171" }, { "raquo", "187" },
        { "copy", "169" }, { "reg", "174" }, { "trade", "8482" }, { "apos", "39" }, { "quot", "34" }, { "amp", "38" },
        { "lt", "60" }, { "gt", "62" }, { "times", "215" }, { "divide", "247" }, { "euro", "8364" }, { "pound", "163" },
        { "yen", "165" }, { "cent", "162" }, { "sect", "167" }, { "para", "182" }, { "middot", "183" }, { "bull", "8226" },
        { "larr", "8592" }, { "rarr", "8594" }, { "uarr", "8593" }, { "darr", "8595" }, { "harr", "8596" }, { "lArr", "8656" }, { "rArr", "8658" },
    };
    if (v.tamanho > 0 && v.dados[0] == '#') { txt(d, "&"); adc_buffer_fatia(d->b, v); txt(d, ";"); return; }
    for (size_t i = 0; i < sizeof MAPA / sizeof MAPA[0]; i++)
        if (adc_fatia_igual(v, MAPA[i].nome)) { txt(d, "&#"); txt(d, MAPA[i].numero); txt(d, ";"); return; }
    /* desconhecida: sai como texto, nunca como entidade inválida */
    txt(d, "&amp;"); esc(d, v); txt(d, ";");
}

static void imagedata(D* d, ADC_Fatia pAlvo, const ADC_Atributo* pAtributos, bool pBloco) {
    bool a;
    txt(d, "<imagedata fileref=\"");
    { ADC_Fatia dir; if (adc_atribs_obter_cstr(&d->ctx->atribs, "imagesdir", &dir) && dir.tamanho && !adc_fatia_comeca(pAlvo, "http") && !adc_fatia_comeca(pAlvo, "/")) { esc(d, dir); txt(d, "/"); } }
    esc(d, pAlvo); txt(d, "\"");
    ADC_Fatia w = adc_atributo_obter(pAtributos, pBloco ? "scaledwidth" : "width", &a);
    if (a && pBloco) { txt(d, " width=\""); esc(d, w); txt(d, "\""); }
    w = adc_atributo_obter(pAtributos, "width", &a);
    if (a) { txt(d, " contentwidth=\""); esc(d, w); if (adc_fatia_indice(w, '%') < 0 && w.tamanho && adc_eh_digito(w.dados[w.tamanho - 1])) txt(d, "px"); txt(d, "\""); }
    ADC_Fatia h = adc_atributo_obter(pAtributos, "height", &a);
    if (a) { txt(d, " contentdepth=\""); esc(d, h); if (adc_fatia_indice(h, '%') < 0 && h.tamanho && adc_eh_digito(h.dados[h.tamanho - 1])) txt(d, "px"); txt(d, "\""); }
    ADC_Fatia al = adc_atributo_obter(pAtributos, "align", &a);
    if (a) { txt(d, " align=\""); esc(d, al); txt(d, "\""); }
    txt(d, "/>");
}

/* ------------------------------------------------------------------ inlines ---- */

static void inline_um(D* d, const ADC_Inline* i) {
    bool a;
    switch (i->tipo) {
        case ADC_IN_TEXTO: esc(d, i->valor); break;
        case ADC_IN_CHARREF:
            if (adc_fatia_igual(i->valor, "callout")) { txt(d, "<co xml:id=\"CO"); adc_buffer_int(d->b, d->listing_corrente); txt(d, "-"); adc_buffer_int(d->b, i->numero); txt(d, "\"/>"); }
            else charref(d, i->valor);
            break;
        case ADC_IN_RAW:
            if (i->valor.dados == NULL && i->filhos) inlines(d, i->filhos);
            else if (adc_fatia_igual(i->alvo, "latexmath") || adc_fatia_igual(i->alvo, "stem") || adc_fatia_igual(i->alvo, "asciimath")) { txt(d, "<inlineequation><mathphrase>"); esc(d, i->valor); txt(d, "</mathphrase></inlineequation>"); }
            else adc_buffer_fatia(d->b, i->valor);
            break;
        case ADC_IN_SPAN: {
            const char* tag = i->variante_span == ADC_SPAN_CODE ? "literal" : i->variante_span == ADC_SPAN_SUPERSCRIPT ? "superscript" : i->variante_span == ADC_SPAN_SUBSCRIPT ? "subscript" : "emphasis";
            txt(d, "<"); txt(d, tag);
            if (i->id.tamanho) { txt(d, " xml:id=\""); esc(d, i->id); txt(d, "\""); }
            if (i->variante_span == ADC_SPAN_STRONG || i->variante_span == ADC_SPAN_MARK || i->papeis.quantidade) {
                txt(d, " role=\"");
                bool algo = false;
                if (i->variante_span == ADC_SPAN_STRONG) { txt(d, "strong"); algo = true; }
                else if (i->variante_span == ADC_SPAN_MARK) { txt(d, "marked"); algo = true; }
                for (int32_t k = 0; k < i->papeis.quantidade; k++) { if (algo) txt(d, " "); esc(d, i->papeis.itens[k]); algo = true; }
                txt(d, "\"");
            }
            txt(d, ">"); inlines(d, i->filhos); txt(d, "</"); txt(d, tag); txt(d, ">");
            break;
        }
        case ADC_IN_REF:
            if (i->variante_ref == ADC_REF_LINK) {
                txt(d, "<link xl:href=\""); esc(d, i->alvo); txt(d, "\"");
                { ADC_Fatia role = adc_atributo_obter(i->atributos, "role", &a); if (a) { txt(d, " role=\""); esc(d, role); txt(d, "\""); } }
                txt(d, ">");
                if (i->filhos) inlines(d, i->filhos); else esc(d, i->alvo);
                txt(d, "</link>");
            } else {
                ADC_Fatia alvo = i->alvo;
                int32_t hash = adc_fatia_indice(alvo, '#');
                if (hash > 0 || (hash < 0 && adc_fatia_termina(alvo, ".adoc"))) {
                    /* outro documento: xref:arquivo.adoc#id[] → link para arquivo.xml#id */
                    ADC_Fatia arq = hash >= 0 ? adc_fatia(alvo.dados, hash) : alvo;
                    txt(d, "<link xl:href=\"");
                    if (adc_fatia_termina(arq, ".adoc")) { esc(d, adc_fatia(arq.dados, arq.tamanho - 5)); txt(d, ".xml"); } else esc(d, arq);
                    if (hash >= 0) { txt(d, "#"); esc(d, adc_fatia(alvo.dados + hash + 1, alvo.tamanho - hash - 1)); }
                    txt(d, "\">");
                    if (i->filhos) inlines(d, i->filhos); else esc(d, alvo);
                    txt(d, "</link>");
                } else {
                    ADC_Fatia id = hash == 0 ? adc_fatia(alvo.dados + 1, alvo.tamanho - 1) : alvo;
                    if (i->filhos) { txt(d, "<link linkend=\""); esc(d, id); txt(d, "\">"); inlines(d, i->filhos); txt(d, "</link>"); }
                    else { txt(d, "<xref linkend=\""); esc(d, id); txt(d, "\"/>"); }
                }
            }
            break;
        case ADC_IN_IMAGEM: {
            ADC_Fatia alt = adc_atributo_obter(i->atributos, "alt", &a);
            txt(d, "<inlinemediaobject>\n<imageobject>\n"); imagedata(d, i->alvo, i->atributos, false); txt(d, "\n</imageobject>\n<textobject><phrase>"); esc(d, alt); txt(d, "</phrase></textobject>\n</inlinemediaobject>");
            break;
        }
        case ADC_IN_NOTA:
            if (i->numero > 0) {
                if (adc_fatia_igual(i->valor, "ref")) { txt(d, "<footnoteref linkend=\"_footnotedef_"); adc_buffer_int(d->b, i->numero); txt(d, "\"/>"); }
                else {
                    txt(d, "<footnote xml:id=\"_footnotedef_"); adc_buffer_int(d->b, i->numero); txt(d, "\"><simpara>");
                    const ADC_NotaRodape* n = NULL;
                    for (const ADC_NotaRodape* k = d->ctx->notas; k; k = k->proxima) if (k->numero == i->numero) { n = k; break; }
                    inlines(d, n ? n->texto : i->filhos);
                    txt(d, "</simpara></footnote>");
                }
            } else inlines(d, i->filhos);
            break;
        case ADC_IN_QUEBRA_LINHA: txt(d, "<?asciidoc-br?>"); if (i->proximo) txt(d, "\n"); break;
        case ADC_IN_ANCORA:
            txt(d, "<anchor xml:id=\""); esc(d, i->id); txt(d, "\"");
            if (i->filhos) { txt(d, " xreflabel=\""); ADC_Fatia t = adc_inlines_texto_puro(d->ctx->arena, i->filhos); esc(d, t); txt(d, "\""); }
            txt(d, "/>");
            break;
        case ADC_IN_TECLA: {
            int32_t n = 0; for (const ADC_Inline* k = i->filhos; k; k = k->proximo) n++;
            if (n > 1) txt(d, "<keycombo>");
            for (const ADC_Inline* k = i->filhos; k; k = k->proximo) { txt(d, "<keycap>"); esc(d, k->valor); txt(d, "</keycap>"); }
            if (n > 1) txt(d, "</keycombo>");
            break;
        }
        case ADC_IN_BOTAO: txt(d, "<guibutton>"); inlines(d, i->filhos); txt(d, "</guibutton>"); break;
        case ADC_IN_MENU: {
            int32_t n = 0; for (const ADC_Inline* k = i->filhos; k; k = k->proximo) n++;
            if (n <= 1) { txt(d, "<guimenu>"); if (i->filhos) esc(d, i->filhos->valor); txt(d, "</guimenu>"); break; }
            txt(d, "<menuchoice><guimenu>"); esc(d, i->filhos->valor); txt(d, "</guimenu>");
            for (const ADC_Inline* k = i->filhos->proximo; k; k = k->proximo) { txt(d, k->proximo ? "<guisubmenu>" : "<guimenuitem>"); esc(d, k->valor); txt(d, k->proximo ? "</guisubmenu>" : "</guimenuitem>"); }
            txt(d, "</menuchoice>");
            break;
        }
        case ADC_IN_ICONE: {
            ADC_Fatia alt = adc_atributo_obter(i->atributos, "alt", &a);
            txt(d, "<inlinemediaobject>\n<imageobject>\n<imagedata fileref=\""); esc(d, adc_atribs_valor_ou(&d->ctx->atribs, "iconsdir", "./images/icons")); txt(d, "/"); esc(d, i->alvo); txt(d, "."); esc(d, adc_atribs_valor_ou(&d->ctx->atribs, "icontype", "png"));
            txt(d, "\"/>\n</imageobject>\n<textobject><phrase>["); if (a) esc(d, alt); else esc(d, i->alvo); txt(d, "]</phrase></textobject>\n</inlinemediaobject>");
            break;
        }
        case ADC_IN_INDICE:
            if (i->filhos) {
                ADC_Fatia t = adc_inlines_texto_puro(d->ctx->arena, i->filhos);
                txt(d, "<indexterm>\n<primary>"); esc(d, t); txt(d, "</primary>\n</indexterm>");
                inlines(d, i->filhos);
            }
            break;
        default: break;
    }
}

static void inlines(D* d, const ADC_Inline* pI) {
    for (const ADC_Inline* i = pI; i; i = i->proximo) inline_um(d, i);
}

/* ------------------------------------------------------------------- blocos ---- */

static void paragrafo(D* d, const ADC_Bloco* b) {
    if (b->titulo) {
        txt(d, "<formalpara"); id_e_papeis(d, b); txt(d, ">\n<title>"); inlines(d, b->titulo); txt(d, "</title>\n<para>"); inlines(d, b->inlines); txt(d, "</para>\n</formalpara>\n");
        return;
    }
    txt(d, "<simpara"); id_e_papeis(d, b); txt(d, ">"); inlines(d, b->inlines); txt(d, "</simpara>\n");
}

static void verbatim_conteudo(D* d, const ADC_Inline* pI) {
    for (const ADC_Inline* i = pI; i; i = i->proximo) {
        if (i->tipo == ADC_IN_TEXTO) esc(d, i->valor);
        else if (i->tipo == ADC_IN_RAW) adc_buffer_fatia(d->b, i->valor);
        else inline_um(d, i);
    }
}

static void listing(D* d, const ADC_Bloco* b) {
    bool source = b->tipo == ADC_BL_LISTING && (adc_fatia_igual(b->meta.estilo, "source") || b->linguagem.tamanho > 0);
    bool com_callout = false;
    for (const ADC_Inline* i = b->inlines; i; i = i->proximo) if (i->tipo == ADC_IN_CHARREF && adc_fatia_igual(i->valor, "callout")) { com_callout = true; break; }
    if (com_callout) { d->listing_corrente++; d->listing_do_callout = d->listing_corrente; }
    if (b->titulo) { txt(d, "<formalpara"); id_e_papeis(d, b); txt(d, ">\n<title>"); inlines(d, b->titulo); txt(d, "</title>\n<para>\n"); }
    if (b->tipo == ADC_BL_LITERAL) {
        txt(d, "<literallayout"); if (!b->titulo) id_e_papeis(d, b); txt(d, " class=\"monospaced\">"); verbatim_conteudo(d, b->inlines); txt(d, "</literallayout>\n");
    } else if (source) {
        txt(d, "<programlisting"); if (!b->titulo) id_e_papeis(d, b);
        if (b->linguagem.tamanho) { txt(d, " language=\""); esc(d, b->linguagem); txt(d, "\""); }
        txt(d, " linenumbering=\""); txt(d, adc_lista_fatias_contem(&b->meta.opcoes, "linenums") ? "numbered" : "unnumbered"); txt(d, "\">");
        verbatim_conteudo(d, b->inlines); txt(d, "</programlisting>\n");
    } else {
        txt(d, "<screen"); if (!b->titulo) id_e_papeis(d, b); txt(d, ">"); verbatim_conteudo(d, b->inlines); txt(d, "</screen>\n");
    }
    if (b->titulo) txt(d, "</para>\n</formalpara>\n");
}

static void admonicao(D* d, const ADC_Bloco* b) {
    ADC_Fatia v = adc_variante_adm_nome(b->variante_adm);
    txt(d, "<"); adc_buffer_fatia(d->b, v); id_e_papeis(d, b); txt(d, ">\n");
    titulo_se(d, b);
    blocos(d, b->filhos);
    txt(d, "</"); adc_buffer_fatia(d->b, v); txt(d, ">\n");
}

static void atribuicao(D* d, const ADC_Bloco* b) {
    if (!b->atribuicao.tamanho && !b->citacao.tamanho) return;
    txt(d, "<attribution>\n");
    if (b->atribuicao.tamanho) { esc(d, b->atribuicao); txt(d, "\n"); }
    if (b->citacao.tamanho) { txt(d, "<citetitle>"); esc(d, b->citacao); txt(d, "</citetitle>\n"); }
    txt(d, "</attribution>\n");
}

static void quote(D* d, const ADC_Bloco* b) {
    txt(d, "<blockquote"); id_e_papeis(d, b); txt(d, ">\n");
    titulo_se(d, b);
    atribuicao(d, b);
    blocos(d, b->filhos);
    txt(d, "</blockquote>\n");
}

static void verse(D* d, const ADC_Bloco* b) {
    txt(d, "<blockquote"); id_e_papeis(d, b); txt(d, ">\n");
    titulo_se(d, b);
    atribuicao(d, b);
    txt(d, "<literallayout>"); inlines(d, b->inlines); txt(d, "</literallayout>\n</blockquote>\n");
}

static const char* numeracao(const ADC_Bloco* b) {
    ADC_Fatia m = b->marcador;
    if (adc_fatia_igual(b->meta.estilo, "loweralpha")) return "loweralpha";
    if (adc_fatia_igual(b->meta.estilo, "upperalpha")) return "upperalpha";
    if (adc_fatia_igual(b->meta.estilo, "lowerroman")) return "lowerroman";
    if (adc_fatia_igual(b->meta.estilo, "upperroman")) return "upperroman";
    if (m.tamanho >= 2 && adc_eh_alfa(m.dados[0])) {
        if (m.dados[m.tamanho - 1] == ')') return (m.dados[0] >= 'A' && m.dados[0] <= 'Z') ? "upperroman" : "lowerroman";
        return (m.dados[0] >= 'A' && m.dados[0] <= 'Z') ? "upperalpha" : "loweralpha";
    }
    if (m.tamanho >= 1 && m.dados[0] == '.') { switch (m.tamanho) { case 1: return "arabic"; case 2: return "loweralpha"; case 3: return "lowerroman"; case 4: return "upperalpha"; default: return "upperroman"; } }
    return "arabic";
}

static void item_corpo(D* d, const ADC_Inline* pPrincipal, const ADC_Bloco* pFilhos) {
    if (pPrincipal) { txt(d, "<simpara>"); inlines(d, pPrincipal); txt(d, "</simpara>\n"); }
    blocos(d, pFilhos);
}

static void lista(D* d, const ADC_Bloco* b) {
    if (b->variante_lista == ADC_LISTA_CALLOUT) {
        txt(d, "<calloutlist"); id_e_papeis(d, b); txt(d, ">\n");
        titulo_se(d, b);
        for (const ADC_Bloco* it = b->filhos; it; it = it->proximo) {
            txt(d, "<callout arearefs=\"CO"); adc_buffer_int(d->b, d->listing_do_callout); txt(d, "-"); adc_buffer_int(d->b, it->numero); txt(d, "\">\n");
            if (it->principal) { txt(d, "<para>"); inlines(d, it->principal); txt(d, "</para>\n"); }
            blocos(d, it->filhos);
            txt(d, "</callout>\n");
        }
        txt(d, "</calloutlist>\n");
        return;
    }
    if (b->variante_lista == ADC_LISTA_ORDERED) {
        txt(d, "<orderedlist"); id_e_papeis(d, b); txt(d, " numeration=\""); txt(d, numeracao(b)); txt(d, "\"");
        if (b->numero > 1) { txt(d, " startingnumber=\""); adc_buffer_int(d->b, b->numero); txt(d, "\""); }
        if (adc_lista_fatias_contem(&b->meta.opcoes, "reversed")) txt(d, " inheritnum=\"ignore\" continuation=\"restarts\"");
        txt(d, ">\n");
        titulo_se(d, b);
        for (const ADC_Bloco* it = b->filhos; it; it = it->proximo) { txt(d, "<listitem>\n"); item_corpo(d, it->principal, it->filhos); txt(d, "</listitem>\n"); }
        txt(d, "</orderedlist>\n");
        return;
    }
    bool checklist = adc_fatia_igual(b->meta.estilo, "checklist") || (b->filhos && b->filhos->principal && b->filhos->principal->tipo == ADC_IN_TEXTO && (adc_fatia_comeca(b->filhos->principal->valor, "[ ] ") || adc_fatia_comeca(b->filhos->principal->valor, "[x] ") || adc_fatia_comeca(b->filhos->principal->valor, "[*] ")));
    txt(d, "<itemizedlist");
    if (b->meta.id.tamanho) { txt(d, " xml:id=\""); esc(d, b->meta.id); txt(d, "\""); }
    if (checklist || b->meta.papeis.quantidade || (b->meta.estilo.tamanho && !adc_fatia_igual(b->meta.estilo, "checklist"))) {
        txt(d, " role=\"");
        bool algo = false;
        if (checklist) { txt(d, "checklist"); algo = true; }
        else if (b->meta.estilo.tamanho) { esc(d, b->meta.estilo); algo = true; }
        for (int32_t k = 0; k < b->meta.papeis.quantidade; k++) { if (algo) txt(d, " "); esc(d, b->meta.papeis.itens[k]); algo = true; }
        txt(d, "\"");
    }
    txt(d, ">\n");
    titulo_se(d, b);
    for (const ADC_Bloco* it = b->filhos; it; it = it->proximo) {
        txt(d, "<listitem>\n");
        if (checklist && it->principal && it->principal->tipo == ADC_IN_TEXTO && it->principal->valor.tamanho >= 4 && it->principal->valor.dados[0] == '[' && it->principal->valor.dados[2] == ']' && it->principal->valor.dados[3] == ' ') {
            bool marcado = it->principal->valor.dados[1] != ' ';
            txt(d, "<simpara>"); txt(d, marcado ? "&#10003; " : "&#10063; ");
            ADC_Inline copia = *it->principal; copia.valor = adc_fatia(copia.valor.dados + 4, copia.valor.tamanho - 4); copia.proximo = NULL;
            inline_um(d, &copia); inlines(d, it->principal->proximo);
            txt(d, "</simpara>\n");
            blocos(d, it->filhos);
        } else item_corpo(d, it->principal, it->filhos);
        txt(d, "</listitem>\n");
    }
    txt(d, "</itemizedlist>\n");
}

static void dlist(D* d, const ADC_Bloco* b) {
    if (adc_fatia_igual(b->meta.estilo, "qanda")) {
        txt(d, "<qandaset"); id_e_papeis(d, b); txt(d, ">\n");
        titulo_se(d, b);
        for (const ADC_Bloco* it = b->filhos; it; it = it->proximo) {
            txt(d, "<qandaentry>\n<question>\n");
            for (int32_t k = 0; k < it->quantidade_termos; k++) { txt(d, "<simpara>"); inlines(d, it->termos[k]); txt(d, "</simpara>\n"); }
            txt(d, "</question>\n");
            if (it->principal || it->filhos) { txt(d, "<answer>\n"); item_corpo(d, it->principal, it->filhos); txt(d, "</answer>\n"); }
            txt(d, "</qandaentry>\n");
        }
        txt(d, "</qandaset>\n");
        return;
    }
    txt(d, "<variablelist");
    if (b->meta.id.tamanho) { txt(d, " xml:id=\""); esc(d, b->meta.id); txt(d, "\""); }
    if (b->meta.estilo.tamanho || b->meta.papeis.quantidade) {
        txt(d, " role=\"");
        bool algo = false;
        if (b->meta.estilo.tamanho) { esc(d, b->meta.estilo); algo = true; }
        for (int32_t k = 0; k < b->meta.papeis.quantidade; k++) { if (algo) txt(d, " "); esc(d, b->meta.papeis.itens[k]); algo = true; }
        txt(d, "\"");
    }
    txt(d, ">\n");
    titulo_se(d, b);
    for (const ADC_Bloco* it = b->filhos; it; it = it->proximo) {
        txt(d, "<varlistentry>\n");
        for (int32_t k = 0; k < it->quantidade_termos; k++) { txt(d, "<term>"); inlines(d, it->termos[k]); txt(d, "</term>\n"); }
        txt(d, "<listitem>\n");
        item_corpo(d, it->principal, it->filhos);
        txt(d, "</listitem>\n</varlistentry>\n");
    }
    txt(d, "</variablelist>\n");
}

static const char* alinhamento_h(ADC_Alinhamento a) { return a == ADC_ALINHA_CENTRO ? "center" : a == ADC_ALINHA_DIREITA ? "right" : "left"; }
static const char* alinhamento_v(ADC_Alinhamento a) { return a == ADC_ALINHA_MEIO ? "middle" : a == ADC_ALINHA_BASE ? "bottom" : "top"; }

static void celula(D* d, const ADC_Bloco* c, bool pCabecalho, int32_t pColuna) {
    txt(d, "<entry align=\""); txt(d, alinhamento_h(c->alinha_h)); txt(d, "\" valign=\""); txt(d, alinhamento_v(c->alinha_v)); txt(d, "\"");
    if (c->colspan > 1) { txt(d, " namest=\"col_"); adc_buffer_int(d->b, pColuna + 1); txt(d, "\" nameend=\"col_"); adc_buffer_int(d->b, pColuna + c->colspan); txt(d, "\""); }
    if (c->rowspan > 1) { txt(d, " morerows=\""); adc_buffer_int(d->b, c->rowspan - 1); txt(d, "\""); }
    txt(d, ">");
    if (pCabecalho) { inlines(d, c->inlines); for (const ADC_Bloco* p = c->filhos; p; p = p->proximo) inlines(d, p->inlines); }
    else if (c->estilo_celula == 'a') { txt(d, "\n"); blocos(d, c->filhos); }
    else if (c->estilo_celula == 'l') { txt(d, "<literallayout class=\"monospaced\">"); for (const ADC_Inline* i = c->inlines; i; i = i->proximo) if (i->tipo == ADC_IN_TEXTO) esc(d, i->valor); txt(d, "</literallayout>"); }
    else {
        const char* abre = c->estilo_celula == 'e' ? "<simpara><emphasis>" : c->estilo_celula == 's' ? "<simpara><emphasis role=\"strong\">" : c->estilo_celula == 'm' ? "<simpara><literal>" : "<simpara>";
        const char* fecha = c->estilo_celula == 'e' ? "</emphasis></simpara>" : c->estilo_celula == 's' ? "</emphasis></simpara>" : c->estilo_celula == 'm' ? "</literal></simpara>" : "</simpara>";
        if (c->filhos) { for (const ADC_Bloco* p = c->filhos; p; p = p->proximo) { txt(d, abre); inlines(d, p->inlines); txt(d, fecha); if (p->proximo) txt(d, "\n"); } }
        else if (c->inlines) { txt(d, abre); inlines(d, c->inlines); txt(d, fecha); }
    }
    txt(d, "</entry>\n");
}

static void tabela(D* d, const ADC_Bloco* b) {
    const ADC_Tabela* T = b->tabela;
    if (!T) return;
    ADC_Fatia frame = T->molduras.tamanho ? T->molduras : adc_fatia_cstr("all");
    if (adc_fatia_igual(frame, "ends")) frame = adc_fatia_cstr("topbot");
    ADC_Fatia grid = T->grade.tamanho ? T->grade : adc_fatia_cstr("all");
    bool rowsep = adc_fatia_igual(grid, "all") || adc_fatia_igual(grid, "rows");
    bool colsep = adc_fatia_igual(grid, "all") || adc_fatia_igual(grid, "cols");
    txt(d, b->titulo ? "<table" : "<informaltable"); id_e_papeis(d, b);
    txt(d, " frame=\""); esc(d, frame); txt(d, "\" rowsep=\""); txt(d, rowsep ? "1" : "0"); txt(d, "\" colsep=\""); txt(d, colsep ? "1" : "0"); txt(d, "\"");
    if (T->largura_percentual > 0 && T->largura_percentual < 100) { txt(d, " width=\""); adc_buffer_int(d->b, T->largura_percentual); txt(d, "%\""); }
    if (T->autowidth) txt(d, " tabstyle=\"autowidth\"");
    txt(d, ">\n");
    titulo_se(d, b);
    txt(d, "<tgroup cols=\""); adc_buffer_int(d->b, T->quantidade_colunas); txt(d, "\">\n");
    for (int32_t k = 0; k < T->quantidade_colunas; k++) {
        txt(d, "<colspec colname=\"col_"); adc_buffer_int(d->b, k + 1); txt(d, "\"");
        if (!T->autowidth && !T->colunas[k].automatica) { txt(d, " colwidth=\""); adc_buffer_int(d->b, T->colunas[k].largura > 0 ? T->colunas[k].largura : 1); txt(d, T->colunas[k].percentual ? "%\"" : "*\""); }
        txt(d, "/>\n");
    }
    bool em_thead = false, em_tbody = false, em_tfoot = false;
    for (const ADC_LinhaTabela* l = T->linhas; l; l = l->proxima) {
        if (l->cabecalho) { if (!em_thead) { txt(d, "<thead>\n"); em_thead = true; } }
        else if (l->rodape) { if (em_thead) { txt(d, "</thead>\n"); em_thead = false; } if (em_tbody) { txt(d, "</tbody>\n"); em_tbody = false; } if (!em_tfoot) { txt(d, "<tfoot>\n"); em_tfoot = true; } }
        else { if (em_thead) { txt(d, "</thead>\n"); em_thead = false; } if (!em_tbody) { txt(d, "<tbody>\n"); em_tbody = true; } }
        txt(d, "<row>\n");
        int32_t coluna = 0;
        for (const ADC_Bloco* c = l->celulas; c; c = c->proximo) { celula(d, c, l->cabecalho || c->estilo_celula == 'h', coluna); coluna += c->colspan > 1 ? c->colspan : 1; }
        txt(d, "</row>\n");
    }
    if (em_thead) txt(d, "</thead>\n");
    if (em_tbody) txt(d, "</tbody>\n");
    if (em_tfoot) txt(d, "</tfoot>\n");
    txt(d, "</tgroup>\n");
    txt(d, b->titulo ? "</table>\n" : "</informaltable>\n");
}

static void imagem_bloco(D* d, const ADC_Bloco* b) {
    bool a;
    ADC_Fatia alt = adc_atributo_obter(b->meta.atributos, "alt", &a);
    txt(d, b->titulo ? "<figure" : "<informalfigure"); id_e_papeis(d, b); txt(d, ">\n");
    titulo_se(d, b);
    txt(d, "<mediaobject>\n<imageobject>\n"); imagedata(d, b->alvo, b->meta.atributos, true); txt(d, "\n</imageobject>\n<textobject><phrase>"); esc(d, alt); txt(d, "</phrase></textobject>\n</mediaobject>\n");
    txt(d, b->titulo ? "</figure>\n" : "</informalfigure>\n");
}

static void secao(D* d, const ADC_Bloco* b) {
    bool parte = b->nivel <= 0;
    const char* tag = parte ? "part" : adc_fatia_igual(b->meta.estilo, "appendix") ? "appendix" : adc_fatia_igual(b->meta.estilo, "preface") ? "preface" : adc_fatia_igual(b->meta.estilo, "bibliography") ? "bibliography" : adc_fatia_igual(b->meta.estilo, "glossary") ? "glossary" : adc_fatia_igual(b->meta.estilo, "index") ? "index" : adc_fatia_igual(b->meta.estilo, "colophon") ? "colophon" : adc_fatia_igual(b->meta.estilo, "dedication") ? "dedication" : adc_fatia_igual(b->meta.estilo, "abstract") ? "abstract" : "section";
    txt(d, "<"); txt(d, tag); id_e_papeis(d, b); txt(d, ">\n<title>"); inlines(d, b->titulo); txt(d, "</title>\n");
    d->profundidade_secao++;
    blocos(d, b->filhos);
    d->profundidade_secao--;
    txt(d, "</"); txt(d, tag); txt(d, ">\n");
}

static void composto(D* d, const ADC_Bloco* b, const char* pTag, const char* pTagSemTitulo) {
    const char* tag = b->titulo || !pTagSemTitulo ? pTag : pTagSemTitulo;
    txt(d, "<"); txt(d, tag); id_e_papeis(d, b); txt(d, ">\n");
    titulo_se(d, b);
    blocos(d, b->filhos);
    txt(d, "</"); txt(d, tag); txt(d, ">\n");
}

static void bloco(D* d, const ADC_Bloco* b) {
    switch (b->tipo) {
        case ADC_BL_SECAO: secao(d, b); break;
        case ADC_BL_TITULO: {
            int32_t n = b->nivel < 1 ? 1 : b->nivel; if (n > 5) n = 5;
            txt(d, "<bridgehead"); id_e_papeis(d, b); txt(d, " renderas=\"sect"); adc_buffer_int(d->b, n); txt(d, "\">"); inlines(d, b->titulo); txt(d, "</bridgehead>\n");
            break;
        }
        case ADC_BL_PARAGRAFO: paragrafo(d, b); break;
        case ADC_BL_LISTING: case ADC_BL_LITERAL: listing(d, b); break;
        case ADC_BL_PASS: adc_buffer_fatia(d->b, b->texto_cru); txt(d, "\n"); break;
        case ADC_BL_STEM:
            txt(d, b->titulo ? "<equation" : "<informalequation"); id_e_papeis(d, b); txt(d, ">\n"); titulo_se(d, b);
            txt(d, "<mathphrase>"); esc(d, b->texto_cru); txt(d, "</mathphrase>\n"); txt(d, b->titulo ? "</equation>\n" : "</informalequation>\n");
            break;
        case ADC_BL_VERSE: verse(d, b); break;
        case ADC_BL_ADMONICAO: admonicao(d, b); break;
        case ADC_BL_EXEMPLO: composto(d, b, "example", "informalexample"); break;
        case ADC_BL_SIDEBAR: composto(d, b, "sidebar", NULL); break;
        case ADC_BL_OPEN:
            if (adc_fatia_igual(b->meta.estilo, "abstract")) composto(d, b, "abstract", NULL);
            else if (adc_fatia_igual(b->meta.estilo, "partintro")) composto(d, b, "partintro", NULL);
            else { titulo_se(d, b); blocos(d, b->filhos); }
            break;
        case ADC_BL_QUOTE: quote(d, b); break;
        case ADC_BL_LISTA: lista(d, b); break;
        case ADC_BL_DLIST: dlist(d, b); break;
        case ADC_BL_TABELA: tabela(d, b); break;
        case ADC_BL_QUEBRA: txt(d, b->variante_quebra == ADC_QUEBRA_PAGE ? "<simpara><?asciidoc-pagebreak?></simpara>\n" : "<simpara><?asciidoc-hr?></simpara>\n"); break;
        case ADC_BL_MACRO:
            if (adc_fatia_igual(b->nome_macro, "image")) imagem_bloco(d, b);
            else if (adc_fatia_igual(b->nome_macro, "toc")) txt(d, "<?asciidoc-toc?>\n");
            else if (adc_fatia_igual(b->nome_macro, "video") || adc_fatia_igual(b->nome_macro, "audio")) {
                bool video = adc_fatia_igual(b->nome_macro, "video");
                txt(d, b->titulo ? "<figure" : "<informalfigure"); id_e_papeis(d, b); txt(d, ">\n"); titulo_se(d, b);
                txt(d, "<mediaobject>\n"); txt(d, video ? "<videoobject>\n<videodata fileref=\"" : "<audioobject>\n<audiodata fileref=\""); esc(d, b->alvo); txt(d, "\"/>\n"); txt(d, video ? "</videoobject>\n" : "</audioobject>\n");
                txt(d, "<textobject><phrase>"); esc(d, b->alvo); txt(d, "</phrase></textobject>\n</mediaobject>\n");
                txt(d, b->titulo ? "</figure>\n" : "</informalfigure>\n");
            }
            break;
        default: break;
    }
}

static void blocos(D* d, const ADC_Bloco* pPrimeiro) {
    for (const ADC_Bloco* b = pPrimeiro; b; b = b->proximo) bloco(d, b);
}

/* ---------------------------------------------------------------- documento ---- */

static void info(D* d) {
    const ADC_Contexto* c = d->ctx;
    const ADC_TabelaAtributos* at = &c->atribs;
    ADC_Fatia v;
    txt(d, "<info>\n<title>");
    if (c->cabecalho.titulo) inlines(d, c->cabecalho.titulo); else esc(d, adc_atribs_valor_ou(at, "untitled-label", "Untitled"));
    txt(d, "</title>\n");
    if (c->cabecalho.revisao_data.tamanho) { txt(d, "<date>"); esc(d, c->cabecalho.revisao_data); txt(d, "</date>\n"); }
    else if (adc_atribs_obter_cstr(at, "docdate", &v) && v.tamanho) { txt(d, "<date>"); esc(d, v); txt(d, "</date>\n"); }
    for (const ADC_Autor* a = c->cabecalho.autores; a; a = a->proximo) {
        txt(d, "<author>\n<personname>\n");
        if (a->primeiro_nome.tamanho) { txt(d, "<firstname>"); esc(d, a->primeiro_nome); txt(d, "</firstname>\n"); }
        if (a->nome_do_meio.tamanho) { txt(d, "<othername>"); esc(d, a->nome_do_meio); txt(d, "</othername>\n"); }
        if (a->ultimo_nome.tamanho) { txt(d, "<surname>"); esc(d, a->ultimo_nome); txt(d, "</surname>\n"); }
        txt(d, "</personname>\n");
        if (a->email.tamanho) { txt(d, "<email>"); esc(d, a->email); txt(d, "</email>\n"); }
        txt(d, "</author>\n");
        if (a == c->cabecalho.autores && a->iniciais.tamanho) { txt(d, "<authorinitials>"); esc(d, a->iniciais); txt(d, "</authorinitials>\n"); }
    }
    if (c->cabecalho.revisao_numero.tamanho || c->cabecalho.revisao_data.tamanho || c->cabecalho.revisao_observacao.tamanho) {
        txt(d, "<revhistory>\n<revision>\n");
        if (c->cabecalho.revisao_numero.tamanho) { txt(d, "<revnumber>"); esc(d, c->cabecalho.revisao_numero); txt(d, "</revnumber>\n"); }
        if (c->cabecalho.revisao_data.tamanho) { txt(d, "<date>"); esc(d, c->cabecalho.revisao_data); txt(d, "</date>\n"); }
        if (c->cabecalho.autores && c->cabecalho.autores->iniciais.tamanho) { txt(d, "<authorinitials>"); esc(d, c->cabecalho.autores->iniciais); txt(d, "</authorinitials>\n"); }
        if (c->cabecalho.revisao_observacao.tamanho) { txt(d, "<revremark>"); esc(d, c->cabecalho.revisao_observacao); txt(d, "</revremark>\n"); }
        txt(d, "</revision>\n</revhistory>\n");
    }
    if (adc_atribs_obter_cstr(at, "description", &v) && v.tamanho) { txt(d, "<abstract>\n<simpara>"); esc(d, v); txt(d, "</simpara>\n</abstract>\n"); }
    if (adc_atribs_obter_cstr(at, "keywords", &v) && v.tamanho) {
        txt(d, "<keywordset>\n");
        int32_t ini = 0;
        for (int32_t i = 0; i <= v.tamanho; i++) {
            if (i == v.tamanho || v.dados[i] == ',') { ADC_Fatia k = adc_fatia_aparar(adc_fatia(v.dados + ini, i - ini)); if (k.tamanho) { txt(d, "<keyword>"); esc(d, k); txt(d, "</keyword>\n"); } ini = i + 1; }
        }
        txt(d, "</keywordset>\n");
    }
    txt(d, "</info>\n");
}

void adc_docbook5_escrever(ADC_Contexto* pC, ADC_Buffer* pSaida) {
    D d; memset(&d, 0, sizeof d);
    d.ctx = pC; d.b = pSaida;
    const ADC_TabelaAtributos* at = &pC->atribs;
    ADC_Fatia doctype = adc_atribs_valor_ou(at, "doctype", "article");
    const char* raiz = adc_fatia_igual(doctype, "book") ? "book" : "article";
    txt(&d, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
    txt(&d, "<"); txt(&d, raiz); txt(&d, " xmlns=\"http://docbook.org/ns/docbook\" xmlns:xl=\"http://www.w3.org/1999/xlink\" version=\"5.0\"");
    if (!adc_atribs_definido(at, "nolang")) { txt(&d, " xml:lang=\""); esc(&d, adc_atribs_valor_ou(at, "lang", "pt-BR")); txt(&d, "\""); }
    if (pC->documento && pC->documento->meta.id.tamanho) { txt(&d, " xml:id=\""); esc(&d, pC->documento->meta.id); txt(&d, "\""); }
    txt(&d, ">\n");
    if (pC->cabecalho.presente || adc_atribs_definido(at, "description") || adc_atribs_definido(at, "keywords")) info(&d);
    if (adc_atribs_definido(at, "stem"))
        adc_msgs_adicionar(&pC->msgs, ADC_NIVEL_AVISO, 0, 0, "DocBook: :stem: está definido; as expressões saem em <literal>, sem MathML.");
    /* preâmbulo: blocos antes da primeira seção (o Asciidoctor os envolve em <preface> no book e os deixa soltos no artigo) */
    const ADC_Bloco* b = pC->documento ? pC->documento->filhos : NULL;
    bool tem_secao = false; for (const ADC_Bloco* k = b; k; k = k->proximo) if (k->tipo == ADC_BL_SECAO) { tem_secao = true; break; }
    if (tem_secao && b && b->tipo != ADC_BL_SECAO && adc_fatia_igual(doctype, "book")) {
        txt(&d, "<preface>\n<title>"); esc(&d, adc_atribs_valor_ou(at, "preface-title", "")); txt(&d, "</title>\n");
        while (b && b->tipo != ADC_BL_SECAO) { bloco(&d, b); b = b->proximo; }
        txt(&d, "</preface>\n");
    }
    for (; b; b = b->proximo) bloco(&d, b);
    txt(&d, "</"); txt(&d, raiz); txt(&d, ">\n");
}
