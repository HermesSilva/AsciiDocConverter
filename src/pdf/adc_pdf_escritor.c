#include "adc_pdf_escritor.h"
#include "../../terceiros/miniz.h"
#include <time.h>

/* ------------------------------------------------------------ utilidades ---- */

void adc_pdf_escrever_numero(ADC_Buffer* pB, double v) {
    if (v != v) v = 0;                 /* NaN */
    if (v > 1e9) v = 1e9;
    if (v < -1e9) v = -1e9;
    if (v == (double)(int64_t)v) { adc_buffer_int(pB, (int64_t)v); return; }
    adc_buffer_decimal(pB, v, 3);
}

void adc_pdf_escrever_string_literal(ADC_Buffer* pB, ADC_Fatia pT) {
    bool ascii = true;
    for (int32_t i = 0; i < pT.tamanho; i++) if ((unsigned char)pT.dados[i] >= 0x80) { ascii = false; break; }
    if (ascii) {
        adc_buffer_char(pB, '(');
        for (int32_t i = 0; i < pT.tamanho; i++) {
            char c = pT.dados[i];
            if (c == '(' || c == ')' || c == '\\') { adc_buffer_char(pB, '\\'); adc_buffer_char(pB, c); }
            else if (c == '\n') adc_buffer_texto(pB, "\\n");
            else if (c == '\r') adc_buffer_texto(pB, "\\r");
            else if (c == '\t') adc_buffer_texto(pB, "\\t");
            else if ((unsigned char)c < 0x20) { char t[8]; snprintf(t, sizeof t, "\\%03o", (unsigned char)c); adc_buffer_texto(pB, t); }
            else adc_buffer_char(pB, c);
        }
        adc_buffer_char(pB, ')');
        return;
    }
    /* UTF-16BE com BOM em hex */
    adc_buffer_texto(pB, "<FEFF");
    int32_t p = 0;
    while (p < pT.tamanho) {
        uint32_t cp = adc_utf8_decodificar(pT.dados, pT.tamanho, &p);
        char t[12];
        if (cp < 0x10000) snprintf(t, sizeof t, "%04X", cp);
        else { uint32_t v = cp - 0x10000; snprintf(t, sizeof t, "%04X%04X", 0xD800 + (v >> 10), 0xDC00 + (v & 0x3FF)); }
        adc_buffer_texto(pB, t);
    }
    adc_buffer_char(pB, '>');
}

/* --------------------------------------------------------------- objetos ---- */

static int32_t reservar_objeto(ADC_PdfDoc* pD) {
    if (pD->n_objetos + 1 >= pD->cap_objetos) {
        int32_t nova = pD->cap_objetos ? pD->cap_objetos * 2 : 64;
        int32_t* no = (int32_t*)realloc(pD->offsets, sizeof(int32_t) * (size_t)nova);
        if (!no) { pD->falhou = true; pD->erro = "sem memória (objetos)"; return 0; }
        for (int32_t i = pD->cap_objetos; i < nova; i++) no[i] = 0;
        pD->offsets = no; pD->cap_objetos = nova;
    }
    return ++pD->n_objetos;
}

static void iniciar_objeto(ADC_PdfDoc* pD, int32_t pNum) {
    if (pNum <= 0 || pNum >= pD->cap_objetos) return;
    pD->offsets[pNum] = pD->saida.tamanho;
    adc_buffer_int(&pD->saida, pNum); adc_buffer_texto(&pD->saida, " 0 obj\n");
}
static void fim_objeto(ADC_PdfDoc* pD) { adc_buffer_texto(&pD->saida, "\nendobj\n"); }

/* Escreve um objeto stream com o dicionário extra (sem << >>) e os dados, comprimindo se pedido. */
static int32_t escrever_stream(ADC_PdfDoc* pD, int32_t pNum, const char* pDictExtra, const uint8_t* pDados, int32_t pTamanho, bool pComprimir) {
    if (!pNum) pNum = reservar_objeto(pD);
    if (!pNum) return 0;
    uint8_t* comp = NULL; mz_ulong comp_tam = 0;
    if (pComprimir && pTamanho > 0) {
        comp_tam = mz_compressBound((mz_ulong)pTamanho);
        comp = (uint8_t*)malloc(comp_tam);
        if (comp && mz_compress2(comp, &comp_tam, pDados, (mz_ulong)pTamanho, MZ_BEST_SPEED + 3) != MZ_OK) { free(comp); comp = NULL; }
    }
    iniciar_objeto(pD, pNum);
    adc_buffer_texto(&pD->saida, "<< ");
    if (pDictExtra) { adc_buffer_texto(&pD->saida, pDictExtra); adc_buffer_char(&pD->saida, ' '); }
    if (comp) adc_buffer_texto(&pD->saida, "/Filter /FlateDecode ");
    adc_buffer_texto(&pD->saida, "/Length "); adc_buffer_int(&pD->saida, comp ? (int64_t)comp_tam : pTamanho);
    adc_buffer_texto(&pD->saida, " >>\nstream\n");
    if (comp) adc_buffer_bytes(&pD->saida, comp, (int32_t)comp_tam); else adc_buffer_bytes(&pD->saida, pDados, pTamanho);
    adc_buffer_texto(&pD->saida, "\nendstream");
    fim_objeto(pD);
    free(comp);
    return pNum;
}

/* ------------------------------------------------------------- documento ---- */

void adc_pdf_doc_iniciar(ADC_PdfDoc* pD, ADC_Arena* pArena) {
    memset(pD, 0, sizeof *pD);
    pD->arena = pArena;
    adc_buffer_iniciar(&pD->saida);
    pD->comprimir = true;
}

void adc_pdf_doc_liberar(ADC_PdfDoc* pD) {
    for (int32_t i = 0; i < pD->n_paginas; i++) { adc_buffer_liberar(&pD->paginas[i].conteudo); free(pD->paginas[i].imagens_usadas); }
    free(pD->paginas); free(pD->offsets);
    adc_buffer_liberar(&pD->saida);
}

int32_t adc_pdf_doc_fonte(ADC_PdfDoc* pD, ADC_PdfFonte* pFonte) {
    if (pD->n_fontes >= ADC_PDF_MAX_FONTES) return -1;
    pD->fontes[pD->n_fontes] = pFonte;
    return pD->n_fontes++;
}

int32_t adc_pdf_doc_nova_pagina(ADC_PdfDoc* pD, double pLargura, double pAltura) {
    if (pD->n_paginas == pD->cap_paginas) {
        int32_t nova = pD->cap_paginas ? pD->cap_paginas * 2 : 16;
        ADC_PdfPagina* np = (ADC_PdfPagina*)realloc(pD->paginas, sizeof(ADC_PdfPagina) * (size_t)nova);
        if (!np) { pD->falhou = true; pD->erro = "sem memória (páginas)"; return -1; }
        pD->paginas = np; pD->cap_paginas = nova;
    }
    ADC_PdfPagina* p = &pD->paginas[pD->n_paginas];
    memset(p, 0, sizeof *p);
    adc_buffer_iniciar(&p->conteudo);
    p->largura = pLargura; p->altura = pAltura;
    return pD->n_paginas++;
}

ADC_PdfPagina* adc_pdf_doc_pagina(ADC_PdfDoc* pD, int32_t pIndice) { return (pIndice >= 0 && pIndice < pD->n_paginas) ? &pD->paginas[pIndice] : NULL; }

/* ----------------------------------------------------------- operadores ---- */

static void num(ADC_Buffer* b, double v) { adc_pdf_escrever_numero(b, v); adc_buffer_char(b, ' '); }

void adc_pdf_pg_cor_preenchimento(ADC_PdfPagina* pP, double r, double g, double b) { num(&pP->conteudo, r); num(&pP->conteudo, g); num(&pP->conteudo, b); adc_buffer_texto(&pP->conteudo, "rg\n"); }
void adc_pdf_pg_cor_traco(ADC_PdfPagina* pP, double r, double g, double b) { num(&pP->conteudo, r); num(&pP->conteudo, g); num(&pP->conteudo, b); adc_buffer_texto(&pP->conteudo, "RG\n"); }
void adc_pdf_pg_largura_traco(ADC_PdfPagina* pP, double w) { num(&pP->conteudo, w); adc_buffer_texto(&pP->conteudo, "w\n"); }
void adc_pdf_pg_retangulo(ADC_PdfPagina* pP, double x, double y, double w, double h, bool pPreencher, bool pTracar) {
    num(&pP->conteudo, x); num(&pP->conteudo, y); num(&pP->conteudo, w); num(&pP->conteudo, h);
    adc_buffer_texto(&pP->conteudo, pPreencher && pTracar ? "re B\n" : pPreencher ? "re f\n" : pTracar ? "re S\n" : "re n\n");
}
void adc_pdf_pg_linha(ADC_PdfPagina* pP, double x1, double y1, double x2, double y2) { num(&pP->conteudo, x1); num(&pP->conteudo, y1); adc_buffer_texto(&pP->conteudo, "m "); num(&pP->conteudo, x2); num(&pP->conteudo, y2); adc_buffer_texto(&pP->conteudo, "l S\n"); }
void adc_pdf_pg_salvar(ADC_PdfPagina* pP) { adc_buffer_texto(&pP->conteudo, "q\n"); }
void adc_pdf_pg_restaurar(ADC_PdfPagina* pP) { adc_buffer_texto(&pP->conteudo, "Q\n"); }
void adc_pdf_pg_recorte(ADC_PdfPagina* pP, double x, double y, double w, double h) { num(&pP->conteudo, x); num(&pP->conteudo, y); num(&pP->conteudo, w); num(&pP->conteudo, h); adc_buffer_texto(&pP->conteudo, "re W n\n"); }

void adc_pdf_pg_texto(ADC_PdfDoc* pD, ADC_PdfPagina* pP, int32_t pIndiceFonte, double pTamanho, double x, double y, ADC_Fatia pTexto) {
    if (pIndiceFonte < 0 || pIndiceFonte >= pD->n_fontes || pTexto.tamanho == 0) return;
    ADC_PdfFonte* f = pD->fontes[pIndiceFonte];
    pP->usa_fonte[pIndiceFonte] = true;
    ADC_Buffer* b = &pP->conteudo;
    adc_buffer_texto(b, "BT /F"); adc_buffer_int(b, pIndiceFonte); adc_buffer_char(b, ' '); num(b, pTamanho); adc_buffer_texto(b, "Tf ");
    num(b, x); num(b, y); adc_buffer_texto(b, "Td <");
    int32_t p = 0;
    while (p < pTexto.tamanho) {
        uint32_t cp = adc_utf8_decodificar(pTexto.dados, pTexto.tamanho, &p);
        int32_t g = adc_pdf_fonte_gid(f, cp);
        adc_pdf_fonte_marcar(f, g);
        char t[8]; snprintf(t, sizeof t, "%04X", (unsigned)(g & 0xFFFF));
        adc_buffer_texto(b, t);
    }
    adc_buffer_texto(b, "> Tj ET\n");
}

void adc_pdf_pg_imagem(ADC_PdfDoc* pD, ADC_PdfPagina* pP, int32_t pIndiceImagem, double x, double y, double w, double h) {
    if (pIndiceImagem < 0 || pIndiceImagem >= pD->n_imagens) return;
    bool ja = false;
    for (int32_t i = 0; i < pP->n_imagens; i++) if (pP->imagens_usadas[i] == pIndiceImagem) { ja = true; break; }
    if (!ja) {
        if (pP->n_imagens == pP->cap_imagens) { int32_t nova = pP->cap_imagens ? pP->cap_imagens * 2 : 8; int32_t* ni = (int32_t*)realloc(pP->imagens_usadas, sizeof(int32_t) * (size_t)nova); if (!ni) return; pP->imagens_usadas = ni; pP->cap_imagens = nova; }
        pP->imagens_usadas[pP->n_imagens++] = pIndiceImagem;
    }
    ADC_Buffer* b = &pP->conteudo;
    adc_buffer_texto(b, "q "); num(b, w); adc_buffer_texto(b, "0 0 "); num(b, h); num(b, x); num(b, y); adc_buffer_texto(b, "cm /Im"); adc_buffer_int(b, pIndiceImagem); adc_buffer_texto(b, " Do Q\n");
}

static void anexar_link(ADC_PdfDoc* pD, ADC_PdfPagina* pP, double x, double y, double w, double h, ADC_Fatia pURI, ADC_Fatia pDestino) {
    ADC_PdfLink* l = ADC_NOVO(pD->arena, ADC_PdfLink);
    if (!l) return;
    l->x = x; l->y = y; l->w = w; l->h = h; l->uri = pURI; l->destino = pDestino;
    if (pP->ultimo_link) pP->ultimo_link->proximo = l; else pP->links = l;
    pP->ultimo_link = l;
}
void adc_pdf_pg_link_uri(ADC_PdfDoc* pD, ADC_PdfPagina* pP, double x, double y, double w, double h, ADC_Fatia pURI) { anexar_link(pD, pP, x, y, w, h, pURI, adc_fatia(NULL, 0)); }
void adc_pdf_pg_link_destino(ADC_PdfDoc* pD, ADC_PdfPagina* pP, double x, double y, double w, double h, ADC_Fatia pDestino) { anexar_link(pD, pP, x, y, w, h, adc_fatia(NULL, 0), pDestino); }

void adc_pdf_doc_destino(ADC_PdfDoc* pD, ADC_Fatia pNome, int32_t pPagina, double y) {
    if (pNome.tamanho == 0) return;
    for (ADC_PdfDestino* d = pD->destinos; d; d = d->proximo) if (adc_fatia_igual_f(d->nome, pNome)) { d->pagina = pPagina; d->y = y; return; }
    ADC_PdfDestino* d = ADC_NOVO(pD->arena, ADC_PdfDestino);
    if (!d) return;
    d->nome = pNome; d->pagina = pPagina; d->y = y; d->proximo = pD->destinos; pD->destinos = d;
}

void adc_pdf_doc_marcador(ADC_PdfDoc* pD, ADC_Fatia pTitulo, int32_t pNivel, int32_t pPagina, double y) {
    ADC_PdfMarcador* m = ADC_NOVO(pD->arena, ADC_PdfMarcador);
    if (!m) return;
    m->titulo = pTitulo; m->nivel = pNivel; m->pagina = pPagina; m->y = y;
    if (pD->ultimo_marcador) pD->ultimo_marcador->proximo = m; else pD->marcadores = m;
    pD->ultimo_marcador = m;
}

/* --------------------------------------------------------------- imagens ---- */

int32_t adc_pdf_doc_imagem_por_chave(ADC_PdfDoc* pD, ADC_Fatia pChave, int32_t* pLargura, int32_t* pAltura) {
    int32_t idx = 0;
    for (ADC_PdfImagem* i = pD->imagens; i; i = i->proxima, idx++) if (adc_fatia_igual_f(i->chave, pChave)) { if (pLargura) *pLargura = i->largura; if (pAltura) *pAltura = i->altura; return idx; }
    return -1;
}

int32_t adc_pdf_doc_imagem(ADC_PdfDoc* pD, ADC_Fatia pChave, int32_t pLargura, int32_t pAltura, int32_t pComponentes,
                           const uint8_t* pPixels, int32_t pTamanhoPixels, const uint8_t* pAlpha, int32_t pTamanhoAlpha,
                           bool pJaDCT, bool pCMYKInvertido) {
    ADC_PdfImagem* im = ADC_NOVO(pD->arena, ADC_PdfImagem);
    if (!im) return -1;
    im->largura = pLargura; im->altura = pAltura; im->chave = pChave;
    /* os XObjects são escritos agora (os bytes são do chamador e podem ser liberados depois) */
    char dict[256];
    if (pAlpha) {
        snprintf(dict, sizeof dict, "/Type /XObject /Subtype /Image /Width %d /Height %d /ColorSpace /DeviceGray /BitsPerComponent 8", pLargura, pAltura);
        im->objeto_smask = escrever_stream(pD, 0, dict, pAlpha, pTamanhoAlpha, true);
    }
    const char* cs = pComponentes == 1 ? "/DeviceGray" : pComponentes == 4 ? "/DeviceCMYK" : "/DeviceRGB";
    if (pJaDCT) {
        snprintf(dict, sizeof dict, "/Type /XObject /Subtype /Image /Width %d /Height %d /ColorSpace %s /BitsPerComponent 8 /Filter /DCTDecode%s%s", pLargura, pAltura, cs,
                 pCMYKInvertido ? " /Decode [1 0 1 0 1 0 1 0]" : "", im->objeto_smask ? "" : "");
        if (im->objeto_smask) { size_t l = strlen(dict); snprintf(dict + l, sizeof dict - l, " /SMask %d 0 R", im->objeto_smask); }
        im->objeto = escrever_stream(pD, 0, dict, pPixels, pTamanhoPixels, false);
    } else {
        snprintf(dict, sizeof dict, "/Type /XObject /Subtype /Image /Width %d /Height %d /ColorSpace %s /BitsPerComponent 8", pLargura, pAltura, cs);
        if (im->objeto_smask) { size_t l = strlen(dict); snprintf(dict + l, sizeof dict - l, " /SMask %d 0 R", im->objeto_smask); }
        im->objeto = escrever_stream(pD, 0, dict, pPixels, pTamanhoPixels, true);
    }
    ADC_PdfImagem** p = &pD->imagens; while (*p) p = &(*p)->proxima; *p = im;
    return pD->n_imagens++;
}

/* ------------------------------------------------------------- finalizar ---- */

static ADC_PdfImagem* imagem_indice(ADC_PdfDoc* pD, int32_t pIdx) { ADC_PdfImagem* i = pD->imagens; while (i && pIdx-- > 0) i = i->proxima; return i; }

bool adc_pdf_doc_finalizar(ADC_PdfDoc* pD, ADC_Buffer* pSaida) {
    if (pD->falhou) return false;
    ADC_Buffer* S = &pD->saida;
    if (S->tamanho == 0) adc_buffer_texto(S, "%PDF-1.7\n%\xE2\xE3\xCF\xD3\n");
    /* fontes: Type0 + CIDFontType2 + FontDescriptor + FontFile2 + ToUnicode */
    for (int32_t i = 0; i < pD->n_fontes; i++) {
        ADC_PdfFonte* f = pD->fontes[i];
        ADC_Buffer ttf; adc_buffer_iniciar(&ttf);
        if (!adc_pdf_fonte_subconjunto(f, &ttf)) { adc_buffer_liberar(&ttf); pD->erro = "falha ao gerar o subconjunto da fonte TrueType"; return false; }
        char dict[128]; snprintf(dict, sizeof dict, "/Length1 %d", ttf.tamanho);
        int32_t obj_file = escrever_stream(pD, 0, dict, ttf.dados, ttf.tamanho, true);
        adc_buffer_liberar(&ttf);
        int32_t obj_desc = reservar_objeto(pD);
        iniciar_objeto(pD, obj_desc);
        adc_buffer_texto(S, "<< /Type /FontDescriptor /FontName /"); adc_buffer_texto(S, f->nome_pdf);
        adc_buffer_texto(S, " /Flags "); adc_buffer_int(S, (f->mono ? 1 : 0) | 4 | (f->italico ? 64 : 0) | (f->negrito ? (1 << 18) : 0));
        adc_buffer_texto(S, " /FontBBox ["); for (int k = 0; k < 4; k++) { adc_pdf_escrever_numero(S, f->bbox[k]); adc_buffer_char(S, ' '); } adc_buffer_texto(S, "] /ItalicAngle ");
        adc_buffer_texto(S, f->italico ? "-12" : "0"); adc_buffer_texto(S, " /Ascent "); adc_pdf_escrever_numero(S, f->ascender);
        adc_buffer_texto(S, " /Descent "); adc_pdf_escrever_numero(S, f->descender); adc_buffer_texto(S, " /CapHeight "); adc_pdf_escrever_numero(S, f->cap_height);
        adc_buffer_texto(S, " /XHeight "); adc_pdf_escrever_numero(S, f->x_height); adc_buffer_texto(S, " /StemV "); adc_buffer_texto(S, f->negrito ? "140" : "80");
        adc_buffer_texto(S, " /FontFile2 "); adc_buffer_int(S, obj_file); adc_buffer_texto(S, " 0 R >>");
        fim_objeto(pD);
        ADC_Buffer tu; adc_buffer_iniciar(&tu); adc_pdf_fonte_escrever_tounicode(f, &tu);
        int32_t obj_tu = escrever_stream(pD, 0, NULL, tu.dados, tu.tamanho, true);
        adc_buffer_liberar(&tu);
        int32_t obj_cid = reservar_objeto(pD);
        iniciar_objeto(pD, obj_cid);
        adc_buffer_texto(S, "<< /Type /Font /Subtype /CIDFontType2 /BaseFont /"); adc_buffer_texto(S, f->nome_pdf);
        adc_buffer_texto(S, " /CIDSystemInfo << /Registry (Adobe) /Ordering (Identity) /Supplement 0 >> /FontDescriptor "); adc_buffer_int(S, obj_desc);
        adc_buffer_texto(S, " 0 R /DW 1000 /W "); adc_pdf_fonte_escrever_W(f, S); adc_buffer_texto(S, " /CIDToGIDMap /Identity >>");
        fim_objeto(pD);
        f->objeto = reservar_objeto(pD);
        iniciar_objeto(pD, f->objeto);
        adc_buffer_texto(S, "<< /Type /Font /Subtype /Type0 /BaseFont /"); adc_buffer_texto(S, f->nome_pdf);
        adc_buffer_texto(S, " /Encoding /Identity-H /DescendantFonts ["); adc_buffer_int(S, obj_cid); adc_buffer_texto(S, " 0 R] /ToUnicode "); adc_buffer_int(S, obj_tu); adc_buffer_texto(S, " 0 R >>");
        fim_objeto(pD);
    }
    /* páginas */
    int32_t obj_pages = reservar_objeto(pD);
    for (int32_t i = 0; i < pD->n_paginas; i++) pD->paginas[i].objeto = reservar_objeto(pD);
    for (int32_t i = 0; i < pD->n_paginas; i++) {
        ADC_PdfPagina* p = &pD->paginas[i];
        int32_t obj_cont = escrever_stream(pD, 0, NULL, p->conteudo.dados, p->conteudo.tamanho, pD->comprimir);
        /* anotações */
        int32_t n_annots = 0; for (ADC_PdfLink* l = p->links; l; l = l->proximo) n_annots++;
        int32_t* annots = n_annots ? (int32_t*)malloc(sizeof(int32_t) * (size_t)n_annots) : NULL;
        int32_t k = 0;
        for (ADC_PdfLink* l = p->links; l; l = l->proximo) {
            int32_t o = reservar_objeto(pD);
            if (annots) annots[k++] = o;
            iniciar_objeto(pD, o);
            adc_buffer_texto(S, "<< /Type /Annot /Subtype /Link /Border [0 0 0] /Rect [");
            adc_pdf_escrever_numero(S, l->x); adc_buffer_char(S, ' '); adc_pdf_escrever_numero(S, l->y); adc_buffer_char(S, ' ');
            adc_pdf_escrever_numero(S, l->x + l->w); adc_buffer_char(S, ' '); adc_pdf_escrever_numero(S, l->y + l->h); adc_buffer_texto(S, "] ");
            if (l->uri.tamanho) { adc_buffer_texto(S, "/A << /S /URI /URI "); adc_pdf_escrever_string_literal(S, l->uri); adc_buffer_texto(S, " >>"); }
            else {
                ADC_PdfDestino* d = pD->destinos; while (d && !adc_fatia_igual_f(d->nome, l->destino)) d = d->proximo;
                if (d && d->pagina >= 0 && d->pagina < pD->n_paginas) { adc_buffer_texto(S, "/Dest ["); adc_buffer_int(S, pD->paginas[d->pagina].objeto); adc_buffer_texto(S, " 0 R /XYZ 0 "); adc_pdf_escrever_numero(S, d->y); adc_buffer_texto(S, " 0]"); }
                else adc_buffer_texto(S, "/Dest [null /XYZ 0 0 0]");
            }
            adc_buffer_texto(S, " >>");
            fim_objeto(pD);
        }
        iniciar_objeto(pD, p->objeto);
        adc_buffer_texto(S, "<< /Type /Page /Parent "); adc_buffer_int(S, obj_pages); adc_buffer_texto(S, " 0 R /MediaBox [0 0 ");
        adc_pdf_escrever_numero(S, p->largura); adc_buffer_char(S, ' '); adc_pdf_escrever_numero(S, p->altura); adc_buffer_texto(S, "] /Contents "); adc_buffer_int(S, obj_cont);
        adc_buffer_texto(S, " 0 R /Resources << /ProcSet [/PDF /Text /ImageC /ImageB] /Font <<");
        for (int32_t fi = 0; fi < pD->n_fontes; fi++) if (p->usa_fonte[fi]) { adc_buffer_texto(S, " /F"); adc_buffer_int(S, fi); adc_buffer_char(S, ' '); adc_buffer_int(S, pD->fontes[fi]->objeto); adc_buffer_texto(S, " 0 R"); }
        adc_buffer_texto(S, " >>");
        if (p->n_imagens) {
            adc_buffer_texto(S, " /XObject <<");
            for (int32_t ii = 0; ii < p->n_imagens; ii++) { ADC_PdfImagem* im = imagem_indice(pD, p->imagens_usadas[ii]); if (!im) continue; adc_buffer_texto(S, " /Im"); adc_buffer_int(S, p->imagens_usadas[ii]); adc_buffer_char(S, ' '); adc_buffer_int(S, im->objeto); adc_buffer_texto(S, " 0 R"); }
            adc_buffer_texto(S, " >>");
        }
        adc_buffer_texto(S, " >>");
        if (n_annots) { adc_buffer_texto(S, " /Annots ["); for (int32_t a = 0; a < n_annots; a++) { adc_buffer_int(S, annots[a]); adc_buffer_texto(S, " 0 R "); } adc_buffer_char(S, ']'); }
        adc_buffer_texto(S, " >>");
        fim_objeto(pD);
        free(annots);
    }
    iniciar_objeto(pD, obj_pages);
    adc_buffer_texto(S, "<< /Type /Pages /Count "); adc_buffer_int(S, pD->n_paginas); adc_buffer_texto(S, " /Kids [");
    for (int32_t i = 0; i < pD->n_paginas; i++) { adc_buffer_int(S, pD->paginas[i].objeto); adc_buffer_texto(S, " 0 R "); }
    adc_buffer_texto(S, "] >>");
    fim_objeto(pD);
    /* marcadores */
    int32_t obj_outlines = 0;
    if (pD->marcadores) {
        obj_outlines = reservar_objeto(pD);
        int32_t n = 0; for (ADC_PdfMarcador* m = pD->marcadores; m; m = m->proximo) { m->objeto = reservar_objeto(pD); n++; }
        (void)n;
        /* calcula pai/irmãos por nível */
        ADC_PdfMarcador* pilha[16] = { 0 };
        for (ADC_PdfMarcador* m = pD->marcadores; m; m = m->proximo) {
            int32_t nivel = m->nivel < 1 ? 1 : m->nivel > 15 ? 15 : m->nivel;
            ADC_PdfMarcador* pai = NULL; for (int32_t k = nivel - 1; k >= 1; k--) if (pilha[k]) { pai = pilha[k]; break; }
            /* irmão anterior: o último do mesmo nível com o mesmo pai */
            ADC_PdfMarcador* ant = NULL; ADC_PdfMarcador* ult_filho = NULL; int32_t filhos = 0;
            for (ADC_PdfMarcador* q = pD->marcadores; q && q != m; q = q->proximo) {
                int32_t nq = q->nivel < 1 ? 1 : q->nivel;
                if (nq == nivel) { ant = q; }
                else if (nq < nivel) { if (q == pai) ant = NULL; }
            }
            for (ADC_PdfMarcador* q = m->proximo; q; q = q->proximo) { int32_t nq = q->nivel < 1 ? 1 : q->nivel; if (nq <= nivel) break; if (nq == nivel + 1) { filhos++; ult_filho = q; } }
            ADC_PdfMarcador* prox = NULL; for (ADC_PdfMarcador* q = m->proximo; q; q = q->proximo) { int32_t nq = q->nivel < 1 ? 1 : q->nivel; if (nq < nivel) break; if (nq == nivel) { prox = q; break; } }
            ADC_PdfMarcador* primeiro_filho = NULL; for (ADC_PdfMarcador* q = m->proximo; q; q = q->proximo) { int32_t nq = q->nivel < 1 ? 1 : q->nivel; if (nq <= nivel) break; if (nq == nivel + 1) { primeiro_filho = q; break; } }
            iniciar_objeto(pD, m->objeto);
            adc_buffer_texto(S, "<< /Title "); adc_pdf_escrever_string_literal(S, m->titulo);
            adc_buffer_texto(S, " /Parent "); adc_buffer_int(S, pai ? pai->objeto : obj_outlines); adc_buffer_texto(S, " 0 R");
            if (ant) { adc_buffer_texto(S, " /Prev "); adc_buffer_int(S, ant->objeto); adc_buffer_texto(S, " 0 R"); }
            if (prox) { adc_buffer_texto(S, " /Next "); adc_buffer_int(S, prox->objeto); adc_buffer_texto(S, " 0 R"); }
            if (primeiro_filho) { adc_buffer_texto(S, " /First "); adc_buffer_int(S, primeiro_filho->objeto); adc_buffer_texto(S, " 0 R /Last "); adc_buffer_int(S, ult_filho->objeto); adc_buffer_texto(S, " 0 R /Count "); adc_buffer_int(S, filhos); }
            if (m->pagina >= 0 && m->pagina < pD->n_paginas) { adc_buffer_texto(S, " /Dest ["); adc_buffer_int(S, pD->paginas[m->pagina].objeto); adc_buffer_texto(S, " 0 R /XYZ 0 "); adc_pdf_escrever_numero(S, m->y); adc_buffer_texto(S, " 0]"); }
            adc_buffer_texto(S, " >>");
            fim_objeto(pD);
            pilha[nivel] = m; for (int32_t k = nivel + 1; k < 16; k++) pilha[k] = NULL;
        }
        ADC_PdfMarcador* primeiro = NULL; ADC_PdfMarcador* ultimo = NULL; int32_t topo = 0;
        for (ADC_PdfMarcador* m = pD->marcadores; m; m = m->proximo) if ((m->nivel < 1 ? 1 : m->nivel) == 1) { if (!primeiro) primeiro = m; ultimo = m; topo++; }
        iniciar_objeto(pD, obj_outlines);
        adc_buffer_texto(S, "<< /Type /Outlines");
        if (primeiro) { adc_buffer_texto(S, " /First "); adc_buffer_int(S, primeiro->objeto); adc_buffer_texto(S, " 0 R /Last "); adc_buffer_int(S, ultimo->objeto); adc_buffer_texto(S, " 0 R /Count "); adc_buffer_int(S, topo); }
        adc_buffer_texto(S, " >>");
        fim_objeto(pD);
    }
    /* destinos nomeados */
    int32_t obj_dests = 0;
    if (pD->destinos) {
        obj_dests = reservar_objeto(pD);
        iniciar_objeto(pD, obj_dests);
        adc_buffer_texto(S, "<< ");
        for (ADC_PdfDestino* d = pD->destinos; d; d = d->proximo) {
            if (d->pagina < 0 || d->pagina >= pD->n_paginas) continue;
            adc_buffer_char(S, '/');
            for (int32_t k = 0; k < d->nome.tamanho; k++) { unsigned char c = (unsigned char)d->nome.dados[k]; if (adc_eh_alnum((char)c) || c == '-' || c == '_' || c == '.') adc_buffer_char(S, (char)c); else { char t[8]; snprintf(t, sizeof t, "#%02X", c); adc_buffer_texto(S, t); } }
            adc_buffer_texto(S, " ["); adc_buffer_int(S, pD->paginas[d->pagina].objeto); adc_buffer_texto(S, " 0 R /XYZ 0 "); adc_pdf_escrever_numero(S, d->y); adc_buffer_texto(S, " 0] ");
        }
        adc_buffer_texto(S, ">>");
        fim_objeto(pD);
    }
    /* info */
    int32_t obj_info = reservar_objeto(pD);
    iniciar_objeto(pD, obj_info);
    adc_buffer_texto(S, "<< /Producer (AsciiDocConverter) /Creator (AsciiDocConverter)");
    if (pD->titulo.tamanho) { adc_buffer_texto(S, " /Title "); adc_pdf_escrever_string_literal(S, pD->titulo); }
    if (pD->autor.tamanho) { adc_buffer_texto(S, " /Author "); adc_pdf_escrever_string_literal(S, pD->autor); }
    if (pD->assunto.tamanho) { adc_buffer_texto(S, " /Subject "); adc_pdf_escrever_string_literal(S, pD->assunto); }
    if (pD->palavras_chave.tamanho) { adc_buffer_texto(S, " /Keywords "); adc_pdf_escrever_string_literal(S, pD->palavras_chave); }
    if (pD->data_definida) {
        if (pD->data_criacao.tamanho) { adc_buffer_texto(S, " /CreationDate ("); adc_buffer_fatia(S, pD->data_criacao); adc_buffer_texto(S, ")"); }
    } else { time_t t = time(NULL); struct tm* tm = gmtime(&t); if (tm) { char d[96]; snprintf(d, sizeof d, " /CreationDate (D:%04d%02d%02d%02d%02d%02dZ)", (tm->tm_year + 1900) % 10000, (tm->tm_mon + 1) % 100, tm->tm_mday % 100, tm->tm_hour % 100, tm->tm_min % 100, tm->tm_sec % 100); adc_buffer_texto(S, d); } }
    adc_buffer_texto(S, " >>");
    fim_objeto(pD);
    /* catálogo */
    int32_t obj_cat = reservar_objeto(pD);
    iniciar_objeto(pD, obj_cat);
    adc_buffer_texto(S, "<< /Type /Catalog /Pages "); adc_buffer_int(S, obj_pages); adc_buffer_texto(S, " 0 R /Lang (pt-BR)");
    if (obj_outlines) { adc_buffer_texto(S, " /Outlines "); adc_buffer_int(S, obj_outlines); adc_buffer_texto(S, " 0 R /PageMode /UseOutlines"); }
    if (obj_dests) { adc_buffer_texto(S, " /Dests "); adc_buffer_int(S, obj_dests); adc_buffer_texto(S, " 0 R"); }
    adc_buffer_texto(S, " >>");
    fim_objeto(pD);
    /* xref */
    int32_t xref = S->tamanho;
    adc_buffer_texto(S, "xref\n0 "); adc_buffer_int(S, pD->n_objetos + 1); adc_buffer_texto(S, "\n0000000000 65535 f \n");
    for (int32_t i = 1; i <= pD->n_objetos; i++) { char t[24]; snprintf(t, sizeof t, "%010d 00000 n \n", pD->offsets[i]); adc_buffer_texto(S, t); }
    adc_buffer_texto(S, "trailer\n<< /Size "); adc_buffer_int(S, pD->n_objetos + 1); adc_buffer_texto(S, " /Root "); adc_buffer_int(S, obj_cat); adc_buffer_texto(S, " 0 R /Info "); adc_buffer_int(S, obj_info);
    adc_buffer_texto(S, " 0 R >>\nstartxref\n"); adc_buffer_int(S, xref); adc_buffer_texto(S, "\n%%EOF\n");
    if (S->sem_memoria) { pD->erro = "sem memória ao montar o PDF"; return false; }
    *pSaida = pD->saida;
    adc_buffer_iniciar(&pD->saida);
    return true;
}
