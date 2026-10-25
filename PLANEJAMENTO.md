# AsciiDocConverter — planejamento: pesquisa e definições

> **Estado em 2026-10-24 (fim do dia): implementado** — o dono aprovou ("Pode prosseguir e implementar") e as
> etapas E0–E6 de §5.7 foram executadas no mesmo dia. Fatos verificados: TCK oficial **13/13**; golden HTML do
> Asciidoctor 2.0.20 **13/13** byte a byte; PDF, Markdown e ASG do corpus inteiro sob ASan/UBSan sem achado;
> fuzzing 3 min sem achado; benchmark com método em `docs/benchmark.md`; `Dist/` com `win-x64` e `linux-x64`
> (o `linux-arm64` exige o contêiner Docker, cujo daemon estava parado — pendência de build, não de código);
> helper `XAsciiDocConverter` no Tootega.Core com 6 testes. **DAD-3, DAD-4, DAD-6 e DAD-8 foram adotadas pela
> recomendação deste plano como padrão reversível** (GFM + HTML embutido com aviso; modo seguro com callback
> único de recursos; `.vcxproj` + `build-linux.sh`; tema padrão + sobrescrita por JSON). O que ficou de fora
> está em `docs/fidelidade.md` ("Não implementado nesta versão"). A **licença deste repositório** continua
> decisão do dono (sem arquivo LICENSE até ele decidir).

## 0. O pedido (dono, 2026-10-24)

Uma **biblioteca de ligação dinâmica em C**, compatível com **Windows e Linux**, consumida por **C#** via
P/Invoke, pertencendo à **plataforma Tootega Core** (vale para todos os SaaS — e, pelo §0 do `CLAUDE.md` do Core,
nada nela conhece SaaS algum), para **converter documentos AsciiDoc em PDF, Markdown e HTML**.

**Onde mora (observado em 2026-10-24, no mesmo dia do pedido):** o pedido inicial dizia `TootegaCore/Tools`; em
seguida o dono mandou os docs para **`AsciiDocConverter/`**, na raiz do ANVIL ao lado de `TootegaCore/`, e essa
pasta é um **repositório git próprio** (`origin` = `github.com/HermesSilva/AsciiDocConverter.git`, branch
`master`, ainda sem commit). Decorrência: o projeto nasce em repositório separado; os nomes foram fixados na
**DAD-5** (nativa `AsciiDocConverter`, helper `XAsciiDocConverter` no `Tootega.Core`), e **como o Core alcança
o `Dist/`** deste repositório (checkout ao lado, como o SaaS faz com o Core? cópia dos binários? submódulo?) fica
para a etapa E6, sem bloquear nada antes. A pasta `TootegaCore/Tools/` continua vazia. O `.gitignore` do ANVIL ganhou `/AsciiDocConverter/`,
pelo mesmo motivo que já ignora `/TootegaCore/` (repositório próprio, histórico próprio).

Este arquivo registra o que foi **apurado com fato** (fontes em §7, consultadas em 2026-10-24), os caminhos
possíveis com seus custos, as decisões que são do dono e a recomendação do assistente. Divergência entre doc e
serviço observada fica escrita com data; onde não há fato, está escrito "não apurado".

## 1. O precedente da casa: `Tootega.LZMA`

A plataforma já tem **uma** biblioteca nativa consumida por P/Invoke, e ela fixa o padrão (lido em
`Back/Core/Tootega.LZMA/` e `Back/Core/Tootega.Core/XLZMA.cs`):

| Aspecto | Como a `Tootega.LZMA` faz | Decorrência para este projeto |
|---|---|---|
| Fonte | C (SDK LZMA) + C++ (`LzmaCodec.cpp`), `extern "C"`, `__declspec(dllexport)` só ao compilar a DLL | mesmo contrato de cabeçalho: exporta ao compilar, importa ao consumir |
| Build Windows | `Tootega.LZMA.vcxproj` + **MSBuild.exe do Visual Studio** (o `dotnet` CLI não carrega os alvos C++; sob MSBuild Core o projeto vira no-op) | `.vcxproj` que **não** quebra o `dotnet build`; se o Core o listar na `TootegaCore.slnx` (pasta `/Native/`), isso exige o repositório deste projeto ao lado do Core — a decidir em E6 |
| Build Linux | `Build-TootegaLZMA.ps1` sobe um contêiner `debian:bookworm-slim` e roda `build-linux.sh` (gcc + `crossbuild-essential-arm64`), `-Wl,--no-undefined`, `strip`, e **confere por `nm` que cada símbolo exportado existe** | x64 **e** arm64; símbolo ausente é erro de build, nunca falha muda em produção |
| Distribuição | binários **versionados** em `Dist/`, um por alvo: `Tootega.LZMA.win-x64.dll`, `.linux-x64.so`, `.linux-arm64.so`; `.gitignore` abre exceção explícita (a regra `dist/` casa com `Dist/` no Windows) | mesmo esquema: `Dist/` versionado, exceção no `.gitignore` |
| Entrega ao host | `Tootega.LZMA.Native.props`: `Content` curinga sobre `Dist/`, copiado à saída do build e da publicação; pasta vazia não quebra o build — a ausência é detectada no boot | um `.props` igual, importado pelo host do SaaS |
| Carga em C# | `XLZMA`: `NativeLibrary.SetDllImportResolver` escolhe o arquivo pelo SO e arquitetura; `GarantirBibliotecaNativa()` falha rápido no boot com mensagem que diz **o que** falta e **como gerar** | invólucro `X<Nome>` em `Tootega.Core`, mesma mecânica |
| Memória | a nativa aloca a saída; o C# copia e chama `FreeBuff` | a nativa aloca, o C# libera pela função da nativa |
| Armadilha registrada | `EntryPoint` explícito quando o nome gerenciado (CE-8, sigla maiúscula) difere do símbolo exportado — sem ele o P/Invoke falhava **calado** | nomear os símbolos já em CE-8 (`ADC_...`) para não precisar de `EntryPoint` |

**O que o precedente não cobre:** a `Tootega.LZMA` é uma casca fina sobre um SDK pronto. Este projeto é um
**parser inteiro e um escritor PDF inteiro** escritos pela casa (DAD-1, DAD-7): o padrão de build, distribuição
e carga **se herda**; o conteúdo é ordens de grandeza maior, e por isso tem etapa de estudo (E0) antes da
primeira linha.

## 2. Fatos apurados (2026-10-24)

### 2.1 Não existe parser de AsciiDoc em C

Busca dirigida (§7, F1) não encontrou **nenhuma** biblioteca C que faça o parse completo de AsciiDoc. O único C
que aparece é o `parsers/asciidoc.c` do Universal Ctags, que extrai títulos de seção para gerar *tags* — não é
um parser. O que existe está em Ruby (a referência), JavaScript (porte), Java (AsciidoctorJ, sobre JRuby),
Go (`libasciidoc`, `asciidoctor-go`, ambos parciais), Haskell (`asciidoc-hs`) e Rust (§2.3).

**Consequência binária:** "biblioteca em C" só é possível por um de três meios — (a) C que **embute** um motor
de outra linguagem, (b) C como **casca** (ABI) sobre um núcleo compilado de outra linguagem, ou (c) parser
**escrito do zero** em C. A escolha é a **DAD-1**.

### 2.2 A implementação de referência e os seus portes

- **Asciidoctor (Ruby, MIT)** é a referência de fato. Roda em CRuby 2.7–3.4, JRuby e TruffleRuby; **mruby não
  está na lista** e nenhuma fonte relata Asciidoctor sobre mruby (F2). Conversores **embutidos**: `html5`,
  `xhtml5`, `docbook5`, `manpage`. **PDF é gem separada** (`asciidoctor-pdf`, sobre Prawn, Ruby) e **Markdown
  não existe** como conversor oficial (F3, F4).
- **Asciidoctor.js (MIT)** tem **duas linhas incompatíveis entre si** (F5, F6, F7, F8):
  - **3.0.4** (publicada em 2024-02-12; última da linha): **transpilada do Ruby por Opal**; a documentação diz
    que "pode rodar em qualquer plataforma JavaScript, incluindo Node.js, GraalVM e o navegador" e distribui
    um build para **motor ECMAScript puro** (`dist/graalvm/asciidoctor.js`, marcado *experimental*; sem
    `require`/`fs`). Nenhuma fonte relata execução em **QuickJS**; a tentativa histórica em Nashorn (2014)
    esbarrou em E/S de arquivo e numa variável global de *timings* — o ponto de extensão para E/S é o
    `file.rb` do Opal. **Linha congelada**: 4.x a substituiu.
  - **4.x** (4.0.0 em 2026-06-22; 4.1.1 em 2026-10-05, `latest`): **reescrita em JavaScript moderno (ESM),
    não mais Opal**, "gerada e mantida com ajuda do Claude Code" (texto da própria documentação). `engines`
    no registro: `node >= 20`; o README do repositório diz "Node.js ≥ 24" e a documentação "22 (LTS) ou
    superior" — **três números divergentes, registrado como divergência**. Tem build de navegador
    (`build/browser/index.js`), portanto existe um artefato sem dependência de Node, **não testado fora de
    navegador e Node**. Fidelidade ao Ruby e maturidade: não apuradas (reescrita de meses).
- **AsciidoctorJ** exige JVM; fora do escopo de uma `.dll`/`.so` em C.

### 2.3 Parsers em Rust (2026)

| Projeto | Licença | Estado declarado (fonte) | Saídas | Observações |
|---|---|---|---|---|
| `asciidoc-parser` (scouten / asciidoc-rs) | MIT ou Apache-2.0 | "a partir da 0.25.0 (julho 2026) feature-complete, heavily tested, ready to be used"; testado contra a definição da linguagem **e a suíte do Asciidoctor**; ~99,5% de cobertura (F9) | **HTML5** embutido; outros backends pelo *trait* `InlineRenderer`, "não suportados diretamente" | não suporta: UTF-16, doctype ≠ `article` (sem `book`), `compat-mode`, títulos setext, `{set:…}`, include por URL, menu `"File > Save"`; **extensões fora da 1.0**; a saída HTML **não é sanitizada**; sem FFI C |
| `asciidork` | MIT ou Apache-2.0 | "**not complete**: unfinished areas, missing error handling, edge cases" (STEM, realce de código) (F10) | HTML (CLI, WASM) | sem releases; sem FFI C |
| `acdc` (nlopes) | MIT | passa o **TCK** (que o autor chama de "pouco abrangente"); 1.500+ testes próprios; "there's lots to be done" (post de 2026-05-24) (F11, F12) | HTML, HTML5 semântico, **Markdown** (CommonMark/GFM, "com degradação onde Markdown não expressa AsciiDoc"), manpage, terminal, **PDF** (listado no README; implementação **não apurada**) | sem releases; AST reestruturada para o **ASG** da especificação; sem FFI C |

Rust gera biblioteca dinâmica com ABI C (`crate-type = cdylib`, `extern "C"`), e os três alvos da casa —
`x86_64-pc-windows-msvc`, `x86_64-unknown-linux-gnu`, `aarch64-unknown-linux-gnu` — são **Tier 1 com host
tools** ("guaranteed to work") (F13). Cross-compilar o arm64 no contêiner Debian exige o *toolchain* Rust nele
(ou `cargo-zigbuild`, que só cobre alvos `gnu` e permite fixar a glibc). **Custo não técnico:** uma **segunda
linguagem compilada na casa**, com toolchain próprio no build e no CI.

### 2.4 A especificação Eclipse e o TCK

O **AsciiDoc Language** (Eclipse Foundation) mantém a especificação — ainda **rascunho** — e o **TCK**: o
implementador fornece um *adapter* que lê AsciiDoc por `stdin` e escreve o **ASG** (*Abstract Semantic Graph*,
JSON com esquema) em `stdout`; o TCK compara com o esperado. O TCK **cobre parse e parte do processamento; não
valida conversão** para HTML ou DocBook. **Sem releases**: compila-se do fonte com Node ≥ 20 (F14, F15).
Decorrência: escrever um parser próprio hoje é mirar um alvo que ainda se move, e a conformidade de **saída**
não teria juiz externo.

### 2.5 Markdown

Nenhuma ferramenta nativa. O que existe (F16): **downdoc** (JS puro, sem dependências, **não cobre a sintaxe
inteira** — "funciona melhor em documentos escritos pensando na conversão"); **pandoc** via DocBook (Haskell,
processo externo — fora de uma biblioteca); o conversor Markdown do **acdc** (Rust); ou um **conversor próprio
sobre a AST do Asciidoctor** — a API de conversor customizado do Asciidoctor.js recebe cada nó
(`convert(node, transform)`, `getNodeName()`, `getContent()`) e **devolve qualquer string** (F17). Markdown é
**menos expressivo** que AsciiDoc (admonições, callouts, tabelas com células mescladas, notas de rodapé,
`include`): toda conversão **degrada**, e a política de degradação é decisão de produto (**DAD-3**).

### 2.6 PDF em C

- **libharu 2.4.6** (2026-03-26; licença zlib; ANSI C; CMake; depende de zlib e libpng): gera PDF por
  primitivas — texto, linhas, imagens, *outlines*, criptografia. **Não tem motor de leiaute** (quebra de
  parágrafo, tabela, lista, quebra de página são de quem chama). UTF-8 **só com fonte TrueType embutida e só
  até sequências de 3 bytes** (cobre o pt-BR inteiro; não cobre emoji) (F18, F19).
- **PDFGen** (Unlicense, arquivo único): "rudimentary PDF output"; sem menção a Unicode nem leiaute (F20).
- **litehtml** (C++, BSD-3): posiciona HTML/CSS e **delega o desenho** a um `document_container` que o
  consumidor implementa (sobre libharu ou cairo); "não totalmente compatível com os padrões HTML/CSS"; é C++
  (F21).
- **cairo + pango**: não apurado em profundidade; cadeia de dependências pesada no Windows (fontconfig,
  freetype, harfbuzz) — anotado, não medido.
- **Não existe** porte do `asciidoctor-pdf` fora do Ruby (F4).

**Consequência (avaliação anterior às DAD-2 e DAD-7):** PDF **dentro** da nativa significa **escrever um motor
de leiaute de documento** (parágrafos, listas, tabelas, imagens, cabeçalho/rodapé, sumário, quebras), com fonte
TrueType de licença livre embutida — obra de meses, que o assistente classificou como "construir commodity".
**O dono decidiu construir, inclusive o escritor** (§2.9, IC-4); a avaliação fica como registro do custo.

### 2.7 PDF em C# — o que a casa já tem

`Tootega.Core` já referencia **iText 9.8.0** (`Tootega.Core.Impressao`: `XDocumentoPDF`, `XRelatorioPDF`,
blocos), e a skill `tootega-relatorios` fixa que **só o `XDocumentoPDF` conhece o iText**. O complemento
**pdfHTML 6.3.4** (NuGet `itext.pdfhtml`, 2026-09-30, depende de `itext >= 9.8.0`) converte **HTML + CSS em
PDF** no mesmo regime de licença do iText Core já adotado (**AGPL ou comercial** — qual deles a casa usa **não
está escrito** em nenhum documento lido) (F22, F23). Era o caminho de **menor esforço** para o PDF — e foi
**descartado pela DAD-2**, porque passa por HTML (IC-1). O `XDocumentoPDF` não participa deste projeto.

### 2.9 Peças para o PDF dentro da nativa (apurado em 2026-10-24, após a DAD-2)

| Peça | Licença | Fato | Uso possível |
|---|---|---|---|
| **asciidoctor-pdf** (Ruby, sobre Prawn + prawn-table + prawn-svg) | MIT | gera PDF **direto do documento, sem formato intermediário** (o README cita pular DocBook, FO e LaTeX); aparência por **tema YAML**; "certas limitações do PDF e do Prawn" não listadas na página (F32) | a **referência de como AsciiDoc fica em PDF**: modelo de leiaute e chaves de tema a estudar (DAD-8); comparação visual por caso (E4) |
| **libharu 2.4.6** | zlib | ANSI C, CMake; fontes TrueType embutidas com **subconjunto**; UTF-8 **≤ 3 bytes, só TrueType**; imagens PNG/JPEG; criptografia; depende de **zlib e libpng** (sem eles, compressão e PNG ficam indisponíveis; opção de build para dispensá-los **não confirmada** — F18, F19, F33) | camada de primitivas pronta; o leiaute continua sendo da casa |
| **miniz** | MIT | zlib/Deflate (RFC 1950/1951) em **par único `miniz.c`/`miniz.h`**, testado em GCC, clang e Visual Studio; escreve PNG; lê/escreve ZIP (F34) | Flate do PDF num escritor próprio; substituto embutível do zlib para o libharu |
| **stb_truetype.h** | domínio público (cabeçalho; alternativa MIT no fim do arquivo, não lida) | lê TrueType: `cmap`, métricas, avanços, *kerning*, rasterização; **não escreve nem subconjunta** fonte; "**NO SECURITY GUARANTEE** — não usar em fonte não confiável" (sem checagem de limites) (F35) | métricas e `cmap` num escritor próprio — aceitável porque **a fonte é a da casa, embutida**, nunca a do usuário; o subconjunto seria obra própria |
| **Liberation Fonts** (métricas de Arial, Times New Roman e Courier New) | SIL OFL 1.1 | sans, serif e mono com cobertura latina completa; a OFL permite embutir em documentos e distribuir junto de software (texto da licença a anexar — F36) | a família embutida no binário (DAD-8) |
| ISO 32000 (PDF 1.7 / 2.0) | — | disponibilidade gratuita pela PDF Association **não apurada** (a página devolveu 403 no fetch) | referência normativa do escritor; o PDF 1.7 (Adobe) é público há anos |

### 2.8 Descartados, com motivo

| Caminho | Motivo do descarte |
|---|---|
| mruby + Asciidoctor Ruby embutido em C | mruby não é *runtime* suportado; nenhuma evidência de funcionamento (F2) |
| Processo *sidecar* (`asciidoctor` Ruby ou `node`) | não é biblioteca de ligação dinâmica; exige runtime instalado no host e no contêiner |
| downdoc como motor de Markdown | cobertura parcial declarada pelo próprio autor (F16) |
| pandoc | Haskell, processo externo, não embutível |
| Parser próprio em C | descartado pelo assistente por D28/D30 (integrar commodity), especificação em rascunho e TCK sem juiz de saída (§2.4), meses de trabalho — **superado pela DAD-1**: o dono decidiu construir (§3-C, §5) |

## 3. Caminhos possíveis

> **DAD-1 resolvida (dono, 2026-10-24): caminho C.** A e B ficam registrados como foram apresentados — são o
> que foi estudado e o que vira **oráculo** do caminho C (§5.1).

### A. C hospedando a referência: QuickJS-ng + Asciidoctor.js (estudado; não adotado)

Biblioteca **em C puro**: embute o motor **QuickJS-ng** (C, MIT, v0.17.0 em 2026-09-18, CMake/Make/Meson,
"mira a especificação ECMAScript mais recente", artefatos de release para Windows, Linux aarch64 e macOS arm64)
(F24) e, como recurso, o *bundle* do **Asciidoctor.js**. HTML5 e DocBook saem **prontos e fiéis à referência**;
Markdown sai de um **conversor próprio em JavaScript** sobre a AST (§2.5), escrito pela casa.

- **A favor:** fidelidade à referência (é o Asciidoctor rodando); uma linguagem só na casa (C); MIT em tudo;
  builds Windows e Linux pelo esquema da `Tootega.LZMA`; o conversor Markdown vê a AST inteira.
- **Contra / a provar:** nenhum relato de Asciidoctor.js em QuickJS — **precisa de protótipo** com **as duas
  linhas** (3.0.4 Opal, build GraalVM; 4.1.x ESM, build de navegador) sobre um corpus; a 3.0.4 está
  congelada (sem correções futuras) e a 4.x é reescrita recente; desempenho de interpretador sem JIT sobre um
  *bundle* grande (medir: inicialização, documento típico, documento de 1 MB); `JSRuntime` do QuickJS é
  **mono-thread** → um runtime por chamada ou um *pool*, decisão de projeto; o *bundle* embutido pode ser
  comprimido com o **mesmo SDK LZMA** já no repositório e pré-compilado em bytecode (`qjsc`) — otimizações,
  não requisitos.

### B. Casca C sobre núcleo Rust (estudado; não adotado)

`cdylib` em Rust (`asciidoc-parser` para HTML, ou `acdc` para HTML+Markdown+PDF) expondo ABI C; a "biblioteca
em C" é o cabeçalho e as funções exportadas.

- **A favor:** velocidade nativa; projetos ativos em 2026; `acdc` já tem Markdown (e lista PDF).
- **Contra:** **não é C** — é Rust com ABI C, e traz uma segunda linguagem compilada e seu toolchain ao build
  e ao CI; nenhum dos três é a referência (divergências de comportamento são certas e o TCK não julga saída);
  `asciidoc-parser` não planeja extensões na 1.0 e não suporta `book`; `acdc` e `asciidork` sem releases;
  o PDF do `acdc` tem implementação não apurada.

### C. Parser próprio em C — **ADOTADO (DAD-1, dono, 2026-10-24)**

O assistente o havia descartado em §2.8 pela tensão com D28/D30; o dono reafirmou — "pegar o melhor que existe,
estudar e criar uma versão com alta performance em C" — e a decisão é dele. O diferencial que justifica
construir: **performance nativa, zero motor embutido, zero dependência externa, uma linguagem só**. O que A e B
custaram em pesquisa não se perde: a referência e os parsers Rust viram **objeto de estudo** (o que parsear, em
que ordem, com que armadilhas) e **oráculo de teste** (saída esperada), nunca dependência. Plano em §5.

### Onde nasce o PDF (ortogonal a A/B) — **DAD-2: P1, com escritor próprio (DAD-7)**

| | Onde | Esforço | Fidelidade | Observação |
|---|---|---|---|---|
| **P1** ✅ | na nativa, leiaute próprio direto da AST — sobre **escritor PDF próprio** (DAD-7; o libharu era a alternativa) | **meses** | limitada ao que se implementar | fonte TrueType livre embutida com subconjunto; **adotado** |
| P2 | na nativa, litehtml + *container* PDF | grande | média (CSS parcial) | C++; passa por HTML — descartado (IC-1) |
| P3 | em C#, HTML da nativa → **iText pdfHTML** | **pequeno** | alta | recomendado pelo assistente; passa por HTML — descartado (IC-1) |
| P4 | em C#, AST/ASG JSON da nativa → blocos do `XDocumentoPDF` | médio | limitada aos blocos da casa | segunda camada fora da nativa — descartado (IC-1) |

## 4. Decisões pendentes do dono (DAD-#)

Uma por vez; bloqueantes (⭐) primeiro. Respondida ⇒ registrada aqui com data **e** refletida neste plano no
mesmo passo.

| # | Decisão | Opções | Recomendação | Bloqueia |
|---|---|---|---|---|
| **DAD-1** ✅ | **Núcleo do parser** — **RESOLVIDA (dono, 2026-10-24): C, parser próprio.** Texto do dono: "Vamos pegar o melhor que existe, estudar e criar uma versão com alta performance em C." O assistente havia recomendado A e registrado a tensão com D28/D30; o dono reafirmou C, e a decisão é dele: o diferencial que justifica construir é **performance nativa sem motor embutido**, e a referência e os parsers Rust passam a ser **objeto de estudo e oráculo de teste**, não dependência. | A · B · **C** · rever | — | nada mais bloqueia o estudo (§5) |
| **DAD-2** ✅ | **Onde nasce o PDF** — **RESOLVIDA (dono, 2026-10-24): dentro da nativa, direto da AST (P1).** Texto do dono: "Ao chamar os métodos de conversão passa o formato de saída desejado, entra array de byte no formato AsciiDoc e sai array de byte no formato desejado; não deve fazer multiconversão, sempre de AsciiDoc para a saída." Isso fixa o **contrato** (§5.5: uma função, bytes → bytes, formato por parâmetro) e descarta P2, P3 e P4, que passam por HTML ou por outra camada. Vira a invariante **IC-1** (§4-bis). | P1 · P2 · P3 · P4 | — | abre DAD-7 e DAD-8 |
| **DAD-7** ✅ | **Camada de primitivas do PDF** — **RESOLVIDA (dono, 2026-10-24): escritor PDF próprio desde o início.** O assistente recomendara o libharu embutido; o dono escolheu construir. Terceiros admitidos no escritor, ambos de **arquivo único**: `miniz` (Flate, MIT) e `stb_truetype` (métricas e `cmap`, domínio público — só sobre a fonte **da casa**, nunca a do usuário). São obra própria: objetos e `xref`, fluxos de conteúdo, **subconjunto TrueType** (`glyf`/`loca`/`cmap`/`hmtx`/`head`/`hhea`/`maxp`), **decodificação PNG** (filtros e entrelaçamento sobre o `inflate` do miniz), passagem de JPEG (`DCTDecode`, lendo só o cabeçalho), metadados, links e marcadores. Vira a invariante **IC-4**. | libharu · **próprio** · libharu depois próprio | — | fixa o escopo de E4 |
| DAD-8 | **Aparência do PDF** (tema: fontes, margens, cabeçalho/rodapé, numeração, sumário, cores de admonição) | tema fixo da casa · tema por JSON em `pOpcoesJSON` (modelo do `asciidoctor-pdf`, que tematiza por YAML) · ambos (padrão da casa + sobrescrita) | tema padrão da casa + sobrescrita por JSON | o renderizador PDF |
| DAD-3 | **Política de degradação do Markdown** | GFM como alvo; o que não tem equivalente (admonição, callout, célula mesclada, rodapé) vira HTML embutido, texto plano, ou é recusado com aviso | GFM + HTML embutido onde o GFM permite, aviso listado na saída (nunca mudo) | o conversor Markdown |
| DAD-4 | **Modo seguro e `include::`** | `SECURE` fixo (sem disco, sem include) · `SERVER`/`SAFE` com diretório-base passado pelo chamador · *callback* de resolução de include em C# | `SECURE` por padrão; include só por *callback* (o chamador decide o que entrega) — alinhado à IA-15 do ANVIL (parser de conteúdo de terceiro em *worker* sandboxado é do consumidor) | o contrato C |
| **DAD-5** ✅ | **Nome, lugar e invólucro** — **RESOLVIDA (dono, 2026-10-24):** "teremos um Helper `XAsciiDocConverter` em C#, nada mais, parecido com `XLZMA`." Logo: o invólucro é **um** arquivo, `XAsciiDocConverter.cs`, em `Tootega.Core`, com a mecânica da `XLZMA` (resolvedor por SO e arquitetura, `GarantirBibliotecaNativa()`, um método por formato); **nenhum projeto C# neste repositório**. O dono **não nomeou o binário**: o assistente derivou do nome do helper — `AsciiDocConverter.{win-x64.dll, linux-x64.so, linux-arm64.so}`, símbolos `ADC_*` — e isso fica aqui como **assunção declarada**, a corrigir se o dono preferir outro. **Como o Core alcança o `Dist/`** (checkout ao lado · cópia dos binários · submódulo) não foi decidido e fica para E6. | opções antigas: `Tootega.AsciiDoc`/`XAsciiDoc` · nome do repositório · invólucro em projeto próprio | — | E6 (consumo) |
| DAD-6 | **Sistema de build** | `.vcxproj` + `build-linux.sh` em Debian (padrão LZMA) · CMake nos dois SOs | padrão LZMA (o fonte é C11 puro, sem terceiro que exija CMake), salvo se o item 6 de §6 mostrar obstáculo no MSVC | scripts de build (E0, item 6) |

## 4-bis. Invariantes do conversor (IC-#) — fixadas pelas decisões do dono

- **IC-1 — Uma chamada, uma conversão** (DAD-2, 2026-10-24). Entra um array de bytes em AsciiDoc, entra o
  formato desejado, sai um array de bytes **naquele** formato. **Nenhum renderizador passa por outro formato**:
  o PDF não nasce do HTML, o Markdown não nasce do HTML, o DocBook não nasce de nada — todos nascem da **AST**.
  Cobrado por teste: o renderizador PDF não referencia o módulo HTML (dependência proibida no build), e a
  conversão de um documento para PDF não produz HTML intermediário em nenhum ponto.
- **IC-2 — Falha nomeada** (herdada de CE-21-quater). Toda falha e todo aviso dizem o quê, por quê, onde
  (linha e coluna) — e chegam ao chamador, no sucesso e na falha.
- **IC-3 — Zero dependência de runtime** (DAD-1). A nativa não carrega motor de linguagem, não abre processo,
  não lê disco por conta própria (include só por callback — DAD-4).
- **IC-4 — O escritor PDF é da casa** (DAD-7, 2026-10-24). Nenhuma biblioteca de PDF embutida ou ligada;
  terceiros no binário são **só** `miniz` e `stb_truetype`, de arquivo único, com licença copiada ao lado e
  versão fixada. Toda peça nova de terceiro é decisão do dono, com data.

## 5. Plano de estudo e proposta de arquitetura (DAD-1 = C, DAD-2 = PDF na nativa)

### 5.1 "O melhor que existe": o que estudar e para quê

| Fonte | Licença | O que se extrai dela | Papel no projeto |
|---|---|---|---|
| **Especificação AsciiDoc Language** (Eclipse; `spec/`, esquema do **ASG** no mesmo repositório) | EPL-2.0 (docs: CC-BY-4.0); **em desenvolvimento** sob o EFSP (F25) | gramática, modelo de documento, o ASG como forma canônica da árvore | define a **AST da casa** (alinhada ao ASG); o **TCK** (adapter `stdin` → ASG JSON) é o **juiz do parse** |
| **Asciidoctor (Ruby)** — `lib/asciidoctor/{reader,parser,substitutors}.rb`, `converter/html5.rb`; `test/` com **24 arquivos** `*_test.rb` + `fixtures/` (F27) | MIT | comportamento de referência: pré-processador (include, condicionais, atributos), detecção de blocos, **ordem das substituições** e grupos (`normal`, `header`, `verbatim`, `pass`, `none`) (F26) | **oráculo de saída**: HTML5 de referência gerado em dev; casos de teste **portados** (MIT, com atribuição) |
| **asciidoc-parser** (Rust) — módulos `attributes`, `blocks`, `content`, `document`, `inlines`, `span`, `parser`, `warnings` (F30) | MIT/Apache-2.0 | organização de um parser semântico moderno verificado contra a suíte do Asciidoctor; a **lista do que deixou de fora** (§2.3) é um mapa das partes caras | referência de **arquitetura** e segundo oráculo de comportamento |
| **acdc** (Rust) — parser por **PEG**, AST alinhada ao ASG, conversor **Markdown** com degradação declarada (F11, F31) | MIT | como mapear ASG → Markdown e o que degrada | referência para a **DAD-3** |
| **asciidocr** (Rust) — *scanner* byte a byte → *parser* por máquina de estados (~500 linhas) → *render*; "30× mais rápido que o Asciidoctor" em benchmark preliminar **sem metodologia publicada** (F29) | — | ordem de grandeza possível de uma implementação nativa | só indica o alvo; **nenhum número entra em texto do produto sem método** (IA-7) |

### 5.2 Por que é difícil (fatos, não opinião — F28)

- **Substituições inline são passes ordenados sobre texto híbrido**: cada passe opera sobre a saída do anterior
  (caracteres especiais → aspas → referências de atributo → *replacements* → macros → *post replacements*), e a
  interpretação depende da ordem. A linguagem **não tem gramática formal limpa**; o grupo de trabalho Eclipse
  registra isso como conflito inerente (issues #16 e #22).
- **Referências de atributo resolvem depois das aspas** e o valor resolvido deve afetar o parse seguinte — uma
  árvore pronta não se "re-parseia"; a saída é um **pré-processador inline** (proposta em discussão na spec).
- **A lista de atributos de bloco (`attrlist`) exige sub-parser**, porque referências de atributo são resolvidas
  antes de parseá-la (issue #33).
- **Detecção do tipo de bloco depende da ordem dos testes** (documentado desde o `asciidoc(1)` original).
- **`include::` e condicionais são pré-processamento**, com acesso a arquivos — segurança e sandbox (DAD-4).

**Regra de fidelidade proposta (binária):** onde a especificação já decide, **a especificação manda**; onde ela
ainda não cobre, **vale o comportamento observado do Asciidoctor 2.0.x**; toda divergência deliberada entre os
dois ou da casa em relação a eles **fica em tabela neste repositório, com data e motivo**.

### 5.3 Arquitetura proposta (C11, zero dependências externas)

```
UTF-8 ──► Leitor / Pré-processador      linhas; include:: por callback do chamador; ifdef/ifndef/ifeval;
          (reader)                       atributos do cabeçalho e de linha de comando
      ──► Parser de blocos               cabeçalho, seções, parágrafos, delimitados, listas (aninhadas,
          (blocks)                       continuação), tabelas (cols/formato), attrlist, título/âncora de bloco
      ──► Parser inline por grupo        passes na ORDEM OFICIAL; a saída de cada passe são NÓS, não texto —
          (inlines)                      é aqui que a casa diverge do regex-sobre-string da referência
      ──► AST em arena (≈ ASG)           um documento = uma arena = um free; strings são fatias da entrada
      ──► Renderizadores (cada um        HTML5 · Markdown GFM (DAD-3) · ASG JSON (TCK e consumidores) ·
          direto da AST — IC-1)          DocBook 5 (opcional) · PDF = motor de leiaute próprio (paginação,
                                         parágrafos, listas, tabelas, imagens, cabeçalho/rodapé, sumário,
                                         fontes TrueType embutidas) sobre o escritor PDF próprio (IC-4)
```

Decisões de projeto que a performance pede e que ficam fixas desde o início:

- **Arena por documento** (sem `malloc` por nó; liberação única); **fatias** (ponteiro + tamanho) sobre a
  entrada, escape só na renderização; **UTF-8 validado na entrada** (BOM e sequência inválida são erros
  nomeados, não ignorados).
- **Sem motor de regex**: *scanners* escritos à mão, despacho por primeiro byte; é o que separa esta
  implementação da referência e o que o estudo das Rust confirma como viável.
- **Limites explícitos** (profundidade de aninhamento, tamanho de entrada, número de includes) com erro que
  diz o limite e o valor encontrado — nunca *stack overflow* silencioso.
- **Sem estado global**: *thread-safe* por construção; cada conversão é independente.
- **Mensagens em pt-BR**, com linha e coluna, nível (aviso/erro) e texto que diz **o quê** e **por quê**
  (CE-21-quater vale dentro da nativa também).
- **Sanitizers e fuzzing** fazem parte do build Linux desde a primeira etapa (ASan/UBSan em debug; libFuzzer ou
  AFL sobre `ADC_Converter`): parser de conteúdo de terceiro é superfície de ataque, e a IA-15 do ANVIL já
  manda rodá-lo em *worker* sandboxado — a nativa não pode ser o elo fraco.

### 5.4 Peças do repositório

```
AsciiDocConverter/                  ← repositório próprio (github.com/HermesSilva/AsciiDocConverter)
├── PLANEJAMENTO.md                 ← este arquivo
├── docs/                           ← tabela de fidelidade (spec × Asciidoctor × casa), decisões DAD-#, benchmarks
├── include/adc.h                   ← o contrato C (§5.5)
├── src/                            ← reader/, blocks/, inlines/, ast/, render/{html5,markdown,asg,docbook,pdf}/, adc.c (ABI)
├── src/render/pdf/                 ← leiaute/ (páginas, caixas, quebra de linha, tabelas), tema/ (DAD-8)
├── src/pdf/                        ← o escritor próprio (IC-4): objetos e xref, fluxos, fontes/ (TrueType: leitura via
│                                     stb_truetype, subconjunto próprio), imagens/ (PNG próprio sobre inflate; JPEG passa)
├── terceiros/                      ← miniz.c/.h e stb_truetype.h, versão fixada, licença ao lado (os únicos — IC-4)
├── fontes/                         ← família TrueType de licença livre embutida no binário (DAD-8; licença junto)
├── test/
│   ├── tck/                        ← adapter do TCK (executável mínimo sobre a lib) + script que o roda
│   ├── casos/                      ← casos portados da suíte do Asciidoctor (MIT, atribuição) e da casa
│   ├── golden/                     ← HTML5 de referência gerado pelo Asciidoctor 2.0.x (script regenera)
│   └── fuzz/                       ← alvos de fuzzing e corpus mínimo
├── bench/                          ← corpus fixo e harness; resultados datados em docs/
├── AsciiDocConverter.vcxproj       ← Windows x64 (MSBuild.exe; no-op sob dotnet CLI)
├── Build-AsciiDocConverter.ps1     ← orquestra Windows + Linux (Docker), confere Dist/
├── build-linux.sh                  ← gcc/aarch64, -Wall -Wextra -Werror, --no-undefined, nm dos símbolos ADC_*
├── AsciiDocConverter.Native.props  ← Content curinga sobre Dist/ para o host
└── Dist/                           ← AsciiDocConverter.win-x64.dll, .linux-x64.so, .linux-arm64.so
```

Em `Tootega.Core` (DAD-5): **só** o helper `XAsciiDocConverter.cs` (resolvedor por SO/arquitetura,
`GarantirBibliotecaNativa()`, e um método por formato — `ParaHTML`, `ParaMarkdown`, `ParaPDF`, `ParaASG` — todos
sobre a mesma `ADC_Converter`), "nada mais, parecido com `XLZMA`". O `XDocumentoPDF` (iText) **não participa**:
o PDF vem pronto da nativa (IC-1).

### 5.5 Contrato C (proposta — símbolos já em CE-8, sem `EntryPoint`)

```c
/* O formato de saída é parâmetro da MESMA função; a entrada é sempre AsciiDoc (IC-1). */
typedef enum { ADC_HTML5 = 1, ADC_MARKDOWN = 2, ADC_PDF = 3, ADC_ASG_JSON = 4, ADC_DOCBOOK5 = 5 } ADC_Formato;

/* Resolve um include:: pedido pelo documento. Devolve 0 e preenche pConteudo/pTamanho (UTF-8, alocados
   pelo chamador e liberados por ele após a conversão), ou outro valor para negar — a negação vira aviso
   no documento, nunca silêncio. Sem callback registrado, todo include é negado (modo seguro — DAD-4). */
typedef int32_t (*ADC_ResolverInclude)(void* pContexto, const char* pCaminho, int32_t pTamanhoCaminho,
                                       const uint8_t** pConteudo, int32_t* pTamanho);

/* Converte um documento AsciiDoc (UTF-8, sem BOM) para o formato pedido.
   Devolve 0 em sucesso; outro valor é código de falha (tabela em adc.h).
   pSaida e pMensagens são alocados pela biblioteca e liberados com ADC_Liberar.
   pMensagens é JSON: lista de avisos/erros (nível, linha, coluna, texto em pt-BR) — devolvida também no
   sucesso, porque aviso do processador não se perde (ERRO MUDO É DEFEITO). */
int32_t ADC_Converter(const uint8_t* pEntradaUTF8, int32_t pTamanho, ADC_Formato pFormato,
                      const char* pOpcoesJSON, ADC_ResolverInclude pInclude, void* pContextoInclude,
                      uint8_t** pSaida, int32_t* pTamanhoSaida, char** pMensagens);
void        ADC_Liberar(void* pMemoria);
const char* ADC_Versao(void);  /* "AsciiDocConverter 0.1.0" */
```

`pOpcoesJSON`: atributos do documento, `standalone`/`embedded`, numeração de seções, política de degradação do
Markdown (DAD-3), limites. Nenhum valor de segredo passa por aqui (IA-13 do ANVIL vale para quem chama).

### 5.6 Testes e oráculos (critério de pronto por etapa)

1. **TCK**: o adapter da casa roda a suíte inteira; **zero falhas** no escopo da etapa é o critério de pronto do
   parse. Prova de que o juiz funciona antes do primeiro nó: TCK compilado e rodando contra um adapter vazio.
2. **Golden HTML5**: para cada caso em `test/casos/`, a saída da casa é comparada à do Asciidoctor 2.0.x
   (gerada por script em máquina de dev com Ruby; versionada em `test/golden/`). Diferença deliberada entra na
   tabela de fidelidade, com motivo; diferença não deliberada é bug.
3. **Fuzzing e sanitizers** no build Linux: nenhum *crash*, nenhuma leitura fora dos limites, nenhum vazamento.
4. **Benchmarks com método**: corpus fixo em `bench/`, medição contra Asciidoctor Ruby e `asciidoc-parser`
   (mesmo hardware, mesma entrada, mediana de N execuções); resultado datado em `docs/`. Meta é **medida**,
   não prometida.
5. **Invólucro C#** (`Tootega.Core.Test.Unit`): toda mensagem da nativa chega ao chamador; ausência do binário
   falha no boot com a mensagem que diz como gerar.

### 5.7 Etapas (pacotes ordenados por dependência)

| Etapa | Entrega | Pronto quando |
|---|---|---|
| **E0 Estudo** | §6 inteiro: tabela de comportamentos, corpus, ASG mapeado, TCK rodando, baseline medida | os sete itens de §6 escritos em `docs/`, com data |
| **E1 Núcleo** | leitor + cabeçalho + seções + parágrafos + listas simples + inline `normal` + HTML5 + ASG JSON; ABI `ADC_*`; build nos 3 alvos | TCK verde no escopo; golden igual nos casos de E1; `ADC_Versao` responde nos 3 alvos |
| **E2 Cobertura** | delimitados, admonições, tabelas, attrlist completo, listas aninhadas/descrição/callouts, include por callback, condicionais, notas de rodapé, âncoras e referências, imagens | TCK verde inteiro; golden igual em todos os casos portados |
| **E3 Markdown** | renderizador GFM com a política DAD-3 e avisos de degradação | corpus convertido; cada degradação listada em `pMensagens` |
| **E4 PDF** | **escritor próprio** (IC-4: objetos, xref, fluxos Flate, fonte TrueType com subconjunto, PNG próprio, JPEG passado, links, marcadores, metadados) + motor de leiaute: página e margens, quebra de linha e de página, parágrafos, títulos, listas, blocos de código, admonições, tabelas (larguras, quebra de célula entre páginas), imagens (PNG/JPEG), cabeçalho/rodapé com numeração, sumário com páginas, links; tema padrão da casa e sobrescrita por JSON (DAD-8); fontes TrueType embutidas com subconjunto | corpus inteiro vira PDF que abre em Acrobat, Edge, Firefox e `pdfinfo`/`qpdf --check` sem aviso; acentos do pt-BR corretos; comparação visual com o `asciidoctor-pdf` registrada por caso (igual, diferente e aceito, ou bug) |
| **E5 Robustez** | fuzzing, sanitizers, limites, benchmarks datados, `docs/` completo | 24 h de fuzz sem achado; benchmark publicado com método |
| **E6 Casa** | `XAsciiDocConverter` em `Tootega.Core`, `.props`, boot, testes do invólucro, `Dist/` versionado | build do Core e de um SaaS verde; boot detecta ausência |

### 5.8 Regras que já valem (herdadas, não dependem das DAD-# abertas)

- **UTF-8 em tudo**, entrada e saída; BOM na entrada é erro nomeado, não ignorado em silêncio.
- **Falha diz o quê e por quê** (CE-21-quater): a nativa devolve as mensagens com linha e coluna; o invólucro
  C# lança exceção que nomeia a operação (`XAsciiDocConverter.ParaHTML`), o formato e o primeiro erro.
- **Binário por alvo em `Dist/`**, versionado, com exceção no `.gitignore` como a LZMA; símbolo conferido por
  `nm` no Linux e por *smoke test* (`ADC_Versao`) nos dois SOs.
- **Código de terceiro não entra**: o que se estuda se cita; caso de teste portado da suíte do Asciidoctor
  leva a atribuição MIT no arquivo. Fonte Rust e Ruby **não se copia nem se traduz linha a linha** — se
  lê, se entende e se escreve a versão da casa.
- **Nenhum número de desempenho em texto de produto sem método publicado** (IA-7).

## 6. O que a etapa E0 (estudo) tem de produzir — antes da primeira linha de parser

1. **Tabela de comportamentos**: por construção da linguagem (cabeçalho, seção, parágrafo, cada tipo de lista,
   cada delimitado, tabela, attrlist, cada substituição inline, include, condicionais, notas, âncoras), o que a
   **especificação** diz, o que o **Asciidoctor 2.0.x** faz, o que o **asciidoc-parser** deixou de fora, e a
   decisão da casa (§5.2, regra de fidelidade).
2. **Corpus**: 50 documentos reais e sintéticos cobrindo a tabela, com **golden HTML5** gerado pelo Asciidoctor
   2.0.x em máquina de dev (script versionado; versão do Asciidoctor fixada e registrada).
3. **ASG lido e mapeado**: o esquema do ASG (repositório da spec) traduzido para a AST em C (tipos, campos,
   o que é obrigatório); divergências anotadas.
4. **TCK compilado e rodando** (Node ≥ 20) contra um adapter vazio em C — prova de que o juiz funciona e
   inventário de quantos testes existem por categoria (número **não apurado** hoje: a página do GitLab não
   carregou no fetch).
5. **Baseline medida**: Asciidoctor Ruby e `asciidoc-parser` sobre o corpus, com método (hardware, mediana de
   N, tamanho da entrada) — é contra isso que "alta performance" vai ser **mostrada**, não afirmada.
6. **Toolchain provado**: `.vcxproj` com C11 no MSVC, `gcc -std=c11 -Wall -Wextra -Werror`, ASan/UBSan no
   contêiner Debian, cross arm64 carregando (QEMU ou máquina real); a estrutura de §5.4 criada vazia **só**
   nesta etapa, nunca antes.
7. **Licenças escritas**: EPL-2.0 (ler a spec e usar o esquema do ASG — confirmar o alcance do EPL sobre o
   esquema no `NOTICE.adoc`), MIT (casos portados com atribuição; `miniz`), domínio público (`stb_truetype`),
   a licença da família de fontes embutida (DAD-8), e a licença **deste** repositório — decisão do dono.
8. **Estudo do PDF** (DAD-2, DAD-7): o modelo de leiaute e o tema do **asciidoctor-pdf** (a referência de
   "como AsciiDoc fica em PDF"); a parte da ISO 32000 que o escritor usa (estrutura de arquivo, `xref`,
   fluxos, `FlateDecode`, fontes `TrueType`/`CIDFontType2` com `ToUnicode`, imagens, anotações de link,
   *outlines*); o que o subconjunto TrueType exige (tabelas obrigatórias, `cmap` reescrito, `loca`/`glyf`
   compactados, *checksums*); e um protótipo de **uma página** com o escritor próprio (título, parágrafo
   acentuado, lista, tabela simples, uma imagem PNG) validado em `qpdf --check`, Acrobat, Edge e Firefox —
   é esse protótipo que prova a IC-4 antes de E4.

Cada item vira um arquivo em `docs/`, com data. Fechado o E0, o plano de E1 ganha a lista exata de construções
e nasce a primeira linha de parser.

## 8. Estado da implementação e como operar (2026-10-24)

> Esta seção é a **entrada rápida** para quem retoma o trabalho; §1–§7 são o registro de pesquisa e decisões.

### 8.1 O que está feito (E0–E6)

| Etapa | Entrega | Prova |
|---|---|---|
| E0 | oráculos locais, fontes, terceiros fixados, toolchain provado | `docs/fidelidade.md`, `terceiros/VERSOES.md`, `fontes/VERSOES.md` |
| E1–E2 | parser (`src/`): leitor + pré-processador, blocos, listas, dlists, tabelas, attrlist (SDR-2), inline por escolha ordenada com localização | TCK oficial **13/13**; golden HTML **13/13** |
| E3 | Markdown GFM com degradação avisada (`src/render/adc_render_markdown.c`) | corpus sem achado (ASan); sem oráculo externo |
| E4 | escritor PDF próprio (`src/pdf/`) + leiaute (`src/render/adc_render_pdf.c`) | PDFs abrem no pypdf/PyMuPDF; páginas conferidas visualmente |
| E5 | fuzz (`test/fuzz`), bench (`bench/`, `docs/benchmark.md`), avisos como erro nos dois compiladores | 3 min de libFuzzer sem achado |
| E6 | `XAsciiDocConverter.cs` + `TestesXAsciiDocConverter` no Core; `AsciiDocConverter.Native.props`; `Dist/` | 6 testes verdes; `dotnet build` do Core sem aviso |
| E6-bis (2026-10-24, dono) | HTML completo com a folha padrão do Asciidoctor embutida; `toc2` lateral, `sectanchors`/`sectlinks`, título de reserva, `noheader`/`notitle`/`nofooter`/`reproducible`/`docdatetime`, `linkcss`, `webfonts` (opt-in, F5), metas `description`/`keywords`/`copyright`, notas fora de `#content` | igual ao `standalone: true` do Asciidoctor no corpus + 3 extras (só `generator`, F5 e a data diferem); golden 13/13 (caso 10 ganhou `sectanchors`/`sectlinks`); C# 6/6 |
| E7 (2026-10-24, dono: "continue testando e corrigindo") | validação em corpus real (15.865 `.adoc` do OpenShift) contra o Asciidoctor.js; ~30 regras do Asciidoctor portadas a partir do código dele (cabeçalho com metadados, nível 0 sem descarte, `leveloffset`, tags de include, includes de URL e ausentes, leitura de itens de lista = `read_lines_for_list_item`, tabelas, marcas, links, ícones por ponto do documento, `toc`) — lista em `docs/fidelidade.md`; F3 revogada (a ratificar) | reprovados 15.643 → 547; golden 23/23 (Windows e Linux); TCK 13/13; ASan/UBSan no corpus; 3 min de libFuzzer sem achado |

### 8.2 Onde está cada coisa

```
include/adc.h                 contrato público (ADC_Converter / ADC_Liberar / ADC_Versao)
src/adc_*.c                   infraestrutura, atributos, leitor, attrlist, blocos, tabelas, inlines, documento, ABI (adc.c)
src/render/                   html5, markdown, asg, pdf (leiaute), comum
src/pdf/                      escritor (objetos/xref/fluxos), fonte TrueType + subconjunto, imagens PNG/JPEG, adc_fontes.h
terceiros/                    miniz 3.1.2, stb_truetype 1.26, asciidoctor.css (Asciidoctor 2.0.20, MIT; embutida no HTML completo) (+ licenças, VERSOES.md)
fontes/                       Liberation 2.1.5 TTF (OFL) → obj/fontes/adc_fontes_embutidas.c (fontes + css) gerado por ferramentas/gerar_fontes.py
src/adc_recursos.h            declaração da folha embutida (adc_css_asciidoctor)
ferramentas/Converter-Adoc.ps1  converte ou valida um .adoc (ou vários: -Recursivo Links|Pastas) pela DLL/.so de Dist (P/Invoke em PowerShell 7)
ferramentas/oraculo-asciidoctor.js  oráculo Asciidoctor.js para Converter-Adoc.ps1 -Oraculo (processo Node persistente)
test/casos + test/golden      corpus e HTML de referência (gerar.js com @asciidoctor/core 3.0.4; comparar-golden.sh)
test/casos-spec               divergências deliberadas da spec (sem golden)
test/tck/adc_tck.c            adapter do TCK; test/adc_cli.c ferramenta de linha de comando; test/fuzz alvo libFuzzer
bench/                        bench.c + rodar.sh + asciidoctorjs.js + corpus
Dist/                         AsciiDocConverter.win-x64.dll, .linux-x64.so (arm64 pendente: Docker)
```

### 8.3 Comandos

- Dev Windows: `.\Compilar-Dev.ps1` (MSVC /W4 /WX) → `obj/dev/{AsciiDocConverter.dll, adc_cli.exe, adc_tck.exe}`.
- Dev Linux/WSL: `bash compilar-dev.sh` (gcc, ASan/UBSan) → `obj/dev-linux/`.
- Distribuição: `.\Build-AsciiDocConverter.ps1` (Windows por MSBuild; Linux x64+arm64 por Docker). Sem Docker:
  `RAIZ=$(pwd) ARQUITETURAS=linux-x64 bash build-linux.sh` no WSL.
- TCK: `node harness/bin/asciidoc-tck.js cli --adapter-command <adc_tck.exe>` no checkout do `asciidoc-tck`.
- Golden: `node test/golden/gerar.js --modulos <node_modules>` e `bash test/comparar-golden.sh`.
- Core: `dotnet test ... --filter FullyQualifiedName~TestesXAsciiDocConverter`.
- Converter com a nativa de `Dist/`: `.\ferramentas\Converter-Adoc.ps1 <arquivo.adoc|pasta> [-Recursivo Links|Pastas]
  [-Formato html,md,pdf,asg] [-Saida <dir>] [-ProfundidadeMaxima n] [-IncluirParciais] [-MostrarAvisos]
  [-Validar] [-Oraculo <node_modules>] [-Log <csv>] [-Opcoes <json>] [-Biblioteca <dll|so>]` — um arquivo, ou vários
  seguindo os links (lidos do ASG) ou a árvore de pastas; `-Validar` reprova falha, erro e truncamento silencioso;
  `-Oraculo` compara o texto com o Asciidoctor.js (`ferramentas/oraculo-asciidoctor.js`); ver o README.

### 8.4 Pendências

- `Dist/AsciiDocConverter.linux-arm64.so`: subir o Docker Desktop e rodar `Build-AsciiDocConverter.ps1 -Alvo Linux`.
- Licença do repositório (dono).
- Commits (dono): este repositório inteiro; no Core, `XAsciiDocConverter.cs`, o teste e o `Tootega.Core.csproj`;
  no ANVIL, a linha `/AsciiDocConverter/` do `.gitignore`.
- Não implementado (ver `docs/fidelidade.md`): DocBook 5, `rowspan` no PDF, realce de sintaxe, doctype `book`,
  `toc::[]` no meio do documento, STEM renderizado.

## 7. Fontes (consultadas em 2026-10-24)

- F1 — busca por parser AsciiDoc em C: [ctags `parsers/asciidoc.c`](https://github.com/universal-ctags/ctags/blob/master/parsers/asciidoc.c), [asciidoctor-go](https://sr.ht/~shulhan/asciidoctor-go/), [asciidoc-hs](https://hackage.haskell.org/package/asciidoc), [libasciidoc](https://github.com/bytesparadise/libasciidoc/blob/master/README.adoc)
- F2 — runtimes do Asciidoctor: [docs.asciidoctor.org](https://docs.asciidoctor.org/asciidoctor/latest/), [repositório](https://github.com/asciidoctor/asciidoctor)
- F3 — conversores embutidos e add-ons: [Available converters](https://docs.asciidoctor.org/asciidoctor/latest/convert/available/)
- F4 — Asciidoctor PDF (Ruby/Prawn): [docs](https://docs.asciidoctor.org/pdf-converter/latest/)
- F5 — Asciidoctor.js 3.0 (Opal, "qualquer plataforma JavaScript"): [docs 3.0](https://docs.asciidoctor.org/asciidoctor.js/3.0/), [instalação, build GraalVM](https://docs.asciidoctor.org/asciidoctor.js/3.0/setup/install/)
- F6 — Asciidoctor.js 4.x (ESM, não Opal; Node 22/24): [docs latest](https://docs.asciidoctor.org/asciidoctor.js/latest/), [repositório](https://github.com/asciidoctor/asciidoctor.js/)
- F7 — versões e datas: [registro npm `@asciidoctor/core`](https://registry.npmjs.org/@asciidoctor/core) (3.0.4 em 2024-02-12; 4.0.0 em 2026-06-22; 4.1.1 em 2026-10-05; `engines.node >= 20`)
- F8 — Nashorn/GraalVM (histórico de embutir fora do Node): [discuss.asciidoctor.org](https://discuss.asciidoctor.org/Nashorn-Asciidoctor-js-and-InputOutput-files-tp2299.html), [asciidoctorg (PoC GraalVM)](https://github.com/ggrossetie/asciidoctorg)
- F9 — asciidoc-parser (Rust): [repositório](https://github.com/scouten/asciidoc-parser), [lib.rs](https://lib.rs/crates/asciidoc-parser)
- F10 — asciidork: [repositório](https://github.com/jaredh159/asciidork)
- F11 — acdc: [repositório](https://github.com/nlopes/acdc)
- F12 — acdc, post do autor (2026-05-24): [Building an AsciiDoc toolchain in Rust](https://nlopes.dev/writing/building-acdc)
- F13 — alvos Tier 1 do Rust: [Platform Support](https://doc.rust-lang.org/rustc/platform-support.html); cross: [cargo-zigbuild](https://github.com/rust-cross/cargo-zigbuild)
- F14 — projeto Eclipse AsciiDoc Language: [projects.eclipse.org](https://projects.eclipse.org/node/18722), [métricas](https://metrics.eclipse.org/projects/asciidoc.asciidoc-lang/)
- F15 — TCK (adapter stdin → ASG JSON; não valida conversão; sem releases): [README do TCK](https://gitlab.eclipse.org/eclipse/asciidoc-lang/asciidoc-tck/-/raw/main/README.adoc)
- F16 — Markdown: [downdoc](https://npmjs.com/package/downdoc), [Asciidoctor + pandoc via DocBook](https://tinyapps.org/blog/201701240700_convert_asciidoc_to_markdown.html)
- F17 — conversor customizado do Asciidoctor.js: [Custom converter](https://docs.asciidoctor.org/asciidoctor.js/latest/extend/converter/custom-converter/)
- F18 — libharu: [repositório](https://github.com/libharu/libharu), [releases (2.4.6)](https://github.com/libharu/libharu/releases), [Arch 2.4.6-1](https://archlinux.org/packages/extra/x86_64/libharu/)
- F19 — libharu UTF-8 (≤ 3 bytes, só TrueType): [UseUTFEncodings](https://www.hollywood-mal.com/docs/html/polybios/docUseUTFEncodings.html), [Fonts and Encodings](https://libharu.sourceforge.net/fonts.html)
- F20 — PDFGen: [repositório](https://github.com/AndreRenaud/PDFGen)
- F21 — litehtml: [repositório](https://github.com/litehtml/litehtml)
- F22 — iText pdfHTML: [produto](https://itextpdf.com/products/itext-pdf-html), [NuGet `itext.pdfhtml` 6.3.4](https://www.nuget.org/packages/itext.pdfhtml), [i7n-pdfhtml (AGPL/comercial)](https://github.com/itext/i7n-pdfhtml)
- F23 — iText na casa: `Back/Core/Tootega.Core/Tootega.Core.csproj` (`itext` 9.8.0), skill `tootega-relatorios` §"PDF — reusa o engine de impressão, não fala iText"
- F24 — QuickJS-ng: [repositório](https://github.com/quickjs-ng/quickjs), [releases (v0.17.0, 2026-09-18)](https://github.com/quickjs-ng/quickjs/releases)
- F25 — repositório da especificação (EPL-2.0; esquema do ASG; EFSP, "em desenvolvimento"): [README asciidoc-lang](https://gitlab.eclipse.org/eclipse/asciidoc-lang/asciidoc-lang/-/raw/main/README.adoc)
- F26 — ordem e grupos de substituição: [Substitutions](https://docs.asciidoctor.org/asciidoc/latest/subs/)
- F27 — suíte de testes do Asciidoctor (24 arquivos `*_test.rb` + `fixtures/`): [asciidoctor/test](https://github.com/asciidoctor/asciidoctor/tree/main/test)
- F28 — por que a linguagem resiste a gramática formal: issues do grupo Eclipse [#16](https://gitlab.eclipse.org/eclipse/asciidoc-lang/asciidoc-lang/-/issues/16), [#22 (pré-processador inline)](https://gitlab.eclipse.org/eclipse/asciidoc-lang/asciidoc-lang/-/issues/22), [#33 (attrlist)](https://gitlab.eclipse.org/eclipse/asciidoc-lang/asciidoc-lang/-/issues/33); ordem de detecção de blocos no [asciidoc(1), cap. 9](https://asciidoc.org/chunked/ch09.html); [lista do Git, 2015](https://lore.kernel.org/all/20150514042544.GA9351@peff.net/); `substitutors.rb` [v2.0.6](https://gem.sh/gems/asciidoctor/v2.0.6/files/lib%2Fasciidoctor%2Fsubstitutors.rb)
- F29 — asciidocr (scanner → parser → render; "30×" sem metodologia): [artigo](https://news.lavx.hu/article/rising-rust-building-a-blazing-fast-asciidoc-parser-from-scratch), [post do autor](https://www.bikesbooksandbullshit.com/bullshit/2025/01/08/writing-an-asciidoc-parser-in-rust.html)
- F30 — estrutura do asciidoc-parser: [parser/src](https://github.com/scouten/asciidoc-parser/tree/main/parser/src)
- F31 — acdc-parser por PEG: [docs.rs](https://docs.rs/acdc-parser)
- F32 — asciidoctor-pdf (MIT; Prawn; tema YAML; sem formato intermediário): [repositório](https://github.com/asciidoctor/asciidoctor-pdf)
- F33 — libharu e as dependências zlib/libpng (sem opção confirmada para dispensá-las): [ebuild Gentoo 2.4.5](https://ftp.kaist.ac.kr/gentoo-portage/media-libs/libharu/libharu-2.4.5.ebuild), [README](https://github.com/libharu/libharu)
- F34 — miniz (MIT, `miniz.c`/`miniz.h`): [repositório](https://github.com/richgel999/miniz)
- F35 — stb_truetype (domínio público; só leitura; sem garantia de segurança): [stb_truetype.h](https://github.com/nothings/stb/blob/master/stb_truetype.h)
- F36 — Liberation Fonts (SIL OFL 1.1): [repositório](https://github.com/liberationfonts/liberation-fonts)
