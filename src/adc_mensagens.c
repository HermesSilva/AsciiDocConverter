#include "adc_mensagens.h"
#include "adc_json.h"
#include <stdarg.h>
#include <stdio.h>

void adc_msgs_iniciar(ADC_Mensagens* pM, ADC_Arena* pArena) {
    pM->arena = pArena; pM->primeira = pM->ultima = NULL;
    pM->quantidade = 0; pM->erros = 0; pM->limite = 1000; pM->estourou = false;
    pM->idioma = ADC_IDIOMA_EN;   /* DAD-10: inglês sem a opção "idioma" */
    pM->arquivo_corrente = NULL; pM->com_arquivo = false;
}

bool adc_idioma_ler(ADC_Fatia pTexto, ADC_Idioma* pIdioma) {
    ADC_Fatia t = adc_fatia_aparar(pTexto);
    if (t.tamanho == 2 && adc_minusculo(t.dados[0]) == 'e' && adc_minusculo(t.dados[1]) == 'n') { *pIdioma = ADC_IDIOMA_EN; return true; }
    if (t.tamanho >= 2 && adc_minusculo(t.dados[0]) == 'p' && adc_minusculo(t.dados[1]) == 't') { *pIdioma = ADC_IDIOMA_PT_BR; return true; }
    return false;
}

void adc_msgs_adicionar(ADC_Mensagens* pM, ADC_Nivel pNivel, int32_t pLinha, int32_t pColuna, const char* pFormato, ...) {
    if (pNivel == ADC_NIVEL_ERRO) pM->erros++;
    if (pM->quantidade >= pM->limite) {
        if (!pM->estourou) {
            pM->estourou = true;
            ADC_Mensagem* m = ADC_NOVO(pM->arena, ADC_Mensagem);
            if (!m) return;
            m->nivel = ADC_NIVEL_AVISO; m->linha = 0; m->coluna = 0;
            m->texto = adc_msgs_tr(pM, "Limite de mensagens atingido: as demais foram omitidas. Corrija as primeiras e converta de novo.");
            m->arquivo = pM->arquivo_corrente;
            if (pM->ultima) pM->ultima->proxima = m; else pM->primeira = m;
            pM->ultima = m; pM->quantidade++;
        }
        return;
    }
    char tmp[1024];
    va_list ap; va_start(ap, pFormato);
    int n = vsnprintf(tmp, sizeof tmp, adc_msgs_tr(pM, pFormato), ap);
    va_end(ap);
    if (n < 0) n = 0;
    if (n >= (int)sizeof tmp) n = (int)sizeof tmp - 1;
    ADC_Mensagem* m = ADC_NOVO(pM->arena, ADC_Mensagem);
    if (!m) return;
    m->nivel = pNivel; m->linha = pLinha; m->coluna = pColuna;
    m->texto = adc_arena_copiar(pM->arena, tmp, n);
    m->arquivo = pM->arquivo_corrente;
    if (!m->texto) return;
    if (pM->ultima) pM->ultima->proxima = m; else pM->primeira = m;
    pM->ultima = m;
    pM->quantidade++;
}

void adc_msgs_para_json(const ADC_Mensagens* pM, ADC_Buffer* pSaida) {
    adc_buffer_char(pSaida, '[');
    bool primeiro = true;
    for (const ADC_Mensagem* m = pM->primeira; m; m = m->proxima) {
        if (!primeiro) adc_buffer_char(pSaida, ',');
        primeiro = false;
        adc_buffer_texto(pSaida, "{\"nivel\":");
        adc_buffer_texto(pSaida, m->nivel == ADC_NIVEL_ERRO ? "\"erro\"" : "\"aviso\"");
        adc_buffer_texto(pSaida, ",\"linha\":"); adc_buffer_int(pSaida, m->linha);
        adc_buffer_texto(pSaida, ",\"coluna\":"); adc_buffer_int(pSaida, m->coluna);
        adc_buffer_texto(pSaida, ",\"texto\":");
        adc_json_escrever_texto(pSaida, adc_fatia_cstr(m->texto));
        if (pM->com_arquivo) {
            adc_buffer_texto(pSaida, ",\"arquivo\":");
            adc_json_escrever_texto(pSaida, adc_fatia_cstr(m->arquivo ? m->arquivo : ""));
        }
        adc_buffer_char(pSaida, '}');
    }
    adc_buffer_char(pSaida, ']');
}
