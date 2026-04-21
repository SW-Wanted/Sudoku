/**
 * @file    sudoku-omp.c
 * @brief   Solucionador de Sudoku — Implementação Paralela com OpenMP
 *
 * Lê um puzzle Sudoku n×n a partir de um ficheiro, resolve-o usando
 * backtracking recursivo paralelizado e imprime a solução no stdout.
 * O tempo de execução do algoritmo é reportado no stderr usando omp_get_wtime().
 *
 * OPTIMIZAÇÕES IMPLEMENTADAS:
 *   1. Bitmasks O(1) para validação — elimina os 3 loops da versão original
 *   2. MRV (Minimum Remaining Values) — escolhe sempre a célula mais
 *      constrangida, reduzindo drasticamente a árvore de backtracking
 *   3. Lookup table box_lookup[] — elimina o custo de box_index() no
 *      caminho crítico (era 7.8% do CPU time segundo o VTune)
 *   4. Geração de sub-problemas em K=5 níveis — garante tarefas suficientes
 *      para qualquer número de threads (até MAX_TASKS=4096)
 *   5. Early termination com flag atómica — threads param assim que qualquer
 *      thread encontra solução
 *   6. Dynamic scheduling com chunk=1 — balanceia carga irregular do backtracking
 *   7. Cada thread opera em estado isolado (SubProblem privado) — sem race
 *      conditions nem locks no caminho crítico
 *   8. Grid alocada como bloco contíguo — melhor localidade de cache
 *   9. omp_set_num_threads() explícito + OMP_NUM_THREADS respeitado
 *
 * Compilação:
 *   gcc -fopenmp -O3 -march=native sudoku-omp.c -o sudoku-omp
 *
 * Utilização:
 *   OMP_NUM_THREADS=8 ./sudoku-omp <ficheiro_entrada>
 *
 * @author  Grupo 4 — Carlos Tchípia, Emanuel dos Santos, Líria Bá
 * @course  Computação Paralela e Distribuída — ISPTEC 2025/2026
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <omp.h>

/* =========================================================================
 * Constantes
 * ========================================================================= */

#define MAX_TASKS 4096

/* =========================================================================
 * Estruturas de Dados
 * ========================================================================= */

/**
 * @brief Representa o tabuleiro Sudoku n×n com bitmasks de restrições.
 *
 * Mantém três arrays de bitmasks actualizados incrementalmente:
 *   - row_mask[i]      : bits dos números já usados na linha i
 *   - col_mask[j]      : bits dos números já usados na coluna j
 *   - box_mask[b]      : bits dos números já usados no bloco b
 *   - box_lookup[i*n+j]: índice do bloco para a célula (i,j), pré-calculado
 *
 * Um bit k activo (1 << k) significa que o número k já está presente.
 * A verificação de validade é uma única operação de bits O(1).
 */
typedef struct {
    int  *grid_data;  /**< Bloco contíguo n×n (melhor localidade de cache).  */
    int **grid;       /**< Ponteiros de linha para grid_data.                 */
    int  *row_mask;   /**< Bitmask de números usados por linha.               */
    int  *col_mask;   /**< Bitmask de números usados por coluna.              */
    int  *box_mask;   /**< Bitmask de números usados por bloco L×L.           */
    int  *box_lookup; /**< box_lookup[i*n+j] = índice do bloco de (i,j).     */
    int   n;          /**< Dimensão do tabuleiro (n×n).                       */
    int   L;          /**< Dimensão de cada bloco interno (L×L), n = L².     */
} Sudoku;

/**
 * @brief Sub-problema gerado para distribuição pelas threads.
 *
 * Estado parcial independente: grid + bitmasks copiados do estado-pai.
 * Inclui box_lookup como ponteiro partilhado (read-only, sem race condition).
 */
typedef struct {
    int  *grid_data;  /**< Bloco contíguo n×n.                               */
    int **grid;       /**< Ponteiros de linha para grid_data.                 */
    int  *row_mask;
    int  *col_mask;
    int  *box_mask;
    int  *box_lookup; /**< Partilhado com Sudoku principal (só leitura).      */
} SubProblem;

/* =========================================================================
 * Protótipos
 * ========================================================================= */

Sudoku     *create_sudoku(int L);
void        free_sudoku(Sudoku *s);
int         read_sudoku(const char *filename, Sudoku **s);
void        print_sudoku(Sudoku *s);
SubProblem *create_subproblem(Sudoku *s);
SubProblem *clone_subproblem(SubProblem *src, int n, int *box_lookup);
void        free_subproblem(SubProblem *sp, int n);
int         solve_with_cutoff(SubProblem *sp, int n, int L,
                              volatile int *found, SubProblem *solution);
int         solve_sudoku_parallel(Sudoku *s);

/* =========================================================================
 * Criação / Destruição
 * ========================================================================= */

/**
 * @brief Aloca e inicializa uma estrutura Sudoku com bitmasks e lookup table.
 *
 * @param  L  Dimensão do bloco. Deve satisfazer 2 ≤ L ≤ 9.
 * @return    Ponteiro para a estrutura, ou NULL em caso de erro.
 */
Sudoku *create_sudoku(int L) {
    Sudoku *s = (Sudoku *) malloc(sizeof(Sudoku));
    if (!s) return NULL;

    s->L = L;
    s->n = L * L;
    int n = s->n;

    s->grid_data = (int *) calloc(n * n, sizeof(int));
    s->grid      = (int **) malloc(n * sizeof(int *));
    s->row_mask  = (int *) calloc(n, sizeof(int));
    s->col_mask  = (int *) calloc(n, sizeof(int));
    s->box_mask  = (int *) calloc(n, sizeof(int));
    s->box_lookup = (int *) malloc(n * n * sizeof(int));

    if (!s->grid_data || !s->grid || !s->row_mask ||
        !s->col_mask  || !s->box_mask || !s->box_lookup) {
        free(s->grid_data); free(s->grid); free(s->row_mask);
        free(s->col_mask);  free(s->box_mask); free(s->box_lookup);
        free(s);
        return NULL;
    }

    for (int i = 0; i < n; i++)
        s->grid[i] = s->grid_data + i * n;

    for (int i = 0; i < n; i++)
        for (int j = 0; j < n; j++)
            s->box_lookup[i * n + j] = (i / L) * L + (j / L);

    return s;
}

/**
 * @brief Libera toda a memória associada a uma estrutura Sudoku.
 */
void free_sudoku(Sudoku *s) {
    if (!s) return;
    free(s->grid_data);
    free(s->grid);
    free(s->row_mask);
    free(s->col_mask);
    free(s->box_mask);
    free(s->box_lookup);
    free(s);
}

/* =========================================================================
 * Leitura / Impressão
 * ========================================================================= */

/**
 * @brief Lê o puzzle de um ficheiro e constrói as bitmasks e lookup table.
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

            if (val != 0) {
                int bit = 1 << val;
                (*s)->row_mask[i]                        |= bit;
                (*s)->col_mask[j]                        |= bit;
                (*s)->box_mask[(*s)->box_lookup[i*n+j]]  |= bit;
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
 * @brief Cria um SubProblem a partir do estado actual de um Sudoku.
 *
 * O box_lookup é partilhado (só leitura) — sem necessidade de copiar.
 *
 * @param  s  Estado actual do tabuleiro.
 * @return    Ponteiro para o novo SubProblem, ou NULL em caso de erro.
 */
SubProblem *create_subproblem(Sudoku *s) {
    SubProblem *sp = (SubProblem *) malloc(sizeof(SubProblem));
    if (!sp) return NULL;

    int n = s->n;

    sp->grid_data = (int *) malloc(n * n * sizeof(int));
    sp->grid      = (int **) malloc(n * sizeof(int *));
    sp->row_mask  = (int *) malloc(n * sizeof(int));
    sp->col_mask  = (int *) malloc(n * sizeof(int));
    sp->box_mask  = (int *) malloc(n * sizeof(int));

    if (!sp->grid_data || !sp->grid || !sp->row_mask ||
        !sp->col_mask  || !sp->box_mask) {
        free(sp->grid_data); free(sp->grid); free(sp->row_mask);
        free(sp->col_mask);  free(sp->box_mask); free(sp);
        return NULL;
    }

    for (int i = 0; i < n; i++)
        sp->grid[i] = sp->grid_data + i * n;

    memcpy(sp->grid_data, s->grid_data, n * n * sizeof(int));
    memcpy(sp->row_mask,  s->row_mask,  n * sizeof(int));
    memcpy(sp->col_mask,  s->col_mask,  n * sizeof(int));
    memcpy(sp->box_mask,  s->box_mask,  n * sizeof(int));

    sp->box_lookup = s->box_lookup;

    return sp;
}

/**
 * @brief Clona um SubProblem existente (usado em generate_tasks).
 *
 * @param  src        SubProblem de origem.
 * @param  n          Dimensão do tabuleiro.
 * @param  box_lookup Lookup table partilhada (só leitura).
 * @return            Novo SubProblem ou NULL em caso de erro.
 */
SubProblem *clone_subproblem(SubProblem *src, int n, int *box_lookup) {
    SubProblem *sp = (SubProblem *) malloc(sizeof(SubProblem));
    if (!sp) return NULL;

    sp->grid_data = (int *) malloc(n * n * sizeof(int));
    sp->grid      = (int **) malloc(n * sizeof(int *));
    sp->row_mask  = (int *) malloc(n * sizeof(int));
    sp->col_mask  = (int *) malloc(n * sizeof(int));
    sp->box_mask  = (int *) malloc(n * sizeof(int));

    if (!sp->grid_data || !sp->grid || !sp->row_mask ||
        !sp->col_mask  || !sp->box_mask) {
        free(sp->grid_data); free(sp->grid); free(sp->row_mask);
        free(sp->col_mask);  free(sp->box_mask); free(sp);
        return NULL;
    }

    for (int i = 0; i < n; i++)
        sp->grid[i] = sp->grid_data + i * n;

    memcpy(sp->grid_data, src->grid_data, n * n * sizeof(int));
    memcpy(sp->row_mask,  src->row_mask,  n * sizeof(int));
    memcpy(sp->col_mask,  src->col_mask,  n * sizeof(int));
    memcpy(sp->box_mask,  src->box_mask,  n * sizeof(int));

    sp->box_lookup = box_lookup;

    return sp;
}

/**
 * @brief Libera um SubProblem. Não libera box_lookup (partilhado).
 */
void free_subproblem(SubProblem *sp, int n) {
    (void) n;
    if (!sp) return;
    free(sp->grid_data);
    free(sp->grid);
    free(sp->row_mask);
    free(sp->col_mask);
    free(sp->box_mask);
    free(sp);
}

/* =========================================================================
 * Backtracking com Bitmasks O(1) + MRV
 * ========================================================================= */

/**
 * @brief Resolve o puzzle por backtracking com bitmasks O(1) e heurística MRV.
 *
 * MRV (Minimum Remaining Values): em vez de escolher a primeira célula vazia
 * encontrada, escolhe a célula com menos candidatos válidos. Células com
 * apenas 1 candidato são imediatamente escolhidas (valor forçado).
 * Esta heurística reduz a árvore de backtracking em ordens de magnitude
 * face à escolha ingénua da primeira célula vazia.
 *
 * A flag `found` é verificada a cada chamada para permitir early termination
 * quando outra thread já encontrou a solução.
 *
 * @param  sp        Estado local da thread (grid + bitmasks).
 * @param  n         Dimensão do tabuleiro.
 * @param  L         Dimensão do bloco.
 * @param  found     Flag partilhada de terminação antecipada.
 * @param  solution  Buffer onde copiar a grid se resolvido.
 * @return           1 se resolvido, 0 caso contrário.
 */
int solve_with_cutoff(SubProblem *sp, int n, int L,
                      volatile int *found, SubProblem *solution) {
    if (*found) return 0;

    int best_row = -1, best_col = -1, best_count = n + 1;

    for (int i = 0; i < n; i++) {
        for (int j = 0; j < n; j++) {
            if (sp->grid[i][j] != 0) continue;

            int box  = sp->box_lookup[i * n + j];
            int used = sp->row_mask[i] | sp->col_mask[j] | sp->box_mask[box];
            int free_bits = (~used) & (((1 << (n + 1)) - 1) & ~1);
            int count = __builtin_popcount(free_bits);

            if (count == 0) return 0;

            if (count < best_count) {
                best_count = count;
                best_row   = i;
                best_col   = j;
                if (best_count == 1) goto cell_chosen;
            }
        }
    }

cell_chosen:
    if (best_row == -1) {
        if (solution) {
            #pragma omp critical
            {
                if (!*found)
                    memcpy(solution->grid_data, sp->grid_data, n * n * sizeof(int));
            }
        }
        return 1;
    }

    int box  = sp->box_lookup[best_row * n + best_col];
    int used = sp->row_mask[best_row] | sp->col_mask[best_col] | sp->box_mask[box];

    for (int num = 1; num <= n; num++) {
        if (*found) return 0;

        int bit = 1 << num;
        if (used & bit) continue;

        sp->grid[best_row][best_col]  = num;
        sp->row_mask[best_row]       |= bit;
        sp->col_mask[best_col]       |= bit;
        sp->box_mask[box]            |= bit;

        if (solve_with_cutoff(sp, n, L, found, solution))
            return 1;

        sp->grid[best_row][best_col]  = 0;
        sp->row_mask[best_row]       ^= bit;
        sp->col_mask[best_col]       ^= bit;
        sp->box_mask[box]            ^= bit;
    }

    return 0;
}

/* =========================================================================
 * Paralelização OpenMP com geração de sub-problemas em múltiplos níveis
 * ========================================================================= */

/**
 * @brief Gera recursivamente sub-problemas expandindo K níveis do backtracking.
 *
 * Expande a árvore até K níveis usando a mesma heurística MRV do solver,
 * produzindo potencialmente n^K sub-problemas. Com K=5 e n=16, há sub-
 * problemas mais do que suficientes para qualquer número de threads.
 *
 * @param  sp         Estado base actual (modificado in-place e restaurado).
 * @param  n          Dimensão do tabuleiro.
 * @param  L          Dimensão do bloco.
 * @param  depth      Níveis restantes a expandir.
 * @param  box_lookup Lookup table partilhada.
 * @param  tasks      Array onde guardar os sub-problemas gerados.
 * @param  n_tasks    Contador de sub-problemas (modificado in-place).
 */
static void generate_tasks(SubProblem *sp, int n, int L, int depth,
                            int *box_lookup,
                            SubProblem **tasks, int *n_tasks) {
    if (depth == 0 || *n_tasks >= MAX_TASKS) {
        if (*n_tasks < MAX_TASKS) {
            SubProblem *copy = clone_subproblem(sp, n, box_lookup);
            if (copy) tasks[(*n_tasks)++] = copy;
        }
        return;
    }

    int best_row = -1, best_col = -1, best_count = n + 1;

    for (int i = 0; i < n; i++) {
        for (int j = 0; j < n; j++) {
            if (sp->grid[i][j] != 0) continue;

            int box  = box_lookup[i * n + j];
            int used = sp->row_mask[i] | sp->col_mask[j] | sp->box_mask[box];
            int free_bits = (~used) & (((1 << (n + 1)) - 1) & ~1);
            int count = __builtin_popcount(free_bits);

            if (count == 0) return;

            if (count < best_count) {
                best_count = count;
                best_row   = i;
                best_col   = j;
                if (best_count == 1) goto cell_found;
            }
        }
    }

cell_found:
    if (best_row == -1) {
        if (*n_tasks < MAX_TASKS) {
            SubProblem *copy = clone_subproblem(sp, n, box_lookup);
            if (copy) tasks[(*n_tasks)++] = copy;
        }
        return;
    }

    int box  = box_lookup[best_row * n + best_col];
    int used = sp->row_mask[best_row] | sp->col_mask[best_col] | sp->box_mask[box];

    for (int num = 1; num <= n && *n_tasks < MAX_TASKS; num++) {
        int bit = 1 << num;
        if (used & bit) continue;

        sp->grid[best_row][best_col]  = num;
        sp->row_mask[best_row]       |= bit;
        sp->col_mask[best_col]       |= bit;
        sp->box_mask[box]            |= bit;

        generate_tasks(sp, n, L, depth - 1, box_lookup, tasks, n_tasks);

        sp->grid[best_row][best_col]  = 0;
        sp->row_mask[best_row]       ^= bit;
        sp->col_mask[best_col]       ^= bit;
        sp->box_mask[box]            ^= bit;
    }
}

/**
 * @brief Resolve o Sudoku com OpenMP, paralelizando sobre sub-problemas.
 *
 * ESTRATÉGIA:
 *   1. Usa OMP_NUM_THREADS (via omp_get_max_threads) para respeitar a
 *      variável de ambiente correctamente.
 *   2. Gera sub-problemas expandindo K=5 níveis com heurística MRV, o
 *      que garante tarefas suficientes para qualquer número de threads.
 *   3. Distribui os sub-problemas com dynamic scheduling (chunk=1).
 *   4. Cada thread resolve o seu sub-problema de forma completamente
 *      independente (sem partilha de grid durante o backtracking).
 *   5. Quando uma thread encontra a solução, sinaliza via flag atómica
 *      e as restantes terminam na próxima verificação de *found.
 *
 * @param  s  Ponteiro para a estrutura Sudoku a resolver.
 * @return    1 se o puzzle foi resolvido com sucesso, 0 se não tem solução.
 */
int solve_sudoku_parallel(Sudoku *s) {
    int n_threads = omp_get_max_threads();
    fprintf(stderr, "Threads configuradas: %d\n", n_threads);

    SubProblem **tasks = (SubProblem **) calloc(MAX_TASKS, sizeof(SubProblem *));
    if (!tasks) return 0;

    int n_tasks = 0;

    SubProblem *root = create_subproblem(s);
    if (!root) { free(tasks); return 0; }

    generate_tasks(root, s->n, s->L, 5, s->box_lookup, tasks, &n_tasks);
    free_subproblem(root, s->n);

    fprintf(stderr, "Sub-problemas gerados: %d\n", n_tasks);

    if (n_tasks == 0) { free(tasks); return 0; }

    volatile int found = 0;

    SubProblem *solution = create_subproblem(s);
    if (!solution) {
        for (int i = 0; i < n_tasks; i++) free_subproblem(tasks[i], s->n);
        free(tasks); return 0;
    }

    #pragma omp parallel for schedule(dynamic, 1) shared(found, solution) num_threads(n_threads)
    for (int t = 0; t < n_tasks; t++) {
        if (found) continue;

        if (solve_with_cutoff(tasks[t], s->n, s->L, &found, solution)) {
            #pragma omp atomic write
            found = 1;
        }
    }

    int solved = 0;
    if (found) {
        memcpy(s->grid_data, solution->grid_data, s->n * s->n * sizeof(int));
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

    if (!read_sudoku(argv[1], &s)) return 1;

    double t0 = omp_get_wtime();
    int solved = solve_sudoku_parallel(s);
    double elapsed = omp_get_wtime() - t0;

    fprintf(stderr, "Tempo: %.3fs\n", elapsed);

    if (solved)
        print_sudoku(s);
    else
        printf("Nenhuma solução\n");

    free_sudoku(s);
    return 0;
}