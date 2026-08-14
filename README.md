# Compiling C Programs

These examples use GCC. Replace `source.c` with the C file you want to compile and `program` with your preferred executable name.

## Standard C

```sh
gcc source.c -o program
```

## With POSIX Threads

Use `-pthread` for programs that include `<pthread.h>`:

```sh
gcc source.c -o program -pthread
```

## With OpenMP

Use `-fopenmp` for programs that include `<omp.h>` or use OpenMP pragmas:

```sh
gcc source.c -o program -fopenmp
```

## Running the Program

On Linux, macOS, or WSL:

```sh
./program
```

On Windows PowerShell:

```powershell
.\program.exe
```

Add common warnings and select a C language standard when needed, for example:

```sh
gcc -Wall -Wextra -std=c11 source.c -o program
```
