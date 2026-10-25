<#
.SYNOPSIS
    Converte (ou valida) um arquivo .adoc, ou uma árvore deles, com a biblioteca nativa AsciiDocConverter.

.DESCRIPTION
    Carrega a DLL/.so de Dist\ (ou a que -Biblioteca indicar) e chama ADC_Converter uma vez por formato,
    HTML5 (documento completo), Markdown, PDF e ASG JSON, gravando ao lado de cada .adoc (ou sob -Saida):

        <nome>.html   <nome>.md   <nome>.pdf   <nome>.asg.json

    Sem -Recursivo, trata só o arquivo de entrada e exibe toda mensagem do processador.

    Com -Recursivo, trata vários documentos:
      Links   parte do arquivo de entrada e segue os links para outros documentos AsciiDoc (xref:outro.adoc[],
              xref:outro#id[], <<outro.adoc#id,...>>, link:outro.adoc[]), em largura, cada documento uma vez só.
              Os links são lidos do ASG que a própria biblioteca gera, então valem também os que vêm de include::.
      Pastas  todo .adoc da pasta de entrada (ou da pasta do arquivo de entrada) e das subpastas. Arquivos e pastas
              cujo nome começa com '_' são parciais por convenção e ficam de fora, salvo com -IncluirParciais.
    Com -Saida, a estrutura abaixo da raiz (a pasta da entrada) é reproduzida no destino.

    -Validar não grava as conversões: converte em memória e REPROVA o documento quando
      FALHA      ADC_Converter devolveu erro num formato;
      ERRO       o processador emitiu mensagem de nível erro;
      TRUNCADO   conteúdo do .adoc não chegou à saída SEM mensagem nenhuma: uma linha de conteúdo do documento
                 depois da última que o ASG cobre, ou um include entregue com conteúdo que não aparece no ASG.
                 Linhas sob ifdef/ifndef/ifeval, comentários, atributos e delimitadores não contam (o script não
                 sabe quais atributos estarão definidos; conteúdo condicional só é cobrado pelo -Oraculo);
      DIVERGE    (com -Oraculo) o texto do HTML difere do que o Asciidoctor 2.0.20 produz para o mesmo arquivo;
      VAZIO      (com -Oraculo) a biblioteca não produziu texto e o Asciidoctor produziu.
    Avisos, links quebrados e recursos negados são registrados, mas não reprovam.

    -Log grava TODO problema (inclusive avisos) num CSV para o Excel: separador ';', UTF-8 com BOM, uma linha por
    ocorrência (Documento;Categoria;Formato;Linha;Coluna;Mensagem). Vale com e sem -Validar.

    Recursos: a biblioteca não lê disco; este script entrega include:: e imagens pelo callback. Ela passa o alvo como
    escrito na diretiva, sem dizer de qual arquivo veio; como no Asciidoctor o include é relativo ao arquivo que o
    contém, o script procura o alvo a partir da pasta dos includes já entregues nesta conversão (do mais recente ao
    mais antigo) e, por fim, a partir da pasta do documento. Imagens: primeiro a pasta do documento.

    Código de saída: 1 se algum documento falhou (ou, com -Validar, foi reprovado); 0 caso contrário.
    Exige PowerShell 7 (pwsh), em Windows ou Linux: a ponte usa System.Runtime.InteropServices.NativeLibrary,
    que não existe no Windows PowerShell 5.1. O binário é escolhido pelo SO e pela arquitetura do processo.

.PARAMETER Entrada
    O arquivo .adoc (UTF-8). Com -Recursivo Pastas, pode ser também uma pasta.
.PARAMETER Recursivo
    Links (segue os links entre documentos a partir da entrada) ou Pastas (toda a árvore da pasta).
.PARAMETER Formatos
    Formatos a gerar (ou validar): html, md, pdf, asg (um ou vários). Padrão: todos. Também aceito como -Formato.
.PARAMETER Saida
    Diretório de saída. Padrão: ao lado de cada .adoc. Ignorado com -Validar (nada é gravado além do -Log).
.PARAMETER Validar
    Verifica cada documento sem gravar as conversões; ver a descrição. Reprovado = código de saída 1.
.PARAMETER Oraculo
    Pasta node_modules com @asciidoctor/core@3.0.4: compara o texto do HTML de cada documento com o do Asciidoctor
    (implica -Validar). Exige Node.js no PATH; usa ferramentas\oraculo-asciidoctor.js.
.PARAMETER Log
    Arquivo CSV com todos os problemas encontrados (sobrescrito a cada execução).
.PARAMETER ProfundidadeMaxima
    Com -Recursivo Links: quantos saltos de link seguir a partir da entrada (0 = sem limite).
.PARAMETER IncluirParciais
    Com -Recursivo Pastas: trata também arquivos e pastas iniciados por '_'.
.PARAMETER MostrarAvisos
    Exibe no console cada aviso, link quebrado e recurso negado (sem a chave, só contagens; o -Log tem todos).
.PARAMETER Biblioteca
    Caminho da nativa. Padrão: Dist\AsciiDocConverter.<so>-<arq>.<ext> deste repositório.
    Use obj\dev\AsciiDocConverter.dll para testar a compilação de desenvolvimento.
.PARAMETER Opcoes
    Arquivo JSON com as opções de ADC_Converter (atributos, markdown, pdf, limites). Aplicado a todos os
    formatos; "autonomo" é forçado a true para o HTML, salvo com -SoCorpoHTML.
.PARAMETER SoCorpoHTML
    HTML só com o corpo (sem <html>/<head>/CSS), como o padrão da biblioteca.

.EXAMPLE
    .\ferramentas\Converter-Adoc.ps1 .\test\casos\13-documento-completo.adoc -Saida $env:TEMP\adoc
.EXAMPLE
    .\ferramentas\Converter-Adoc.ps1 F:\AsciiDoc\Docs\index.adoc -Recursivo Links -Formato html
.EXAMPLE
    .\ferramentas\Converter-Adoc.ps1 F:\AsciiDoc\Docs -Recursivo Pastas -Validar -Formato html,asg -Log D:\tmp\validacao.csv
.EXAMPLE
    .\ferramentas\Converter-Adoc.ps1 F:\AsciiDoc\Docs -Recursivo Pastas -Oraculo D:\tmp\AsciiDocConverter\golden-tool\node_modules -Formato html -Log D:\tmp\oraculo.csv
#>
#requires -Version 7.0
[CmdletBinding()]
param(
    [Parameter(Mandatory, Position = 0)] [string] $Entrada,
    [ValidateSet('Links', 'Pastas')] [string] $Recursivo,
    [Alias('Formato')] [ValidateSet('html', 'md', 'pdf', 'asg')] [string[]] $Formatos = @('html', 'md', 'pdf', 'asg'),
    [string] $Saida,
    [switch] $Validar,
    [string] $Oraculo,
    [string] $Log,
    [ValidateRange(0, [int]::MaxValue)] [int] $ProfundidadeMaxima = 0,
    [switch] $IncluirParciais,
    [switch] $MostrarAvisos,
    [string] $Biblioteca,
    [string] $Opcoes,
    [switch] $SoCorpoHTML
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

# --- Entrada, raiz e validações dos parâmetros -----------------------------------------------------------------
$itemEntrada = Get-Item -LiteralPath $Entrada
if ($itemEntrada.PSIsContainer -and $Recursivo -ne 'Pastas') {
    throw "A entrada '$($itemEntrada.FullName)' é uma pasta. Para tratar a árvore inteira use -Recursivo Pastas; para seguir os links, informe o arquivo .adoc de partida com -Recursivo Links."
}
if ($ProfundidadeMaxima -gt 0 -and $Recursivo -ne 'Links') { throw "-ProfundidadeMaxima só vale com -Recursivo Links." }
if ($IncluirParciais -and $Recursivo -ne 'Pastas') { throw "-IncluirParciais só vale com -Recursivo Pastas." }
if ($Oraculo) { $Validar = [switch]::new($true) }
if ($Validar -and $Saida) { Write-Warning "-Saida é ignorado com -Validar: a validação não grava as conversões." ; $Saida = $null }
$raiz = if ($itemEntrada.PSIsContainer) { $itemEntrada.FullName } else { $itemEntrada.DirectoryName }
$raizSaida = if ($Saida) { (New-Item -ItemType Directory -Force -Path $Saida).FullName } else { $null }

# --- Biblioteca nativa ------------------------------------------------------------------------------------------
if (-not $Biblioteca) {
    $arq = switch ([System.Runtime.InteropServices.RuntimeInformation]::ProcessArchitecture) {
        'X64'   { 'x64' }
        'Arm64' { 'arm64' }
        default { throw "Arquitetura do processo sem binário em Dist: $_ (há x64 e arm64)." }
    }
    $nome = if ($IsWindows) { "AsciiDocConverter.win-$arq.dll" }
            elseif ($IsLinux) { "AsciiDocConverter.linux-$arq.so" }
            else { throw "Sistema operacional sem binário em Dist (há Windows e Linux)." }
    $Biblioteca = Join-Path (Split-Path $PSScriptRoot -Parent) 'Dist' $nome
}
if (-not (Test-Path -LiteralPath $Biblioteca)) {
    throw "Biblioteca nativa não encontrada: $Biblioteca. Gere-a com Build-AsciiDocConverter.ps1 (Dist) ou Compilar-Dev.ps1 (obj\dev)."
}
$Biblioteca = (Resolve-Path -LiteralPath $Biblioteca).Path

# --- Ponte P/Invoke e validador (C# compilado em memória; uma vez por sessão) -----------------------------------
if (-not ('AdcPonte3' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.IO;
using System.Net;
using System.Runtime.InteropServices;
using System.Text;
using System.Text.Json;
using System.Text.RegularExpressions;

public sealed class AdcPonte3 : IDisposable
{
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    public delegate int Resolver(IntPtr pCtx, int pTipo, IntPtr pCaminho, int pTamCaminho, out IntPtr pConteudo, out int pTamanho);
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    delegate int ConverterFn(IntPtr pEntrada, int pTamanho, int pFormato, IntPtr pOpcoes, Resolver pResolver, IntPtr pCtx,
                             out IntPtr pSaida, out int pTamSaida, out IntPtr pMensagens);
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    delegate void LiberarFn(IntPtr pMemoria);
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    delegate IntPtr VersaoFn();

    readonly IntPtr _lib;
    readonly ConverterFn _converter;
    readonly LiberarFn _liberar;
    readonly VersaoFn _versao;
    readonly Resolver _resolver;                         // mantido vivo durante toda a conversão
    readonly List<GCHandle> _presos = new List<GCHandle>();
    readonly List<string> _dirsDeInclude = new List<string>(); // pastas dos includes entregues nesta conversão

    // Pasta do documento corrente: base dos caminhos relativos. Trocar a cada documento.
    public string DirDocumento = "";
    public readonly List<string> RecursosLidos = new List<string>();
    public readonly List<string> RecursosNegados = new List<string>();
    // includes entregues na ÚLTIMA chamada de Converter (caminho como a biblioteca pediu, e o conteúdo)
    public readonly List<string> UltimosIncludesCaminhos = new List<string>();
    public readonly List<byte[]> UltimosIncludesConteudos = new List<byte[]>();

    public AdcPonte3(string pBiblioteca)
    {
        _lib = NativeLibrary.Load(pBiblioteca);
        _converter = Marshal.GetDelegateForFunctionPointer<ConverterFn>(NativeLibrary.GetExport(_lib, "ADC_Converter"));
        _liberar   = Marshal.GetDelegateForFunctionPointer<LiberarFn>(NativeLibrary.GetExport(_lib, "ADC_Liberar"));
        _versao    = Marshal.GetDelegateForFunctionPointer<VersaoFn>(NativeLibrary.GetExport(_lib, "ADC_Versao"));
        _resolver = ResolverNoDisco;
    }

    public string Versao() { return Marshal.PtrToStringUTF8(_versao()) ?? "(sem versão)"; }

    public void LimparRegistro() { RecursosLidos.Clear(); RecursosNegados.Clear(); }

    // Ordem de busca: include = pastas dos includes já entregues (mais recente primeiro), depois o documento;
    // imagem = documento primeiro, depois os includes. O primeiro arquivo existente vale.
    string Localizar(int pTipo, string pCaminho)
    {
        if (Path.IsPathRooted(pCaminho)) return File.Exists(pCaminho) ? pCaminho : null;
        var bases = new List<string>();
        if (pTipo == 2) bases.Add(DirDocumento);
        for (int i = _dirsDeInclude.Count - 1; i >= 0; i--) if (!bases.Contains(_dirsDeInclude[i])) bases.Add(_dirsDeInclude[i]);
        if (!bases.Contains(DirDocumento)) bases.Add(DirDocumento);
        foreach (string b in bases)
        {
            string completo = Path.GetFullPath(Path.Combine(b, pCaminho));
            if (File.Exists(completo)) return completo;
        }
        return null;
    }

    int ResolverNoDisco(IntPtr pCtx, int pTipo, IntPtr pCaminho, int pTamCaminho, out IntPtr pConteudo, out int pTamanho)
    {
        pConteudo = IntPtr.Zero; pTamanho = 0;
        string caminho = Marshal.PtrToStringUTF8(pCaminho, pTamCaminho);
        try
        {
            if (caminho.Contains("://")) { RecursosNegados.Add(caminho + " (URL: o script só lê arquivos locais)"); return 3; }
            string completo = Localizar(pTipo, caminho);
            if (completo == null) { RecursosNegados.Add(caminho + " (não encontrado a partir de " + DirDocumento + " nem das pastas dos includes)"); return 2; }
            byte[] bytes = File.ReadAllBytes(completo);
            if (pTipo == 1 && bytes.Length >= 3 && bytes[0] == 0xEF && bytes[1] == 0xBB && bytes[2] == 0xBF)
            {
                byte[] sem = new byte[bytes.Length - 3];
                Array.Copy(bytes, 3, sem, 0, sem.Length);
                bytes = sem;
            }
            GCHandle h = GCHandle.Alloc(bytes, GCHandleType.Pinned);
            _presos.Add(h);
            pConteudo = h.AddrOfPinnedObject(); pTamanho = bytes.Length;
            if (pTipo == 1)
            {
                _dirsDeInclude.Add(Path.GetDirectoryName(completo));
                UltimosIncludesCaminhos.Add(caminho);
                UltimosIncludesConteudos.Add(bytes);
            }
            RecursosLidos.Add(caminho);
            return 0;
        }
        catch (Exception e) { RecursosNegados.Add(caminho + " (" + e.Message + ")"); return 2; }
    }

    public sealed class Resultado
    {
        public int Codigo;          // ADC_Resultado (0 = OK)
        public byte[] Saida;        // vazio quando Codigo != 0
        public string Mensagens;    // JSON: [{nivel, linha, coluna, texto}], também no sucesso
    }

    public Resultado Converter(byte[] pEntrada, int pFormato, string pOpcoesJSON)
    {
        byte[] opcoes = pOpcoesJSON == null ? null : Encoding.UTF8.GetBytes(pOpcoesJSON + "\0");
        GCHandle hEntrada = GCHandle.Alloc(pEntrada, GCHandleType.Pinned);
        GCHandle hOpcoes = opcoes == null ? default(GCHandle) : GCHandle.Alloc(opcoes, GCHandleType.Pinned);
        IntPtr saida = IntPtr.Zero, msgs = IntPtr.Zero;
        _dirsDeInclude.Clear();
        UltimosIncludesCaminhos.Clear();
        UltimosIncludesConteudos.Clear();
        try
        {
            int rc = _converter(hEntrada.AddrOfPinnedObject(), pEntrada.Length, pFormato,
                                opcoes == null ? IntPtr.Zero : hOpcoes.AddrOfPinnedObject(), _resolver, IntPtr.Zero,
                                out saida, out int tam, out msgs);
            Resultado r = new Resultado();
            r.Codigo = rc;
            r.Mensagens = msgs == IntPtr.Zero ? "[]" : (Marshal.PtrToStringUTF8(msgs) ?? "[]");
            r.Saida = new byte[rc == 0 ? tam : 0];
            if (rc == 0 && tam > 0) Marshal.Copy(saida, r.Saida, 0, tam);
            return r;
        }
        finally
        {
            if (saida != IntPtr.Zero) _liberar(saida);
            if (msgs != IntPtr.Zero) _liberar(msgs);
            hEntrada.Free();
            if (opcoes != null) hOpcoes.Free();
            foreach (GCHandle h in _presos) h.Free();
            _presos.Clear();
        }
    }

    public void Dispose() { if (_lib != IntPtr.Zero) NativeLibrary.Free(_lib); }
}

public static class AdcValidador
{
    // --- ASG: refs, cobertura de linhas do documento principal e arquivos incluídos ---------------------------

    public sealed class InfoASG
    {
        public List<string> Refs = new List<string>();          // "variante\talvo"
        public int UltimaLinhaPrincipal;                          // maior linha do documento principal coberta
        public HashSet<string> Arquivos = new HashSet<string>(); // valores de "file" (includes que produziram nós)
    }

    public static InfoASG LerASG(byte[] pASG)
    {
        var info = new InfoASG();
        // listas e blocos aninhados passam fácil dos 64 níveis padrão do System.Text.Json
        using (JsonDocument doc = JsonDocument.Parse(pASG, new JsonDocumentOptions { MaxDepth = 4096 }))
        {
            var pilha = new Stack<JsonElement>();
            pilha.Push(doc.RootElement);
            while (pilha.Count > 0)
            {
                JsonElement e = pilha.Pop();
                if (e.ValueKind == JsonValueKind.Array)
                {
                    var filhos = new List<JsonElement>();
                    foreach (JsonElement f in e.EnumerateArray()) filhos.Add(f);
                    for (int i = filhos.Count - 1; i >= 0; i--) pilha.Push(filhos[i]);
                    continue;
                }
                if (e.ValueKind != JsonValueKind.Object) continue;
                JsonElement nome, alvo, variante, local;
                if (e.TryGetProperty("name", out nome) && nome.ValueKind == JsonValueKind.String && nome.GetString() == "ref"
                    && e.TryGetProperty("target", out alvo) && alvo.ValueKind == JsonValueKind.String)
                {
                    string v = e.TryGetProperty("variant", out variante) && variante.ValueKind == JsonValueKind.String ? variante.GetString() : "xref";
                    info.Refs.Add(v + "\t" + alvo.GetString());
                }
                if (e.TryGetProperty("location", out local) && local.ValueKind == JsonValueKind.Array)
                {
                    foreach (JsonElement ponto in local.EnumerateArray())
                    {
                        if (ponto.ValueKind != JsonValueKind.Object) continue;
                        JsonElement arq, lin;
                        if (ponto.TryGetProperty("file", out arq))
                        {
                            if (arq.ValueKind == JsonValueKind.Array) { foreach (JsonElement a in arq.EnumerateArray()) if (a.ValueKind == JsonValueKind.String) info.Arquivos.Add(a.GetString()); }
                            else if (arq.ValueKind == JsonValueKind.String) info.Arquivos.Add(arq.GetString());
                        }
                        else if (ponto.TryGetProperty("line", out lin) && lin.ValueKind == JsonValueKind.Number)
                        {
                            int n = lin.GetInt32();
                            if (n > info.UltimaLinhaPrincipal) info.UltimaLinhaPrincipal = n;
                        }
                    }
                }
                var filhos2 = new List<JsonElement>();
                foreach (JsonProperty p in e.EnumerateObject())
                    if (p.Name != "location" && (p.Value.ValueKind == JsonValueKind.Array || p.Value.ValueKind == JsonValueKind.Object)) filhos2.Add(p.Value);
                for (int i = filhos2.Count - 1; i >= 0; i--) pilha.Push(filhos2[i]);
            }
        }
        return info;
    }

    // --- Linhas de conteúdo de um arquivo AsciiDoc -----------------------------------------------------------
    // "Significativa" = linha que TEM de virar algo na saída, qualquer que seja o valor dos atributos. Ficam de fora:
    // vazias, comentários, diretivas e tudo sob ifdef/ifndef/ifeval (pode estar excluído), entradas de atributo,
    // linhas de atributo de bloco/âncora, títulos de bloco, delimitadores, continuação (+), toc::[], include::
    // (tratado à parte) e as linhas de autor/revisão logo abaixo do título do documento.
    static readonly Regex ReCondicional = new Regex(@"^(ifdef|ifndef|ifeval)::[^\[]*\[(.*)\]\s*$", RegexOptions.Compiled);
    // a mesma do Asciidoctor (AttributeEntryRx): depois do 1º caractere de palavra, qualquer coisa menos ':' (velero-1.12 vale)
    static readonly Regex ReAtributo = new Regex(@"^:!?\w[^:]*:(\s.*)?$", RegexOptions.Compiled);
    static readonly Regex ReDelimitador = new Regex(@"^(-{4,}|={4,}|\*{4,}|\.{4,}|_{4,}|\+{4,}|--|\|={3,}|,={3,}|:={3,}|!={3,}|`{3,}.*)$", RegexOptions.Compiled);
    static readonly Regex ReVerbatim = new Regex(@"^(-{4,}|\.{4,}|\+{4,}|`{3,}.*)$", RegexOptions.Compiled);
    static readonly Regex ReInclude = new Regex(@"^include::([^\[]+)\[", RegexOptions.Compiled);

    // ifeval::[expr] sempre abre bloco (a expressão fica nos colchetes); ifdef/ifndef::x[] abre bloco e
    // ifdef/ifndef::x[texto] é a forma de uma linha só.
    static bool AbreBloco(Match pM)
    {
        if (!pM.Success) return false;
        return pM.Groups[1].Value == "ifeval" || pM.Groups[2].Value.Length == 0;
    }

    public sealed class Linhas
    {
        public string[] Texto;
        public bool[] Significativa;
        public List<string> IncludesEmVerbatim = new List<string>(); // nomes de arquivo incluídos dentro de ----/..../++++
    }

    public static Linhas Classificar(byte[] pConteudo)
    {
        string s = Encoding.UTF8.GetString(pConteudo);
        if (s.Length > 0 && s[0] == '\uFEFF') s = s.Substring(1);
        var r = new Linhas();
        r.Texto = s.Replace("\r\n", "\n").Split('\n');
        r.Significativa = new bool[r.Texto.Length];
        bool comentario = false, cabecalho = false, viuConteudo = false;
        int cond = 0;
        string verbatim = null;
        for (int i = 0; i < r.Texto.Length; i++)
        {
            string t = r.Texto[i].Trim();
            if (comentario) { if (t.Length >= 4 && t.TrimEnd('/').Length == 0) comentario = false; continue; }
            if (verbatim != null)
            {
                if (t == verbatim) { verbatim = null; continue; }
                Match mi = ReInclude.Match(t);
                if (mi.Success) { string alvo = mi.Groups[1].Value; int k = alvo.LastIndexOf('}'); r.IncludesEmVerbatim.Add(Path.GetFileName(k >= 0 ? alvo.Substring(k + 1) : alvo)); continue; }
                if (cond == 0 && t.Length > 0 && !t.StartsWith("ifdef::") && !t.StartsWith("ifndef::") && !t.StartsWith("ifeval::") && !t.StartsWith("endif::")) { r.Significativa[i] = true; viuConteudo = true; }
                else if (t.StartsWith("ifdef::") || t.StartsWith("ifndef::") || t.StartsWith("ifeval::")) { if (AbreBloco(ReCondicional.Match(t))) cond++; }
                else if (t.StartsWith("endif::") && cond > 0) cond--;
                continue;
            }
            if (t.Length == 0) { cabecalho = false; continue; }
            if (t.Length >= 4 && t.TrimEnd('/').Length == 0) { comentario = true; continue; }
            if (t.StartsWith("//")) continue;
            Match mc = ReCondicional.Match(t);
            if (mc.Success) { if (AbreBloco(mc)) cond++; continue; }
            if (t.StartsWith("endif::")) { if (cond > 0) cond--; continue; }
            if (cond > 0) continue;
            if (cabecalho) continue;
            if (t.StartsWith("include::")) continue;
            if (ReAtributo.IsMatch(t)) continue;
            if (t.StartsWith("[") && t.EndsWith("]")) continue;
            if (ReVerbatim.IsMatch(t)) { verbatim = t; continue; }
            if (ReDelimitador.IsMatch(t)) continue;
            if (t == "+" || t.StartsWith("toc::[")) continue;
            if (t.Length > 1 && t[0] == '.' && t[1] != '.' && t[1] != ' ') continue;
            if (!viuConteudo && t.StartsWith("= ")) { r.Significativa[i] = true; viuConteudo = true; cabecalho = true; continue; }
            viuConteudo = true;
            r.Significativa[i] = true;
        }
        return r;
    }

    // Truncamento silencioso: devolve "linha\tmensagem" para cada indício (vazio = nada perdido).
    public static List<string> Truncamento(byte[] pFonte, InfoASG pASG, List<string> pIncludes, List<byte[]> pConteudos)
    {
        var problemas = new List<string>();
        Linhas L = Classificar(pFonte);
        for (int i = 0; i < L.Texto.Length; i++)
        {
            if (!L.Significativa[i] || i + 1 <= pASG.UltimaLinhaPrincipal) continue;
            int restantes = 0;
            for (int k = i; k < L.Texto.Length; k++) if (L.Significativa[k]) restantes++;
            string trecho = L.Texto[i].Trim();
            if (trecho.Length > 80) trecho = trecho.Substring(0, 80) + "...";
            problemas.Add((i + 1) + "\tA saída termina na linha " + pASG.UltimaLinhaPrincipal + " do documento; " + restantes +
                " linha(s) de conteúdo a partir da linha " + (i + 1) + " não chegaram à saída, sem mensagem do processador. Primeira: \"" + trecho + "\"");
            break;
        }
        // includes feitos dentro de bloco literal (----, ...., ++++) não têm nó próprio no ASG: junta os nomes de TODOS
        // os arquivos entregues (o include pode estar num módulo aninhado), antes de cobrar qualquer um
        var verbatim = new HashSet<string>(L.IncludesEmVerbatim, StringComparer.OrdinalIgnoreCase);
        var classificados = new List<Linhas>();
        for (int j = 0; j < pIncludes.Count; j++)
        {
            Linhas I = Classificar(pConteudos[j]);
            classificados.Add(I);
            foreach (string v in I.IncludesEmVerbatim) verbatim.Add(v);
        }
        var vistos = new HashSet<string>();
        for (int j = 0; j < pIncludes.Count; j++)
        {
            string p = pIncludes[j];
            if (!vistos.Add(p) || pASG.Arquivos.Contains(p)) continue;
            Linhas I = classificados[j];
            if (verbatim.Contains(Path.GetFileName(p))) continue;     // incluído dentro de bloco literal: sem nó próprio
            int sig = 0, primeira = -1;
            for (int k = 0; k < I.Texto.Length; k++) if (I.Significativa[k]) { sig++; if (primeira < 0) primeira = k; }
            if (sig == 0) continue;                                    // só atributos/condicionais: não gera nó
            string trecho = I.Texto[primeira].Trim();
            if (trecho.Length > 80) trecho = trecho.Substring(0, 80) + "...";
            problemas.Add("0\tO include '" + p + "' foi entregue com " + sig + " linha(s) de conteúdo, mas nada dele chegou à saída, sem mensagem do processador. Primeira: \"" + trecho + "\"");
        }
        return problemas;
    }

    // --- Comparação com o oráculo ----------------------------------------------------------------------------
    static readonly Regex ReTag = new Regex(@"<[^>]*>", RegexOptions.Compiled);
    static readonly Regex ReEspacos = new Regex(@"\s+", RegexOptions.Compiled);

    public static string TextoDoHtml(string pHtml)
    {
        string s = ReTag.Replace(pHtml ?? "", " ");
        s = WebUtility.HtmlDecode(s).Replace("\u200B", "");
        return ReEspacos.Replace(s, " ").Trim();
    }

    // null = textos iguais; senão a descrição da primeira diferença.
    public static string Comparar(string pNosso, string pOraculo)
    {
        if (pNosso == pOraculo) return null;
        int n = Math.Min(pNosso.Length, pOraculo.Length), i = 0;
        while (i < n && pNosso[i] == pOraculo[i]) i++;
        int ini = Math.Max(0, i - 30);
        string a = pNosso.Substring(ini, Math.Min(90, pNosso.Length - ini));
        string b = pOraculo.Substring(ini, Math.Min(90, pOraculo.Length - ini));
        return "Texto difere a partir do caractere " + i + " (biblioteca " + pNosso.Length + " caracteres, Asciidoctor " + pOraculo.Length +
               "). Biblioteca: \"" + a + "\" | Asciidoctor: \"" + b + "\"";
    }
}
'@
}

$nomeDoCodigo = @{
    1 = 'ADC_ERRO_PARAMETRO (parâmetro inválido)'
    2 = 'ADC_ERRO_ENTRADA (não é UTF-8 sem BOM)'
    3 = 'ADC_ERRO_FORMATO (formato não implementado nesta versão)'
    4 = 'ADC_ERRO_LIMITE (limite excedido)'
    5 = 'ADC_ERRO_OPCOES (JSON de opções inválido)'
    6 = 'ADC_ERRO_CONVERSAO (o renderizador não produziu a saída)'
    7 = 'ADC_ERRO_MEMORIA'
}
$categoriasReprovam = @('FALHA', 'ERRO', 'TRUNCADO', 'DIVERGE', 'VAZIO')

# --- Opções: um JSON por formato (o HTML ganha "autonomo") -------------------------------------------------------
$opcoesBase = [ordered]@{}
if ($Opcoes) {
    $textoOpcoes = [System.IO.File]::ReadAllText((Resolve-Path -LiteralPath $Opcoes).Path, [System.Text.UTF8Encoding]::new($false))
    try { $objeto = $textoOpcoes | ConvertFrom-Json -AsHashtable }
    catch { throw "O arquivo de opções '$Opcoes' não é JSON válido: $($_.Exception.Message)" }
    foreach ($chave in $objeto.Keys) { $opcoesBase[$chave] = $objeto[$chave] }
}
$script:arquivoAtual = $null
function Opcoes-Para([string] $pFormato) {
    $o = [ordered]@{}
    foreach ($chave in $opcoesBase.Keys) { $o[$chave] = $opcoesBase[$chave] }
    if ($script:arquivoAtual) {
        # atributos intrínsecos que o Asciidoctor define ao converter um arquivo; os das opções prevalecem
        $at = [ordered]@{}
        if ($o.Contains('atributos') -and $o['atributos']) { foreach ($k in $o['atributos'].Keys) { $at[$k] = $o['atributos'][$k] } }
        $arqAtual = $script:arquivoAtual
        $intrinsecos = [ordered]@{ docfile = $arqAtual.FullName; docdir = $arqAtual.DirectoryName; docname = [System.IO.Path]::GetFileNameWithoutExtension($arqAtual.Name); docfilesuffix = $arqAtual.Extension }
        foreach ($k in $intrinsecos.Keys) { if (-not $at.Contains($k)) { $at[$k] = $intrinsecos[$k] } }
        $o['atributos'] = $at
    }
    if ($pFormato -eq 'html') { $o['autonomo'] = -not $SoCorpoHTML }
    if ($pFormato -eq 'fragmento') { $o['autonomo'] = $false }
    if ($o.Count -eq 0) { return $null }
    return ($o | ConvertTo-Json -Depth 20 -Compress)
}

$tabela = @(
    @{ Chave = 'html'; Formato = 1; Nome = 'HTML5';    Extensao = '.html'     }
    @{ Chave = 'md';   Formato = 2; Nome = 'Markdown'; Extensao = '.md'       }
    @{ Chave = 'pdf';  Formato = 3; Nome = 'PDF';      Extensao = '.pdf'      }
    @{ Chave = 'asg';  Formato = 4; Nome = 'ASG JSON'; Extensao = '.asg.json' }
) | Where-Object { $Formatos -contains $_.Chave }

# --- Log CSV (Excel pt-BR: ';', UTF-8 com BOM, RFC 4180) ---------------------------------------------------------
$logW = $null
if ($Log) {
    $caminhoLog = [System.IO.Path]::GetFullPath($Log)
    [void][System.IO.Directory]::CreateDirectory([System.IO.Path]::GetDirectoryName($caminhoLog))
    $logW = [System.IO.StreamWriter]::new($caminhoLog, $false, [System.Text.UTF8Encoding]::new($true))
    $logW.WriteLine('Documento;Categoria;Formato;Linha;Coluna;Mensagem')
}
function Campo-CSV([object] $pValor) {
    $s = [string]$pValor
    if ($s.IndexOfAny([char[]]@(';', '"', "`n", "`r")) -ge 0) { return '"' + $s.Replace('"', '""') + '"' }
    return $s
}
function Registrar([string] $pDocumento, [string] $pCategoria, [string] $pFormato, [int] $pLinha, [int] $pColuna, [string] $pMensagem) {
    if ($logW) { $logW.WriteLine((@($pDocumento, $pCategoria, $pFormato, $pLinha, $pColuna, $pMensagem) | ForEach-Object { Campo-CSV $_ }) -join ';') }
}

# --- Oráculo (Asciidoctor.js num processo Node persistente) ------------------------------------------------------
$oraculoProc = $null
if ($Oraculo) {
    $node = Get-Command node -ErrorAction SilentlyContinue
    if (-not $node) { throw "-Oraculo exige o Node.js no PATH (node não encontrado)." }
    $modulos = (Resolve-Path -LiteralPath $Oraculo).Path
    if (-not (Test-Path -LiteralPath (Join-Path $modulos '@asciidoctor\core'))) { throw "-Oraculo: '$modulos' não contém @asciidoctor/core (instale @asciidoctor/core@3.0.4 nessa pasta)." }
    $psi = [System.Diagnostics.ProcessStartInfo]::new($node.Source)
    $psi.ArgumentList.Add((Join-Path $PSScriptRoot 'oraculo-asciidoctor.js'))
    $psi.ArgumentList.Add($modulos)
    $psi.RedirectStandardInput = $true; $psi.RedirectStandardOutput = $true; $psi.RedirectStandardError = $true
    $psi.UseShellExecute = $false
    $psi.StandardOutputEncoding = [System.Text.UTF8Encoding]::new($false)
    $psi.StandardInputEncoding = [System.Text.UTF8Encoding]::new($false)
    $oraculoProc = [System.Diagnostics.Process]::Start($psi)
    $oraculoErro = $oraculoProc.StandardError.ReadToEndAsync()
    $primeira = $oraculoProc.StandardOutput.ReadLine()
    if (-not $primeira) { throw "-Oraculo: o processo Node terminou sem responder. Saída de erro: $($oraculoErro.Result)" }
    $versaoOraculo = ($primeira | ConvertFrom-Json).pronto
}
function Html-Do-Oraculo([string] $pArquivo) {
    $oraculoProc.StandardInput.WriteLine((@{ arquivo = $pArquivo; raiz = $raiz } | ConvertTo-Json -Compress))
    $oraculoProc.StandardInput.Flush()
    $linha = $oraculoProc.StandardOutput.ReadLine()
    if ($null -eq $linha) { throw "O oráculo (Node) terminou durante '$pArquivo'. Saída de erro: $($oraculoErro.Result)" }
    return ($linha | ConvertFrom-Json)
}

# --- Links entre documentos -------------------------------------------------------------------------------------
# Regra do Asciidoctor: no xref, a parte antes de '#' é outro documento (sem extensão = .adoc); sem '#', só quando
# termina em .adoc (senão é um id deste documento). No link:, só alvo relativo com extensão AsciiDoc.
$extensoesAsciiDoc = @('.adoc', '.asciidoc', '.asc', '.ad')
function Destino-Do-Link([string] $pDirDocumento, [string] $pVariante, [string] $pAlvo) {
    if ($pAlvo -match '^[A-Za-z][A-Za-z0-9+.-]*:' ) { return $null }   # URL, mailto:, C:\...
    $hash = $pAlvo.IndexOf('#')
    $caminho = if ($hash -ge 0) { $pAlvo.Substring(0, $hash) } else { $pAlvo }
    if ($caminho.Length -eq 0) { return $null }
    $ext = [System.IO.Path]::GetExtension($caminho).ToLowerInvariant()
    if ($pVariante -eq 'xref') {
        if ($hash -lt 0 -and $ext -ne '.adoc') { return $null }
        if ($ext -eq '') { $caminho += '.adoc' }
        elseif ($extensoesAsciiDoc -notcontains $ext) { return $null }
    }
    else {
        if ($extensoesAsciiDoc -notcontains $ext) { return $null }
    }
    return [System.IO.Path]::GetFullPath([System.IO.Path]::Combine($pDirDocumento, $caminho))
}

# --- Um documento -----------------------------------------------------------------------------------------------
$ponte = [AdcPonte3]::new($Biblioteca)
$comparador = if ($IsWindows) { [System.StringComparer]::OrdinalIgnoreCase } else { [System.StringComparer]::Ordinal }

function Novo-Problema([string] $pCategoria, [string] $pFormato, [int] $pLinha, [int] $pColuna, [string] $pMensagem) {
    return [pscustomobject]@{ Categoria = $pCategoria; Formato = $pFormato; Linha = $pLinha; Coluna = $pColuna; Mensagem = $pMensagem }
}

# Converte (e, com -Validar, verifica) um .adoc. $pDetalhado = modo de arquivo único (uma linha por formato).
function Tratar-Documento([System.IO.FileInfo] $pArquivo, [string] $pDirSaida, [bool] $pDetalhado, [bool] $pQuerRefs) {
    $r = [ordered]@{ Formatos = [System.Collections.Generic.List[string]]::new(); Refs = $null; Problemas = [System.Collections.Generic.List[object]]::new() }
    $bytes = [System.IO.File]::ReadAllBytes($pArquivo.FullName)
    if ($bytes.Length -ge 3 -and $bytes[0] -eq 0xEF -and $bytes[1] -eq 0xBB -and $bytes[2] -eq 0xBF) {
        $bytes = $bytes[3..($bytes.Length - 1)]
        $r.Problemas.Add((Novo-Problema 'AVISO' '' 0 0 'O arquivo tem BOM UTF-8, que a biblioteca recusa; o BOM foi removido antes de converter.'))
    }
    $base = [System.IO.Path]::GetFileNameWithoutExtension($pArquivo.Name)
    $ponte.DirDocumento = $pArquivo.DirectoryName
    $script:arquivoAtual = $pArquivo
    $ponte.LimparRegistro()
    if ($pDetalhado) {
        Write-Host "$($ponte.Versao())  |  $Biblioteca" -ForegroundColor DarkGray
        Write-Host "Entrada: $($pArquivo.FullName) ($($bytes.Length) bytes)" -ForegroundColor DarkGray
    }
    $asg = $null; $incCaminhos = $null; $incConteudos = $null
    foreach ($f in $tabela) {
        $cronometro = [System.Diagnostics.Stopwatch]::StartNew()
        $c = $ponte.Converter($bytes, $f.Formato, (Opcoes-Para $f.Chave))
        $cronometro.Stop()
        if ($f.Formato -eq 4 -and $c.Codigo -eq 0) { $asg = $c.Saida; $incCaminhos = $ponte.UltimosIncludesCaminhos.ToArray(); $incConteudos = $ponte.UltimosIncludesConteudos.ToArray() }
        foreach ($m in @($c.Mensagens | ConvertFrom-Json)) {
            $r.Problemas.Add((Novo-Problema $(if ($m.nivel -eq 'erro') { 'ERRO' } else { 'AVISO' }) $f.Nome ([int]$m.linha) ([int]$m.coluna) $m.texto))
        }
        if ($c.Codigo -eq 0) {
            $r.Formatos.Add($f.Chave)
            if (-not $Validar) {
                $destino = Join-Path $pDirSaida ($base + $f.Extensao)
                [System.IO.File]::WriteAllBytes($destino, $c.Saida)
                if ($pDetalhado) { Write-Host ("OK   {0,-9} {1,9:n0} bytes  {2,6:n1} ms  -> {3}" -f $f.Nome, $c.Saida.Length, $cronometro.Elapsed.TotalMilliseconds, $destino) -ForegroundColor Green }
            }
            elseif ($pDetalhado) { Write-Host ("OK   {0,-9} {1,9:n0} bytes  {2,6:n1} ms  (validação: não gravado)" -f $f.Nome, $c.Saida.Length, $cronometro.Elapsed.TotalMilliseconds) -ForegroundColor Green }
        }
        else {
            $descricao = if ($nomeDoCodigo.ContainsKey($c.Codigo)) { $nomeDoCodigo[$c.Codigo] } else { 'código desconhecido' }
            $r.Problemas.Add((Novo-Problema 'FALHA' $f.Nome 0 0 ("ADC_Converter devolveu {0}: {1}" -f $c.Codigo, $descricao)))
            if ($pDetalhado) { Write-Host ("FALHA {0,-9} ADC_Converter devolveu {1}: {2}" -f $f.Nome, $c.Codigo, $descricao) -ForegroundColor Red }
        }
    }
    if (($pQuerRefs -or $Validar) -and -not $asg) {
        # o ASG não foi pedido: gera só para ler links e cobertura, sem gravar
        $c = $ponte.Converter($bytes, 4, (Opcoes-Para 'asg'))
        if ($c.Codigo -eq 0) { $asg = $c.Saida; $incCaminhos = $ponte.UltimosIncludesCaminhos.ToArray(); $incConteudos = $ponte.UltimosIncludesConteudos.ToArray() }
        else { $r.Problemas.Add((Novo-Problema 'FALHA' 'ASG JSON' 0 0 "ADC_Converter devolveu $($c.Codigo) ao gerar o ASG usado para links e validação; este documento não foi verificado.")) }
    }
    if ($asg) {
        $info = [AdcValidador]::LerASG($asg)
        if ($pQuerRefs) { $r.Refs = $info.Refs }
        if ($Validar) {
            $lista = [System.Collections.Generic.List[string]]::new([string[]]$incCaminhos)
            $conteudos = [System.Collections.Generic.List[byte[]]]::new([byte[][]]$incConteudos)
            foreach ($t in [AdcValidador]::Truncamento($bytes, $info, $lista, $conteudos)) {
                $partes = $t -split "`t", 2
                $r.Problemas.Add((Novo-Problema 'TRUNCADO' 'ASG JSON' ([int]$partes[0]) 0 $partes[1]))
            }
        }
    }
    if ($oraculoProc) {
        $nosso = $ponte.Converter($bytes, 1, (Opcoes-Para 'fragmento'))
        $resposta = Html-Do-Oraculo $pArquivo.FullName
        if ($resposta.PSObject.Properties['erro']) {
            $r.Problemas.Add((Novo-Problema 'AVISO' 'HTML5' 0 0 "O Asciidoctor (oráculo) não converteu o arquivo: $($resposta.erro); comparação não feita."))
        }
        elseif ($nosso.Codigo -eq 0) {
            $textoNosso = [AdcValidador]::TextoDoHtml([System.Text.Encoding]::UTF8.GetString($nosso.Saida))
            $textoOraculo = [AdcValidador]::TextoDoHtml([string]$resposta.html)
            $diferenca = [AdcValidador]::Comparar($textoNosso, $textoOraculo)
            if ($diferenca) {
                $categoria = if ($textoNosso.Length -eq 0 -and $textoOraculo.Length -gt 0) { 'VAZIO' } else { 'DIVERGE' }
                $r.Problemas.Add((Novo-Problema $categoria 'HTML5' 0 0 $diferenca))
            }
        }
    }
    foreach ($n in ($ponte.RecursosNegados | Select-Object -Unique)) { $r.Problemas.Add((Novo-Problema 'RECURSO_NEGADO' '' 0 0 $n)) }
    if ($pDetalhado) {
        foreach ($p in $r.Problemas) {
            $posicao = if ($p.Linha -gt 0) { " [{0}:{1}]" -f $p.Linha, $p.Coluna } else { '' }
            $cor = if ($categoriasReprovam -contains $p.Categoria) { 'Red' } else { 'Yellow' }
            if ($p.Categoria -ne 'FALHA') { Write-Host ("      {0}{1} {2}{3}" -f $p.Categoria.ToLowerInvariant(), $posicao, $(if ($p.Formato) { "($($p.Formato)) " } else { '' }), $p.Mensagem) -ForegroundColor $cor }
        }
        if ($ponte.RecursosLidos.Count -gt 0) {
            Write-Host ("Recursos lidos: {0}" -f (($ponte.RecursosLidos | Select-Object -Unique) -join ', ')) -ForegroundColor DarkGray
        }
    }
    return $r
}

# --- Fila de documentos -----------------------------------------------------------------------------------------
function Caminho-Relativo([string] $pCaminho) { return [System.IO.Path]::GetRelativePath($raiz, $pCaminho) }
function Fora-Da-Raiz([string] $pRelativo) { return $pRelativo -eq '..' -or $pRelativo.StartsWith('..' + [System.IO.Path]::DirectorySeparatorChar) -or $pRelativo.StartsWith('../') -or [System.IO.Path]::IsPathRooted($pRelativo) }

$fila = [System.Collections.Generic.Queue[object]]::new()
$vistos = [System.Collections.Generic.HashSet[string]]::new($comparador)
$parciais = 0
if ($Recursivo -eq 'Pastas') {
    $todos = Get-ChildItem -LiteralPath $raiz -Recurse -File -Filter '*.adoc' | Sort-Object FullName
    foreach ($a in $todos) {
        if (-not $IncluirParciais) {
            $segmentos = (Caminho-Relativo $a.FullName) -split '[\\/]'
            if (@($segmentos | Where-Object { $_.StartsWith('_') }).Count -gt 0) { $parciais++; continue }
        }
        [void]$vistos.Add($a.FullName)
        $fila.Enqueue(@($a.FullName, 0))
    }
}
else {
    [void]$vistos.Add($itemEntrada.FullName)
    $fila.Enqueue(@($itemEntrada.FullName, 0))
}
$totalInicial = $fila.Count
$detalhado = -not $Recursivo

$stats = [ordered]@{ Documentos = 0; Reprovados = 0; Arquivos = 0; ForaDaRaiz = 0; AlemDaProfundidade = 0 }
$porCategoria = [ordered]@{}
$docsPorCategoria = @{}
$tipos = @{}
$quebrados = [System.Collections.Generic.List[string]]::new()
$foraDaRaiz = [System.Collections.Generic.List[string]]::new()
$relogio = [System.Diagnostics.Stopwatch]::StartNew()
if (-not $detalhado) {
    Write-Host "$($ponte.Versao())  |  $Biblioteca" -ForegroundColor DarkGray
    $acao = if ($Validar) { 'Validação' } else { 'Conversão' }
    Write-Host ("{0} recursiva por {1} a partir de {2}; formatos: {3}; {4}" -f $acao, $Recursivo.ToLowerInvariant(), $itemEntrada.FullName, ($Formatos -join ', '), $(if ($Validar) { 'nada é gravado' } elseif ($raizSaida) { "saída: $raizSaida" } else { 'saída ao lado de cada .adoc' })) -ForegroundColor DarkGray
}
if ($oraculoProc) { Write-Host "Oráculo: Asciidoctor.js $versaoOraculo" -ForegroundColor DarkGray }
if ($logW) { Write-Host "Log: $caminhoLog" -ForegroundColor DarkGray }

try {
    while ($fila.Count -gt 0) {
        $item = $fila.Dequeue()
        $caminho = [string]$item[0]; $profundidade = [int]$item[1]
        $relativo = Caminho-Relativo $caminho
        if ($raizSaida -and (Fora-Da-Raiz $relativo)) {
            $stats.ForaDaRaiz++; $foraDaRaiz.Add($caminho)
            Registrar $relativo 'FORA_DA_RAIZ' '' 0 0 "Fora de $raiz; não convertido porque -Saida reproduz só o que está abaixo dela."
            continue
        }
        $arquivo = [System.IO.FileInfo]::new($caminho)
        $dirSaida = $arquivo.DirectoryName
        if ($raizSaida) {
            $dirSaida = [System.IO.Path]::GetFullPath([System.IO.Path]::Combine($raizSaida, [System.IO.Path]::GetDirectoryName($relativo)))
            [void][System.IO.Directory]::CreateDirectory($dirSaida)
        }
        try { $r = Tratar-Documento $arquivo $dirSaida $detalhado ($Recursivo -eq 'Links') }
        catch {
            # um documento problemático não derruba a execução: vira FALHA com a causa
            $r = [ordered]@{ Formatos = [System.Collections.Generic.List[string]]::new(); Refs = $null; Problemas = [System.Collections.Generic.List[object]]::new() }
            $r.Problemas.Add((Novo-Problema 'FALHA' '' 0 0 "O script falhou ao tratar este documento: $($_.Exception.Message)"))
            if ($detalhado) { Write-Host "FALHA $($_.Exception.Message)" -ForegroundColor Red }
        }
        $stats.Documentos++
        if (-not $Validar) { $stats.Arquivos += $r.Formatos.Count }

        # links: enfileira os documentos novos
        $novos = 0
        if ($r.Refs) {
            foreach ($ref in $r.Refs) {
                $partes = $ref -split "`t", 2
                $alvo = Destino-Do-Link $arquivo.DirectoryName $partes[0] $partes[1]
                if (-not $alvo) { continue }
                if ($vistos.Contains($alvo)) { continue }
                if (-not [System.IO.File]::Exists($alvo)) {
                    [void]$vistos.Add($alvo)
                    $r.Problemas.Add((Novo-Problema 'LINK_QUEBRADO' '' 0 0 "$($partes[1]) -> $alvo (não existe)"))
                    $quebrados.Add("$relativo : $($partes[1]) (não existe: $alvo)")
                    continue
                }
                if ($ProfundidadeMaxima -gt 0 -and $profundidade + 1 -gt $ProfundidadeMaxima) { $stats.AlemDaProfundidade++; continue }
                [void]$vistos.Add($alvo)
                $fila.Enqueue(@($alvo, ($profundidade + 1)))   # parênteses: a vírgula tem precedência sobre o +
                $novos++
            }
        }

        # contabiliza e registra
        $reprovado = $false
        foreach ($p in $r.Problemas) {
            Registrar $relativo $p.Categoria $p.Formato $p.Linha $p.Coluna $p.Mensagem
            if (-not $porCategoria.Contains($p.Categoria)) { $porCategoria[$p.Categoria] = 0; $docsPorCategoria[$p.Categoria] = [System.Collections.Generic.List[string]]::new() }
            $porCategoria[$p.Categoria]++
            if ($docsPorCategoria[$p.Categoria].Count -lt 20 -and -not $docsPorCategoria[$p.Categoria].Contains($relativo)) { $docsPorCategoria[$p.Categoria].Add($relativo) }
            $tipo = $p.Categoria + ': ' + (($p.Mensagem -replace "'[^']*'", "'...'" -replace '"[^"]*"', '"..."' -replace '\d+', '#'))
            if ($tipo.Length -gt 160) { $tipo = $tipo.Substring(0, 160) + '...' }
            $tipos[$tipo] = 1 + $(if ($tipos.ContainsKey($tipo)) { $tipos[$tipo] } else { 0 })
            # sem -Validar só falha de formato reprova (erro do processador é exibido, mas o arquivo foi gerado)
            if ($categoriasReprovam -contains $p.Categoria -and ($Validar -or $p.Categoria -eq 'FALHA')) { $reprovado = $true }
        }
        if ($reprovado) { $stats.Reprovados++ }
        if ($detalhado) { continue }

        # console: uma linha por documento (na validação, só os reprovados e o progresso)
        $avisos = @($r.Problemas | Where-Object { $categoriasReprovam -notcontains $_.Categoria }).Count
        $pendentes = if ($Recursivo -eq 'Links') { " (fila: $($fila.Count))" } else { " ($($stats.Documentos)/$totalInicial)" }
        if ($Validar) {
            if ($reprovado) {
                $primeiro = @($r.Problemas | Where-Object { $categoriasReprovam -contains $_.Categoria })[0]
                $onde = if ($primeiro.Linha -gt 0) { " linha $($primeiro.Linha):" } else { ':' }
                Write-Host ("{0,-8} {1}{2} {3}" -f $primeiro.Categoria, $relativo, $onde, $primeiro.Mensagem) -ForegroundColor Red
            }
            if ($stats.Documentos % 500 -eq 0) { Write-Host ("... {0} documento(s) verificado(s), {1} reprovado(s){2}" -f $stats.Documentos, $stats.Reprovados, $pendentes) -ForegroundColor DarkGray }
        }
        else {
            $resumo = ($r.Formatos -join ',')
            if ($avisos) { $resumo += "; $avisos aviso(s)" }
            if ($Recursivo -eq 'Links') { $resumo += "; +$novos link(s)" }
            $cor = if ($reprovado) { 'Red' } elseif ($avisos) { 'Yellow' } else { 'Green' }
            Write-Host ("{0,-5} {1}  [{2}]{3}" -f $(if ($reprovado) { 'FALHA' } else { 'OK' }), $relativo, $resumo, $pendentes) -ForegroundColor $cor
        }
        foreach ($p in $r.Problemas) {
            $reprova = $categoriasReprovam -contains $p.Categoria
            if ($MostrarAvisos -or ($reprova -and -not $Validar)) {
                $posicao = if ($p.Linha -gt 0) { " [{0}:{1}]" -f $p.Linha, $p.Coluna } else { '' }
                Write-Host ("      {0}{1} {2}" -f $p.Categoria.ToLowerInvariant(), $posicao, $p.Mensagem) -ForegroundColor $(if ($reprova) { 'Red' } else { 'Yellow' })
            }
        }
    }
}
finally {
    $ponte.Dispose()
    if ($logW) { $logW.Dispose() }
    if ($oraculoProc) { try { $oraculoProc.StandardInput.Close(); [void]$oraculoProc.WaitForExit(5000) } catch { } }
}

# --- Resumo -----------------------------------------------------------------------------------------------------
$relogio.Stop()
if ($detalhado) {
    if ($stats.Reprovados -gt 0) {
        $msg = if ($Validar) { 'Documento reprovado na validação' } else { 'Formato(s) falharam' }
        Write-Host "$msg; veja as mensagens acima." -ForegroundColor Red
        exit 1
    }
    if ($Validar) { Write-Host 'Documento aprovado na validação.' -ForegroundColor Green }
    exit 0
}
Write-Host ''
$verbo = if ($Validar) { 'verificados' } else { 'convertidos' }
$extra = if ($Validar) { '' } else { " ($($stats.Arquivos) arquivo(s) gerado(s))" }
Write-Host ("Documentos {0}: {1}{2} em {3:n1} s" -f $verbo, $stats.Documentos, $extra, $relogio.Elapsed.TotalSeconds) -ForegroundColor Cyan
if ($Validar) {
    $cor = if ($stats.Reprovados) { 'Red' } else { 'Green' }
    Write-Host ("Reprovados: {0} de {1}" -f $stats.Reprovados, $stats.Documentos) -ForegroundColor $cor
}
elseif ($stats.Reprovados) { Write-Host "Documentos com falha: $($stats.Reprovados)" -ForegroundColor Red }
foreach ($cat in $porCategoria.Keys) {
    $cor = if ($categoriasReprovam -contains $cat) { 'Red' } else { 'Yellow' }
    Write-Host ("  {0,-15} {1,7} ocorrência(s)" -f $cat, $porCategoria[$cat]) -ForegroundColor $cor
}
if ($tipos.Count) {
    Write-Host 'Problemas mais frequentes (números e trechos entre aspas generalizados):' -ForegroundColor Cyan
    foreach ($t in ($tipos.GetEnumerator() | Sort-Object Value -Descending | Select-Object -First 15)) {
        Write-Host ("  {0,7}  {1}" -f $t.Value, $t.Key) -ForegroundColor DarkGray
    }
}
foreach ($cat in $categoriasReprovam) {
    if (-not $docsPorCategoria.ContainsKey($cat)) { continue }
    Write-Host "Primeiros documentos com $($cat):" -ForegroundColor Red
    foreach ($d in $docsPorCategoria[$cat]) { Write-Host "  $d" -ForegroundColor Red }
}
if ($Recursivo -eq 'Pastas' -and $parciais) { Write-Host "Parciais ignorados (nome ou pasta iniciados por '_'): $parciais (use -IncluirParciais para tratá-los)" -ForegroundColor DarkGray }
if ($stats.AlemDaProfundidade) { Write-Host "Links não seguidos por -ProfundidadeMaxima $($ProfundidadeMaxima): $($stats.AlemDaProfundidade)" -ForegroundColor DarkGray }
if ($quebrados.Count) {
    Write-Host "Links quebrados: $($quebrados.Count)" -ForegroundColor Yellow
    foreach ($q in ($quebrados | Select-Object -First 10)) { Write-Host "  $q" -ForegroundColor Yellow }
    if ($quebrados.Count -gt 10) { Write-Host "  ... e mais $($quebrados.Count - 10) (todos no -Log)" -ForegroundColor Yellow }
}
if ($foraDaRaiz.Count) {
    Write-Host "Documentos fora de $raiz, não convertidos porque -Saida reproduz só o que está abaixo dela: $($foraDaRaiz.Count)" -ForegroundColor Yellow
    foreach ($d in ($foraDaRaiz | Select-Object -First 10)) { Write-Host "  $d" -ForegroundColor Yellow }
}
if ($logW) { Write-Host "Todos os problemas, um por linha: $caminhoLog" -ForegroundColor Cyan }
elseif ($porCategoria.Count) { Write-Host "Para a lista completa use -Log <arquivo.csv>." -ForegroundColor DarkGray }
if ($stats.Reprovados -gt 0) { exit 1 }
exit 0
