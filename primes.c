/*
Znajdowanie liczb pierwszych w zakresie <m, n>

Warianty algorytmu:
    k1  – sekwencyjne dzielenie przez liczby pierwsze
    k2  – równoległe dzielenie (OpenMP, schedule dynamiczny)
    k3  – sekwencyjne sito Eratostenesa (bez lokalności danych)
    k3a – sekwencyjne sito blokowe (z lokalnością danych)
    k4  – równoległe sito funkcyjne, schedule(static)
    k4a – równoległe sito funkcyjne, schedule(dynamic) + FS guard
    k5  – równoległe sito domenowe blokowe (z lokalnością danych)

Użycie:
    ./primes_omp [m] [n]
    Domyślnie: m=2, n=100000000

Liczba wątków:
    OMP_NUM_THREADS=8 ./primes_omp 2 100000000
 */

#include <assert.h>
#include <math.h>
#include <omp.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Rozmiar bloku w bajtach dla algorytmu segmentowego
#define BLOCK_SIZE (128 * 2024)

/*
 * Buduje tablicę sita dla liczb z zakresu [0, sqrtN].
 * primeArray[i] == 1 gdy i jest liczbą pierwszą, 0 wpp.
 */
static void build_prime_array(int sqrtN, char *primeArray) {
  memset(primeArray, 1, (sqrtN + 1) * sizeof(char));
  primeArray[0] = primeArray[1] = 0;
  for (int i = 2; i * i <= sqrtN; i++) {
    if (primeArray[i]) {
      for (int j = i * i; j <= sqrtN; j += i) {
        primeArray[j] = 0;
      }
    }
  }
}

// K1 - Sekwencyjne dzielenie przez liczby pierwsze, zakres <m;n>
long long k1_sequential_division(int m, int n) {
  assert(m >= 2 && n >= m && "Invalid range");
  int sqrtN = (int)sqrt((double)n);
  size_t rangeLen = (size_t)(n - m) + 1;
  char *primeArray = (char *)malloc((sqrtN + 1) * sizeof(char));
  char *result = (char *)malloc(rangeLen);
  if (!primeArray || !result) {
    fprintf(stderr, "malloc failed in k1\n");
    free(primeArray);
    free(result);
    return -1;
  }

  memset(result, 1, rangeLen);
  build_prime_array(sqrtN, primeArray);

  // Dla kazdej liczby z zakresu sprawdź dzielniki do sqrt(i)
  for (int i = m; i <= n; i++) {
    for (int j = 2; (long long)j * j <= i; j++) {
      if (primeArray[j] && i % j == 0) {
        result[i - m] = 0;
        break;
      }
    }
  }

  long long count = 0;
  for (int i = 0; i <= n; i++) {
    count += result[i];
  }

  free(primeArray);
  free(result);
  return count;
}

// K2 - Równoległe dzielenie przez liczby pierwsze w zakresie <m; n>
// Zrównoleglenie zewnętrznej pętli po i
// Mozliwe wystąpienie false sharing. Wątki zapisują do sąsiednich
// komórek result[] lezących na tej samej linii cache. Przy duzym CHUNK
// kazdy wątek pracuje na ciągłym obszare, co ogranicza liczbę
// konfliktów do granich chunków.
// Synchronizacja: niejawna bariera na końcu #pragma omp for

long long k2_parallel_division(int m, int n) {
  assert(m >= 2 && n >= m && "invalid range");
  int sqrtN = (int)sqrt((double)n);
  size_t rangeLen = (size_t)(n - m) + 1;
  char *primeArray = (char *)malloc((sqrtN + 1) * sizeof(char));
  char *result = (char *)malloc(rangeLen);
  if (!primeArray || !result) {
    fprintf(stderr, "malloc failed in k2\n");
    free(primeArray);
    free(result);
    return -1;
  }

  memset(result, 1, rangeLen);
  build_prime_array(sqrtN, primeArray);

  // schedule(dynamic, 1024)
  //  chunk = 1024 iteracji pobieranych dynamicznie
  //  mniejszy chunk -> lepsze zrównoleglenie
  //  większy chunk -> mniejszy narzut synchronizacji
#pragma omp parallel for schedule(dynamic, 1024) default(none)                 \
    shared(result, primeArray, m, n)
  for (int i = m; i <= n; i++) {
    for (int j = 2; (long long)j * j <= i; j++) {
      if (primeArray[j] && i % j == 0) {
        result[i - m] = 0;
        break;
      }
    }
  }

  long long count = 0;
#pragma omp parallel for reduction(+ : count) default(none) shared(result, n, m)
  for (int i = 0; i <= n - m; i++)
    count += result[i];

  free(primeArray);
  free(result);
  return count;
}

// K3 - Sekwencyjne sito Erastotenesa bez lokalności danych
// Podejście funkcyjne: dla kazdej liczby pierwszej p iterujemy
// po całym zakresie <m;n> wykreślając wielokrotności p
// Przy duzym n tablica resukt[] nie mieści się w cache
// Kazda iteracja wewnątrz pętli trafia do l3 lub RAM
// liczne cache miss
long long k3_sequential_sieve(int m, int n) {
  assert(m >= 2 && n >= m && "invalid range");
  int sqrtN = (int)sqrt((double)n);
  size_t rangeLen = (size_t)(n - m) + 1;
  char *primeArray = (char *)malloc((sqrtN + 1) * sizeof(char));
  char *result = (char *)malloc(rangeLen);
  if (!primeArray || !result) {
    fprintf(stderr, "malloc failed in k3\n");
    free(primeArray);
    free(result);
    return -1;
  }

  memset(result, 1, rangeLen);
  build_prime_array(sqrtN, primeArray);

  for (int p = 2; p <= sqrtN; p++) {
    if (!primeArray[p])
      continue;

    // Pierwsza wielokrotność p >= m (pomijamy p samo w sobie)
    long long fm = ((long long)m + p - 1) / p * p;
    if (fm == p)
      fm += p;

    // Wykreślanie przez cały zakres <m,n> – brak lokalności
    for (long long j = fm; j <= n; j += p)
      result[j - m] = 0;
  }

  long long count = 0;
  for (int i = 0; i <= n - m; i++)
    count += result[i];

  free(primeArray);
  free(result);
  return count;
}

// K3a Sito sekwencyjne blokowe z lokalnością dostępu do danych
// Róznica względem K3: zmiana kolejności pętli
// for blok {for p {for j w bloku}}
// Zakres <m:n> dzielony jest na bloki rozmiaru BLOCK_SIZE.
// Dla kazdego bloku <low;high> wykreślamy wielokrotności wszystkich
// liczb pierwszych p <= sqrt(high)
long long k3a_sequential_sieve_local(int m, int n, int blockSize) {
  assert(m >= 2 && n >= m && blockSize > 0 && "invalid args");
  int sqrtN = (int)sqrt((double)n);
  size_t rangeLen = (size_t)(n - m) + 1;
  char *primeArray = (char *)malloc((sqrtN + 1) * sizeof(char));
  char *result = (char *)malloc(rangeLen);
  if (!primeArray || !result) {
    fprintf(stderr, "malloc failed in k3a\n");
    free(primeArray);
    free(result);
    return -1;
  }

  memset(result, 1, rangeLen);
  build_prime_array(sqrtN, primeArray);

  // Zewnętrzna pętla: kolejne bloki zakresu <m,n>
  for (int low = m; low <= n; low += blockSize) {
    int high = low + blockSize - 1;
    if (high > n)
      high = n;

    // Wewnętrzna pętla: wszystkie liczby pierwsze dla bloku
    for (int p = 2; (long long)p * p <= high; p++) {
      if (!primeArray[p])
        continue;

      // Pierwsza wielokrotność p w bloku [low, high]
      long long fm = ((long long)low + p - 1) / p * p;
      if (fm == p)
        fm += p; // p jest pierwsze – pomijamy

      // Wykreślanie wyłącznie w bieżącym bloku
      for (long long k = fm; k <= high; k += p)
        result[k - m] = 0;
    }
  }

  long long count = 0;
  for (int i = 0; i <= n - m; i++)
    count += result[i];

  free(primeArray);
  free(result);
  return count;
}

// K4 - Równoległe sito funkcyjne, schedule(static)
// Zrównoleglona pętla funkcyjna po liczbach pierwszych p
// kazdy wątek otrzymuje podzbiór liczb pierwszych i wykreśla
// ich wielokrotności w całym zakresie <m;n>
// Wyścig:
//  Wiele wątków pisze jednocześnie do result[]. Zapis result[j]=0
//  jest idempotentny (0 zapisany wielokrotnie daje 0), więc wynik
//  jest poprawny. Jednak jednoczesne zapisy do tej samej linii
//  cache przez różne wątki powodują false sharing.
// False sharing:
//  Wątek A wykreśla wielokrotności p1, wątek B wielokrotności p2.
//  Jeśli ich indeksy trafiają do tej samej linii cache (64 B),
//  każdy zapis unieważnia tę linię u drugiego wątku -> wielokrotne
//  przeładowania linii między rdzeniami. Efekt tym silniejszy,
//  im mniejsze p (gęstsze wykreślenia -> więcej kolizji).
// Brak lokalności: jak k3 – każdy wątek chodzi po całym result[].
long long k4_parallel_sieve_functional(int m, int n) {
  assert(m >= 2 && n >= m && "invalid range");
  int sqrtN = (int)sqrt((double)n);
  size_t rangeLen = (size_t)(n - m) + 1;
  char *primeArray = (char *)malloc((sqrtN + 1) * sizeof(char));
  char *result = (char *)malloc(rangeLen);
  if (!primeArray || !result) {
    fprintf(stderr, "malloc failed in k4\n");
    free(primeArray);
    free(result);
    return -1;
  }

  memset(result, 1, rangeLen);
  build_prime_array(sqrtN, primeArray);

// schedule(static): równy podział zakresu p.
#pragma omp parallel for schedule(static) default(none)                        \
    shared(result, primeArray, m, n, sqrtN)
  for (int p = 2; p <= sqrtN; p++) {
    if (!primeArray[p])
      continue;

    long long fm = ((long long)m + p - 1) / p * p;
    if (fm == p)
      fm += p;

    for (long long j = fm; j <= n; j += p)
      result[j - m] = 0;
  }

  long long count = 0;
#pragma omp parallel for reduction(+ : count) default(none) shared(result, n, m)
  for (int i = 0; i <= n - m; i++)
    count += result[i];

  free(primeArray);
  free(result);
  return count;
}

// K4a - Równoległe sito funkcyjne, guard przed false sharing
// schedule(dynamic):
//   Wątki pobierają kolejne liczby pierwsze p dynamicznie.
//   Lepsze zrównoważenie niż static – wątek kończący szybko
//   (duże p) od razu pobiera nowe zadanie zamiast czekać.
//   Kosztem jest narzut synchronizacji przy pobieraniu zadań.
// Guard przed false sharing: if (result[j-m]) result[j-m] = 0
//   Przed każdym zapisem sprawdzamy czy element nie jest już
//   wykreślony. Zapis do pamięci cache następuje tylko gdy
//   wartość zmienia się z 1 na 0. Przy gęstych wykreśleniach
//   (małe p, duże zachodzenie zakresów różnych wątków) redukuje
//   liczbę unieważnień linii cache między wątkami.
//   Kosztem jest dodatkowy odczyt przed każdym (potencjalnym)
//   zapisem – opłacalny gdy liczba kolizji FS jest duża.

long long k4a_parallel_sieve_functional_fs(int m, int n) {
  assert(m >= 2 && n >= m && "invalid range");
  int sqrtN = (int)sqrt((double)n);
  size_t rangeLen = (size_t)(n - m) + 1;
  char *primeArray = (char *)malloc((sqrtN + 1) * sizeof(char));
  char *result = (char *)malloc(rangeLen);
  if (!primeArray || !result) {
    fprintf(stderr, "malloc failed in k4a\n");
    free(primeArray);
    free(result);
    return -1;
  }

  memset(result, 1, rangeLen);
  build_prime_array(sqrtN, primeArray);

#pragma omp parallel for schedule(dynamic) default(none)                       \
    shared(result, primeArray, m, n, sqrtN)
  for (int p = 2; p <= sqrtN; p++) {
    if (!primeArray[p])
      continue;

    long long fm = ((long long)m + p - 1) / p * p;
    if (fm == p)
      fm += p;

    // zapis tylko gdy element nie jest jeszcze wykreślony
    for (long long j = fm; j <= n; j += p)
      if (result[j - m])
        result[j - m] = 0;
  }

  long long count = 0;
#pragma omp parallel for reduction(+ : count) default(none) shared(result, n, m)
  for (int i = 0; i <= n - m; i++)
    count += result[i];

  free(primeArray);
  free(result);
  return count;
}

// K5 - Równoległe sito domenowe z lokalnością dostępu do danych
// Kadzdy wątek dostaje fragment tablicy result[] (domena) i całą
// tablicę primeArray[] read only
// Brak wyścigu:
//  Bloki są rozłączne: wątek b zapisuje wyłącznie do
//  result[b*BLOCK_SIZE .. (b+1)*BLOCK_SIZE - 1].
//  Brak współdzielonych zapisów -> wynik zawsze poprawny.
// False sharing:
//  Tylko na granicach bloków (ostatni/pierwszy element sąsiednich
//  bloków mogą leżeć w tej samej linii cache 64 B).
//  Przy BLOCK_SIZE = 128*1024 stanowi to 2 / 131072 < 0.002%
//  elementów – efekt całkowicie pomijalny.
// Lokalność:
//  Każdy wątek pracuje na bloku mieszczącym się w jego L1/L2.
//  primeArray[] (~10 KB dla n=10^8) jest replikowana w cache
//  każdego rdzenia (tylko odczyt -> brak unieważnień).
// Zliczanie: reduction(+:count) – atomowa redukcja OpenMP,
//  każdy wątek sumuje swój blok lokalnie, wyniki scalane na końcu.
long long k5_parallel_sieve_domain(int m, int n, int blockSize) {
  assert(m >= 2 && n >= m && blockSize > 0 && "invalid args");
  int sqrtN = (int)sqrt((double)n);
  size_t rangeLen = (size_t)(n - m) + 1;
  char *primeArray = (char *)malloc((sqrtN + 1) * sizeof(char));
  char *result = (char *)malloc(rangeLen);
  if (!primeArray || !result) {
    fprintf(stderr, "malloc failed in k5\n");
    free(primeArray);
    free(result);
    return -1;
  }

  memset(result, 1, rangeLen);
  build_prime_array(sqrtN, primeArray);

  int numberOfBlocks = (n - m) / blockSize + 1;

#pragma omp parallel for schedule(static) default(none)                        \
    shared(result, primeArray, m, n, blockSize, numberOfBlocks)
  for (int b = 0; b < numberOfBlocks; b++) {
    long long low = (long long)m + (long long)b * blockSize;
    long long high = low + blockSize - 1;
    if (high > n)
      high = n;

    for (int p = 2; (long long)p * p <= high; p++) {
      if (!primeArray[p])
        continue;

      long long fm = ((long long)low + p - 1) / p * p;
      if (fm == p)
        fm += p;

      for (long long k = fm; k <= high; k += p)
        result[k - m] = 0;
    }
  }

  long long count = 0;
#pragma omp parallel for reduction(+ : count) default(none) shared(result, n, m)
  for (int i = 0; i <= n - m; i++)
    count += result[i];

  free(primeArray);
  free(result);
  return count;
}

// Wypisanie wyników
static void print_result(const char *name, long long count, int m, int n,
                         double elapsed) {
  double speed = (double)(n - m + 1) / elapsed / 1e6;
  printf("%-48s  count=%lld  time=%.6f s  speed=%.3f Mliczb/s\n", name, count,
         elapsed, speed);
}

// Main
int main(int argc, char *argv[]) {
  int m = 2;
  int n = 100 * 1000 * 1000;

  if (argc >= 3) {
    m = atoi(argv[1]);
    n = atoi(argv[2]);
  } else if (argc == 2) {
    n = atoi(argv[1]);
  }
  if (m < 2)
    m = 2;
  if (m > n) {
    fprintf(stderr, "Pusty zakres: m=%d > n=%d\n", m, n);
    return 1;
  }

  printf("=== Liczby pierwsze w zakresie <%d, %d> ===\n", m, n);
  printf("OpenMP: %d watkow na %d procesorach\n", omp_get_max_threads(),
         omp_get_num_procs());
  printf("BLOCK_SIZE = %d B (%d KB)\n\n", BLOCK_SIZE, BLOCK_SIZE / 1024);

  double t0, t1;
  long long count;

  /*
   * Wielokrotne wykonanie w jednej sesji – wymagane przez VTune
   * dla uzyskania wiarygodnych metryk sprzętowych (zadanie 14).
   * Czas mierzony jako średnia z 'repeats' powtórzeń.
   */
  const int repeats = 5;     /* dla k3, k3a, k4, k4a, k5  */
  const int repeats_div = 3; /* dla k1, k2 – wolniejsze    */

  /* -- k1 ---------------------------------------------------- */
  t0 = omp_get_wtime();
  for (int r = 0; r < repeats_div; r++)
    count = k1_sequential_division(m, n);
  t1 = omp_get_wtime();
  print_result("[k1]  seq. dzielenie", count, m, n, (t1 - t0) / repeats_div);

  /* -- k2 ---------------------------------------------------- */
  t0 = omp_get_wtime();
  for (int r = 0; r < repeats_div; r++)
    count = k2_parallel_division(m, n);
  t1 = omp_get_wtime();
  print_result("[k2]  par. dzielenie          (dynamic,1024)", count, m, n,
               (t1 - t0) / repeats_div);

  /* -- k3 ---------------------------------------------------- */
  t0 = omp_get_wtime();
  for (int r = 0; r < repeats; r++)
    count = k3_sequential_sieve(m, n);
  t1 = omp_get_wtime();
  print_result("[k3]  seq. sito funk.         (bez lokalnosci)", count, m, n,
               (t1 - t0) / repeats);

  /* -- k3a --------------------------------------------------- */
  t0 = omp_get_wtime();
  for (int r = 0; r < repeats; r++)
    count = k3a_sequential_sieve_local(m, n, BLOCK_SIZE);
  t1 = omp_get_wtime();
  print_result("[k3a] seq. sito blokowe       (lokalnosc)", count, m, n,
               (t1 - t0) / repeats);

  /* -- k4 ---------------------------------------------------- */
  t0 = omp_get_wtime();
  for (int r = 0; r < repeats; r++)
    count = k4_parallel_sieve_functional(m, n);
  t1 = omp_get_wtime();
  print_result("[k4]  par. sito funk.         (static)", count, m, n,
               (t1 - t0) / repeats);

  /* -- k4a --------------------------------------------------- */
  t0 = omp_get_wtime();
  for (int r = 0; r < repeats; r++)
    count = k4a_parallel_sieve_functional_fs(m, n);
  t1 = omp_get_wtime();
  print_result("[k4a] par. sito funk.         (dynamic+FS guard)", count, m, n,
               (t1 - t0) / repeats);

  /* -- k5 ---------------------------------------------------- */
  t0 = omp_get_wtime();
  for (int r = 0; r < repeats; r++)
    count = k5_parallel_sieve_domain(m, n, BLOCK_SIZE);
  t1 = omp_get_wtime();
  print_result("[k5]  par. sito domenowe      (lokalnosc, static)", count, m, n,
               (t1 - t0) / repeats);

  return 0;
}