/*
 * AST do documento — alinhada ao ASG da especificação AsciiDoc Language (asg/schema.json, draft-01).
 * Tudo vive na arena do documento; fatias apontam para a entrada ou para a arena.
 *
 * O ASG conhece: document, section, heading (discrete), list/listItem, dlist/dlistItem, break,
 * blockMacro (image/toc/audio/video), leaf (listing, literal, paragraph, pass, stem, verse),
 * parent (admonition, example, sidebar, open, quote); inlines: span (strong/emphasis/code/mark),
 * ref (link/xref), text/charref/raw. O que a casa precisa além disso (tabela, nota de rodapé,
 * imagem inline, teclas, quebra de linha, sup/sub) existe aqui e é emitido no ASG do modo mais
 * próximo possível, com a divergência registrada em docs/fidelidade.md.
 */
#ifndef ADC_AST_H
#define ADC_AST_H

#include "adc_comum.h"

typedef struct ADC_Posicao { int32_t linha; int32_t coluna; } ADC_Posicao;
typedef struct ADC_Local { ADC_Posicao inicio; ADC_Posicao fim; int32_t arquivo; } ADC_Local;

/* ------------------------------------------------------------- atributos ---- */

/* Par nome/valor de attrlist ou de macro. Posicional usa nome "$1", "$2", ... */
typedef struct ADC_Atributo {
    ADC_Fatia             nome;
    ADC_Fatia             valor;
    struct ADC_Atributo*  proximo;
} ADC_Atributo;

typedef struct ADC_ListaFatias {
    ADC_Fatia*  itens;
    int32_t     quantidade;
    int32_t     capacidade;
} ADC_ListaFatias;

/* Metadados de bloco (SDR-2): o mapa de atributos, papéis e opções acumulados. */
typedef struct ADC_Metadados {
    ADC_Atributo*    atributos;     /* lista; último vence, salvo role/opts (acumulam)             */
    ADC_ListaFatias  papeis;        /* roles                                                        */
    ADC_ListaFatias  opcoes;        /* options                                                      */
    ADC_Fatia        id;
    ADC_Fatia        estilo;        /* "style": o 1º posicional sem atalhos, ou style=              */
    ADC_Fatia        titulo_cru;    /* .Título (linha) ou title=                                    */
    ADC_Fatia        reftext_cru;
    bool             titulo_citado; /* title='...' (parse inline) vs. title="..." (texto puro)        */
    ADC_Local        local;         /* da 1ª linha de atributo até a última antes do bloco           */
    bool             tem_local;
    bool             vazio;
} ADC_Metadados;

/* --------------------------------------------------------------- inlines ---- */

typedef enum ADC_TipoInline {
    ADC_IN_TEXTO = 1,       /* valor = texto                                                    */
    ADC_IN_CHARREF,         /* valor = entidade sem & e ; (ex.: "amp", "#8212", "#x2014")        */
    ADC_IN_RAW,             /* valor = texto que passa cru (passthrough)                        */
    ADC_IN_SPAN,            /* variante_span; filhos                                            */
    ADC_IN_REF,             /* variante_ref; alvo; filhos (texto do link)                       */
    ADC_IN_IMAGEM,          /* alvo; atributos (alt, width, height, title, link)                 */
    ADC_IN_NOTA,            /* nota de rodapé: numero; id (opcional); filhos (texto)            */
    ADC_IN_QUEBRA_LINHA,    /* " +" no fim da linha                                              */
    ADC_IN_ANCORA,          /* [[id]] / [#id] inline: id; reftext em filhos                      */
    ADC_IN_TECLA,           /* kbd:[a+b]: filhos = cada tecla como ADC_IN_TEXTO                  */
    ADC_IN_BOTAO,           /* btn:[Rótulo]: filhos                                              */
    ADC_IN_MENU,            /* menu:Arquivo[Salvar > Como]: filhos = cada item como TEXTO        */
    ADC_IN_ICONE,           /* icon:nome[]: alvo = nome                                          */
    ADC_IN_INDICE           /* ((termo)) / indexterm: visível = filhos                           */
} ADC_TipoInline;

typedef enum ADC_VarianteSpan {
    ADC_SPAN_STRONG = 1, ADC_SPAN_EMPHASIS, ADC_SPAN_CODE, ADC_SPAN_MARK,
    ADC_SPAN_SUPERSCRIPT, ADC_SPAN_SUBSCRIPT
} ADC_VarianteSpan;

typedef enum ADC_VarianteRef { ADC_REF_LINK = 1, ADC_REF_XREF } ADC_VarianteRef;

typedef struct ADC_Inline {
    ADC_TipoInline       tipo;
    ADC_VarianteSpan     variante_span;
    ADC_VarianteRef      variante_ref;
    bool                 unconstrained;    /* form                                                 */
    ADC_Fatia            valor;
    ADC_Fatia            alvo;
    ADC_Fatia            id;
    ADC_ListaFatias      papeis;
    ADC_Atributo*        atributos;        /* attrlist do macro                                    */
    int32_t              numero;           /* nota de rodapé                                       */
    bool                 alvo_externo;     /* link: abre em outra janela (^)                       */
    struct ADC_Inline*   filhos;
    struct ADC_Inline*   proximo;
    ADC_Local            local;
    int32_t              desl_inicio;      /* deslocamentos no texto pré-processado (uso interno do  */
    int32_t              desl_fim;         /* parser inline, até fixar 'local'); fim é exclusivo     */
} ADC_Inline;

/* ---------------------------------------------------------------- blocos ---- */

typedef enum ADC_TipoBloco {
    ADC_BL_DOCUMENTO = 1,
    ADC_BL_SECAO,
    ADC_BL_TITULO,          /* heading discreto                                                   */
    ADC_BL_PARAGRAFO,
    ADC_BL_LISTA,           /* variante_lista; filhos = itens                                     */
    ADC_BL_ITEM_LISTA,      /* principal; filhos = blocos anexados                                */
    ADC_BL_DLIST,           /* filhos = itens                                                     */
    ADC_BL_ITEM_DLIST,      /* termos[]; principal (descrição); filhos                            */
    ADC_BL_LISTING,
    ADC_BL_LITERAL,
    ADC_BL_PASS,
    ADC_BL_STEM,
    ADC_BL_VERSE,
    ADC_BL_ADMONICAO,       /* variante_adm; filhos                                               */
    ADC_BL_EXEMPLO,
    ADC_BL_SIDEBAR,
    ADC_BL_OPEN,
    ADC_BL_QUOTE,
    ADC_BL_TABELA,          /* tabela                                                             */
    ADC_BL_CELULA,          /* filho de linha de tabela: inlines ou filhos (célula AsciiDoc)       */
    ADC_BL_QUEBRA,          /* variante_quebra                                                    */
    ADC_BL_MACRO,           /* nome_macro: image/toc/video/audio; alvo                            */
    ADC_BL_COMENTARIO       /* nunca sai no ASG; mantido só para conservar nada (descartado)       */
} ADC_TipoBloco;

typedef enum ADC_Forma { ADC_FORMA_NENHUMA = 0, ADC_FORMA_PARAGRAFO, ADC_FORMA_DELIMITADA, ADC_FORMA_INDENTADA, ADC_FORMA_MACRO } ADC_Forma;
typedef enum ADC_VarianteLista { ADC_LISTA_UNORDERED = 1, ADC_LISTA_ORDERED, ADC_LISTA_CALLOUT } ADC_VarianteLista;
typedef enum ADC_VarianteAdm { ADC_ADM_NOTE = 1, ADC_ADM_TIP, ADC_ADM_IMPORTANT, ADC_ADM_WARNING, ADC_ADM_CAUTION } ADC_VarianteAdm;
typedef enum ADC_VarianteQuebra { ADC_QUEBRA_PAGE = 1, ADC_QUEBRA_THEMATIC } ADC_VarianteQuebra;

typedef enum ADC_Alinhamento { ADC_ALINHA_PADRAO = 0, ADC_ALINHA_ESQUERDA, ADC_ALINHA_CENTRO, ADC_ALINHA_DIREITA, ADC_ALINHA_TOPO, ADC_ALINHA_MEIO, ADC_ALINHA_BASE } ADC_Alinhamento;

typedef struct ADC_Coluna {
    int32_t          largura;       /* peso relativo (cols="1,2") ou % (cols="30%,70%")             */
    bool             percentual;
    bool             automatica;    /* ~                                                             */
    ADC_Alinhamento  horizontal;
    ADC_Alinhamento  vertical;
    char             estilo;        /* 'd' padrão, 'a' asciidoc, 'e' emphasis, 'h' header, 'l' literal, 'm' mono, 's' strong */
} ADC_Coluna;

typedef struct ADC_LinhaTabela {
    struct ADC_Bloco*        celulas;    /* ADC_BL_CELULA encadeadas por 'proximo' */
    bool                     cabecalho;
    bool                     rodape;
    struct ADC_LinhaTabela*  proxima;
} ADC_LinhaTabela;

typedef struct ADC_Tabela {
    ADC_Coluna*       colunas;
    int32_t           quantidade_colunas;
    ADC_LinhaTabela*  linhas;
    int32_t           quantidade_linhas;
    char              separador;        /* '|' padrão, ',' CSV, ':' DSV, '!' alternativo            */
    bool              tem_cabecalho;
    bool              tem_rodape;
    bool              autowidth;
    int32_t           largura_percentual;  /* width=50% ; 0 = 100%                                   */
    ADC_Fatia         molduras;         /* frame: all/topbot/sides/none                              */
    ADC_Fatia         grade;            /* grid: all/rows/cols/none                                  */
} ADC_Tabela;

typedef struct ADC_Bloco {
    ADC_TipoBloco       tipo;
    /* :icons: no PONTO do documento em que o bloco nasceu (o Asciidoctor reaplica as entradas de atributo do corpo
       na ordem durante a conversão — playback_attributes): 0 = sem ícones, 1 = font, 2 = imagem */
    int8_t              icons_estado;
    ADC_Forma           forma;
    ADC_Fatia           delimitador;
    ADC_Fatia           marcador;       /* lista / item                                             */
    ADC_VarianteLista   variante_lista;
    ADC_VarianteAdm     variante_adm;
    ADC_VarianteQuebra  variante_quebra;
    ADC_Fatia           nome_macro;     /* image, toc, video, audio                                 */
    ADC_Fatia           alvo;           /* alvo do macro                                            */
    int32_t             nivel;          /* seção / heading (0 = título do documento)                */
    ADC_Metadados       meta;
    ADC_Inline*         titulo;         /* título de bloco / de seção (parseado)                    */
    ADC_Inline*         reftext;
    ADC_Inline*         inlines;        /* conteúdo de folha (parágrafo, listing como 1 texto, ...) */
    ADC_Inline*         principal;      /* item de lista                                            */
    ADC_Inline**        termos;         /* item de dlist                                            */
    int32_t             quantidade_termos;
    ADC_Fatia           texto_cru;      /* verbatim (listing/literal/pass/stem) e célula sem parse  */
    ADC_Fatia           linguagem;      /* [source,ruby]                                            */
    ADC_Fatia           atribuicao;     /* quote/verse: attribution                                 */
    ADC_Fatia           citacao;        /* quote/verse: citetitle                                   */
    int32_t             numero;         /* callout <n>; item ordenado: início                       */
    /* tabela */
    ADC_Tabela*         tabela;
    int32_t             colspan;        /* célula */
    int32_t             rowspan;
    ADC_Alinhamento     alinha_h;
    ADC_Alinhamento     alinha_v;
    char                estilo_celula;
    /* árvore */
    struct ADC_Bloco*   filhos;
    struct ADC_Bloco*   ultimo_filho;
    struct ADC_Bloco*   proximo;
    struct ADC_Bloco*   pai;
    int32_t             quantidade_filhos;
    ADC_Local           local;
    bool                id_gerado;      /* id sintetizado do título (não sai no ASG)                 */
} ADC_Bloco;

/* -------------------------------------------------------------- documento ---- */

typedef struct ADC_Autor {
    ADC_Fatia nome_completo;
    ADC_Fatia primeiro_nome;
    ADC_Fatia nome_do_meio;
    ADC_Fatia ultimo_nome;
    ADC_Fatia iniciais;
    ADC_Fatia email;
    struct ADC_Autor* proximo;
} ADC_Autor;

typedef struct ADC_NotaRodape {
    int32_t                numero;
    ADC_Fatia              id;
    ADC_Inline*            texto;
    struct ADC_NotaRodape* proxima;
} ADC_NotaRodape;

typedef struct ADC_Cabecalho {
    ADC_Inline*  titulo;
    ADC_Fatia    titulo_cru;
    ADC_Autor*   autores;
    ADC_Fatia    revisao_numero;
    ADC_Fatia    revisao_data;
    ADC_Fatia    revisao_observacao;
    ADC_Local    local;
    bool         presente;
} ADC_Cabecalho;

typedef struct ADC_Arquivo { ADC_Fatia caminho; struct ADC_Arquivo* proximo; int32_t indice; } ADC_Arquivo;

/* Helpers de construção. */
void adc_bloco_anexar(ADC_Bloco* pPai, ADC_Bloco* pFilho);
void adc_lista_fatias_adicionar(ADC_Arena* pArena, ADC_ListaFatias* pLista, ADC_Fatia pItem); /* sem duplicar */
bool adc_lista_fatias_contem(const ADC_ListaFatias* pLista, const char* pItem);
ADC_Fatia adc_atributo_obter(const ADC_Atributo* pLista, const char* pNome, bool* pEncontrado);
void adc_atributo_definir(ADC_Arena* pArena, ADC_Atributo** pLista, ADC_Fatia pNome, ADC_Fatia pValor);
ADC_Fatia adc_tipo_bloco_nome(ADC_TipoBloco pTipo);  /* nome ASG: "paragraph", "listing", ... */
ADC_Fatia adc_variante_adm_nome(ADC_VarianteAdm pV); /* "note", ... */
const char* adc_variante_adm_rotulo(ADC_VarianteAdm pV); /* "Nota", "Dica", ... (pt-BR) */

#endif
