/**
 * @file    sudoku-mpi.c
 * @brief   Solucionador de Sudoku — Implementação Híbrida MPI + OpenMP
 *
 * Resolve um puzzle Sudoku n×n combinando MPI para paralelismo entre nós
 * (memória distribuída) e OpenMP para paralelismo dentro de cada nó
 * (memória partilhada). Aplica a Metodologia de Foster para decomposição
 * e distribuição do espaço de pesquisa do backtracking.
 *
 * Compilação:
 *   mpicc -fopenmp -O2 sudoku-mpi.c -o sudoku-mpi
 *
 * Execução:
 *   mpirun -np 4 ./sudoku-mpi <ficheiro_entrada>
 *
 * =========================================================================
 * METODOLOGIA DE FOSTER
 * =========================================================================
 *
 * 1. PARTICIONAMENTO (Decomposição do espaço de pesquisa)
 *    - Expandem-se K=4 níveis da árvore de backtracking a partir do estado
 *      inicial, gerando sub-problemas independentes (estados parciais com
 *      candidatos fixados até K níveis de profundidade).
 *    - Cada sub-problema é uma tarefa primitiva: resolver o restante por
 *      backtracking completo com verificação O(1) via bitmasks.
 *    - Com n=9 e K=4, até n^K = 6 561 sub-problemas; na prática muito
 *      menos devido às restrições, mas suficientes para p*t workers.
 *    - Todos os processos geram o mesmo conjunto localmente, sem
 *      comunicação adicional para distribuição de tarefas.
 *
 * 2. COMUNICAÇÃO
 *    - Processo 0 (mestre) lê o ficheiro e faz broadcast de L e do
 *      tabuleiro inicial (n*n inteiros) via MPI_Bcast.
 *    - Sem comunicação entre workers durante a pesquisa (memória disjunta
 *      entre processos; threads dentro do mesmo processo partilham apenas
 *      a flag de terminação e o buffer de resultado).
 *    - Quando um processo encontra solução, envia o tabuleiro ao mestre
 *      via MPI_Send (ponto-a-ponto); caso contrário envia sinal negativo.
 *    - O mestre aguarda p-1 mensagens com MPI_Probe + MPI_Recv
 *      (MPI_ANY_SOURCE) e aceita a primeira solução válida.
 *
 * 3. AGREGAÇÃO
 *    - Distribuição cíclica dos sub-problemas: processo id trata todos
 *      os sub-problemas de índice i tais que i % p == id.
 *    - Dentro de cada processo, as threads OpenMP partilham o subconjunto
 *      local com dynamic scheduling (schedule(dynamic,1)).
 *    - A distribuição cíclica garante balanceamento de carga sem
 *      comunicação adicional.
 *
 * 4. MAPEAMENTO
 *    - Um processo MPI por nó de cálculo; threads OpenMP dentro de cada
 *      processo (controlado por OMP_NUM_THREADS ou omp_set_num_threads).
 *    - Processo 0 acumula papel duplo: coordenação (broadcast + recepção)
 *      e participação na pesquisa (resolve o seu subconjunto de tarefas).
 *
 * @author  Grupo 4 — Carlos Tchípia, Emanuel dos Santos, Líria Bá
 * @course  Computação Paralela e Distribuída — ISPTEC 2025/2026
 */

#include <mpi.h>
#include <omp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* =========================================================================
 * Constantes
 * ========================================================================= */
#define TAG_SOLUCAO 10 /**< worker → mestre: tabuleiro resolvido    */
#define TAG_SEM_SOL 11 /**< worker → mestre: sem solução encontrada */
#define MAX_TASKS 4096 /**< capacidade máxima do array de sub-problemas */
#define EXPAND_DEPTH 4 /**< níveis de expansão da árvore de pesquisa   */

/* =========================================================================
 * Estruturas de Dados
 * ========================================================================= */

/**
 * @brief Representa o tabuleiro Sudoku com array plano e bitmasks.
 *
 * Usa array plano (flat) em vez de int** para facilitar a transmissão MPI
 * sem tipos derivados: grid[i*n + j] é a célula (i, j).
 * As três bitmasks de restrições permitem verificação de validade em O(1),
 * eliminando os três ciclos da versão serial (linha + coluna + bloco).
 */
typedef struct {
  int *grid;     /**< Array plano n*n, alocado dinamicamente.          */
  int *row_mask; /**< Bitmask de números usados por linha.             */
  int *col_mask; /**< Bitmask de números usados por coluna.            */
  int *box_mask; /**< Bitmask de números usados por bloco L×L.         */
  int n;         /**< Dimensão do tabuleiro (n×n).                     */
  int L;         /**< Dimensão de cada bloco interno (L×L), n = L².   */
} Sudoku;

/**
 * @brief Sub-problema independente distribuído a cada thread/processo.
 *
 * Cópia completa do estado (grid + bitmasks) num ponto da árvore de pesquisa,
 * pronta para continuar o backtracking de forma completamente independente,
 * sem partilha de memória de escrita com outros sub-problemas.
 */
typedef struct {
  int *grid;     /**< Array plano n*n com estado parcial.  */
  int *row_mask; /**< Cópia das bitmasks de linha.         */
  int *col_mask; /**< Cópia das bitmasks de coluna.        */
  int *box_mask; /**< Cópia das bitmasks de bloco.         */
} SubProblem;

/* =========================================================================
 * Protótipos
 * ========================================================================= */

Sudoku *create_sudoku(int L);
void free_sudoku(Sudoku *s);
int read_sudoku(const char *filename, Sudoku **s);
void print_sudoku(Sudoku *s);
SubProblem *create_subproblem(int *grid, int *row_mask, int *col_mask,
                              int *box_mask, int n);
void free_subproblem(SubProblem *sp);
void generate_tasks(int *grid, int *row_mask, int *col_mask, int *box_mask,
                    int n, int L, int depth, SubProblem **tasks, int *n_tasks,
                    int max_t);
int solve_with_cutoff(SubProblem *sp, int n, int L, volatile int *found);

/* =========================================================================
 * Auxiliar: índice do bloco
 * ========================================================================= */

/** Calcula o índice linear do bloco L×L ao qual a célula (row, col) pertence.
 */
static inline int box_index(int row, int col, int L) {
  return (row / L) * L + (col / L);
}

/* =========================================================================
 * Criação e Destruição do Sudoku
 * ========================================================================= */

/**
 * @brief Aloca e inicializa uma estrutura Sudoku com grid e bitmasks a zero.
 *
 * Calcula n = L², aloca o array plano n*n e os três arrays de bitmasks com
 * calloc (tudo inicializado a 0). Em caso de falha libera tudo e devolve NULL.
 *
 * @param  L  Dimensão do bloco. Deve satisfazer 2 ≤ L ≤ 9.
 * @return    Ponteiro para a estrutura criada, ou NULL em caso de erro.
 */
Sudoku *create_sudoku(int L) {
  Sudoku *s = (Sudoku *)malloc(sizeof(Sudoku));
  if (!s)
    return NULL;

  s->L = L;
  s->n = L * L;

  s->grid = (int *)calloc(s->n * s->n, sizeof(int));
  s->row_mask = (int *)calloc(s->n, sizeof(int));
  s->col_mask = (int *)calloc(s->n, sizeof(int));
  s->box_mask = (int *)calloc(s->n, sizeof(int));

  if (!s->grid || !s->row_mask || !s->col_mask || !s->box_mask) {
    free(s->grid);
    free(s->row_mask);
    free(s->col_mask);
    free(s->box_mask);
    free(s);
    return NULL;
  }
  return s;
}

/**
 * @brief Libera toda a memória associada a uma estrutura Sudoku.
 *
 * Libera o array plano, os três arrays de bitmasks e a própria estrutura.
 * É seguro chamar com s == NULL.
 *
 * @param s  Ponteiro para a estrutura a libertar.
 */
void free_sudoku(Sudoku *s) {
  if (!s)
    return;
  free(s->grid);
  free(s->row_mask);
  free(s->col_mask);
  free(s->box_mask);
  free(s);
}

/* =========================================================================
 * Leitura e Impressão
 * ========================================================================= */

/**
 * @brief Lê um puzzle Sudoku de um ficheiro de texto.
 *
 * Formato esperado: primeira linha contém L (dimensão do bloco); as n=L²
 * linhas seguintes contêm n inteiros separados por espaço em [0, n],
 * onde 0 representa célula vazia. Valida L (2 ≤ L ≤ 9) e cada valor lido.
 * Não constrói bitmasks — isso é feito separadamente após o broadcast MPI.
 *
 * @param  filename  Caminho para o ficheiro de entrada.
 * @param  s         Endereço do ponteiro que receberá a estrutura criada.
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
    fprintf(stderr, "Valor de L inválido\n");
    fclose(fp);
    return 0;
  }

  *s = create_sudoku(L);
  if (!*s) {
    fclose(fp);
    return 0;
  }

  int n = (*s)->n;
  for (int i = 0; i < n * n; i++) {
    if (fscanf(fp, "%d", &(*s)->grid[i]) != 1) {
      fprintf(stderr, "Erro ao ler célula %d\n", i);
      free_sudoku(*s);
      *s = NULL;
      fclose(fp);
      return 0;
    }
    if ((*s)->grid[i] < 0 || (*s)->grid[i] > n) {
      fprintf(stderr, "Valor inválido na célula %d\n", i);
      free_sudoku(*s);
      *s = NULL;
      fclose(fp);
      return 0;
    }
  }

  fclose(fp);
  return 1;
}

/**
 * @brief Imprime o tabuleiro resolvido no stdout.
 *
 * Percorre o array plano, imprimindo cada valor seguido de espaço, excepto
 * o último de cada linha. Cada linha é terminada com '\n'.
 *
 * @param s  Ponteiro para a estrutura Sudoku com o tabuleiro preenchido.
 */
void print_sudoku(Sudoku *s) {
  for (int i = 0; i < s->n; i++) {
    for (int j = 0; j < s->n; j++) {
      printf("%d", s->grid[i * s->n + j]);
      if (j < s->n - 1)
        printf(" ");
    }
    printf("\n");
  }
}

/* =========================================================================
 * Sub-problemas
 * ========================================================================= */

/**
 * @brief Cria um sub-problema como cópia completa do estado actual.
 *
 * Aloca e copia o array plano da grid e os três arrays de bitmasks.
 * O sub-problema resultante é completamente independente: pode ser resolvido
 * por qualquer thread sem interferir com outros sub-problemas.
 *
 * @param  grid      Array plano n*n com o estado actual.
 * @param  row_mask  Bitmasks de linha.
 * @param  col_mask  Bitmasks de coluna.
 * @param  box_mask  Bitmasks de bloco.
 * @param  n         Dimensão do tabuleiro.
 * @return           Ponteiro para o novo SubProblem, ou NULL em caso de erro.
 */
SubProblem *create_subproblem(int *grid, int *row_mask, int *col_mask,
                              int *box_mask, int n) {
  SubProblem *sp = (SubProblem *)malloc(sizeof(SubProblem));
  if (!sp)
    return NULL;

  sp->grid = (int *)malloc(n * n * sizeof(int));
  sp->row_mask = (int *)malloc(n * sizeof(int));
  sp->col_mask = (int *)malloc(n * sizeof(int));
  sp->box_mask = (int *)malloc(n * sizeof(int));

  if (!sp->grid || !sp->row_mask || !sp->col_mask || !sp->box_mask) {
    free(sp->grid);
    free(sp->row_mask);
    free(sp->col_mask);
    free(sp->box_mask);
    free(sp);
    return NULL;
  }

  memcpy(sp->grid, grid, n * n * sizeof(int));
  memcpy(sp->row_mask, row_mask, n * sizeof(int));
  memcpy(sp->col_mask, col_mask, n * sizeof(int));
  memcpy(sp->box_mask, box_mask, n * sizeof(int));

  return sp;
}

/**
 * @brief Libera um SubProblem alocado por create_subproblem.
 *
 * @param sp  Ponteiro para o SubProblem a libertar.
 */
void free_subproblem(SubProblem *sp) {
  if (!sp)
    return;
  free(sp->grid);
  free(sp->row_mask);
  free(sp->col_mask);
  free(sp->box_mask);
  free(sp);
}

/* =========================================================================
 * Geração de Sub-problemas
 * ========================================================================= */

/**
 * @brief Gera recursivamente sub-problemas expandindo `depth` níveis da árvore.
 *
 * Usa a heurística MRV (Minimum Remaining Values): a cada nível selecciona
 * a célula vazia com menos candidatos válidos. Isso reduz o factor de
 * ramificação e, consequentemente, o número de sub-problemas gerados — mas
 * sub-problemas mais "focados" e menos propensos a serem ramos mortos.
 *
 * Forward checking gratuito: se qualquer célula vazia tiver 0 candidatos,
 * a função retorna imediatamente sem criar nenhum sub-problema para este ramo.
 *
 * @param  grid      Array plano n*n de trabalho (modificado e restaurado).
 * @param  row_mask  Bitmasks de linha (modificadas e restauradas).
 * @param  col_mask  Bitmasks de coluna.
 * @param  box_mask  Bitmasks de bloco.
 * @param  n         Dimensão do tabuleiro.
 * @param  L         Dimensão do bloco.
 * @param  depth     Níveis restantes a expandir.
 * @param  tasks     Array de saída onde os sub-problemas são registados.
 * @param  n_tasks   Contador de sub-problemas gerados (modificado in-place).
 * @param  max_t     Capacidade máxima do array tasks.
 */
void generate_tasks(int *grid, int *row_mask, int *col_mask, int *box_mask,
                    int n, int L, int depth, SubProblem **tasks, int *n_tasks,
                    int max_t) {

  if (*n_tasks >= max_t)
    return;

  int valid_mask = (2 << n) - 2;
  int row = -1, col = -1, min_cnt = n + 1;

  for (int i = 0; i < n && min_cnt > 1; i++) {
    for (int j = 0; j < n && min_cnt > 1; j++) {
      if (grid[i * n + j] != 0)
        continue;
      int box = box_index(i, j, L);
      int used = row_mask[i] | col_mask[j] | box_mask[box];
      int cnt = __builtin_popcount(valid_mask & ~used);
      if (cnt == 0)
        return;
      if (cnt < min_cnt) {
        min_cnt = cnt;
        row = i;
        col = j;
      }
    }
  }

  if (row == -1 || depth == 0) {
    tasks[*n_tasks] = create_subproblem(grid, row_mask, col_mask, box_mask, n);
    if (tasks[*n_tasks])
      (*n_tasks)++;
    return;
  }

  int box = box_index(row, col, L);
  int used = row_mask[row] | col_mask[col] | box_mask[box];
  int available = valid_mask & ~used;

  for (int avail = available; avail && *n_tasks < max_t; avail &= avail - 1) {
    int bit = avail & (-avail);
    int num = __builtin_ctz(bit);

    grid[row * n + col] = num;
    row_mask[row] |= bit;
    col_mask[col] |= bit;
    box_mask[box] |= bit;

    generate_tasks(grid, row_mask, col_mask, box_mask, n, L, depth - 1, tasks,
                   n_tasks, max_t);

    grid[row * n + col] = 0;
    row_mask[row] ^= bit;
    col_mask[col] ^= bit;
    box_mask[box] ^= bit;
  }
}

/* =========================================================================
 * Backtracking com Bitmasks — O(1) por verificação
 * ========================================================================= */

/**
 * @brief Resolve o puzzle por backtracking com MRV, forward checking e
 * bitmasks.
 *
 * OPTIMIZAÇÕES FACE À VERSÃO ANTERIOR:
 *
 *   1. MRV (Minimum Remaining Values): em vez de seleccionar a primeira célula
 *      vazia, varre todas as células e escolhe a que tem menos candidatos
 *      válidos. Reduz dramaticamente o factor de ramificação — por exemplo,
 *      se uma célula só admite 1 candidato, não há ramificação nesse nível.
 *
 *   2. Forward checking: durante o varrimento MRV, se qualquer célula vazia
 *      tiver 0 candidatos válidos, o ramo actual é imediatamente podado sem
 *      descer mais na recursão. Detecta inconsistências com antecedência.
 *
 *   3. Iteração por bits disponíveis: usa `avail &= avail-1` e __builtin_ctz
 *      para iterar apenas sobre os candidatos válidos, evitando o ciclo 1..n
 *      com verificações de skip.
 *
 * As bitmasks são actualizadas incrementalmente: colocação activa o bit,
 * backtrack desactiva com XOR. Custo O(1) por colocação e por remoção.
 * Ao retornar 1, sp->grid contém o tabuleiro completamente preenchido.
 *
 * @param  sp     Estado local da thread (grid + bitmasks, cópia privada).
 * @param  n      Dimensão do tabuleiro.
 * @param  L      Dimensão do bloco.
 * @param  found  Flag de terminação antecipada partilhada entre threads OMP.
 * @return        1 se resolvido, 0 caso contrário.
 */
int solve_with_cutoff(SubProblem *sp, int n, int L, volatile int *found) {
  if (*found)
    return 0;

  int valid_mask = (2 << n) - 2;
  int row = -1, col = -1, min_cnt = n + 1;

  for (int i = 0; i < n && min_cnt > 1; i++) {
    for (int j = 0; j < n && min_cnt > 1; j++) {
      if (sp->grid[i * n + j] != 0)
        continue;
      int box = box_index(i, j, L);
      int used = sp->row_mask[i] | sp->col_mask[j] | sp->box_mask[box];
      int cnt = __builtin_popcount(valid_mask & ~used);
      if (cnt == 0)
        return 0;
      if (cnt < min_cnt) {
        min_cnt = cnt;
        row = i;
        col = j;
      }
    }
  }

  if (row == -1)
    return 1;

  int box = box_index(row, col, L);
  int used = sp->row_mask[row] | sp->col_mask[col] | sp->box_mask[box];
  int available = valid_mask & ~used;

  for (int avail = available; avail; avail &= avail - 1) {
    if (*found)
      return 0;

    int bit = avail & (-avail);
    int num = __builtin_ctz(bit);

    sp->grid[row * n + col] = num;
    sp->row_mask[row] |= bit;
    sp->col_mask[col] |= bit;
    sp->box_mask[box] |= bit;

    if (solve_with_cutoff(sp, n, L, found))
      return 1;

    sp->grid[row * n + col] = 0;
    sp->row_mask[row] ^= bit;
    sp->col_mask[col] ^= bit;
    sp->box_mask[box] ^= bit;
  }

  return 0;
}

/* =========================================================================
 * Ponto de Entrada
 * ========================================================================= */

/**
 * @brief Ponto de entrada do programa híbrido MPI + OpenMP.
 *
 * Processo 0 lê o ficheiro e faz broadcast do tabuleiro. Todos os processos
 * geram o mesmo conjunto de sub-problemas localmente (EXPAND_DEPTH níveis)
 * e cada processo resolve o seu subconjunto (índice i % p == id) usando
 * múltiplas threads OpenMP com dynamic scheduling. O primeiro processo a
 * encontrar solução envia-a ao mestre via MPI_Send; os restantes enviam
 * um sinal negativo. O mestre aguarda p-1 mensagens e imprime o resultado.
 *
 * @param  argc  Número de argumentos (deve ser 2).
 * @param  argv  Vetor de argumentos; argv[1] é o caminho do ficheiro.
 * @return       0 em caso de sucesso, 1 em caso de erro.
 */
int main(int argc, char *argv[]) {
  int id, p;

  MPI_Init(&argc, &argv);
  MPI_Comm_rank(MPI_COMM_WORLD, &id);
  MPI_Comm_size(MPI_COMM_WORLD, &p);

  int L = 0, n = 0;
  int *flat_grid = NULL;

  if (id == 0) {
    if (argc != 2) {
      fprintf(stderr, "Uso: %s <ficheiro_entrada>\n", argv[0]);
      L = -1;
      MPI_Bcast(&L, 1, MPI_INT, 0, MPI_COMM_WORLD);
      MPI_Finalize();
      return 1;
    }

    Sudoku *s0 = NULL;
    if (!read_sudoku(argv[1], &s0)) {
      L = -1;
      MPI_Bcast(&L, 1, MPI_INT, 0, MPI_COMM_WORLD);
      MPI_Finalize();
      return 1;
    }

    L = s0->L;
    n = s0->n;

    MPI_Bcast(&L, 1, MPI_INT, 0, MPI_COMM_WORLD);
    MPI_Bcast(s0->grid, n * n, MPI_INT, 0, MPI_COMM_WORLD);

    flat_grid = (int *)malloc(n * n * sizeof(int));
    memcpy(flat_grid, s0->grid, n * n * sizeof(int));
    free_sudoku(s0);
  } else {
    MPI_Bcast(&L, 1, MPI_INT, 0, MPI_COMM_WORLD);
    if (L == -1) {
      MPI_Finalize();
      return 1;
    }

    n = L * L;
    flat_grid = (int *)calloc(n * n, sizeof(int));
    MPI_Bcast(flat_grid, n * n, MPI_INT, 0, MPI_COMM_WORLD);
  }

  MPI_Barrier(MPI_COMM_WORLD);
  double tempo = -MPI_Wtime();

  int *row_mask = (int *)calloc(n, sizeof(int));
  int *col_mask = (int *)calloc(n, sizeof(int));
  int *box_mask = (int *)calloc(n, sizeof(int));

  for (int i = 0; i < n; i++)
    for (int j = 0; j < n; j++) {
      int val = flat_grid[i * n + j];
      if (val != 0) {
        int bit = 1 << val;
        row_mask[i] |= bit;
        col_mask[j] |= bit;
        box_mask[box_index(i, j, L)] |= bit;
      }
    }

  int *wgrid = (int *)malloc(n * n * sizeof(int));
  int *wrow = (int *)malloc(n * sizeof(int));
  int *wcol = (int *)malloc(n * sizeof(int));
  int *wbox = (int *)malloc(n * sizeof(int));
  memcpy(wgrid, flat_grid, n * n * sizeof(int));
  memcpy(wrow, row_mask, n * sizeof(int));
  memcpy(wcol, col_mask, n * sizeof(int));
  memcpy(wbox, box_mask, n * sizeof(int));

  SubProblem **tasks = (SubProblem **)calloc(MAX_TASKS, sizeof(SubProblem *));
  int n_tasks = 0;
  generate_tasks(wgrid, wrow, wcol, wbox, n, L, EXPAND_DEPTH, tasks, &n_tasks,
                 MAX_TASKS);

  free(wgrid);
  free(wrow);
  free(wcol);
  free(wbox);
  free(row_mask);
  free(col_mask);
  free(box_mask);

  int my_count = 0;
  for (int t = id; t < n_tasks; t += p)
    my_count++;

  SubProblem **my_tasks = NULL;
  if (my_count > 0) {
    my_tasks = (SubProblem **)malloc(my_count * sizeof(SubProblem *));
    int k = 0;
    for (int t = id; t < n_tasks; t += p)
      my_tasks[k++] = tasks[t];
  }

  volatile int local_found = 0;
  int *local_result = (int *)calloc(n * n, sizeof(int));

#pragma omp parallel for schedule(dynamic, 1) shared(local_found, local_result)
  for (int t = 0; t < my_count; t++) {
    if (local_found)
      continue;

    SubProblem *sp = my_tasks[t];

    if (solve_with_cutoff(sp, n, L, &local_found)) {
#pragma omp critical
      if (!local_found) {
        memcpy(local_result, sp->grid, n * n * sizeof(int));
        local_found = 1;
      }
    }
  }

  free(my_tasks);
  for (int t = 0; t < n_tasks; t++)
    free_subproblem(tasks[t]);
  free(tasks);

  int global_solved = 0;
  int *final_grid = NULL;

  if (id == 0) {
    if (local_found) {
      global_solved = 1;
      final_grid = local_result;
      local_result = NULL;
    } else {
      final_grid = (int *)malloc(n * n * sizeof(int));
      MPI_Status status;

      for (int w = 1; w < p; w++) {
        MPI_Probe(MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD, &status);

        if (status.MPI_TAG == TAG_SOLUCAO && !global_solved) {
          MPI_Recv(final_grid, n * n, MPI_INT, status.MPI_SOURCE, TAG_SOLUCAO,
                   MPI_COMM_WORLD, MPI_STATUS_IGNORE);
          global_solved = 1;
        } else if (status.MPI_TAG == TAG_SOLUCAO) {
          int *buf = (int *)malloc(n * n * sizeof(int));
          MPI_Recv(buf, n * n, MPI_INT, status.MPI_SOURCE, TAG_SOLUCAO,
                   MPI_COMM_WORLD, MPI_STATUS_IGNORE);
          free(buf);
        } else {
          int dummy;
          MPI_Recv(&dummy, 1, MPI_INT, status.MPI_SOURCE, TAG_SEM_SOL,
                   MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        }
      }
    }

    tempo += MPI_Wtime();

    if (global_solved) {
      Sudoku tmp = {final_grid, NULL, NULL, NULL, n, L};
      print_sudoku(&tmp);
    } else {
      printf("Nenhuma solução\n");
    }
    fflush(stdout);
    fprintf(stderr, "%.1fs\n", tempo);

  } else {
    if (local_found) {
      MPI_Send(local_result, n * n, MPI_INT, 0, TAG_SOLUCAO, MPI_COMM_WORLD);
    } else {
      int dummy = 0;
      MPI_Send(&dummy, 1, MPI_INT, 0, TAG_SEM_SOL, MPI_COMM_WORLD);
    }
  }

  free(flat_grid);
  if (local_result)
    free(local_result);
  if (id == 0 && final_grid)
    free(final_grid);

  MPI_Finalize();
  return 0;
}
