/*
 * Entrada WebAssembly da AsciiDocConverter (DAD-13, 2026-10-31). Compilada SÓ por build-wasm.sh — fica fora de
 * src/, que os builds nativos compilam por curinga, para a .dll/.so não mudarem de comportamento (DAD-9).
 *
 * Contrato com o hospedeiro JavaScript (a extensão Tootega AsciiDoc do VS Code — src/export/abi.ts):
 *   exportações: adc_wasm_alloc/adc_wasm_free, adc_wasm_converter, adc_wasm_saida, adc_wasm_saida_tamanho,
 *                adc_wasm_mensagens, adc_wasm_limpar, adc_wasm_versao, adc_wasm_resolvido;
 *   importação:  env.adc_wasm_resolver(tipo, caminho, tam, origem, tamOrigem) → 0 quando entregue; antes de devolver 0
 *                o hospedeiro chama adc_wasm_resolvido(conteudo, tam, caminhoResolvido, tam) com memória alocada por
 *                adc_wasm_alloc, válida até a conversão terminar (quem a libera é o hospedeiro).
 *
 * Por aqui entram o que a ABI nativa não tem: resolvedor com origem (includes aninhados), mensagens com "arquivo",
 * DocBook 5 e as opções dataUri/folhaDeEstilo/idioma/pdf.dataDeCriacao.
 */
#include "../include/adc.h"
#include "../src/adc_interno.h"
#include <stdlib.h>
#include <string.h>

#define ADC_WASM_EXPORT(nome) __attribute__((export_name(#nome), visibility("default")))

__attribute__((import_module("env"), import_name("adc_wasm_resolver")))
extern int32_t adc_wasm_resolver(int32_t pTipo, const char* pCaminho, int32_t pTamanhoCaminho, const char* pOrigem, int32_t pTamanhoOrigem);

static uint8_t* g_saida = NULL;
static int32_t  g_tamanho = 0;
static char*    g_mensagens = NULL;

static const uint8_t* g_res_conteudo = NULL;
static int32_t        g_res_tamanho = 0;
static const char*    g_res_caminho = NULL;
static int32_t        g_res_tamanho_caminho = 0;

ADC_WASM_EXPORT(adc_wasm_alloc) void* adc_wasm_alloc(int32_t pTamanho) { return malloc(pTamanho > 0 ? (size_t)pTamanho : 1u); }
ADC_WASM_EXPORT(adc_wasm_free) void adc_wasm_free(void* pMemoria) { free(pMemoria); }

ADC_WASM_EXPORT(adc_wasm_resolvido) void adc_wasm_resolvido(const uint8_t* pConteudo, int32_t pTamanho, const char* pCaminho, int32_t pTamanhoCaminho) {
    g_res_conteudo = pConteudo; g_res_tamanho = pTamanho;
    g_res_caminho = pCaminho; g_res_tamanho_caminho = pTamanhoCaminho;
}

static int32_t resolver_wasm(void* pContexto, int32_t pTipo, const char* pCaminho, int32_t pTamanhoCaminho,
                             const char* pOrigem, int32_t pTamanhoOrigem,
                             const uint8_t** pConteudo, int32_t* pTamanho,
                             const char** pCaminhoResolvido, int32_t* pTamanhoResolvido) {
    (void)pContexto;
    g_res_conteudo = NULL; g_res_tamanho = 0; g_res_caminho = NULL; g_res_tamanho_caminho = 0;
    int32_t rc = adc_wasm_resolver(pTipo, pCaminho, pTamanhoCaminho, pOrigem, pTamanhoOrigem);
    if (rc != 0) return rc;
    if (!g_res_conteudo) return 1;
    *pConteudo = g_res_conteudo; *pTamanho = g_res_tamanho;
    if (pCaminhoResolvido) *pCaminhoResolvido = g_res_caminho;
    if (pTamanhoResolvido) *pTamanhoResolvido = g_res_tamanho_caminho;
    return 0;
}

ADC_WASM_EXPORT(adc_wasm_limpar) void adc_wasm_limpar(void) {
    ADC_Liberar(g_saida); ADC_Liberar(g_mensagens);
    g_saida = NULL; g_tamanho = 0; g_mensagens = NULL;
}

ADC_WASM_EXPORT(adc_wasm_converter) int32_t adc_wasm_converter(const uint8_t* pEntrada, int32_t pTamanho, int32_t pFormato, const char* pOpcoesJSON) {
    adc_wasm_limpar();
    ADC_Extras extras;
    memset(&extras, 0, sizeof extras);
    extras.resolver2 = resolver_wasm;
    extras.permitir_docbook = true;
    extras.com_arquivo = true;
    return adc_converter_interno(pEntrada, pTamanho, pFormato, pOpcoesJSON && pOpcoesJSON[0] ? pOpcoesJSON : NULL, NULL, NULL, &extras, &g_saida, &g_tamanho, &g_mensagens);
}

ADC_WASM_EXPORT(adc_wasm_saida) const uint8_t* adc_wasm_saida(void) { return g_saida; }
ADC_WASM_EXPORT(adc_wasm_saida_tamanho) int32_t adc_wasm_saida_tamanho(void) { return g_tamanho; }
ADC_WASM_EXPORT(adc_wasm_mensagens) const char* adc_wasm_mensagens(void) { return g_mensagens ? g_mensagens : "[]"; }
ADC_WASM_EXPORT(adc_wasm_versao) const char* adc_wasm_versao(void) { return ADC_Versao(); }
