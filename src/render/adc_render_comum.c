#include "../adc_render.h"

void adc_html_escapar(ADC_Buffer* pB, ADC_Fatia pT) {
    for (int32_t i = 0; i < pT.tamanho; i++) {
        char c = pT.dados[i];
        if (c == '&') adc_buffer_texto(pB, "&amp;");
        else if (c == '<') adc_buffer_texto(pB, "&lt;");
        else if (c == '>') adc_buffer_texto(pB, "&gt;");
        else adc_buffer_char(pB, c);
    }
}

void adc_html_escapar_atributo(ADC_Buffer* pB, ADC_Fatia pT) {
    for (int32_t i = 0; i < pT.tamanho; i++) {
        char c = pT.dados[i];
        if (c == '&') adc_buffer_texto(pB, "&amp;");
        else if (c == '<') adc_buffer_texto(pB, "&lt;");
        else if (c == '>') adc_buffer_texto(pB, "&gt;");
        else if (c == '"') adc_buffer_texto(pB, "&quot;");
        else adc_buffer_char(pB, c);
    }
}

void adc_render_numero_secao(ADC_Buffer* pB, const int32_t* pContadores, int32_t pNivel) {
    for (int32_t k = 1; k <= pNivel; k++) { adc_buffer_int(pB, pContadores[k]); adc_buffer_char(pB, '.'); }
}

bool adc_render_legenda(ADC_Contexto* pC, const ADC_Bloco* pB, ADC_Buffer* pSaida, const char* pAtributo, int32_t* pContador) {
    if (!pB->titulo) return false;
    bool a; ADC_Fatia legenda = adc_atributo_obter(pB->meta.atributos, "caption", &a);
    if (a) { adc_buffer_fatia(pSaida, legenda); return true; }
    ADC_Fatia rotulo;
    if (!adc_atribs_obter_cstr(&pC->atribs, pAtributo, &rotulo) || rotulo.tamanho == 0) return false;
    (*pContador)++;
    adc_buffer_fatia(pSaida, rotulo); adc_buffer_char(pSaida, ' '); adc_buffer_int(pSaida, *pContador); adc_buffer_texto(pSaida, ". ");
    return true;
}

const ADC_Inline* adc_render_texto_xref(ADC_Contexto* pC, ADC_Fatia pAlvo) {
    ADC_RegistroId* r = adc_ctx_achar_id(pC, pAlvo);
    if (!r) return NULL;
    if (r->texto) return r->texto;
    if (r->bloco && r->bloco->titulo) return r->bloco->titulo;
    return NULL;
}

ADC_Fatia adc_render_rotulo_admonicao(ADC_Contexto* pC, ADC_VarianteAdm pV) {
    const char* nome = pV == ADC_ADM_NOTE ? "note-caption" : pV == ADC_ADM_TIP ? "tip-caption" : pV == ADC_ADM_IMPORTANT ? "important-caption" : pV == ADC_ADM_WARNING ? "warning-caption" : "caution-caption";
    ADC_Fatia v;
    if (adc_atribs_obter_cstr(&pC->atribs, nome, &v)) return v;
    return adc_fatia_cstr(adc_variante_adm_rotulo(pV));
}
