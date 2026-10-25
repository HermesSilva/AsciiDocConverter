# Benchmark (com método)

**Nenhum número daqui vai a texto de produto sem este método ao lado** (IA-7 da casa).

## Método

- Máquina: Intel Core i9-14900K, WSL 2 (Ubuntu 24.04), gcc 13.3, `-O2 -DNDEBUG`, um processo, uma thread.
- Harness: `bench/rodar.sh` (`bench/bench.c`): converte cada arquivo 21 vezes por formato e reporta a **mediana**
  e o mínimo. Só a biblioteca é medida (sem leitura de disco dentro do laço). Para o PDF, o custo inclui
  descomprimir as fontes embutidas, gerar o subconjunto TrueType e comprimir os fluxos — é custo fixo por chamada.
- Baseline: **Asciidoctor.js 3.0.4** (o Asciidoctor 2.0.20 transpilado para JavaScript, Node 26), HTML5 embutido,
  mesma máquina, mesmos arquivos, mediana de 21 (`bench/asciidoctorjs.js`), após o aquecimento do JIT.
  O Asciidoctor Ruby e o `asciidoc-parser` (Rust) não estavam instalados na máquina; ficam para uma rodada futura.
- Corpus (`bench/corpus/`): `medio.adoc` (1,1 KB, o documento completo do corpus de testes) e `grande.adoc`
  (58 KB: o mesmo conteúdo repetido em 60 seções, com listas, tabelas, admonições, código e rodapés).

## Resultado em 2026-10-24 (AsciiDocConverter 0.1.0)

| Arquivo | Formato | Mediana | Vazão | Asciidoctor.js 3.0.4 (HTML5) | Razão |
|---|---|---:|---:|---:|---:|
| medio.adoc (1,1 KB) | HTML5 | 0,035 ms | 30 MB/s | 2,76 ms | 79× |
| medio.adoc | Markdown | 0,032 ms | 33 MB/s | — | — |
| medio.adoc | ASG JSON | 0,051 ms | 21 MB/s | — | — |
| medio.adoc | PDF | 18,5 ms | — | — | — |
| grande.adoc (58 KB) | HTML5 | 2,51 ms | 22 MB/s | 99,3 ms | 40× |
| grande.adoc | Markdown | 1,96 ms | 28 MB/s | — | — |
| grande.adoc | ASG JSON | 3,25 ms | 17 MB/s | — | — |
| grande.adoc | PDF | 53,5 ms | 1,0 MB/s | — | — |

Leitura honesta: a comparação é com um interpretador (Ruby transpilado rodando no V8), não com outra
implementação nativa; a razão diz "ordem de grandeza", não "quantas vezes melhor que o estado da arte".
O PDF tem ~18 ms de custo fixo por documento (fontes); num serviço, vale reaproveitar nada — cada chamada é
independente por construção (IC-3), e 18 ms é o preço aceito.

## Como repetir

```
# Linux/WSL, na raiz do repositório
ADC_GOLDEN_MODULOS=<pasta node_modules com @asciidoctor/core@3.0.4> bash bench/rodar.sh 21
```
