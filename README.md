# 🧩 Sudoku Solver

Este projeto foi criado como parte da **Componente Prática do Exame** da disciplina de **Computação Paralela e Distribuída** durante o ano lectivo **2025/2026** na turma **EIN6_M3** pelo [**Grupo 04**](./CONTRIBUTORS.md).

---
## 📋 Sobre o Projecto

Este trabalho implementa um programa que resolve quebra-cabeças Sudoku.

| Fase | Versão | Tecnologia | Prazo |
|---|---|---|---|
| ✅ V0 | Serial |  | 23 Mar 2026 |
| ✅ V1 | Paralela | OpenMP | 21 Abr 2026 |
| ✅ V2 | Distribuída | MPI + OpenMP | 15 Junho 2026 |

---

## 🗂️ Estrutura do Projecto

```
CPD-Sudoku/
├── docs/
│   ├── sudoku-report.pdf
│   └── sudoku-subject.pdf
|
├── sudoku/
│   ├── sudoku-serial.c
│   ├── sudoku-omp.c
│   └── sudoku-mpi.c
|
├── tests/
│   ├── 9x9-nosol.txt
│   ├── 9x9.txt
│   ├── 16x16-nosol.txt
│   ├── 16x16-zeros.txt
│   └── 16x16.txt
|
├── Makefile
└── README.md
```

---

## 🔧 Compilação

### Opção 1: Comandos directos
```bash
gcc -fopenmp -O2 sudoku/sudoku-serial.c -o sudoku-serial
gcc -fopenmp -O2 sudoku/sudoku-omp.c -o sudoku-omp
mpicc -fopenmp -O2 sudoku/sudoku-mpi.c -o sudoku-mpi
```

> **Nota:** A flag `-fopenmp` é necessária mesmo na versão serial porque usamos `omp_get_wtime()` para medir o tempo.

### Opção 2: Comando Make
Compilar o projecto (serial + OpenMP + MPI)
```bash
make
```
Recompilar o projecto
```bash
make re
```

Limpar o projecto
```bash
make clean
```
---

## ▶️ Utilização

```bash
./sudoku-serial <ficheiro_entrada>
./sudoku-omp <ficheiro_entrada>
mpirun -np <num_processos> ./sudoku-mpi <ficheiro_entrada>
```

### Exemplos:
```bash
./sudoku-serial tests/9x9.txt
./sudoku-omp tests/9x9.txt
mpirun -np 4 ./sudoku-mpi tests/9x9.txt
```
---

## 📥 Formato de Entrada

O ficheiro deve conter:
```
L
linha1 (n números separados por espaço)
linha2 (n números)
...
linhan (n números)
```

Onde:
- `L` = √n (ex: L=3 para Sudoku 9×9)
- `0` representa células vazias
- Números de 1 a n são pistas iniciais

**Exemplo (4×4):**
```
2
0 0 3 1
3 0 0 0
4 2 0 0
1 3 0 2
```

---

## 📤 Formato de Saída

**Com solução** → `stdout`:
```
2 4 3 1
3 1 2 4
4 2 1 3
1 3 4 2
```

**Sem solução** → `stdout`:
```
Nenhuma solução
```

**Tempo de execução** → `stderr`:
```
0.0s
```

---

## 🧪 Testes Realizados

Testamos com cinco tipos de casos:

### 1. **9x9.txt** - Sudoku 9×9
- Instância de teste usada para validar a versão serial e a versão paralela
- Boa para confirmar leitura, validação e preenchimento do tabuleiro

### 2. **9x9-nosol.txt** - Sudoku 9×9 sem solução
- Detecta correctamente que não há solução
- Imprime "Nenhuma solução"

### 3. **16x16.txt** - Sudoku 16×16
- Instância 16×16 para testar escalabilidade
- Pode demorar significativamente mais que 9×9

### 4. **16x16-nosol.txt** - Sudoku 16×16 sem solução
- Caso impossível em dimensão maior
- Verifica deteção de falhas em 16×16

### 5. **16x16-zeros.txt** - Sudoku 16×16 vazio (stress)
- Sem pistas iniciais (pior caso)
- Útil para stress e medições de desempenho
---
  
### Comandos de Testes
 
Execução directa com os ficheiros presentes no repositório:
```bash
./sudoku-serial tests/9x9.txt
./sudoku-serial tests/9x9-nosol.txt
./sudoku-serial tests/16x16.txt
./sudoku-serial tests/16x16-nosol.txt
./sudoku-serial tests/16x16-zeros.txt
```
```bash
./sudoku-omp tests/9x9.txt
./sudoku-omp tests/9x9-nosol.txt
./sudoku-omp tests/16x16.txt
./sudoku-omp tests/16x16-nosol.txt
./sudoku-omp tests/16x16-zeros.txt
```
```bash
mpirun -np 4 ./sudoku-mpi tests/9x9.txt
mpirun -np 4 ./sudoku-mpi tests/9x9-nosol.txt
mpirun -np 4 ./sudoku-mpi tests/16x16.txt
mpirun -np 4 ./sudoku-mpi tests/16x16-nosol.txt
mpirun -np 4 ./sudoku-mpi tests/16x16-zeros.txt
```

Ou via Makefile:
```bash
make test-mpi
```

### Separar saída e tempo:
```bash
./sudoku-serial tests/9x9.txt > solucao.txt 2> tempo.txt
mpirun -np 4 ./sudoku-mpi tests/9x9.txt > solucao.txt 2> tempo.txt
```

```bash
cat solucao.txt   # mostra a solução (stdout)
cat tempo.txt     # mostra o tempo (stderr)
```
---

## 💡 Algoritmo Utilizado

Usamos **backtracking**, que funciona da seguinte forma:

1. Procuramos uma célula vazia no tabuleiro
2. Tentamos colocar números de 1 até n
3. Para cada número, verificamos se é válido (não quebra regras do Sudoku)
4. Se for válido, avançamos recursivamente para a próxima célula
5. Se chegarmos a um impasse, voltamos atrás e tentamos outro número
6. Se testarmos todos os números e nenhum funcionar, não há solução

Escolhemos este algoritmo porque:
- É relativamente simples de implementar e entender
- Sempre encontra solução se ela existir
- Detecta corretamente quando não há solução
- Facilita a paralelização nas próximas fases

---

## 🛠️ Decisões de Implementação

### Estrutura de Dados
```c
typedef struct {
    int **grid;    // Matriz dinâmica
    int n;         // Tamanho n×n
    int L;         // Tamanho do bloco L×L
} Sudoku;
```

### Cálculo do Bloco
Para determinar qual bloco L×L verificar, usamos:
```c
int start_row = (row / L) * L;
int start_col = (col / L) * L;
```

Esta fórmula dá-nos o canto superior esquerdo do bloco correspondente.

### Alocação Dinâmica
Decidimos usar `malloc` e `calloc` em vez de arrays fixos para:
- Suportar qualquer tamanho de Sudoku (4×4, 9×9, 16×16...)
- Praticar gestão de memória dinâmica
- Ter código mais flexível

### Validação
A função `is_valid()` verifica três condições:
1. Número não repete na linha
2. Número não repete na coluna
3. Número não repete no bloco L×L

---

## 🐛 Dificuldades Encontradas

Durante a implementação tivemos alguns desafios:

**Ponteiros Duplos (int\*\*)**  
Inicialmente tivemos alguma confusão com a alocação de `int**` para a matriz. Resolvemos revendo a matéria de ponteiros e alocação dinâmica das aulas.

**Segmentation Fault**  
Esquecemo-nos de alocar memória para cada linha individualmente. Corrigido usando `calloc` para cada linha e adicionando verificações de erro.

**Validação do Bloco**  
Calcular o bloco correcto foi a parte mais complicada. Testamos várias abordagens até chegarmos à fórmula final. Desenhamos exemplos no papel para visualizar melhor.

**Medição de Tempo**  
No início não tínhamos certeza de como usar `omp_get_wtime()` correctamente. Depois de ler a documentação do OpenMP percebemos o esquema do tempo negativo + tempo positivo.

**Detecção de Casos Impossíveis**  
Garantir que o algoritmo detecta correctamente casos sem solução. Testamos com os ficheiros `9x9-nosol.txt` e `16x16-nosol.txt` e verificamos que funciona como esperado.

---

## 📊 Resultados

### Performance:
- **9x9.txt:** ~0.0s a 0.1s (dependendo da dificuldade)
- **9x9-nosol.txt:** ~0.0s (detecção rápida)
- **16x16.txt:** pode demorar significativamente mais
- **16x16-nosol.txt:** detecção rápida de inconsistência
- **16x16-zeros.txt:** caso de stress, mais pesado

### Correcção:
- ✅ Resolve os Sudokus dos ficheiros de teste correctamente
- ✅ Detecta casos sem solução correctamente
- ✅ Não tem memory leaks (verificado com valgrind)

### Validação:
Todos os resultados foram validados manualmente:
- Não há repetições nas linhas
- Não há repetições nas colunas
- Não há repetições nos blocos L×L
- Todas as células preenchidas estão entre 1 e n

---

## 📚 Referências

- Material das aulas de Computação Paralela e Distribuída
- Enunciado do projecto (Prof. João da Costa)
- "Introduction to Algorithms" (Cormen et al.) - Secção sobre Backtracking
