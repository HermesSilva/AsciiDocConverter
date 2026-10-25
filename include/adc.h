/*
 * AsciiDocConverter — contrato C (ABI) da biblioteca nativa.
 *
 * Uma chamada, uma conversão (IC-1): entra um documento AsciiDoc em UTF-8, entra o formato desejado,
 * sai um array de bytes naquele formato. Nenhum renderizador passa por outro formato.
 *
 * Memória: tudo o que a biblioteca devolve por ponteiro duplo (pSaida, pMensagens) é alocado por ela e
 * deve ser liberado com ADC_Liberar. Nada do que o chamador passa é retido após o retorno.
 *
 * Thread-safety: não há estado global; chamadas concorrentes são independentes.
 */
#ifndef ADC_H
#define ADC_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_WIN32)
#  if defined(ADC_EXPORTS)
#    define ADC_API __declspec(dllexport)
#  elif defined(ADC_ESTATICO)
#    define ADC_API
#  else
#    define ADC_API __declspec(dllimport)
#  endif
#else
#  if defined(ADC_ESTATICO)
#    define ADC_API
#  else
#    define ADC_API __attribute__((visibility("default")))
#  endif
#endif

/* O formato de saída é parâmetro da MESMA função; a entrada é sempre AsciiDoc. */
typedef enum ADC_Formato {
    ADC_HTML5    = 1,
    ADC_MARKDOWN = 2,
    ADC_PDF      = 3,
    ADC_ASG_JSON = 4,
    ADC_DOCBOOK5 = 5
} ADC_Formato;

/* Códigos devolvidos por ADC_Converter. Em todos os casos diferentes de ADC_OK, pMensagens traz o
 * motivo em texto (pt-BR), com linha e coluna quando houver. */
typedef enum ADC_Resultado {
    ADC_OK                   = 0,
    ADC_ERRO_PARAMETRO       = 1,  /* ponteiro nulo, tamanho negativo, formato fora do enum              */
    ADC_ERRO_ENTRADA         = 2,  /* não é UTF-8 válido, ou traz BOM                                    */
    ADC_ERRO_FORMATO         = 3,  /* formato conhecido mas não implementado nesta versão                */
    ADC_ERRO_LIMITE          = 4,  /* limite explícito excedido (tamanho, aninhamento, includes)         */
    ADC_ERRO_OPCOES          = 5,  /* pOpcoesJSON não é JSON válido ou traz valor fora do domínio        */
    ADC_ERRO_CONVERSAO       = 6,  /* o renderizador não conseguiu produzir a saída (detalhe nas msgs)   */
    ADC_ERRO_MEMORIA         = 7
} ADC_Resultado;

/*
 * Resolve um recurso pedido pelo documento: o alvo de um include:: ou de uma imagem (image::/image:)
 * quando o renderizador precisa dos bytes (PDF). A biblioteca NUNCA lê disco por conta própria (IC-3).
 *
 * Devolve 0 e preenche *pConteudo e *pTamanho (memória do chamador, válida até ADC_Converter retornar),
 * ou outro valor para negar — a negação vira aviso no documento, nunca silêncio. Sem callback
 * registrado, todo pedido é negado (modo seguro).
 *
 * pTipo: 1 = include (texto AsciiDoc, UTF-8), 2 = imagem (bytes brutos).
 */
typedef int32_t (*ADC_ResolverRecurso)(void* pContexto, int32_t pTipo,
                                       const char* pCaminho, int32_t pTamanhoCaminho,
                                       const uint8_t** pConteudo, int32_t* pTamanho);

/*
 * Converte um documento AsciiDoc (UTF-8, sem BOM) para o formato pedido.
 *
 * pOpcoesJSON (opcional, pode ser NULL): objeto JSON com
 *   "atributos":  { "nome": "valor", ... }   atributos do documento (como -a da linha de comando)
 *   "autonomo":   true|false                 HTML5: documento completo (true) ou só o corpo (false, padrão)
 *   "markdown":   { "htmlEmbutido": true }   política de degradação (DAD-3)
 *   "pdf":        { ...tema... }             sobrescrita do tema padrão (DAD-8)
 *   "limites":    { "tamanhoMaximo": n, "aninhamentoMaximo": n, "includesMaximo": n }   (includes: total, padrão 10000)
 *
 * pSaida/pTamanhoSaida: a saída (texto UTF-8 sem terminador para HTML/Markdown/ASG; bytes para PDF).
 * pMensagens: JSON — array de { "nivel": "aviso"|"erro", "linha": n, "coluna": n, "texto": "..." },
 *             sempre preenchido (pode ser "[]"), também no sucesso. Aviso do processador não se perde.
 */
ADC_API int32_t ADC_Converter(const uint8_t* pEntradaUTF8, int32_t pTamanho, int32_t pFormato,
                              const char* pOpcoesJSON,
                              ADC_ResolverRecurso pResolver, void* pContextoResolver,
                              uint8_t** pSaida, int32_t* pTamanhoSaida, char** pMensagens);

/* Libera memória devolvida por ADC_Converter (pSaida ou pMensagens). Aceita NULL. */
ADC_API void ADC_Liberar(void* pMemoria);

/* Texto fixo de versão, p.ex. "AsciiDocConverter 0.1.0". Não liberar. */
ADC_API const char* ADC_Versao(void);

#ifdef __cplusplus
}
#endif

#endif /* ADC_H */
