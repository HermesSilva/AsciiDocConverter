#include "adc_pdf_fonte.h"

#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#define STBTT_malloc(x,u) ((void)(u), malloc(x))
#define STBTT_free(x,u)   ((void)(u), free(x))
#if defined(_MSC_VER)
#  pragma warning(push)
#  pragma warning(disable: 4244 4456 4457 4701 4702 4100 4127 4267)
#endif
#if defined(__GNUC__)
#  pragma GCC diagnostic push
#  pragma GCC diagnostic ignored "-Wunused-function"
#  pragma GCC diagnostic ignored "-Wsign-compare"
#  pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#  pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif
#include "../../terceiros/stb_truetype.h"
#if defined(__GNUC__)
#  pragma GCC diagnostic pop
#endif
#if defined(_MSC_VER)
#  pragma warning(pop)
#endif

/* ------------------------------------------------------------ leitura ---- */

static uint32_t be32(const uint8_t* p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }
static uint16_t be16(const uint8_t* p) { return (uint16_t)(((uint16_t)p[0] << 8) | p[1]); }
static int16_t  bes16(const uint8_t* p) { return (int16_t)be16(p); }

/* Localiza uma tabela pelo tag; devolve offset (0 se ausente) e tamanho. */
static uint32_t tabela(const ADC_PdfFonte* pF, const char* pTag, uint32_t* pTamanho) {
    const uint8_t* d = pF->dados;
    if (pF->tamanho < 12) return 0;
    uint32_t n = be16(d + 4);
    for (uint32_t i = 0; i < n; i++) {
        uint32_t o = 12 + i * 16;
        if (o + 16 > (uint32_t)pF->tamanho) return 0;
        if (memcmp(d + o, pTag, 4) == 0) {
            uint32_t off = be32(d + o + 8), tam = be32(d + o + 12);
            if (off > (uint32_t)pF->tamanho || tam > (uint32_t)pF->tamanho - off) return 0;
            if (pTamanho) *pTamanho = tam;
            return off;
        }
    }
    return 0;
}

bool adc_pdf_fonte_carregar(ADC_PdfFonte* pF, ADC_Arena* pArena, const uint8_t* pDados, int32_t pTamanho, const char* pNomePdf) {
    memset(pF, 0, sizeof *pF);
    pF->arena = pArena; pF->dados = pDados; pF->tamanho = pTamanho; pF->nome_pdf = pNomePdf;
    stbtt_fontinfo* info = (stbtt_fontinfo*)adc_arena_alocar(pArena, sizeof(stbtt_fontinfo));
    if (!info) return false;
    if (!stbtt_InitFont(info, pDados, stbtt_GetFontOffsetForIndex(pDados, 0))) return false;
    pF->info = info;
    uint32_t tam;
    uint32_t head = tabela(pF, "head", &tam);
    uint32_t maxp = tabela(pF, "maxp", &tam);
    if (!head || !maxp) return false;
    pF->unidades_por_em = be16(pDados + head + 18);
    if (pF->unidades_por_em <= 0) return false;
    pF->n_glifos = be16(pDados + maxp + 4);
    int asc, desc, gap;
    stbtt_GetFontVMetrics(info, &asc, &desc, &gap);
    double esc = 1000.0 / pF->unidades_por_em;
    pF->ascender = asc * esc; pF->descender = desc * esc; pF->altura_linha = (asc - desc + gap) * esc;
    int x0, y0, x1, y1;
    stbtt_GetFontBoundingBox(info, &x0, &y0, &x1, &y1);
    pF->bbox[0] = x0 * esc; pF->bbox[1] = y0 * esc; pF->bbox[2] = x1 * esc; pF->bbox[3] = y1 * esc;
    /* cap height e x height aproximados pelas caixas de 'H' e 'x' */
    int g = stbtt_FindGlyphIndex(info, 'H'); if (g) { stbtt_GetGlyphBox(info, g, &x0, &y0, &x1, &y1); pF->cap_height = y1 * esc; } else pF->cap_height = 700;
    g = stbtt_FindGlyphIndex(info, 'x'); if (g) { stbtt_GetGlyphBox(info, g, &x0, &y0, &x1, &y1); pF->x_height = y1 * esc; } else pF->x_height = 500;
    /* OS/2: peso e itálico */
    uint32_t os2 = tabela(pF, "OS/2", &tam);
    if (os2 && tam >= 64) { pF->negrito = be16(pDados + os2 + 4) >= 600; pF->italico = (be16(pDados + os2 + 62) & 1) != 0; }
    uint32_t post = tabela(pF, "post", &tam);
    if (post && tam >= 16) pF->mono = be32(pDados + post + 12) != 0;
    pF->usado = (uint8_t*)adc_arena_alocar(pArena, (size_t)(pF->n_glifos + 7) / 8 + 1);
    if (!pF->usado) return false;
    adc_pdf_fonte_marcar(pF, 0);
    return true;
}

int32_t adc_pdf_fonte_gid(const ADC_PdfFonte* pF, uint32_t pCodepoint) {
    int g = stbtt_FindGlyphIndex((const stbtt_fontinfo*)pF->info, (int)pCodepoint);
    return g > 0 ? g : 0;
}

double adc_pdf_fonte_avanco(const ADC_PdfFonte* pF, int32_t pGid) {
    int adv, lsb;
    stbtt_GetGlyphHMetrics((const stbtt_fontinfo*)pF->info, pGid, &adv, &lsb);
    return adv * 1000.0 / pF->unidades_por_em;
}

void adc_pdf_fonte_marcar(ADC_PdfFonte* pF, int32_t pGid) {
    if (pGid < 0 || pGid >= pF->n_glifos) return;
    pF->usado[pGid >> 3] |= (uint8_t)(1u << (pGid & 7));
}

static bool usado(const ADC_PdfFonte* pF, int32_t pGid) { return (pF->usado[pGid >> 3] >> (pGid & 7)) & 1; }

double adc_pdf_fonte_largura(ADC_PdfFonte* pF, ADC_Fatia pTexto, double pTamanho, bool pMarcar) {
    double total = 0; int32_t p = 0;
    while (p < pTexto.tamanho) {
        uint32_t cp = adc_utf8_decodificar(pTexto.dados, pTexto.tamanho, &p);
        int32_t g = adc_pdf_fonte_gid(pF, cp);
        if (pMarcar) adc_pdf_fonte_marcar(pF, g);
        total += adc_pdf_fonte_avanco(pF, g);
    }
    return total * pTamanho / 1000.0;
}

/* ------------------------------------------------------- subconjunto ---- */

/* Inclui os componentes de glifos compostos no conjunto usado. */
static void fechar_compostos(ADC_PdfFonte* pF, uint32_t loca, bool longa, uint32_t glyf, uint32_t glyf_tam) {
    const uint8_t* d = pF->dados;
    bool mudou = true;
    int32_t voltas = 0;
    while (mudou && voltas++ < 8) {
        mudou = false;
        for (int32_t g = 0; g < pF->n_glifos; g++) {
            if (!usado(pF, g)) continue;
            uint32_t ini, fim;
            if (longa) { ini = be32(d + loca + 4u * (uint32_t)g); fim = be32(d + loca + 4u * (uint32_t)g + 4); }
            else { ini = 2u * be16(d + loca + 2u * (uint32_t)g); fim = 2u * be16(d + loca + 2u * (uint32_t)g + 2); }
            if (fim <= ini || fim > glyf_tam || fim - ini < 10) continue;
            const uint8_t* gl = d + glyf + ini;
            int16_t contornos = bes16(gl);
            if (contornos >= 0) continue;
            /* composto: percorre componentes */
            uint32_t p = 10;
            for (;;) {
                if (p + 4 > fim - ini) break;
                uint16_t flags = be16(gl + p); uint16_t idx = be16(gl + p + 2);
                p += 4;
                if (idx < pF->n_glifos && !usado(pF, idx)) { adc_pdf_fonte_marcar(pF, idx); mudou = true; }
                p += (flags & 1) ? 4 : 2;            /* ARG_1_AND_2_ARE_WORDS */
                if (flags & 8) p += 2;               /* WE_HAVE_A_SCALE */
                else if (flags & 0x40) p += 4;       /* WE_HAVE_AN_X_AND_Y_SCALE */
                else if (flags & 0x80) p += 8;       /* WE_HAVE_A_TWO_BY_TWO */
                if (!(flags & 0x20)) break;          /* MORE_COMPONENTS */
            }
        }
    }
}

static uint32_t soma_tabela(const uint8_t* p, uint32_t tam) {
    uint32_t s = 0;
    for (uint32_t i = 0; i < tam; i += 4) {
        uint32_t v = 0;
        for (uint32_t k = 0; k < 4; k++) v = (v << 8) | (i + k < tam ? p[i + k] : 0);
        s += v;
    }
    return s;
}

static void put32(ADC_Buffer* b, uint32_t v) { uint8_t t[4] = { (uint8_t)(v >> 24), (uint8_t)(v >> 16), (uint8_t)(v >> 8), (uint8_t)v }; adc_buffer_bytes(b, t, 4); }
static void put16(ADC_Buffer* b, uint16_t v) { uint8_t t[2] = { (uint8_t)(v >> 8), (uint8_t)v }; adc_buffer_bytes(b, t, 2); }

bool adc_pdf_fonte_subconjunto(ADC_PdfFonte* pF, ADC_Buffer* pSaida) {
    const uint8_t* d = pF->dados;
    uint32_t tam_head, tam_hhea, tam_maxp, tam_hmtx, tam_loca, tam_glyf, tam_cvt = 0, tam_fpgm = 0, tam_prep = 0;
    uint32_t head = tabela(pF, "head", &tam_head), hhea = tabela(pF, "hhea", &tam_hhea), maxp = tabela(pF, "maxp", &tam_maxp);
    uint32_t hmtx = tabela(pF, "hmtx", &tam_hmtx), loca = tabela(pF, "loca", &tam_loca), glyf = tabela(pF, "glyf", &tam_glyf);
    uint32_t cvt = tabela(pF, "cvt ", &tam_cvt), fpgm = tabela(pF, "fpgm", &tam_fpgm), prep = tabela(pF, "prep", &tam_prep);
    if (!head || !hhea || !maxp || !hmtx || !loca || !glyf) return false;
    bool longa = bes16(d + head + 50) != 0;
    uint32_t n = (uint32_t)pF->n_glifos;
    if ((longa && tam_loca < 4 * (n + 1)) || (!longa && tam_loca < 2 * (n + 1))) return false;
    fechar_compostos(pF, loca, longa, glyf, tam_glyf);

    /* novo glyf: só os glifos usados, na ordem; nova loca longa (32 bits) */
    ADC_Buffer nglyf; adc_buffer_iniciar(&nglyf);
    ADC_Buffer nloca; adc_buffer_iniciar(&nloca);
    for (uint32_t g = 0; g < n; g++) {
        put32(&nloca, (uint32_t)nglyf.tamanho);
        if (!usado(pF, (int32_t)g)) continue;
        uint32_t ini, fim;
        if (longa) { ini = be32(d + loca + 4 * g); fim = be32(d + loca + 4 * g + 4); }
        else { ini = 2u * be16(d + loca + 2 * g); fim = 2u * be16(d + loca + 2 * g + 2); }
        if (fim <= ini || fim > tam_glyf) continue;
        adc_buffer_bytes(&nglyf, d + glyf + ini, (int32_t)(fim - ini));
        while (nglyf.tamanho % 4) adc_buffer_char(&nglyf, 0);
    }
    put32(&nloca, (uint32_t)nglyf.tamanho);

    /* head com indexToLocFormat = 1 e checkSumAdjustment = 0 */
    uint8_t* nhead = (uint8_t*)malloc(tam_head);
    if (!nhead) { adc_buffer_liberar(&nglyf); adc_buffer_liberar(&nloca); return false; }
    memcpy(nhead, d + head, tam_head);
    nhead[8] = nhead[9] = nhead[10] = nhead[11] = 0;
    nhead[50] = 0; nhead[51] = 1;

    struct { const char* tag; const uint8_t* dados; uint32_t tam; } tabelas[9];
    int nt = 0;
    if (cvt)  { tabelas[nt].tag = "cvt "; tabelas[nt].dados = d + cvt;  tabelas[nt].tam = tam_cvt;  nt++; }
    if (fpgm) { tabelas[nt].tag = "fpgm"; tabelas[nt].dados = d + fpgm; tabelas[nt].tam = tam_fpgm; nt++; }
    tabelas[nt].tag = "glyf"; tabelas[nt].dados = nglyf.dados; tabelas[nt].tam = (uint32_t)nglyf.tamanho; nt++;
    tabelas[nt].tag = "head"; tabelas[nt].dados = nhead; tabelas[nt].tam = tam_head; nt++;
    tabelas[nt].tag = "hhea"; tabelas[nt].dados = d + hhea; tabelas[nt].tam = tam_hhea; nt++;
    tabelas[nt].tag = "hmtx"; tabelas[nt].dados = d + hmtx; tabelas[nt].tam = tam_hmtx; nt++;
    tabelas[nt].tag = "loca"; tabelas[nt].dados = nloca.dados; tabelas[nt].tam = (uint32_t)nloca.tamanho; nt++;
    tabelas[nt].tag = "maxp"; tabelas[nt].dados = d + maxp; tabelas[nt].tam = tam_maxp; nt++;
    if (prep) { tabelas[nt].tag = "prep"; tabelas[nt].dados = d + prep; tabelas[nt].tam = tam_prep; nt++; }

    /* cabeçalho: sfnt version 0x00010000, numTables, searchRange, entrySelector, rangeShift */
    adc_buffer_iniciar(pSaida);
    put32(pSaida, 0x00010000);
    put16(pSaida, (uint16_t)nt);
    uint16_t es = 0, sr = 1; while (sr * 2 <= nt) { sr *= 2; es++; }
    put16(pSaida, (uint16_t)(sr * 16)); put16(pSaida, es); put16(pSaida, (uint16_t)(nt * 16 - sr * 16));
    uint32_t off = 12 + 16u * (uint32_t)nt;
    for (int i = 0; i < nt; i++) {
        adc_buffer_bytes(pSaida, tabelas[i].tag, 4);
        put32(pSaida, soma_tabela(tabelas[i].dados, tabelas[i].tam));
        put32(pSaida, off);
        put32(pSaida, tabelas[i].tam);
        off += (tabelas[i].tam + 3u) & ~3u;
    }
    for (int i = 0; i < nt; i++) {
        adc_buffer_bytes(pSaida, tabelas[i].dados, (int32_t)tabelas[i].tam);
        while (pSaida->tamanho % 4) adc_buffer_char(pSaida, 0);
    }
    /* checkSumAdjustment = 0xB1B0AFBA - soma do arquivo */
    uint32_t soma = soma_tabela(pSaida->dados, (uint32_t)pSaida->tamanho);
    uint32_t adj = 0xB1B0AFBAu - soma;
    /* localiza head no arquivo gerado */
    uint32_t off_head = 12 + 16u * (uint32_t)nt;
    for (int i = 0; i < nt; i++) { if (strcmp(tabelas[i].tag, "head") == 0) break; off_head += (tabelas[i].tam + 3u) & ~3u; }
    if (off_head + 12 <= (uint32_t)pSaida->tamanho) { pSaida->dados[off_head + 8] = (uint8_t)(adj >> 24); pSaida->dados[off_head + 9] = (uint8_t)(adj >> 16); pSaida->dados[off_head + 10] = (uint8_t)(adj >> 8); pSaida->dados[off_head + 11] = (uint8_t)adj; }
    free(nhead);
    adc_buffer_liberar(&nglyf); adc_buffer_liberar(&nloca);
    return !pSaida->sem_memoria;
}

void adc_pdf_fonte_escrever_W(ADC_PdfFonte* pF, ADC_Buffer* pB) {
    /* formato: [ gid [w] gid [w] ... ] agrupando faixas contíguas */
    adc_buffer_char(pB, '[');
    int32_t g = 0;
    while (g < pF->n_glifos) {
        if (!usado(pF, g)) { g++; continue; }
        int32_t ini = g;
        adc_buffer_int(pB, ini); adc_buffer_texto(pB, " [");
        while (g < pF->n_glifos && usado(pF, g)) {
            adc_buffer_int(pB, (int64_t)(adc_pdf_fonte_avanco(pF, g) + 0.5)); adc_buffer_char(pB, ' ');
            g++;
            if (g - ini >= 64) break;
        }
        adc_buffer_texto(pB, "] ");
    }
    adc_buffer_char(pB, ']');
}

void adc_pdf_fonte_escrever_tounicode(ADC_PdfFonte* pF, ADC_Buffer* pB) {
    /* gid → codepoint: varre o cmap pelos codepoints BMP (e alguns blocos acima) — suficiente para as fontes da casa */
    adc_buffer_texto(pB,
        "/CIDInit /ProcSet findresource begin\n12 dict begin\nbegincmap\n/CIDSystemInfo << /Registry (Adobe) /Ordering (UCS) /Supplement 0 >> def\n"
        "/CMapName /Adobe-Identity-UCS def\n/CMapType 2 def\n1 begincodespacerange\n<0000> <FFFF>\nendcodespacerange\n");
    /* coleta pares */
    int32_t cap = 256, n = 0;
    uint32_t* pares = (uint32_t*)malloc(sizeof(uint32_t) * 2 * (size_t)cap);
    if (!pares) { adc_buffer_texto(pB, "endcmap\nCMapName currentdict /CMap defineresource pop\nend\nend\n"); return; }
    uint8_t* visto = (uint8_t*)calloc((size_t)(pF->n_glifos + 7) / 8 + 1, 1);
    for (uint32_t cp = 0x20; cp < 0x30000; cp++) {
        if (cp == 0x2FFFF) break;
        if (cp >= 0x3000 && cp < 0x1F000) { if (cp == 0x3000) cp = 0x1F000 - 1; continue; } /* pula CJK (fontes da casa não têm) */
        int32_t g = adc_pdf_fonte_gid(pF, cp);
        if (g <= 0 || !usado(pF, g) || (visto && ((visto[g >> 3] >> (g & 7)) & 1))) continue;
        if (visto) visto[g >> 3] |= (uint8_t)(1u << (g & 7));
        if (n == cap) { cap *= 2; uint32_t* np = (uint32_t*)realloc(pares, sizeof(uint32_t) * 2 * (size_t)cap); if (!np) break; pares = np; }
        pares[2 * n] = (uint32_t)g; pares[2 * n + 1] = cp; n++;
    }
    for (int32_t i = 0; i < n; i += 100) {
        int32_t lote = n - i < 100 ? n - i : 100;
        adc_buffer_int(pB, lote); adc_buffer_texto(pB, " beginbfchar\n");
        for (int32_t k = i; k < i + lote; k++) {
            char tmp[48];
            uint32_t cp = pares[2 * k + 1];
            if (cp < 0x10000) snprintf(tmp, sizeof tmp, "<%04X> <%04X>\n", pares[2 * k], cp);
            else { uint32_t v = cp - 0x10000; snprintf(tmp, sizeof tmp, "<%04X> <%04X%04X>\n", pares[2 * k], 0xD800 + (v >> 10), 0xDC00 + (v & 0x3FF)); }
            adc_buffer_texto(pB, tmp);
        }
        adc_buffer_texto(pB, "endbfchar\n");
    }
    free(pares); free(visto);
    adc_buffer_texto(pB, "endcmap\nCMapName currentdict /CMap defineresource pop\nend\nend\n");
}

void adc_pdf_fonte_liberar(ADC_PdfFonte* pF) { (void)pF; /* tudo na arena */ }
