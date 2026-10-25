/*
 * Tabelas: |=== (PSV), ,=== (CSV), :=== (DSV), !=== (PSV alternativo).
 * Células acumulam em sequência e são distribuídas em linhas pelo número de colunas (cols= ou a 1ª linha).
 */
#include "adc_blocos.h"
#include "adc_attrlist.h"
#include <stdlib.h>
#include <stdio.h>

typedef struct CelulaCrua {
    ADC_Fatia   spec;        /* "2+", ".2+", "^.>", "a", "h" ... (antes do separador)             */
    ADC_Fatia   texto;       /* conteúdo bruto (pode ter várias linhas)                            */
    int32_t     linha;
    int32_t     coluna;
    int32_t     arquivo;
    bool        fim_de_linha_fonte;   /* esta célula era a última da sua linha de texto            */
} CelulaCrua;

static ADC_Alinhamento alinh_h(char c) { return c == '<' ? ADC_ALINHA_ESQUERDA : c == '^' ? ADC_ALINHA_CENTRO : c == '>' ? ADC_ALINHA_DIREITA : ADC_ALINHA_PADRAO; }
static ADC_Alinhamento alinh_v(char c) { return c == '<' ? ADC_ALINHA_TOPO : c == '^' ? ADC_ALINHA_MEIO : c == '>' ? ADC_ALINHA_BASE : ADC_ALINHA_PADRAO; }

/* Lê uma spec de coluna ou de célula: [mult*][span+][.vspan+][h-align][.v-align][estilo] */
static void ler_spec(ADC_Fatia pS, int32_t* pMult, int32_t* pColspan, int32_t* pRowspan, ADC_Alinhamento* pH, ADC_Alinhamento* pV, char* pEstilo, int32_t* pLargura, bool* pPercent, bool* pAuto) {
    int32_t i = 0; int32_t n = pS.tamanho;
    *pMult = 1; *pColspan = 1; *pRowspan = 1; *pH = ADC_ALINHA_PADRAO; *pV = ADC_ALINHA_PADRAO; *pEstilo = 'd';
    if (pLargura) { *pLargura = 1; *pPercent = false; *pAuto = false; }
    /* número inicial: pode ser mult (seguido de *), colspan (seguido de +), ou largura (fim/%/alinhamento) */
    int32_t num = 0; bool tem_num = false;
    while (i < n && adc_eh_digito(pS.dados[i])) { num = num * 10 + (pS.dados[i] - '0'); i++; tem_num = true; }
    if (tem_num && i < n && pS.dados[i] == '*') { *pMult = num; i++; tem_num = false; num = 0; while (i < n && adc_eh_digito(pS.dados[i])) { num = num * 10 + (pS.dados[i] - '0'); i++; tem_num = true; } }
    if (tem_num && i < n && pS.dados[i] == '+') { *pColspan = num; i++; tem_num = false; }
    else if (tem_num && i < n && pS.dados[i] == '.' && i + 1 < n && adc_eh_digito(pS.dados[i + 1])) {
        /* colspan.rowspan+ */
        int32_t j = i + 1; int32_t r = 0; while (j < n && adc_eh_digito(pS.dados[j])) { r = r * 10 + (pS.dados[j] - '0'); j++; }
        if (j < n && pS.dados[j] == '+') { *pColspan = num; *pRowspan = r; i = j + 1; tem_num = false; }
    }
    if (i < n && pS.dados[i] == '.' && i + 1 < n && adc_eh_digito(pS.dados[i + 1])) {
        int32_t j = i + 1; int32_t r = 0; while (j < n && adc_eh_digito(pS.dados[j])) { r = r * 10 + (pS.dados[j] - '0'); j++; }
        if (j < n && pS.dados[j] == '+') { *pRowspan = r; i = j + 1; }
    }
    if (tem_num) {
        if (pLargura) { *pLargura = num; if (i < n && pS.dados[i] == '%') { *pPercent = true; i++; } }
    }
    if (i < n && (pS.dados[i] == '<' || pS.dados[i] == '^' || pS.dados[i] == '>')) { *pH = alinh_h(pS.dados[i]); i++; }
    if (i < n && pS.dados[i] == '.' && i + 1 < n && (pS.dados[i + 1] == '<' || pS.dados[i + 1] == '^' || pS.dados[i + 1] == '>')) { *pV = alinh_v(pS.dados[i + 1]); i += 2; }
    /* ColumnSpecRx do Asciidoctor: [mult*][h-align][.v-align][largura|~][estilo] — a largura vem depois do
       alinhamento e ANTES do estilo (".^3a" = meio, largura 3, AsciiDoc) */
    if (i < n && pS.dados[i] == '~') { if (pAuto) *pAuto = true; i++; }
    if (i < n && adc_eh_digito(pS.dados[i])) {
        int32_t w = 0; while (i < n && adc_eh_digito(pS.dados[i])) { w = w * 10 + (pS.dados[i] - '0'); i++; }
        if (pLargura) *pLargura = w;
        if (i < n && pS.dados[i] == '%') { if (pPercent) *pPercent = true; i++; }
    }
    if (i < n && (pS.dados[i] == 'a' || pS.dados[i] == 'd' || pS.dados[i] == 'e' || pS.dados[i] == 'h' || pS.dados[i] == 'l' || pS.dados[i] == 'm' || pS.dados[i] == 's' || pS.dados[i] == 'v')) { *pEstilo = pS.dados[i]; i++; }
}

/* Spec de CÉLULA exatamente como CellSpecStartRx/CellSpecEndRx do Asciidoctor:
   (?:(\d+(?:\.\d*)?|(?:\d*\.)?\d+)([*+]))?([<^>](?:\.[<^>]?)?|(?:[<^>]?\.)?[<^>])?([a-z])?
   Número só vale com '*' ou '+' (span/duplicação): "iDRAC 10|" é conteúdo, "2+|" é spec. */
static bool eh_alinh(char c) { return c == '<' || c == '^' || c == '>'; }
static bool spec_celula_valida(ADC_Fatia pS) {
    const char* s = pS.dados; int32_t n = pS.tamanho;
    if (n == 0) return false;
    int32_t i = 0;
    /* grupo span/duplicação: dígitos [. dígitos] seguidos de * ou + */
    {
        int32_t j = 0, d1 = 0, d2 = 0;
        while (j < n && adc_eh_digito(s[j])) { j++; d1++; }
        if (j < n && s[j] == '.') { int32_t k = j + 1; while (k < n && adc_eh_digito(s[k])) { k++; d2++; } if (k < n && (s[k] == '*' || s[k] == '+') && (d1 > 0 || d2 > 0)) j = k; }
        if ((d1 > 0 || d2 > 0) && j < n && (s[j] == '*' || s[j] == '+')) i = j + 1;
    }
    /* alinhamentos possíveis a partir de i: nada, A, A., A.B, .B (A, B em <^>) — tenta cada um com a letra final */
    int32_t fins[5]; int32_t nf = 0;
    fins[nf++] = i;
    if (i < n && eh_alinh(s[i])) {
        fins[nf++] = i + 1;
        if (i + 1 < n && s[i + 1] == '.') { fins[nf++] = i + 2; if (i + 2 < n && eh_alinh(s[i + 2])) fins[nf++] = i + 3; }
    } else if (i < n && s[i] == '.' && i + 1 < n && eh_alinh(s[i + 1])) fins[nf++] = i + 2;
    for (int32_t k = 0; k < nf; k++) {
        int32_t f = fins[k];
        if (f == n && f > 0) return true;                                  /* sem letra */
        if (f + 1 == n && s[f] >= 'a' && s[f] <= 'z') return true;         /* letra de estilo */
    }
    return false;
}

static ADC_Coluna* ler_cols(ADC_Contexto* pC, ADC_Fatia pCols, int32_t* pN) {
    ADC_Coluna tmp[256]; int32_t n = 0;
    int32_t ini = 0;
    for (int32_t i = 0; i <= pCols.tamanho; i++) {
        if (i == pCols.tamanho || pCols.dados[i] == ',' || pCols.dados[i] == ';') {
            ADC_Fatia s = adc_fatia_aparar(adc_fatia(pCols.dados + ini, i - ini));
            ini = i + 1;
            int32_t mult, cs, rs, w; ADC_Alinhamento h, v; char est; bool pct, aut;
            ler_spec(s, &mult, &cs, &rs, &h, &v, &est, &w, &pct, &aut);
            for (int32_t k = 0; k < mult && n < 256; k++) {
                tmp[n].largura = w > 0 ? w : 1; tmp[n].percentual = pct; tmp[n].automatica = aut; tmp[n].horizontal = h; tmp[n].vertical = v; tmp[n].estilo = est; n++;
            }
        }
    }
    if (n == 0) { *pN = 0; return NULL; }
    ADC_Coluna* cols = ADC_NOVOS(pC->arena, ADC_Coluna, n);
    if (cols) memcpy(cols, tmp, sizeof(ADC_Coluna) * (size_t)n);
    *pN = n;
    return cols;
}

/* Divide uma linha CSV/DSV em campos (com aspas duplas no CSV). */
static int32_t dividir_csv(ADC_Contexto* pC, ADC_Fatia pL, char pSep, CelulaCrua* pSaida, int32_t pMax, const ADC_Linha* pLinha) {
    int32_t n = 0; int32_t i = 0;
    while (i <= pL.tamanho && n < pMax) {
        ADC_Fatia campo;
        if (pSep == ',' && i < pL.tamanho && pL.dados[i] == '"') {
            ADC_Buffer b; adc_buffer_iniciar(&b);
            int32_t k = i + 1;
            while (k < pL.tamanho) {
                if (pL.dados[k] == '"') { if (k + 1 < pL.tamanho && pL.dados[k + 1] == '"') { adc_buffer_char(&b, '"'); k += 2; continue; } k++; break; }
                adc_buffer_char(&b, pL.dados[k]); k++;
            }
            int32_t tam; uint8_t* d = adc_buffer_entregar(&b, &tam);
            char* c = d ? adc_arena_copiar(pC->arena, (const char*)d, tam) : NULL; free(d);
            campo = c ? adc_fatia(c, tam) : adc_fatia("", 0);
            while (k < pL.tamanho && pL.dados[k] != pSep) k++;
            i = k;
        } else {
            int32_t k = i; while (k < pL.tamanho && pL.dados[k] != pSep) k++;
            campo = adc_fatia_aparar(adc_fatia(pL.dados + i, k - i));
            i = k;
        }
        pSaida[n].spec = adc_fatia(NULL, 0); pSaida[n].texto = campo; pSaida[n].linha = pLinha->numero; pSaida[n].coluna = 1; pSaida[n].arquivo = pLinha->arquivo; pSaida[n].fim_de_linha_fonte = false;
        n++;
        if (i >= pL.tamanho) break;
        i++; /* separador */
        if (i == pL.tamanho) { /* separador no fim: campo vazio final */ pSaida[n].spec = adc_fatia(NULL, 0); pSaida[n].texto = adc_fatia("", 0); pSaida[n].linha = pLinha->numero; pSaida[n].coluna = 1; pSaida[n].arquivo = pLinha->arquivo; pSaida[n].fim_de_linha_fonte = false; n++; break; }
    }
    if (n > 0) pSaida[n - 1].fim_de_linha_fonte = true;
    return n;
}

static ADC_Bloco* celula_de(ADC_Contexto* pC, const CelulaCrua* pCel, const ADC_Coluna* pCol, bool pCabecalho, const ADC_Escopo* pE) {
    ADC_Bloco* cel = ADC_NOVO(pC->arena, ADC_Bloco);
    if (!cel) return NULL;
    cel->tipo = ADC_BL_CELULA;
    cel->local.inicio.linha = pCel->linha; cel->local.inicio.coluna = pCel->coluna; cel->local.arquivo = pCel->arquivo;
    cel->local.fim = cel->local.inicio;
    int32_t mult, cs, rs; ADC_Alinhamento h, v; char est;
    ler_spec(pCel->spec, &mult, &cs, &rs, &h, &v, &est, NULL, NULL, NULL);
    cel->colspan = cs; cel->rowspan = rs;
    cel->alinha_h = h != ADC_ALINHA_PADRAO ? h : (pCol ? pCol->horizontal : ADC_ALINHA_PADRAO);
    cel->alinha_v = v != ADC_ALINHA_PADRAO ? v : (pCol ? pCol->vertical : ADC_ALINHA_PADRAO);
    char estilo = est != 'd' ? est : (pCol ? pCol->estilo : 'd');
    if (pCabecalho) estilo = 'h';
    cel->estilo_celula = estilo;
    ADC_Fatia texto = adc_fatia_aparar(pCel->texto);
    /* remove linhas vazias iniciais/finais mantendo as internas */
    while (texto.tamanho > 0 && (texto.dados[0] == '\n')) { texto.dados++; texto.tamanho--; }
    while (texto.tamanho > 0 && (texto.dados[texto.tamanho - 1] == '\n' || adc_eh_espaco(texto.dados[texto.tamanho - 1]))) texto.tamanho--;
    cel->texto_cru = texto;
    if (estilo == 'a') {
        ADC_Escopo e; memset(&e, 0, sizeof e); (void)pE;
        adc_blocos_parsear_texto(pC, cel, texto, pCel->linha, pCel->arquivo);
    } else if (estilo == 'l' || estilo == 'v') {
        cel->inlines = adc_parsear_texto_inline(pC, texto, pCel->linha, pCel->coluna, pCel->arquivo, estilo == 'l' ? ADC_SUBS_VERBATIM : ADC_SUBS_NORMAL);
    } else {
        /* parágrafos separados por linha vazia → cada um vira um PARAGRAFO filho; um só → inlines diretos */
        int32_t ini = 0; int32_t npar = 0;
        for (int32_t i = 0; i <= texto.tamanho; i++) {
            bool quebra = (i == texto.tamanho) || (texto.dados[i] == '\n' && i + 1 < texto.tamanho && texto.dados[i + 1] == '\n');
            if (quebra) {
                ADC_Fatia par = adc_fatia_aparar(adc_fatia(texto.dados + ini, i - ini));
                while (par.tamanho > 0 && par.dados[0] == '\n') { par.dados++; par.tamanho--; }
                if (par.tamanho > 0) {
                    ADC_Inline* in = adc_parsear_texto_inline(pC, par, pCel->linha, pCel->coluna, pCel->arquivo, ADC_SUBS_NORMAL);
                    if (estilo == 'e' || estilo == 's' || estilo == 'm') {
                        ADC_Inline* w = ADC_NOVO(pC->arena, ADC_Inline);
                        if (w) { w->tipo = ADC_IN_SPAN; w->variante_span = estilo == 'e' ? ADC_SPAN_EMPHASIS : estilo == 's' ? ADC_SPAN_STRONG : ADC_SPAN_CODE; w->filhos = in; in = w; }
                    }
                    ADC_Bloco* p = ADC_NOVO(pC->arena, ADC_Bloco);
                    if (p) { p->tipo = ADC_BL_PARAGRAFO; p->forma = ADC_FORMA_PARAGRAFO; p->inlines = in; p->local = cel->local; adc_bloco_anexar(cel, p); npar++; }
                }
                while (i < texto.tamanho && texto.dados[i] == '\n') i++;
                ini = i;
            }
        }
        if (npar == 1) { cel->inlines = cel->filhos->inlines; cel->filhos = NULL; cel->ultimo_filho = NULL; cel->quantidade_filhos = 0; }
    }
    return cel;
}

ADC_Bloco* adc_tabela_parsear(ADC_Contexto* pC, ADC_Bloco* pPai, ADC_Metadados* pMetaPendente, ADC_Fatia pDelim, const ADC_Linha* pAbertura) {
    ADC_Metadados meta_copia; memset(&meta_copia, 0, sizeof meta_copia); meta_copia.vazio = true;
    ADC_Metadados* pMeta = &meta_copia;
    if (pMetaPendente) { meta_copia = *pMetaPendente; memset(pMetaPendente, 0, sizeof *pMetaPendente); pMetaPendente->vazio = true; }
    char sep = pDelim.dados[0] == '|' ? '|' : pDelim.dados[0] == ',' ? ',' : pDelim.dados[0] == ':' ? ':' : '!';
    ADC_Fatia formato = pMeta ? adc_atributo_obter(pMeta->atributos, "format", NULL) : adc_fatia(NULL, 0);
    if (adc_fatia_igual(formato, "csv")) sep = ',';
    else if (adc_fatia_igual(formato, "dsv")) sep = ':';
    if (pMeta) { bool a; ADC_Fatia s = adc_atributo_obter(pMeta->atributos, "separator", &a); if (a && s.tamanho == 1) sep = s.dados[0]; }
    bool csv = sep == ',' || sep == ':' || (pMeta && adc_fatia_igual(formato, "csv")) || (pMeta && adc_fatia_igual(formato, "dsv"));
    bool psv = !csv;

    ADC_Bloco* tb = ADC_NOVO(pC->arena, ADC_Bloco);
    if (!tb) return NULL;
    tb->tipo = ADC_BL_TABELA; tb->forma = ADC_FORMA_DELIMITADA; tb->delimitador = pDelim;
    { ADC_Fatia ic; if (adc_atribs_obter_cstr(&pC->atribs, "icons", &ic)) tb->icons_estado = adc_fatia_igual(ic, "font") ? 1 : 2; }   /* :icons: neste ponto */
    tb->local.inicio.linha = pAbertura->numero; tb->local.inicio.coluna = 1; tb->local.arquivo = pAbertura->arquivo;
    tb->pai = pPai;
    ADC_Tabela* T = ADC_NOVO(pC->arena, ADC_Tabela);
    if (!T) return NULL;
    tb->tabela = T;
    T->separador = sep;
    if (pMeta) {
        bool a;
        ADC_Fatia cols = adc_atributo_obter(pMeta->atributos, "cols", &a);
        if (a) T->colunas = ler_cols(pC, cols, &T->quantidade_colunas);
        ADC_Fatia w = adc_atributo_obter(pMeta->atributos, "width", &a);
        if (a) { ADC_Fatia ww = w; if (ww.tamanho && ww.dados[ww.tamanho - 1] == '%') ww.tamanho--; T->largura_percentual = adc_fatia_para_int(ww, 0); }
        T->molduras = adc_atributo_obter(pMeta->atributos, "frame", &a);
        T->grade = adc_atributo_obter(pMeta->atributos, "grid", &a);
        if (adc_lista_fatias_contem(&pMeta->opcoes, "header")) T->tem_cabecalho = true;
        if (adc_lista_fatias_contem(&pMeta->opcoes, "footer")) T->tem_rodape = true;
        if (adc_lista_fatias_contem(&pMeta->opcoes, "autowidth")) T->autowidth = true;
        if (adc_lista_fatias_contem(&pMeta->opcoes, "noheader")) T->tem_cabecalho = false;
    }
    bool header_explicito = T->tem_cabecalho || (pMeta && adc_lista_fatias_contem(&pMeta->opcoes, "noheader"));

    /* coleta células cruas */
    int32_t cap = 256, n = 0;
    CelulaCrua* cels = (CelulaCrua*)malloc(sizeof(CelulaCrua) * (size_t)cap);
    if (!cels) return NULL;
    ADC_Buffer acum; adc_buffer_iniciar(&acum); /* conteúdo da célula aberta (PSV, multi-linha) */
    bool celula_aberta = false; CelulaCrua aberta; memset(&aberta, 0, sizeof aberta);
    bool primeira_linha_feita = false; int32_t cels_na_primeira_linha = 0; bool vazia_apos_primeira = false;
    bool fechada = false; ADC_Linha ultima = *pAbertura;
    const ADC_Linha* L;
    int32_t linhas_lidas = 0;
    while ((L = adc_leitor_espiar(&pC->leitor, 0)) != NULL) {
        ADC_Fatia t = L->texto;
        if (adc_fatia_igual_f(adc_fatia_aparar_direita(t), pDelim)) { ultima = *L; adc_leitor_pular(&pC->leitor, 1); fechada = true; break; }
        ultima = *L;
        adc_leitor_pular(&pC->leitor, 1);
        /* o Asciidoctor lê a tabela com skip_line_comments: linha "//..." (não "///") some, até dentro de célula */
        if (t.tamanho >= 2 && t.dados[0] == '/' && t.dados[1] == '/' && !(t.tamanho >= 3 && t.dados[2] == '/')) continue;
        linhas_lidas++;
        if (csv) {
            if (adc_fatia_so_espacos(t)) { if (primeira_linha_feita && linhas_lidas == 2) vazia_apos_primeira = true; continue; }
            if (n + 256 > cap) { cap *= 2; CelulaCrua* nc = (CelulaCrua*)realloc(cels, sizeof(CelulaCrua) * (size_t)cap); if (!nc) break; cels = nc; }
            int32_t k = dividir_csv(pC, t, sep, cels + n, 256, L);
            if (!primeira_linha_feita) { primeira_linha_feita = true; cels_na_primeira_linha = k; }
            n += k;
            continue;
        }
        /* PSV: percorre a linha procurando separadores não escapados */
        if (adc_fatia_so_espacos(t) && primeira_linha_feita && linhas_lidas == 2) vazia_apos_primeira = true;
        if (adc_fatia_so_espacos(t) && celula_aberta) { adc_buffer_char(&acum, '\n'); continue; }
        if (adc_fatia_so_espacos(t)) continue;
        int32_t i = 0; int32_t ini_seg = 0; bool algum_sep = false;
        while (i < t.tamanho) {
            char c = t.dados[i];
            if (c == '\\' && i + 1 < t.tamanho && t.dados[i + 1] == sep) { i += 2; continue; }
            if (c == sep) {
                /* o que vem antes: se há célula aberta, fecha-a com o texto; a spec da próxima é o trecho
                   imediatamente antes do separador (sem espaços) se for spec válida */
                ADC_Fatia antes = adc_fatia(t.dados + ini_seg, i - ini_seg);
                /* separa spec: sufixo de 'antes' sem espaços que seja spec válida */
                int32_t s = antes.tamanho; while (s > 0 && !adc_eh_espaco(antes.dados[s - 1])) s--;
                ADC_Fatia spec = adc_fatia(antes.dados + s, antes.tamanho - s);
                ADC_Fatia conteudo_antes = adc_fatia(antes.dados, s);
                if (spec.tamanho > 0 && !spec_celula_valida(spec)) {
                    /* não é spec: tudo é conteúdo (|`failureDomains`| e "iDRAC 10|" — o trecho inteiro é a célula) */
                    conteudo_antes = antes; spec = adc_fatia(NULL, 0);
                } else if (spec.tamanho > 0 && s == 0 && ini_seg > 0) {
                    /* no meio da linha a spec precisa de espaço antes (CellSpecEndRx: [ \t]+spec$): |x|a| é conteúdo */
                    conteudo_antes = antes; spec = adc_fatia(NULL, 0);
                }
                if (celula_aberta) {
                    if (acum.tamanho > 0 && conteudo_antes.tamanho > 0) adc_buffer_char(&acum, ' ');
                    adc_buffer_fatia(&acum, conteudo_antes);
                    int32_t tam; uint8_t* d = adc_buffer_entregar(&acum, &tam);
                    char* cp = d ? adc_arena_copiar(pC->arena, (const char*)d, tam) : NULL; free(d);
                    aberta.texto = cp ? adc_fatia(cp, tam) : adc_fatia("", 0);
                    if (n >= cap) { cap *= 2; CelulaCrua* nc = (CelulaCrua*)realloc(cels, sizeof(CelulaCrua) * (size_t)cap); if (!nc) break; cels = nc; }
                    cels[n++] = aberta;
                } else if (!primeira_linha_feita && conteudo_antes.tamanho > 0 && !adc_fatia_so_espacos(conteudo_antes)) {
                    /* texto antes do primeiro separador da tabela: ignorado (Asciidoctor também ignora) */
                }
                /* abre a próxima */
                memset(&aberta, 0, sizeof aberta);
                aberta.spec = spec; aberta.linha = L->numero; aberta.coluna = i + 2; aberta.arquivo = L->arquivo;
                celula_aberta = true;
                adc_buffer_iniciar(&acum);
                ini_seg = i + 1;
                algum_sep = true;
                i++;
                continue;
            }
            i++;
        }
        /* resto da linha pertence à célula aberta */
        if (celula_aberta) {
            ADC_Fatia resto = adc_fatia(t.dados + ini_seg, t.tamanho - ini_seg);
            /* remove escapes "\|" */
            if (acum.tamanho > 0 || algum_sep) { if (acum.tamanho > 0) adc_buffer_char(&acum, '\n'); }
            for (int32_t k = 0; k < resto.tamanho; k++) { if (resto.dados[k] == '\\' && k + 1 < resto.tamanho && resto.dados[k + 1] == sep) { adc_buffer_char(&acum, sep); k++; } else adc_buffer_char(&acum, resto.dados[k]); }
            if (algum_sep && !primeira_linha_feita) { primeira_linha_feita = true; cels_na_primeira_linha = n + 1; }
            if (algum_sep) { cels[n > 0 ? n - 1 : 0].fim_de_linha_fonte = false; }
        }
    }
    if (celula_aberta) {
        int32_t tam; uint8_t* d = adc_buffer_entregar(&acum, &tam);
        char* cp = d ? adc_arena_copiar(pC->arena, (const char*)d, tam) : NULL; free(d);
        aberta.texto = cp ? adc_fatia(cp, tam) : adc_fatia("", 0);
        if (n >= cap) { cap *= 2; CelulaCrua* nc = (CelulaCrua*)realloc(cels, sizeof(CelulaCrua) * (size_t)cap); if (nc) cels = nc; }
        if (n < cap) cels[n++] = aberta;
    }
    adc_buffer_liberar(&acum);
    if (!fechada) adc_msgs_adicionar(&pC->msgs, ADC_NIVEL_AVISO, pAbertura->numero, 1, "Tabela aberta na linha %d não foi fechada com '%.*s'; foi encerrada no fim do documento.", pAbertura->numero, pDelim.tamanho, pDelim.dados);
    tb->local.fim.linha = ultima.numero; tb->local.fim.coluna = adc_utf8_contar(adc_fatia_aparar_direita(ultima.texto));

    /* número de colunas */
    if (T->quantidade_colunas == 0) {
        int32_t ncol = 0;
        if (cels_na_primeira_linha > 0) { for (int32_t k = 0; k < cels_na_primeira_linha && k < n; k++) { int32_t mult, cs, rs; ADC_Alinhamento h, v; char est; ler_spec(cels[k].spec, &mult, &cs, &rs, &h, &v, &est, NULL, NULL, NULL); ncol += cs; } }
        if (ncol == 0) ncol = n > 0 ? n : 1;
        T->quantidade_colunas = ncol;
        T->colunas = ADC_NOVOS(pC->arena, ADC_Coluna, ncol);
        if (T->colunas) for (int32_t k = 0; k < ncol; k++) { T->colunas[k].largura = 1; T->colunas[k].estilo = 'd'; }
        if (!header_explicito && psv && cels_na_primeira_linha > 0 && vazia_apos_primeira) T->tem_cabecalho = true;
        if (!header_explicito && csv && vazia_apos_primeira) T->tem_cabecalho = true;
    } else if (!header_explicito && vazia_apos_primeira && cels_na_primeira_linha == T->quantidade_colunas) T->tem_cabecalho = true;
    /* Asciidoctor: com cols explícito, a primeira linha com todas as células seguida de linha vazia é header */

    /* distribui em linhas */
    ADC_Escopo e; memset(&e, 0, sizeof e);
    int32_t ncol = T->quantidade_colunas;
    int32_t* ocupadas = (int32_t*)calloc((size_t)ncol, sizeof(int32_t)); /* rowspans pendentes por coluna */
    ADC_LinhaTabela* ultima_linha = NULL;
    int32_t idx = 0; int32_t nlin = 0;
    while (idx < n && ocupadas) {
        ADC_LinhaTabela* linha = ADC_NOVO(pC->arena, ADC_LinhaTabela);
        if (!linha) break;
        bool cab = (nlin == 0 && T->tem_cabecalho);
        linha->cabecalho = cab;
        int32_t col = 0; ADC_Bloco* ult_cel = NULL;
        while (col < ncol) {
            if (ocupadas[col] > 0) { ocupadas[col]--; col++; continue; }
            if (idx >= n) break;
            ADC_Bloco* cel = celula_de(pC, &cels[idx], &T->colunas[col], cab, &e);
            idx++;
            if (!cel) break;
            if (cel->colspan > ncol - col) cel->colspan = ncol - col;
            for (int32_t k = 0; k < cel->colspan; k++) ocupadas[col + k] = cel->rowspan - 1;
            col += cel->colspan;
            cel->pai = tb;
            if (ult_cel) ult_cel->proximo = cel; else linha->celulas = cel;
            ult_cel = cel;
        }
        if (ultima_linha) ultima_linha->proxima = linha; else T->linhas = linha;
        ultima_linha = linha;
        nlin++;
    }
    free(ocupadas);
    if (T->tem_rodape && ultima_linha && nlin > 1) ultima_linha->rodape = true;
    T->quantidade_linhas = nlin;
    free(cels);
    adc_aplicar_metadados(pC, tb, pMeta);
    return tb;
}
