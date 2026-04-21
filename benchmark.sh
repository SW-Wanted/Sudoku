#!/bin/bash

# Script de benchmark para comparar versões serial e paralela do Sudoku

echo "=========================================="
echo "  SUDOKU SOLVER - PERFORMANCE BENCHMARK"
echo "=========================================="
echo ""

# Cores para output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Verifica se executáveis existem
if [ ! -f "./sudoku-serial" ]; then
    echo -e "${RED}Erro: sudoku-serial não encontrado${NC}"
    echo "Execute: make sudoku-serial"
    exit 1
fi

if [ ! -f "./sudoku-omp" ]; then
    echo -e "${RED}Erro: sudoku-omp não encontrado${NC}"
    echo "Execute: make sudoku-omp"
    exit 1
fi

# Função para extrair tempo de execução
get_time() {
    local output=$1
    echo "$output" | grep -oP '\d+\.\d+(?=s)' | head -1
}

# Função para calcular speedup
calculate_speedup() {
    local serial_time=$1
    local parallel_time=$2
    echo "scale=2; $serial_time / $parallel_time" | bc
}

# Testes
TESTS=(
    "tests/input_4x4.txt:4x4 Básico"
    "tests/input_9x9.txt:9x9 Básico"
    "tests/9x9.txt:9x9 Médio"
    "tests/16x16.txt:16x16 Difícil"
)

echo -e "${BLUE}Compilando com otimizações...${NC}"
make clean > /dev/null 2>&1
make all > /dev/null 2>&1

if [ $? -ne 0 ]; then
    echo -e "${RED}Erro na compilação${NC}"
    exit 1
fi

echo -e "${GREEN}Compilação OK${NC}"
echo ""

# Número de threads para testar
THREAD_COUNTS=(1 2 4 8)

for test_entry in "${TESTS[@]}"; do
    IFS=':' read -r test_file test_name <<< "$test_entry"
    
    if [ ! -f "$test_file" ]; then
        echo -e "${YELLOW}Aviso: $test_file não encontrado, pulando...${NC}"
        continue
    fi
    
    echo "=========================================="
    echo -e "${BLUE}Teste: $test_name${NC}"
    echo "Arquivo: $test_file"
    echo "=========================================="
    
    # Executa versão serial
    echo -n "Serial:     "
    serial_output=$(./sudoku-serial "$test_file" 2>&1)
    serial_time=$(echo "$serial_output" | tail -1)
    echo -e "${GREEN}$serial_time${NC}"
    
    # Extrai apenas o número
    serial_seconds=$(echo "$serial_time" | grep -oP '\d+\.\d+')
    
    # Executa versão paralela com diferentes números de threads
    for threads in "${THREAD_COUNTS[@]}"; do
        export OMP_NUM_THREADS=$threads
        echo -n "Paralelo ($threads threads): "
        
        parallel_output=$(./sudoku-omp "$test_file" 2>&1)
        parallel_time=$(echo "$parallel_output" | tail -1)
        parallel_seconds=$(echo "$parallel_time" | grep -oP '\d+\.\d+')
        
        # Calcula speedup
        if [ -n "$serial_seconds" ] && [ -n "$parallel_seconds" ]; then
            speedup=$(echo "scale=2; $serial_seconds / $parallel_seconds" | bc)
            echo -e "${GREEN}$parallel_time${NC} (speedup: ${YELLOW}${speedup}x${NC})"
        else
            echo -e "${GREEN}$parallel_time${NC}"
        fi
    done
    
    echo ""
done

echo "=========================================="
echo -e "${BLUE}ANÁLISE DE ESCALABILIDADE${NC}"
echo "=========================================="
echo ""

# Teste de escalabilidade com 9x9
test_file="tests/9x9.txt"
if [ -f "$test_file" ]; then
    echo "Teste: 9x9 (escalabilidade)"
    echo "Threads | Tempo    | Speedup | Eficiência"
    echo "--------|----------|---------|------------"
    
    # Serial (baseline)
    serial_output=$(./sudoku-serial "$test_file" 2>&1)
    serial_time=$(echo "$serial_output" | tail -1 | grep -oP '\d+\.\d+')
    
    for threads in 1 2 4 8 16; do
        export OMP_NUM_THREADS=$threads
        parallel_output=$(./sudoku-omp "$test_file" 2>&1)
        parallel_time=$(echo "$parallel_output" | tail -1 | grep -oP '\d+\.\d+')
        
        if [ -n "$serial_time" ] && [ -n "$parallel_time" ]; then
            speedup=$(echo "scale=2; $serial_time / $parallel_time" | bc)
            efficiency=$(echo "scale=2; ($speedup / $threads) * 100" | bc)
            printf "%7d | %7.2fs | %6.2fx | %9.1f%%\n" $threads $parallel_time $speedup $efficiency
        fi
    done
fi

echo ""
echo "=========================================="
echo -e "${GREEN}Benchmark concluído!${NC}"
echo "=========================================="
echo ""
echo "Dicas para análise VTune:"
echo "  1. vtune -collect hotspots -result-dir vtune_hotspots ./sudoku-omp tests/16x16.txt"
echo "  2. vtune -collect threading -result-dir vtune_threading ./sudoku-omp tests/16x16.txt"
echo "  3. vtune -collect memory-access -result-dir vtune_memory ./sudoku-omp tests/16x16.txt"
echo ""
