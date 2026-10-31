# AsciiDocConverter

Biblioteca nativa em **C11, sem dependências de runtime**, que converte **AsciiDoc** em **HTML5**, **Markdown
(GFM)**, **PDF** e **ASG JSON** — numa chamada só: bytes AsciiDoc entram, o formato é parâmetro, bytes saem
(IC-1: nenhum renderizador passa por outro formato). Windows x64 e Linux x64/arm64, consumida por C# via
P/Invoke pelo helper `XAsciiDocConverter` do **Tootega Core**.

Parser próprio (decisão do dono, DAD-1) escrito contra a **especificação AsciiDoc Language** (Eclipse) e o
comportamento observado do **Asciidoctor 2.0.x**; escritor PDF próprio (DAD-7), com as fontes **Liberation 2.1.5**
embutidas. Terceiros, só de arquivo único: `miniz` (Flate), `stb_truetype` (métricas) e a folha de estilo padrão do
Asciidoctor (`terceiros/asciidoctor.css`, MIT), embutida no HTML5 completo (`"autonomo": true`) para o arquivo
abrir com o mesmo visual do Asciidoctor — `terceiros/VERSOES.md` fixa as versões. Plano, decisões e invariantes:
**`PLANEJAMENTO.md`**.

## Contrato (`include/adc.h`)

```c
int32_t ADC_Converter(const uint8_t* entradaUTF8, int32_t tamanho, int32_t formato /* 1 HTML5, 2 Markdown, 3 PDF, 4 ASG */,
                      const char* opcoesJSON, ADC_ResolverRecurso resolver, void* contexto,
                      uint8_t** saida, int32_t* tamanhoSaida, char** mensagensJSON);
void ADC_Liberar(void* memoria);
const char* ADC_Versao(void);
```

Falha devolve um código e **sempre** as mensagens (JSON: nível, linha, coluna, texto) — também no sucesso,
porque aviso do processador não se perde. **O texto sai em inglês; com `"idioma": "pt-BR"` nas opções, em
português** (DAD-10, 2026-10-31 — o helper C# do Tootega Core manda o idioma sempre). A biblioteca **nunca lê
disco**: `include::` e imagens chegam pelo callback do chamador; sem callback, tudo é negado com aviso (modo seguro).

**A ABI acima está congelada** (DAD-9): o que nasceu depois — resolvedor com origem (include dentro de include),
mensagens com o arquivo de origem, DocBook 5, imagens em `data:` e folha de estilo própria — só é alcançável pela
**entrada WebAssembly** (`wasm/adc_wasm.c`, `Dist/AsciiDocConverter.wasm`), que a extensão
[Tootega AsciiDoc](https://github.com/HermesSilva/TootegaAsciiDoc) do VS Code consome. Opções novas, todas opt-in:
`idioma`, `dataUri`, `folhaDeEstilo`, `pdf.dataDeCriacao` (ver `include/adc.h`).

## Como compilar

| O quê | Comando |
|---|---|
| Distribuição (`Dist/`, versionada) | `.\Build-AsciiDocConverter.ps1` — Windows pelo MSBuild do Visual Studio; Linux x64+arm64 num contêiner Debian (Docker). Num Linux/WSL sem Docker: `RAIZ=$(pwd) ARQUITETURAS=linux-x64 bash build-linux.sh` |
| Ciclo de desenvolvimento | `.\Compilar-Dev.ps1` (MSVC, `/W4 /WX`) ou `bash compilar-dev.sh` (gcc, ASan/UBSan) → `obj/dev*/adc_cli` e `adc_tck` |
| WebAssembly (`Dist/AsciiDocConverter.wasm`) | `bash wasm/build-wasm.sh` num Linux/WSL com o [wasi-sdk](https://github.com/WebAssembly/wasi-sdk/releases) em `~/wasi-sdk` (ou `WASI_SDK_PATH`); do Windows, `.\Build-AsciiDocConverter.ps1 -Alvo Wasm` chama o WSL. Mesmo fonte C; nada de Docker |
| Fontes embutidas | geradas no build por `ferramentas/gerar_fontes.py` (Python 3, só biblioteca padrão) a partir de `fontes/*.ttf` |

## Como converter um arquivo pela linha de comando

`ferramentas/Converter-Adoc.ps1 <arquivo.adoc|pasta> [-Recursivo Links|Pastas] [-Formato html,md,pdf,asg] [-Saida <dir>]`
(PowerShell 7, Windows ou Linux) carrega a nativa de `Dist/` e gera `<nome>.html` (documento completo), `<nome>.md`,
`<nome>.pdf` e `<nome>.asg.json` (ou só os formatos pedidos em `-Formato`) ao lado de cada `.adoc`, ou sob `-Saida`
reproduzindo as pastas. Formato que falha não derruba os demais e o script sai com código 1.

- **Sem `-Recursivo`**: só o arquivo de entrada, com toda mensagem do processador (linha e coluna).
- **`-Recursivo Links`**: parte da entrada e segue os links para outros documentos (`xref:`, `<<doc#id>>`, `link:` para
  `.adoc`), lidos do ASG que a própria nativa gera; cada documento uma vez, link quebrado listado no resumo;
  `-ProfundidadeMaxima n` limita os saltos.
- **`-Recursivo Pastas`**: todo `.adoc` da árvore; nomes iniciados por `_` (parciais) ficam de fora sem `-IncluirParciais`.
- `-MostrarAvisos` lista cada aviso, link quebrado e recurso negado; sem ele, só contagens (erros aparecem sempre).
- **`-Log <arquivo.csv>`** grava todo problema, um por linha (`Documento;Categoria;Formato;Linha;Coluna;Mensagem`,
  `;` e UTF-8 com BOM para o Excel).
- **`-Validar`** não grava as conversões e reprova o documento com `FALHA` (código de erro da nativa), `ERRO`
  (mensagem de erro do processador) ou `TRUNCADO`: conteúdo que não chegou à saída sem mensagem nenhuma, ou seja,
  linha de conteúdo depois da última que o ASG cobre, ou include com conteúdo ausente do ASG. Linhas sob
  `ifdef`/`ifndef`, comentários, atributos e delimitadores não contam.
- **`-Oraculo <node_modules>`** (implica `-Validar`) compara o texto do HTML de cada documento com o do Asciidoctor.js
  3.0.4, o mesmo oráculo do golden, por `ferramentas/oraculo-asciidoctor.js`; diferença reprova como `DIVERGE`
  ou `VAZIO`. Exige Node no PATH.
- Limites conhecidos da validação: conteúdo sob `ifdef`/`ifndef`/`ifeval` só é cobrado pelo oráculo, porque o
  script não sabe quais atributos estarão definidos; e o oráculo compara o corpo em modo fragmento, então o título
  do documento só é cobrado pela verificação de truncamento.
- Rodada de referência em 2026-10-24 sobre a árvore do OpenShift, com 15.865 `.adoc`, HTML e oráculo: cerca de 6 min;
  reprovados caíram de 15.643 para 547 (3,4%) com as correções listadas em `docs/fidelidade.md`; o corpus
  `test/casos` passa, salvo o caso 15, que tem de propósito uma seção de nível 0 (erro também no Asciidoctor).

O script resolve `include::` relativo ao arquivo que contém a diretiva, como o Asciidoctor, buscando primeiro a partir
das pastas dos includes já entregues. Isso é necessário porque a nativa entrega ao callback o alvo como escrito, sem
dizer de qual arquivo veio. Para testar a compilação de desenvolvimento, aponte
`-Biblioteca obj\dev\AsciiDocConverter.dll`.

## Como testar

- **TCK oficial** (parse, ASG): `node harness/bin/asciidoc-tck.js cli --adapter-command <obj/dev/adc_tck.exe>` no
  checkout de `gitlab.eclipse.org/eclipse/asciidoc-lang/asciidoc-tck` — 13/13 em 2026-10-24.
- **Golden HTML** (conversão): `node test/golden/gerar.js --modulos <node_modules com @asciidoctor/core@3.0.4>`
  regenera `test/golden/*.html`; `bash test/comparar-golden.sh` compara byte a byte — 23/23 (os casos 14 a 23 vêm da validação em corpus real).
- **Fuzzing**: `test/fuzz/fuzz_converter.c` (clang, libFuzzer+ASan+UBSan); 3 min sem achado em 2026-10-24.
- **Benchmark**: `bash bench/rodar.sh` — `docs/benchmark.md` tem o método e os números.
- **C#**: `TestesXAsciiDocConverter` em `TootegaCore/Back/Core/Tootega.Core.Test.Unit`.

- **HTML completo** (`autonomo`): conferido byte a byte com o Asciidoctor em `standalone: true` sobre o corpus e
  documentos extras (`toc` lateral, `linkcss`, `nofooter`, `sectanchors`...); ver `docs/fidelidade.md`.

Divergências deliberadas e o que não está implementado: **`docs/fidelidade.md`**.

## Uso em C#

```csharp
String html = XAsciiDocConverter.ParaHTML(texto);
Byte[] pdf  = XAsciiDocConverter.ParaPDF(texto, "{\"pdf\":{\"pagina\":\"A4\"}}", (tipo, caminho) => LerDoRepositorio(caminho));
var r = XAsciiDocConverter.Converter(texto, XAsciiDocConverter.Formato.Markdown);
foreach (var m in r.Mensagens) Log(m);   // avisos nunca se perdem
```

O Tootega.Core importa `AsciiDocConverter.Native.props` quando este repositório está ao lado do `TootegaCore`
(raiz do SaaS), copiando `Dist/` para a saída; a ausência da nativa é acusada no boot por
`XAsciiDocConverter.GarantirBibliotecaNativa()`.
