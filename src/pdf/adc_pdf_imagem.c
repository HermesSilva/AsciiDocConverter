#include "adc_pdf_imagem.h"
#include "../../terceiros/miniz.h"

static uint32_t be32(const uint8_t* p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }

static bool falha(ADC_ImagemDecodificada* pI, const char* pErro) { pI->erro = pErro; return false; }

/* ---------------------------------------------------------------- PNG ---- */

static int paeth(int a, int b, int c) {
    int p = a + b - c, pa = abs(p - a), pb = abs(p - b), pc = abs(p - c);
    return (pa <= pb && pa <= pc) ? a : (pb <= pc) ? b : c;
}

/* Desfiltra 'linhas' linhas de 'bytes_linha' bytes, com 'bpp' bytes por pixel (>=1). Entrada: filtro+dados por linha. */
static bool desfiltrar(const uint8_t* pEntrada, int32_t pLinhas, int32_t pBytesLinha, int32_t pBpp, uint8_t* pSaida) {
    const uint8_t* ant = NULL;
    for (int32_t y = 0; y < pLinhas; y++) {
        uint8_t filtro = pEntrada[(size_t)y * ((size_t)pBytesLinha + 1)];
        const uint8_t* src = pEntrada + (size_t)y * ((size_t)pBytesLinha + 1) + 1;
        uint8_t* dst = pSaida + (size_t)y * (size_t)pBytesLinha;
        for (int32_t x = 0; x < pBytesLinha; x++) {
            int a = x >= pBpp ? dst[x - pBpp] : 0;
            int b = ant ? ant[x] : 0;
            int c = (ant && x >= pBpp) ? ant[x - pBpp] : 0;
            int v = src[x];
            switch (filtro) {
                case 0: break;
                case 1: v += a; break;
                case 2: v += b; break;
                case 3: v += (a + b) / 2; break;
                case 4: v += paeth(a, b, c); break;
                default: return false;
            }
            dst[x] = (uint8_t)v;
        }
        ant = dst;
    }
    return true;
}

typedef struct Png { int32_t w, h, prof, tipo, entrelacado; const uint8_t* plte; int32_t n_plte; const uint8_t* trns; int32_t n_trns; } Png;

static int32_t canais(int32_t pTipo) { switch (pTipo) { case 0: return 1; case 2: return 3; case 3: return 1; case 4: return 2; case 6: return 4; default: return 0; } }

/* Lê a amostra 'i' (índice de amostra na linha) de uma linha desfiltrada, para profundidades 1/2/4/8/16; devolve 0..255. */
static int32_t amostra(const uint8_t* pLinha, int32_t pProf, int32_t pIdx) {
    switch (pProf) {
        case 8: return pLinha[pIdx];
        case 16: return pLinha[pIdx * 2];
        case 4: return ((pLinha[pIdx / 2] >> (4 * (1 - (pIdx & 1)))) & 0xF) * 17;
        case 2: return ((pLinha[pIdx / 4] >> (2 * (3 - (pIdx & 3)))) & 0x3) * 85;
        case 1: return ((pLinha[pIdx / 8] >> (7 - (pIdx & 7))) & 0x1) * 255;
        default: return 0;
    }
}
static int32_t indice_paleta(const uint8_t* pLinha, int32_t pProf, int32_t pIdx) {
    switch (pProf) {
        case 8: return pLinha[pIdx];
        case 4: return (pLinha[pIdx / 2] >> (4 * (1 - (pIdx & 1)))) & 0xF;
        case 2: return (pLinha[pIdx / 4] >> (2 * (3 - (pIdx & 3)))) & 0x3;
        case 1: return (pLinha[pIdx / 8] >> (7 - (pIdx & 7))) & 0x1;
        default: return 0;
    }
}

/* Converte uma linha desfiltrada (w pixels) para RGB/cinza 8 bits + alpha, escrevendo nos destinos com passo. */
static void converter_linha(const Png* P, const uint8_t* pLinha, int32_t pW, uint8_t* pRgb, int32_t pComp, uint8_t* pAlpha, int32_t pX0, int32_t pPasso) {
    int32_t nc = canais(P->tipo);
    for (int32_t x = 0; x < pW; x++) {
        int32_t px = pX0 + x * pPasso;
        uint8_t* o = pRgb + (size_t)px * (size_t)pComp;
        uint8_t a = 255;
        if (P->tipo == 3) {
            int32_t idx = indice_paleta(pLinha, P->prof, x);
            if (idx < P->n_plte) { o[0] = P->plte[idx * 3]; o[1] = P->plte[idx * 3 + 1]; o[2] = P->plte[idx * 3 + 2]; } else { o[0] = o[1] = o[2] = 0; }
            if (P->trns && idx < P->n_trns) a = P->trns[idx];
        } else if (P->tipo == 0) {
            o[0] = (uint8_t)amostra(pLinha, P->prof, x);
            if (P->trns && P->n_trns >= 2) { int32_t v = P->prof == 16 ? (pLinha[x * 2] << 8 | pLinha[x * 2 + 1]) : amostra(pLinha, P->prof, x) * ((1 << P->prof) - 1) / 255; if (v == ((P->trns[0] << 8) | P->trns[1])) a = 0; }
        } else if (P->tipo == 2) {
            for (int k = 0; k < 3; k++) o[k] = (uint8_t)amostra(pLinha, P->prof, x * nc + k);
        } else if (P->tipo == 4) {
            o[0] = (uint8_t)amostra(pLinha, P->prof, x * 2); a = (uint8_t)amostra(pLinha, P->prof, x * 2 + 1);
        } else if (P->tipo == 6) {
            for (int k = 0; k < 3; k++) o[k] = (uint8_t)amostra(pLinha, P->prof, x * 4 + k);
            a = (uint8_t)amostra(pLinha, P->prof, x * 4 + 3);
        }
        if (pAlpha) pAlpha[px] = a;
    }
}

static bool decodificar_png(const uint8_t* d, int32_t n, ADC_ImagemDecodificada* pI) {
    if (n < 33) return falha(pI, "PNG truncado");
    Png P; memset(&P, 0, sizeof P);
    ADC_Buffer idat; adc_buffer_iniciar(&idat);
    int32_t p = 8;
    bool ihdr = false;
    while (p + 12 <= n) {
        uint32_t tam = be32(d + p); const uint8_t* tipo = d + p + 4; const uint8_t* dados = d + p + 8;
        if (tam > (uint32_t)(n - p - 12)) { adc_buffer_liberar(&idat); return falha(pI, "PNG com bloco (chunk) truncado"); }
        if (memcmp(tipo, "IHDR", 4) == 0 && tam >= 13) {
            P.w = (int32_t)be32(dados); P.h = (int32_t)be32(dados + 4); P.prof = dados[8]; P.tipo = dados[9]; P.entrelacado = dados[12];
            ihdr = true;
        } else if (memcmp(tipo, "PLTE", 4) == 0) { P.plte = dados; P.n_plte = (int32_t)tam / 3; }
        else if (memcmp(tipo, "tRNS", 4) == 0) { P.trns = dados; P.n_trns = (int32_t)tam; }
        else if (memcmp(tipo, "IDAT", 4) == 0) adc_buffer_bytes(&idat, dados, (int32_t)tam);
        else if (memcmp(tipo, "IEND", 4) == 0) break;
        p += 12 + (int32_t)tam;
    }
    if (!ihdr || P.w <= 0 || P.h <= 0 || P.w > 20000 || P.h > 20000) { adc_buffer_liberar(&idat); return falha(pI, "PNG sem IHDR válido ou com dimensões fora do limite (20000 px)"); }
    int32_t nc = canais(P.tipo);
    if (!nc || !(P.prof == 1 || P.prof == 2 || P.prof == 4 || P.prof == 8 || P.prof == 16)) { adc_buffer_liberar(&idat); return falha(pI, "PNG com tipo de cor ou profundidade não suportados"); }
    if (P.tipo == 3 && !P.plte) { adc_buffer_liberar(&idat); return falha(pI, "PNG com paleta sem bloco PLTE"); }
    int32_t bpp = (nc * P.prof + 7) / 8; if (bpp < 1) bpp = 1;
    /* inflate */
    size_t bruto_tam_max = (size_t)P.h * ((size_t)P.w * (size_t)nc * (size_t)P.prof / 8 + 1 + 8) + 1024;
    uint8_t* bruto = (uint8_t*)malloc(bruto_tam_max);
    if (!bruto) { adc_buffer_liberar(&idat); return falha(pI, "sem memória para o PNG"); }
    mz_ulong fora = (mz_ulong)bruto_tam_max;
    int rc = mz_uncompress(bruto, &fora, idat.dados, (mz_ulong)idat.tamanho);
    adc_buffer_liberar(&idat);
    if (rc != MZ_OK) { free(bruto); return falha(pI, "PNG com dados IDAT corrompidos (inflate falhou)"); }
    int32_t comp = (P.tipo == 0 || P.tipo == 4) ? 1 : 3;
    bool tem_alpha = P.tipo == 4 || P.tipo == 6 || P.trns != NULL;
    uint8_t* rgb = (uint8_t*)malloc((size_t)P.w * (size_t)P.h * (size_t)comp);
    uint8_t* alpha = tem_alpha ? (uint8_t*)malloc((size_t)P.w * (size_t)P.h) : NULL;
    if (!rgb || (tem_alpha && !alpha)) { free(bruto); free(rgb); free(alpha); return falha(pI, "sem memória para a imagem"); }
    if (!P.entrelacado) {
        int32_t bytes_linha = (P.w * nc * P.prof + 7) / 8;
        if ((size_t)fora < (size_t)P.h * ((size_t)bytes_linha + 1)) { free(bruto); free(rgb); free(alpha); return falha(pI, "PNG com dados insuficientes"); }
        uint8_t* linhas = (uint8_t*)malloc((size_t)P.h * (size_t)bytes_linha);
        if (!linhas || !desfiltrar(bruto, P.h, bytes_linha, bpp, linhas)) { free(linhas); free(bruto); free(rgb); free(alpha); return falha(pI, "PNG com filtro inválido"); }
        for (int32_t y = 0; y < P.h; y++) converter_linha(&P, linhas + (size_t)y * (size_t)bytes_linha, P.w, rgb + (size_t)y * (size_t)P.w * (size_t)comp, comp, alpha ? alpha + (size_t)y * (size_t)P.w : NULL, 0, 1);
        free(linhas);
    } else {
        /* Adam7 */
        static const int32_t x0[7] = { 0, 4, 0, 2, 0, 1, 0 }, y0[7] = { 0, 0, 4, 0, 2, 0, 1 }, dx[7] = { 8, 8, 4, 4, 2, 2, 1 }, dy[7] = { 8, 8, 8, 4, 4, 2, 2 };
        size_t pos = 0;
        for (int pass = 0; pass < 7; pass++) {
            int32_t pw = (P.w - x0[pass] + dx[pass] - 1) / dx[pass], ph = (P.h - y0[pass] + dy[pass] - 1) / dy[pass];
            if (pw <= 0 || ph <= 0) continue;
            int32_t bytes_linha = (pw * nc * P.prof + 7) / 8;
            if (pos + (size_t)ph * ((size_t)bytes_linha + 1) > (size_t)fora) { free(bruto); free(rgb); free(alpha); return falha(pI, "PNG entrelaçado com dados insuficientes"); }
            uint8_t* linhas = (uint8_t*)malloc((size_t)ph * (size_t)bytes_linha);
            if (!linhas || !desfiltrar(bruto + pos, ph, bytes_linha, bpp, linhas)) { free(linhas); free(bruto); free(rgb); free(alpha); return falha(pI, "PNG entrelaçado com filtro inválido"); }
            for (int32_t y = 0; y < ph; y++) {
                int32_t py = y0[pass] + y * dy[pass];
                converter_linha(&P, linhas + (size_t)y * (size_t)bytes_linha, pw, rgb + (size_t)py * (size_t)P.w * (size_t)comp, comp, alpha ? alpha + (size_t)py * (size_t)P.w : NULL, x0[pass], dx[pass]);
            }
            free(linhas);
            pos += (size_t)ph * ((size_t)bytes_linha + 1);
        }
    }
    free(bruto);
    pI->largura = P.w; pI->altura = P.h; pI->componentes = comp;
    pI->pixels = rgb; pI->tamanho_pixels = P.w * P.h * comp;
    /* alpha totalmente opaco: descarta */
    if (alpha) { bool opaco = true; for (int32_t i = 0; i < P.w * P.h; i++) if (alpha[i] != 255) { opaco = false; break; } if (opaco) { free(alpha); alpha = NULL; } }
    pI->alpha = alpha; pI->tamanho_alpha = alpha ? P.w * P.h : 0;
    return true;
}

/* --------------------------------------------------------------- JPEG ---- */

static bool decodificar_jpeg(const uint8_t* d, int32_t n, ADC_ImagemDecodificada* pI) {
    int32_t p = 2; bool adobe = false; int32_t transform = -1;
    while (p + 4 <= n) {
        if (d[p] != 0xFF) { p++; continue; }
        uint8_t m = d[p + 1];
        if (m == 0xD8 || (m >= 0xD0 && m <= 0xD7) || m == 0x01 || m == 0xFF) { p += 2; continue; }
        if (p + 4 > n) break;
        int32_t tam = (d[p + 2] << 8) | d[p + 3];
        if (m == 0xEE && tam >= 12 && p + 2 + tam <= n && memcmp(d + p + 4, "Adobe", 5) == 0) { adobe = true; transform = d[p + 2 + tam - 1]; }
        if ((m >= 0xC0 && m <= 0xC3) || (m >= 0xC5 && m <= 0xC7) || (m >= 0xC9 && m <= 0xCB) || (m >= 0xCD && m <= 0xCF)) {
            if (p + 10 > n) break;
            int32_t h = (d[p + 5] << 8) | d[p + 6], w = (d[p + 7] << 8) | d[p + 8], nc = d[p + 9];
            if (w <= 0 || h <= 0) return falha(pI, "JPEG com dimensões inválidas");
            if (!(nc == 1 || nc == 3 || nc == 4)) return falha(pI, "JPEG com número de componentes não suportado");
            pI->largura = w; pI->altura = h; pI->componentes = nc;
            pI->pixels = (uint8_t*)malloc((size_t)n); if (!pI->pixels) return falha(pI, "sem memória para o JPEG");
            memcpy(pI->pixels, d, (size_t)n); pI->tamanho_pixels = n;
            pI->dct = true;
            pI->cmyk_invertido = nc == 4 && adobe;
            (void)transform;
            return true;
        }
        if (m == 0xDA) break; /* SOS antes de SOF: inválido */
        p += 2 + tam;
    }
    return falha(pI, "JPEG sem marcador SOF (dimensões não encontradas)");
}

bool adc_imagem_decodificar(const uint8_t* pDados, int32_t pTamanho, ADC_ImagemDecodificada* pI) {
    memset(pI, 0, sizeof *pI);
    if (!pDados || pTamanho < 8) return falha(pI, "imagem vazia ou truncada");
    static const uint8_t png_magica[8] = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };
    if (memcmp(pDados, png_magica, 8) == 0) return decodificar_png(pDados, pTamanho, pI);
    if (pDados[0] == 0xFF && pDados[1] == 0xD8) return decodificar_jpeg(pDados, pTamanho, pI);
    return falha(pI, "formato de imagem não reconhecido (suportados: PNG e JPEG)");
}

void adc_imagem_liberar(ADC_ImagemDecodificada* pI) { free(pI->pixels); free(pI->alpha); memset(pI, 0, sizeof *pI); }
