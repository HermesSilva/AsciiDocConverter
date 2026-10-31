#include "adc_contexto.h"
#include <stdio.h>

void adc_ctx_registrar_id(ADC_Contexto* pC, ADC_Fatia pId, ADC_Inline* pTexto, ADC_Bloco* pBloco) {
    if (pId.tamanho == 0) return;
    for (ADC_RegistroId* r = pC->ids; r; r = r->proximo) {
        if (adc_fatia_igual_f(r->id, pId)) {
            adc_msgs_adicionar(&pC->msgs, ADC_NIVEL_AVISO, pBloco ? pBloco->local.inicio.linha : 0, 0,
                "O id '%.*s' está definido mais de uma vez; referências a ele apontarão para a primeira ocorrência.", pId.tamanho, pId.dados);
            return;
        }
    }
    ADC_RegistroId* r = ADC_NOVO(pC->arena, ADC_RegistroId);
    if (!r) return;
    r->id = pId; r->texto = pTexto; r->bloco = pBloco;
    r->proximo = pC->ids; pC->ids = r;
}

ADC_RegistroId* adc_ctx_achar_id(ADC_Contexto* pC, ADC_Fatia pId) {
    for (ADC_RegistroId* r = pC->ids; r; r = r->proximo) if (adc_fatia_igual_f(r->id, pId)) return r;
    return NULL;
}

static void texto_puro_rec(ADC_Buffer* pB, const ADC_Inline* pI, bool pParaId) {
    for (; pI; pI = pI->proximo) {
        switch (pI->tipo) {
            case ADC_IN_TEXTO: adc_buffer_fatia(pB, pI->valor); break;
            case ADC_IN_RAW:
                if (pParaId) break;
                if (pI->valor.dados == NULL) { texto_puro_rec(pB, pI->filhos, pParaId); break; }
                for (int32_t i = 0; i < pI->valor.tamanho; i++) {
                    char c = pI->valor.dados[i];
                    if (c == '&') {
                        int32_t f = i + 1; while (f < pI->valor.tamanho && pI->valor.dados[f] != ';' && f - i < 12) f++;
                        if (f < pI->valor.tamanho && pI->valor.dados[f] == ';') { ADC_Inline tmp; memset(&tmp, 0, sizeof tmp); tmp.tipo = ADC_IN_CHARREF; tmp.valor = adc_fatia(pI->valor.dados + i + 1, f - i - 1); texto_puro_rec(pB, &tmp, false); i = f; continue; }
                    }
                    if (c == '<') { int32_t f = i; while (f < pI->valor.tamanho && pI->valor.dados[f] != '>') f++; i = f; continue; }
                    adc_buffer_char(pB, c);
                }
                break;
            case ADC_IN_CHARREF: {
                if (pParaId) break;
                /* entidades numéricas viram o caractere; nomeadas comuns também */
                ADC_Fatia v = pI->valor;
                if (v.tamanho > 1 && v.dados[0] == '#') {
                    uint32_t cp = 0; bool hex = v.dados[1] == 'x' || v.dados[1] == 'X';
                    for (int32_t k = hex ? 2 : 1; k < v.tamanho; k++) {
                        char c = v.dados[k];
                        int d = (c >= '0' && c <= '9') ? c - '0' : (c >= 'a' && c <= 'f') ? c - 'a' + 10 : (c >= 'A' && c <= 'F') ? c - 'A' + 10 : -1;
                        if (d < 0) break;
                        cp = hex ? cp * 16 + (uint32_t)d : cp * 10 + (uint32_t)d;
                    }
                    char tmp[4]; int32_t n = adc_utf8_codificar(cp ? cp : '?', tmp);
                    adc_buffer_bytes(pB, tmp, n);
                } else if (adc_fatia_igual(v, "amp")) adc_buffer_char(pB, '&');
                else if (adc_fatia_igual(v, "lt")) adc_buffer_char(pB, '<');
                else if (adc_fatia_igual(v, "gt")) adc_buffer_char(pB, '>');
                else if (adc_fatia_igual(v, "quot")) adc_buffer_char(pB, '"');
                else if (adc_fatia_igual(v, "nbsp")) adc_buffer_texto(pB, "\xC2\xA0");
                else { adc_buffer_char(pB, '&'); adc_buffer_fatia(pB, v); adc_buffer_char(pB, ';'); }
                break;
            }
            case ADC_IN_QUEBRA_LINHA: adc_buffer_char(pB, '\n'); break;
            case ADC_IN_IMAGEM: { bool a; ADC_Fatia alt = adc_atributo_obter(pI->atributos, "alt", &a); if (a) adc_buffer_fatia(pB, alt); break; }
            case ADC_IN_NOTA: case ADC_IN_ANCORA: case ADC_IN_INDICE: break;
            default: texto_puro_rec(pB, pI->filhos, pParaId); break;
        }
    }
}

static ADC_Fatia texto_puro_com(ADC_Arena* pArena, const ADC_Inline* pInlines, bool pParaId) {
    ADC_Buffer b; adc_buffer_iniciar(&b);
    texto_puro_rec(&b, pInlines, pParaId);
    int32_t n; uint8_t* d = adc_buffer_entregar(&b, &n);
    if (!d) return adc_fatia("", 0);
    char* c = adc_arena_copiar(pArena, (const char*)d, n);
    free(d);
    return c ? adc_fatia(c, n) : adc_fatia("", 0);
}

ADC_Fatia adc_inlines_texto_puro(ADC_Arena* pArena, const ADC_Inline* pInlines) { return texto_puro_com(pArena, pInlines, false); }
ADC_Fatia adc_inlines_texto_para_id(ADC_Arena* pArena, const ADC_Inline* pInlines) { return texto_puro_com(pArena, pInlines, true); }

ADC_Fatia adc_ctx_gerar_id(ADC_Contexto* pC, ADC_Fatia pTexto) {
    ADC_Fatia prefixo = adc_atribs_valor_ou(&pC->atribs, "idprefix", "_");
    ADC_Fatia sep = adc_atribs_valor_ou(&pC->atribs, "idseparator", "_");
    char sepc = sep.tamanho > 0 ? sep.dados[0] : 0;
    ADC_Buffer b; adc_buffer_iniciar(&b);
    adc_buffer_fatia(&b, prefixo);
    bool ultimo_sep = true;
    int32_t p = 0;
    while (p < pTexto.tamanho) {
        int32_t ini = p;
        uint32_t cp = adc_utf8_decodificar(pTexto.dados, pTexto.tamanho, &p);
        if (cp < 0x80) {
            char c = (char)cp;
            if (adc_eh_alnum(c)) { adc_buffer_char(&b, adc_minusculo(c)); ultimo_sep = false; }
            else if (c == '_' || c == '-' || c == '.') { if (!ultimo_sep && sepc) { adc_buffer_char(&b, c == '_' ? (sepc ? sepc : '_') : c); ultimo_sep = true; } }
            else if (c == ' ' || c == '\t' || c == '\n') { if (!ultimo_sep && sepc) { adc_buffer_char(&b, sepc); ultimo_sep = true; } }
            /* outros ASCII (pontuação) são descartados */
        } else if (adc_utf8_alfa_digito_em(pTexto.dados, pTexto.tamanho, ini)) {
            /* letras acentuadas entram em minúsculo quando Latin-1 (À-Þ → à-þ, salvo ×) */
            if (cp >= 0xC0 && cp <= 0xDE && cp != 0xD7) cp += 0x20;
            char tmp[4]; int32_t n = adc_utf8_codificar(cp, tmp); adc_buffer_bytes(&b, tmp, n);
            ultimo_sep = false;
        } else {
            /* pontuação/símbolo Unicode (aspas curvas, ©, travessão): descartado como no Asciidoctor */
        }
    }
    /* remove separador final */
    while (b.tamanho > prefixo.tamanho && sepc && b.dados[b.tamanho - 1] == (uint8_t)sepc) b.tamanho--;
    if (b.tamanho == prefixo.tamanho) adc_buffer_texto(&b, "id");
    int32_t base_tam = b.tamanho;
    /* unicidade */
    for (int32_t sufixo = 2; ; sufixo++) {
        ADC_Fatia cand = adc_buffer_fatia_de(&b);
        bool existe = adc_ctx_achar_id(pC, cand) != NULL;
        if (!existe) break;
        b.tamanho = base_tam;
        if (sepc) adc_buffer_char(&b, sepc);
        adc_buffer_int(&b, sufixo);
        if (sufixo > 10000) break;
    }
    int32_t n; uint8_t* d = adc_buffer_entregar(&b, &n);
    if (!d) return adc_fatia("_id", 3);
    char* c = adc_arena_copiar(pC->arena, (const char*)d, n);
    free(d);
    return c ? adc_fatia(c, n) : adc_fatia("_id", 3);
}

int32_t adc_ctx_registrar_nota(ADC_Contexto* pC, ADC_Fatia pId, ADC_Inline* pTexto) {
    ADC_NotaRodape* n = ADC_NOVO(pC->arena, ADC_NotaRodape);
    if (!n) return 0;
    n->numero = ++pC->quantidade_notas;
    n->id = pId; n->texto = pTexto;
    if (pC->ultima_nota) pC->ultima_nota->proxima = n; else pC->notas = n;
    pC->ultima_nota = n;
    return n->numero;
}

ADC_NotaRodape* adc_ctx_achar_nota(ADC_Contexto* pC, ADC_Fatia pId) {
    if (pId.tamanho == 0) return NULL;
    for (ADC_NotaRodape* n = pC->notas; n; n = n->proxima) if (adc_fatia_igual_f(n->id, pId)) return n;
    return NULL;
}

bool adc_ctx_tem_resolvedor(const ADC_Contexto* pC) { return pC->resolver != NULL || pC->resolver2 != NULL; }

int32_t adc_ctx_resolver_recurso(ADC_Contexto* pC, int32_t pTipo, ADC_Fatia pCaminho, const uint8_t** pConteudo, int32_t* pTamanho) {
    *pConteudo = NULL; *pTamanho = 0;
    if (pC->resolver2) {
        const char* resolvido = NULL; int32_t tam = 0;
        ADC_Fatia origem = adc_leitor_caminho(&pC->leitor, 0);
        return pC->resolver2(pC->contexto_resolver2, pTipo, pCaminho.dados, pCaminho.tamanho, origem.dados ? origem.dados : "", origem.tamanho, pConteudo, pTamanho, &resolvido, &tam);
    }
    if (pC->resolver) return pC->resolver(pC->contexto_resolver, pTipo, pCaminho.dados, pCaminho.tamanho, pConteudo, pTamanho);
    return -1;
}
