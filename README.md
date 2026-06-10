# Sieve of Eratosthenes — OpenMP Performance Study

Projekt badający wydajność siedmiu wariantów algorytmu znajdowania liczb pierwszych w zakresie `<m, n>`, ze szczególnym naciskiem na lokalizację danych w pamięci podręcznej i zrównoleglenie przy użyciu **OpenMP**.

---

## Spis treści

1. [Opis projektu](#opis-projektu)
2. [Warianty algorytmu](#warianty-algorytmu)
3. [Struktura repozytorium](#struktura-repozytorium)
4. [Wymagania](#wymagania)
5. [Kompilacja](#kompilacja)
6. [Uruchomienie](#uruchomienie)
7. [Wyniki pomiarów](#wyniki-pomiarów)
8. [Analiza wydajności (Intel VTune)](#analiza-wydajności-intel-vtune)
9. [Szczegóły implementacji](#szczegóły-implementacji)
10. [Uwagi techniczne](#uwagi-techniczne)

---

## Opis projektu

Celem projektu jest empiryczna analiza wpływu:

- **lokalizacji dostępu do danych** (ang. *data locality*) — algorytmy segmentowe vs. funkcyjne,
- **false sharing** — współdzielenie linii cache między wątkami OpenMP,
- **strategii harmonogramowania OpenMP** — `schedule(static)` vs. `schedule(dynamic)`,

na czas wykonania i przepustowość (Mliczb/s) przy wyszukiwaniu liczb pierwszych dla n do 10⁸.

Projekt zawiera dwa główne pliki źródłowe:
- **`primes.c`** — wersja referencyjna z nieco uproszczoną funkcją `build_prime_array`,
- **`primes_omp.c`** — wersja finalna ze zoptymalizowaną `build_prime_array2` i poprawioną arytmetyką pierwszej wielokrotności.

---

## Warianty algorytmu

| Wariant | Nazwa | Typ | Strategia | Uwagi |
|---------|-------|-----|-----------|-------|
| **k1** | `k1_sequential_division` | Sekwencyjny | Próbne dzielenia do √i | Najwolniejszy; O(n·√n) |
| **k2** | `k2_parallel_division` | Równoległy | Próbne dzielenia, `schedule(dynamic,1024)` | Zrównoleglenie po `i`; możliwy false sharing na granicach chunków |
| **k3** | `k3_sequential_sieve` | Sekwencyjny | Sito funkcyjne (bez lokalności) | Dla każdego p iteracja po całym `result[]` → liczne cache-miss w L3/RAM |
| **k3a** | `k3a_sequential_sieve_local` | Sekwencyjny | Sito **blokowe** (z lokalnością) | Zmiana kolejności pętli: `for blok { for p { for j w bloku } }` |
| **k4** | `k4_parallel_sieve_functional` | Równoległy | Sito funkcyjne, `schedule(static)` | Każdy wątek = podzbiór liczb pierwszych; silny false sharing dla małych p |
| **k4a** | `k4a_parallel_sieve_functional_fs` | Równoległy | Sito funkcyjne, `schedule(dynamic)` + FS guard | Guard `if(result[j-m]) result[j-m]=0` redukuje unieważnienia linii cache |
| **k5** | `k5_parallel_sieve_domain` | Równoległy | Sito **domenowe blokowe**, `schedule(static)` | Każdy wątek = własna domena (brak wyścigu); najlepsza lokalność |

### Kluczowe parametry

```c
#define BLOCK_SIZE (128 * 1024)   // 128 KB — mieści się w L2 cache
```

Rozmiar bloku dobrany tak, aby każdy blok mieścił się w pamięci podręcznej L2 procesora. Dla typowych procesorów L2 = 256 KB–1 MB, więc blok 128 KB jest bezpieczną wartością.

---

## Struktura repozytorium

```
projekt/
├── primes.c                      # Wersja referencyjna (build_prime_array v1)
├── primes_omp.c                  # Wersja finalna (build_prime_array2, poprawiona arytmetyka)
├── primes                        # Binarny — wersja seq. (primes.c)
├── primes_omp                    # Binarny — wersja finalna
├── primes_v1, primes_v2          # Wcześniejsze binarne warianty
│
├── res.md                        # Surowe wyniki pomiarów (kilka sesji)
├── final_results_2_100000000.csv     # Eksport VTune: metryki per funkcja, zakres <2, 10^8>
├── final_2_50000000.csv              # Eksport VTune: zakres <2, 5·10^7>
├── final_50000000_100000000.csv      # Eksport VTune: zakres <5·10^7, 10^8>
├── result_*.csv                  # Dodatkowe eksporty pośrednie
│
├── vtune_guide.md                # Instrukcja obsługi Intel VTune Profiler (krok po kroku)
├── error.md                      # Błędy VTune (brak symboli jądra — niekrytyczne)
│
├── sprawozdanie.pdf              # Sprawozdanie z projektu (PDF)
├── sprawozdanie.pages            # Sprawozdanie — wersja edytowalna (Pages)
├── sprawozdanie_rownolegle.*     # Wersja alternatywna sprawozdania
│
├── 2-5/, 2-10/, 5-10/           # Katalogi z dodatkowymi pomiarami dla podzakresów
├── zdjecia/                      # Zrzuty ekranu z VTune
│
├── z1.c, z2.c, z2               # Pliki pomocnicze (zadania 1 i 2)
└── .gitignore                    # Ignorowane pliki (binaria, debuginfo, *.o, *.dSYM)
```

---

## Wymagania

- **Kompilator C**: `gcc` ≥ 9.0 z obsługą OpenMP (flaga `-fopenmp`)
- **Biblioteka matematyczna**: `libm` (flaga `-lm`)
- **System**: Linux (testowano na Ubuntu z kernelem 5.x/6.x) lub macOS z GCC z Homebrew
- **Opcjonalnie**: Intel VTune Profiler do analizy mikroarchitekturalnej

### Weryfikacja środowiska

```bash
gcc --version
echo | gcc -fopenmp -x c - -o /dev/null && echo "OpenMP OK"
nproc                          # liczba logicznych rdzeni
lscpu | grep -E "Model|Core|Thread|cache"
```

---

## Kompilacja

### Wersja finalna (zalecana)

```bash
# Pełna optymalizacja z symbolami debugowania (potrzebne dla VTune)
gcc -O3 -fopenmp -g -march=native -o primes_omp primes_omp.c -lm
```

### Wersja referencyjna

```bash
gcc -O3 -fopenmp -g -march=native -o primes primes.c -lm
```

### Flagi kompilacji

| Flaga | Znaczenie |
|-------|-----------|
| `-O3` | Pełna optymalizacja (autovectorization, inlining, loop unrolling) |
| `-fopenmp` | Włączenie obsługi OpenMP |
| `-g` | Symbole debugowania (wymagane przez VTune do mapowania na linie kodu) |
| `-march=native` | Instrukcje specyficzne dla bieżącego CPU (AVX2, SSE4.2 itp.) |
| `-lm` | Linkowanie biblioteki matematycznej (sqrt) |

---

## Uruchomienie

```bash
# Domyślny zakres <2, 100 000 000>
./primes_omp

# Zakres <m, n> podany jako argumenty
./primes_omp 2 100000000
./primes_omp 50000000 100000000
./primes_omp 2 50000000

# Ustawienie liczby wątków OpenMP
OMP_NUM_THREADS=4 ./primes_omp 2 100000000
OMP_NUM_THREADS=8 ./primes_omp 2 100000000
```

### Przykładowe wyjście

```
=== Liczby pierwsze w zakresie <2, 100000000> ===
OpenMP: 4 watkow na 4 procesorach
BLOCK_SIZE = 131072 B (128 KB)

[k1]  seq. dzielenie                              count=5761455  time=76.745750 s  speed=1.303 Mliczb/s
[k2]  par. dzielenie          (dynamic,1024)      count=5761455  time=32.484996 s  speed=3.078 Mliczb/s
[k3]  seq. sito funk.         (bez lokalnosci)    count=5761455  time=0.914123 s  speed=109.394 Mliczb/s
[k3a] seq. sito blokowe       (lokalnosc)         count=5761455  time=0.234371 s  speed=426.674 Mliczb/s
[k4]  par. sito funk.         (static)            count=5761455  time=0.824415 s  speed=121.298 Mliczb/s
[k4a] par. sito funk.         (dynamic+FS guard)  count=5761455  time=0.523840 s  speed=190.898 Mliczb/s
[k5]  par. sito domenowe      (lokalnosc, static)  count=5761455  time=0.140937 s  speed=709.535 Mliczb/s
```

---

## Wyniki pomiarów

Pomiary przeprowadzone na maszynie **4-rdzeniowej** (4 wątki OpenMP), n = 10⁸.

### Zakres `<2, 100 000 000>`

| Wariant | Czas [s] | Prędkość [Mliczb/s] | Przyspieszenie vs k3a |
|---------|----------|---------------------|----------------------|
| k1 — seq. dzielenie | 76.75 | 1.30 | 0.003× |
| k2 — par. dzielenie | 32.48 | 3.08 | 0.007× |
| k3 — seq. sito (bez lok.) | 0.914 | 109.4 | 0.55× |
| **k3a — seq. sito blokowe** | **0.234** | **426.7** | **1.00×** *(baza)* |
| k4 — par. sito funk. (static) | 0.824 | 121.3 | 0.29× |
| k4a — par. sito + FS guard | 0.524 | 190.9 | 0.45× |
| **k5 — par. sito domenowe** | **0.141** | **709.5** | **1.66×** |

### Zakres `<50 000 000, 100 000 000>`

| Wariant | Czas [s] | Prędkość [Mliczb/s] |
|---------|----------|---------------------|
| k1 | 50.65 | 0.99 |
| k2 | 20.87 | 2.40 |
| k3 | 0.502 | 99.6 |
| k3a | 0.154 | 324.2 |
| k4 | 0.471 | 106.2 |
| k4a | 0.393 | 127.3 |
| **k5** | **0.088** | **567.2** |

### Wnioski z pomiarów

1. **Lokalność danych jest kluczowa**: k3a (sekwencyjne blokowe) jest ~3.9× szybsze niż k3 (sekwencyjne funkcyjne), mimo że oba są jednowątkowe.
2. **False sharing niszczy skalowalność**: k4 (równoległy funkcyjny) jest *wolniejszy* niż k3a (sekwencyjny blokowy) — narzut na unieważnienia linii cache przewyższa zysk z równoległości.
3. **k5 to najlepszy wariant**: brak wyścigu + lokalność + równoległość = 1.66× ponad najlepszy wariant sekwencyjny.
4. **Podejście dzielenia (k1/k2)**: drastycznie wolniejsze niż sito — różnica rzędów wielkości (O(n·√n) vs O(n log log n)).

---

## Analiza wydajności (Intel VTune)

Projekt zawiera pełną instrukcję profilowania w pliku [`vtune_guide.md`](vtune_guide.md).

### Szybki start

```bash
# 1. Kompiluj z symbolami
gcc -O3 -fopenmp -g -march=native -o primes_omp primes_omp.c -lm

# 2. Ustaw wątki
export OMP_NUM_THREADS=4

# 3. Uruchom VTune (Microarchitecture Exploration)
vtune -collect uarch-exploration -result-dir ./vtune_results -- ./primes_omp 2 100000000

# 4. Przeglądaj wyniki
vtune-gui ./vtune_results
```

### Kluczowe metryki VTune z pliku `final_results_2_100000000.csv`

| Funkcja | CPU Time [s] | CPI Rate | Memory Bound | False Sharing |
|---------|-------------|----------|--------------|---------------|
| k2 (wątek) | 611.4 | 1.19 | wysoki | 0 |
| k1 | 426.6 | 0.83 | wysoki | 0 |
| k4a (wątek) | 17.7 | 3.57 | **1.0 (max)** | 0 |
| k4 (wątek) | 10.2 | 3.57 | **1.0 (max)** | **0.94** |
| k3 | 8.1 | 2.57 | **1.0 (max)** | 0.96 |
| k5 (wątek) | 2.6 | **0.91** | niski | 0 |
| k3a | 1.7 | **0.55** | niski | 0 |

> **Interpretacja CPI Rate**: k3a (0.55) i k5 (0.91) mają najniższe CPI — procesor efektywnie wykonuje instrukcje. k4/k4a mają CPI ≈ 3.57 — każda instrukcja czeka ~3.5 cykla na dane z pamięci (false sharing + brak lokalności).

---

## Szczegóły implementacji

### Budowa tablicy liczb pierwszych bazowych

Obie wersje budują tablicę `primeArray[0..sqrtN]` sitem prostym przed właściwym algorytmem.

**`build_prime_array` (primes.c)** — standardowe sito:
```c
for (int i = 2; i*i <= sqrtN; i++)
    if (primeArray[i])
        for (int j = i*i; j <= sqrtN; j += i)
            primeArray[j] = 0;
```

**`build_prime_array2` (primes_omp.c)** — rozszerzone kryterium sprawdzania złożoności:
```c
for (int i = 2; i*i*i*i <= n; i++)
    if (primeArray[i])
        for (int j = i*i; j*j <= n; j += i)
            primeArray[j] = 0;
```

### Obliczanie pierwszej wielokrotności p w bloku

Kluczowa operacja w algorytmach sitowych — wyznaczenie najmniejszej wielokrotności `p` nie mniejszej niż `low`, z pominięciem samego `p`:

```c
long long fm = low / p;
if (fm <= 1) {
    fm = p + p;           // p jest pierwsze — zaczynamy od 2p
} else if (low % p) {
    fm = fm * p + p;      // zaokrąglenie w górę
} else {
    fm = fm * p;          // low jest dokładną wielokrotnością p
}
```

### Harmonogramowanie OpenMP

| Wariant | Dyrektywa | Uzasadnienie |
|---------|-----------|--------------|
| k2 | `schedule(dynamic, 1024)` | Nierówna praca per iteracja (i·√i) — dynamika wyrównuje obciążenie |
| k4 | `schedule(static)` | Równy podział p — prosta demonstracja false sharing |
| k4a | `schedule(dynamic)` | Małe p → dużo pracy; duże p → mało pracy; dynamika wyrównuje |
| k5 | `schedule(static)` | Bloki mają równą pracę → static wystarczy; brak synchronizacji |

### False sharing — mechanizm i mitygacja

Cache line ma 64 bajty = 64 elementy `char`. Gdy dwa wątki wykreślają wielokrotności różnych liczb pierwszych trafiające do tej samej linii cache, każdy zapis unieważnia linię u drugiego wątku.

**k4a — guard przed false sharing:**
```c
for (long long j = fm; j <= n; j += p)
    if (result[j - m])          // odczyt przed zapisem
        result[j - m] = 0;      // zapis tylko gdy zmienia wartość
```
Przy dużym nakładaniu się zakresów (małe `p`) redukuje liczbę unieważnień kosztem dodatkowego odczytu.

**k5 — eliminacja false sharing przez dekompozycję domenową:**
Każdy wątek pracuje na rozłącznym fragmencie `result[]`. Bloki sąsiednie mogą dzielić co najwyżej 2 elementy na granicy linii cache (< 0.002% przy `BLOCK_SIZE = 128 KB`).

---

## Uwagi techniczne

### Poprawność wyników

Wszystkie warianty zwracają identyczną liczbę liczb pierwszych (np. 5 761 455 dla zakresu `<2, 10⁸>`), co potwierdza poprawność implementacji.

### Pętla powtórzeń (dla VTune)

```c
const int repeats     = 10;   // k3, k3a, k4, k4a, k5
const int repeats_div = 5;    // k1, k2 — wolniejsze
```
Wielokrotne wykonanie w jednej sesji zapewnia wiarygodne metryki sprzętowe VTune (próbkowanie wymaga statystycznie wystarczającej liczby zdarzeń).

### Znane problemy VTune

Przy profilowaniu na maszynie wirtualnej lub bez symboli jądra pojawiają się ostrzeżenia (zapisane w [`error.md`](error.md)):
```
Cannot locate file `vmlinux'
Cannot locate debugging information for `/usr/lib/x86_64-linux-gnu/libgomp.so.1.0.0'
```
Są to **niekrytyczne** ostrzeżenia — analiza funkcji użytkownika (k1–k5) działa poprawnie.

### Złożoność obliczeniowa

| Wariant | Złożoność czasowa | Złożoność pamięciowa |
|---------|-------------------|----------------------|
| k1, k2 | O(n · √n) | O(√n + n) |
| k3, k4, k4a | O(n log log n) | O(√n + n) |
| k3a, k5 | O(n log log n) | O(√n + n) |

Sita blokowe (k3a, k5) mają identyczną złożoność asymptotyczną jak k3/k4, ale drastycznie lepszy współczynnik przy dużych n dzięki lokalizacji w cache.

---

## Licencja

Projekt edukacyjny — laboratorium z programowania równoległego (OpenMP).
