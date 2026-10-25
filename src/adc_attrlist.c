#include "adc_attrlist.h"

/* Remove escapes de barra sobre a aspa pQuote (SDR-2: n barras → n/2; ímpar escapa a aspa). */
static ADC_Fatia desescapar_aspas(ADC_Arena* pArena, ADC_Fatia pV, char pQuote) {
    bool tem = false;
    for (int32_t i = 0; i < pV.tamanho; i++) if (pV.dados[i] == '\\') { tem = true; break; }
    if (!tem) return pV;
    char* saida = (char*)adc_arena_alocar(pArena, (size_t)pV.tamanho + 1);
    if (!saida) return pV;
    int32_t o = 0;
    for (int32_t i = 0; i < pV.tamanho; i++) {
        if (pV.dados[i] == '\\') {
            int32_t n = 0; while (i + n < pV.tamanho && pV.dados[i + n] == '\\') n++;
            bool escapa = (i + n < pV.tamanho) && pV.dados[i + n] == pQuote;
            if (escapa) { for (int32_t k = 0; k < n / 2; k++) saida[o++] = '\\'; if (n % 2 == 1) { saida[o++] = pQuote; i += n; } else i += n - 1; }
            else { for (int32_t k = 0; k < n; k++) saida[o++] = '\\'; i += n - 1; }
        } else saida[o++] = pV.dados[i];
    }
    return adc_fatia(saida, o);
}

static void acrescentar(ADC_Arena* pArena, ADC_Atributo** pLista, ADC_Atributo** pUltimo, ADC_Fatia pNome, ADC_Fatia pValor) {
    ADC_Atributo* a = ADC_NOVO(pArena, ADC_Atributo);
    if (!a) return;
    a->nome = pNome; a->valor = pValor;
    if (*pUltimo) (*pUltimo)->proximo = a; else *pLista = a;
    *pUltimo = a;
}

ADC_Atributo* adc_attrlist_parsear(ADC_Arena* pArena, ADC_Fatia pTexto, bool pAtalhos, ADC_Metadados* pMeta) {
    ADC_Atributo* lista = NULL; ADC_Atributo* ultimo = NULL;
    const char* d = pTexto.dados; int32_t n = pTexto.tamanho;
    int32_t i = 0, indice = 0;
    while (i <= n) {
        /* uma entrada: [nome =] valor, terminada em ',' ou fim */
        while (i < n && adc_eh_espaco(d[i])) i++;
        indice++;
        int32_t ini = i;
        ADC_Fatia nome = adc_fatia(NULL, 0);
        ADC_Fatia valor = adc_fatia(NULL, 0);
        bool citado = false; char aspa = 0;
        /* tenta nome= */
        int32_t k = i;
        while (k < n && (adc_eh_alnum(d[k]) || d[k] == '_' || d[k] == '-' || d[k] == '.' || d[k] == ':' || d[k] == '!')) k++;
        int32_t k2 = k; while (k2 < n && adc_eh_espaco(d[k2])) k2++;
        if (k > i && k2 < n && d[k2] == '=' ) {
            nome = adc_fatia(d + i, k - i);
            i = k2 + 1;
            while (i < n && adc_eh_espaco(d[i])) i++;
        }
        if (i < n && (d[i] == '"' || d[i] == '\'')) {
            aspa = d[i];
            /* procura a aspa de fechamento não escapada (número ímpar de barras antes) */
            int32_t f = -1;
            for (int32_t q = i + 1; q < n; q++) {
                if (d[q] == aspa) {
                    int32_t barras = 0; int32_t r = q - 1; while (r > i && d[r] == '\\') { barras++; r--; }
                    if (barras % 2 == 0) {
                        /* a aspa de fechamento deve ser seguida de vírgula, espaço ou fim */
                        if (q + 1 == n || d[q + 1] == ',' || adc_eh_espaco(d[q + 1])) { f = q; break; }
                    }
                }
            }
            if (f > 0) {
                citado = true;
                valor = desescapar_aspas(pArena, adc_fatia(d + i + 1, f - i - 1), aspa);
                i = f + 1;
                while (i < n && adc_eh_espaco(d[i])) i++;
                if (i < n && d[i] == ',') i++;
            }
        }
        if (!citado) {
            int32_t f = i;
            while (f < n && d[f] != ',') f++;
            valor = adc_fatia_aparar(adc_fatia(d + i, f - i));
            i = f + 1;
        }
        (void)ini;
        if (nome.tamanho > 0) {
            if (adc_fatia_igual(nome, "roles")) nome = adc_fatia_cstr("role");
            else if (adc_fatia_igual(nome, "options")) nome = adc_fatia_cstr("opts");
            if (pMeta && (adc_fatia_igual(nome, "title") || adc_fatia_igual(nome, "reftext"))) {
                if (adc_fatia_igual(nome, "title")) { pMeta->titulo_cru = valor; pMeta->titulo_citado = citado && aspa == '\''; }
                else pMeta->reftext_cru = valor;
            }
            acrescentar(pArena, &lista, &ultimo, nome, valor);
        } else {
            if (valor.tamanho == 0) { if (i > n) break; continue; }
            if (indice == 1 && pAtalhos && !citado && pMeta) {
                if (adc_attrlist_atalhos(pArena, valor, pMeta)) {
                    /* ainda registramos o posicional como veio (informativo) */
                    char tmp[16]; int32_t t = snprintf(tmp, sizeof tmp, "$%d", indice);
                    acrescentar(pArena, &lista, &ultimo, adc_fatia(adc_arena_copiar(pArena, tmp, t), t), valor);
                    if (i > n) break;
                    continue;
                }
            }
            char tmp[16]; int32_t t2 = snprintf(tmp, sizeof tmp, "$%d", indice);
            acrescentar(pArena, &lista, &ultimo, adc_fatia(adc_arena_copiar(pArena, tmp, t2), t2), valor);
        }
        if (i > n) break;
    }
    if (pMeta) adc_attrlist_fundir(pArena, pMeta, lista);
    return lista;
}

static void acumular_lista(ADC_Arena* pArena, ADC_ListaFatias* pLista, ADC_Fatia pValor, bool pVirgulaSepara) {
    int32_t ini = 0;
    for (int32_t i = 0; i <= pValor.tamanho; i++) {
        bool sep = i == pValor.tamanho || adc_eh_espaco(pValor.dados[i]) || (pVirgulaSepara && pValor.dados[i] == ',');
        if (sep) {
            ADC_Fatia item = adc_fatia(pValor.dados + ini, i - ini);
            if (item.tamanho > 0) adc_lista_fatias_adicionar(pArena, pLista, item);
            ini = i + 1;
        }
    }
}

void adc_attrlist_fundir(ADC_Arena* pArena, ADC_Metadados* pMeta, ADC_Atributo* pNovos) {
    for (ADC_Atributo* a = pNovos; a; a = a->proximo) {
        if (adc_fatia_igual(a->nome, "role")) { acumular_lista(pArena, &pMeta->papeis, a->valor, false); continue; }
        if (adc_fatia_igual(a->nome, "opts")) { acumular_lista(pArena, &pMeta->opcoes, a->valor, true); continue; }
        if (adc_fatia_igual(a->nome, "id")) pMeta->id = a->valor;
        if (adc_fatia_igual(a->nome, "style")) pMeta->estilo = a->valor;
        if (a->nome.dados[0] == '$' && a->valor.tamanho == 0) continue; /* posicional vazio não substitui */
        adc_atributo_definir(pArena, &pMeta->atributos, a->nome, a->valor);
    }
    /* o 1º posicional sem atalhos é o estilo */
    bool achou;
    ADC_Fatia p1 = adc_atributo_obter(pMeta->atributos, "$1", &achou);
    if (achou && p1.tamanho > 0 && pMeta->estilo.tamanho == 0) {
        bool simples = true;
        for (int32_t i = 0; i < p1.tamanho; i++) if (p1.dados[i] == '#' || p1.dados[i] == '.' || p1.dados[i] == '%' || adc_eh_espaco(p1.dados[i])) { simples = false; break; }
        if (simples) pMeta->estilo = p1;
    }
    pMeta->vazio = false;
}

bool adc_attrlist_atalhos(ADC_Arena* pArena, ADC_Fatia pValor, ADC_Metadados* pMeta) {
    /* style#id.role%opt — sem espaços; cada parte não vazia */
    for (int32_t i = 0; i < pValor.tamanho; i++) if (adc_eh_espaco(pValor.dados[i])) return false;
    bool tem_marca = false;
    for (int32_t i = 0; i < pValor.tamanho; i++) if (pValor.dados[i] == '#' || pValor.dados[i] == '.' || pValor.dados[i] == '%') { tem_marca = true; break; }
    if (!tem_marca) return false;
    /* valida: nenhuma parte vazia */
    int32_t ini = 0; char tipo = 's';
    for (int32_t i = 0; i <= pValor.tamanho; i++) {
        if (i == pValor.tamanho || pValor.dados[i] == '#' || pValor.dados[i] == '.' || pValor.dados[i] == '%') {
            if (i - ini == 0 && !(tipo == 's' && ini == 0)) return false;
            if (i < pValor.tamanho) { tipo = pValor.dados[i]; ini = i + 1; }
        }
    }
    ini = 0; tipo = 's';
    for (int32_t i = 0; i <= pValor.tamanho; i++) {
        if (i == pValor.tamanho || pValor.dados[i] == '#' || pValor.dados[i] == '.' || pValor.dados[i] == '%') {
            ADC_Fatia parte = adc_fatia(pValor.dados + ini, i - ini);
            if (parte.tamanho > 0) {
                switch (tipo) {
                    case 's': pMeta->estilo = parte; adc_atributo_definir(pArena, &pMeta->atributos, adc_fatia_cstr("style"), parte); break;
                    case '#': pMeta->id = parte; adc_atributo_definir(pArena, &pMeta->atributos, adc_fatia_cstr("id"), parte); break;
                    case '.': adc_lista_fatias_adicionar(pArena, &pMeta->papeis, parte); break;
                    case '%': adc_lista_fatias_adicionar(pArena, &pMeta->opcoes, parte); break;
                }
            }
            if (i < pValor.tamanho) { tipo = pValor.dados[i]; ini = i + 1; }
        }
    }
    pMeta->vazio = false;
    return true;
}

void adc_attrlist_mapear_posicionais(ADC_Arena* pArena, ADC_Atributo** pLista, const char* const* pNomes) {
    for (int32_t i = 0; pNomes[i]; i++) {
        char tmp[16]; snprintf(tmp, sizeof tmp, "$%d", i + 1);
        bool achou;
        ADC_Fatia v = adc_atributo_obter(*pLista, tmp, &achou);
        if (!achou || v.tamanho == 0) continue;
        /* SDR-6: o posicional só sobrescreve o nomeado se vier depois na lista (ordem do documento) */
        bool nomeado_depois = false; bool viu_pos = false;
        for (ADC_Atributo* a = *pLista; a; a = a->proximo) {
            if (adc_fatia_igual(a->nome, tmp)) viu_pos = true;
            else if (adc_fatia_igual(a->nome, pNomes[i]) && viu_pos) nomeado_depois = true;
        }
        if (nomeado_depois) continue;
        adc_atributo_definir(pArena, pLista, adc_fatia_cstr(pNomes[i]), v);
    }
}
