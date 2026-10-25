#include "adc_documento.h"
#include "adc_blocos.h"
#include "adc_inlines.h"
#include <stdio.h>
#include <stdlib.h>

bool adc_doc_iniciar(ADC_Contexto* pC, ADC_Arena* pArena, const ADC_Json* pOpcoes, ADC_ResolverRecurso pResolver, void* pContexto, const char** pErroOpcoes) {
    memset(pC, 0, sizeof *pC);
    pC->arena = pArena;
    adc_atribs_iniciar(&pC->atribs, pArena);
    adc_msgs_iniciar(&pC->msgs, pArena);
    pC->resolver = pResolver; pC->contexto_resolver = pContexto;
    pC->opcoes.tamanho_maximo = 64 * 1024 * 1024;
    pC->opcoes.aninhamento_maximo = 64;
    pC->opcoes.includes_maximo = 10000;   /* total; o Asciidoctor não limita o total (só a profundidade), e um assembly real passa de 64 */
    pC->opcoes.raiz = pOpcoes;
    if (pOpcoes) {
        if (pOpcoes->tipo != ADC_JSON_OBJETO) { if (pErroOpcoes) *pErroOpcoes = "as opções devem ser um objeto JSON"; return false; }
        pC->opcoes.autonomo = adc_json_bool(pOpcoes, "autonomo", false);
        const ADC_Json* md = adc_json_membro(pOpcoes, "markdown");
        pC->opcoes.md_html_embutido = adc_json_bool(md, "htmlEmbutido", true);
        const ADC_Json* lim = adc_json_membro(pOpcoes, "limites");
        if (lim) {
            double v = adc_json_numero(lim, "tamanhoMaximo", pC->opcoes.tamanho_maximo); if (v > 0 && v < 2147483647.0) pC->opcoes.tamanho_maximo = (int32_t)v;
            v = adc_json_numero(lim, "aninhamentoMaximo", pC->opcoes.aninhamento_maximo); if (v >= 1 && v <= 1000) pC->opcoes.aninhamento_maximo = (int32_t)v;
            v = adc_json_numero(lim, "includesMaximo", pC->opcoes.includes_maximo); if (v >= 0 && v <= 100000) pC->opcoes.includes_maximo = (int32_t)v;
        }
        pC->opcoes.pdf_tema = adc_json_membro(pOpcoes, "pdf");
        const ADC_Json* at = adc_json_membro(pOpcoes, "atributos");
        if (at) {
            if (at->tipo != ADC_JSON_OBJETO) { if (pErroOpcoes) *pErroOpcoes = "\"atributos\" deve ser um objeto { nome: valor }"; return false; }
            for (const ADC_Json* f = at->filhos; f; f = f->proximo) {
                ADC_Fatia nome = f->chave;
                bool remover = nome.tamanho > 0 && nome.dados[nome.tamanho - 1] == '!';
                if (remover) nome.tamanho--;
                if (!adc_atribs_nome_valido(nome)) { if (pErroOpcoes) *pErroOpcoes = "nome de atributo inválido em \"atributos\""; return false; }
                if (remover || f->tipo == ADC_JSON_NULO || (f->tipo == ADC_JSON_BOOL && !f->booleano)) { adc_atribs_remover(&pC->atribs, nome); continue; }
                ADC_Fatia valor;
                if (f->tipo == ADC_JSON_TEXTO) valor = f->texto;
                else if (f->tipo == ADC_JSON_BOOL) valor = adc_fatia("", 0);
                else if (f->tipo == ADC_JSON_NUMERO) { char tmp[32]; int n = snprintf(tmp, sizeof tmp, "%.15g", f->numero); valor = adc_fatia(adc_arena_copiar(pArena, tmp, n), n); }
                else { if (pErroOpcoes) *pErroOpcoes = "valor de atributo deve ser texto, número, booleano ou null"; return false; }
                /* "nome@" = valor pode ser sobrescrito pelo documento (como -a nome@=valor) */
                bool travar = true;
                if (nome.tamanho > 0 && nome.dados[nome.tamanho - 1] == '@') { nome.tamanho--; travar = false; }
                adc_atribs_definir(&pC->atribs, nome, valor, travar);
            }
        }
    } else {
        pC->opcoes.md_html_embutido = true;
    }
    /* intrínsecos do documento (os que o Asciidoctor define por padrão e afetam o parse/renderização) */
    static const struct { const char* n; const char* v; } padrao[] = {
        { "sectids", "" }, { "attribute-missing", "skip" }, { "attribute-undefined", "drop-line" },
        { "toc-title", "Sumário" }, { "toclevels", "2" }, { "idprefix", "_" }, { "idseparator", "_" },
        { "caution-caption", "Cuidado" }, { "important-caption", "Importante" }, { "note-caption", "Nota" },
        { "tip-caption", "Dica" }, { "warning-caption", "Atenção" }, { "appendix-caption", "Apêndice" },
        { "example-caption", "Exemplo" }, { "figure-caption", "Figura" }, { "table-caption", "Tabela" },
        { "listing-caption", "" }, { "footnotes-title", "" }, { "untitled-label", "Sem título" },
        { "version-label", "Versão" }, { "last-update-label", "Última atualização" },
        { "prewrap", "" }, { "max-include-depth", "64" }, { "iconsdir", "./images/icons" }, { "imagesdir", "" },
        { "doctype", "article" }, { "backend", "html5" }, { "basebackend", "html" }, { "filetype", "html" },
        { "asciidoctor", "" }, { "adc", "" }, { "lang", "pt-BR" },
    };
    for (size_t i = 0; i < sizeof padrao / sizeof padrao[0]; i++) {
        ADC_Fatia nome = adc_fatia_cstr(padrao[i].n);
        if (!adc_atribs_obter(&pC->atribs, nome, NULL)) {
            /* não sobrescreve removidos pela API: adc_atribs_obter devolve false também para removidos;
               distinguimos verificando se existe entrada */
            adc_atribs_definir(&pC->atribs, nome, adc_fatia_cstr(padrao[i].v), false);
        }
    }
    adc_leitor_iniciar(&pC->leitor, pArena, &pC->atribs, &pC->msgs, pResolver, pContexto, pC->opcoes.includes_maximo);
    return true;
}

/* -------------------------------------------------------- cabeçalho ---- */

static void definir(ADC_Contexto* pC, const char* pNome, ADC_Fatia pValor) {
    if (!adc_atribs_obter(&pC->atribs, adc_fatia_cstr(pNome), NULL) || true) adc_atribs_definir(&pC->atribs, adc_fatia_cstr(pNome), pValor, false);
}

static ADC_Fatia iniciais(ADC_Contexto* pC, ADC_Fatia pA, ADC_Fatia pB, ADC_Fatia pCc) {
    char tmp[16]; int32_t n = 0;
    if (pA.tamanho) tmp[n++] = pA.dados[0];
    if (pB.tamanho) tmp[n++] = pB.dados[0];
    if (pCc.tamanho) tmp[n++] = pCc.dados[0];
    return adc_fatia(adc_arena_copiar(pC->arena, tmp, n), n);
}

/* "Primeiro Meio Último <email>" — underscores viram espaços dentro de uma parte */
static ADC_Autor* ler_autor(ADC_Contexto* pC, ADC_Fatia pTexto) {
    ADC_Autor* a = ADC_NOVO(pC->arena, ADC_Autor);
    if (!a) return NULL;
    ADC_Fatia t = adc_fatia_aparar(pTexto);
    int32_t lt = adc_fatia_indice(t, '<');
    if (lt >= 0) {
        int32_t gt = -1; for (int32_t k = lt; k < t.tamanho; k++) if (t.dados[k] == '>') { gt = k; break; }
        if (gt > lt) { a->email = adc_fatia(t.dados + lt + 1, gt - lt - 1); t = adc_fatia_aparar(adc_fatia(t.dados, lt)); }
    }
    a->nome_completo = t;
    /* divide em até 3 partes por espaço */
    ADC_Fatia partes[3]; int32_t np = 0; int32_t ini = 0;
    for (int32_t i = 0; i <= t.tamanho && np < 3; i++) {
        if (i == t.tamanho || t.dados[i] == ' ') {
            if (i > ini) partes[np++] = adc_fatia(t.dados + ini, i - ini);
            ini = i + 1;
            if (np == 2 && i < t.tamanho) { ADC_Fatia resto = adc_fatia_aparar(adc_fatia(t.dados + ini, t.tamanho - ini)); if (resto.tamanho) partes[np++] = resto; break; }
        }
    }
    if (np == 1) { a->primeiro_nome = partes[0]; }
    else if (np == 2) { a->primeiro_nome = partes[0]; a->ultimo_nome = partes[1]; }
    else if (np == 3) { a->primeiro_nome = partes[0]; a->nome_do_meio = partes[1]; a->ultimo_nome = partes[2]; }
    /* underscores em partes → espaços (cópia) */
    ADC_Fatia* ps[3] = { &a->primeiro_nome, &a->nome_do_meio, &a->ultimo_nome };
    for (int k = 0; k < 3; k++) {
        if (adc_fatia_indice(*ps[k], '_') >= 0) { char* c = adc_arena_copiar(pC->arena, ps[k]->dados, ps[k]->tamanho); if (c) { for (int32_t i = 0; i < ps[k]->tamanho; i++) if (c[i] == '_') c[i] = ' '; *ps[k] = adc_fatia(c, ps[k]->tamanho); } }
    }
    if (adc_fatia_indice(a->nome_completo, '_') >= 0) { char* c = adc_arena_copiar(pC->arena, a->nome_completo.dados, a->nome_completo.tamanho); if (c) { for (int32_t i = 0; i < a->nome_completo.tamanho; i++) if (c[i] == '_') c[i] = ' '; a->nome_completo = adc_fatia(c, a->nome_completo.tamanho); } }
    a->iniciais = iniciais(pC, a->primeiro_nome, a->nome_do_meio, a->ultimo_nome);
    return a;
}

static bool parece_revisao(ADC_Fatia pL) {
    pL = adc_fatia_aparar(pL);
    if (pL.tamanho == 0) return false;
    if (pL.dados[0] == 'v' && pL.tamanho > 1 && adc_eh_digito(pL.dados[1])) return true;
    if (adc_eh_digito(pL.dados[0])) return true;  /* "1.0, 2026-10-24" ou data */
    return false;
}

static void ler_revisao(ADC_Contexto* pC, ADC_Fatia pL) {
    ADC_Fatia t = adc_fatia_aparar(pL);
    int32_t dp = adc_fatia_indice(t, ':');
    if (dp >= 0) { pC->cabecalho.revisao_observacao = adc_fatia_aparar(adc_fatia(t.dados + dp + 1, t.tamanho - dp - 1)); t = adc_fatia_aparar(adc_fatia(t.dados, dp)); }
    int32_t v = adc_fatia_indice(t, ',');
    ADC_Fatia num = v >= 0 ? adc_fatia_aparar(adc_fatia(t.dados, v)) : t;
    ADC_Fatia data = v >= 0 ? adc_fatia_aparar(adc_fatia(t.dados + v + 1, t.tamanho - v - 1)) : adc_fatia(NULL, 0);
    if (v < 0 && num.tamanho && !(num.dados[0] == 'v')) {
        /* só número ou só data: se tem '-' ou '/' é data */
        if (adc_fatia_indice(num, '-') >= 0 || adc_fatia_indice(num, '/') >= 0) { data = num; num = adc_fatia(NULL, 0); }
    }
    if (num.tamanho && num.dados[0] == 'v') { num.dados++; num.tamanho--; }
    pC->cabecalho.revisao_numero = num; pC->cabecalho.revisao_data = data;
    if (num.tamanho) definir(pC, "revnumber", num);
    if (data.tamanho) definir(pC, "revdate", data);
    if (pC->cabecalho.revisao_observacao.tamanho) definir(pC, "revremark", pC->cabecalho.revisao_observacao);
}

static void parsear_cabecalho(ADC_Contexto* pC, ADC_Bloco* pDoc) {
    ADC_Leitor* L = &pC->leitor;
    /* pula linhas vazias e comentários iniciais; aceita entradas de atributo antes do título */
    for (;;) {
        const ADC_Linha* l = adc_leitor_espiar(L, 0);
        if (!l) return;
        ADC_Fatia t = l->texto;
        if (adc_fatia_so_espacos(t)) { adc_leitor_pular(L, 1); continue; }
        if (t.tamanho >= 2 && t.dados[0] == '/' && t.dados[1] == '/') {
            if (t.tamanho >= 4 && t.dados[2] == '/' && t.dados[3] == '/') { ADC_Fatia d = adc_fatia_aparar_direita(t); adc_leitor_pular(L, 1); const ADC_Linha* n; while ((n = adc_leitor_espiar(L, 0)) != NULL) { bool f = adc_fatia_igual_f(adc_fatia_aparar_direita(n->texto), d); adc_leitor_pular(L, 1); if (f) break; } }
            else adc_leitor_pular(L, 1);
            continue;
        }
        ADC_Fatia nome, valor; bool remover;
        if (adc_linha_eh_entrada_atributo(t, &nome, &valor, &remover)) { adc_definir_atributo_documento(pC, nome, valor, remover, l->numero); adc_leitor_pular(L, 1); continue; }
        break;
    }
    /* Linhas de atributo de bloco ([id="x"], [[x]], [.papel]) logo antes do título são do DOCUMENTO, como no
       Asciidoctor (<body id="x">). Só se o título vier em seguida; senão ficam para o primeiro bloco do corpo. */
    ADC_Metadados meta_doc; memset(&meta_doc, 0, sizeof meta_doc); meta_doc.vazio = true;
    {
        /* como o parse_block_metadata_lines do Asciidoctor: linhas vazias e comentários entre as linhas de atributo e
           o título não as separam dele */
        int32_t k = 0, atributos = 0;
        const ADC_Linha* p;
        while ((p = adc_leitor_espiar(L, k)) != NULL) {
            ADC_Fatia tx = p->texto;
            if (adc_linha_eh_atributo_bloco(tx)) { atributos++; k++; continue; }
            /* parse_block_metadata_lines também aceita (e aplica) entradas de atributo entre os metadados e o título —
               é o caso de [id="x"] + include::_attributes/...[] + = Título */
            if (atributos > 0 && (adc_fatia_so_espacos(tx) || adc_linha_eh_entrada_atributo(tx, NULL, NULL, NULL) || (tx.tamanho >= 2 && tx.dados[0] == '/' && tx.dados[1] == '/' && !(tx.tamanho >= 4 && tx.dados[2] == '/' && tx.dados[3] == '/')))) { k++; continue; }
            break;
        }
        if (atributos > 0 && p && p->texto.tamanho > 2 && p->texto.dados[0] == '=' && p->texto.dados[1] == ' ') {
            for (int32_t j = 0; j < k; j++) {
                const ADC_Linha* a = adc_leitor_espiar(L, 0);
                ADC_Fatia nome, valor; bool remover;
                if (adc_linha_eh_atributo_bloco(a->texto)) adc_ler_linha_atributos(pC, &meta_doc, a);
                else if (adc_linha_eh_entrada_atributo(a->texto, &nome, &valor, &remover)) adc_definir_atributo_documento(pC, nome, valor, remover, a->numero);
                adc_leitor_pular(L, 1);
            }
        }
    }
    const ADC_Linha* l = adc_leitor_espiar(L, 0);
    if (!l) return;
    ADC_Fatia t = l->texto;
    /* título nível 0: "= Título" (ou "# Título" estilo Markdown) */
    if (!((t.tamanho > 2 && t.dados[0] == '=' && t.dados[1] == ' ') || (t.tamanho > 2 && t.dados[0] == '#' && t.dados[1] == ' '))) return;
    ADC_Fatia titulo = adc_fatia_aparar(adc_fatia(t.dados + 2, t.tamanho - 2));
    /* sufixo " =" opcional */
    if (titulo.tamanho > 2 && titulo.dados[titulo.tamanho - 1] == '=' && adc_eh_espaco(titulo.dados[titulo.tamanho - 2])) titulo = adc_fatia_aparar(adc_fatia(titulo.dados, titulo.tamanho - 1));
    pC->cabecalho.presente = true;
    pC->cabecalho.titulo_cru = titulo;
    pC->cabecalho.local.inicio.linha = l->numero; pC->cabecalho.local.inicio.coluna = 1; pC->cabecalho.local.arquivo = l->arquivo;
    pC->cabecalho.local.fim.linha = l->numero; pC->cabecalho.local.fim.coluna = adc_utf8_contar(adc_fatia_aparar_direita(t));
    int32_t linha_titulo = l->numero, arq = l->arquivo;
    adc_leitor_pular(L, 1);
    /* título: subtítulo após ": " vai para 'subtitle' quando o título é dividido (mantemos o texto inteiro em doctitle) */
    definir(pC, "doctitle", titulo);
    /* linha de autor e de revisão: imediatamente após, sem linha vazia, não sendo entrada de atributo */
    const ADC_Linha* a = adc_leitor_espiar(L, 0);
    if (a && !adc_fatia_so_espacos(a->texto) && !adc_linha_eh_entrada_atributo(a->texto, NULL, NULL, NULL) && !(a->texto.tamanho > 1 && a->texto.dados[0] == '=' )) {
        /* autores separados por ';' */
        ADC_Fatia at = a->texto;
        ADC_Autor* ult = NULL; int32_t idx = 0; int32_t ini = 0;
        for (int32_t i = 0; i <= at.tamanho; i++) {
            if (i == at.tamanho || at.dados[i] == ';') {
                ADC_Fatia um = adc_fatia_aparar(adc_fatia(at.dados + ini, i - ini));
                if (um.tamanho) {
                    ADC_Autor* au = ler_autor(pC, um);
                    if (au) { if (ult) ult->proximo = au; else pC->cabecalho.autores = au; ult = au; idx++;
                        if (idx == 1) { definir(pC, "author", au->nome_completo); definir(pC, "firstname", au->primeiro_nome); definir(pC, "lastname", au->ultimo_nome); definir(pC, "middlename", au->nome_do_meio); definir(pC, "authorinitials", au->iniciais); if (au->email.tamanho) definir(pC, "email", au->email); }
                        else { char tmp[32]; int n = snprintf(tmp, sizeof tmp, "author_%d", idx); definir(pC, adc_arena_copiar(pC->arena, tmp, n), au->nome_completo); }
                    }
                }
                ini = i + 1;
            }
        }
        if (idx > 0) { char tmp[8]; int n = snprintf(tmp, sizeof tmp, "%d", idx); definir(pC, "authorcount", adc_fatia(adc_arena_copiar(pC->arena, tmp, n), n)); }
        pC->cabecalho.local.fim.linha = a->numero; pC->cabecalho.local.fim.coluna = adc_utf8_contar(adc_fatia_aparar_direita(a->texto));
        adc_leitor_pular(L, 1);
        const ADC_Linha* r = adc_leitor_espiar(L, 0);
        if (r && !adc_fatia_so_espacos(r->texto) && !adc_linha_eh_entrada_atributo(r->texto, NULL, NULL, NULL) && parece_revisao(r->texto)) {
            ler_revisao(pC, r->texto);
            pC->cabecalho.local.fim.linha = r->numero; pC->cabecalho.local.fim.coluna = adc_utf8_contar(adc_fatia_aparar_direita(r->texto));
            adc_leitor_pular(L, 1);
        }
    }
    /* entradas de atributo do cabeçalho até a linha vazia */
    for (;;) {
        const ADC_Linha* e = adc_leitor_espiar(L, 0);
        if (!e) break;
        if (adc_fatia_so_espacos(e->texto)) break;
        ADC_Fatia nome, valor; bool remover;
        if (adc_linha_eh_entrada_atributo(e->texto, &nome, &valor, &remover)) {
            ADC_Linha l0 = *e; adc_leitor_pular(L, 1);
            if (valor.tamanho > 0 && valor.dados[valor.tamanho - 1] == '\\') {
                ADC_Buffer b; adc_buffer_iniciar(&b); adc_buffer_bytes(&b, valor.dados, valor.tamanho - 1);
                const ADC_Linha* N;
                while ((N = adc_leitor_espiar(L, 0)) != NULL) { ADC_Fatia nt = adc_fatia_aparar(N->texto); if (nt.tamanho == 0) break; bool cont = nt.dados[nt.tamanho - 1] == '\\'; adc_buffer_char(&b, ' '); adc_buffer_bytes(&b, nt.dados, cont ? nt.tamanho - 1 : nt.tamanho); adc_leitor_pular(L, 1); if (!cont) break; }
                int32_t tam; uint8_t* d = adc_buffer_entregar(&b, &tam);
                if (d) { char* c = adc_arena_copiar(pC->arena, (const char*)d, tam); free(d); if (c) valor = adc_fatia(c, tam); }
            }
            adc_definir_atributo_documento(pC, nome, valor, remover, l0.numero);
            pC->cabecalho.local.fim.linha = l0.numero; pC->cabecalho.local.fim.coluna = adc_utf8_contar(adc_fatia_aparar_direita(l0.texto));
            continue;
        }
        if (e->texto.tamanho >= 2 && e->texto.dados[0] == '/' && e->texto.dados[1] == '/') { adc_leitor_pular(L, 1); continue; }
        break;
    }
    /* o título é parseado depois dos atributos do cabeçalho (eles podem ser referenciados nele) */
    pC->cabecalho.titulo = adc_parsear_texto_inline(pC, titulo, linha_titulo, 3, arq, ADC_SUBS_TITULO);
    pDoc->titulo = pC->cabecalho.titulo;
    pDoc->nivel = 0;
    if (!meta_doc.vazio) {
        pDoc->meta = meta_doc;
        if (meta_doc.id.tamanho) adc_ctx_registrar_id(pC, meta_doc.id, pDoc->titulo, pDoc);
    }
}

void adc_doc_parsear(ADC_Contexto* pC, ADC_Fatia pTexto, ADC_Fatia pCaminho) {
    ADC_Bloco* doc = ADC_NOVO(pC->arena, ADC_Bloco);
    if (!doc) return;
    doc->tipo = ADC_BL_DOCUMENTO;
    doc->local.inicio.linha = 1; doc->local.inicio.coluna = 1;
    pC->documento = doc;
    adc_leitor_abrir(&pC->leitor, pTexto, pCaminho);
    parsear_cabecalho(pC, doc);
    pC->toc_ativo = adc_atribs_obter_cstr(&pC->atribs, "toc", &pC->toc_valor);
    pC->toc_titulo = adc_atribs_valor_ou(&pC->atribs, "toc-title", "Table of Contents");
    pC->toc_niveis = adc_atribs_valor_ou(&pC->atribs, "toclevels", "2");
    { ADC_Fatia ic; pC->icons_cabecalho = adc_atribs_obter_cstr(&pC->atribs, "icons", &ic) ? (adc_fatia_igual(ic, "font") ? 1 : 2) : 0; }
    ADC_Escopo e; memset(&e, 0, sizeof e);
    e.permite_secao = true; e.nivel_secao = 0;
    adc_blocos_parsear(pC, doc, &e);
    /* rede de segurança: entrada que sobrou sem ser processada vira erro com a linha — nunca descarte mudo */
    {
        const ADC_Linha* resto = adc_leitor_espiar(&pC->leitor, 0);
        if (resto) {
            int32_t linha = resto->numero;   /* o ponteiro deixa de valer ao avançar o leitor */
            int32_t n = 0; while (adc_leitor_espiar(&pC->leitor, 0) != NULL) { adc_leitor_pular(&pC->leitor, 1); n++; }
            adc_msgs_adicionar(&pC->msgs, ADC_NIVEL_ERRO, linha, 1, "O conteúdo a partir desta linha (%d linha(s)) não pôde ser estruturado e ficou fora da saída. Isto é um defeito do conversor: informe o trecho.", (int)n);
        }
    }
    /* fim do documento = fim do último bloco ou do cabeçalho */
    if (doc->ultimo_filho) doc->local.fim = doc->ultimo_filho->local.fim;
    else if (pC->cabecalho.presente) doc->local.fim = pC->cabecalho.local.fim;
    else { doc->local.fim.linha = 1; doc->local.fim.coluna = 0; }
    if (pC->cabecalho.presente && pC->cabecalho.local.inicio.linha < doc->local.inicio.linha) doc->local.inicio = pC->cabecalho.local.inicio;
    if (doc->filhos && !pC->cabecalho.presente) doc->local.inicio = doc->filhos->local.inicio;
    /* seções sem id ganham id sintético — já feito no parse; notas de rodapé ficam em pC->notas */
}

ADC_Inline* adc_doc_parsear_inlines(ADC_Contexto* pC, ADC_Fatia pTexto) {
    /* normaliza CRLF → LF (o leitor de blocos já faz isso linha a linha) */
    bool tem_cr = false;
    for (int32_t i = 0; i < pTexto.tamanho; i++) if (pTexto.dados[i] == '\r') { tem_cr = true; break; }
    if (tem_cr) {
        char* c = (char*)adc_arena_alocar(pC->arena, (size_t)pTexto.tamanho + 1);
        if (c) { int32_t o = 0; for (int32_t i = 0; i < pTexto.tamanho; i++) if (pTexto.dados[i] != '\r' || (i + 1 < pTexto.tamanho && pTexto.dados[i + 1] != '\n')) c[o++] = pTexto.dados[i]; pTexto = adc_fatia(c, o); }
    }
    /* divide em linhas para construir os segmentos */
    int32_t n = 1; for (int32_t i = 0; i < pTexto.tamanho; i++) if (pTexto.dados[i] == '\n') n++;
    ADC_Segmento* segs = ADC_NOVOS(pC->arena, ADC_Segmento, n);
    if (!segs) return NULL;
    int32_t k = 0, ini = 0;
    for (int32_t i = 0; i <= pTexto.tamanho; i++) {
        if (i == pTexto.tamanho || pTexto.dados[i] == '\n') { segs[k].inicio = ini; segs[k].linha = k + 1; segs[k].coluna = 1; segs[k].arquivo = 0; k++; ini = i + 1; }
    }
    int32_t* mapa = NULL;
    ADC_Fatia resolvido = adc_atribs_resolver(&pC->atribs, pTexto, &pC->msgs, 1, true, &mapa);
    ADC_Origem o; o.segmentos = segs; o.quantidade = n; o.mapa = resolvido.dados == pTexto.dados ? NULL : mapa; o.tamanho_original = pTexto.tamanho;
    return adc_inlines_parsear(pC, resolvido, &o, ADC_SUBS_NORMAL);
}
