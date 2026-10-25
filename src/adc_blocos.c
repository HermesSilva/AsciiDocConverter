#include "adc_blocos.h"
#include "adc_attrlist.h"
#include <stdio.h>
#include <stdlib.h>

/* ========================================================= reconhecedores ==== */

static bool linha_vazia(ADC_Fatia pL) { return adc_fatia_so_espacos(pL); }

/* Linha inteira composta pelo mesmo caractere, pelo menos pMin vezes. */
static bool so_de(ADC_Fatia pL, char pC, int32_t pMin) {
    if (pL.tamanho < pMin) return false;
    for (int32_t i = 0; i < pL.tamanho; i++) if (pL.dados[i] != pC) return false;
    return true;
}

bool adc_linha_eh_delimitador(ADC_Fatia pL, ADC_TipoBloco* pTipo, char* pChar) {
    pL = adc_fatia_aparar_direita(pL);
    if (pL.tamanho < 2) return false;
    char c = pL.dados[0];
    ADC_TipoBloco t = 0;
    if (pL.tamanho == 2 && c == '-' && pL.dados[1] == '-') t = ADC_BL_OPEN;
    else if (pL.tamanho >= 4) {
        if (c == '-' && so_de(pL, '-', 4)) t = ADC_BL_LISTING;
        else if (c == '.' && so_de(pL, '.', 4)) t = ADC_BL_LITERAL;
        else if (c == '=' && so_de(pL, '=', 4)) t = ADC_BL_EXEMPLO;
        else if (c == '*' && so_de(pL, '*', 4)) t = ADC_BL_SIDEBAR;
        else if (c == '_' && so_de(pL, '_', 4)) t = ADC_BL_QUOTE;
        else if (c == '+' && so_de(pL, '+', 4)) t = ADC_BL_PASS;
        else if (c == '/' && so_de(pL, '/', 4)) t = ADC_BL_COMENTARIO;
        else if (c == '~' && so_de(pL, '~', 4)) t = ADC_BL_OPEN;
        else if ((c == '|' || c == ',' || c == ':' || c == '!') && so_de(adc_fatia(pL.dados + 1, pL.tamanho - 1), '=', 3)) t = ADC_BL_TABELA;   /* ^[|,:!]={3,}$ */
    }
    if (c == '`' && pL.tamanho >= 3 && pL.dados[1] == '`' && pL.dados[2] == '`') {
        /* ``` ou ```lang (estilo Markdown): o fecho é ``` */
        bool ok = true; for (int32_t i = 3; i < pL.tamanho; i++) if (!(adc_eh_alnum(pL.dados[i]) || pL.dados[i] == '-' || pL.dados[i] == '_' || pL.dados[i] == '+' || pL.dados[i] == '#')) { ok = false; break; }
        if (ok) t = ADC_BL_LISTING;
    }
    if (!t) return false;
    if (pTipo) *pTipo = t;
    if (pChar) *pChar = c;
    return true;
}

bool adc_linha_eh_atributo_bloco(ADC_Fatia pL) {
    pL = adc_fatia_aparar_direita(pL);
    if (pL.tamanho < 2 || pL.dados[0] != '[' || pL.dados[pL.tamanho - 1] != ']') return false;
    if (pL.tamanho >= 4 && pL.dados[1] == '[' && pL.dados[pL.tamanho - 2] == ']') return true; /* âncora [[id]] */
    /* BlockAttributeLineRx do Asciidoctor: ^\[(?:|[\p{Word}.#%{,"'].*)\]$ — vazio, ou o 1º caractere é de palavra
       ou um de .#%{,"' ; o resto é livre (até "]" no meio: [source,terminal,subs="attributes+"]] vale) */
    if (pL.tamanho == 2) return true;
    unsigned char c1 = (unsigned char)pL.dados[1];
    return adc_eh_alnum((char)c1) || c1 == '_' || c1 >= 0x80 || c1 == '.' || c1 == '#' || c1 == '%' || c1 == '{' || c1 == ',' || c1 == '"' || c1 == '\'';
}

/* * item / - item / ** item / . item / 1. item / a. item / i) item / <1> callout */
bool adc_linha_eh_item_lista(ADC_Fatia pL, ADC_Fatia* pMarcador, ADC_VarianteLista* pVariante, int32_t* pIniTexto) {
    int32_t i = 0;
    while (i < pL.tamanho && adc_eh_espaco(pL.dados[i])) i++;
    if (i >= pL.tamanho) return false;
    int32_t ini = i;
    char c = pL.dados[i];
    if (c == '*' || c == '-') {
        int32_t k = i; while (k < pL.tamanho && pL.dados[k] == c) k++;
        if (c == '-' && k - i != 1) return false;
        if (k - i > 5) return false;
        if (k < pL.tamanho && adc_eh_espaco(pL.dados[k])) {
            int32_t t = k; while (t < pL.tamanho && adc_eh_espaco(pL.dados[t])) t++;
            if (t >= pL.tamanho) return false; /* "* " sem texto não é item */
            if (pMarcador) *pMarcador = adc_fatia(pL.dados + ini, k - ini);
            if (pVariante) *pVariante = ADC_LISTA_UNORDERED;
            if (pIniTexto) *pIniTexto = t;
            return true;
        }
        return false;
    }
    if (c == '.') {
        int32_t k = i; while (k < pL.tamanho && pL.dados[k] == '.') k++;
        if (k - i > 5) return false;
        if (k < pL.tamanho && adc_eh_espaco(pL.dados[k])) {
            int32_t t = k; while (t < pL.tamanho && adc_eh_espaco(pL.dados[t])) t++;
            if (t >= pL.tamanho) return false;
            if (pMarcador) *pMarcador = adc_fatia(pL.dados + ini, k - ini);
            if (pVariante) *pVariante = ADC_LISTA_ORDERED;
            if (pIniTexto) *pIniTexto = t;
            return true;
        }
        return false;
    }
    if (adc_eh_digito(c)) {
        int32_t k = i; while (k < pL.tamanho && adc_eh_digito(pL.dados[k])) k++;
        if (k < pL.tamanho && pL.dados[k] == '.' && k + 1 < pL.tamanho && adc_eh_espaco(pL.dados[k + 1]) && k - i <= 6) {
            int32_t t = k + 1; while (t < pL.tamanho && adc_eh_espaco(pL.dados[t])) t++;
            if (t >= pL.tamanho) return false;
            if (pMarcador) *pMarcador = adc_fatia(pL.dados + ini, k + 1 - ini);
            if (pVariante) *pVariante = ADC_LISTA_ORDERED;
            if (pIniTexto) *pIniTexto = t;
            return true;
        }
        return false;
    }
    if (adc_eh_alfa(c) && i + 1 < pL.tamanho && (pL.dados[i + 1] == '.' || pL.dados[i + 1] == ')') && i + 2 < pL.tamanho && adc_eh_espaco(pL.dados[i + 2])) {
        /* a. / A. / i) — um só caractere (romanos curtos também: ii) iii) iv) ) */
        int32_t t = i + 2; while (t < pL.tamanho && adc_eh_espaco(pL.dados[t])) t++;
        if (t >= pL.tamanho) return false;
        if (pMarcador) *pMarcador = adc_fatia(pL.dados + ini, 2);
        if (pVariante) *pVariante = ADC_LISTA_ORDERED;
        if (pIniTexto) *pIniTexto = t;
        return true;
    }
    if (adc_eh_alfa(c)) {
        /* romanos: ii) iii) iv) vi) ... até 4 letras de ivxlc, seguidos de ')' ou '.' */
        int32_t k = i; while (k < pL.tamanho && k - i < 5 && (adc_minusculo(pL.dados[k]) == 'i' || adc_minusculo(pL.dados[k]) == 'v' || adc_minusculo(pL.dados[k]) == 'x')) k++;
        if (k > i + 1 && k < pL.tamanho && (pL.dados[k] == ')' || pL.dados[k] == '.') && k + 1 < pL.tamanho && adc_eh_espaco(pL.dados[k + 1])) {
            int32_t t = k + 1; while (t < pL.tamanho && adc_eh_espaco(pL.dados[t])) t++;
            if (t >= pL.tamanho) return false;
            if (pMarcador) *pMarcador = adc_fatia(pL.dados + ini, k + 1 - ini);
            if (pVariante) *pVariante = ADC_LISTA_ORDERED;
            if (pIniTexto) *pIniTexto = t;
            return true;
        }
    }
    if (c == '<' && i == 0) {
        int32_t k = i + 1;
        if (k < pL.tamanho && pL.dados[k] == '.') k++;
        else while (k < pL.tamanho && adc_eh_digito(pL.dados[k])) k++;
        if (k > i + 1 && k < pL.tamanho && pL.dados[k] == '>' && k + 1 < pL.tamanho && adc_eh_espaco(pL.dados[k + 1])) {
            int32_t t = k + 1; while (t < pL.tamanho && adc_eh_espaco(pL.dados[t])) t++;
            if (t >= pL.tamanho) return false;
            if (pMarcador) *pMarcador = adc_fatia(pL.dados + ini, k + 1 - ini);
            if (pVariante) *pVariante = ADC_LISTA_CALLOUT;
            if (pIniTexto) *pIniTexto = t;
            return true;
        }
    }
    return false;
}

/* termo:: descrição | termo::: | termo;; */
bool adc_linha_eh_item_dlist(ADC_Fatia pL, ADC_Fatia* pTermo, ADC_Fatia* pMarcador, int32_t* pIniDesc) {
    if (pL.tamanho < 3) return false;
    int32_t i = 0; while (i < pL.tamanho && adc_eh_espaco(pL.dados[i])) i++;
    if (i >= pL.tamanho) return false;
    if (pL.dados[i] == ':' || pL.dados[i] == ';') return false;
    /* procura "::" (2 a 4) ou ";;" seguido de espaço ou fim, com termo não vazio antes */
    for (int32_t k = i + 1; k < pL.tamanho; k++) {
        char c = pL.dados[k];
        if (c == ':' || c == ';') {
            int32_t e = k; while (e < pL.tamanho && pL.dados[e] == c) e++;
            int32_t n = e - k;
            bool ok = (c == ':' && n >= 2 && n <= 4) || (c == ';' && n == 2);
            if (!ok) { k = e - 1; continue; }
            if (e < pL.tamanho && !adc_eh_espaco(pL.dados[e])) { k = e - 1; continue; }
            ADC_Fatia termo = adc_fatia_aparar(adc_fatia(pL.dados + i, k - i));
            if (termo.tamanho == 0) return false;
            /* o termo não pode ser uma URL "http://" (o "::" nunca é precedido de ':' no nosso loop) */
            if (pTermo) *pTermo = termo;
            if (pMarcador) *pMarcador = adc_fatia(pL.dados + k, n);
            int32_t t = e; while (t < pL.tamanho && adc_eh_espaco(pL.dados[t])) t++;
            if (pIniDesc) *pIniDesc = t;
            return true;
        }
    }
    return false;
}

/* :nome: valor  |  :nome!:  |  :!nome: */
bool adc_linha_eh_entrada_atributo(ADC_Fatia pL, ADC_Fatia* pNome, ADC_Fatia* pValor, bool* pRemover) {
    if (pL.tamanho < 3 || pL.dados[0] != ':') return false;
    int32_t i = 1; bool remover = false;
    if (pL.dados[i] == '!') { remover = true; i++; }
    int32_t ini = i;
    /* Asciidoctor (AttributeEntryRx ^:(!?\w[^:]*?):): o 1º caractere é de palavra; os demais, qualquer um menos ':'
       — :velero-1.12: é entrada de atributo. O '!' final (remoção) é tratado abaixo. */
    if (i >= pL.tamanho || !(adc_eh_alnum(pL.dados[i]) || pL.dados[i] == '_')) return false;
    while (i < pL.tamanho && pL.dados[i] != ':') i++;
    if (i < pL.tamanho && i - 1 > ini && pL.dados[i - 1] == '!') remover = true;
    if (i >= pL.tamanho || pL.dados[i] != ':') return false;
    if (i + 1 < pL.tamanho && !adc_eh_espaco(pL.dados[i + 1])) return false;
    if (pNome) *pNome = adc_fatia(pL.dados + ini, (pL.dados[i - 1] == '!' && i - 1 > ini ? i - 1 : i) - ini);
    if (pValor) *pValor = adc_fatia_aparar(adc_fatia(pL.dados + i + 1, pL.tamanho - i - 1));
    if (pRemover) *pRemover = remover;
    return true;
}

static bool eh_titulo_secao(ADC_Fatia pL, int32_t* pNivel, ADC_Fatia* pTitulo) {
    if (pL.tamanho < 3 || pL.dados[0] != '=') return false;
    int32_t k = 0; while (k < pL.tamanho && pL.dados[k] == '=') k++;
    if (k > 6 || k >= pL.tamanho || !adc_eh_espaco(pL.dados[k])) return false;
    ADC_Fatia t = adc_fatia_aparar(adc_fatia(pL.dados + k, pL.tamanho - k));
    if (t.tamanho == 0) return false;
    /* sufixo opcional " ====" do mesmo tamanho */
    int32_t e = t.tamanho; while (e > 0 && t.dados[e - 1] == '=') e--;
    if (e < t.tamanho && t.tamanho - e == k && e > 0 && adc_eh_espaco(t.dados[e - 1])) t = adc_fatia_aparar(adc_fatia(t.dados, e));
    if (pNivel) *pNivel = k - 1;
    if (pTitulo) *pTitulo = t;
    return true;
}

static bool eh_titulo_bloco(ADC_Fatia pL) {
    return pL.tamanho >= 2 && pL.dados[0] == '.' && pL.dados[1] != '.' && !adc_eh_espaco(pL.dados[1]);
}

static bool eh_comentario_linha(ADC_Fatia pL) {
    return pL.tamanho >= 2 && pL.dados[0] == '/' && pL.dados[1] == '/' && !(pL.tamanho >= 4 && so_de(adc_fatia_aparar_direita(pL), '/', 4));
}

static bool eh_macro_bloco(ADC_Fatia pL, ADC_Fatia* pNome, ADC_Fatia* pAlvo, ADC_Fatia* pAttrs) {
    static const char* nomes[] = { "image", "video", "audio", "toc", "include" };
    for (size_t i = 0; i < 5; i++) {
        size_t n = strlen(nomes[i]);
        if ((size_t)pL.tamanho > n + 2 && memcmp(pL.dados, nomes[i], n) == 0 && pL.dados[n] == ':' && pL.dados[n + 1] == ':') {
            int32_t abre = -1;
            for (int32_t k = (int32_t)n + 2; k < pL.tamanho; k++) { if (pL.dados[k] == '[') { abre = k; break; } if (adc_eh_espaco(pL.dados[k])) return false; }
            ADC_Fatia fim = adc_fatia_aparar_direita(pL);
            if (abre < 0 || fim.dados[fim.tamanho - 1] != ']') return false;
            if (i != 3 && abre == (int32_t)n + 2) return false; /* alvo obrigatório, salvo toc */
            *pNome = adc_fatia(nomes[i], (int32_t)n);
            *pAlvo = adc_fatia(pL.dados + n + 2, abre - (int32_t)n - 2);
            *pAttrs = adc_fatia(pL.dados + abre + 1, fim.tamanho - abre - 2);
            return true;
        }
    }
    return false;
}

static bool eh_quebra(ADC_Fatia pL, ADC_VarianteQuebra* pV) {
    ADC_Fatia t = adc_fatia_aparar_direita(pL);
    if (adc_fatia_igual(t, "<<<")) { *pV = ADC_QUEBRA_PAGE; return true; }
    if (adc_fatia_igual(t, "'''") || adc_fatia_igual(t, "---") || adc_fatia_igual(t, "***") || adc_fatia_igual(t, "- - -") || adc_fatia_igual(t, "* * *")) { *pV = ADC_QUEBRA_THEMATIC; return true; }
    return false;
}

static bool eh_admonicao_paragrafo(ADC_Fatia pL, ADC_VarianteAdm* pV, int32_t* pIniTexto) {
    static const struct { const char* n; ADC_VarianteAdm v; } tab[] = {
        { "NOTE:", ADC_ADM_NOTE }, { "TIP:", ADC_ADM_TIP }, { "IMPORTANT:", ADC_ADM_IMPORTANT }, { "WARNING:", ADC_ADM_WARNING }, { "CAUTION:", ADC_ADM_CAUTION }
    };
    for (size_t i = 0; i < 5; i++) {
        size_t n = strlen(tab[i].n);
        if ((size_t)pL.tamanho > n + 1 && memcmp(pL.dados, tab[i].n, n) == 0 && adc_eh_espaco(pL.dados[n])) {
            int32_t t = (int32_t)n; while (t < pL.tamanho && adc_eh_espaco(pL.dados[t])) t++;
            if (t >= pL.tamanho) return false;
            *pV = tab[i].v; *pIniTexto = t;
            return true;
        }
    }
    return false;
}

static bool estilo_para_admonicao(ADC_Fatia pEstilo, ADC_VarianteAdm* pV) {
    if (adc_fatia_igual(pEstilo, "NOTE")) { *pV = ADC_ADM_NOTE; return true; }
    if (adc_fatia_igual(pEstilo, "TIP")) { *pV = ADC_ADM_TIP; return true; }
    if (adc_fatia_igual(pEstilo, "IMPORTANT")) { *pV = ADC_ADM_IMPORTANT; return true; }
    if (adc_fatia_igual(pEstilo, "WARNING")) { *pV = ADC_ADM_WARNING; return true; }
    if (adc_fatia_igual(pEstilo, "CAUTION")) { *pV = ADC_ADM_CAUTION; return true; }
    return false;
}

/* ============================================================= utilitários ==== */

static ADC_Bloco* novo_bloco(ADC_Contexto* pC, ADC_TipoBloco pTipo, const ADC_Linha* pL) {
    ADC_Bloco* b = ADC_NOVO(pC->arena, ADC_Bloco);
    if (!b) return NULL;
    b->tipo = pTipo;
    { ADC_Fatia ic; if (adc_atribs_obter_cstr(&pC->atribs, "icons", &ic)) b->icons_estado = adc_fatia_igual(ic, "font") ? 1 : 2; }
    if (pL) {
        b->local.inicio.linha = pL->numero; b->local.inicio.coluna = 1; b->local.arquivo = pL->arquivo;
        b->local.fim.linha = pL->numero; b->local.fim.coluna = adc_utf8_contar(pL->texto);
    }
    return b;
}

static void fim_em(ADC_Bloco* pB, const ADC_Linha* pL) {
    if (!pL) return;
    pB->local.fim.linha = pL->numero;
    int32_t c = adc_utf8_contar(adc_fatia_aparar_direita(pL->texto));
    pB->local.fim.coluna = c > 0 ? c : 1;
}

static void fim_como_ultimo_filho(ADC_Bloco* pB) {
    if (pB->ultimo_filho) pB->local.fim = pB->ultimo_filho->local.fim;
}

ADC_Inline* adc_parsear_texto_inline(ADC_Contexto* pC, ADC_Fatia pTexto, int32_t pLinha, int32_t pColuna, int32_t pArquivo, ADC_Subs pSubs) {
    int32_t* mapa = NULL;
    ADC_Fatia resolvido = (pSubs == ADC_SUBS_NENHUMA || pSubs == ADC_SUBS_VERBATIM) ? pTexto : adc_atribs_resolver(&pC->atribs, pTexto, &pC->msgs, pLinha, true, &mapa);
    ADC_Segmento seg;
    ADC_Origem o = adc_origem_linha(&seg, pLinha, pColuna, pArquivo, resolvido.dados == pTexto.dados ? NULL : mapa, pTexto.tamanho);
    return adc_inlines_parsear(pC, resolvido, &o, pSubs);
}

/* Junta linhas com '\n' na arena e constrói os segmentos de origem. */
typedef struct Juncao { ADC_Fatia texto; ADC_Segmento* segs; int32_t n; } Juncao;

static Juncao juntar_linhas(ADC_Contexto* pC, const ADC_Linha* pLinhas, int32_t pN, int32_t pColunaBase) {
    Juncao j; memset(&j, 0, sizeof j);
    int32_t total = 0;
    for (int32_t i = 0; i < pN; i++) total += pLinhas[i].texto.tamanho + 1;
    char* buf = (char*)adc_arena_alocar(pC->arena, (size_t)total + 1);
    j.segs = ADC_NOVOS(pC->arena, ADC_Segmento, pN > 0 ? pN : 1);
    if (!buf || !j.segs) return j;
    int32_t o = 0;
    for (int32_t i = 0; i < pN; i++) {
        j.segs[i].inicio = o; j.segs[i].linha = pLinhas[i].numero; j.segs[i].coluna = pColunaBase; j.segs[i].arquivo = pLinhas[i].arquivo;
        memcpy(buf + o, pLinhas[i].texto.dados, (size_t)pLinhas[i].texto.tamanho);
        o += pLinhas[i].texto.tamanho;
        if (i + 1 < pN) buf[o++] = '\n';
    }
    buf[o] = 0;
    j.texto = adc_fatia(buf, o); j.n = pN;
    return j;
}

static ADC_Inline* parsear_linhas_inline(ADC_Contexto* pC, const ADC_Linha* pLinhas, int32_t pN, ADC_Subs pSubs) {
    Juncao j = juntar_linhas(pC, pLinhas, pN, 1);
    if (!j.texto.dados) return NULL;
    int32_t* mapa = NULL;
    ADC_Fatia resolvido = (pSubs == ADC_SUBS_NENHUMA || pSubs == ADC_SUBS_VERBATIM) ? j.texto : adc_atribs_resolver(&pC->atribs, j.texto, &pC->msgs, pN ? pLinhas[0].numero : 0, true, &mapa);
    ADC_Origem o; o.segmentos = j.segs; o.quantidade = j.n; o.mapa = resolvido.dados == j.texto.dados ? NULL : mapa; o.tamanho_original = j.texto.tamanho;
    return adc_inlines_parsear(pC, resolvido, &o, pSubs);
}

/* Lê até pMax linhas da antevisão em um array (para parágrafos). */
#define MAX_LINHAS_BLOCO 4096

void adc_definir_atributo_documento(ADC_Contexto* pC, ADC_Fatia pNome, ADC_Fatia pValor, bool pRemover, int32_t pLinha) {
    ADC_Fatia nome = adc_atribs_normalizar_nome(pC->arena, pNome);
    if (pRemover) {
        if (!adc_atribs_remover(&pC->atribs, nome))
            adc_msgs_adicionar(&pC->msgs, ADC_NIVEL_AVISO, pLinha, 1, "O atributo '%.*s' foi fixado pela API e não pode ser removido pelo documento.", nome.tamanho, nome.dados);
        return;
    }
    /* o valor recebe as substituições de cabeçalho: referências a atributos */
    ADC_Fatia valor = adc_atribs_resolver(&pC->atribs, pValor, &pC->msgs, pLinha, true, NULL);
    if (adc_fatia_igual(nome, "leveloffset") && valor.tamanho > 0 && (valor.dados[0] == '+' || valor.dados[0] == '-')) {
        ADC_Fatia atual = adc_atribs_valor_ou(&pC->atribs, "leveloffset", "0");
        int32_t v = adc_fatia_para_int(atual, 0) + adc_fatia_para_int(adc_fatia(valor.dados + (valor.dados[0] == '+' ? 1 : 0), valor.tamanho - (valor.dados[0] == '+' ? 1 : 0)), 0);
        char tmp[16]; int32_t n = snprintf(tmp, sizeof tmp, "%d", v);
        valor = adc_fatia(adc_arena_copiar(pC->arena, tmp, n), n);
    }
    if (!adc_atribs_definir(&pC->atribs, nome, valor, false))
        adc_msgs_adicionar(&pC->msgs, ADC_NIVEL_AVISO, pLinha, 1, "O atributo '%.*s' foi fixado pela API; a definição no documento foi ignorada.", nome.tamanho, nome.dados);
    else adc_atribs_marcar_documento(&pC->atribs, nome);
    if (adc_fatia_igual(nome, "hardbreaks") || adc_fatia_igual(nome, "hardbreaks-option")) pC->hardbreaks = true;
}

/* Aplica metadados acumulados ao bloco: id, papéis, opções, título (parse inline), reftext. */
void adc_aplicar_metadados(ADC_Contexto* pC, ADC_Bloco* pB, ADC_Metadados* pMeta) {
    if (!pMeta || pMeta->vazio) { return; }
    pB->meta = *pMeta;
    if (pMeta->titulo_cru.tamanho > 0 && !pB->titulo) {
        pB->titulo = adc_parsear_texto_inline(pC, pMeta->titulo_cru, pMeta->local.inicio.linha, 2, pMeta->local.arquivo, ADC_SUBS_TITULO);
    }
    if (pMeta->reftext_cru.tamanho > 0) pB->reftext = adc_parsear_texto_inline(pC, pMeta->reftext_cru, pMeta->local.inicio.linha, 1, pMeta->local.arquivo, ADC_SUBS_TITULO);
    if (pMeta->id.tamanho > 0) adc_ctx_registrar_id(pC, pMeta->id, pB->reftext ? pB->reftext : pB->titulo, pB);
    if (adc_lista_fatias_contem(&pMeta->opcoes, "hardbreaks")) pC->hardbreaks = true;
}

static void limpar_meta(ADC_Metadados* pM) { memset(pM, 0, sizeof *pM); pM->vazio = true; }

/* Linha de atributos de bloco: [attrs] ou [[id,reftext]] */
static void ler_linha_atributos(ADC_Contexto* pC, ADC_Metadados* pMeta, const ADC_Linha* pL) {
    ADC_Fatia t = adc_fatia_aparar_direita(pL->texto);
    if (!pMeta->tem_local) { pMeta->local.inicio.linha = pL->numero; pMeta->local.inicio.coluna = 1; pMeta->local.arquivo = pL->arquivo; pMeta->tem_local = true; }
    pMeta->local.fim.linha = pL->numero; pMeta->local.fim.coluna = adc_utf8_contar(t);
    if (t.tamanho >= 4 && t.dados[1] == '[') {
        ADC_Fatia dentro = adc_fatia(t.dados + 2, t.tamanho - 4);
        int32_t v = adc_fatia_indice(dentro, ',');
        pMeta->id = adc_fatia_aparar(v >= 0 ? adc_fatia(dentro.dados, v) : dentro);
        if (v >= 0) pMeta->reftext_cru = adc_fatia_aparar(adc_fatia(dentro.dados + v + 1, dentro.tamanho - v - 1));
        adc_atributo_definir(pC->arena, &pMeta->atributos, adc_fatia_cstr("id"), pMeta->id);
        pMeta->vazio = false;
        return;
    }
    ADC_Fatia dentro = adc_fatia(t.dados + 1, t.tamanho - 2);
    dentro = adc_atribs_resolver(&pC->atribs, dentro, &pC->msgs, pL->numero, true, NULL);
    adc_attrlist_parsear(pC->arena, dentro, true, pMeta);
}

void adc_ler_linha_atributos(ADC_Contexto* pC, ADC_Metadados* pMeta, const ADC_Linha* pL) { ler_linha_atributos(pC, pMeta, pL); }

/* ================================================================ parse ==== */

static bool interrompe_paragrafo(ADC_Contexto* pC, ADC_Fatia pL, const ADC_Escopo* pE) {
    if (linha_vazia(pL)) return true;
    if (adc_linha_eh_atributo_bloco(pL)) return true;
    if (adc_linha_eh_delimitador(pL, NULL, NULL)) return true;
    ADC_Fatia t = adc_fatia_aparar_direita(pL);
    if (adc_fatia_igual(t, "+")) return true;             /* continuação de lista (SDR-7, universal) */
    if (pE->em_tabela && pL.tamanho > 0 && pL.dados[0] == pE->separador_tabela) return true;
    (void)pC;
    return false;
}

static bool qualquer_lista(ADC_Fatia pT);

static ADC_Bloco* parsear_paragrafo(ADC_Contexto* pC, const ADC_Escopo* pE, ADC_Metadados* pMeta, bool pPrincipalDeLista, ADC_Fatia pMarcadorLista) {
    ADC_Linha* linhas = (ADC_Linha*)malloc(sizeof(ADC_Linha) * MAX_LINHAS_BLOCO);
    if (!linhas) return NULL;
    int32_t n = 0;
    const ADC_Linha* L;
    /* Asciidoctor (read_paragraph_lines com break_at_list = in_list && skipped == 0): parágrafo direto num item, sem
       linha vazia antes, termina em qualquer linha de lista; depois de vazia (ou de '+', que vira vazia), não */
    bool para_em_lista = pE->buffer_item && pC->vazias_antes_bloco == 0;
    while ((L = adc_leitor_espiar(&pC->leitor, 0)) != NULL && n < MAX_LINHAS_BLOCO) {
        if (n > 0) {
            if (interrompe_paragrafo(pC, L->texto, pE)) break;
            if (para_em_lista && qualquer_lista(L->texto)) break;
            if (pPrincipalDeLista) {
                /* SDR-8: um item irmão ou aninhado interrompe o texto principal */
                ADC_Fatia m; ADC_VarianteLista v; int32_t it;
                if (adc_linha_eh_item_lista(L->texto, &m, &v, &it)) break;
                ADC_Fatia termo, mk; int32_t id;
                if (adc_linha_eh_item_dlist(L->texto, &termo, &mk, &id)) break;
            }
            if (eh_comentario_linha(L->texto)) { adc_leitor_pular(&pC->leitor, 1); continue; }
        }
        linhas[n++] = *L;
        adc_leitor_pular(&pC->leitor, 1);
    }
    (void)pMarcadorLista;
    if (n == 0) { free(linhas); return NULL; }
    ADC_Bloco* b = novo_bloco(pC, ADC_BL_PARAGRAFO, &linhas[0]);
    if (!b) { free(linhas); return NULL; }
    b->forma = ADC_FORMA_PARAGRAFO;
    fim_em(b, &linhas[n - 1]);
    /* estilo transforma antes do parse inline (SDR-3) */
    ADC_Fatia estilo = pMeta ? pMeta->estilo : adc_fatia(NULL, 0);
    ADC_VarianteAdm adm;
    bool hard_antes = pC->hardbreaks;
    if (pMeta && adc_lista_fatias_contem(&pMeta->opcoes, "hardbreaks")) pC->hardbreaks = true;
    if (adc_fatia_igual(estilo, "literal") || adc_fatia_igual(estilo, "listing") || adc_fatia_igual(estilo, "source")) {
        b->tipo = adc_fatia_igual(estilo, "literal") ? ADC_BL_LITERAL : ADC_BL_LISTING;
        Juncao j = juntar_linhas(pC, linhas, n, 1);
        b->texto_cru = j.texto;
        b->inlines = parsear_linhas_inline(pC, linhas, n, ADC_SUBS_VERBATIM);
        if (pMeta) { bool a; ADC_Fatia lang = adc_atributo_obter(pMeta->atributos, "$2", &a); if (a) b->linguagem = lang; lang = adc_atributo_obter(pMeta->atributos, "language", &a); if (a) b->linguagem = lang; }
    } else if (adc_fatia_igual(estilo, "pass")) {
        b->tipo = ADC_BL_PASS;
        Juncao j = juntar_linhas(pC, linhas, n, 1);
        b->texto_cru = adc_atribs_resolver(&pC->atribs, j.texto, &pC->msgs, linhas[0].numero, false, NULL);
        b->inlines = parsear_linhas_inline(pC, linhas, n, ADC_SUBS_NENHUMA);
    } else if (adc_fatia_igual(estilo, "comment")) {
        pC->hardbreaks = hard_antes; free(linhas); return NULL;
    } else if (adc_fatia_igual(estilo, "verse")) {
        b->tipo = ADC_BL_VERSE; b->inlines = parsear_linhas_inline(pC, linhas, n, ADC_SUBS_NORMAL);
    } else if (adc_fatia_igual(estilo, "stem") || adc_fatia_igual(estilo, "latexmath") || adc_fatia_igual(estilo, "asciimath")) {
        b->tipo = ADC_BL_STEM; Juncao j = juntar_linhas(pC, linhas, n, 1); b->texto_cru = j.texto; b->inlines = parsear_linhas_inline(pC, linhas, n, ADC_SUBS_NENHUMA);
    } else {
        /* admonição por prefixo "NOTE: " só no primeiro parágrafo-linha */
        int32_t ini_texto;
        ADC_Fatia estilo_lower = estilo;
        if (estilo.tamanho == 0 && eh_admonicao_paragrafo(linhas[0].texto, &adm, &ini_texto)) {
            ADC_Bloco* wrap = novo_bloco(pC, ADC_BL_ADMONICAO, &linhas[0]);
            if (!wrap) { free(linhas); return NULL; }
            wrap->variante_adm = adm; wrap->forma = ADC_FORMA_PARAGRAFO;
            fim_em(wrap, &linhas[n - 1]);
            ADC_Linha primeira = linhas[0];
            primeira.texto = adc_fatia(primeira.texto.dados + ini_texto, primeira.texto.tamanho - ini_texto);
            primeira.deslocamento += ini_texto;
            linhas[0] = primeira;
            Juncao j = juntar_linhas(pC, linhas, n, 1);
            j.segs[0].coluna = ini_texto + 1;
            int32_t* mapa = NULL;
            ADC_Fatia resolvido = adc_atribs_resolver(&pC->atribs, j.texto, &pC->msgs, linhas[0].numero, true, &mapa);
            ADC_Origem o; o.segmentos = j.segs; o.quantidade = j.n; o.mapa = resolvido.dados == j.texto.dados ? NULL : mapa; o.tamanho_original = j.texto.tamanho;
            b->inlines = adc_inlines_parsear(pC, resolvido, &o, ADC_SUBS_NORMAL);
            b->local.inicio.coluna = ini_texto + 1;
            adc_bloco_anexar(wrap, b);
            adc_aplicar_metadados(pC, wrap, pMeta);
            pC->hardbreaks = hard_antes;
            free(linhas);
            return wrap;
        }
        b->inlines = parsear_linhas_inline(pC, linhas, n, ADC_SUBS_NORMAL);
        if (estilo_para_admonicao(estilo_lower, &adm)) {
            ADC_Bloco* wrap = novo_bloco(pC, ADC_BL_ADMONICAO, &linhas[0]);
            if (!wrap) { free(linhas); return NULL; }
            wrap->variante_adm = adm; wrap->forma = ADC_FORMA_PARAGRAFO; wrap->local = b->local;
            adc_bloco_anexar(wrap, b);
            adc_aplicar_metadados(pC, wrap, pMeta);
            wrap->meta.estilo = adc_fatia(NULL, 0);
            pC->hardbreaks = hard_antes;
            free(linhas);
            return wrap;
        }
        if (adc_fatia_igual(estilo, "quote") || adc_fatia_igual(estilo, "sidebar") || adc_fatia_igual(estilo, "example") || adc_fatia_igual(estilo, "abstract") || adc_fatia_igual(estilo, "partintro") || adc_fatia_igual(estilo, "open")) {
            ADC_TipoBloco t = adc_fatia_igual(estilo, "quote") ? ADC_BL_QUOTE : adc_fatia_igual(estilo, "sidebar") ? ADC_BL_SIDEBAR : adc_fatia_igual(estilo, "example") ? ADC_BL_EXEMPLO : ADC_BL_OPEN;
            ADC_Bloco* wrap = novo_bloco(pC, t, &linhas[0]);
            if (!wrap) { free(linhas); return NULL; }
            wrap->forma = ADC_FORMA_PARAGRAFO; wrap->local = b->local;
            adc_bloco_anexar(wrap, b);
            adc_aplicar_metadados(pC, wrap, pMeta);
            if (t == ADC_BL_QUOTE && pMeta) { bool a; ADC_Fatia v = adc_atributo_obter(pMeta->atributos, "$2", &a); if (a) wrap->atribuicao = v; v = adc_atributo_obter(pMeta->atributos, "$3", &a); if (a) wrap->citacao = v; v = adc_atributo_obter(pMeta->atributos, "attribution", &a); if (a) wrap->atribuicao = v; v = adc_atributo_obter(pMeta->atributos, "citetitle", &a); if (a) wrap->citacao = v; }
            pC->hardbreaks = hard_antes;
            free(linhas);
            return wrap;
        }
    }
    if (b->tipo == ADC_BL_VERSE && pMeta) { bool a; ADC_Fatia v = adc_atributo_obter(pMeta->atributos, "$2", &a); if (a) b->atribuicao = v; v = adc_atributo_obter(pMeta->atributos, "$3", &a); if (a) b->citacao = v; }
    adc_aplicar_metadados(pC, b, pMeta);
    pC->hardbreaks = hard_antes;
    free(linhas);
    return b;
}

/* Parágrafo literal (indentado). */
static ADC_Bloco* parsear_literal_indentado(ADC_Contexto* pC, const ADC_Escopo* pE, ADC_Metadados* pMeta) {
    ADC_Linha* linhas = (ADC_Linha*)malloc(sizeof(ADC_Linha) * MAX_LINHAS_BLOCO);
    if (!linhas) return NULL;
    int32_t n = 0;
    const ADC_Linha* L;
    while ((L = adc_leitor_espiar(&pC->leitor, 0)) != NULL && n < MAX_LINHAS_BLOCO) {
        if (linha_vazia(L->texto)) break;
        if (n > 0 && (adc_linha_eh_atributo_bloco(L->texto) || adc_linha_eh_delimitador(L->texto, NULL, NULL) || adc_fatia_igual(adc_fatia_aparar_direita(L->texto), "+"))) break;
        if (pE->em_tabela && L->texto.tamanho > 0 && L->texto.dados[0] == pE->separador_tabela) break;
        if (n > 0 && !adc_eh_espaco(L->texto.dados[0]) && pE->em_item_lista) break;
        linhas[n++] = *L;
        adc_leitor_pular(&pC->leitor, 1);
    }
    if (n == 0) { free(linhas); return NULL; }
    /* remove a indentação comum */
    int32_t menor = INT32_MAX;
    for (int32_t i = 0; i < n; i++) { if (linha_vazia(linhas[i].texto)) continue; int32_t k = 0; while (k < linhas[i].texto.tamanho && adc_eh_espaco(linhas[i].texto.dados[k])) k++; if (k < menor) menor = k; }
    if (menor == INT32_MAX) menor = 0;
    for (int32_t i = 0; i < n; i++) { int32_t k = menor < linhas[i].texto.tamanho ? menor : linhas[i].texto.tamanho; linhas[i].texto = adc_fatia(linhas[i].texto.dados + k, linhas[i].texto.tamanho - k); linhas[i].deslocamento += k; }
    ADC_Bloco* b = novo_bloco(pC, ADC_BL_LITERAL, &linhas[0]);
    if (!b) { free(linhas); return NULL; }
    b->forma = ADC_FORMA_INDENTADA;
    b->local.inicio.coluna = menor + 1;
    fim_em(b, &linhas[n - 1]);
    ADC_Fatia estilo = pMeta ? pMeta->estilo : adc_fatia(NULL, 0);
    if (adc_fatia_igual(estilo, "normal") || adc_fatia_igual(estilo, "paragraph")) {
        b->tipo = ADC_BL_PARAGRAFO;
        b->inlines = parsear_linhas_inline(pC, linhas, n, ADC_SUBS_NORMAL);
    } else {
        Juncao j = juntar_linhas(pC, linhas, n, menor + 1);
        b->texto_cru = j.texto;
        if (adc_fatia_igual(estilo, "source") || adc_fatia_igual(estilo, "listing")) b->tipo = ADC_BL_LISTING;
        b->inlines = parsear_linhas_inline(pC, linhas, n, ADC_SUBS_VERBATIM);
    }
    adc_aplicar_metadados(pC, b, pMeta);
    free(linhas);
    return b;
}

/* Bloco delimitado: a linha de abertura já foi espiada (não consumida). */
static ADC_Bloco* parsear_delimitado(ADC_Contexto* pC, ADC_Bloco* pPai, const ADC_Escopo* pE, ADC_Metadados* pMetaPendente, ADC_TipoBloco pTipo, const ADC_Linha* pAbertura) {
    /* os metadados são deste bloco; os pendentes ficam limpos para os filhos */
    ADC_Metadados meta_copia; limpar_meta(&meta_copia);
    ADC_Metadados* pMeta = &meta_copia;
    if (pMetaPendente) { meta_copia = *pMetaPendente; limpar_meta(pMetaPendente); }
    ADC_Fatia delim = adc_fatia_aparar_direita(pAbertura->texto);
    ADC_Linha abertura = *pAbertura;
    adc_leitor_pular(&pC->leitor, 1);
    ADC_Fatia estilo = pMeta ? pMeta->estilo : adc_fatia(NULL, 0);
    ADC_Fatia linguagem_cerca = adc_fatia(NULL, 0);
    if (delim.tamanho > 3 && delim.dados[0] == '`') { linguagem_cerca = adc_fatia(delim.dados + 3, delim.tamanho - 3); delim = adc_fatia(delim.dados, 3); }

    if (pTipo == ADC_BL_TABELA) return adc_tabela_parsear(pC, pPai, pMeta, delim, &abertura);

    /* decide o tipo final pelo estilo (SDR-3) */
    ADC_TipoBloco tipo = pTipo;
    ADC_VarianteAdm adm = 0;
    bool verbatim = (tipo == ADC_BL_LISTING || tipo == ADC_BL_LITERAL || tipo == ADC_BL_PASS || tipo == ADC_BL_COMENTARIO);
    if (tipo == ADC_BL_EXEMPLO && estilo_para_admonicao(estilo, &adm)) tipo = ADC_BL_ADMONICAO;
    else if (tipo == ADC_BL_OPEN) {
        if (estilo_para_admonicao(estilo, &adm)) tipo = ADC_BL_ADMONICAO;
        else if (adc_fatia_igual(estilo, "source") || adc_fatia_igual(estilo, "listing")) { tipo = ADC_BL_LISTING; verbatim = true; }
        else if (adc_fatia_igual(estilo, "literal")) { tipo = ADC_BL_LITERAL; verbatim = true; }
        else if (adc_fatia_igual(estilo, "pass")) { tipo = ADC_BL_PASS; verbatim = true; }
        else if (adc_fatia_igual(estilo, "comment")) { tipo = ADC_BL_COMENTARIO; verbatim = true; }
        else if (adc_fatia_igual(estilo, "quote")) tipo = ADC_BL_QUOTE;
        else if (adc_fatia_igual(estilo, "verse")) tipo = ADC_BL_VERSE;
        else if (adc_fatia_igual(estilo, "sidebar")) tipo = ADC_BL_SIDEBAR;
        else if (adc_fatia_igual(estilo, "example")) tipo = ADC_BL_EXEMPLO;
        else if (adc_fatia_igual(estilo, "stem") || adc_fatia_igual(estilo, "latexmath") || adc_fatia_igual(estilo, "asciimath")) { tipo = ADC_BL_STEM; verbatim = true; }
    }
    else if (tipo == ADC_BL_QUOTE && adc_fatia_igual(estilo, "verse")) tipo = ADC_BL_VERSE;
    else if (tipo == ADC_BL_LISTING && adc_fatia_igual(estilo, "literal")) tipo = ADC_BL_LITERAL;
    else if (tipo == ADC_BL_LITERAL && (adc_fatia_igual(estilo, "source") || adc_fatia_igual(estilo, "listing"))) tipo = ADC_BL_LISTING;
    else if (tipo == ADC_BL_PASS && (adc_fatia_igual(estilo, "stem") || adc_fatia_igual(estilo, "latexmath") || adc_fatia_igual(estilo, "asciimath"))) tipo = ADC_BL_STEM;
    else if (tipo == ADC_BL_SIDEBAR && estilo_para_admonicao(estilo, &adm)) tipo = ADC_BL_ADMONICAO;

    ADC_Bloco* b = novo_bloco(pC, tipo, &abertura);
    if (!b) return NULL;
    b->forma = ADC_FORMA_DELIMITADA;
    b->delimitador = delim;
    b->variante_adm = adm;
    if (++pC->profundidade > pC->opcoes.aninhamento_maximo) {
        adc_msgs_adicionar(&pC->msgs, ADC_NIVEL_ERRO, abertura.numero, 1, "Blocos aninhados além do limite de %d níveis; o bloco '%.*s' não foi processado.", pC->opcoes.aninhamento_maximo, delim.tamanho, delim.dados);
        pC->limite_estourado = true; pC->profundidade--;
        return NULL;
    }

    if (verbatim || tipo == ADC_BL_VERSE) {
        ADC_Linha* linhas = (ADC_Linha*)malloc(sizeof(ADC_Linha) * MAX_LINHAS_BLOCO);
        if (!linhas) { pC->profundidade--; return NULL; }
        int32_t n = 0; const ADC_Linha* L; bool fechado = false;
        ADC_Linha ultima = abertura;
        while ((L = adc_leitor_espiar(&pC->leitor, 0)) != NULL) {
            if (adc_fatia_igual_f(adc_fatia_aparar_direita(L->texto), delim)) { ultima = *L; adc_leitor_pular(&pC->leitor, 1); fechado = true; break; }
            if (n < MAX_LINHAS_BLOCO) linhas[n++] = *L; else ultima = *L;
            ultima = *L;
            adc_leitor_pular(&pC->leitor, 1);
        }
        if (!fechado) adc_msgs_adicionar(&pC->msgs, ADC_NIVEL_AVISO, abertura.numero, 1, "Bloco delimitado por '%.*s' aberto na linha %d não foi fechado; foi encerrado no fim do documento.", delim.tamanho, delim.dados, abertura.numero);
        fim_em(b, &ultima);
        if (tipo == ADC_BL_COMENTARIO) { pC->profundidade--; free(linhas); return NULL; }
        Juncao j = juntar_linhas(pC, linhas, n, 1);
        b->texto_cru = j.texto;
        if (tipo == ADC_BL_PASS) {
            b->texto_cru = adc_atribs_resolver(&pC->atribs, j.texto, &pC->msgs, abertura.numero, false, NULL);
            b->inlines = parsear_linhas_inline(pC, linhas, n, ADC_SUBS_NENHUMA);
        } else if (tipo == ADC_BL_VERSE) b->inlines = parsear_linhas_inline(pC, linhas, n, ADC_SUBS_NORMAL);
        else if (tipo == ADC_BL_STEM) b->inlines = parsear_linhas_inline(pC, linhas, n, ADC_SUBS_NENHUMA);
        else {
            /* subs=attributes+ em listing: resolve atributos antes */
            bool a = false; ADC_Fatia subs = pMeta ? adc_atributo_obter(pMeta->atributos, "subs", &a) : adc_fatia(NULL, 0);
            bool subs_atributos = false, subs_marcas = false;
            if (pMeta && a) {
                /* subs="attributes", "attributes+", "+attributes", "verbatim,attributes", "normal" */
                int32_t ini = 0;
                for (int32_t k = 0; k <= subs.tamanho; k++) {
                    if (k == subs.tamanho || subs.dados[k] == ',') {
                        ADC_Fatia um = adc_fatia_aparar(adc_fatia(subs.dados + ini, k - ini));
                        while (um.tamanho && (um.dados[0] == '+' || um.dados[0] == '-')) { um.dados++; um.tamanho--; }
                        while (um.tamanho && um.dados[um.tamanho - 1] == '+') um.tamanho--;
                        if (adc_fatia_igual(um, "attributes") || adc_fatia_igual(um, "normal")) subs_atributos = true;
                        if (adc_fatia_igual(um, "quotes") || adc_fatia_igual(um, "normal")) subs_marcas = true;
                        ini = k + 1;
                    }
                }
            }
            if (subs_atributos) {
                ADC_Fatia r = adc_atribs_resolver(&pC->atribs, j.texto, &pC->msgs, abertura.numero, false, NULL);
                b->texto_cru = r;
                ADC_Segmento seg; ADC_Origem o = adc_origem_linha(&seg, abertura.numero + 1, 1, abertura.arquivo, NULL, r.tamanho);
                b->inlines = adc_inlines_parsear(pC, r, &o, subs_marcas ? ADC_SUBS_VERBATIM_MARCAS : ADC_SUBS_VERBATIM);
            } else b->inlines = parsear_linhas_inline(pC, linhas, n, subs_marcas ? ADC_SUBS_VERBATIM_MARCAS : ADC_SUBS_VERBATIM);
        }
        if (pMeta) {
            bool a; ADC_Fatia lang = adc_atributo_obter(pMeta->atributos, "$2", &a);
            if (a && (adc_fatia_igual(estilo, "source") || tipo == ADC_BL_LISTING)) b->linguagem = lang;
            lang = adc_atributo_obter(pMeta->atributos, "language", &a); if (a) b->linguagem = lang;
            if (tipo == ADC_BL_VERSE) { ADC_Fatia v = adc_atributo_obter(pMeta->atributos, "$2", &a); if (a) b->atribuicao = v; v = adc_atributo_obter(pMeta->atributos, "$3", &a); if (a) b->citacao = v; v = adc_atributo_obter(pMeta->atributos, "attribution", &a); if (a) b->atribuicao = v; v = adc_atributo_obter(pMeta->atributos, "citetitle", &a); if (a) b->citacao = v; }
        }
        if (tipo == ADC_BL_LISTING && b->linguagem.tamanho == 0 && linguagem_cerca.tamanho) { b->linguagem = linguagem_cerca; b->meta.estilo = adc_fatia_cstr("source"); }
        /* :source-language: vigente torna todo bloco listing delimitado um bloco source nessa linguagem */
        if (tipo == ADC_BL_LISTING && b->linguagem.tamanho == 0) { ADC_Fatia sl; if (adc_atribs_obter_cstr(&pC->atribs, "source-language", &sl) && sl.tamanho) { b->linguagem = sl; if (pMeta) pMeta->estilo = adc_fatia_cstr("source"); } }
        adc_aplicar_metadados(pC, b, pMeta);
        pC->profundidade--;
        free(linhas);
        return b;
    }

    /* composto: parseia filhos até o delimitador de fechamento */
    ADC_Escopo e; memset(&e, 0, sizeof e);
    e.fechamento = delim; e.nivel_secao = pE->nivel_secao; e.permite_secao = false;
    e.em_tabela = pE->em_tabela; e.separador_tabela = pE->separador_tabela;
    adc_blocos_parsear(pC, b, &e);
    const ADC_Linha* F = adc_leitor_espiar(&pC->leitor, 0);
    if (F && adc_fatia_igual_f(adc_fatia_aparar_direita(F->texto), delim)) { fim_em(b, F); adc_leitor_pular(&pC->leitor, 1); }
    else {
        adc_msgs_adicionar(&pC->msgs, ADC_NIVEL_AVISO, abertura.numero, 1, "Bloco delimitado por '%.*s' aberto na linha %d não foi fechado; foi encerrado no fim do documento.", delim.tamanho, delim.dados, abertura.numero);
        fim_como_ultimo_filho(b);
    }
    if (tipo == ADC_BL_QUOTE && pMeta) { bool a; ADC_Fatia v = adc_atributo_obter(pMeta->atributos, "$2", &a); if (a) b->atribuicao = v; v = adc_atributo_obter(pMeta->atributos, "$3", &a); if (a) b->citacao = v; v = adc_atributo_obter(pMeta->atributos, "attribution", &a); if (a) b->atribuicao = v; v = adc_atributo_obter(pMeta->atributos, "citetitle", &a); if (a) b->citacao = v; }
    adc_aplicar_metadados(pC, b, pMeta);
    if (tipo == ADC_BL_ADMONICAO) b->meta.estilo = adc_fatia(NULL, 0);
    pC->profundidade--;
    return b;
}

/* ---------------------------------------------------------------- listas ---- */

static ADC_Bloco* parsear_lista(ADC_Contexto* pC, ADC_Bloco* pPai, const ADC_Escopo* pE, ADC_Metadados* pMeta);
static ADC_Bloco* parsear_dlist(ADC_Contexto* pC, ADC_Bloco* pPai, const ADC_Escopo* pE, ADC_Metadados* pMeta);

/* Marcadores equivalentes: "1." ≡ "2." (numerados), "a." ≡ "b.", "i)" ≡ "ii)" */
static bool marcador_equivalente(ADC_Fatia pA, ADC_Fatia pB) {
    if (adc_fatia_igual_f(pA, pB)) return true;
    if (pA.tamanho == 0 || pB.tamanho == 0) return false;
    bool numA = adc_eh_digito(pA.dados[0]), numB = adc_eh_digito(pB.dados[0]);
    if (numA && numB) return true;
    bool alA = adc_eh_alfa(pA.dados[0]) && pA.tamanho >= 2, alB = adc_eh_alfa(pB.dados[0]) && pB.tamanho >= 2;
    if (alA && alB) return pA.dados[pA.tamanho - 1] == pB.dados[pB.tamanho - 1];
    if (pA.dados[0] == '<' && pB.dados[0] == '<') return true;
    return false;
}

/* ================================================= linhas de um item de lista ==== */
/* Tradução de Parser.read_lines_for_list_item do Asciidoctor 2.0.20: lê do leitor corrente as linhas que pertencem
   ao item (inclusive as das listas aninhadas, que o item analisa depois a partir deste buffer). Regras:
   - item irmão (mesmo tipo e marcador) encerra;
   - '+' ativa a continuação: o próximo bloco é anexado (o '+' vira linha vazia, salvo dentro de lista aninhada);
   - linha vazia seguida de '+' é continuação "destacada" (o '+' vira vazia no fim);
   - depois de linha vazia, só continuam lista aninhada, literal indentado ou '+';
   - bloco delimitado só entra com continuação ativa (e entra inteiro, até o delimitador de fecho). */

static bool eh_mais(ADC_Fatia t) { return adc_fatia_igual(adc_fatia_aparar_direita(t), "+"); }

/* Lista aninhável (ulist/olist/dlist; colist não). pSoDlist: dentro de lista aninhada só dlist conta.
   *pDlistSemTexto = dlist cujo termo não traz descrição na mesma linha. */
static bool lista_aninhavel(ADC_Fatia pT, bool pSoDlist, bool* pDlistSemTexto) {
    ADC_Fatia m; ADC_VarianteLista v; int32_t it;
    if (!pSoDlist && adc_linha_eh_item_lista(pT, &m, &v, &it) && v != ADC_LISTA_CALLOUT) return true;
    ADC_Fatia termo, mk; int32_t id;
    if (adc_linha_eh_item_dlist(pT, &termo, &mk, &id)) { if (pDlistSemTexto) *pDlistSemTexto = id >= pT.tamanho || adc_fatia_so_espacos(adc_fatia(pT.dados + id, pT.tamanho - id)); return true; }
    return false;
}

static bool qualquer_lista(ADC_Fatia pT) {
    ADC_Fatia m; ADC_VarianteLista v; int32_t it; ADC_Fatia termo, mk; int32_t id;
    return adc_linha_eh_item_lista(pT, &m, &v, &it) || adc_linha_eh_item_dlist(pT, &termo, &mk, &id);
}

static bool eh_irmao(ADC_Fatia pT, bool pDlist, ADC_VarianteLista pVar, ADC_Fatia pMarcador) {
    if (pDlist) { ADC_Fatia termo, mk; int32_t id; return adc_linha_eh_item_dlist(pT, &termo, &mk, &id) && adc_fatia_igual_f(mk, pMarcador); }
    ADC_Fatia m; ADC_VarianteLista v; int32_t it;
    return adc_linha_eh_item_lista(pT, &m, &v, &it) && v == pVar && marcador_equivalente(m, pMarcador);
}

typedef struct BufItem { ADC_Linha* l; int32_t n, cap; } BufItem;
static void buf_push(BufItem* b, const ADC_Linha* pL) {
    if (b->n >= b->cap) { int32_t nc = b->cap ? b->cap * 2 : 64; ADC_Linha* nv = (ADC_Linha*)realloc(b->l, sizeof(ADC_Linha) * (size_t)nc); if (!nv) return; b->l = nv; b->cap = nc; }
    b->l[b->n++] = *pL;
}

/* Literal indentado dentro do item: read_lines_until(break_on_blank_lines, break_on_list_continuation[, irmão]). */
static void ler_literal_do_item(ADC_Contexto* pC, BufItem* b, bool pDlist, ADC_VarianteLista pVar, ADC_Fatia pMarcador) {
    const ADC_Linha* L;
    while ((L = adc_leitor_espiar(&pC->leitor, 0)) != NULL) {
        if (linha_vazia(L->texto) || eh_mais(L->texto)) break;
        if (pDlist && eh_irmao(L->texto, true, pVar, pMarcador)) break;
        buf_push(b, L); adc_leitor_pular(&pC->leitor, 1);
    }
}

/* Devolve as linhas do item em *pSaida (na arena) e a quantidade. pE: escopo de quem contém a lista (o
   delimitador de fecho dele nunca é lido para dentro do item). */
static int32_t ler_linhas_do_item(ADC_Contexto* pC, const ADC_Escopo* pE, bool pDlist, ADC_VarianteLista pVar, ADC_Fatia pMarcador, bool pTemTexto, ADC_Linha** pSaida) {
    enum { INATIVA, ATIVA, CONGELADA } cont = INATIVA;
    BufItem b = { NULL, 0, 0 };
    bool aninhada = false, tem_texto = pTemTexto;
    int32_t destacada = -1;
    for (;;) {
        const ADC_Linha* P = adc_leitor_espiar(&pC->leitor, 0);
        if (!P) break;
        ADC_Linha este = *P;
        ADC_Fatia te = adc_fatia_aparar_direita(este.texto);
        if (pE->fechamento.tamanho > 0 && adc_fatia_igual_f(te, pE->fechamento)) break;   /* fim do bloco que contém a lista */
        if (eh_irmao(este.texto, pDlist, pVar, pMarcador)) break;
        bool tem_ant = b.n > 0;
        bool ant_mais = tem_ant && eh_mais(b.l[b.n - 1].texto);
        bool ant_vazia = tem_ant && linha_vazia(b.l[b.n - 1].texto);   /* valor antes de qualquer troca abaixo */
        if (ant_mais) {
            if (cont == INATIVA) {
                cont = ATIVA; tem_texto = true;
                if (!aninhada) { ADC_Linha v = b.l[b.n - 1]; v.texto = adc_fatia(v.texto.dados, 0); b.l[b.n - 1] = v; }
            }
            if (eh_mais(este.texto)) {
                adc_leitor_pular(&pC->leitor, 1);
                if (cont != CONGELADA) { cont = CONGELADA; buf_push(&b, &este); }
                continue;
            }
        }
        ADC_TipoBloco tipo_delim; char ch_delim;
        if (adc_linha_eh_delimitador(este.texto, &tipo_delim, &ch_delim)) {
            if (cont != ATIVA) break;
            /* entra o bloco inteiro, até o delimitador de fecho (``` fecha ```) */
            ADC_Fatia fecho = te;
            if (fecho.tamanho >= 3 && fecho.dados[0] == '`') fecho = adc_fatia(fecho.dados, 3);
            buf_push(&b, &este); adc_leitor_pular(&pC->leitor, 1);
            const ADC_Linha* Q;
            while ((Q = adc_leitor_espiar(&pC->leitor, 0)) != NULL) {
                ADC_Linha q = *Q; buf_push(&b, &q); adc_leitor_pular(&pC->leitor, 1);
                if (adc_fatia_igual_f(adc_fatia_aparar_direita(q.texto), fecho)) break;
            }
            cont = INATIVA;
            continue;
        }
        if (pDlist && cont != ATIVA && adc_linha_eh_atributo_bloco(este.texto)) {
            /* em dlist, linhas de atributo só ficam no item se o que vem depois é uma lista que não é irmã */
            int32_t k = 1; bool interrompe = false; bool decidiu = false;
            const ADC_Linha* N;
            while ((N = adc_leitor_espiar(&pC->leitor, k)) != NULL) {
                if (adc_linha_eh_delimitador(N->texto, NULL, NULL)) { interrompe = true; decidiu = true; break; }
                if (linha_vazia(N->texto) || adc_linha_eh_atributo_bloco(N->texto)) { k++; continue; }
                if (qualquer_lista(N->texto) && !eh_irmao(N->texto, pDlist, pVar, pMarcador)) { decidiu = true; break; }
                interrompe = true; decidiu = true; break;
            }
            if (interrompe) break;
            for (int32_t z = 0; z < k; z++) { const ADC_Linha* A = adc_leitor_espiar(&pC->leitor, 0); ADC_Linha a = *A; if (decidiu) buf_push(&b, &a); adc_leitor_pular(&pC->leitor, 1); }
            continue;
        }
        if (cont == ATIVA && !linha_vazia(este.texto)) {
            if (adc_eh_espaco(este.texto.dados[0])) {
                ler_literal_do_item(pC, &b, pDlist, pVar, pMarcador);
                cont = INATIVA;
                continue;
            }
            adc_leitor_pular(&pC->leitor, 1);
            if (eh_titulo_bloco(este.texto) || adc_linha_eh_atributo_bloco(este.texto) || adc_linha_eh_entrada_atributo(este.texto, NULL, NULL, NULL)) {
                buf_push(&b, &este);
            } else {
                bool sem_texto = false;
                if (lista_aninhavel(este.texto, aninhada, &sem_texto)) {
                    aninhada = true;
                    /* Asciidoctor: aqui a lista é achada com match?, que não atualiza $~; o "$~[3] vazio?" lê um $~ que
                       sobrou de outro teste (o de irmão também usa match? quando o marcador é Regexp, como na dlist) e
                       que não tem o texto do termo. Na prática has_text vira falso sempre que a aninhada é dlist.
                       Consequência real: depois de "where:\n+\ntermo:: desc", título/parágrafo após linha vazia é
                       absorvido pelo item (e o título solto no fim do item é descartado). Conferido no oráculo. */
                    ADC_Fatia m3; ADC_VarianteLista v3; int32_t it3;
                    bool eh_dlist = !(adc_linha_eh_item_lista(este.texto, &m3, &v3, &it3) && v3 != ADC_LISTA_CALLOUT);
                    (void)sem_texto;
                    if (eh_dlist) tem_texto = false;
                }
                buf_push(&b, &este);
                cont = INATIVA;
            }
            continue;
        }
        if (ant_vazia) {
            if (linha_vazia(este.texto)) {
                /* pula as vazias e olha a próxima (as puladas não entram) */
                while ((P = adc_leitor_espiar(&pC->leitor, 0)) != NULL && linha_vazia(P->texto)) adc_leitor_pular(&pC->leitor, 1);
                if (!P) break;
                este = *P; te = adc_fatia_aparar_direita(este.texto);
                if (pE->fechamento.tamanho > 0 && adc_fatia_igual_f(te, pE->fechamento)) break;
                if (eh_irmao(este.texto, pDlist, pVar, pMarcador)) break;
            }
            if (eh_mais(este.texto)) {
                destacada = b.n; buf_push(&b, &este); adc_leitor_pular(&pC->leitor, 1);
            } else if (tem_texto) {
                bool sem_texto = false;
                if (eh_irmao(este.texto, pDlist, pVar, pMarcador)) break;
                if (lista_aninhavel(este.texto, false, &sem_texto)) {
                    buf_push(&b, &este); adc_leitor_pular(&pC->leitor, 1);
                    aninhada = true; if (sem_texto) tem_texto = false;
                } else if (este.texto.tamanho > 0 && adc_eh_espaco(este.texto.dados[0])) {
                    ler_literal_do_item(pC, &b, pDlist, pVar, pMarcador);
                } else break;
            } else {
                if (!aninhada && b.n > 0) b.n--;   /* tira a vazia anterior: o texto vem colado ao termo */
                buf_push(&b, &este); adc_leitor_pular(&pC->leitor, 1);
                tem_texto = true;
            }
            continue;
        }
        /* caso geral: a linha entra */
        if (!linha_vazia(este.texto)) tem_texto = true;
        { bool sem_texto = false; if (lista_aninhavel(este.texto, aninhada, &sem_texto)) { aninhada = true; if (sem_texto) tem_texto = false; } }
        buf_push(&b, &este); adc_leitor_pular(&pC->leitor, 1);
    }
    if (destacada >= 0 && destacada < b.n) b.l[destacada].texto = adc_fatia(b.l[destacada].texto.dados, 0);
    while (b.n > 0 && linha_vazia(b.l[b.n - 1].texto)) b.n--;
    if (b.n > 0 && eh_mais(b.l[b.n - 1].texto)) b.n--;
    *pSaida = NULL;
    if (b.n > 0) {
        *pSaida = ADC_NOVOS(pC->arena, ADC_Linha, b.n);
        if (*pSaida) memcpy(*pSaida, b.l, sizeof(ADC_Linha) * (size_t)b.n);
    }
    int32_t n = *pSaida ? b.n : 0;
    free(b.l);
    return n;
}

/* Troca o leitor pelo buffer do item; devolve o leitor salvo (para restaurar com fim_buffer_do_item). */
static ADC_Leitor inicio_buffer_do_item(ADC_Contexto* pC, const ADC_Linha* pLinhas, int32_t pN) {
    ADC_Leitor salvo = pC->leitor;
    ADC_Leitor sub;
    adc_leitor_iniciar(&sub, pC->arena, &pC->atribs, &pC->msgs, pC->resolver, pC->contexto_resolver, pC->opcoes.includes_maximo);
    sub.arquivos = salvo.arquivos; sub.quantidade_arquivos = salvo.quantidade_arquivos;
    sub.includes_feitos = salvo.includes_feitos;
    adc_leitor_abrir_linhas(&sub, pLinhas ? pLinhas : &salvo.fila[0], pLinhas ? pN : 0);
    pC->leitor = sub;
    return salvo;
}

static void fim_buffer_do_item(ADC_Contexto* pC, ADC_Leitor* pSalvo) {
    pSalvo->includes_feitos = pC->leitor.includes_feitos;
    pSalvo->arquivos = pC->leitor.arquivos; pSalvo->quantidade_arquivos = pC->leitor.quantidade_arquivos;
    pC->leitor = *pSalvo;
}

/* Blocos do item (depois do texto principal), lidos do buffer: linha vazia não encerra o item. */
static void blocos_do_buffer(ADC_Contexto* pC, ADC_Bloco* pItem, const ADC_Escopo* pE, ADC_Fatia pMarcador) {
    ADC_Escopo e = *pE;
    e.em_item_lista = true; e.buffer_item = true; e.marcador_lista = pMarcador; e.permite_secao = false;
    e.fechamento = adc_fatia(NULL, 0); e.em_tabela = false;
    adc_blocos_parsear(pC, pItem, &e);
    if (pItem->ultimo_filho) pItem->local.fim = pItem->ultimo_filho->local.fim;
}

static ADC_Bloco* parsear_item(ADC_Contexto* pC, const ADC_Escopo* pE, ADC_Fatia pMarcador, const ADC_Linha* pLinhaMarcador, int32_t pIniTexto, ADC_Bloco* pLista) {
    /* cópia: pLinhaMarcador aponta para a fila do leitor, que é trocado pelo buffer do item logo abaixo */
    const ADC_Linha marca = *pLinhaMarcador;
    const ADC_Linha* pL = &marca;
    ADC_Bloco* item = novo_bloco(pC, ADC_BL_ITEM_LISTA, pL);
    if (!item) return NULL;
    item->marcador = pMarcador;
    item->pai = pLista;
    /* texto principal: resto da linha + linhas seguintes não interrompidas */
    ADC_Linha primeira = *pL;
    primeira.texto = adc_fatia(pL->texto.dados + pIniTexto, pL->texto.tamanho - pIniTexto);
    primeira.deslocamento += pIniTexto;
    adc_leitor_pular(&pC->leitor, 1);
    /* as linhas do item vão para um buffer (read_lines_for_list_item) e o item é analisado a partir dele */
    ADC_Linha* buf = NULL;
    int32_t nbuf = ler_linhas_do_item(pC, pE, false, pLista->variante_lista, pMarcador, true, &buf);
    ADC_Leitor salvo = inicio_buffer_do_item(pC, buf, nbuf);
    ADC_Linha* linhas = (ADC_Linha*)malloc(sizeof(ADC_Linha) * MAX_LINHAS_BLOCO);
    if (!linhas) { fim_buffer_do_item(pC, &salvo); return NULL; }
    int32_t n = 0; linhas[n++] = primeira;
    int32_t* colunas_cont = (int32_t*)malloc(sizeof(int32_t) * MAX_LINHAS_BLOCO);
    if (!colunas_cont) { free(linhas); fim_buffer_do_item(pC, &salvo); return NULL; }
    colunas_cont[0] = pIniTexto + 1;
    ADC_Linha ultima_original = *pL;
    const ADC_Linha* L;
    while ((L = adc_leitor_espiar(&pC->leitor, 0)) != NULL && n < MAX_LINHAS_BLOCO) {
        if (interrompe_paragrafo(pC, L->texto, pE)) break;
        ADC_Fatia m; ADC_VarianteLista v; int32_t it;
        if (adc_linha_eh_item_lista(L->texto, &m, &v, &it)) break;
        ADC_Fatia termo, mk; int32_t id;
        if (adc_linha_eh_item_dlist(L->texto, &termo, &mk, &id)) break;
        if (eh_comentario_linha(L->texto)) { adc_leitor_pular(&pC->leitor, 1); continue; }
        /* linha de continuação do texto principal: espaços iniciais não contam, mas a coluna de origem sim */
        ADC_Linha c = *L; c.texto = adc_fatia_aparar(c.texto);
        colunas_cont[n] = (int32_t)(c.texto.dados - L->texto.dados) + 1;
        c.deslocamento += colunas_cont[n] - 1;
        linhas[n++] = c;
        ultima_original = *L;
        adc_leitor_pular(&pC->leitor, 1);
    }
    Juncao j = juntar_linhas(pC, linhas, n, pIniTexto + 1);
    for (int32_t i = 1; i < n; i++) j.segs[i].coluna = colunas_cont[i];
    int32_t* mapa = NULL;
    ADC_Fatia resolvido = adc_atribs_resolver(&pC->atribs, j.texto, &pC->msgs, pL->numero, true, &mapa);
    ADC_Origem o; o.segmentos = j.segs; o.quantidade = j.n; o.mapa = resolvido.dados == j.texto.dados ? NULL : mapa; o.tamanho_original = j.texto.tamanho;
    item->principal = adc_inlines_parsear(pC, resolvido, &o, ADC_SUBS_NORMAL);
    fim_em(item, &ultima_original);
    free(linhas); free(colunas_cont);
    blocos_do_buffer(pC, item, pE, pMarcador);
    fim_buffer_do_item(pC, &salvo);
    return item;
}

static ADC_Bloco* parsear_lista(ADC_Contexto* pC, ADC_Bloco* pPai, const ADC_Escopo* pE, ADC_Metadados* pMetaPendente) {
    ADC_Metadados meta_copia; limpar_meta(&meta_copia);
    ADC_Metadados* pMeta = &meta_copia;
    if (pMetaPendente) { meta_copia = *pMetaPendente; limpar_meta(pMetaPendente); }
    const ADC_Linha* L = adc_leitor_espiar(&pC->leitor, 0);
    ADC_Fatia marcador; ADC_VarianteLista variante; int32_t ini;
    if (!L || !adc_linha_eh_item_lista(L->texto, &marcador, &variante, &ini)) return NULL;
    ADC_Bloco* lista = novo_bloco(pC, variante == ADC_LISTA_CALLOUT ? ADC_BL_LISTA : ADC_BL_LISTA, L);
    if (!lista) return NULL;
    lista->pai = pPai;
    lista->variante_lista = variante;
    lista->marcador = marcador;
    if (variante == ADC_LISTA_ORDERED && adc_eh_digito(marcador.dados[0])) lista->numero = adc_fatia_para_int(adc_fatia(marcador.dados, marcador.tamanho - 1), 1);
    if (pMeta) { bool a; ADC_Fatia st = adc_atributo_obter(pMeta->atributos, "start", &a); if (a) lista->numero = adc_fatia_para_int(st, 1); }
    if (++pC->profundidade > pC->opcoes.aninhamento_maximo) {
        adc_msgs_adicionar(&pC->msgs, ADC_NIVEL_ERRO, L->numero, 1, "Listas aninhadas além do limite de %d níveis.", pC->opcoes.aninhamento_maximo);
        pC->limite_estourado = true; pC->profundidade--; adc_leitor_pular(&pC->leitor, 1); return NULL;
    }
    for (;;) {
        L = adc_leitor_espiar(&pC->leitor, 0);
        if (!L) break;
        /* linhas vazias entre itens não encerram a lista se o próximo é item do mesmo marcador */
        int32_t k = 0;
        while (L && linha_vazia(L->texto)) { k++; L = adc_leitor_espiar(&pC->leitor, k); }
        if (!L) break;
        ADC_Fatia m; ADC_VarianteLista v; int32_t it;
        if (!adc_linha_eh_item_lista(L->texto, &m, &v, &it) || !marcador_equivalente(m, marcador)) {
            /* linhas de atributo antes de um item do mesmo marcador: pertencem ao item (ignoramos metadados de item) */
            break;
        }
        adc_leitor_pular(&pC->leitor, k);
        ADC_Bloco* item = parsear_item(pC, pE, m, L, it, lista);
        if (!item) break;
        if (variante == ADC_LISTA_CALLOUT) item->numero = adc_fatia_igual(adc_fatia(m.dados + 1, m.tamanho - 2), ".") ? lista->quantidade_filhos + 1 : adc_fatia_para_int(adc_fatia(m.dados + 1, m.tamanho - 2), 0);
        adc_bloco_anexar(lista, item);
        lista->local.fim = item->local.fim;
    }
    pC->profundidade--;
    adc_aplicar_metadados(pC, lista, pMeta);
    return lista;
}

static ADC_Bloco* parsear_dlist(ADC_Contexto* pC, ADC_Bloco* pPai, const ADC_Escopo* pE, ADC_Metadados* pMetaPendente) {
    ADC_Metadados meta_copia; limpar_meta(&meta_copia);
    ADC_Metadados* pMeta = &meta_copia;
    if (pMetaPendente) { meta_copia = *pMetaPendente; limpar_meta(pMetaPendente); }
    const ADC_Linha* L = adc_leitor_espiar(&pC->leitor, 0);
    ADC_Fatia termo, marcador; int32_t ini;
    if (!L || !adc_linha_eh_item_dlist(L->texto, &termo, &marcador, &ini)) return NULL;
    ADC_Bloco* lista = novo_bloco(pC, ADC_BL_DLIST, L);
    if (!lista) return NULL;
    lista->pai = pPai; lista->marcador = marcador;
    if (++pC->profundidade > pC->opcoes.aninhamento_maximo) { pC->limite_estourado = true; pC->profundidade--; adc_leitor_pular(&pC->leitor, 1); return NULL; }
    for (;;) {
        L = adc_leitor_espiar(&pC->leitor, 0);
        if (!L) break;
        int32_t k = 0;
        while (L && linha_vazia(L->texto)) { k++; L = adc_leitor_espiar(&pC->leitor, k); }
        if (!L) break;
        ADC_Fatia t, m; int32_t id;
        if (!adc_linha_eh_item_dlist(L->texto, &t, &m, &id) || !adc_fatia_igual_f(m, marcador)) break;
        adc_leitor_pular(&pC->leitor, k);
        ADC_Bloco* item = novo_bloco(pC, ADC_BL_ITEM_DLIST, L);
        if (!item) break;
        item->pai = lista; item->marcador = marcador;
        /* termos: a linha corrente e as seguintes que também sejam "termo::" sem descrição */
        ADC_Inline** termos = ADC_NOVOS(pC->arena, ADC_Inline*, 16);
        int32_t nt = 0;
        ADC_Fatia desc_inline = adc_fatia(NULL, 0);
        int32_t desc_col = 0; ADC_Linha linha_desc = *L;
        for (;;) {
            if (nt < 16) termos[nt++] = adc_parsear_texto_inline(pC, t, L->numero, (int32_t)(t.dados - L->texto.dados) + 1, L->arquivo, ADC_SUBS_NORMAL);
            adc_leitor_pular(&pC->leitor, 1);
            if (id < L->texto.tamanho) { desc_inline = adc_fatia(L->texto.dados + id, L->texto.tamanho - id); desc_col = id + 1; linha_desc = *L; break; }
            const ADC_Linha* N = adc_leitor_espiar(&pC->leitor, 0);
            if (N && adc_linha_eh_item_dlist(N->texto, &t, &m, &id) && adc_fatia_igual_f(m, marcador)) { L = N; continue; }
            break;
        }
        item->termos = termos; item->quantidade_termos = nt;
        /* o resto do item vem do buffer (read_lines_for_list_item); sem descrição na linha do termo, a linha vazia
           antes do texto já foi tirada do buffer, então a descrição só pode começar na linha contígua */
        ADC_Linha* buf = NULL;
        int32_t nbuf = ler_linhas_do_item(pC, pE, true, ADC_LISTA_UNORDERED, marcador, desc_inline.tamanho > 0, &buf);
        ADC_Leitor salvo = inicio_buffer_do_item(pC, buf, nbuf);
        ADC_Linha* linhas = (ADC_Linha*)malloc(sizeof(ADC_Linha) * MAX_LINHAS_BLOCO);
        if (!linhas) { fim_buffer_do_item(pC, &salvo); break; }
        int32_t n = 0;
        if (desc_inline.tamanho > 0) { ADC_Linha d = linha_desc; d.texto = desc_inline; d.deslocamento += desc_col - 1; linhas[n++] = d; }
        else {
            const ADC_Linha* N = adc_leitor_espiar(&pC->leitor, 0);
            ADC_Fatia m2; ADC_VarianteLista v2; int32_t it2; ADC_Fatia t2, mk2; int32_t id2;
            /* o 1º bloco é lido como texto (text_only): até linha indentada é descrição, sem virar literal */
            if (N && !linha_vazia(N->texto) && !adc_linha_eh_item_lista(N->texto, &m2, &v2, &it2) && !adc_linha_eh_item_dlist(N->texto, &t2, &mk2, &id2) && !interrompe_paragrafo(pC, N->texto, pE)) {
                ADC_Linha d = *N; d.texto = adc_fatia_aparar(d.texto); desc_col = (int32_t)(d.texto.dados - N->texto.dados) + 1; linhas[n++] = d;
                adc_leitor_pular(&pC->leitor, 1);
            }
        }
        if (n > 0) {
            const ADC_Linha* C;
            while ((C = adc_leitor_espiar(&pC->leitor, 0)) != NULL && n < MAX_LINHAS_BLOCO) {
                if (interrompe_paragrafo(pC, C->texto, pE)) break;
                ADC_Fatia m2; ADC_VarianteLista v2; int32_t it2; ADC_Fatia t2, mk2; int32_t id2;
                if (adc_linha_eh_item_lista(C->texto, &m2, &v2, &it2)) break;
                if (adc_linha_eh_item_dlist(C->texto, &t2, &mk2, &id2)) break;
                if (eh_comentario_linha(C->texto)) { adc_leitor_pular(&pC->leitor, 1); continue; }
                ADC_Linha c = *C; c.texto = adc_fatia_aparar(c.texto); linhas[n++] = c;
                adc_leitor_pular(&pC->leitor, 1);
            }
            Juncao j = juntar_linhas(pC, linhas, n, desc_col);
            for (int32_t i = 1; i < n; i++) j.segs[i].coluna = 1;
            int32_t* mapa = NULL;
            ADC_Fatia resolvido = adc_atribs_resolver(&pC->atribs, j.texto, &pC->msgs, linhas[0].numero, true, &mapa);
            ADC_Origem o; o.segmentos = j.segs; o.quantidade = j.n; o.mapa = resolvido.dados == j.texto.dados ? NULL : mapa; o.tamanho_original = j.texto.tamanho;
            item->principal = adc_inlines_parsear(pC, resolvido, &o, ADC_SUBS_NORMAL);
            fim_em(item, &linhas[n - 1]);
        }
        free(linhas);
        blocos_do_buffer(pC, item, pE, marcador);
        fim_buffer_do_item(pC, &salvo);
        adc_bloco_anexar(lista, item);
        lista->local.fim = item->local.fim;
    }
    pC->profundidade--;
    adc_aplicar_metadados(pC, lista, pMeta);
    return lista;
}

/* ------------------------------------------------------- um bloco ---- */

ADC_Bloco* adc_blocos_parsear_um(ADC_Contexto* pC, ADC_Bloco* pPai, const ADC_Escopo* pE, ADC_Metadados* pMeta) {
    int32_t vazias = 0;   /* o "skipped" do next_block do Asciidoctor */
    for (;;) {
        const ADC_Linha* L = adc_leitor_espiar(&pC->leitor, 0);
        if (!L) return NULL;
        ADC_Fatia t = L->texto;
        ADC_Fatia td = adc_fatia_aparar_direita(t);
        if (pE->fechamento.tamanho > 0 && adc_fatia_igual_f(td, pE->fechamento)) return NULL;
        if (pE->em_tabela && t.tamanho > 0 && t.dados[0] == pE->separador_tabela && !adc_linha_eh_delimitador(t, NULL, NULL)) return NULL;
        if (linha_vazia(t) || (pE->buffer_item && adc_fatia_igual(td, "+"))) {
            adc_leitor_pular(&pC->leitor, 1); vazias++;
            if (pE->em_item_lista && !pE->buffer_item && pMeta && pMeta->vazio) return NULL;
            continue;
        }
        pC->vazias_antes_bloco = vazias;
        if (eh_comentario_linha(t)) { adc_leitor_pular(&pC->leitor, 1); continue; }
        ADC_Fatia nome, valor; bool remover;
        if (adc_linha_eh_entrada_atributo(t, &nome, &valor, &remover)) {
            /* continuação de valor com " \" no fim */
            ADC_Linha l0 = *L;
            adc_leitor_pular(&pC->leitor, 1);
            if (valor.tamanho > 0 && valor.dados[valor.tamanho - 1] == '\\') {
                ADC_Buffer b; adc_buffer_iniciar(&b);
                adc_buffer_bytes(&b, valor.dados, valor.tamanho - 1);
                const ADC_Linha* N;
                while ((N = adc_leitor_espiar(&pC->leitor, 0)) != NULL) {
                    ADC_Fatia nt = adc_fatia_aparar(N->texto);
                    if (nt.tamanho == 0) break;
                    bool cont = nt.dados[nt.tamanho - 1] == '\\';
                    adc_buffer_char(&b, ' ');
                    adc_buffer_bytes(&b, nt.dados, cont ? nt.tamanho - 1 : nt.tamanho);
                    adc_leitor_pular(&pC->leitor, 1);
                    if (!cont) break;
                }
                int32_t tam; uint8_t* d = adc_buffer_entregar(&b, &tam);
                if (d) { char* c = adc_arena_copiar(pC->arena, (const char*)d, tam); free(d); if (c) valor = adc_fatia(c, tam); }
            }
            adc_definir_atributo_documento(pC, nome, valor, remover, l0.numero);
            continue;
        }
        ADC_TipoBloco tipo_delim; char cd;
        if (adc_linha_eh_delimitador(t, &tipo_delim, &cd)) {
            if (tipo_delim == ADC_BL_COMENTARIO) {
                ADC_Fatia delim = td; adc_leitor_pular(&pC->leitor, 1);
                const ADC_Linha* N;
                while ((N = adc_leitor_espiar(&pC->leitor, 0)) != NULL) { bool f = adc_fatia_igual_f(adc_fatia_aparar_direita(N->texto), delim); adc_leitor_pular(&pC->leitor, 1); if (f) break; }
                continue;
            }
            ADC_Linha ab = *L;
            ADC_Bloco* b = parsear_delimitado(pC, pPai, pE, pMeta, tipo_delim, &ab);
            limpar_meta(pMeta);
            if (!b) { if (pC->limite_estourado) return NULL; continue; }
            return b;
        }
        if (adc_linha_eh_atributo_bloco(t)) { ler_linha_atributos(pC, pMeta, L); adc_leitor_pular(&pC->leitor, 1); continue; }
        if (eh_titulo_bloco(t)) {
            pMeta->titulo_cru = adc_fatia(t.dados + 1, td.tamanho - 1); pMeta->vazio = false;
            if (!pMeta->tem_local) { pMeta->local.inicio.linha = L->numero; pMeta->local.inicio.coluna = 1; pMeta->local.arquivo = L->arquivo; pMeta->tem_local = true; }
            pMeta->local.fim.linha = L->numero; pMeta->local.fim.coluna = adc_utf8_contar(td);
            adc_leitor_pular(&pC->leitor, 1); continue;
        }
        int32_t nivel; ADC_Fatia titulo;
        if (eh_titulo_secao(t, &nivel, &titulo)) {
            int32_t lo = adc_fatia_para_int(adc_atribs_valor_ou(&pC->atribs, "leveloffset", "0"), 0) + pC->leitor.leveloffset;
            nivel += lo; if (nivel < 0) nivel = 0;
            bool discreto = pMeta && (adc_fatia_igual(pMeta->estilo, "discrete") || adc_fatia_igual(pMeta->estilo, "float"));
            if (discreto || !pE->permite_secao) {
                if (!discreto) adc_msgs_adicionar(&pC->msgs, ADC_NIVEL_AVISO, L->numero, 1, "Título de seção dentro de um bloco não abre seção; foi tratado como título discreto. Use [discrete] para explicitar.");
                ADC_Bloco* h = novo_bloco(pC, ADC_BL_TITULO, L);
                if (!h) return NULL;
                h->nivel = nivel;
                h->titulo = adc_parsear_texto_inline(pC, titulo, L->numero, (int32_t)(titulo.dados - t.dados) + 1, L->arquivo, ADC_SUBS_TITULO);
                adc_leitor_pular(&pC->leitor, 1);
                adc_aplicar_metadados(pC, h, pMeta);
                if (h->meta.id.tamanho == 0 && adc_atribs_definido(&pC->atribs, "sectids")) { h->meta.id = adc_ctx_gerar_id(pC, adc_inlines_texto_para_id(pC->arena, h->titulo)); h->id_gerado = true; adc_ctx_registrar_id(pC, h->meta.id, h->titulo, h); }
                limpar_meta(pMeta);
                return h;
            }
            /* "= Título" no corpo: no nível do documento vira seção de nível 0 (parte), como no Asciidoctor, com erro
               quando o doctype não é book — nunca encerra o documento descartando o resto */
            bool sect0 = nivel == 0 && pPai && pPai->tipo == ADC_BL_DOCUMENTO;
            if (sect0 && !adc_fatia_igual(adc_atribs_valor_ou(&pC->atribs, "doctype", "article"), "book"))
                adc_msgs_adicionar(&pC->msgs, ADC_NIVEL_ERRO, L->numero, 1, "Seção de nível 0 ('= %.*s') no corpo de um documento '%.*s': só o doctype book admite partes. A seção foi mantida no nível 0; para subordiná-la, use '==' ou leveloffset=+1 no include.",
                    titulo.tamanho, titulo.dados, adc_atribs_valor_ou(&pC->atribs, "doctype", "article").tamanho, adc_atribs_valor_ou(&pC->atribs, "doctype", "article").dados);
            if (!sect0 && nivel <= pE->nivel_secao) return NULL; /* fecha a seção corrente */
            if (nivel > pE->nivel_secao + 1) adc_msgs_adicionar(&pC->msgs, ADC_NIVEL_AVISO, L->numero, 1, "Nível de seção %d fora de sequência (esperava até %d).", nivel, pE->nivel_secao + 1);
            ADC_Bloco* s = novo_bloco(pC, ADC_BL_SECAO, L);
            if (!s) return NULL;
            s->nivel = nivel;
            s->titulo = adc_parsear_texto_inline(pC, titulo, L->numero, (int32_t)(titulo.dados - t.dados) + 1, L->arquivo, ADC_SUBS_TITULO);
            adc_leitor_pular(&pC->leitor, 1);
            adc_aplicar_metadados(pC, s, pMeta);
            if (s->meta.id.tamanho == 0 && adc_atribs_definido(&pC->atribs, "sectids")) { s->meta.id = adc_ctx_gerar_id(pC, adc_inlines_texto_para_id(pC->arena, s->titulo)); s->id_gerado = true; adc_ctx_registrar_id(pC, s->meta.id, s->titulo, s); }
            limpar_meta(pMeta);
            pC->secoes_vistas++;
            s->pai = pPai;
            ADC_Escopo e = *pE; e.nivel_secao = nivel; e.permite_secao = true;
            adc_blocos_parsear(pC, s, &e);
            if (s->ultimo_filho) s->local.fim = s->ultimo_filho->local.fim;
            return s;
        }
        ADC_Fatia mnome, malvo, mattrs;
        if (eh_macro_bloco(t, &mnome, &malvo, &mattrs) && !adc_fatia_igual(mnome, "include")) {
            ADC_Bloco* b = novo_bloco(pC, ADC_BL_MACRO, L);
            if (!b) return NULL;
            b->forma = ADC_FORMA_MACRO; b->nome_macro = mnome;
            b->alvo = adc_atribs_resolver(&pC->atribs, malvo, &pC->msgs, L->numero, true, NULL);
            ADC_Fatia attrs = adc_atribs_resolver(&pC->atribs, mattrs, &pC->msgs, L->numero, true, NULL);
            ADC_Atributo* lista = adc_attrlist_parsear(pC->arena, attrs, false, NULL);
            if (adc_fatia_igual(mnome, "image")) { static const char* const pos[] = { "alt", "width", "height", NULL }; adc_attrlist_mapear_posicionais(pC->arena, &lista, pos); bool a; adc_atributo_obter(lista, "alt", &a); if (!a) { int32_t ult = b->alvo.tamanho; for (int32_t k = b->alvo.tamanho - 1; k >= 0; k--) { if (b->alvo.dados[k] == '.') { ult = k; break; } if (b->alvo.dados[k] == '/') break; } int32_t ini = 0; for (int32_t k = 0; k < b->alvo.tamanho; k++) if (b->alvo.dados[k] == '/') ini = k + 1; adc_atributo_definir(pC->arena, &lista, adc_fatia_cstr("alt"), adc_fatia(b->alvo.dados + ini, ult - ini)); } }
            else if (adc_fatia_igual(mnome, "video") || adc_fatia_igual(mnome, "audio")) { static const char* const pos[] = { "poster", "width", "height", NULL }; adc_attrlist_mapear_posicionais(pC->arena, &lista, pos); }
            adc_leitor_pular(&pC->leitor, 1);
            adc_aplicar_metadados(pC, b, pMeta);
            /* funde os atributos do macro aos metadados (o macro é a última linha de atributos — SDR-6) */
            for (ADC_Atributo* a = lista; a; a = a->proximo) adc_atributo_definir(pC->arena, &b->meta.atributos, a->nome, a->valor);
            b->meta.vazio = false;
            limpar_meta(pMeta);
            return b;
        }
        ADC_VarianteQuebra vq;
        if (eh_quebra(t, &vq)) {
            ADC_Bloco* b = novo_bloco(pC, ADC_BL_QUEBRA, L);
            if (!b) return NULL;
            b->variante_quebra = vq;
            adc_leitor_pular(&pC->leitor, 1);
            adc_aplicar_metadados(pC, b, pMeta);
            limpar_meta(pMeta);
            return b;
        }
        ADC_Fatia m; ADC_VarianteLista v; int32_t it;
        if (adc_linha_eh_item_lista(t, &m, &v, &it)) {
            ADC_Bloco* b = parsear_lista(pC, pPai, pE, pMeta);
            limpar_meta(pMeta);
            if (!b) { if (pC->limite_estourado) return NULL; continue; }
            return b;
        }
        ADC_Fatia termo, mk; int32_t id;
        if (adc_linha_eh_item_dlist(t, &termo, &mk, &id)) {
            ADC_Bloco* b = parsear_dlist(pC, pPai, pE, pMeta);
            limpar_meta(pMeta);
            if (!b) { if (pC->limite_estourado) return NULL; continue; }
            return b;
        }
        if (adc_eh_espaco(t.dados[0]) && !(pMeta && (adc_fatia_igual(pMeta->estilo, "normal") || adc_fatia_igual(pMeta->estilo, "paragraph")) && false)) {
            ADC_Bloco* b = parsear_literal_indentado(pC, pE, pMeta);
            limpar_meta(pMeta);
            if (!b) continue;
            return b;
        }
        if (adc_fatia_igual(td, "+") && pE->em_item_lista) return NULL;
        ADC_Bloco* b = parsear_paragrafo(pC, pE, pMeta, false, adc_fatia(NULL, 0));
        limpar_meta(pMeta);
        if (!b) { if (adc_leitor_espiar(&pC->leitor, 0) == L) adc_leitor_pular(&pC->leitor, 1); continue; }
        return b;
    }
}

void adc_blocos_parsear(ADC_Contexto* pC, ADC_Bloco* pPai, const ADC_Escopo* pE) {
    /* os metadados pendentes são do contexto: uma linha [attr] lida antes de um título que fecha a seção
       corrente tem de valer para a seção que se abre no nível de cima */
    ADC_Metadados* meta = &pC->meta_pendente;
    if (pC->profundidade_blocos++ == 0) limpar_meta(meta);
    for (;;) {
        ADC_Bloco* b = adc_blocos_parsear_um(pC, pPai, pE, meta);
        if (!b) {
            if (!meta->vazio && adc_leitor_espiar(&pC->leitor, 0) == NULL) {
                adc_msgs_adicionar(&pC->msgs, ADC_NIVEL_AVISO, meta->local.inicio.linha, 1, "Linha de atributos de bloco no fim do documento sem bloco a que se aplicar; foi ignorada.");
                limpar_meta(meta);
            } else if (!meta->vazio && !pE->permite_secao) {
                /* atributos antes do delimitador de fechamento: não há bloco; descartam-se */
                adc_msgs_adicionar(&pC->msgs, ADC_NIVEL_AVISO, meta->local.inicio.linha, 1, "Linha de atributos de bloco imediatamente antes do fim de um bloco, sem bloco a que se aplicar; foi ignorada.");
                limpar_meta(meta);
            }
            pC->profundidade_blocos--;
            return;
        }
        adc_bloco_anexar(pPai, b);
    }
}

void adc_blocos_parsear_texto(ADC_Contexto* pC, ADC_Bloco* pPai, ADC_Fatia pTexto, int32_t pLinhaBase, int32_t pArquivo) {
    /* troca o leitor por um sub-leitor sobre o texto, parseia, e restaura */
    ADC_Leitor salvo = pC->leitor;
    ADC_Leitor sub;
    adc_leitor_iniciar(&sub, pC->arena, &pC->atribs, &pC->msgs, pC->resolver, pC->contexto_resolver, pC->opcoes.includes_maximo);
    sub.arquivos = salvo.arquivos; sub.quantidade_arquivos = salvo.quantidade_arquivos;
    sub.includes_feitos = salvo.includes_feitos;
    /* abre a fonte com o índice de arquivo do pai e numeração deslocada */
    if (sub.topo + 1 < ADC_LEITOR_MAX_FONTES) {
        ADC_Fonte* f = &sub.pilha[++sub.topo];
        f->dados = pTexto.dados; f->tamanho = pTexto.tamanho; f->pos = 0; f->linha = pLinhaBase - 1; f->arquivo = pArquivo; f->nivel_offset = 0;
    }
    pC->leitor = sub;
    ADC_Escopo e; memset(&e, 0, sizeof e);
    e.permite_secao = false;
    adc_blocos_parsear(pC, pPai, &e);
    salvo.includes_feitos = pC->leitor.includes_feitos;
    salvo.arquivos = pC->leitor.arquivos; salvo.quantidade_arquivos = pC->leitor.quantidade_arquivos;
    pC->leitor = salvo;
}
