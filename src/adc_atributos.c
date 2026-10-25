#include "adc_atributos.h"
#include <stdio.h>

static uint32_t hash_fatia(ADC_Fatia pF) {
    uint32_t h = 2166136261u;
    for (int32_t i = 0; i < pF.tamanho; i++) { h ^= (unsigned char)adc_minusculo(pF.dados[i]); h *= 16777619u; }
    return h;
}

void adc_atribs_iniciar(ADC_TabelaAtributos* pT, ADC_Arena* pArena) {
    memset(pT, 0, sizeof *pT);
    pT->arena = pArena;
}

bool adc_atribs_nome_valido(ADC_Fatia pNome) {
    if (pNome.tamanho == 0) return false;
    for (int32_t i = 0; i < pNome.tamanho; i++) {
        char c = pNome.dados[i];
        if (adc_eh_alnum(c) || c == '_') continue;
        if (c == '-' && i > 0) continue;
        return false;
    }
    return true;
}

ADC_Fatia adc_atribs_normalizar_nome(ADC_Arena* pArena, ADC_Fatia pNome) {
    bool tem_maiuscula = false;
    for (int32_t i = 0; i < pNome.tamanho; i++) if (pNome.dados[i] >= 'A' && pNome.dados[i] <= 'Z') { tem_maiuscula = true; break; }
    if (!tem_maiuscula) return pNome;
    char* c = adc_arena_copiar(pArena, pNome.dados, pNome.tamanho);
    if (!c) return pNome;
    for (int32_t i = 0; i < pNome.tamanho; i++) c[i] = adc_minusculo(c[i]);
    return adc_fatia(c, pNome.tamanho);
}

static ADC_EntradaAtributo* achar(const ADC_TabelaAtributos* pT, ADC_Fatia pNome) {
    uint32_t b = hash_fatia(pNome) % ADC_ATRIB_BALDES;
    for (ADC_EntradaAtributo* e = pT->baldes[b]; e; e = e->proxima) {
        if (e->nome.tamanho != pNome.tamanho) continue;
        bool igual = true;
        for (int32_t i = 0; i < pNome.tamanho; i++) if (adc_minusculo(e->nome.dados[i]) != adc_minusculo(pNome.dados[i])) { igual = false; break; }
        if (igual) return e;
    }
    return NULL;
}

bool adc_atribs_definir(ADC_TabelaAtributos* pT, ADC_Fatia pNome, ADC_Fatia pValor, bool pTravar) {
    ADC_EntradaAtributo* e = achar(pT, pNome);
    if (e) {
        if (e->travado && !pTravar) return false;
        e->valor = pValor; e->removido = false; e->travado = e->travado || pTravar;
        return true;
    }
    e = ADC_NOVO(pT->arena, ADC_EntradaAtributo);
    if (!e) return false;
    e->nome = adc_atribs_normalizar_nome(pT->arena, pNome);
    e->valor = pValor; e->travado = pTravar;
    uint32_t b = hash_fatia(pNome) % ADC_ATRIB_BALDES;
    e->proxima = pT->baldes[b];
    pT->baldes[b] = e;
    if (pT->ultima) pT->ultima->proxima_ordem = e; else pT->ordem = e;
    pT->ultima = e;
    pT->quantidade++;
    return true;
}

void adc_atribs_marcar_documento(ADC_TabelaAtributos* pT, ADC_Fatia pNome) {
    ADC_EntradaAtributo* e = achar(pT, pNome);
    if (e) e->do_documento = true;
}

bool adc_atribs_remover(ADC_TabelaAtributos* pT, ADC_Fatia pNome) {
    ADC_EntradaAtributo* e = achar(pT, pNome);
    if (e) {
        if (e->travado) return false;
        e->removido = true; e->valor = adc_fatia(NULL, 0);
        return true;
    }
    e = ADC_NOVO(pT->arena, ADC_EntradaAtributo);
    if (!e) return false;
    e->nome = adc_atribs_normalizar_nome(pT->arena, pNome);
    e->removido = true;
    uint32_t b = hash_fatia(pNome) % ADC_ATRIB_BALDES;
    e->proxima = pT->baldes[b];
    pT->baldes[b] = e;
    if (pT->ultima) pT->ultima->proxima_ordem = e; else pT->ordem = e;
    pT->ultima = e;
    pT->quantidade++;
    return true;
}

bool adc_atribs_obter(const ADC_TabelaAtributos* pT, ADC_Fatia pNome, ADC_Fatia* pValor) {
    ADC_EntradaAtributo* e = achar(pT, pNome);
    if (!e || e->removido) return false;
    if (pValor) *pValor = e->valor;
    return true;
}

bool adc_atribs_obter_cstr(const ADC_TabelaAtributos* pT, const char* pNome, ADC_Fatia* pValor) {
    return adc_atribs_obter(pT, adc_fatia_cstr(pNome), pValor);
}

bool adc_atribs_definido(const ADC_TabelaAtributos* pT, const char* pNome) {
    return adc_atribs_obter(pT, adc_fatia_cstr(pNome), NULL);
}

ADC_Fatia adc_atribs_valor_ou(const ADC_TabelaAtributos* pT, const char* pNome, const char* pPadrao) {
    ADC_Fatia v;
    if (adc_atribs_obter(pT, adc_fatia_cstr(pNome), &v)) return v;
    return adc_fatia_cstr(pPadrao);
}

bool adc_atribs_intrinseco(ADC_Fatia pNome, ADC_Fatia* pValor) {
    static const struct { const char* nome; const char* valor; } tabela[] = {
        { "blank", "" }, { "empty", "" }, { "sp", " " }, { "nbsp", "&#160;" }, { "zwsp", "&#8203;" },
        { "wj", "&#8288;" }, { "apos", "&#39;" }, { "quot", "&#34;" }, { "lsquo", "&#8216;" },
        { "rsquo", "&#8217;" }, { "ldquo", "&#8220;" }, { "rdquo", "&#8221;" },
        { "deg", "&#176;" }, { "plus", "&#43;" }, { "brvbar", "&#166;" }, { "vbar", "|" },
        { "amp", "&" }, { "lt", "<" }, { "gt", ">" }, { "startsb", "[" }, { "endsb", "]" },
        { "caret", "^" }, { "asterisk", "*" }, { "tilde", "~" }, { "backslash", "\\" },
        { "backtick", "`" }, { "two-colons", "::" }, { "two-semicolons", ";;" }, { "cpp", "C&#43;&#43;" },
        { "pp", "&#43;&#43;" }, { "zwj", "&#8205;" },
    };
    for (size_t i = 0; i < sizeof tabela / sizeof tabela[0]; i++) {
        if (adc_fatia_igual(pNome, tabela[i].nome)) { if (pValor) *pValor = adc_fatia_cstr(tabela[i].valor); return true; }
    }
    return false;
}

static void mapa_anexar(int32_t** pMapa, int32_t* pCap, int32_t pIndice, int32_t pOrigem) {
    if (!pMapa || !*pMapa) return;
    if (pIndice >= *pCap) return; /* caller dimensiona; nunca deve acontecer */
    (*pMapa)[pIndice] = pOrigem;
}

ADC_Fatia adc_atribs_resolver(ADC_TabelaAtributos* pT, ADC_Fatia pTexto, ADC_Mensagens* pMsgs,
                              int32_t pLinha, bool pAvisar, int32_t** pMapa) {
    /* caminho rápido: sem '{' nem '\\{' não há nada a fazer */
    bool tem_chave = false;
    for (int32_t i = 0; i < pTexto.tamanho; i++) if (pTexto.dados[i] == '{') { tem_chave = true; break; }
    if (!tem_chave) { if (pMapa) *pMapa = NULL; return pTexto; }

    ADC_Buffer saida; adc_buffer_iniciar(&saida);
    /* mapa dinâmico (malloc) — copiado para a arena ao fim */
    int32_t cap_mapa = pTexto.tamanho + 1, n_mapa = 0;
    int32_t* mapa = NULL;
    if (pMapa) { mapa = (int32_t*)malloc(sizeof(int32_t) * (size_t)cap_mapa); if (!mapa) pMapa = NULL; }
    #define EMITIR(ch, origem) do { adc_buffer_char(&saida, (ch)); if (mapa) { if (n_mapa >= cap_mapa) { cap_mapa *= 2; int32_t* nm = (int32_t*)realloc(mapa, sizeof(int32_t) * (size_t)cap_mapa); if (!nm) { free(mapa); mapa = NULL; } else mapa = nm; } if (mapa) mapa[n_mapa] = (origem); n_mapa++; } } while (0)

    const char* d = pTexto.dados; int32_t n = pTexto.tamanho;
    int32_t i = 0;
    while (i < n) {
        char c = d[i];
        if (c == '\\' && i + 1 < n && d[i + 1] == '{') {
            /* \{nome} escapa a referência: emite "{" e segue; a localização do "{" é a da barra */
            int32_t fecha = -1;
            for (int32_t k = i + 2; k < n && k - i < 256; k++) { if (d[k] == '}') { fecha = k; break; } if (d[k] == '{' || d[k] == ' ') break; }
            if (fecha > 0) {
                EMITIR('{', i);
                for (int32_t k = i + 2; k <= fecha; k++) EMITIR(d[k], k);
                i = fecha + 1;
                continue;
            }
            EMITIR(c, i); i++;
            continue;
        }
        if (c == '{') {
            int32_t fecha = -1;
            for (int32_t k = i + 1; k < n && k - i < 256; k++) {
                char ck = d[k];
                if (ck == '}') { fecha = k; break; }
                if (!(adc_eh_alnum(ck) || ck == '_' || ck == '-' || ck == ':' || ck == ' ' || ck == '.' || ck == '/' || ck == '+' || ck == '@' || ck == '%' || ck == '(' || ck == ')' || ck == ',' || ck == '=' || ck == '!' || ck == '?' || ck == '\'' || ck == '"' || ck == '&' || ck == ';' || ck == '#' || ck == '*' || ((unsigned char)ck >= 0x80))) break;
            }
            if (fecha > 0 && fecha > i + 1) {
                ADC_Fatia nome = adc_fatia(d + i + 1, fecha - i - 1);
                ADC_Fatia valor = adc_fatia(NULL, 0); bool ok = false;
                if (adc_fatia_comeca(nome, "counter:") || adc_fatia_comeca(nome, "counter2:")) {
                    bool silencioso = nome.dados[7] == '2';
                    ADC_Fatia resto = adc_fatia(nome.dados + (silencioso ? 9 : 8), nome.tamanho - (silencioso ? 9 : 8));
                    int32_t sep = adc_fatia_indice(resto, ':');
                    ADC_Fatia cnome = sep >= 0 ? adc_fatia(resto.dados, sep) : resto;
                    ADC_Fatia inicial = sep >= 0 ? adc_fatia(resto.dados + sep + 1, resto.tamanho - sep - 1) : adc_fatia(NULL, 0);
                    ADC_Fatia atual;
                    char tmp[32]; int32_t tam;
                    if (adc_atribs_obter(pT, cnome, &atual) && atual.tamanho > 0) {
                        if (atual.tamanho == 1 && adc_eh_alfa(atual.dados[0])) { tmp[0] = (char)(atual.dados[0] + 1); tam = 1; }
                        else { tam = snprintf(tmp, sizeof tmp, "%d", adc_fatia_para_int(atual, 0) + 1); }
                    } else if (inicial.tamanho > 0) {
                        tam = inicial.tamanho < 31 ? inicial.tamanho : 31; memcpy(tmp, inicial.dados, (size_t)tam);
                    } else { tmp[0] = '1'; tam = 1; }
                    char* novo = adc_arena_copiar(pT->arena, tmp, tam);
                    if (novo && adc_atribs_nome_valido(cnome)) {
                        adc_atribs_definir(pT, cnome, adc_fatia(novo, tam), false);
                        if (!silencioso) { valor = adc_fatia(novo, tam); ok = true; }
                        else { valor = adc_fatia(NULL, 0); ok = true; }
                    }
                } else if (adc_fatia_comeca(nome, "set:")) {
                    ADC_Fatia resto = adc_fatia(nome.dados + 4, nome.tamanho - 4);
                    int32_t sep = adc_fatia_indice(resto, ':');
                    ADC_Fatia snome = sep >= 0 ? adc_fatia(resto.dados, sep) : resto;
                    ADC_Fatia svalor = sep >= 0 ? adc_fatia(resto.dados + sep + 1, resto.tamanho - sep - 1) : adc_fatia("", 0);
                    bool apagar = snome.tamanho > 0 && snome.dados[snome.tamanho - 1] == '!';
                    if (apagar) snome.tamanho--;
                    if (adc_atribs_nome_valido(snome)) {
                        if (apagar) adc_atribs_remover(pT, snome); else adc_atribs_definir(pT, snome, svalor, false);
                        valor = adc_fatia(NULL, 0); ok = true;
                    }
                } else if (adc_atribs_nome_valido(nome)) {
                    if (adc_atribs_obter(pT, nome, &valor)) ok = true;
                    else if (adc_atribs_intrinseco(nome, &valor)) ok = true;
                }
                if (ok) {
                    /* a localização do texto resolvido é a da referência: 1º byte → início, demais → fim */
                    for (int32_t k = 0; k < valor.tamanho; k++) EMITIR(valor.dados[k], k == 0 ? i : fecha);
                    i = fecha + 1;
                    continue;
                }
                if (adc_atribs_nome_valido(nome)) {
                    /* attribute-missing: skip (padrão, silencioso — como o Asciidoctor), warn (avisa e mantém),
                       drop (remove a referência), drop-line (aqui equivale a drop) */
                    ADC_Fatia politica = adc_atribs_valor_ou(pT, "attribute-missing", "skip");
                    if (pAvisar && pMsgs && adc_fatia_igual(politica, "warn"))
                        adc_msgs_adicionar(pMsgs, ADC_NIVEL_AVISO, pLinha, i + 1,
                            "Referência ao atributo '%.*s' não definido: o texto {%.*s} foi mantido como está (attribute-missing=warn).",
                            nome.tamanho, nome.dados, nome.tamanho, nome.dados);
                    if (adc_fatia_igual(politica, "drop") || adc_fatia_igual(politica, "drop-line")) { i = fecha + 1; continue; }
                }
            }
            EMITIR(c, i); i++;
            continue;
        }
        EMITIR(c, i); i++;
    }
    #undef EMITIR
    int32_t tam;
    uint8_t* dados = adc_buffer_entregar(&saida, &tam);
    if (!dados) { free(mapa); if (pMapa) *pMapa = NULL; return pTexto; }
    char* copia = adc_arena_copiar(pT->arena, (const char*)dados, tam);
    free(dados);
    if (!copia) { free(mapa); if (pMapa) *pMapa = NULL; return pTexto; }
    if (pMapa) {
        if (mapa) {
            int32_t* m = ADC_NOVOS(pT->arena, int32_t, tam + 1);
            if (m) { memcpy(m, mapa, sizeof(int32_t) * (size_t)tam); m[tam] = n; }
            *pMapa = m;
        } else *pMapa = NULL;
    }
    free(mapa);
    return adc_fatia(copia, tam);
}
