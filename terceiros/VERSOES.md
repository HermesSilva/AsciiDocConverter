# Terceiros embutidos

Todos de arquivo único, copiados para cá e fixados; nenhum é dependência de runtime (IC-3/IC-4).

| O quê | Versão | Origem | Licença | Para quê |
|---|---|---|---|---|
| `miniz.c` / `miniz.h` | 3.1.2 | github.com/richgel999/miniz | MIT (`LICENSE-miniz.txt`) | Flate (fluxos do PDF, descompressão das fontes embutidas, PNG) |
| `stb_truetype.h` | 1.26 (commit 003501f) | github.com/nothings/stb | domínio público / MIT (`LICENSE-stb.txt`) | métricas e glifos das fontes TrueType no PDF |
| `asciidoctor.css` | a do `@asciidoctor/core` 3.0.4 (Asciidoctor 2.0.20), 29021 bytes | `dist/css/asciidoctor.css` do pacote npm | MIT (`LICENSE-asciidoctor-css.txt`) | folha de estilo embutida no HTML5 completo (`autonomo`), para o mesmo visual do Asciidoctor |

A folha é copiada byte a byte para `obj/fontes/adc_fontes_embutidas.c` por `ferramentas/gerar_fontes.py` (junto com
as fontes Liberation de `fontes/`). Para atualizá-la, substitua o arquivo e registre a versão aqui.
