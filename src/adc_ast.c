#include "adc_ast.h"

void adc_bloco_anexar(ADC_Bloco* pPai, ADC_Bloco* pFilho) {
    pFilho->pai = pPai;
    pFilho->proximo = NULL;
    if (pPai->ultimo_filho) pPai->ultimo_filho->proximo = pFilho; else pPai->filhos = pFilho;
    pPai->ultimo_filho = pFilho;
    pPai->quantidade_filhos++;
}

bool adc_lista_fatias_contem(const ADC_ListaFatias* pLista, const char* pItem) {
    for (int32_t i = 0; i < pLista->quantidade; i++) if (adc_fatia_igual(pLista->itens[i], pItem)) return true;
    return false;
}

void adc_lista_fatias_adicionar(ADC_Arena* pArena, ADC_ListaFatias* pLista, ADC_Fatia pItem) {
    if (pItem.tamanho == 0) return;
    for (int32_t i = 0; i < pLista->quantidade; i++) if (adc_fatia_igual_f(pLista->itens[i], pItem)) return;
    if (pLista->quantidade == pLista->capacidade) {
        int32_t nova = pLista->capacidade ? pLista->capacidade * 2 : 4;
        ADC_Fatia* itens = ADC_NOVOS(pArena, ADC_Fatia, nova);
        if (!itens) return;
        if (pLista->quantidade) memcpy(itens, pLista->itens, sizeof(ADC_Fatia) * (size_t)pLista->quantidade);
        pLista->itens = itens;
        pLista->capacidade = nova;
    }
    pLista->itens[pLista->quantidade++] = pItem;
}

ADC_Fatia adc_atributo_obter(const ADC_Atributo* pLista, const char* pNome, bool* pEncontrado) {
    for (const ADC_Atributo* a = pLista; a; a = a->proximo) {
        if (adc_fatia_igual(a->nome, pNome)) { if (pEncontrado) *pEncontrado = true; return a->valor; }
    }
    if (pEncontrado) *pEncontrado = false;
    return adc_fatia(NULL, 0);
}

void adc_atributo_definir(ADC_Arena* pArena, ADC_Atributo** pLista, ADC_Fatia pNome, ADC_Fatia pValor) {
    ADC_Atributo* ultimo = NULL;
    for (ADC_Atributo* a = *pLista; a; a = a->proximo) {
        if (adc_fatia_igual_f(a->nome, pNome)) { a->valor = pValor; return; }
        ultimo = a;
    }
    ADC_Atributo* n = ADC_NOVO(pArena, ADC_Atributo);
    if (!n) return;
    n->nome = pNome; n->valor = pValor;
    if (ultimo) ultimo->proximo = n; else *pLista = n;
}

ADC_Fatia adc_tipo_bloco_nome(ADC_TipoBloco pTipo) {
    switch (pTipo) {
        case ADC_BL_DOCUMENTO: return adc_fatia_cstr("document");
        case ADC_BL_SECAO: return adc_fatia_cstr("section");
        case ADC_BL_TITULO: return adc_fatia_cstr("heading");
        case ADC_BL_PARAGRAFO: return adc_fatia_cstr("paragraph");
        case ADC_BL_LISTA: return adc_fatia_cstr("list");
        case ADC_BL_ITEM_LISTA: return adc_fatia_cstr("listItem");
        case ADC_BL_DLIST: return adc_fatia_cstr("dlist");
        case ADC_BL_ITEM_DLIST: return adc_fatia_cstr("dlistItem");
        case ADC_BL_LISTING: return adc_fatia_cstr("listing");
        case ADC_BL_LITERAL: return adc_fatia_cstr("literal");
        case ADC_BL_PASS: return adc_fatia_cstr("pass");
        case ADC_BL_STEM: return adc_fatia_cstr("stem");
        case ADC_BL_VERSE: return adc_fatia_cstr("verse");
        case ADC_BL_ADMONICAO: return adc_fatia_cstr("admonition");
        case ADC_BL_EXEMPLO: return adc_fatia_cstr("example");
        case ADC_BL_SIDEBAR: return adc_fatia_cstr("sidebar");
        case ADC_BL_OPEN: return adc_fatia_cstr("open");
        case ADC_BL_QUOTE: return adc_fatia_cstr("quote");
        case ADC_BL_TABELA: return adc_fatia_cstr("table");
        case ADC_BL_CELULA: return adc_fatia_cstr("cell");
        case ADC_BL_QUEBRA: return adc_fatia_cstr("break");
        case ADC_BL_MACRO: return adc_fatia_cstr("macro");
        default: return adc_fatia_cstr("unknown");
    }
}

ADC_Fatia adc_variante_adm_nome(ADC_VarianteAdm pV) {
    switch (pV) {
        case ADC_ADM_NOTE: return adc_fatia_cstr("note");
        case ADC_ADM_TIP: return adc_fatia_cstr("tip");
        case ADC_ADM_IMPORTANT: return adc_fatia_cstr("important");
        case ADC_ADM_WARNING: return adc_fatia_cstr("warning");
        case ADC_ADM_CAUTION: return adc_fatia_cstr("caution");
        default: return adc_fatia_cstr("note");
    }
}

const char* adc_variante_adm_rotulo(ADC_VarianteAdm pV) {
    switch (pV) {
        case ADC_ADM_NOTE: return "Nota";
        case ADC_ADM_TIP: return "Dica";
        case ADC_ADM_IMPORTANT: return "Importante";
        case ADC_ADM_WARNING: return "Atenção";
        case ADC_ADM_CAUTION: return "Cuidado";
        default: return "Nota";
    }
}
