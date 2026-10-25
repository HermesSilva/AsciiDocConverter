/*
 * Atributos do documento: tabela nome → valor, com os intrínsecos (nbsp, empty, ...), os travados pela
 * API (opções "atributos" têm precedência sobre o documento, como -a na linha de comando) e a resolução
 * de referências {nome} — o "pré-processador inline em modo atributos" da SDR-2/SDR-5.
 */
#ifndef ADC_ATRIBUTOS_H
#define ADC_ATRIBUTOS_H

#include "adc_comum.h"
#include "adc_mensagens.h"

typedef struct ADC_EntradaAtributo {
    ADC_Fatia                     nome;
    ADC_Fatia                     valor;
    bool                          travado;     /* definido pela API: o documento não altera           */
    bool                          removido;    /* :nome!: — existe na tabela como "não definido"       */
    bool                          do_documento;  /* definido por entrada ":nome:" do documento      */
    struct ADC_EntradaAtributo*   proxima;       /* encadeamento do balde                           */
    struct ADC_EntradaAtributo*   proxima_ordem; /* encadeamento na ordem de criação                */
} ADC_EntradaAtributo;

#define ADC_ATRIB_BALDES 128

typedef struct ADC_TabelaAtributos {
    ADC_Arena*            arena;
    ADC_EntradaAtributo*  baldes[ADC_ATRIB_BALDES];
    ADC_EntradaAtributo*  ordem;        /* lista em ordem de definição (para o ASG)                 */
    ADC_EntradaAtributo*  ultima;
    int32_t               quantidade;
    int32_t               contador_nota;
} ADC_TabelaAtributos;

void      adc_atribs_iniciar(ADC_TabelaAtributos* pT, ADC_Arena* pArena);
/* Define (ou redefine). Devolve false se o nome está travado pela API. Nome em minúsculas. */
bool      adc_atribs_definir(ADC_TabelaAtributos* pT, ADC_Fatia pNome, ADC_Fatia pValor, bool pTravar);
bool      adc_atribs_remover(ADC_TabelaAtributos* pT, ADC_Fatia pNome);
/* Marca que o atributo veio de uma entrada do documento (sai no ASG). */
void      adc_atribs_marcar_documento(ADC_TabelaAtributos* pT, ADC_Fatia pNome);
/* Devolve true se definido (mesmo com valor vazio). */
bool      adc_atribs_obter(const ADC_TabelaAtributos* pT, ADC_Fatia pNome, ADC_Fatia* pValor);
bool      adc_atribs_obter_cstr(const ADC_TabelaAtributos* pT, const char* pNome, ADC_Fatia* pValor);
bool      adc_atribs_definido(const ADC_TabelaAtributos* pT, const char* pNome);
ADC_Fatia adc_atribs_valor_ou(const ADC_TabelaAtributos* pT, const char* pNome, const char* pPadrao);
/* Valor dos intrínsecos de caractere ({nbsp}, {amp}, ...): true se pNome é um deles. */
bool      adc_atribs_intrinseco(ADC_Fatia pNome, ADC_Fatia* pValor);
/* Nome válido de atributo: [a-zA-Z0-9_][-a-zA-Z0-9_]* */
bool      adc_atribs_nome_valido(ADC_Fatia pNome);
/* Normaliza para minúsculas na arena (nomes de atributo não diferenciam caixa). */
ADC_Fatia adc_atribs_normalizar_nome(ADC_Arena* pArena, ADC_Fatia pNome);

/*
 * Resolve referências {nome} na fatia. Devolve a fatia resultante (a mesma entrada quando nada mudou;
 * senão, memória da arena). Escapes: "\{nome}" vira "{nome}". Referência a atributo indefinido fica
 * como está (attribute-missing=skip, padrão do Asciidoctor) e gera aviso, salvo se pAvisar=false.
 * Contadores {counter:nome} e {counter2:nome} e {set:nome:valor} são tratados.
 *
 * pMapa (opcional): array com pResultado.tamanho+1 inteiros, recebendo para cada byte da saída o
 * deslocamento do byte de origem correspondente na entrada (para rastrear localização — SDR-5).
 */
ADC_Fatia adc_atribs_resolver(ADC_TabelaAtributos* pT, ADC_Fatia pTexto, ADC_Mensagens* pMsgs,
                              int32_t pLinha, bool pAvisar, int32_t** pMapa);

#endif
