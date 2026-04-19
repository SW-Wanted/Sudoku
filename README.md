# Sudoku

Este projeto foi criado como parte da componente prática do exame da disciplina de **Computação Paralela e Distribuída** durante o ano lectivo **2025/2026** na turma **EIN6_M3** pelo [**Grupo 04**](./MEMBERS.md)

---
## 📋 Sobre o Projecto

Este trabalho implementa um programa que resolve quebra-cabeças Sudoku.

| Fase | Versão | Tecnologia | Prazo |
|---|---|---|---|
| ✅ V0 | Serial |  | 23 Mar 2026 |
| 🔲 V1 | Paralela | OpenMP | 21 Abr 2026 |
| 🔲 V2 | Distribuída | MPI | 18 Mai 2026 |

---

## 🗂️ Estrutura do Projecto

```
CPD-MiniProjectos/
├── docs/
│   └── CPD2025_2026-Projecto-v0.pdf
├── sudoku/
│   └── serial/
│       └── sudoku-serial.c
├── tests/
│   ├── input_4x4.txt
│   ├── input_4x4_impossible.txt
│   ├── input_9x9.txt
│   ├── 9x9.txt
│   ├── 9x9-nosol.txt
│   ├── 16x16.txt
│   ├── 16x16-nosol.txt
│   └── 16x16-zeros.txt
├── Makefile
└── README.md
```

---

## 🔧 Compilação

### Opção 1: Comando directo
```bash
gcc -fopenmp -O2 sudoku/serial/sudoku-serial.c -o sudoku-serial
```

> **Nota:** A flag `-fopenmp` é necessária mesmo na versão serial porque usamos `omp_get_wtime()` para medir o tempo.

### Opção 2: Comando Make
Compilar o projecto
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
```

### Exemplos:
```bash
./sudoku-serial tests/input_4x4.txt
```
```bash
./sudoku-serial tests/input_4x4_impossible.txt
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

Testamos com três tipos de casos:

### 1. **input_4x4.txt** - Sudoku Básico
```text
2
0 0 3 1
3 0 0 0
4 2 0 0
1 3 0 2
```
- Resolve instantaneamente (~0.0s)
- Sudoku 4×4 com solução válida

### 2. **input_4x4_impossible.txt** - Caso Impossível

```text
2
1 2 3 4
1 0 0 0
0 0 0 0
0 0 0 0
```
- Detecta correctamente que não há solução
- Imprime "Nenhuma solução"

Este Sudoku é impossível porque:
- Linha 0: `[1, 2, 3, 4]`
- Linha 1: `[1, _, _, _]`
- O número **1 já está na coluna 0**, tornando impossível preencher a posição [1,0]

### 3. **input_9x9.txt** - Sudoku Complexo
```text
3
5 3 0 0 7 0 0 0 0
6 0 0 1 9 5 0 0 0
0 9 8 0 0 0 0 6 0
8 0 0 0 6 0 0 0 3
4 0 0 8 0 3 0 0 1
7 0 0 0 2 0 0 0 6
0 6 0 0 0 0 2 8 0
0 0 0 4 1 9 0 0 5
0 0 0 0 8 0 0 7 9
```
- Demora alguns segundos dependendo da dificuldade
- Sudoku 9×9 com solução válida
---
 
### Comandos de Testes
 
4×4 com solução:
```bash
make test1  
```
4×4 impossível:
```bash
make test2
```
9×9 com solução:
```bash
make test3
```
executa os três testes:
```bash
make test
```

### Separar saída e tempo:
```bash
./sudoku-serial tests/input_4x4.txt > solucao.txt 2> tempo.txt
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
Garantir que o algoritmo detecta correctamente casos sem solução. Testamos com o ficheiro `input_impossible.txt` e verificamos que funciona como esperado.

---

## 📊 Resultados

### Performance:
- **input_4×4.txt:** ~0.0s
- **input_4x4_impossible.txt:** ~0.0s (detecção rápida)
- **input_9×9.txt:** ~0.0s a 0.1s (dependendo da dificuldade)

### Correcção:
- ✅ Resolve Sudokus 4×4 correctamente
- ✅ Resolve Sudokus 9×9 correctamente
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