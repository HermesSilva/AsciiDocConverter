/*
 * ASG JSON — o oráculo do TCK. Segue asg/schema.json (draft-01). O que a casa modela além do esquema
 * (tabela, nota de rodapé, imagem inline, tecla, quebra de linha, sup/sub) sai aqui do modo mais próximo
 * possível; a divergência está em docs/fidelidade.md.
 */
#include "../adc_render.h"
#include "../adc_json.h"

typedef struct W { ADC_Contexto* ctx; ADC_Buffer* b; bool com_arquivo; } W;

static void chave(W* w, const char* pNome) { adc_json_escrever_texto(w->b, adc_fatia_cstr(pNome)); adc_buffer_char(w->b, ':'); }
static void texto_json(W* w, ADC_Fatia pT) { adc_json_escrever_texto(w->b, pT); }

static void local(W* w, const ADC_Local* pL) {
    if (pL->inicio.linha <= 0) return;
    adc_buffer_texto(w->b, ",\"location\":[{\"line\":"); adc_buffer_int(w->b, pL->inicio.linha);
    adc_buffer_texto(w->b, ",\"col\":"); adc_buffer_int(w->b, pL->inicio.coluna);
    if (pL->arquivo > 0) { adc_buffer_texto(w->b, ",\"file\":["); texto_json(w, adc_leitor_caminho(&w->ctx->leitor, pL->arquivo)); adc_buffer_char(w->b, ']'); }
    adc_buffer_texto(w->b, "},{\"line\":"); adc_buffer_int(w->b, pL->fim.linha);
    adc_buffer_texto(w->b, ",\"col\":"); adc_buffer_int(w->b, pL->fim.coluna);
    if (pL->arquivo > 0) { adc_buffer_texto(w->b, ",\"file\":["); texto_json(w, adc_leitor_caminho(&w->ctx->leitor, pL->arquivo)); adc_buffer_char(w->b, ']'); }
    adc_buffer_texto(w->b, "}]");
}

static void inlines(W* w, const ADC_Inline* pI);

static void inline_um(W* w, const ADC_Inline* i) {
    adc_buffer_char(w->b, '{');
    switch (i->tipo) {
        case ADC_IN_TEXTO: adc_buffer_texto(w->b, "\"name\":\"text\",\"type\":\"string\",\"value\":"); texto_json(w, i->valor); break;
        case ADC_IN_CHARREF:
            if (adc_fatia_igual(i->valor, "callout")) { adc_buffer_texto(w->b, "\"name\":\"callout\",\"type\":\"string\",\"value\":"); char tmp[16]; snprintf(tmp, sizeof tmp, "%d", i->numero); texto_json(w, adc_fatia_cstr(tmp)); }
            else { adc_buffer_texto(w->b, "\"name\":\"charref\",\"type\":\"string\",\"value\":"); texto_json(w, i->valor); }
            break;
        case ADC_IN_RAW: adc_buffer_texto(w->b, "\"name\":\"raw\",\"type\":\"string\",\"value\":"); texto_json(w, i->valor); break;
        case ADC_IN_SPAN: {
            const char* v = i->variante_span == ADC_SPAN_STRONG ? "strong" : i->variante_span == ADC_SPAN_EMPHASIS ? "emphasis" : i->variante_span == ADC_SPAN_CODE ? "code" : i->variante_span == ADC_SPAN_MARK ? "mark" : i->variante_span == ADC_SPAN_SUPERSCRIPT ? "superscript" : "subscript";
            adc_buffer_texto(w->b, "\"name\":\"span\",\"type\":\"inline\",\"variant\":\""); adc_buffer_texto(w->b, v);
            adc_buffer_texto(w->b, "\",\"form\":\""); adc_buffer_texto(w->b, i->unconstrained ? "unconstrained" : "constrained"); adc_buffer_char(w->b, '"');
            if (i->id.tamanho) { adc_buffer_texto(w->b, ",\"id\":"); texto_json(w, i->id); }
            if (i->papeis.quantidade) { adc_buffer_texto(w->b, ",\"roles\":["); for (int32_t k = 0; k < i->papeis.quantidade; k++) { if (k) adc_buffer_char(w->b, ','); texto_json(w, i->papeis.itens[k]); } adc_buffer_char(w->b, ']'); }
            adc_buffer_texto(w->b, ",\"inlines\":"); inlines(w, i->filhos);
            break;
        }
        case ADC_IN_REF:
            adc_buffer_texto(w->b, "\"name\":\"ref\",\"type\":\"inline\",\"variant\":\""); adc_buffer_texto(w->b, i->variante_ref == ADC_REF_LINK ? "link" : "xref");
            adc_buffer_texto(w->b, "\",\"target\":"); texto_json(w, i->alvo);
            adc_buffer_texto(w->b, ",\"inlines\":"); inlines(w, i->filhos);
            break;
        case ADC_IN_IMAGEM:
            adc_buffer_texto(w->b, "\"name\":\"image\",\"type\":\"inline\",\"form\":\"macro\",\"target\":"); texto_json(w, i->alvo);
            break;
        case ADC_IN_NOTA:
            adc_buffer_texto(w->b, "\"name\":\"footnote\",\"type\":\"inline\",\"number\":"); adc_buffer_int(w->b, i->numero);
            if (i->id.tamanho) { adc_buffer_texto(w->b, ",\"id\":"); texto_json(w, i->id); }
            adc_buffer_texto(w->b, ",\"inlines\":"); inlines(w, i->filhos);
            break;
        case ADC_IN_QUEBRA_LINHA: adc_buffer_texto(w->b, "\"name\":\"text\",\"type\":\"string\",\"value\":\"\\n\""); break;
        case ADC_IN_ANCORA: adc_buffer_texto(w->b, "\"name\":\"anchor\",\"type\":\"inline\",\"id\":"); texto_json(w, i->id); adc_buffer_texto(w->b, ",\"inlines\":"); inlines(w, i->filhos); break;
        case ADC_IN_TECLA: adc_buffer_texto(w->b, "\"name\":\"kbd\",\"type\":\"inline\",\"inlines\":"); inlines(w, i->filhos); break;
        case ADC_IN_BOTAO: adc_buffer_texto(w->b, "\"name\":\"btn\",\"type\":\"inline\",\"inlines\":"); inlines(w, i->filhos); break;
        case ADC_IN_MENU: adc_buffer_texto(w->b, "\"name\":\"menu\",\"type\":\"inline\",\"inlines\":"); inlines(w, i->filhos); break;
        case ADC_IN_ICONE: adc_buffer_texto(w->b, "\"name\":\"icon\",\"type\":\"inline\",\"target\":"); texto_json(w, i->alvo); break;
        case ADC_IN_INDICE: adc_buffer_texto(w->b, "\"name\":\"indexterm\",\"type\":\"inline\",\"inlines\":"); inlines(w, i->filhos); break;
        default: adc_buffer_texto(w->b, "\"name\":\"text\",\"type\":\"string\",\"value\":\"\""); break;
    }
    local(w, &i->local);
    adc_buffer_char(w->b, '}');
}

static void inlines(W* w, const ADC_Inline* pI) {
    adc_buffer_char(w->b, '[');
    bool primeiro = true;
    for (const ADC_Inline* i = pI; i; i = i->proximo) {
        if (i->tipo == ADC_IN_RAW && i->valor.dados == NULL && i->filhos) { /* pass:q[] — emite os filhos */
            for (const ADC_Inline* f = i->filhos; f; f = f->proximo) { if (!primeiro) adc_buffer_char(w->b, ','); primeiro = false; inline_um(w, f); }
            continue;
        }
        if (!primeiro) adc_buffer_char(w->b, ',');
        primeiro = false;
        inline_um(w, i);
    }
    adc_buffer_char(w->b, ']');
}

static void metadados(W* w, const ADC_Bloco* b) {
    const ADC_Metadados* m = &b->meta;
    if (m->vazio) return;
    bool tem_attrs = false;
    for (const ADC_Atributo* a = m->atributos; a; a = a->proximo) { if (adc_fatia_igual(a->nome, "role") || adc_fatia_igual(a->nome, "opts")) continue; tem_attrs = true; break; }
    if (!tem_attrs && m->papeis.quantidade == 0 && m->opcoes.quantidade == 0) return;
    adc_buffer_texto(w->b, ",\"metadata\":{");
    bool algo = false;
    if (tem_attrs) {
        adc_buffer_texto(w->b, "\"attributes\":{");
        bool primeiro = true;
        for (const ADC_Atributo* a = m->atributos; a; a = a->proximo) {
            if (adc_fatia_igual(a->nome, "role") || adc_fatia_igual(a->nome, "opts")) continue;
            if (!primeiro) adc_buffer_char(w->b, ',');
            primeiro = false;
            texto_json(w, a->nome); adc_buffer_char(w->b, ':'); texto_json(w, a->valor);
        }
        adc_buffer_char(w->b, '}');
        algo = true;
    }
    if (m->papeis.quantidade) {
        if (algo) adc_buffer_char(w->b, ',');
        adc_buffer_texto(w->b, "\"roles\":[");
        for (int32_t k = 0; k < m->papeis.quantidade; k++) { if (k) adc_buffer_char(w->b, ','); texto_json(w, m->papeis.itens[k]); }
        adc_buffer_char(w->b, ']'); algo = true;
    }
    if (m->opcoes.quantidade) {
        if (algo) adc_buffer_char(w->b, ',');
        adc_buffer_texto(w->b, "\"options\":[");
        for (int32_t k = 0; k < m->opcoes.quantidade; k++) { if (k) adc_buffer_char(w->b, ','); texto_json(w, m->opcoes.itens[k]); }
        adc_buffer_char(w->b, ']'); algo = true;
    }
    if (m->tem_local && algo) local(w, &m->local);
    adc_buffer_char(w->b, '}');
}

static void comuns(W* w, const ADC_Bloco* b) {
    if (b->meta.id.tamanho && !b->id_gerado) { adc_buffer_texto(w->b, ",\"id\":"); texto_json(w, b->meta.id); }
    if (b->titulo && b->tipo != ADC_BL_SECAO && b->tipo != ADC_BL_TITULO) { adc_buffer_texto(w->b, ",\"title\":"); inlines(w, b->titulo); }
    if (b->reftext) { adc_buffer_texto(w->b, ",\"reftext\":"); inlines(w, b->reftext); }
    metadados(w, b);
}

static void blocos(W* w, const ADC_Bloco* pPrimeiro);

static void forma(W* w, const ADC_Bloco* b) {
    if (b->forma == ADC_FORMA_DELIMITADA) { adc_buffer_texto(w->b, ",\"form\":\"delimited\",\"delimiter\":"); texto_json(w, b->delimitador); }
    else if (b->forma == ADC_FORMA_INDENTADA) adc_buffer_texto(w->b, ",\"form\":\"indented\"");
    else if (b->forma == ADC_FORMA_PARAGRAFO && b->tipo != ADC_BL_PARAGRAFO) adc_buffer_texto(w->b, ",\"form\":\"paragraph\"");
    else if (b->forma == ADC_FORMA_MACRO) adc_buffer_texto(w->b, ",\"form\":\"macro\"");
}

static void bloco(W* w, const ADC_Bloco* b) {
    adc_buffer_char(w->b, '{');
    adc_buffer_texto(w->b, "\"name\":");
    switch (b->tipo) {
        case ADC_BL_SECAO:
            adc_buffer_texto(w->b, "\"section\",\"type\":\"block\",\"title\":"); inlines(w, b->titulo);
            adc_buffer_texto(w->b, ",\"level\":"); adc_buffer_int(w->b, b->nivel);
            comuns(w, b);
            local(w, &b->local);
            if (b->filhos) { adc_buffer_texto(w->b, ",\"blocks\":"); blocos(w, b->filhos); }
            adc_buffer_char(w->b, '}');
            return;
        case ADC_BL_TITULO:
            adc_buffer_texto(w->b, "\"heading\",\"type\":\"block\",\"title\":"); inlines(w, b->titulo);
            adc_buffer_texto(w->b, ",\"level\":"); adc_buffer_int(w->b, b->nivel);
            comuns(w, b); break;
        case ADC_BL_PARAGRAFO: case ADC_BL_LISTING: case ADC_BL_LITERAL: case ADC_BL_PASS: case ADC_BL_STEM: case ADC_BL_VERSE:
            texto_json(w, adc_tipo_bloco_nome(b->tipo)); adc_buffer_texto(w->b, ",\"type\":\"block\"");
            forma(w, b);
            comuns(w, b);
            adc_buffer_texto(w->b, ",\"inlines\":"); inlines(w, b->inlines);
            break;
        case ADC_BL_ADMONICAO: case ADC_BL_EXEMPLO: case ADC_BL_SIDEBAR: case ADC_BL_OPEN: case ADC_BL_QUOTE:
            texto_json(w, adc_tipo_bloco_nome(b->tipo)); adc_buffer_texto(w->b, ",\"type\":\"block\"");
            if (b->tipo == ADC_BL_ADMONICAO) { adc_buffer_texto(w->b, ",\"variant\":"); texto_json(w, adc_variante_adm_nome(b->variante_adm)); }
            forma(w, b);
            comuns(w, b);
            if (b->filhos) { adc_buffer_texto(w->b, ",\"blocks\":"); blocos(w, b->filhos); }
            break;
        case ADC_BL_LISTA:
            adc_buffer_texto(w->b, "\"list\",\"type\":\"block\",\"variant\":\"");
            adc_buffer_texto(w->b, b->variante_lista == ADC_LISTA_ORDERED ? "ordered" : b->variante_lista == ADC_LISTA_CALLOUT ? "callout" : "unordered");
            adc_buffer_texto(w->b, "\",\"marker\":"); texto_json(w, b->marcador);
            comuns(w, b);
            adc_buffer_texto(w->b, ",\"items\":"); blocos(w, b->filhos);
            break;
        case ADC_BL_ITEM_LISTA:
            adc_buffer_texto(w->b, "\"listItem\",\"type\":\"block\",\"marker\":"); texto_json(w, b->marcador);
            adc_buffer_texto(w->b, ",\"principal\":"); inlines(w, b->principal);
            comuns(w, b);
            if (b->filhos) { adc_buffer_texto(w->b, ",\"blocks\":"); blocos(w, b->filhos); }
            break;
        case ADC_BL_DLIST:
            adc_buffer_texto(w->b, "\"dlist\",\"type\":\"block\",\"marker\":"); texto_json(w, b->marcador);
            comuns(w, b);
            adc_buffer_texto(w->b, ",\"items\":"); blocos(w, b->filhos);
            break;
        case ADC_BL_ITEM_DLIST:
            adc_buffer_texto(w->b, "\"dlistItem\",\"type\":\"block\",\"marker\":"); texto_json(w, b->marcador);
            adc_buffer_texto(w->b, ",\"terms\":[");
            for (int32_t k = 0; k < b->quantidade_termos; k++) { if (k) adc_buffer_char(w->b, ','); inlines(w, b->termos[k]); }
            adc_buffer_char(w->b, ']');
            if (b->principal) { adc_buffer_texto(w->b, ",\"principal\":"); inlines(w, b->principal); }
            comuns(w, b);
            if (b->filhos) { adc_buffer_texto(w->b, ",\"blocks\":"); blocos(w, b->filhos); }
            break;
        case ADC_BL_QUEBRA:
            adc_buffer_texto(w->b, "\"break\",\"type\":\"block\",\"variant\":\""); adc_buffer_texto(w->b, b->variante_quebra == ADC_QUEBRA_PAGE ? "page" : "thematic"); adc_buffer_char(w->b, '"');
            comuns(w, b); break;
        case ADC_BL_MACRO:
            texto_json(w, b->nome_macro); adc_buffer_texto(w->b, ",\"type\":\"block\",\"form\":\"macro\"");
            if (b->alvo.tamanho) { adc_buffer_texto(w->b, ",\"target\":"); texto_json(w, b->alvo); }
            comuns(w, b); break;
        case ADC_BL_TABELA: {
            adc_buffer_texto(w->b, "\"table\",\"type\":\"block\"");
            forma(w, b);
            comuns(w, b);
            adc_buffer_texto(w->b, ",\"columns\":"); adc_buffer_int(w->b, b->tabela ? b->tabela->quantidade_colunas : 0);
            adc_buffer_texto(w->b, ",\"rows\":[");
            bool pl = true;
            for (const ADC_LinhaTabela* l = b->tabela ? b->tabela->linhas : NULL; l; l = l->proxima) {
                if (!pl) { adc_buffer_char(w->b, ','); }
                pl = false;
                adc_buffer_texto(w->b, "{\"header\":"); adc_buffer_texto(w->b, l->cabecalho ? "true" : "false");
                adc_buffer_texto(w->b, ",\"cells\":[");
                bool pc = true;
                for (const ADC_Bloco* c = l->celulas; c; c = c->proximo) {
                    if (!pc) { adc_buffer_char(w->b, ','); }
                    pc = false;
                    adc_buffer_texto(w->b, "{\"name\":\"cell\",\"type\":\"block\",\"colspan\":"); adc_buffer_int(w->b, c->colspan);
                    adc_buffer_texto(w->b, ",\"rowspan\":"); adc_buffer_int(w->b, c->rowspan);
                    if (c->filhos) { adc_buffer_texto(w->b, ",\"blocks\":"); blocos(w, c->filhos); }
                    else { adc_buffer_texto(w->b, ",\"inlines\":"); inlines(w, c->inlines); }
                    local(w, &c->local);
                    adc_buffer_char(w->b, '}');
                }
                adc_buffer_texto(w->b, "]}");
            }
            adc_buffer_char(w->b, ']');
            break;
        }
        default:
            adc_buffer_texto(w->b, "\"paragraph\",\"type\":\"block\",\"inlines\":[]");
            break;
    }
    local(w, &b->local);
    adc_buffer_char(w->b, '}');
}

static void blocos(W* w, const ADC_Bloco* pPrimeiro) {
    adc_buffer_char(w->b, '[');
    bool primeiro = true;
    for (const ADC_Bloco* b = pPrimeiro; b; b = b->proximo) {
        if (!primeiro) adc_buffer_char(w->b, ',');
        primeiro = false;
        bloco(w, b);
    }
    adc_buffer_char(w->b, ']');
}

void adc_asg_escrever_inlines(ADC_Contexto* pC, const ADC_Inline* pInlines, ADC_Buffer* pSaida) {
    W w; w.ctx = pC; w.b = pSaida; w.com_arquivo = false;
    inlines(&w, pInlines);
}

void adc_asg_escrever_documento(ADC_Contexto* pC, ADC_Buffer* pSaida) {
    W w; w.ctx = pC; w.b = pSaida; w.com_arquivo = false;
    const ADC_Bloco* doc = pC->documento;
    adc_buffer_texto(pSaida, "{\"name\":\"document\",\"type\":\"block\"");
    if (pC->cabecalho.presente) {
        adc_buffer_texto(pSaida, ",\"attributes\":{");
        bool primeiro = true;
        for (const ADC_EntradaAtributo* e = pC->atribs.ordem; e; e = e->proxima_ordem) {
            if (!e->do_documento || e->removido) continue;
            if (!primeiro) { adc_buffer_char(pSaida, ','); }
            primeiro = false;
            texto_json(&w, e->nome); adc_buffer_char(pSaida, ':'); texto_json(&w, e->valor);
        }
        adc_buffer_texto(pSaida, "},\"header\":{");
        if (pC->cabecalho.titulo) { adc_buffer_texto(pSaida, "\"title\":"); inlines(&w, pC->cabecalho.titulo); }
        if (pC->cabecalho.autores) {
            adc_buffer_texto(pSaida, ",\"authors\":[");
            bool pa = true;
            for (const ADC_Autor* a = pC->cabecalho.autores; a; a = a->proximo) {
                if (!pa) { adc_buffer_char(pSaida, ','); }
                pa = false;
                adc_buffer_texto(pSaida, "{\"fullname\":"); texto_json(&w, a->nome_completo);
                if (a->iniciais.tamanho) { adc_buffer_texto(pSaida, ",\"initials\":"); texto_json(&w, a->iniciais); }
                if (a->primeiro_nome.tamanho) { adc_buffer_texto(pSaida, ",\"firstname\":"); texto_json(&w, a->primeiro_nome); }
                if (a->nome_do_meio.tamanho) { adc_buffer_texto(pSaida, ",\"middlename\":"); texto_json(&w, a->nome_do_meio); }
                if (a->ultimo_nome.tamanho) { adc_buffer_texto(pSaida, ",\"lastname\":"); texto_json(&w, a->ultimo_nome); }
                if (a->email.tamanho) { adc_buffer_texto(pSaida, ",\"address\":"); texto_json(&w, a->email); }
                adc_buffer_char(pSaida, '}');
            }
            adc_buffer_char(pSaida, ']');
        }
        if (pC->cabecalho.titulo) local(&w, &pC->cabecalho.local);
        else {
            /* sem título não há membro antes: escreve o location sem a vírgula inicial */
            int32_t antes = pSaida->tamanho;
            local(&w, &pC->cabecalho.local);
            if (pSaida->tamanho > antes && pSaida->dados[antes] == ',') memmove(pSaida->dados + antes, pSaida->dados + antes + 1, (size_t)(pSaida->tamanho - antes - 1)), pSaida->tamanho--;
        }
        adc_buffer_char(pSaida, '}');
    }
    if (doc && doc->filhos) { adc_buffer_texto(pSaida, ",\"blocks\":"); blocos(&w, doc->filhos); }
    if (doc) local(&w, &doc->local);
    adc_buffer_char(pSaida, '}');
}
