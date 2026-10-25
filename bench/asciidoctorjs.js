// Mede o Asciidoctor.js 3.0.4 (núcleo Ruby 2.0.20 transpilado) no mesmo arquivo, HTML5 embutido, mediana de N.
// Uso: node asciidoctorjs.js <arquivo.adoc> <repeticoes> <pasta node_modules>
'use strict';
const fs = require('fs');
const path = require('path');
const { createRequire } = require('module');
const [,, arquivo, repStr, modulos] = process.argv;
const rep = Math.max(3, parseInt(repStr || '21', 10));
const req = createRequire(path.join(modulos, 'package.json'));
const Asciidoctor = req('@asciidoctor/core')();
const texto = fs.readFileSync(arquivo, 'utf8');
const bytes = Buffer.byteLength(texto, 'utf8');
const t = [];
let tam = 0;
for (let i = 0; i < rep; i++) {
  const t0 = process.hrtime.bigint();
  const html = Asciidoctor.convert(texto, { standalone: false, safe: 'safe' });
  t.push(Number(process.hrtime.bigint() - t0) / 1e6);
  tam = html.length;
}
t.sort((a, b) => a - b);
const med = t[Math.floor(rep / 2)];
console.log(`Asciidoctor.js ${Asciidoctor.getVersion()} HTML5 mediana=${med.toFixed(3)} ms  min=${t[0].toFixed(3)} ms  vazao=${((bytes / 1048576) / (med / 1000)).toFixed(2)} MB/s  saida=${tam} chars`);
