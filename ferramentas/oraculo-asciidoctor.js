// Oráculo do Converter-Adoc.ps1 -Oraculo: converte .adoc com o Asciidoctor.js 3.0.4 (Asciidoctor 2.0.20), o mesmo
// oráculo do golden (test/golden/gerar.js), para comparar com a biblioteca nativa.
//
// Processo persistente: lê do stdin uma linha JSON por documento, {"arquivo": "...", "raiz": "..."}, e escreve no
// stdout uma linha JSON, {"arquivo": "...", "html": "..."} (fragmento, sem <html>) ou {"arquivo": "...", "erro": "..."}.
// A primeira linha escrita é {"pronto": "<versão>"}. Os avisos do Asciidoctor vão para um MemoryLogger (não poluem o
// protocolo).
//
// Uso: node oraculo-asciidoctor.js <pasta node_modules com @asciidoctor/core@3.0.4>
// O pacote NÃO fica no repositório: instale fora (ex.: d:\tmp\AsciiDocConverter\golden-tool) e aponte a pasta.
'use strict';
const fs = require('fs');
const path = require('path');
const readline = require('readline');
const { createRequire } = require('module');

const modulos = process.argv[2];
if (!modulos) {
  process.stderr.write('oraculo-asciidoctor.js: informe a pasta node_modules que contém @asciidoctor/core@3.0.4.\n');
  process.exit(2);
}
let Asciidoctor;
try {
  Asciidoctor = createRequire(path.join(modulos, 'package.json'))('@asciidoctor/core')();
} catch (e) {
  process.stderr.write('oraculo-asciidoctor.js: não consegui carregar @asciidoctor/core de ' + modulos + ': ' + e.message + '\n');
  process.exit(2);
}
const logger = Asciidoctor.MemoryLogger.create();
Asciidoctor.LoggerManager.setLogger(logger);

// os mesmos padrões pt-BR da biblioteca (iguais aos de test/golden/gerar.js), para comparar texto e não idioma
const atributos = {
  'note-caption': 'Nota', 'tip-caption': 'Dica', 'important-caption': 'Importante', 'warning-caption': 'Atenção',
  'caution-caption': 'Cuidado', 'example-caption': 'Exemplo', 'figure-caption': 'Figura', 'table-caption': 'Tabela',
  'toc-title': 'Sumário', 'untitled-label': 'Sem título', 'version-label': 'Versão', 'appendix-caption': 'Apêndice',
  'last-update-label': 'Última atualização', 'lang': 'pt-BR',
};
// definidos em modo fraco (sufixo '@'): o documento pode sobrescrevê-los (ex.: :toc-title: Navegação), como na
// biblioteca, onde esses padrões também cedem ao que o documento define
for (const k of Object.keys(atributos)) atributos[k] = atributos[k] + '@';

process.stdout.write(JSON.stringify({ pronto: Asciidoctor.getVersion() + ' (core ' + Asciidoctor.getCoreVersion() + ')' }) + '\n');

const rl = readline.createInterface({ input: process.stdin, crlfDelay: Infinity });
rl.on('line', (linha) => {
  let arquivo = '';
  try {
    const pedido = JSON.parse(linha);
    arquivo = pedido.arquivo;
    // Sem base_dir: o leitor do documento resolve include:: a partir do base_dir, que por padrão é a pasta do
    // próprio arquivo (a semântica correta). safe: 'unsafe' porque no modo safe o base_dir vira limite e os
    // includes com ../ (comuns: ../../_attributes/...) são "recuperados" para dentro dele, com o aviso "illegal
    // reference to ancestor of jail" — o arquivo incluído muda. É ferramenta local, sobre arquivos do usuário.
    const html = Asciidoctor.convertFile(arquivo.replace(/\\/g, '/'), {
      to_file: false, standalone: false, safe: 'unsafe', attributes: atributos,
    });
    process.stdout.write(JSON.stringify({ arquivo, html: String(html) }) + '\n');
  } catch (e) {
    process.stdout.write(JSON.stringify({ arquivo, erro: String(e && e.message ? e.message : e) }) + '\n');
  }
  logger.clear && logger.clear();
});
rl.on('close', () => process.exit(0));
