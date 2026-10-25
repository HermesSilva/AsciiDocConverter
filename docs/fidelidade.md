# Tabela de fidelidade — especificação × Asciidoctor 2.0.x × AsciiDocConverter

Regra (PLANEJAMENTO.md §5.2, binária): **onde a especificação AsciiDoc Language já decide, ela manda; onde não
cobre, vale o comportamento observado do Asciidoctor 2.0.20**; toda divergência deliberada fica aqui, com data.

Oráculos: o **TCK** (13 testes, `asciidoc-tck` em 2026-10-24 — 13/13 verdes pelo harness oficial em Node e pelo
adapter `test/tck/adc_tck.c`), o **golden HTML** gerado pelo Asciidoctor.js 3.0.4 (`test/golden/`, 23/23 iguais
byte a byte em `test/comparar-golden.sh`, no Windows e no Linux) e a **validação em corpus real**
(`ferramentas/Converter-Adoc.ps1 -Oraculo`, seção ao fim).

## Divergências deliberadas (seguimos a especificação; o Asciidoctor 2.0.x faz diferente)

| # | Construção | Especificação | Asciidoctor 2.0.20 | AsciiDocConverter | Caso |
|---|---|---|---|---|---|
| F1 | Bloco aberto com `~~~~` (4+ tis) | SDR-1: forma de delimitador variável para o open block | não reconhece; vira parágrafo com `~` subscrito | reconhece como open block (nesting por tamanho) | `test/casos-spec/sdr-001-open-block-til.adoc` |
| F2 | Marcação dentro do valor de um atributo (`:frase: texto *forte*` e `{frase}`) | SDR-5: o pré-processador resolve a referência antes do parse inline; o valor é interpretado | aplica `attributes` depois de `quotes`: o `*forte*` sai literal | interpreta (`<strong>`) | `casos-spec/sdr-005-atributo-com-marcacao.adoc` |
| ~~F3~~ | Item de lista após parágrafo anexado com `+`, sem linha vazia | SDR-8: o item só interrompe o texto principal, não o bloco anexado | o irmão interrompe o parágrafo anexado | **revogada em 2026-10-24**: a leitura de itens passou a ser a tradução de `read_lines_for_list_item` do Asciidoctor (o irmão encerra o item), porque documentos reais (16 mil `.adoc` do OpenShift) são escritos contra o Asciidoctor e a divergência quebrava listas em centenas deles. **A ratificar pelo dono.** | `casos-spec/sdr-008-item-nao-interrompe-paragrafo-anexado.adoc` (agora sai como no Asciidoctor) |
| F4 | Intrínsecos `{amp}` `{lt}` `{gt}` | SDR-5: specialchars é tarefa do conversor sobre todo texto | emite `& < >` crus (HTML inválido) | escapa (`&amp; &lt; &gt;`) | `casos-spec/specialchars-intrinsecos.adoc` |
| F5 | Fontes web no HTML completo (`webfonts`) | a especificação não trata de saída | liga `fonts.googleapis.com` por padrão (só `:webfonts!:` desliga) | só liga quando o documento ou as opções **definem** `webfonts` (vazio = família padrão do Asciidoctor; com valor = a família indicada): a biblioteca roda dentro de um SaaS e não dispara pedido externo sem ordem (decisão de 2026-10-24) | não é construção de parse; ver a seção seguinte |

### HTML5 completo (`autonomo`) — validado contra o Asciidoctor 2.0.20 em 2026-10-24

A opção `autonomo` gera o documento inteiro na estrutura do conversor html5 do Asciidoctor, com a **folha de estilo
padrão dele embutida** (`terceiros/asciidoctor.css`, MIT, a mesma que o `@asciidoctor/core` 3.0.4 embute) — decisão
do dono em 2026-10-24, para o arquivo abrir com o mesmo visual. Conferido byte a byte com `standalone: true` sobre o
corpus de `test/casos` e três documentos extras (`webfonts`, `linkcss`, `toc` left/right/`toc-position`, `nolang`,
`notitle`, `noheader`, `nofooter`, `reproducible`, `docdatetime`, `description`/`keywords`/`copyright`,
`sectanchors`/`sectlinks`, `icons=font`, documento sem cabeçalho). As únicas linhas diferentes são o
`<meta name="generator">` (nome do conversor), a F5 e a data: o Asciidoctor escreve `last-update-label` +
`docdatetime` (hora do arquivo de entrada); a biblioteca não tem relógio nem lê o disco, então só escreve a linha
quando o chamador define `docdatetime` nas opções (`atributos`), e nunca com `:reproducible:`.

## Comportamentos herdados do Asciidoctor onde a especificação ainda não decide

| Construção | Comportamento adotado (igual ao Asciidoctor 2.0.20) |
|---|---|
| Escape com barra | contextual: a barra só escapa uma marcação que casaria ali; o trecho casado sai literal sem a barra (`\*a\*` → `*a\*`); barra sem marcação a seguir fica como texto |
| Listas de tipos diferentes adjacentes (mesmo com linha vazia) | aninham (`* a` seguido de `. b` põe a ordenada dentro de `a`) |
| Título de seção dentro de bloco delimitado | não abre seção; vira título discreto, com aviso |
| `attribute-missing` | `skip` (padrão, silencioso), `warn` (avisa), `drop`/`drop-line` (remove a referência) |
| `:source-language:` | todo bloco listing **delimitado** posterior vira `source` nessa linguagem; `[listing]` em parágrafo não |
| `kbd:[]`, `btn:[]`, `menu:[]` | só com `:experimental:` |
| Reticências e travessão colado | `&#8230;&#8203;` e `&#8212;&#8203;` (com zero-width space), ` -- ` entre espaços → thin space + em dash |
| Larguras de coluna de tabela | percentuais truncados em 4 casas; a última coluna recebe o resto |
| id de seção | `_` + título em minúsculas, pontuação removida, espaços → `_`; `charref` e trechos crus descartados; duplicata ganha `_2`, `_3` |

## Validação em corpus real — o que foi corrigido para seguir o Asciidoctor (2026-10-24)

Corpus: `F:\AsciiDoc\Docs`, 15.865 `.adoc` da documentação do OpenShift; cada documento convertido pela biblioteca e
pelo Asciidoctor.js 3.0.4 (modo `unsafe`, `base_dir` = pasta do documento, padrões pt-BR em modo fraco) e o texto do
HTML comparado (`Converter-Adoc.ps1 -Oraculo`). Reprovados: **15.643 → 547** (3,4%). Cada regra abaixo foi lida no
código do Asciidoctor (Asciidoctor.js 3.0.4 = Asciidoctor 2.0.20) e tem caso no golden (`test/casos/14` a `23`).

| Regra | Onde no Asciidoctor |
|---|---|
| Linhas de atributo/âncora antes do `= Título` (com vazias, comentários e entradas de atributo entre eles) são do documento; `<body id>` | `parse_block_metadata_lines`, `parse_document_header` |
| `leveloffset=+1` (sinal explícito); `= Título` no corpo de `article` vira seção de nível 0 com erro, nunca descarte | `Helpers.int_from_str`; `initialize_section` |
| Include com `leveloffset` é cercado por linhas vazias (separam blocos na fronteira) | `push_include` |
| `tag`/`tags` com negação `!x`, curingas `*`/`**`, só negações = tudo menos | `preprocess_include_directive` |
| Include de URL sem `:allow-uri-read:` vira `link:URL[role=include]`; include ausente vira `Unresolved directive in <arq> - include::...[...]` | `resolve_include_path`, `replace_next_line` |
| Total de includes sem limite prático (o Asciidoctor só limita a profundidade): padrão 10.000 | `maxdepth` |
| Nome de atributo: `\w` seguido de qualquer coisa menos `:` (`:velero-1.12:`) | `AttributeEntryRx` |
| Linha de atributos de bloco: `^\[(|[\p{Word}.#%{,"'].*)\]$` (aceita `]]`) | `BlockAttributeLineRx` |
| Itens de lista: buffer do item com continuação ativa/congelada/destacada, irmão encerra, `skipped == 0` interrompe parágrafo em linha de lista, quirk de `$~` que zera `has_text` após dlist aninhada | `read_lines_for_list_item`, `parse_list_item`, `read_paragraph_lines` |
| Delimitador de tabela `|={3,}`; comentários `//` dentro da tabela somem; spec de célula exige espaço antes e número só com `*`/`+`; spec de coluna `[mult*][h][.v][largura|~][estilo]` | `parse_table`, `CellSpecEndRx`, `ColumnSpecRx` |
| Marcas constrained não abrem depois de `;:}` (e `<>&`, que já viraram entidades); `` ` `` nem depois de `"'` `` ` ``; `#` nem depois de `&` | `QUOTE_SUBS` |
| `[papel]` antes de marca = papel; `[x-]` = monoespaçado "old behavior" (literal com `` ` ``) | `parse_quoted_text_attributes`, `extract_passthroughs` |
| Passthrough (`pass:[]`, `+..+`) é indivisível ao procurar o fecho de `*`/`_`/`` ` `` | `extract_passthroughs` antes de `quotes` |
| Texto de link só tem atributos se contém `=` (vírgulas fazem parte do texto) | `InlineLinkRx`, `InlineLinkMacroRx` |
| `\https://...` escapa o link automático | `InlineLinkRx` |
| `--` no início/fim de linha também vira travessão | `REPLACEMENTS` |
| Quebra forçada ` +` só no fim da LINHA do texto (não dentro de `*...*`); no fim do texto, `<br>` sem quebra | `HardLineBreakRx` |
| `kbd`/`menu`: itens não se fundem | `InlineKbdBtnMacroRx` |
| `:icons:` (qualquer valor ≠ font) → `<img>` de ícone em admonição e callouts; com ícones o comentário antes do callout some; o valor vale no ponto do documento (playback) | `convert_admonition`, `convert_inline_callout`, `playback_attributes` |
| `toc::[]` só com `:toc: macro`, senão `<!-- toc disabled -->`; `:toc:`, `toc-title` e `toclevels` valem como no fim do cabeçalho | `convert_toc`, `finalize_header` |
| `[%collapsible]` → `<details>` sem número de legenda | `convert_example` |
| `subs="+quotes"` em listagem aplica as marcas | `apply_subs` |

## O que a casa modela além do ASG (draft-01) e como sai no ASG

| Nó da casa | No ASG |
|---|---|
| tabela | `{"name":"table","columns":n,"rows":[{"header":bool,"cells":[{"name":"cell","colspan","rowspan","inlines"\|"blocks"}]}]}` |
| nota de rodapé | `{"name":"footnote","type":"inline","number":n,"id"?,"inlines"}` |
| imagem inline | `{"name":"image","type":"inline","form":"macro","target"}` |
| quebra de linha forçada | texto `"\n"` |
| sup/sub | `span` com `variant` `superscript`/`subscript` |
| âncora, kbd, btn, menu, icon, indexterm | `{"name":<nome>,"type":"inline",...}` |
| callout em listing | `{"name":"callout","type":"string","value":"n"}` |

Esses nomes **não estão no esquema** `asg/schema.json` (draft-01), que ainda não cobre tabelas nem rodapés; quando
a especificação os definir, a saída muda para a forma oficial e esta tabela é atualizada.

## Não implementado nesta versão (0.1.0)

- DocBook 5 (`ADC_DOCBOOK5` devolve `ADC_ERRO_FORMATO` com mensagem).
- `rowspan` no PDF (as células com `rowspan` ocupam só a própria linha; a mescla sai certa no HTML).
- `toc::[]` no PDF (o PDF põe o sumário no início; no HTML ele sai no lugar do macro com `:toc: macro`, como no Asciidoctor; o Markdown omite com aviso).
- Divergências restantes no corpus real (547 documentos em 2026-10-24): log em `d:\tmp\AsciiDocConverter\validacao\rodada12.csv`;
  as mais frequentes são continuação `+` em casos raros de aninhamento, escapes com barra em contextos específicos e
  URLs com `<...>` (o Asciidoctor corta `&gt;` do link).
- Realce de sintaxe de código (nenhum `source-highlighter`; no HTML completo, o atributo gera aviso e nenhum script é injetado).
- Doctype `book` (partes), `compat-mode`, títulos setext, include por URL, STEM renderizado (sai como texto; no HTML completo, `:stem:` gera aviso e o MathJax não é injetado).
- No HTML completo: `:docinfo:` (exigiria ler `docinfo*.html` do disco) e `:stylesheet:` personalizada sem `:linkcss:`
  (idem) — ambos geram aviso; a folha padrão é embutida no lugar. Com `:linkcss:`, a referência sai como
  `<link href="{stylesdir}/{stylesheet}">`, como no Asciidoctor. `iconfont-remote` não é distinguido: com
  `icons=font` o link vai para `iconfont-cdn` se definido, senão para `{stylesdir}/{iconfont-name}.css` se
  `iconfont-name` estiver definido, senão para o CDN padrão do Font Awesome 4.7.0.
