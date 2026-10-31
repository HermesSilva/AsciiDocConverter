/*
 * Contexto de uma conversão: tudo o que o parser e os renderizadores compartilham para UM documento.
 * Não há estado global (IC-3): cada chamada a ADC_Converter cria e destrói o seu.
 */
#ifndef ADC_CONTEXTO_H
#define ADC_CONTEXTO_H

#include "adc_comum.h"
#include "adc_ast.h"
#include "adc_atributos.h"
#include "adc_mensagens.h"
#include "adc_leitor.h"
#include "adc_json.h"
#include "../include/adc.h"

typedef struct ADC_Opcoes {
    bool             autonomo;             /* HTML5 completo                                         */
    bool             md_html_embutido;     /* Markdown: degrada para HTML embutido onde o GFM permite */
    int32_t          tamanho_maximo;       /* bytes de entrada                                        */
    int32_t          aninhamento_maximo;   /* profundidade de blocos                                  */
    int32_t          includes_maximo;
    const ADC_Json*  pdf_tema;             /* objeto "pdf" das opções (sobrescrita do tema)           */
    const ADC_Json*  raiz;                 /* o JSON inteiro                                          */
    /* 2026-10-31 (DAD-10..12): opções novas, todas opcionais — sem elas o comportamento é o de sempre */
    ADC_Idioma       idioma;               /* "idioma": en (padrão) | pt-BR — idioma das mensagens    */
    bool             data_uri;             /* "dataUri": HTML5 embute as imagens em base64 (tipo 2)   */
    ADC_Fatia        folha_de_estilo;      /* "folhaDeEstilo": CSS pedido ao resolvedor (tipo 3)      */
    bool             pdf_data_definida;    /* "pdf.dataDeCriacao" presente ("" = PDF sem data)        */
    ADC_Fatia        pdf_data_criacao;     /* texto já no formato PDF: D:AAAAMMDDHHmmSSZ              */
} ADC_Opcoes;

/* Registro de ids para xref: id → texto de referência (título da seção, reftext ou título do bloco). */
typedef struct ADC_RegistroId {
    ADC_Fatia               id;
    ADC_Inline*             texto;
    ADC_Bloco*              bloco;
    struct ADC_RegistroId*  proximo;
} ADC_RegistroId;

typedef struct ADC_Contexto {
    ADC_Arena*            arena;
    ADC_TabelaAtributos   atribs;
    ADC_Mensagens         msgs;
    ADC_Leitor            leitor;
    ADC_Opcoes            opcoes;
    ADC_Bloco*            documento;
    ADC_Cabecalho         cabecalho;
    ADC_NotaRodape*       notas;
    ADC_NotaRodape*       ultima_nota;
    int32_t               quantidade_notas;
    ADC_RegistroId*       ids;
    int32_t               profundidade;         /* aninhamento corrente de blocos                 */
    int32_t               secoes_vistas;        /* para o preâmbulo e para numeração              */
    ADC_ResolverRecurso   resolver;
    void*                 contexto_resolver;
    ADC_ResolverRecurso2  resolver2;            /* v2 (entrada WebAssembly); tem precedência       */
    void*                 contexto_resolver2;
    bool                  limite_estourado;
    bool                  hardbreaks;           /* opção %hardbreaks do bloco corrente             */
    int32_t               contador_callout;     /* numeração <.> automática                        */
    int32_t               contador_imagem;      /* "Figura N." quando figure-caption               */
    int32_t               contador_tabela;
    int32_t               contador_exemplo;
    int32_t               contador_listing;
    bool                  modo_tck_inline;      /* adapter do TCK: só parsear inlines              */
    ADC_Metadados         meta_pendente;        /* linhas de atributo/título lidas e ainda sem bloco */
    int32_t               profundidade_blocos;  /* chamadas aninhadas de adc_blocos_parsear          */
    /* O sumário é fixado ao fechar o cabeçalho (Asciidoctor, finalize_header): :toc: definido só no corpo não
       liga o sumário automático nem o toc::[]. */
    bool                  toc_ativo;
    ADC_Fatia             toc_valor;
    int8_t                icons_cabecalho;      /* :icons: no fim do cabeçalho: 0 nenhum, 1 font, 2 imagem */
    ADC_Fatia             toc_titulo;           /* toc-title e toclevels no fim do cabeçalho (o sumário usa o */
    ADC_Fatia             toc_niveis;           /* estado do cabeçalho; :toc-title: no corpo não o muda)       */
    /* linhas vazias puladas antes do bloco corrente (o "skipped" do next_block do Asciidoctor): dentro de item de
       lista, parágrafo com skipped == 0 é interrompido por qualquer linha de lista */
    int32_t               vazias_antes_bloco;
} ADC_Contexto;

/* ids */
void      adc_ctx_registrar_id(ADC_Contexto* pC, ADC_Fatia pId, ADC_Inline* pTexto, ADC_Bloco* pBloco);
ADC_RegistroId* adc_ctx_achar_id(ADC_Contexto* pC, ADC_Fatia pId);
/* Gera id único a partir do texto de um título: "_titulo_da_secao" (idprefix/idseparator). */
ADC_Fatia adc_ctx_gerar_id(ADC_Contexto* pC, ADC_Fatia pTextoPuro);
/* Texto puro (sem marcação) de uma lista de inlines, na arena. */
ADC_Fatia adc_inlines_texto_puro(ADC_Arena* pArena, const ADC_Inline* pInlines);
/* Texto base para gerar id de seção: como o Asciidoctor, descarta referências de caractere e trechos crus. */
ADC_Fatia adc_inlines_texto_para_id(ADC_Arena* pArena, const ADC_Inline* pInlines);
/* Nota de rodapé: registra e devolve o número. pId pode ser vazia. */
int32_t   adc_ctx_registrar_nota(ADC_Contexto* pC, ADC_Fatia pId, ADC_Inline* pTexto);
ADC_NotaRodape* adc_ctx_achar_nota(ADC_Contexto* pC, ADC_Fatia pId);
/* Há resolvedor de recursos registrado (v1 ou v2)? */
bool      adc_ctx_tem_resolvedor(const ADC_Contexto* pC);
/* Pede um recurso (1 include, 2 imagem, 3 folha de estilo) ao resolvedor registrado — v2 com a origem no documento
 * principal, senão v1. Devolve 0 e preenche quando entregue; outro valor quando negado ou sem resolvedor (-1). */
int32_t   adc_ctx_resolver_recurso(ADC_Contexto* pC, int32_t pTipo, ADC_Fatia pCaminho, const uint8_t** pConteudo, int32_t* pTamanho);

#endif
