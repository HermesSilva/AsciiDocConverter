/*
 * Benchmark com método: converte um arquivo N vezes em cada formato e imprime a mediana (ms) e a
 * vazão (MB/s). Compilar junto dos fontes com -O2 (ver bench/rodar.sh). Mede só a biblioteca
 * (sem E/S de disco dentro do laço).
 *   bench <arquivo.adoc> [repeticoes=21]
 */
#include "../include/adc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static double agora_ms(void) {
#if defined(_WIN32)
    return (double)clock() * 1000.0 / CLOCKS_PER_SEC;
#else
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
#endif
}

static int comparar(const void* a, const void* b) { double x = *(const double*)a, y = *(const double*)b; return x < y ? -1 : x > y; }

int main(int argc, char** argv) {
    if (argc < 2) { fprintf(stderr, "uso: bench <arquivo.adoc> [repeticoes]\n"); return 2; }
    FILE* f = fopen(argv[1], "rb"); if (!f) { fprintf(stderr, "nao consegui ler %s\n", argv[1]); return 2; }
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t* d = (uint8_t*)malloc((size_t)n); if (fread(d, 1, (size_t)n, f) != (size_t)n) return 2; fclose(f);
    int rep = argc > 2 ? atoi(argv[2]) : 21; if (rep < 3) rep = 3;
    const char* nomes[] = { "", "HTML5", "Markdown", "PDF", "ASG" };
    printf("arquivo=%s bytes=%ld repeticoes=%d versao=%s\n", argv[1], n, rep, ADC_Versao());
    for (int fmt = 1; fmt <= 4; fmt++) {
        double* t = (double*)malloc(sizeof(double) * (size_t)rep);
        int32_t tam_saida = 0;
        for (int i = 0; i < rep; i++) {
            uint8_t* s = NULL; int32_t tam = 0; char* m = NULL;
            double t0 = agora_ms();
            int32_t rc = ADC_Converter(d, (int32_t)n, fmt, NULL, NULL, NULL, &s, &tam, &m);
            t[i] = agora_ms() - t0;
            if (rc != 0) { fprintf(stderr, "formato %s falhou: %d %s\n", nomes[fmt], rc, m ? m : ""); return 1; }
            tam_saida = tam;
            ADC_Liberar(s); ADC_Liberar(m);
        }
        qsort(t, (size_t)rep, sizeof(double), comparar);
        double mediana = t[rep / 2];
        printf("%-9s mediana=%8.3f ms  min=%8.3f ms  vazao=%7.2f MB/s  saida=%d bytes\n", nomes[fmt], mediana, t[0], (n / 1048576.0) / (mediana / 1000.0), tam_saida);
        free(t);
    }
    free(d);
    return 0;
}
