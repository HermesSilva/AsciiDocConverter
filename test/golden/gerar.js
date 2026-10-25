// Gera o HTML de referência (golden) de cada caso em test/casos/*.adoc com o Asciidoctor.js 3.0.4
// (transpilado do Asciidoctor Ruby 2.0.20) — o oráculo de saída do renderizador HTML5.
//
// Uso: node gerar.js [--modulos <pasta com node_modules>]
// O pacote @asciidoctor/core@3.0.4 NÃO fica no repositório: instale fora (ex.: d:\tmp\AsciiDocConverter\golden-tool)
// e aponte com --modulos. Sem o argumento, procura em ../../node_modules e em %ADC_GOLDEN_MODULOS%.
//
// Os atributos abaixo igualam os padrões pt-BR da biblioteca para o golden comparar texto e não idioma.
'use strict';
const fs = require('fs');
const path = require('path');
const { createRequire } = require('module');

const args = process.argv.slice(2);
let modulos = process.env.ADC_GOLDEN_MODULOS || path.join(__dirname, '..', '..', 'node_modules');
const i = args.indexOf('--modulos');
if (i >= 0) modulos = args[i + 1];
const req = createRequire(path.join(modulos, 'package.json'));
const Asciidoctor = req('@asciidoctor/core')();

const versao = Asciidoctor.getVersion() + ' (core ' + Asciidoctor.getCoreVersion() + ')';
const casos = path.join(__dirname, '..', 'casos');
const saida = __dirname;
const atributos = {
  'note-caption': 'Nota', 'tip-caption': 'Dica', 'important-caption': 'Importante', 'warning-caption': 'Atenção',
  'caution-caption': 'Cuidado', 'example-caption': 'Exemplo', 'figure-caption': 'Figura', 'table-caption': 'Tabela',
  'toc-title': 'Sumário', 'untitled-label': 'Sem título', 'version-label': 'Versão', 'appendix-caption': 'Apêndice',
  'last-update-label': 'Última atualização', 'lang': 'pt-BR',
};
let n = 0;
for (const arquivo of fs.readdirSync(casos).filter((f) => f.endsWith('.adoc')).sort()) {
  const texto = fs.readFileSync(path.join(casos, arquivo), 'utf8');
  const html = Asciidoctor.convert(texto, { standalone: false, safe: 'safe', attributes: atributos, base_dir: casos });
  fs.writeFileSync(path.join(saida, arquivo.replace(/\.adoc$/, '.html')), html + '\n', 'utf8');
  n++;
}
fs.writeFileSync(path.join(saida, 'VERSAO.txt'), 'Golden gerado por Asciidoctor.js ' + versao + ' em ' + new Date().toISOString().slice(0, 10) + '\n', 'utf8');
console.log(n + ' casos → golden (' + versao + ')');
