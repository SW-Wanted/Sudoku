/**
 * @file    sudoku-omp.c
 * @brief   Solucionador de Sudoku — Implementação Paralela com OpenMP
 *
 * Lê um puzzle Sudoku n×n a partir de um ficheiro, resolve-o usando
 * backtracking recursivo paralelizado e imprime a solução no stdout.
 * O tempo de execução do algoritmo é reportado no stderr usando omp_get_wtime().
 *
 * OPTIMIZAÇÕES IMPLEMENTADAS:
 *   1. Bitmasks O(1) para is_valid   — elimina os 3 loops da versão original
 *   2. Geração de sub-problemas em K níveis — garante trabalho suficiente para
 *      todas as threads, mesmo em puzzles com poucas células na 1ª posição
 *   3. Early termination com flag atómica — threads param assim que qualquer
 *      thread encontra solução
 *   4. Dynamic scheduling — balanceia carga irregular do backtracking
 *   5. Estrutura SudokuState separada — cada thread opera em estado isolado
 *      sem partilha de memória durante o backtracking
 *
 * Compilação:
 *   gcc -fopenmp -O2 sudoku-omp.c -o sudoku-omp
 *
 * Utilização:
 *   ./sudoku-omp <ficheiro_entrada>
 *
 * @author  Grupo 4 — Carlos Tchípia, Emanuel dos Santos, Líria Bá
 * @course  Computação Paralela e Distribuída — ISPTEC 2025/2026
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <omp.h>

/* =========================================================================
 * Estruturas de Dados
 * ========================================================================= */

/**
 * @brief Representa o tabuleiro Sudoku n×n com bitmasks de restrições.
 *
 * Para além da grid, mantém três arrays de bitmasks actualizados
 * incrementalmente a cada colocação/remoção de número:
 *   - row_mask[i]  : bits dos números já usados na linha i
 *   - col_mask[j]  : bits dos números já usados na coluna j
 *   - box_mask[b]  : bits dos números já usados no bloco b
 *
 * Um bit k activo (1 << k) significa que o número k já está presente.
 * A verificação de validade torna-se uma única operação de bits O(1).
 */
typedef struct {
    int **grid;       /**< Matriz n×n alocada dinamicamente.              */
    int  *row_mask;   /**< Bitmask de números usados por linha.           */
    int  *col_mask;   /**< Bitmask de números usados por coluna.          */
    int  *box_mask;   /**< Bitmask de números usados por bloco L×L.       */
    int   n;          /**< Dimensão do tabuleiro (n×n).                   */
    int   L;          /**< Dimensão de cada bloco interno (L×L), n = L². */
} Sudoku;

/**
 * @brief Sub-problema gerado para distribuição pelas threads.
 *
 * Representa um estado parcial do tabuleiro com as bitmasks já actualizadas,
 * pronto para continuar o backtracking de forma independente.
 */
typedef struct {
    int **grid;
    int  *row_mask;
    int  *col_mask;
    int  *box_mask;
} SubProblem;

/* =========================================================================
 * Protótipos
 * ========================================================================= */

Sudoku     *create_sudoku(int L);
void        free_sudoku(Sudoku *s);
int         read_sudoku(const char *filename, Sudoku **s);
void        print_sudoku(Sudoku *s);
SubProblem *create_subproblem(Sudoku *s);
void        free_subproblem(SubProblem *sp, int n);
int         solve_with_cutoff(SubProblem *sp, int n, int L,
                              volatile int *found, SubProblem *solution);
int         solve_sudoku_parallel(Sudoku *s);

/* =========================================================================
 * Índice do bloco
 * ========================================================================= */

/** Calcula o índice linear do bloco L×L ao qual a célula (row, col) pertence. */
static inline int box_index(int row, int col, int L) {
    return (row / L) * L + (col / L);
}

/* =========================================================================
 * Criação / Destruição
 * ========================================================================= */

/**
 * @brief Aloca e inicializa uma estrutura Sudoku com bitmasks a zero.
 *
 * @param  L  Dimensão do bloco. Deve satisfazer 2 ≤ L ≤ 9.
 * @return    Ponteiro para a estrutura, ou NULL em caso de erro.
 */
Sudoku *create_sudoku(int L) {
    Sudoku *s = (Sudoku *) malloc(sizeof(Sudoku));
    if (!s) return NULL;

    s->L = L;
    s->n = L * L;

    s->grid = (int **) malloc(s->n * sizeof(int *));
    if (!s->grid) { free(s); return NULL; }

    for (int i = 0; i < s->n; i++) {
        s->grid[i] = (int *) calloc(s->n, sizeof(int));
        if (!s->grid[i]) {
            for (int j = 0; j < i; j++) free(s->grid[j]);
            free(s->grid); free(s); return NULL;
        }
    }

    s->row_mask = (int *) calloc(s->n, sizeof(int));
    s->col_mask = (int *) calloc(s->n, sizeof(int));
    s->box_mask = (int *) calloc(s->n, sizeof(int));

    if (!s->row_mask || !s->col_mask || !s->box_mask) {
        free(s->row_mask); free(s->col_mask); free(s->box_mask);
        for (int i = 0; i < s->n; i++) free(s->grid[i]);
        free(s->grid); free(s); return NULL;
    }

    return s;
}

/**
 * @brief Libera toda a memória associada a uma estrutura Sudoku.
 */
void free_sudoku(Sudoku *s) {
    if (!s) return;
    if (s->grid) {
        for (int i = 0; i < s->n; i++)
            if (s->grid[i]) free(s->grid[i]);
        free(s->grid);
    }
    free(s->row_mask);
    free(s->col_mask);
    free(s->box_mask);
    free(s);
}

/* =========================================================================
 * Leitura / Impressão
 * ========================================================================= */

/**
 * @brief Lê o puzzle de um ficheiro e constrói as bitmasks iniciais.
 *
 * Para cada valor não-zero lido, activa o bit correspondente nas bitmasks
 * da linha, coluna e bloco, de forma a que o backtracking parta de um
 * estado consistente sem ter de reconstruir as máscaras a cada chamada.
 *
 * @param  filename  Caminho para o ficheiro de entrada.
 * @param  s         Endereço do ponteiro receptor.
 * @return           1 em caso de sucesso, 0 em caso de erro.
 */
int read_sudoku(const char *filename, Sudoku **s) {
    FILE *fp = fopen(filename, "r");
    if (!fp) {
        fprintf(stderr, "Erro ao abrir ficheiro: %s\n", filename);
        return 0;
    }

    int L;
    if (fscanf(fp, "%d", &L) != 1 || L < 2 || L > 9) {
        fprintf(stderr, "Valor de L inválido: deve estar entre 2 e 9\n");
        fclose(fp);
        return 0;
    }

    *s = create_sudoku(L);
    if (!*s) { fclose(fp); return 0; }

    int n = (*s)->n;

    for (int i = 0; i < n; i++) {
        for (int j = 0; j < n; j++) {
            int val;
            if (fscanf(fp, "%d", &val) != 1 || val < 0 || val > n) {
                fprintf(stderr, "Valor inválido na célula [%d][%d]\n", i, j);
                free_sudoku(*s); *s = NULL; fclose(fp); return 0;
            }
            (*s)->grid[i][j] = val;

            /* Actualiza bitmasks para os valores inicialmente preenchidos */
            if (val != 0) {
                int bit = 1 << val;
                (*s)->row_mask[i]                   |= bit;
                (*s)->col_mask[j]                   |= bit;
                (*s)->box_mask[box_index(i, j, L)]  |= bit;
            }
        }
    }

    fclose(fp);
    return 1;
}

/**
 * @brief Imprime o tabuleiro resolvido no stdout.
 */
void print_sudoku(Sudoku *s) {
    for (int i = 0; i < s->n; i++) {
        for (int j = 0; j < s->n; j++) {
            printf("%d", s->grid[i][j]);
            if (j < s->n - 1) printf(" ");
        }
        printf("\n");
    }
}

/* =========================================================================
 * Sub-problemas para paralelização
 * ========================================================================= */

/**
 * @brief Cria uma cópia independente do estado actual para uma thread.
 *
 * Copia a grid e as três bitmasks. O SubProblem não contém L/n porque
 * esses valores são passados como parâmetros às funções que operam nele,
 * evitando overhead de acesso a struct adicional no caminho crítico.
 *
 * @param  s  Estado actual do tabuleiro.
 * @return    Ponteiro para o novo SubProblem, ou NULL em caso de erro.
 */
SubProblem *create_subproblem(Sudoku *s) {
    SubProblem *sp = (SubProblem *) malloc(sizeof(SubProblem));
    if (!sp) return NULL;

    int n = s->n;

    /* Grid — cópia linha a linha */
    sp->grid = (int **) malloc(n * sizeof(int *));
    if (!sp->grid) { free(sp); return NULL; }

    for (int i = 0; i < n; i++) {
        sp->grid[i] = (int *) malloc(n * sizeof(int));
        if (!sp->grid[i]) {
            for (int j = 0; j < i; j++) free(sp->grid[j]);
            free(sp->grid); free(sp); return NULL;
        }
        memcpy(sp->grid[i], s->grid[i], n * sizeof(int));
    }

    /* Bitmasks — cópia directa */
    sp->row_mask = (int *) malloc(n * sizeof(int));
    sp->col_mask = (int *) malloc(n * sizeof(int));
    sp->box_mask = (int *) malloc(n * sizeof(int));

    if (!sp->row_mask || !sp->col_mask || !sp->box_mask) {
        free(sp->row_mask); free(sp->col_mask); free(sp->box_mask);
        for (int i = 0; i < n; i++) free(sp->grid[i]);
        free(sp->grid); free(sp); return NULL;
    }

    memcpy(sp->row_mask, s->row_mask, n * sizeof(int));
    memcpy(sp->col_mask, s->col_mask, n * sizeof(int));
    memcpy(sp->box_mask, s->box_mask, n * sizeof(int));

    return sp;
}

/**
 * @brief Libera um SubProblem alocado por create_subproblem.
 */
void free_subproblem(SubProblem *sp, int n) {
    if (!sp) return;
    if (sp->grid) {
        for (int i = 0; i < n; i++) free(sp->grid[i]);
        free(sp->grid);
    }
    free(sp->row_mask);
    free(sp->col_mask);
    free(sp->box_mask);
    free(sp);
}

/* =========================================================================
 * Backtracking com Bitmasks (O(1) por verificação)
 * ========================================================================= */

/**
 * @brief Resolve o puzzle por backtracking com verificação O(1) via bitmasks.
 *
 * DIFERENÇA FUNDAMENTAL face à versão original:
 *   Versão original — is_valid percorre linha (n iter) + coluna (n iter)
 *                     + bloco (L² iter) = até 3n comparações por tentativa.
 *   Esta versão      — verifica com uma única operação de bits:
 *                     (row | col | box) & (1 << num) == 0  →  válido.
 *
 * As bitmasks são actualizadas incrementalmente: ao colocar um número,
 * activamos o bit; ao fazer backtrack, desactivamos. Custo: O(1) por
 * colocação e O(1) por remoção, em vez de O(n) para re-verificar.
 *
 * Verifica a flag `found` a cada nível para terminar antecipadamente
 * quando outra thread já encontrou a solução.
 *
 * @param  sp        Estado local da thread (grid + bitmasks).
 * @param  n         Dimensão do tabuleiro.
 * @param  L         Dimensão do bloco.
 * @param  found     Flag partilhada de terminação antecipada.
 * @param  solution  Buffer onde copiar o estado final se resolvido.
 * @return           1 se resolvido, 0 caso contrário.
 */
int solve_with_cutoff(SubProblem *sp, int n, int L,
                      volatile int *found, SubProblem *solution) {

    if (*found) return 0;

    /* Encontra a próxima célula vazia */
    int row = -1, col = -1;
    for (int i = 0; i < n && row == -1; i++)
        for (int j = 0; j < n && row == -1; j++)
            if (sp->grid[i][j] == 0) { row = i; col = j; }

    /* Nenhuma célula vazia: solução encontrada */
    if (row == -1) return 1;

    int box = box_index(row, col, L);

    /* Máscara dos números já proibidos nesta célula */
    int used = sp->row_mask[row] | sp->col_mask[col] | sp->box_mask[box];

    for (int num = 1; num <= n; num++) {
        if (*found) return 0;

        int bit = 1 << num;

        /*
         * VERIFICAÇÃO O(1):
         * Se o bit do número não está em nenhuma máscara, é válido.
         * Substitui os 3 loops da versão original.
         */
        if (used & bit) continue;

        /* Coloca o número e actualiza bitmasks */
        sp->grid[row][col]  = num;
        sp->row_mask[row]  |= bit;
        sp->col_mask[col]  |= bit;
        sp->box_mask[box]  |= bit;

        if (solve_with_cutoff(sp, n, L, found, solution)) {
            /* Copia solução para o buffer partilhado antes de sinalizar */
            if (solution) {
                for (int i = 0; i < n; i++)
                    memcpy(solution->grid[i], sp->grid[i], n * sizeof(int));
            }
            return 1;
        }

        /* Backtrack: remove o número e repõe bitmasks */
        sp->grid[row][col]  = 0;
        sp->row_mask[row]  ^= bit;
        sp->col_mask[col]  ^= bit;
        sp->box_mask[box]  ^= bit;
    }

    return 0;
}

/* =========================================================================
 * Paralelização OpenMP com geração de sub-problemas em múltiplos níveis
 * ========================================================================= */

/**
 * @brief Gera recursivamente sub-problemas expandindo K níveis do backtracking.
 *
 * Em vez de paralelizar apenas no primeiro nível (máx. n tarefas), esta
 * função expande a árvore até K níveis, produzindo potencialmente n^K
 * sub-problemas. Isso garante que há tarefas suficientes para todas as
 * threads, mesmo quando a primeira célula só tem 2-3 valores válidos.
 *
 * @param  s        Estado base do tabuleiro.
 * @param  depth    Níveis restantes a expandir.
 * @param  tasks    Array onde guardar os sub-problemas gerados.
 * @param  n_tasks  Contador de sub-problemas (modificado in-place).
 * @param  max_t    Capacidade máxima do array tasks.
 */
static void generate_tasks(Sudoku *s, int depth,
                            SubProblem **tasks, int *n_tasks, int max_t) {
    if (depth == 0 || *n_tasks >= max_t) {
        if (*n_tasks < max_t) {
            tasks[*n_tasks] = create_subproblem(s);
            if (tasks[*n_tasks]) (*n_tasks)++;
        }
        return;
    }

    /* Encontra a próxima célula vazia */
    int row = -1, col = -1;
    for (int i = 0; i < s->n && row == -1; i++)
        for (int j = 0; j < s->n && row == -1; j++)
            if (s->grid[i][j] == 0) { row = i; col = j; }

    if (row == -1) {
        /* Tabuleiro já completo — sub-problema trivial */
        if (*n_tasks < max_t) {
            tasks[*n_tasks] = create_subproblem(s);
            if (tasks[*n_tasks]) (*n_tasks)++;
        }
        return;
    }

    int box = box_index(row, col, s->L);
    int used = s->row_mask[row] | s->col_mask[col] | s->box_mask[box];

    for (int num = 1; num <= s->n && *n_tasks < max_t; num++) {
        int bit = 1 << num;
        if (used & bit) continue;

        /* Aplica num e expande recursivamente */
        s->grid[row][col]  = num;
        s->row_mask[row]  |= bit;
        s->col_mask[col]  |= bit;
        s->box_mask[box]  |= bit;

        generate_tasks(s, depth - 1, tasks, n_tasks, max_t);

        /* Desfaz para explorar o próximo valor */
        s->grid[row][col]  = 0;
        s->row_mask[row]  ^= bit;
        s->col_mask[col]  ^= bit;
        s->box_mask[box]  ^= bit;
    }
}

/**
 * @brief Resolve o Sudoku com OpenMP, paralelizando sobre sub-problemas.
 *
 * ESTRATÉGIA:
 *   1. Gera sub-problemas expandindo K=3 níveis da árvore de backtracking.
 *      Com n=9 e K=3, potencialmente até 9³=729 sub-problemas, mais do que
 *      suficiente para qualquer número de threads.
 *   2. Distribui esses sub-problemas pelas threads com dynamic scheduling.
 *   3. Cada thread resolve o seu sub-problema de forma completamente
 *      independente (sem partilha de memória durante o backtracking).
 *   4. Quando uma thread encontra a solução, sinaliza via flag atómica e
 *      as restantes terminam antecipadamente na próxima verificação de *found.
 *
 * @param  s  Ponteiro para a estrutura Sudoku a resolver.
 * @return    1 se o puzzle foi resolvido com sucesso, 0 se não tem solução.
 */
int solve_sudoku_parallel(Sudoku *s) {

    /* Máximo de sub-problemas a gerar */
    #define MAX_TASKS 1024

    SubProblem **tasks = (SubProblem **) calloc(MAX_TASKS, sizeof(SubProblem *));
    if (!tasks) return 0;

    int n_tasks = 0;

    /*
     * Gera sub-problemas expandindo 3 níveis.
     * K=3 é um bom compromisso: gera sub-problemas suficientes sem
     * explodir o número de cópias de memória.
     */
    generate_tasks(s, 3, tasks, &n_tasks, MAX_TASKS);

    if (n_tasks == 0) { free(tasks); return 0; }

    volatile int found = 0;

    /*
     * Buffer para a solução — alocado antes da região paralela.
     * A thread vencedora copia a sua grid aqui enquanto ainda detém
     * o resultado, antes de sinalizar `found`.
     */
    SubProblem *solution = create_subproblem(s);
    if (!solution) {
        for (int i = 0; i < n_tasks; i++) free_subproblem(tasks[i], s->n);
        free(tasks); return 0;
    }

    #pragma omp parallel for schedule(dynamic, 1) shared(found, solution)
    for (int t = 0; t < n_tasks; t++) {
        if (found) continue;

        SubProblem *sp = tasks[t];

        /*
         * Cada thread usa o seu próprio SubProblem isolado.
         * Não há partilha de grid durante o backtracking — sem race
         * conditions e sem necessidade de locks no caminho crítico.
         */
        if (solve_with_cutoff(sp, s->n, s->L, &found, solution)) {
            #pragma omp atomic write
            found = 1;
        }
    }

    /* Copia a solução de volta para o tabuleiro original */
    int solved = 0;
    if (found) {
        for (int i = 0; i < s->n; i++)
            memcpy(s->grid[i], solution->grid[i], s->n * sizeof(int));
        solved = 1;
    }

    free_subproblem(solution, s->n);
    for (int i = 0; i < n_tasks; i++) free_subproblem(tasks[i], s->n);
    free(tasks);

    return solved;
}

/* =========================================================================
 * Ponto de Entrada
 * ========================================================================= */

/**
 * @brief Ponto de entrada do programa.
 *
 * Valida o argumento de linha de comando, lê o ficheiro de entrada,
 * mede o tempo de execução com omp_get_wtime(), invoca o solucionador
 * paralelo e imprime o resultado. O tempo é enviado para stderr e a
 * solução para stdout, permitindo captura e análise separada dos dois fluxos.
 *
 * @param  argc  Número de argumentos (deve ser 2).
 * @param  argv  Vetor de argumentos; argv[1] é o caminho do ficheiro.
 * @return       0 em caso de sucesso, 1 em caso de erro.
 */
int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Uso: %s <ficheiro_entrada>\n", argv[0]);
        return 1;
    }

    Sudoku *s = NULL;
    double exec_time;

    if (!read_sudoku(argv[1], &s)) return 1;

    exec_time = -omp_get_wtime();
    int solved = solve_sudoku_parallel(s);
    exec_time += omp_get_wtime();

    fprintf(stderr, "%.1fs\n", exec_time);

    if (solved)
        print_sudoku(s);
    else
        printf("Nenhuma solução\n");

    free_sudoku(s);
    return 0;
}