# Gera obj/fontes/adc_fontes_embutidas.c com os recursos embutidos no binario:
#   - as 12 fontes Liberation comprimidas (zlib) como arrays C (adc_fontes.h);
#   - a folha de estilo padrao do Asciidoctor (terceiros/asciidoctor.css) como array C (adc_recursos.h).
# Roda em qualquer Python 3 (Windows ou Linux); so usa a biblioteca padrao. So-ASCII por regra da casa.
# Uso: python -I ferramentas/gerar_fontes.py [saida.c]
import io, os, sys, zlib

RAIZ = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FONTES = os.path.join(RAIZ, 'fontes')
CSS = os.path.join(RAIZ, 'terceiros', 'asciidoctor.css')
SAIDA = sys.argv[1] if len(sys.argv) > 1 else os.path.join(RAIZ, 'obj', 'fontes', 'adc_fontes_embutidas.c')
NOMES = [
    ('LiberationSerif-Regular', 'serif_regular'), ('LiberationSerif-Bold', 'serif_negrito'),
    ('LiberationSerif-Italic', 'serif_italico'), ('LiberationSerif-BoldItalic', 'serif_negrito_italico'),
    ('LiberationSans-Regular', 'sans_regular'), ('LiberationSans-Bold', 'sans_negrito'),
    ('LiberationSans-Italic', 'sans_italico'), ('LiberationSans-BoldItalic', 'sans_negrito_italico'),
    ('LiberationMono-Regular', 'mono_regular'), ('LiberationMono-Bold', 'mono_negrito'),
    ('LiberationMono-Italic', 'mono_italico'), ('LiberationMono-BoldItalic', 'mono_negrito_italico'),
]

def array_c(dados):
    linhas = []
    for i in range(0, len(dados), 24):
        linhas.append(','.join(str(b) for b in dados[i:i + 24]) + ',\n')
    return ''.join(linhas)

def main():
    os.makedirs(os.path.dirname(SAIDA), exist_ok=True)
    partes = ['/* GERADO por ferramentas/gerar_fontes.py - nao editar. Fontes Liberation 2.1.5 (SIL OFL 1.1), zlib;\n'
              ' * folha de estilo padrao do Asciidoctor (MIT), texto UTF-8. */\n',
              '#include <stdint.h>\n#include "adc_fontes.h"\n#include "adc_recursos.h"\n\n']
    tabela = []
    total_cru = 0
    total_comp = 0
    for arquivo, ident in NOMES:
        caminho = os.path.join(FONTES, arquivo + '.ttf')
        with open(caminho, 'rb') as f:
            cru = f.read()
        comp = zlib.compress(cru, 9)
        total_cru += len(cru)
        total_comp += len(comp)
        partes.append('static const uint8_t dados_%s[%d] = {\n' % (ident, len(comp)))
        partes.append(array_c(comp))
        partes.append('};\n')
        tabela.append('    { "%s", "%s", dados_%s, %d, %d },\n' % (ident, arquivo, ident, len(comp), len(cru)))
    partes.append('\nconst ADC_FonteEmbutida adc_fontes_embutidas[] = {\n')
    partes.extend(tabela)
    partes.append('};\nconst int32_t adc_fontes_embutidas_quantidade = %d;\n' % len(NOMES))

    with open(CSS, 'rb') as f:
        css = f.read()
    if css.startswith(b'\xef\xbb\xbf'):
        raise SystemExit('%s: a folha de estilo tem BOM; grave-a em UTF-8 sem BOM.' % CSS)
    css = css.replace(b'\r\n', b'\n')
    partes.append('\nconst uint8_t adc_css_asciidoctor[%d] = {\n' % len(css))
    partes.append(array_c(css))
    partes.append('};\nconst int32_t adc_css_asciidoctor_tamanho = %d;\n' % len(css))

    with io.open(SAIDA, 'w', encoding='ascii', newline='\n') as f:
        f.write(''.join(partes))
    print('%s: %d fontes, %d bytes TTF -> %d bytes zlib; css %d bytes' % (SAIDA, len(NOMES), total_cru, total_comp, len(css)))

if __name__ == '__main__':
    main()
