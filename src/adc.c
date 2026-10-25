/*
 * ABI pública (include/adc.h): valida, parseia uma vez, renderiza no formato pedido (IC-1).
 */
#ifndef ADC_EXPORTS
#define ADC_EXPORTS
#endif
#include "../include/adc.h"
#include "adc_documento.h"
#include "adc_render.h"
#include "adc_json.h"

#define ADC_VERSAO_TEXTO "AsciiDocConverter 0.1.0"

static int32_t falhar(ADC_Arena* pArena, ADC_Mensagens* pMsgs, int32_t pCodigo, char** pMensagens) {
    if (pMensagens) {
        ADC_Buffer b; adc_buffer_iniciar(&b);
        if (pMsgs) adc_msgs_para_json(pMsgs, &b); else adc_buffer_texto(&b, "[]");
        int32_t n; *pMensagens = (char*)adc_buffer_entregar(&b, &n);
    }
    if (pArena) adc_arena_liberar(pArena);
    return pCodigo;
}

ADC_API int32_t ADC_Converter(const uint8_t* pEntradaUTF8, int32_t pTamanho, int32_t pFormato,
                              const char* pOpcoesJSON,
                              ADC_ResolverRecurso pResolver, void* pContextoResolver,
                              uint8_t** pSaida, int32_t* pTamanhoSaida, char** pMensagens) {
    if (pSaida) *pSaida = NULL;
    if (pTamanhoSaida) *pTamanhoSaida = 0;
    if (pMensagens) *pMensagens = NULL;

    ADC_Arena arena; adc_arena_iniciar(&arena, 64 * 1024);
    ADC_Mensagens msgs; adc_msgs_iniciar(&msgs, &arena);

    if (!pSaida || !pTamanhoSaida || (!pEntradaUTF8 && pTamanho > 0) || pTamanho < 0) {
        adc_msgs_adicionar(&msgs, ADC_NIVEL_ERRO, 0, 0, "ADC_Converter: parâmetro inválido (pSaida/pTamanhoSaida nulos, entrada nula com tamanho > 0, ou tamanho negativo).");
        return falhar(&arena, &msgs, ADC_ERRO_PARAMETRO, pMensagens);
    }
    if (pFormato < ADC_HTML5 || pFormato > ADC_DOCBOOK5) {
        adc_msgs_adicionar(&msgs, ADC_NIVEL_ERRO, 0, 0, "ADC_Converter: formato %d desconhecido. Válidos: 1=HTML5, 2=Markdown, 3=PDF, 4=ASG JSON, 5=DocBook 5.", (int)pFormato);
        return falhar(&arena, &msgs, ADC_ERRO_PARAMETRO, pMensagens);
    }
    if (pFormato == ADC_DOCBOOK5) {
        adc_msgs_adicionar(&msgs, ADC_NIVEL_ERRO, 0, 0, "ADC_Converter: o formato DocBook 5 ainda não está implementado nesta versão (%s).", ADC_VERSAO_TEXTO);
        return falhar(&arena, &msgs, ADC_ERRO_FORMATO, pMensagens);
    }
    if (pTamanho >= 3 && pEntradaUTF8[0] == 0xEF && pEntradaUTF8[1] == 0xBB && pEntradaUTF8[2] == 0xBF) {
        adc_msgs_adicionar(&msgs, ADC_NIVEL_ERRO, 1, 1, "A entrada começa com BOM (EF BB BF). O contrato é UTF-8 sem BOM: remova os 3 bytes iniciais antes de chamar.");
        return falhar(&arena, &msgs, ADC_ERRO_ENTRADA, pMensagens);
    }
    int32_t inval = adc_utf8_validar(pEntradaUTF8, pTamanho);
    if (inval >= 0) {
        int32_t linha = 1, col = 1;
        for (int32_t i = 0; i < inval; i++) { if (pEntradaUTF8[i] == '\n') { linha++; col = 1; } else col++; }
        adc_msgs_adicionar(&msgs, ADC_NIVEL_ERRO, linha, col, "A entrada não é UTF-8 válido: byte 0x%02X no deslocamento %d (linha %d, coluna %d). Reencode o documento em UTF-8.", pEntradaUTF8[inval], inval, linha, col);
        return falhar(&arena, &msgs, ADC_ERRO_ENTRADA, pMensagens);
    }

    ADC_Json* opcoes = NULL;
    if (pOpcoesJSON && pOpcoesJSON[0]) {
        const char* erro = NULL; int32_t pos = 0;
        opcoes = adc_json_ler(&arena, adc_fatia_cstr(pOpcoesJSON), &erro, &pos);
        if (!opcoes) {
            adc_msgs_adicionar(&msgs, ADC_NIVEL_ERRO, 0, pos + 1, "pOpcoesJSON inválido na posição %d: %s.", pos, erro ? erro : "JSON malformado");
            return falhar(&arena, &msgs, ADC_ERRO_OPCOES, pMensagens);
        }
    }

    ADC_Contexto* ctx = ADC_NOVO(&arena, ADC_Contexto);
    if (!ctx) return falhar(&arena, &msgs, ADC_ERRO_MEMORIA, pMensagens);
    const char* erro_opcoes = NULL;
    if (!adc_doc_iniciar(ctx, &arena, opcoes, pResolver, pContextoResolver, &erro_opcoes)) {
        adc_msgs_adicionar(&msgs, ADC_NIVEL_ERRO, 0, 0, "pOpcoesJSON: %s.", erro_opcoes ? erro_opcoes : "valor fora do domínio");
        return falhar(&arena, &msgs, ADC_ERRO_OPCOES, pMensagens);
    }
    if (pTamanho > ctx->opcoes.tamanho_maximo) {
        adc_msgs_adicionar(&ctx->msgs, ADC_NIVEL_ERRO, 0, 0, "A entrada tem %d bytes e o limite é %d (limites.tamanhoMaximo). Divida o documento ou eleve o limite.", pTamanho, ctx->opcoes.tamanho_maximo);
        return falhar(&arena, &ctx->msgs, ADC_ERRO_LIMITE, pMensagens);
    }

    adc_doc_parsear(ctx, adc_fatia((const char*)pEntradaUTF8, pTamanho), adc_fatia(NULL, 0));
    if (ctx->limite_estourado) return falhar(&arena, &ctx->msgs, ADC_ERRO_LIMITE, pMensagens);
    if (arena.sem_memoria) {
        adc_msgs_adicionar(&ctx->msgs, ADC_NIVEL_ERRO, 0, 0, "Sem memória durante o parse (arena com %zu bytes).", arena.total);
        return falhar(&arena, &ctx->msgs, ADC_ERRO_MEMORIA, pMensagens);
    }

    ADC_Buffer saida; adc_buffer_iniciar(&saida);
    bool ok = true;
    switch (pFormato) {
        case ADC_HTML5: adc_html5_escrever(ctx, &saida); break;
        case ADC_MARKDOWN: adc_markdown_escrever(ctx, &saida); break;
        case ADC_ASG_JSON: adc_asg_escrever_documento(ctx, &saida); break;
        case ADC_PDF: ok = adc_pdf_escrever(ctx, &saida); break;
        default: ok = false; break;
    }
    if (!ok) { adc_buffer_liberar(&saida); return falhar(&arena, &ctx->msgs, ADC_ERRO_CONVERSAO, pMensagens); }
    if (saida.sem_memoria) {
        adc_buffer_liberar(&saida);
        adc_msgs_adicionar(&ctx->msgs, ADC_NIVEL_ERRO, 0, 0, "Sem memória ao montar a saída.");
        return falhar(&arena, &ctx->msgs, ADC_ERRO_MEMORIA, pMensagens);
    }
    *pSaida = adc_buffer_entregar(&saida, pTamanhoSaida);
    if (!*pSaida) { adc_msgs_adicionar(&ctx->msgs, ADC_NIVEL_ERRO, 0, 0, "Sem memória ao entregar a saída."); return falhar(&arena, &ctx->msgs, ADC_ERRO_MEMORIA, pMensagens); }
    if (pMensagens) {
        ADC_Buffer b; adc_buffer_iniciar(&b);
        adc_msgs_para_json(&ctx->msgs, &b);
        int32_t n; *pMensagens = (char*)adc_buffer_entregar(&b, &n);
    }
    adc_arena_liberar(&arena);
    return ADC_OK;
}

ADC_API void ADC_Liberar(void* pMemoria) { free(pMemoria); }

ADC_API const char* ADC_Versao(void) { return ADC_VERSAO_TEXTO; }
