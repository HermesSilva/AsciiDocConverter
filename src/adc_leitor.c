#include "adc_leitor.h"
#include "adc_attrlist.h"

void adc_leitor_iniciar(ADC_Leitor* pL, ADC_Arena* pArena, ADC_TabelaAtributos* pAtributos, ADC_Mensagens* pMsgs,
                        ADC_ResolverRecurso pResolver, void* pContexto, int32_t pIncludesMaximo) {
    memset(pL, 0, sizeof *pL);
    pL->arena = pArena; pL->atributos = pAtributos; pL->msgs = pMsgs;
    pL->resolver = pResolver; pL->contexto_resolver = pContexto;
    pL->topo = -1;
    pL->includes_maximo = pIncludesMaximo > 0 ? pIncludesMaximo : 10000;
}

static int32_t registrar_arquivo(ADC_Leitor* pL, ADC_Fatia pCaminho) {
    ADC_Arquivo* a = ADC_NOVO(pL->arena, ADC_Arquivo);
    if (!a) return 0;
    a->caminho = pCaminho;
    a->indice = pL->quantidade_arquivos++;
    ADC_Arquivo** p = &pL->arquivos;
    while (*p) p = &(*p)->proximo;
    *p = a;
    return a->indice;
}

ADC_Fatia adc_leitor_caminho(const ADC_Leitor* pL, int32_t pIndice) {
    for (const ADC_Arquivo* a = pL->arquivos; a; a = a->proximo) if (a->indice == pIndice) return a->caminho;
    return adc_fatia(NULL, 0);
}

static bool empilhar(ADC_Leitor* pL, ADC_Fatia pTexto, int32_t pArquivo, int32_t pLevelOffset) {
    if (pL->topo + 1 >= ADC_LEITOR_MAX_FONTES) return false;
    ADC_Fonte* f = &pL->pilha[++pL->topo];
    f->dados = pTexto.dados; f->tamanho = pTexto.tamanho; f->pos = 0; f->linha = 0;
    f->arquivo = pArquivo; f->nivel_offset = pLevelOffset;
    f->vazia_antes = false; f->vazia_depois = false;
    f->profundidade_condicional_ao_entrar = pL->profundidade_condicional;
    pL->leveloffset += pLevelOffset;
    return true;
}

void adc_leitor_abrir(ADC_Leitor* pL, ADC_Fatia pTexto, ADC_Fatia pCaminho) {
    int32_t idx = registrar_arquivo(pL, pCaminho);
    empilhar(pL, pTexto, idx, 0);
    pL->fim = false;
}

/* Lê a próxima linha crua da fonte do topo; false quando a fonte acabou (e a desempilha). */
static bool linha_crua(ADC_Leitor* pL, ADC_Linha* pLinha) {
    for (;;) {
        if (pL->topo < 0) return false;
        ADC_Fonte* f = &pL->pilha[pL->topo];
        if (f->vazia_antes || (f->pos >= f->tamanho && f->vazia_depois)) {
            /* linha vazia sintética do include com leveloffset */
            if (f->vazia_antes) f->vazia_antes = false; else f->vazia_depois = false;
            pLinha->texto = adc_fatia(f->dados, 0);
            pLinha->numero = f->linha; pLinha->arquivo = f->arquivo; pLinha->deslocamento = f->pos;
            return true;
        }
        if (f->pos >= f->tamanho) {
            if (pL->profundidade_condicional > f->profundidade_condicional_ao_entrar) {
                adc_msgs_adicionar(pL->msgs, ADC_NIVEL_AVISO, f->linha, 0,
                    "Condicional (ifdef/ifndef/ifeval) sem endif:: correspondente até o fim do arquivo; o bloco foi fechado automaticamente.");
                pL->profundidade_condicional = f->profundidade_condicional_ao_entrar;
            }
            pL->leveloffset -= f->nivel_offset;
            pL->topo--;
            continue;
        }
        int32_t ini = f->pos;
        const char* d = f->dados;
        int32_t n = f->tamanho;
        int32_t p = ini;
        while (p < n && d[p] != '\n') p++;
        int32_t fim = p;
        if (fim > ini && d[fim - 1] == '\r') fim--;
        f->pos = (p < n) ? p + 1 : n;
        f->linha++;
        pLinha->texto = adc_fatia(d + ini, fim - ini);
        pLinha->numero = f->linha;
        pLinha->arquivo = f->arquivo;
        pLinha->deslocamento = ini;
        return true;
    }
}

/* ------------------------------------------------------- diretivas ---- */

/* Reconhece "ifdef::", "ifndef::", "ifeval::", "endif::", "include::" no início da linha.
 * Devolve o nome da diretiva (fatia) e separa alvo e attrlist. */
static bool eh_diretiva(ADC_Fatia pLinha, ADC_Fatia* pNome, ADC_Fatia* pAlvo, ADC_Fatia* pAttrs) {
    static const char* nomes[] = { "ifdef", "ifndef", "ifeval", "endif", "include" };
    for (size_t i = 0; i < sizeof nomes / sizeof nomes[0]; i++) {
        size_t n = strlen(nomes[i]);
        if ((size_t)pLinha.tamanho > n + 2 && memcmp(pLinha.dados, nomes[i], n) == 0 && pLinha.dados[n] == ':' && pLinha.dados[n + 1] == ':') {
            /* alvo até '[' ; attrlist até o último ']' que fecha a linha */
            int32_t abre = -1;
            for (int32_t k = (int32_t)n + 2; k < pLinha.tamanho; k++) if (pLinha.dados[k] == '[') { abre = k; break; }
            if (abre < 0 || pLinha.dados[pLinha.tamanho - 1] != ']') return false;
            *pNome = adc_fatia(nomes[i], (int32_t)n);
            *pAlvo = adc_fatia(pLinha.dados + n + 2, abre - (int32_t)n - 2);
            *pAttrs = adc_fatia(pLinha.dados + abre + 1, pLinha.tamanho - abre - 2);
            /* include exige alvo não vazio; ifdef/ifndef também; ifeval/endif não */
            if ((i == 0 || i == 1 || i == 4) && pAlvo->tamanho == 0) return false;
            if (i == 4 && pAlvo->tamanho > 0 && pAlvo->dados[0] == ' ') return false;
            return true;
        }
    }
    return false;
}

/* ifdef::a,b[] (qualquer) / ifdef::a+b[] (todos) */
static bool avaliar_ifdef(ADC_Leitor* pL, ADC_Fatia pAlvo, bool pNegado) {
    bool ou = adc_fatia_indice(pAlvo, ',') >= 0;
    bool e = adc_fatia_indice(pAlvo, '+') >= 0;
    bool resultado = ou ? false : true;
    int32_t ini = 0;
    for (int32_t i = 0; i <= pAlvo.tamanho; i++) {
        if (i == pAlvo.tamanho || pAlvo.dados[i] == ',' || pAlvo.dados[i] == '+') {
            ADC_Fatia nome = adc_fatia_aparar(adc_fatia(pAlvo.dados + ini, i - ini));
            bool def = nome.tamanho > 0 && adc_atribs_obter(pL->atributos, nome, NULL);
            if (ou) resultado = resultado || def;
            else if (e) resultado = resultado && def;
            else resultado = def;
            ini = i + 1;
        }
    }
    return pNegado ? !resultado : resultado;
}

static ADC_Fatia sem_aspas(ADC_Fatia pF) {
    pF = adc_fatia_aparar(pF);
    if (pF.tamanho >= 2 && ((pF.dados[0] == '"' && pF.dados[pF.tamanho - 1] == '"') || (pF.dados[0] == '\'' && pF.dados[pF.tamanho - 1] == '\'')))
        return adc_fatia(pF.dados + 1, pF.tamanho - 2);
    return pF;
}

/* ifeval::[{a} == "x"] — operadores == != < <= > >= ; números comparam como número, senão texto. */
static bool avaliar_ifeval(ADC_Leitor* pL, ADC_Fatia pExpr, int32_t pLinha) {
    ADC_Fatia r = adc_atribs_resolver(pL->atributos, pExpr, pL->msgs, pLinha, true, NULL);
    static const char* ops[] = { "==", "!=", "<=", ">=", "<", ">" };
    for (size_t o = 0; o < 6; o++) {
        size_t on = strlen(ops[o]);
        for (int32_t i = 0; i + (int32_t)on <= r.tamanho; i++) {
            if (memcmp(r.dados + i, ops[o], on) != 0) continue;
            ADC_Fatia a = sem_aspas(adc_fatia(r.dados, i));
            ADC_Fatia b = sem_aspas(adc_fatia(r.dados + i + (int32_t)on, r.tamanho - i - (int32_t)on));
            int32_t na = adc_fatia_para_int(a, INT32_MIN), nb = adc_fatia_para_int(b, INT32_MIN);
            int cmp;
            if (na != INT32_MIN && nb != INT32_MIN) cmp = (na < nb) ? -1 : (na > nb) ? 1 : 0;
            else {
                int32_t m = a.tamanho < b.tamanho ? a.tamanho : b.tamanho;
                cmp = memcmp(a.dados, b.dados, (size_t)m);
                if (cmp == 0) cmp = (a.tamanho < b.tamanho) ? -1 : (a.tamanho > b.tamanho) ? 1 : 0;
            }
            switch (o) {
                case 0: return cmp == 0; case 1: return cmp != 0; case 2: return cmp <= 0;
                case 3: return cmp >= 0; case 4: return cmp < 0; default: return cmp > 0;
            }
        }
    }
    adc_msgs_adicionar(pL->msgs, ADC_NIVEL_AVISO, pLinha, 0,
        "ifeval:: sem operador reconhecido (==, !=, <, <=, >, >=): a expressão '%.*s' foi avaliada como falsa.", r.tamanho, r.dados);
    return false;
}

/* Trata include::alvo[attrs]. Devolve true se a linha foi consumida (e algo pode ter sido empilhado). */
/* ===================================================== tags de include ==== */
/* Seleção de linhas por tag como o PreprocessorReader do Asciidoctor 2.0.20: tag=x, tags=a;b, negação !x, curingas
   * (toda região marcada) e ** (todas as linhas); só negações = todas as linhas menos as regiões negadas. As linhas de
   diretiva tag::x[] / end::x[] nunca entram. */
#define ADC_TAGS_MAX 32
typedef struct SeletorTags {
    ADC_Fatia nome[ADC_TAGS_MAX]; bool sel[ADC_TAGS_MAX]; int32_t n;
    int8_t    curinga;            /* -1 = nil; 0/1 */
    bool      base, atual;
    ADC_Fatia pilha_nome[64]; bool pilha_sel[64]; int32_t topo;  /* tag ativa e seleção */
} SeletorTags;

static int32_t tag_indice(const SeletorTags* s, ADC_Fatia pNome) { for (int32_t i = 0; i < s->n; i++) if (adc_fatia_igual_f(s->nome[i], pNome)) return i; return -1; }

static void tag_definir(SeletorTags* s, ADC_Fatia pNome, bool pSel) {
    int32_t i = tag_indice(s, pNome);
    if (i >= 0) { s->sel[i] = pSel; return; }
    if (s->n < ADC_TAGS_MAX) { s->nome[s->n] = pNome; s->sel[s->n] = pSel; s->n++; }
}

static void tag_remover(SeletorTags* s, int32_t i) { for (int32_t k = i; k + 1 < s->n; k++) { s->nome[k] = s->nome[k + 1]; s->sel[k] = s->sel[k + 1]; } s->n--; }

/* Monta o seletor; devolve false se não há tag alguma (então nada é filtrado por tag). */
static bool tags_iniciar(SeletorTags* s, ADC_Fatia pTag, bool pTemTag, ADC_Fatia pTags, bool pTemTags) {
    memset(s, 0, sizeof *s); s->curinga = -1;
    if (pTemTag && pTag.tamanho) {
        if (pTag.dados[0] == '!') tag_definir(s, adc_fatia(pTag.dados + 1, pTag.tamanho - 1), false);
        else tag_definir(s, pTag, true);
    } else if (pTemTags) {
        char sep = adc_fatia_indice(pTags, ',') >= 0 ? ',' : ';';   /* split_delimited_value */
        int32_t ini = 0;
        for (int32_t k = 0; k <= pTags.tamanho; k++) {
            if (k == pTags.tamanho || pTags.dados[k] == sep) {
                ADC_Fatia um = adc_fatia_aparar(adc_fatia(pTags.dados + ini, k - ini));
                if (um.tamanho && !adc_fatia_igual(um, "!")) {
                    if (um.dados[0] == '!') tag_definir(s, adc_fatia(um.dados + 1, um.tamanho - 1), false);
                    else tag_definir(s, um, true);
                }
                ini = k + 1;
            }
        }
    }
    if (s->n == 0) return false;
    int32_t i2 = tag_indice(s, adc_fatia_cstr("**")), i1 = tag_indice(s, adc_fatia_cstr("*"));
    if (i2 >= 0) {
        s->base = s->atual = s->sel[i2]; tag_remover(s, i2);
        i1 = tag_indice(s, adc_fatia_cstr("*"));
        if (i1 >= 0) { s->curinga = s->sel[i1] ? 1 : 0; tag_remover(s, i1); }
        else if (!s->atual && s->n > 0 && !s->sel[0]) s->curinga = 1;
    } else if (i1 >= 0) {
        bool w = s->sel[i1];
        if (i1 == 0) { s->base = s->atual = !w; s->curinga = w ? 1 : 0; }
        else { s->base = s->atual = false; s->curinga = w ? 1 : 0; }
        tag_remover(s, i1);
    } else {
        bool algum_true = false; for (int32_t k = 0; k < s->n; k++) if (s->sel[k]) algum_true = true;
        s->base = s->atual = !algum_true;
    }
    return true;
}

/* Linha de diretiva "tag::nome[]" / "end::nome[]" (TagDirectiveRx: \b(?:tag|(e)nd)::(\S+?)\[\](?=$|[ \r])). */
static bool tag_diretiva(ADC_Fatia L, bool* pFim, ADC_Fatia* pNome) {
    for (int32_t k = 0; k + 7 <= L.tamanho; k++) {
        bool t = memcmp(L.dados + k, "tag::", 5) == 0, e = memcmp(L.dados + k, "end::", 5) == 0;
        if (!t && !e) continue;
        if (k > 0) { char a = L.dados[k - 1]; if ((a >= 'a' && a <= 'z') || (a >= 'A' && a <= 'Z') || (a >= '0' && a <= '9') || a == '_') continue; }
        int32_t j = k + 5, ini = j;
        while (j + 1 < L.tamanho && !(L.dados[j] == '[' && L.dados[j + 1] == ']')) { if (L.dados[j] == ' ' || L.dados[j] == '\t') break; j++; }
        if (j + 1 >= L.tamanho || L.dados[j] != '[' || L.dados[j + 1] != ']' || j == ini) continue;
        if (j + 2 < L.tamanho && L.dados[j + 2] != ' ' && L.dados[j + 2] != '\r') continue;
        *pFim = e; *pNome = adc_fatia(L.dados + ini, j - ini);
        return true;
    }
    return false;
}

/* Processa uma linha; devolve true se ela entra no include. */
static bool tags_linha(SeletorTags* s, ADC_Fatia L) {
    bool fim; ADC_Fatia nome;
    if (adc_fatia_indice(L, ':') >= 0 && tag_diretiva(L, &fim, &nome)) {
        ADC_Fatia ativa = s->topo > 0 ? s->pilha_nome[s->topo - 1] : adc_fatia(NULL, 0);
        if (fim) {
            if (s->topo > 0 && adc_fatia_igual_f(nome, ativa)) {
                s->topo--;
                s->atual = s->topo > 0 ? s->pilha_sel[s->topo - 1] : s->base;
            } else if (tag_indice(s, nome) >= 0) {
                for (int32_t k = s->topo - 1; k >= 0; k--) if (adc_fatia_igual_f(s->pilha_nome[k], nome)) { for (int32_t z = k; z + 1 < s->topo; z++) { s->pilha_nome[z] = s->pilha_nome[z + 1]; s->pilha_sel[z] = s->pilha_sel[z + 1]; } s->topo--; break; }
            }
        } else {
            int32_t i = tag_indice(s, nome);
            if (i >= 0) s->atual = s->sel[i];
            else if (s->curinga >= 0) s->atual = (s->topo > 0 && !s->atual) ? false : s->curinga == 1;
            else return false;
            if (s->topo < 64) { s->pilha_nome[s->topo] = nome; s->pilha_sel[s->topo] = s->atual; s->topo++; }
        }
        return false;   /* a diretiva nunca entra */
    }
    return s->atual;
}

/* Linha que substitui a diretiva (como o replace_next_line do Asciidoctor), na arena. */
static ADC_Fatia linha_substituta(ADC_Leitor* pL, const char* pAntes, ADC_Fatia pMeio, const char* pDepois, ADC_Fatia pFim, const char* pFecho) {
    ADC_Buffer b; adc_buffer_iniciar(&b);
    adc_buffer_texto(&b, pAntes); adc_buffer_fatia(&b, pMeio); adc_buffer_texto(&b, pDepois); adc_buffer_fatia(&b, pFim); adc_buffer_texto(&b, pFecho);
    int32_t tam; uint8_t* d = adc_buffer_entregar(&b, &tam);
    char* c = d ? adc_arena_copiar(pL->arena, (const char*)d, tam) : NULL;
    free(d);
    return c ? adc_fatia(c, tam) : adc_fatia("", 0);
}

/* Alvo com esquema (http://, https://, ftp://...): o "uriish?" do Asciidoctor. */
static bool alvo_uri(ADC_Fatia p) {
    for (int32_t i = 0; i + 2 < p.tamanho; i++) {
        char c = p.dados[i];
        if (c == ':' && p.dados[i + 1] == '/' && p.dados[i + 2] == '/') return i > 1;
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '+' || c == '-' || c == '.')) return false;
    }
    return false;
}

/* Devolve a linha que substitui a diretiva (dados != NULL), ou nada. */
static ADC_Fatia fazer_include(ADC_Leitor* pL, ADC_Fatia pAlvo, ADC_Fatia pAttrs, const ADC_Linha* pLinha) {
    ADC_Fatia nada = { 0 };
    ADC_Fatia alvo = adc_atribs_resolver(pL->atributos, pAlvo, pL->msgs, pLinha->numero, true, NULL);
    /* Asciidoctor: include de URI sem :allow-uri-read: vira um link para o alvo */
    if (alvo_uri(alvo) && !adc_atribs_definido(pL->atributos, "allow-uri-read"))
        return linha_substituta(pL, "link:", alvo, "[role=include]", nada, "");
    if (pL->includes_feitos >= pL->includes_maximo) {
        adc_msgs_adicionar(pL->msgs, ADC_NIVEL_AVISO, pLinha->numero, 1,
            "Limite de %d includes atingido: include::%.*s[] foi ignorado.", pL->includes_maximo, alvo.tamanho, alvo.dados);
        return nada;
    }
    if (!pL->resolver) {
        adc_msgs_adicionar(pL->msgs, ADC_NIVEL_AVISO, pLinha->numero, 1,
            "include::%.*s[] negado: a conversão está em modo seguro, sem resolvedor de recursos registrado pelo chamador.", alvo.tamanho, alvo.dados);
        return nada;
    }
    if (pL->topo + 1 >= ADC_LEITOR_MAX_FONTES) {
        adc_msgs_adicionar(pL->msgs, ADC_NIVEL_AVISO, pLinha->numero, 1,
            "include::%.*s[] ignorado: profundidade máxima de includes (%d) atingida.", alvo.tamanho, alvo.dados, ADC_LEITOR_MAX_FONTES);
        return nada;
    }
    const uint8_t* conteudo = NULL; int32_t tamanho = 0;
    int32_t rc = pL->resolver(pL->contexto_resolver, 1, alvo.dados, alvo.tamanho, &conteudo, &tamanho);
    if (rc != 0 || !conteudo || tamanho < 0) {
        adc_msgs_adicionar(pL->msgs, ADC_NIVEL_ERRO, pLinha->numero, 1,
            "include::%.*s[] não encontrado ou negado pelo chamador (código %d): o conteúdo não foi incluído; no lugar sai a linha \"Unresolved directive\", como no Asciidoctor.", alvo.tamanho, alvo.dados, (int)rc);
        /* Asciidoctor: "Unresolved directive in <arquivo> - include::alvo[attrs]" */
        ADC_Fatia arquivo = adc_leitor_caminho(pL, pLinha->arquivo);
        if (arquivo.tamanho == 0) {
            ADC_Fatia docfile;
            if (adc_atribs_obter_cstr(pL->atributos, "docfile", &docfile) && docfile.tamanho) {
                int32_t k = docfile.tamanho; while (k > 0 && docfile.dados[k - 1] != '/' && docfile.dados[k - 1] != '\\') k--;
                arquivo = adc_fatia(docfile.dados + k, docfile.tamanho - k);
            } else arquivo = adc_fatia_cstr("<stdin>");
        }
        ADC_Fatia sem_attrs = linha_substituta(pL, "Unresolved directive in ", arquivo, " - include::", alvo, "[");
        return linha_substituta(pL, "", sem_attrs, "", pAttrs, "]");
    }
    if (adc_utf8_validar(conteudo, tamanho) >= 0) {
        adc_msgs_adicionar(pL->msgs, ADC_NIVEL_AVISO, pLinha->numero, 1,
            "include::%.*s[] ignorado: o conteúdo entregue não é UTF-8 válido.", alvo.tamanho, alvo.dados);
        return nada;
    }
    pL->includes_feitos++;
    /* atributos do include: leveloffset=+1, lines=, tag(s)=, indent= */
    ADC_Atributo* attrs = adc_attrlist_parsear(pL->arena, pAttrs, false, NULL);
    int32_t leveloffset = 0;
    bool achou;
    ADC_Fatia lo = adc_atributo_obter(attrs, "leveloffset", &achou);
    bool tem_leveloffset = achou;
    if (achou) leveloffset = adc_fatia_para_int(lo, 0);
    ADC_Fatia texto = adc_fatia((const char*)conteudo, tamanho);

    ADC_Fatia linhas = adc_atributo_obter(attrs, "lines", &achou);
    bool tem_tag1 = false, tem_tags2 = false;
    ADC_Fatia tag1 = adc_atributo_obter(attrs, "tag", &tem_tag1);
    ADC_Fatia tags2 = adc_atributo_obter(attrs, "tags", &tem_tags2);
    SeletorTags seletor;
    /* lines= tem precedência: com ele as tags são ignoradas (como no Asciidoctor) */
    bool tem_tags = linhas.tamanho == 0 && tags_iniciar(&seletor, tag1, tem_tag1, tags2, tem_tags2);
    ADC_Fatia indent = adc_atributo_obter(attrs, "indent", &achou);
    bool tem_indent = achou;

    if (linhas.tamanho > 0 || tem_tags || tem_indent) {
        /* filtra linhas: produz um novo texto na arena */
        ADC_Buffer b; adc_buffer_iniciar(&b);
        int32_t num = 0; int32_t pos = 0;
        int32_t menor_indent = INT32_MAX;
        if (tem_indent) {
            /* mede a indentação mínima das linhas não vazias */
            int32_t p2 = 0;
            while (p2 < texto.tamanho) {
                int32_t fimL = p2; while (fimL < texto.tamanho && texto.dados[fimL] != '\n') fimL++;
                int32_t k = p2; while (k < fimL && texto.dados[k] == ' ') k++;
                if (k < fimL && texto.dados[k] != '\r') { int32_t ind = k - p2; if (ind < menor_indent) menor_indent = ind; }
                p2 = fimL + 1;
            }
            if (menor_indent == INT32_MAX) menor_indent = 0;
        }
        int32_t indent_alvo = tem_indent ? adc_fatia_para_int(indent, 0) : 0;
        while (pos < texto.tamanho) {
            int32_t fimL = pos; while (fimL < texto.tamanho && texto.dados[fimL] != '\n') fimL++;
            ADC_Fatia L = adc_fatia(texto.dados + pos, fimL - pos);
            if (L.tamanho > 0 && L.dados[L.tamanho - 1] == '\r') L.tamanho--;
            num++;
            bool incluir = true;
            if (tem_tags) incluir = tags_linha(&seletor, L);
            if (incluir && linhas.tamanho > 0) {
                /* lines=1..5;8;10..-1 */
                incluir = false;
                int32_t ini = 0;
                for (int32_t k = 0; k <= linhas.tamanho; k++) {
                    if (k == linhas.tamanho || linhas.dados[k] == ';' || linhas.dados[k] == ',') {
                        ADC_Fatia faixa = adc_fatia_aparar(adc_fatia(linhas.dados + ini, k - ini));
                        int32_t dd = -1;
                        for (int32_t q = 0; q + 1 < faixa.tamanho; q++) if (faixa.dados[q] == '.' && faixa.dados[q + 1] == '.') { dd = q; break; }
                        int32_t de, ate;
                        if (dd >= 0) { de = adc_fatia_para_int(adc_fatia(faixa.dados, dd), 1); ate = adc_fatia_para_int(adc_fatia(faixa.dados + dd + 2, faixa.tamanho - dd - 2), -1); }
                        else { de = ate = adc_fatia_para_int(faixa, 0); }
                        if (ate == -1) ate = INT32_MAX;
                        if (num >= de && num <= ate) incluir = true;
                        ini = k + 1;
                    }
                }
            }
            if (incluir) {
                if (tem_indent) {
                    int32_t k = 0; while (k < L.tamanho && k < menor_indent && L.dados[k] == ' ') k++;
                    adc_buffer_repetir(&b, ' ', indent_alvo);
                    adc_buffer_bytes(&b, L.dados + k, L.tamanho - k);
                } else adc_buffer_fatia(&b, L);
                adc_buffer_char(&b, '\n');
            }
            pos = fimL + 1;
        }
        int32_t tam; uint8_t* d = adc_buffer_entregar(&b, &tam);
        if (d) { char* c = adc_arena_copiar(pL->arena, (const char*)d, tam); free(d); if (c) texto = adc_fatia(c, tam); }
    }
    int32_t idx = registrar_arquivo(pL, alvo);
    if (empilhar(pL, texto, idx, leveloffset) && tem_leveloffset) { pL->pilha[pL->topo].vazia_antes = true; pL->pilha[pL->topo].vazia_depois = true; }
    return nada;
}

/* Produz a próxima linha já pré-processada na fila; false no fim. */
static bool preencher_uma(ADC_Leitor* pL) {
    ADC_Linha L;
    for (;;) {
        if (!linha_crua(pL, &L)) return false;
        ADC_Fatia nome, alvo, attrs;
        bool pulando = pL->profundidade_condicional > 0 && pL->condicional[pL->profundidade_condicional - 1];
        if (eh_diretiva(L.texto, &nome, &alvo, &attrs)) {
            if (adc_fatia_igual(nome, "endif")) {
                if (pL->profundidade_condicional > 0) pL->profundidade_condicional--;
                else adc_msgs_adicionar(pL->msgs, ADC_NIVEL_AVISO, L.numero, 1, "endif:: sem ifdef/ifndef/ifeval correspondente; a linha foi ignorada.");
                continue;
            }
            if (adc_fatia_igual(nome, "ifdef") || adc_fatia_igual(nome, "ifndef") || adc_fatia_igual(nome, "ifeval")) {
                bool negado = nome.dados[2] == 'n';
                bool resultado;
                if (pulando) resultado = false;
                else if (nome.dados[2] == 'e') resultado = avaliar_ifeval(pL, attrs, L.numero);
                else resultado = avaliar_ifdef(pL, alvo, negado);
                if (nome.dados[2] != 'e' && attrs.tamanho > 0) {
                    /* forma de uma linha: ifdef::a[texto] → o texto entra se verdadeiro */
                    if (resultado && !pulando) {
                        L.texto = attrs;
                        L.deslocamento += (int32_t)(attrs.dados - L.texto.dados);
                        pL->fila[(pL->fila_inicio + pL->fila_tamanho) % ADC_LEITOR_ANTEVISAO] = L;
                        pL->fila_tamanho++;
                        return true;
                    }
                    continue;
                }
                if (pL->profundidade_condicional >= ADC_LEITOR_MAX_CONDICIONAIS) {
                    adc_msgs_adicionar(pL->msgs, ADC_NIVEL_AVISO, L.numero, 1, "Condicionais aninhados além de %d níveis; a diretiva foi ignorada.", ADC_LEITOR_MAX_CONDICIONAIS);
                    continue;
                }
                pL->condicional[pL->profundidade_condicional++] = pulando || !resultado;
                continue;
            }
            if (adc_fatia_igual(nome, "include")) {
                if (pulando) continue;
                ADC_Fatia subst = fazer_include(pL, alvo, attrs, &L);
                if (!subst.dados) continue;
                L.texto = subst;   /* a diretiva vira a linha substituta, com o mesmo número */
            }
        }
        if (pulando) continue;
        pL->fila[(pL->fila_inicio + pL->fila_tamanho) % ADC_LEITOR_ANTEVISAO] = L;
        pL->fila_tamanho++;
        return true;
    }
}

void adc_leitor_abrir_linhas(ADC_Leitor* pL, const ADC_Linha* pLinhas, int32_t pN) {
    pL->prontas = pLinhas; pL->n_prontas = pN; pL->i_pronta = 0;
}

const ADC_Linha* adc_leitor_espiar(ADC_Leitor* pL, int32_t pN) {
    if (pL->prontas) return (pN >= 0 && pL->i_pronta + pN < pL->n_prontas) ? &pL->prontas[pL->i_pronta + pN] : NULL;
    if (pN >= ADC_LEITOR_ANTEVISAO) return NULL;
    while (pL->fila_tamanho <= pN) {
        if (!preencher_uma(pL)) return NULL;
    }
    return &pL->fila[(pL->fila_inicio + pN) % ADC_LEITOR_ANTEVISAO];
}

bool adc_leitor_ler(ADC_Leitor* pL, ADC_Linha* pLinha) {
    const ADC_Linha* l = adc_leitor_espiar(pL, 0);
    if (!l) return false;
    *pLinha = *l;
    if (pL->prontas) { pL->i_pronta++; return true; }
    pL->fila_inicio = (pL->fila_inicio + 1) % ADC_LEITOR_ANTEVISAO;
    pL->fila_tamanho--;
    return true;
}

void adc_leitor_pular(ADC_Leitor* pL, int32_t pN) {
    ADC_Linha l;
    for (int32_t i = 0; i < pN; i++) if (!adc_leitor_ler(pL, &l)) return;
}

bool adc_leitor_fim(ADC_Leitor* pL) { return adc_leitor_espiar(pL, 0) == NULL; }
