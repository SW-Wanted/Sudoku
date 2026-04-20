# 🛠️ COMANDOS ÚTEIS - SUDOKU SOLVER

## 📦 COMPILAÇÃO

### Compilar tudo
```bash
make all
```

### Compilar com flags específicas
```bash
# Máxima otimização
gcc -Wall -Wextra -O3 -fopenmp -march=native sudoku/paralela/sudoku-omp.c -o sudoku-omp

# Debug com VTune
gcc -Wall -Wextra -O2 -g -fopenmp sudoku/paralela/sudoku-omp.c -o sudoku-omp

# Sem otimizações (debug)
gcc -Wall -Wextra -O0 -g -fopenmp sudoku/paralela/sudoku-omp.c -o sudoku-omp
```

### Verificar suporte OpenMP
```bash
gcc -fopenmp -dM -E - < /dev/null | grep -i openmp
```

---

## 🎮 EXECUÇÃO

### Controlar número de threads
```bash
# 1 thread (baseline)
export OMP_NUM_THREADS=1
./sudoku-omp tests/9x9.txt

# 4 threads (recomendado)
export OMP_NUM_THREADS=4
./sudoku-omp tests/9x9.txt

# Máximo de threads disponíveis
export OMP_NUM_THREADS=$(nproc)
./sudoku-omp tests/9x9.txt
```

### Separar stdout e stderr
```bash
# Apenas solução
./sudoku-omp tests/9x9.txt 2>/dev/null

# Apenas tempo
./sudoku-omp tests/9x9.txt 1>/dev/null

# Ambos em arquivos separados
./sudoku-omp tests/9x9.txt 1>solution.txt 2>time.txt
```

### Executar múltiplas vezes
```bash
# 10 execuções
for i in {1..10}; do
    echo "Execução $i:"
    ./sudoku-omp tests/9x9.txt 2>&1 | tail -1
done

# Calcular média de tempo
for i in {1..10}; do
    ./sudoku-omp tests/9x9.txt 2>&1 | tail -1 | grep -oP '\d+\.\d+'
done | awk '{sum+=$1} END {print "Média:", sum/NR "s"}'
```

---

## 📊 BENCHMARK

### Comparar serial vs paralelo
```bash
echo "Serial:"
time ./sudoku-serial tests/9x9.txt > /dev/null

echo "Paralelo (4 threads):"
export OMP_NUM_THREADS=4
time ./sudoku-omp tests/9x9.txt > /dev/null
```

### Teste de escalabilidade
```bash
for threads in 1 2 4 8 16; do
    export OMP_NUM_THREADS=$threads
    echo -n "$threads threads: "
    ./sudoku-omp tests/9x9.txt 2>&1 | tail -1
done
```

### Benchmark completo
```bash
chmod +x benchmark.sh
./benchmark.sh
```

---

## 🔬 INTEL VTUNE

### Instalação (Linux)
```bash
# Download do Intel oneAPI
wget https://registrationcenter-download.intel.com/akdlm/IRC_NAS/...

# Instalar
sudo sh ./l_BaseKit_...sh

# Adicionar ao PATH
source /opt/intel/oneapi/setvars.sh
```

### Análises básicas

#### 1. Hotspots (funções mais lentas)
```bash
vtune -collect hotspots -result-dir vtune_hotspots ./sudoku-omp tests/16x16.txt

# Ver relatório
vtune -report summary -result-dir vtune_hotspots
vtune -report hotspots -result-dir vtune_hotspots
```

#### 2. Threading (análise de threads)
```bash
vtune -collect threading -result-dir vtune_threading ./sudoku-omp tests/16x16.txt

# Ver relatório
vtune -report summary -result-dir vtune_threading
vtune -report top-tasks -result-dir vtune_threading
```

#### 3. Memory Access (cache misses)
```bash
vtune -collect memory-access -result-dir vtune_memory ./sudoku-omp tests/16x16.txt

# Ver relatório
vtune -report summary -result-dir vtune_memory
vtune -report top-memory -result-dir vtune_memory
```

### Análises avançadas

#### 4. HPC Performance (overview completo)
```bash
vtune -collect hpc-performance -result-dir vtune_hpc ./sudoku-omp tests/16x16.txt
vtune -report summary -result-dir vtune_hpc
```

#### 5. Microarchitecture (CPU pipeline)
```bash
vtune -collect uarch-exploration -result-dir vtune_uarch ./sudoku-omp tests/16x16.txt
vtune -report summary -result-dir vtune_uarch
```

### Exportar relatórios

#### CSV
```bash
vtune -report hotspots -result-dir vtune_hotspots -format csv -csv-delimiter ";" > hotspots.csv
```

#### HTML
```bash
vtune -report summary -result-dir vtune_hotspots -format html -report-output hotspots.html
```

### GUI (interface gráfica)
```bash
vtune-gui vtune_hotspots
```

---

## 🧪 TESTES

### Verificar correção
```bash
# Comparar serial vs paralelo
./sudoku-serial tests/9x9.txt > serial_out.txt
./sudoku-omp tests/9x9.txt > parallel_out.txt
diff serial_out.txt parallel_out.txt

# Deve retornar vazio (arquivos idênticos)
```

### Teste de stress (race conditions)
```bash
# 100 execuções
for i in {1..100}; do
    ./sudoku-omp tests/9x9.txt > out_$i.txt 2>/dev/null
done

# Verificar se todas são idênticas
md5sum out_*.txt | awk '{print $1}' | sort -u | wc -l
# Deve retornar 1 (todos iguais)

# Limpar
rm out_*.txt
```

### Teste de memória (valgrind)
```bash
# Verificar memory leaks
valgrind --leak-check=full --show-leak-kinds=all ./sudoku-omp tests/9x9.txt

# Verificar race conditions (helgrind)
valgrind --tool=helgrind ./sudoku-omp tests/9x9.txt
```

---

## 📈 ANÁLISE DE PERFORMANCE

### Medir tempo preciso
```bash
# Com time
time ./sudoku-omp tests/9x9.txt > /dev/null

# Com perf
perf stat ./sudoku-omp tests/9x9.txt > /dev/null
```

### Profiling com perf
```bash
# Coletar dados
perf record -g ./sudoku-omp tests/16x16.txt

# Ver relatório
perf report

# Ver flamegraph
perf script | stackcollapse-perf.pl | flamegraph.pl > flamegraph.svg
```

### Monitorar CPU
```bash
# htop em tempo real
htop &
./sudoku-omp tests/16x16.txt

# mpstat (utilização por core)
mpstat -P ALL 1 &
./sudoku-omp tests/16x16.txt
```

---

## 🐛 DEBUG

### GDB com OpenMP
```bash
# Compilar com debug
gcc -g -O0 -fopenmp sudoku/paralela/sudoku-omp.c -o sudoku-omp-debug

# Executar no GDB
gdb ./sudoku-omp-debug

# Dentro do GDB:
(gdb) set environment OMP_NUM_THREADS 4
(gdb) break solve_sudoku_parallel
(gdb) run tests/9x9.txt
(gdb) info threads
(gdb) thread 2
(gdb) backtrace
```

### Verificar variáveis OpenMP
```bash
# Ver configuração
export OMP_DISPLAY_ENV=TRUE
./sudoku-omp tests/9x9.txt

# Desabilitar nested parallelism
export OMP_NESTED=FALSE

# Controlar scheduling
export OMP_SCHEDULE="dynamic,1"
```

---

## 📊 VISUALIZAÇÃO

### Gerar gráficos de speedup (gnuplot)
```bash
# Coletar dados
echo "Threads Tempo" > speedup.dat
for threads in 1 2 4 8; do
    export OMP_NUM_THREADS=$threads
    time=$(./sudoku-omp tests/9x9.txt 2>&1 | tail -1 | grep -oP '\d+\.\d+')
    echo "$threads $time" >> speedup.dat
done

# Plotar
gnuplot << EOF
set terminal png size 800,600
set output 'speedup.png'
set title 'Speedup vs Threads'
set xlabel 'Threads'
set ylabel 'Tempo (s)'
plot 'speedup.dat' with linespoints title 'Sudoku 9x9'
EOF
```

---

## 🔧 OTIMIZAÇÕES ADICIONAIS

### Compilar com diferentes otimizações
```bash
# -O2 (padrão)
gcc -O2 -fopenmp sudoku/paralela/sudoku-omp.c -o sudoku-omp-O2

# -O3 (agressivo)
gcc -O3 -fopenmp sudoku/paralela/sudoku-omp.c -o sudoku-omp-O3

# -Ofast (máximo)
gcc -Ofast -fopenmp sudoku/paralela/sudoku-omp.c -o sudoku-omp-Ofast

# Com vetorização
gcc -O3 -fopenmp -march=native -ftree-vectorize sudoku/paralela/sudoku-omp.c -o sudoku-omp-vec

# Comparar
for opt in O2 O3 Ofast vec; do
    echo "$opt:"
    ./sudoku-omp-$opt tests/9x9.txt 2>&1 | tail -1
done
```

### Profile-guided optimization (PGO)
```bash
# 1. Compilar com instrumentação
gcc -O2 -fopenmp -fprofile-generate sudoku/paralela/sudoku-omp.c -o sudoku-omp-pgo

# 2. Executar para coletar perfil
./sudoku-omp-pgo tests/9x9.txt > /dev/null

# 3. Recompilar com perfil
gcc -O2 -fopenmp -fprofile-use sudoku/paralela/sudoku-omp.c -o sudoku-omp-optimized

# 4. Comparar
echo "Sem PGO:"
./sudoku-omp tests/9x9.txt 2>&1 | tail -1
echo "Com PGO:"
./sudoku-omp-optimized tests/9x9.txt 2>&1 | tail -1
```

---

## 📝 NOTAS

### Variáveis de ambiente OpenMP úteis
```bash
export OMP_NUM_THREADS=4          # Número de threads
export OMP_SCHEDULE="dynamic,1"   # Tipo de scheduling
export OMP_NESTED=FALSE           # Desabilitar nested parallelism
export OMP_DISPLAY_ENV=TRUE       # Mostrar configuração
export OMP_PROC_BIND=close        # Afinidade de threads
export OMP_PLACES=cores           # Placement de threads
```

### Limpar arquivos temporários
```bash
# Limpar compilação
make clean

# Limpar resultados VTune
rm -rf vtune_*

# Limpar arquivos de teste
rm -f *.txt *.dat *.png *.csv *.html

# Limpar tudo
make clean && rm -rf vtune_* *.txt *.dat *.png *.csv *.html
```

---

## 🚀 WORKFLOW RECOMENDADO

### 1. Desenvolvimento
```bash
# Compilar
make all

# Testar correção
make test
make test-omp

# Comparar serial vs paralelo
./sudoku-serial tests/9x9.txt > serial.txt
./sudoku-omp tests/9x9.txt > parallel.txt
diff serial.txt parallel.txt
```

### 2. Benchmark
```bash
# Executar benchmark
./benchmark.sh

# Análise de escalabilidade
for threads in 1 2 4 8; do
    export OMP_NUM_THREADS=$threads
    echo "$threads threads:"
    ./sudoku-omp tests/16x16.txt 2>&1 | tail -1
done
```

### 3. Profiling (VTune)
```bash
# Hotspots
vtune -collect hotspots -result-dir vtune_hotspots ./sudoku-omp tests/16x16.txt
vtune -report summary -result-dir vtune_hotspots

# Threading
vtune -collect threading -result-dir vtune_threading ./sudoku-omp tests/16x16.txt
vtune -report summary -result-dir vtune_threading

# Abrir GUI
vtune-gui vtune_hotspots
```

### 4. Análise e Otimização
```bash
# Identificar hotspots
vtune -report hotspots -result-dir vtune_hotspots

# Verificar utilização de threads
vtune -report top-tasks -result-dir vtune_threading

# Otimizar código baseado nos resultados
# ... editar código ...

# Recompilar e testar
make clean && make all
./benchmark.sh
```

---

**Dica:** Salve este arquivo para referência rápida durante desenvolvimento e análise!
