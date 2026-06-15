/**
 * @file    sudoku-mpi.c
 * @brief   Solucionador de Sudoku — Implementação Distribuída com MPI
 *
 * Resolve um puzzle Sudoku n×n usando múltiplos processos MPI.
 * Aplica a Metodologia de Foster para paralelização por decomposição
 * do espaço de pesquisa do backtracking.
 *
 * Compilação:
 *   mpicc -O2 sudoku-mpi.c -o sudoku-mpi
 *
 * Execução:
 *   mpirun -np 4 ./sudoku-mpi <ficheiro_entrada>
 *
 * =========================================================================
 * METODOLOGIA DE FOSTER
 * =========================================================================
 *
 * 1. PARTICIONAMENTO (Decomposição do espaço de pesquisa)
 *    - A tarefa primitiva é: dado um tabuleiro com a 1.ª célula vazia
 *      já preenchida com um candidato fixo, resolver o resto por
 *      backtracking completo.
 *    - Se a 1.ª célula vazia admite k candidatos válidos (1..n),
 *      temos k tarefas primitivas independentes entre si.
 *
 * 2. COMUNICAÇÃO
 *    - Processo 0 (mestre) lê o ficheiro e faz broadcast do tabuleiro
 *      inicial (L + n*n inteiros) para todos os processos via MPI_Bcast.
 *    - Não há comunicação entre workers durante a pesquisa (espaços
 *      de pesquisa disjuntos → zero interacção).
 *    - Quando um worker encontra a solução, envia o tabuleiro resolvido
 *      ao mestre via MPI_Send (ponto-a-ponto).
 *    - O mestre aguarda com MPI_Recv usando MPI_ANY_SOURCE para aceitar
 *      a primeira resposta válida.
 *
 * 3. AGREGAÇÃO
 *    - Os candidatos da 1.ª célula vazia são numerados de 1 a n.
 *    - Cada processo trata os candidatos cujo índice satisfaz:
 *        candidato % p == id   (distribuição cíclica)
 *    - Esta agregação garante balanceamento de carga sem comunicação
 *      adicional.
 *
 * 4. MAPEAMENTO
 *    - Um processo MPI por CPU disponível (controlado por mpirun -np).
 *    - Processo 0 acumula papel duplo: distribuição + recepção do resultado.
 *    - Workers (id > 0) apenas pesquisam e, se encontrarem solução, enviam.
 *
 * @author  Grupo 4 — Carlos Tchípia, Emanuel dos Santos, Líria Bá
 * @course  Computação Paralela e Distribuída — ISPTEC 2025/2026
 */

#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* =========================================================================
 * Tags MPI
 * ========================================================================= */
#define TAG_SOLUCAO   10   /* worker → mestre: tabuleiro resolvido   */
#define TAG_SEM_SOL   11   /* worker → mestre: sem solução encontrada */

/* =========================================================================
 * Estrutura de Dados
 * ========================================================================= */

/**
 * @brief Representa o tabuleiro Sudoku.
 *
 * Usa array plano (flat) em vez de int** para facilitar a transmissão
 * via MPI sem tipos derivados: grid[i*n + j] é a célula (i, j).
 */
typedef struct {
    int *grid;  /**< Array plano n*n, alocado dinamicamente. */
    int  n;     /**< Dimensão do tabuleiro (n×n). */
    int  L;     /**< Dimensão de cada bloco interno (L×L), n = L². */
} Sudoku;

/* =========================================================================
 * Protótipos
 * ========================================================================= */

Sudoku *create_sudoku(int L);
void    free_sudoku(Sudoku *s);
int     read_sudoku(const char *filename, Sudoku **s);
void    print_sudoku(Sudoku *s);
int     is_valid(Sudoku *s, int row, int col, int num);
int     find_empty_cell(Sudoku *s, int *row, int *col);
int     solve_sudoku(Sudoku *s);
Sudoku *clone_sudoku(Sudoku *src);

/* =========================================================================
 * Implementação
 * ========================================================================= */

Sudoku *create_sudoku(int L) {
    Sudoku *s = (Sudoku *) malloc(sizeof(Sudoku));
    if (!s) return NULL;

    s->L    = L;
    s->n    = L * L;
    s->grid = (int *) calloc(s->n * s->n, sizeof(int));
    if (!s->grid) { free(s); return NULL; }

    return s;
}

void free_sudoku(Sudoku *s) {
    if (!s) return;
    free(s->grid);
    free(s);
}

/**
 * @brief Clona um tabuleiro (cópia profunda).
 * Necessário para que cada candidato parta de um estado independente.
 */
Sudoku *clone_sudoku(Sudoku *src) {
    Sudoku *dst = create_sudoku(src->L);
    if (!dst) return NULL;
    memcpy(dst->grid, src->grid, src->n * src->n * sizeof(int));
    return dst;
}

int read_sudoku(const char *filename, Sudoku **s) {
    FILE *fp = fopen(filename, "r");
    if (!fp) {
        fprintf(stderr, "Erro ao abrir ficheiro: %s\n", filename);
        return 0;
    }

    int L;
    if (fscanf(fp, "%d", &L) != 1 || L < 2 || L > 9) {
        fprintf(stderr, "Valor de L inválido\n");
        fclose(fp);
        return 0;
    }

    *s = create_sudoku(L);
    if (!*s) { fclose(fp); return 0; }

    int n = (*s)->n;
    for (int i = 0; i < n * n; i++) {
        if (fscanf(fp, "%d", &(*s)->grid[i]) != 1) {
            fprintf(stderr, "Erro ao ler célula %d\n", i);
            free_sudoku(*s); *s = NULL;
            fclose(fp); return 0;
        }
        if ((*s)->grid[i] < 0 || (*s)->grid[i] > n) {
            fprintf(stderr, "Valor inválido na célula %d\n", i);
            free_sudoku(*s); *s = NULL;
            fclose(fp); return 0;
        }
    }

    fclose(fp);
    return 1;
}

void print_sudoku(Sudoku *s) {
    for (int i = 0; i < s->n; i++) {
        for (int j = 0; j < s->n; j++) {
            printf("%d", s->grid[i * s->n + j]);
            if (j < s->n - 1) printf(" ");
        }
        printf("\n");
    }
}

int is_valid(Sudoku *s, int row, int col, int num) {
    int n = s->n, L = s->L;

    /* Verificar linha */
    for (int j = 0; j < n; j++)
        if (s->grid[row * n + j] == num) return 0;

    /* Verificar coluna */
    for (int i = 0; i < n; i++)
        if (s->grid[i * n + col] == num) return 0;

    /* Verificar bloco L×L */
    int sr = (row / L) * L;
    int sc = (col / L) * L;
    for (int i = sr; i < sr + L; i++)
        for (int j = sc; j < sc + L; j++)
            if (s->grid[i * n + j] == num) return 0;

    return 1;
}

int find_empty_cell(Sudoku *s, int *row, int *col) {
    int n = s->n;
    for (int i = 0; i < n; i++)
        for (int j = 0; j < n; j++)
            if (s->grid[i * n + j] == 0) {
                *row = i; *col = j;
                return 1;
            }
    return 0;
}

int solve_sudoku(Sudoku *s) {
    int row, col;
    if (!find_empty_cell(s, &row, &col)) return 1;

    for (int num = 1; num <= s->n; num++) {
        if (is_valid(s, row, col, num)) {
            s->grid[row * s->n + col] = num;
            if (solve_sudoku(s)) return 1;
            s->grid[row * s->n + col] = 0;
        }
    }
    return 0;
}

/* =========================================================================
 * Ponto de Entrada
 * ========================================================================= */

int main(int argc, char *argv[]) {
    int id, p;

    MPI_Init(&argc, &argv);
    MPI_Comm_rank(MPI_COMM_WORLD, &id);
    MPI_Comm_size(MPI_COMM_WORLD, &p);

    /* ------------------------------------------------------------------
     * PASSO 1 — Processo 0 lê o ficheiro
     * ------------------------------------------------------------------ */
    int L = 0, n = 0;

    if (id == 0) {
        if (argc != 2) {
            fprintf(stderr, "Uso: %s <ficheiro_entrada>\n", argv[0]);
            MPI_Abort(MPI_COMM_WORLD, 1);
        }
    }

    /* ------------------------------------------------------------------
     * COMUNICAÇÃO (Foster §2): Broadcast de L para todos os processos
     * ------------------------------------------------------------------ */

    /* Processo 0 lê L do ficheiro para fazer o broadcast */
    int *flat_grid = NULL;

    if (id == 0) {
        Sudoku *s0 = NULL;
        if (!read_sudoku(argv[1], &s0)) {
            L = -1; /* sinaliza erro */
            MPI_Bcast(&L, 1, MPI_INT, 0, MPI_COMM_WORLD);
            MPI_Finalize();
            return 1;
        }
        L = s0->L;
        n = s0->n;

        /* Broadcast de L primeiro, para os outros alocarem */
        MPI_Bcast(&L, 1, MPI_INT, 0, MPI_COMM_WORLD);

        /* Broadcast do tabuleiro inicial (array plano n*n) */
        MPI_Bcast(s0->grid, n * n, MPI_INT, 0, MPI_COMM_WORLD);

        flat_grid = (int *) malloc(n * n * sizeof(int));
        memcpy(flat_grid, s0->grid, n * n * sizeof(int));
        free_sudoku(s0);
    } else {
        /* Workers recebem L */
        MPI_Bcast(&L, 1, MPI_INT, 0, MPI_COMM_WORLD);
        if (L == -1) { MPI_Finalize(); return 1; }

        n = L * L;
        flat_grid = (int *) calloc(n * n, sizeof(int));

        /* Workers recebem o tabuleiro inicial */
        MPI_Bcast(flat_grid, n * n, MPI_INT, 0, MPI_COMM_WORLD);
    }

    /* ------------------------------------------------------------------
     * MEDIÇÃO DE TEMPO — barreira para eliminar tempo de inicialização
     * (padrão do exemplo CSAT das aulas)
     * ------------------------------------------------------------------ */
    MPI_Barrier(MPI_COMM_WORLD);
    double tempo = -MPI_Wtime();

    /* ------------------------------------------------------------------
     * PARTICIONAMENTO (Foster §1):
     * Encontrar a 1.ª célula vazia e listar os seus candidatos válidos.
     * Cada candidato é uma tarefa primitiva independente.
     * ------------------------------------------------------------------ */

    /* Reconstruir estrutura local a partir do flat_grid recebido */
    Sudoku base;
    base.L    = L;
    base.n    = n;
    base.grid = flat_grid;

    int first_row = -1, first_col = -1;
    find_empty_cell(&base, &first_row, &first_col);

    int local_solved = 0;
    int *local_result = (int *) calloc(n * n, sizeof(int));

    if (first_row == -1) {
        /* Tabuleiro já completo — processo 0 imprime directamente */
        if (id == 0) {
            memcpy(local_result, flat_grid, n * n * sizeof(int));
            local_solved = 1;
        }
    } else {
        /* ------------------------------------------------------------------
         * AGREGAÇÃO + MAPEAMENTO (Foster §3 e §4):
         * Distribuição cíclica dos candidatos entre processos.
         * Processo id trata os candidatos num tal que (num-1) % p == id.
         * ------------------------------------------------------------------ */
        for (int num = 1; num <= n && !local_solved; num++) {
            if ((num - 1) % p != id) continue; /* não é meu candidato */

            if (!is_valid(&base, first_row, first_col, num)) continue;

            /* Clonar tabuleiro e fixar o candidato */
            Sudoku *candidate = create_sudoku(L);
            if (!candidate) continue;
            memcpy(candidate->grid, flat_grid, n * n * sizeof(int));
            candidate->grid[first_row * n + first_col] = num;

            /* Backtracking no sub-espaço deste candidato */
            if (solve_sudoku(candidate)) {
                local_solved = 1;
                memcpy(local_result, candidate->grid, n * n * sizeof(int));
            }
            free_sudoku(candidate);
        }
    }

    /* ------------------------------------------------------------------
     * COMUNICAÇÃO (Foster §2) — recolha da solução:
     * Workers com solução enviam ao mestre (ponto-a-ponto).
     * Mestre aguarda de qualquer fonte (MPI_ANY_SOURCE).
     * ------------------------------------------------------------------ */

    int global_solved = 0;
    int *final_grid   = NULL;

    if (id == 0) {
        /* Verificar se o próprio mestre encontrou solução */
        if (local_solved) {
            global_solved = 1;
            final_grid = local_result;
            local_result = NULL; /* evitar double-free */
        } else {
            final_grid = (int *) malloc(n * n * sizeof(int));

            /* Receber de qualquer worker que tenha solução */
            /* Primeiro, perguntar a todos quantos encontraram solução */
            MPI_Status status;
            int solutions_received = 0;

            /* Receberemos p-1 mensagens (uma por worker) */
            for (int w = 1; w < p; w++) {
                int tag_recebido;
                MPI_Probe(MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD, &status);
                tag_recebido = status.MPI_TAG;

                if (tag_recebido == TAG_SOLUCAO && solutions_received == 0) {
                    /* Aceitar a primeira solução válida */
                    MPI_Recv(final_grid, n * n, MPI_INT,
                             status.MPI_SOURCE, TAG_SOLUCAO,
                             MPI_COMM_WORLD, MPI_STATUS_IGNORE);
                    global_solved = 1;
                    solutions_received++;
                } else {
                    /* Descartar mensagens extra (sem solução ou duplicadas) */
                    int dummy;
                    if (tag_recebido == TAG_SOLUCAO) {
                        int *buf = (int *) malloc(n * n * sizeof(int));
                        MPI_Recv(buf, n * n, MPI_INT,
                                 status.MPI_SOURCE, TAG_SOLUCAO,
                                 MPI_COMM_WORLD, MPI_STATUS_IGNORE);
                        free(buf);
                    } else {
                        MPI_Recv(&dummy, 1, MPI_INT,
                                 status.MPI_SOURCE, TAG_SEM_SOL,
                                 MPI_COMM_WORLD, MPI_STATUS_IGNORE);
                    }
                }
            }
        }

        tempo += MPI_Wtime();
        fprintf(stderr, "%.1fs\n", tempo);

        if (global_solved) {
            /* Imprimir usando a estrutura auxiliar */
            Sudoku result_s;
            result_s.L    = L;
            result_s.n    = n;
            result_s.grid = final_grid;
            print_sudoku(&result_s);
        } else {
            printf("Nenhuma solução\n");
        }

    } else {
        /* Workers enviam resultado ao mestre */
        if (local_solved) {
            MPI_Send(local_result, n * n, MPI_INT, 0, TAG_SOLUCAO,
                     MPI_COMM_WORLD);
        } else {
            int dummy = 0;
            MPI_Send(&dummy, 1, MPI_INT, 0, TAG_SEM_SOL,
                     MPI_COMM_WORLD);
        }
    }

    /* ------------------------------------------------------------------
     * Limpeza de memória
     * ------------------------------------------------------------------ */
    free(flat_grid);
    if (local_result) free(local_result);
    if (id == 0 && final_grid) free(final_grid);

    MPI_Finalize();
    return 0;
}