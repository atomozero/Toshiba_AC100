#!/bin/bash
# Test sul PC del driver NVEC: simula l'EC che scrive e legge byte per byte.
# Uso: tests/nvec/run.sh
set -e
cd "$(dirname "$0")"
g++ -std=c++17 -g -Istub -Wall -Wno-unused-function -Wno-format \
	-fsanitize=address,undefined -o nvec-test test.cpp
# ASan crashes at random with address space randomization here
setarch -R ./nvec-test
