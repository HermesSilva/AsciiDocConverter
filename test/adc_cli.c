/*
 * Ferramenta de desenvolvimento: converte um arquivo .adoc para o formato pedido pela ABI pública.
 *   adc_cli <entrada.adoc> <html|md|asg|pdf> [saida] [opcoes.json]
 * Resolve include::/imagens relativos ao diretório da entrada (só aqui, para teste — a biblioteca não lê disco).
 */
#include "../include/adc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct Ctx { char dir[1024]; char* buffers[256]; int n; } Ctx;

static char* ler_arquivo(const char* pCaminho, long* pTam) {
    FILE* f = fopen(pCaminho, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    char* d = (char*)malloc((size_t)n + 1);
    if (!d) { fclose(f); return NULL; }
    if (n > 0 && fread(d, 1, (size_t)n, f) != (size_t)n) { fclose(f); free(d); return NULL; }
    d[n] = 0; fclose(f); *pTam = n;
    return d;
}

static int32_t resolver(void* pCtx, int32_t pTipo, const char* pCaminho, int32_t pTam, const uint8_t** pConteudo, int32_t* pTamanho) {
    Ctx* c = (Ctx*)pCtx; (void)pTipo;
    if (c->n >= 256) return 1;
    char caminho[2048];
    snprintf(caminho, sizeof caminho, "%s%.*s", c->dir, pTam, pCaminho);
    long n; char* d = ler_arquivo(caminho, &n);
    if (!d) return 2;
    c->buffers[c->n++] = d;
    *pConteudo = (const uint8_t*)d; *pTamanho = (int32_t)n;
    return 0;
}

int main(int argc, char** argv) {
    if (argc < 3) { fprintf(stderr, "uso: adc_cli <entrada.adoc> <html|md|asg|pdf> [saida] [opcoes.json]\n"); return 2; }
    long n; char* texto = ler_arquivo(argv[1], &n);
    if (!texto) { fprintf(stderr, "nao consegui ler %s\n", argv[1]); return 2; }
    int32_t formato = strcmp(argv[2], "html") == 0 ? ADC_HTML5 : strcmp(argv[2], "md") == 0 ? ADC_MARKDOWN : strcmp(argv[2], "asg") == 0 ? ADC_ASG_JSON : strcmp(argv[2], "pdf") == 0 ? ADC_PDF : 0;
    if (!formato) { fprintf(stderr, "formato desconhecido: %s\n", argv[2]); return 2; }
    Ctx ctx; memset(&ctx, 0, sizeof ctx);
    const char* barra = strrchr(argv[1], '/'); const char* barra2 = strrchr(argv[1], '\\');
    if (barra2 > barra) barra = barra2;
    if (barra) snprintf(ctx.dir, sizeof ctx.dir, "%.*s", (int)(barra - argv[1] + 1), argv[1]);
    char* opcoes = NULL; long on = 0;
    if (argc >= 5) { opcoes = ler_arquivo(argv[4], &on); if (!opcoes) { fprintf(stderr, "nao consegui ler %s\n", argv[4]); return 2; } }
    uint8_t* saida = NULL; int32_t tam = 0; char* msgs = NULL;
    int32_t rc = ADC_Converter((const uint8_t*)texto, (int32_t)n, formato, opcoes, resolver, &ctx, &saida, &tam, &msgs);
    if (msgs && strcmp(msgs, "[]") != 0) fprintf(stderr, "%s\n", msgs);
    if (rc != ADC_OK) { fprintf(stderr, "ADC_Converter devolveu %d\n", rc); ADC_Liberar(msgs); return 1; }
    if (argc >= 4 && strcmp(argv[3], "-") != 0) {
        FILE* f = fopen(argv[3], "wb");
        if (!f) { fprintf(stderr, "nao consegui escrever %s\n", argv[3]); return 2; }
        fwrite(saida, 1, (size_t)tam, f); fclose(f);
    } else {
        fwrite(saida, 1, (size_t)tam, stdout);
    }
    ADC_Liberar(saida); ADC_Liberar(msgs);
    for (int i = 0; i < ctx.n; i++) free(ctx.buffers[i]);
    free(texto); free(opcoes);
    return 0;
}
