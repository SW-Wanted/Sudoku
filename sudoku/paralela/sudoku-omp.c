/**
 * @file    sudoku-paralela.c
 * @brief   Solucionador de Sudoku — Implementação Paralela com OpenMP
 *
 * Lê um puzzle Sudoku n×n a partir de um ficheiro, resolve-o usando
 * backtracking recursivo paralelizado e imprime a solução no stdout. 
 * O tempo de execução do algoritmo é reportado no stderr usando omp_get_wtime().
 *
 * Compilação:
 *   gcc -fopenmp -O2 sudoku-paralela.c -o sudoku-paralela
 *
 * Utilização:
 *   ./sudoku-paralela <ficheiro_entrada>
 *
 * @author  Grupo 4 — Carlos Tchípia, Emanuel dos Santos, Líria Bá
 * @course  Computação Paralela e Distribuída — ISPTEC 2025/2026
 */

#include <stdio.h>
#include <stdlib.h>
#include <omp.h>

/* =========================================================================
 * Estrutura de Dados
 * ========================================================================= */

/**
 * @brief Representa o tabuleiro de um puzzle Sudoku de tamanho variável.
 *
 * O tabuleiro é uma matriz quadrada n×n, dividida em n blocos de L×L células.
 * A relação entre os campos é sempre n = L². As células vazias são
 * representadas pelo valor 0; células preenchidas contêm valores no
 * intervalo [1, n].
 */
typedef struct {
    int **grid;  /**< Matriz n×n alocada dinamicamente. */
    int   n;     /**< Dimensão do tabuleiro (n×n). */
    int   L;     /**< Dimensão de cada bloco interno (L×L), onde n = L². */
} Sudoku;

/* =========================================================================
 * Protótipos
 * ========================================================================= */

Sudoku *create_sudoku(int L);
Sudoku *copy_sudoku(Sudoku *s);
void    free_sudoku(Sudoku *s);
int     read_sudoku(const char *filename, Sudoku **s);
void    print_sudoku(Sudoku *s);
int     is_valid(Sudoku *s, int row, int col, int num);
int     find_empty_cell(Sudoku *s, int *row, int *col);
int     solve_sudoku_serial(Sudoku *s);
int     solve_sudoku_parallel(Sudoku *s);

/* =========================================================================
 * Implementação
 * ========================================================================= */

/**
 * @brief Aloca e inicializa uma estrutura Sudoku para o tamanho de bloco L.
 *
 * Calcula n = L², aloca a matriz n×n com calloc (todos os valores
 * inicializados a 0) e preenche os campos da estrutura. Em caso de falha
 * em qualquer alocação, libera tudo o que já foi alocado e devolve NULL.
 *
 * @param  L  Dimensão do bloco (√n). Deve satisfazer 2 ≤ L ≤ 9.
 * @return    Ponteiro para a estrutura criada, ou NULL em caso de erro.
 */
Sudoku *create_sudoku(int L) {
    Sudoku *s = (Sudoku *) malloc(sizeof(Sudoku));
    if (!s) {
        fprintf(stderr, "Erro ao alocar memória para Sudoku\n");
        return NULL;
    }

    s->L = L;
    s->n = L * L;

    s->grid = (int **) malloc(s->n * sizeof(int *));
    if (!s->grid) {
        fprintf(stderr, "Erro ao alocar memória para grid\n");
        free(s);
        return NULL;
    }


    for (int i = 0; i < s->n; i++) {
        s->grid[i] = (int *) calloc(s->n, sizeof(int));
        if (!s->grid[i]) {
            fprintf(stderr, "Erro ao alocar memória para linha %d\n", i);
            for (int j = 0; j < i; j++) free(s->grid[j]);
            free(s->grid);
            free(s);
            return NULL;
        }
    }

    return s;
}

/**
 * @brief Cria uma cópia profunda de uma estrutura Sudoku.
 *
 * Aloca uma nova estrutura com as mesmas dimensões e copia todos os
 * valores da matriz. Utilizada para criar tabuleiros independentes
 * para cada thread na versão paralela.
 *
 * @param  s  Ponteiro para a estrutura Sudoku a copiar.
 * @return    Ponteiro para a nova estrutura, ou NULL em caso de erro.
 */
Sudoku *copy_sudoku(Sudoku *s) {
    if (!s) return NULL;

    Sudoku *copy = create_sudoku(s->L);
    if (!copy) return NULL;

    for (int i = 0; i < s->n; i++) {
        for (int j = 0; j < s->n; j++) {
            copy->grid[i][j] = s->grid[i][j];
        }
    }

    return copy;
}

/**
 * @brief Libera toda a memória associada a uma estrutura Sudoku.
 *
 * Percorre cada linha da matriz e libera-a individualmente, depois libera
 * o array de ponteiros e por fim a própria estrutura. É seguro chamar
 * esta função com s == NULL.
 *
 * @param s  Ponteiro para a estrutura a libertar.
 */
void free_sudoku(Sudoku *s) {
    if (!s) return;

    if (s->grid) {
        for (int i = 0; i < s->n; i++) {
            if (s->grid[i]) free(s->grid[i]);
        }
        free(s->grid);
    }
    free(s);
}

/**
 * @brief Lê um puzzle Sudoku de um ficheiro de texto e preenche a estrutura.
 *
 * O formato esperado do ficheiro é: primeira linha contém L (dimensão do
 * bloco); as n=L² linhas seguintes contêm cada uma n inteiros separados
 * por espaço, no intervalo [0, n], onde 0 representa célula vazia.
 * A função valida L (2 ≤ L ≤ 9) e cada valor lido. Em caso de erro fecha
 * o ficheiro, libera a estrutura parcialmente criada e devolve 0.
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
        fprintf(stderr, "Valor de L inválido: deve estar entre 2 e 9\n");
        fclose(fp);
        return 0;
    }

    *s = create_sudoku(L);
    if (!*s) {
        fclose(fp);
        return 0;
    }

    for (int i = 0; i < (*s)->n; i++) {
        for (int j = 0; j < (*s)->n; j++) {
            if (fscanf(fp, "%d", &(*s)->grid[i][j]) != 1) {
                fprintf(stderr, "Erro ao ler célula [%d][%d]\n", i, j);
                free_sudoku(*s);
                *s = NULL;
                fclose(fp);
                return 0;
            }

            if ((*s)->grid[i][j] < 0 || (*s)->grid[i][j] > (*s)->n) {
                fprintf(stderr, "Valor inválido na célula [%d][%d]: %d\n",
                        i, j, (*s)->grid[i][j]);
                free_sudoku(*s);
                *s = NULL;
                fclose(fp);
                return 0;
            }
        }
    }

    fclose(fp);
    return 1;
}

/**
 * @brief Imprime o tabuleiro resolvido no stdout.
 *
 * Percorre todas as n linhas e n colunas da matriz, imprimindo cada valor
 * seguido de um espaço, excepto o último valor de cada linha. Cada linha
 * é terminada com '\n'. O formato é exactamente o especificado no enunciado.
 *
 * @param s  Ponteiro para a estrutura Sudoku com o tabuleiro preenchido.
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

/**
 * @brief Verifica se um número pode ser colocado numa célula sem violar as regras.
 *
 * As três restrições verificadas são: (1) o número não aparece em nenhuma
 * outra célula da mesma linha; (2) o mesmo para a coluna; (3) o mesmo para
 * o bloco L×L ao qual a célula pertence. O canto superior esquerdo desse
 * bloco é determinado por start_row = (row/L)*L e start_col = (col/L)*L.
 *
 * @param  s    Ponteiro para a estrutura Sudoku.
 * @param  row  Índice da linha da célula alvo (0-indexado).
 * @param  col  Índice da coluna da célula alvo (0-indexado).
 * @param  num  Valor a testar, no intervalo [1, n].
 * @return      1 se a colocação é válida, 0 caso contrário.
 */
int is_valid(Sudoku *s, int row, int col, int num) {
    for (int j = 0; j < s->n; j++)
        if (s->grid[row][j] == num) return 0;

    for (int i = 0; i < s->n; i++)
        if (s->grid[i][col] == num) return 0;

    int start_row = (row / s->L) * s->L;
    int start_col = (col / s->L) * s->L;

    for (int i = start_row; i < start_row + s->L; i++)
        for (int j = start_col; j < start_col + s->L; j++)
            if (s->grid[i][j] == num) return 0;

    return 1;
}

/**
 * @brief Encontra a primeira célula vazia no tabuleiro em ordem de leitura.
 *
 * Percorre o tabuleiro da esquerda para a direita e de cima para baixo,
 * procurando a primeira célula cujo valor seja 0. Quando encontrada,
 * escreve os seus índices nos ponteiros fornecidos e devolve 1. Se não
 * existir nenhuma célula vazia, devolve 0 (tabuleiro completamente
 * preenchido).
 *
 * @param  s    Ponteiro para a estrutura Sudoku.
 * @param  row  Ponteiro onde será escrito o índice da linha encontrada.
 * @param  col  Ponteiro onde será escrito o índice da coluna encontrada.
 * @return      1 se existe célula vazia, 0 se o tabuleiro está completo.
 */
int find_empty_cell(Sudoku *s, int *row, int *col) {
    for (*row = 0; *row < s->n; (*row)++)
        for (*col = 0; *col < s->n; (*col)++)
            if (s->grid[*row][*col] == 0) return 1;
    return 0;
}

/**
 * @brief Resolve o Sudoku recursivamente por backtracking (versão serial).
 *
 * A cada chamada, localiza a próxima célula vazia; se não existir, o
 * tabuleiro está completo e devolve 1. Caso contrário, testa os valores
 * de 1 a n: para cada valor válido (verificado por is_valid), coloca-o na
 * célula e chama-se recursivamente. Se a chamada recursiva falhar, repõe
 * a célula a 0 (backtrack) e tenta o valor seguinte. Se nenhum valor
 * funcionar, devolve 0, propagando o backtrack para o nível anterior.
 *
 * @param  s  Ponteiro para a estrutura Sudoku a resolver.
 * @return    1 se o puzzle foi resolvido com sucesso, 0 se não tem solução.
 */
int solve_sudoku_serial(Sudoku *s) {
    int row, col;

    if (!find_empty_cell(s, &row, &col)) return 1;

    for (int num = 1; num <= s->n; num++) {
        if (is_valid(s, row, col, num)) {
            s->grid[row][col] = num;
            if (solve_sudoku_serial(s)) return 1;
            s->grid[row][col] = 0;
        }
    }

    return 0;
}

/**
 * @brief Resolve o Sudoku usando paralelização OpenMP na primeira célula vazia.
 *
 * Encontra a primeira célula vazia e paraleliza a tentativa de diferentes
 * valores (1 a n) usando OpenMP tasks. Cada thread trabalha com uma cópia
 * independente do tabuleiro. Quando uma thread encontra solução, uma flag
 * compartilhada é ativada para cancelar as outras threads. A solução é
 * copiada de volta para o tabuleiro original.
 *
 * @param  s  Ponteiro para a estrutura Sudoku a resolver.
 * @return    1 se o puzzle foi resolvido com sucesso, 0 se não tem solução.
 */
int solve_sudoku_parallel(Sudoku *s) {
    int row, col;

    if (!find_empty_cell(s, &row, &col)) return 1;

    int found = 0;
    Sudoku *solution = NULL;

    #pragma omp parallel shared(found, solution)
    {
        #pragma omp single
        {
            for (int num = 1; num <= s->n; num++) {
                if (is_valid(s, row, col, num)) {
                    #pragma omp task firstprivate(num) shared(found, solution)
                    {
                        if (!found) {
                            Sudoku *local_copy = copy_sudoku(s);
                            if (local_copy) {
                                local_copy->grid[row][col] = num;
                                
                                if (solve_sudoku_serial(local_copy)) {
                                    #pragma omp critical
                                    {
                                        if (!found) {
                                            found = 1;
                                            solution = local_copy;
                                        } else {
                                            free_sudoku(local_copy);
                                        }
                                    }
                                } else {
                                    free_sudoku(local_copy);
                                }
                            }
                        }
                    }
                }
            }
            #pragma omp taskwait
        }
    }

    if (found && solution) {
        for (int i = 0; i < s->n; i++) {
            for (int j = 0; j < s->n; j++) {
                s->grid[i][j] = solution->grid[i][j];
            }
        }
        free_sudoku(solution);
        return 1;
    }

    return 0;
}

/* =========================================================================
 * Ponto de Entrada
 * ========================================================================= */

/**
 * @brief Ponto de entrada do programa.
 *
 * Valida o argumento de linha de comando, lê o ficheiro de entrada,
 * mede o tempo de execução do algoritmo paralelo com omp_get_wtime(), 
 * invoca o solucionador paralelo e imprime o resultado. O tempo é enviado 
 * para stderr e a solução (ou a mensagem "Nenhuma solução") para stdout, 
 * permitindo que os dois fluxos sejam capturados e analisados separadamente.
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
