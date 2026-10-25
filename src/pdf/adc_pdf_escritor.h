/*
 * Escritor PDF próprio (IC-4): objetos, xref, fluxos comprimidos (Flate via miniz), páginas, anotações de
 * link, marcadores (outlines), destinos nomeados e metadados. PDF 1.7, xref clássico, sem criptografia.
 * Não conhece AsciiDoc: quem fala com ele é o motor de leiaute (render/adc_render_pdf.c).
 */
#ifndef ADC_PDF_ESCRITOR_H
#define ADC_PDF_ESCRITOR_H

#include "../adc_comum.h"
#include "adc_pdf_fonte.h"

typedef struct ADC_PdfLink {
    double      x, y, w, h;          /* retângulo (pt, origem no canto inferior esquerdo da página) */
    ADC_Fatia   uri;                 /* externo: URI                                              */
    ADC_Fatia   destino;             /* interno: nome do destino                                  */
    struct ADC_PdfLink* proximo;
} ADC_PdfLink;

typedef struct ADC_PdfDestino {
    ADC_Fatia   nome;
    int32_t     pagina;              /* índice 0-based                                            */
    double      y;                   /* topo do destino                                           */
    struct ADC_PdfDestino* proximo;
} ADC_PdfDestino;

typedef struct ADC_PdfMarcador {
    ADC_Fatia   titulo;              /* UTF-8                                                     */
    int32_t     nivel;               /* 1 = topo                                                  */
    int32_t     pagina;
    double      y;
    int32_t     objeto;              /* preenchido ao escrever                                    */
    struct ADC_PdfMarcador* proximo;
} ADC_PdfMarcador;

typedef struct ADC_PdfImagem {
    int32_t     objeto;              /* XObject                                                   */
    int32_t     objeto_smask;        /* 0 se não houver alpha                                     */
    int32_t     largura, altura;     /* pixels                                                    */
    ADC_Fatia   chave;               /* caminho da imagem (para reutilizar)                       */
    struct ADC_PdfImagem* proxima;
} ADC_PdfImagem;

typedef struct ADC_PdfPagina {
    ADC_Buffer    conteudo;          /* fluxo de conteúdo (operadores)                            */
    ADC_PdfLink*  links;
    ADC_PdfLink*  ultimo_link;
    double        largura, altura;
    bool          usa_fonte[ADC_PDF_MAX_FONTES];
    int32_t*      imagens_usadas;    /* índices em doc->imagens (array dinâmico)                   */
    int32_t       n_imagens, cap_imagens;
    int32_t       objeto;            /* reservado no fim                                          */
} ADC_PdfPagina;

typedef struct ADC_PdfDoc {
    ADC_Arena*       arena;
    ADC_Buffer       saida;
    int32_t*         offsets;        /* por número de objeto (1-based)                            */
    int32_t          n_objetos, cap_objetos;
    ADC_PdfPagina*   paginas;        /* array dinâmico                                            */
    int32_t          n_paginas, cap_paginas;
    ADC_PdfFonte*    fontes[ADC_PDF_MAX_FONTES];
    int32_t          n_fontes;
    ADC_PdfImagem*   imagens;
    int32_t          n_imagens;
    ADC_PdfDestino*  destinos;
    ADC_PdfMarcador* marcadores;
    ADC_PdfMarcador* ultimo_marcador;
    ADC_Fatia        titulo, autor, assunto, palavras_chave;
    bool             comprimir;
    bool             falhou;
    const char*      erro;
} ADC_PdfDoc;

void   adc_pdf_doc_iniciar(ADC_PdfDoc* pD, ADC_Arena* pArena);
void   adc_pdf_doc_liberar(ADC_PdfDoc* pD);

/* Registra uma fonte (já carregada); devolve o índice (recurso /F<índice>). -1 se cheio. */
int32_t adc_pdf_doc_fonte(ADC_PdfDoc* pD, ADC_PdfFonte* pFonte);

/* Nova página; devolve o índice. */
int32_t adc_pdf_doc_nova_pagina(ADC_PdfDoc* pD, double pLargura, double pAltura);
ADC_PdfPagina* adc_pdf_doc_pagina(ADC_PdfDoc* pD, int32_t pIndice);

/* Operadores de conteúdo na página. Cores em 0..1. */
void adc_pdf_pg_cor_preenchimento(ADC_PdfPagina* pP, double r, double g, double b);
void adc_pdf_pg_cor_traco(ADC_PdfPagina* pP, double r, double g, double b);
void adc_pdf_pg_largura_traco(ADC_PdfPagina* pP, double w);
void adc_pdf_pg_retangulo(ADC_PdfPagina* pP, double x, double y, double w, double h, bool pPreencher, bool pTracar);
void adc_pdf_pg_linha(ADC_PdfPagina* pP, double x1, double y1, double x2, double y2);
void adc_pdf_pg_salvar(ADC_PdfPagina* pP);
void adc_pdf_pg_restaurar(ADC_PdfPagina* pP);
void adc_pdf_pg_recorte(ADC_PdfPagina* pP, double x, double y, double w, double h);
/* Texto: posiciona e mostra uma run UTF-8 com a fonte pIndice e tamanho pTamanho (marca glifos usados). */
void adc_pdf_pg_texto(ADC_PdfDoc* pD, ADC_PdfPagina* pP, int32_t pIndiceFonte, double pTamanho, double x, double y, ADC_Fatia pTextoUTF8);
/* Imagem: desenha o XObject pIndice no retângulo. */
void adc_pdf_pg_imagem(ADC_PdfDoc* pD, ADC_PdfPagina* pP, int32_t pIndiceImagem, double x, double y, double w, double h);
/* Link (anotação) na página. */
void adc_pdf_pg_link_uri(ADC_PdfDoc* pD, ADC_PdfPagina* pP, double x, double y, double w, double h, ADC_Fatia pURI);
void adc_pdf_pg_link_destino(ADC_PdfDoc* pD, ADC_PdfPagina* pP, double x, double y, double w, double h, ADC_Fatia pDestino);

/* Destinos nomeados e marcadores. */
void adc_pdf_doc_destino(ADC_PdfDoc* pD, ADC_Fatia pNome, int32_t pPagina, double y);
void adc_pdf_doc_marcador(ADC_PdfDoc* pD, ADC_Fatia pTitulo, int32_t pNivel, int32_t pPagina, double y);

/* Imagens: registra os bytes decodificados (ver adc_pdf_imagem.h); devolve o índice ou -1. */
int32_t adc_pdf_doc_imagem(ADC_PdfDoc* pD, ADC_Fatia pChave, int32_t pLargura, int32_t pAltura, int32_t pComponentes,
                           const uint8_t* pPixels, int32_t pTamanhoPixels, const uint8_t* pAlpha, int32_t pTamanhoAlpha,
                           bool pJaDCT, bool pCMYKInvertido);
int32_t adc_pdf_doc_imagem_por_chave(ADC_PdfDoc* pD, ADC_Fatia pChave, int32_t* pLargura, int32_t* pAltura);

/* Escreve o PDF inteiro em pSaida. false em falha (pD->erro explica). */
bool adc_pdf_doc_finalizar(ADC_PdfDoc* pD, ADC_Buffer* pSaida);

/* Utilidades. */
void adc_pdf_escrever_numero(ADC_Buffer* pB, double v);     /* "12.34" sem zeros à direita, 3 casas */
void adc_pdf_escrever_string_literal(ADC_Buffer* pB, ADC_Fatia pTexto); /* (…) com escapes, UTF-16BE com BOM quando não ASCII */

#endif
