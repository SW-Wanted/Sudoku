#include <stdio.h>
#include <stdlib.h>
#include <omp.h>

// Estrutura para armazenar o tabuleiro do Sudoku
typedef struct {
    int **grid;      // Matriz do tabuleiro
    int n;           // Tamanho do tabuleiro (n x n)
    int L;           // Tamanho do bloco (L x L), onde n = L²
} Sudoku;

// Protótipos das funções
Sudoku* create_sudoku(int L);
void free_sudoku(Sudoku *s);
int read_sudoku(const char *filename, Sudoku **s);
void print_sudoku(Sudoku *s);
int solve_sudoku(Sudoku *s, int row, int col);
int is_valid(Sudoku *s, int row, int col, int num);
int find_empty_cell(Sudoku *s, int *row, int *col);

/**
 * Cria e inicializa a estrutura do Sudoku
 */
Sudoku* create_sudoku(int L) {
    Sudoku *s = (Sudoku*) malloc(sizeof(Sudoku));
    if (!s) {
        fprintf(stderr, "Erro ao alocar memória para Sudoku\n");
        return NULL;
    }
    
    s->L = L;
    s->n = L * L;
    
    // Aloca a matriz dinamicamente
    s->grid = (int**) malloc(s->n * sizeof(int*));
    if (!s->grid) {
        fprintf(stderr, "Erro ao alocar memória para grid\n");
        free(s);
        return NULL;
    }
    
    for (int i = 0; i < s->n; i++) {
        s->grid[i] = (int*) calloc(s->n, sizeof(int));
        if (!s->grid[i]) {
            fprintf(stderr, "Erro ao alocar memória para linha %d\n", i);
            // Libera memória já alocada
            for (int j = 0; j < i; j++) {
                free(s->grid[j]);
            }
            free(s->grid);
            free(s);
            return NULL;
        }
    }
    
    return s;
}

/**
 * Libera a memória alocada para o Sudoku
 */
void free_sudoku(Sudoku *s) {
    if (!s) return;
    
    if (s->grid) {
        for (int i = 0; i < s->n; i++) {
            if (s->grid[i]) {
                free(s->grid[i]);
            }
        }
        free(s->grid);
    }
    free(s);
}

/**
 * Lê o Sudoku do ficheiro de entrada
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
    
    // Lê a matriz
    for (int i = 0; i < (*s)->n; i++) {
        for (int j = 0; j < (*s)->n; j++) {
            if (fscanf(fp, "%d", &(*s)->grid[i][j]) != 1) {
                fprintf(stderr, "Erro ao ler célula [%d][%d]\n", i, j);
                free_sudoku(*s);
                *s = NULL;
                fclose(fp);
                return 0;
            }
            
            // Valida o valor lido
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
 * Imprime a solução do Sudoku
 */
void print_sudoku(Sudoku *s) {
    for (int i = 0; i < s->n; i++) {
        for (int j = 0; j < s->n; j++) {
            printf("%d", s->grid[i][j]);
            if (j < s->n - 1) {
                printf(" ");
            }
        }
        printf("\n");
    }
}

/**
 * Verifica se é válido colocar um número numa posição
 */
int is_valid(Sudoku *s, int row, int col, int num) {
    // Verifica a linha
    for (int j = 0; j < s->n; j++) {
        if (s->grid[row][j] == num) {
            return 0;
        }
    }
    
    // Verifica a coluna
    for (int i = 0; i < s->n; i++) {
        if (s->grid[i][col] == num) {
            return 0;
        }
    }
    
    // Verifica o bloco L x L
    int start_row = (row / s->L) * s->L;
    int start_col = (col / s->L) * s->L;
    
    for (int i = start_row; i < start_row + s->L; i++) {
        for (int j = start_col; j < start_col + s->L; j++) {
            if (s->grid[i][j] == num) {
                return 0;
            }
        }
    }
    
    return 1;
}

/**
 * Encontra a próxima célula vazia (com valor 0)
 */
int find_empty_cell(Sudoku *s, int *row, int *col) {
    for (*row = 0; *row < s->n; (*row)++) {
        for (*col = 0; *col < s->n; (*col)++) {
            if (s->grid[*row][*col] == 0) {
                return 1;
            }
        }
    }
    return 0;
}

/**
 * Resolve o Sudoku usando backtracking
 */
int solve_sudoku(Sudoku *s, int row, int col) {
    // Encontra próxima célula vazia
    if (!find_empty_cell(s, &row, &col)) {
        return 1; // Todas as células estão preenchidas - solução encontrada
    }
    
    // Tenta números de 1 a n
    for (int num = 1; num <= s->n; num++) {
        if (is_valid(s, row, col, num)) {
            s->grid[row][col] = num;
            
            // Recursivamente tenta resolver o resto
            if (solve_sudoku(s, row, col)) {
                return 1;
            }
            
            // Se não funcionou, volta atrás (backtrack)
            s->grid[row][col] = 0;
        }
    }
    
    return 0; // Nenhuma solução encontrada
}

int main(int argc, char *argv[]) {
    // Verifica argumentos
    if (argc != 2) {
        fprintf(stderr, "Uso: %s <ficheiro_entrada>\n", argv[0]);
        return 1;
    }
    
    Sudoku *s = NULL;
    double exec_time;
    
    // Lê o ficheiro de entrada
    if (!read_sudoku(argv[1], &s)) {
        return 1;
    }
    
    // Inicia a contagem do tempo
    exec_time = -omp_get_wtime();
    
    // Resolve o Sudoku
    int solved = solve_sudoku(s, 0, 0);
    
    // Finaliza a contagem do tempo
    exec_time += omp_get_wtime();
    
    // Imprime o tempo no stderr
    fprintf(stderr, "%.1fs\n", exec_time);
    
    // Imprime o resultado no stdout
    if (solved) {
        print_sudoku(s);
    } else {
        printf("Nenhuma solução\n");
    }
    
    // Libera memória
    free_sudoku(s);
    
    return 0;
}