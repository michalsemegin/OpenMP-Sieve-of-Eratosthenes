# Plan działania w Intel VTune Profiler — projekt liczby pierwsze

## 0. Przygotowanie — kompilacja

Przed otwarciem VTune skompiluj kod z symbolami debugowania i pełną optymalizacją:

```bash
gcc -O3 -fopenmp -g -march=native -o primes_omp primes.c -lm
```

> [!IMPORTANT]
> Flaga `-g` jest **obowiązkowa** — bez niej VTune nie potrafi zmapować wyników na linie kodu i nazwy funkcji.  
> Flaga `-O3` zapewnia, że profilujesz faktycznie zoptymalizowany kod (wymóg zadania 14).

Ustaw liczbę wątków (np. tyle ile rdzeni fizycznych — sprawdź w punkcie 1 poniżej):
```bash
export OMP_NUM_THREADS=8   # dostosuj do swojego CPU
```

---

## 1. Zebranie informacji o procesorze (Punkt 1 sprawozdania)

Przed uruchomieniem VTune zapisz parametry systemu:

```bash
# Model CPU i liczba rdzeni
lscpu | grep -E "Model name|Socket|Core|Thread|cache"

# Hierarchia pamięci podręcznej
lscpu --caches

# System operacyjny
uname -a
cat /etc/os-release | grep PRETTY
```

Na Windows: `wmic cpu get Name,NumberOfCores,NumberOfLogicalProcessors` lub narzędzie **CPU-Z**.

---

## 2. Uruchomienie VTune — pierwsze kroki

### 2.1 Nowy projekt

1. Otwórz **Intel VTune Profiler**
2. Kliknij **New Project…** (lewy górny róg lub `Ctrl+N`)
3. Nadaj nazwę: `primes_experiment`
4. Kliknij **Create Project**

---

## 3. Konfiguracja analizy — Microarchitecture Exploration

> [!IMPORTANT]
> Ten **jeden** typ analizy dostarcza **wszystkich** metryk wymaganych w sprawozdaniu jednocześnie.

### 3.1 Dodaj konfigurację analizy

1. W panelu po lewej kliknij **Configure Analysis** (ikona zębatka lub przycisk `+`)
2. W sekcji **WHAT** (górna część):
   - Application: `./primes_omp`
   - Application parameters: `2 100000000` *(zakres domyślny; zmieniaj wg punktu 5)*
   - Working directory: katalog z binarką
3. W sekcji **HOW** (dolna część):
   - Wybierz zakładkę **Algorithm Analysis**
   - Kliknij **Microarchitecture Exploration**
4. Po prawej zobaczysz opis analizy — upewnij się że widzisz:
   - *"Characterizes the performance of your application in terms of microarchitecture…"*

### 3.2 Parametry kolekcji (opcje zaawansowane)

Kliknij **Advanced** pod wyborem analizy:
- **Sampling interval**: zostaw domyślne `1ms`
- **CPU time**: `Hardware Event-Based Sampling`
- Zaznacz ☑ **Collect stacks** (potrzebne do Bottom-up)

### 3.3 Uruchomienie

Kliknij **▶ Start** (przycisk play, prawy górny róg).

VTune uruchomi `primes_omp`, zbierze dane, po zakończeniu programu automatycznie otworzy wyniki.

---

## 4. Odczyt wyników — co gdzie kliknąć

Po zakończeniu analizy VTune otwiera widok wyników z kilkoma zakładkami:

### 4.1 Zakładka **Summary** — główne metryki globalne

Zawiera:
| Metryka VTune | Nazwa w sprawozdaniu |
|---|---|
| **Elapsed Time** | `Tobl` — czas przetwarzania |
| **Instructions Retired** | liczba instrukcji asemblera |
| **Clockticks** | liczba cykli procesorów |
| **Retiring** (%) | udział wykorzystanych zasobów |
| **Front-End Bound** (%) | ograniczenie wejścia |
| **Back-End Bound** (%) | ograniczenie wyjścia |
| **Memory Bound** (%) | ograniczenie systemu pamięci |
| **Core Bound** (%) | ograniczenie jednostek wykonawczych |

> [!NOTE]
> Wartości na Summary są **zagregowane dla całego procesu** (wszystkie k1–k5 razem).  
> Dla wartości **per-funkcja** przejdź do zakładki Bottom-up (punkt 4.2).

**Co kliknąć na Summary:**
- Rozwiń drzewo **Top-down Microarchitecture Analysis** — zobaczysz hierarchię:
  ```
  Retiring          X%
  Front-End Bound   X%
    ├─ Fetch Latency
    └─ Fetch Bandwidth
  Back-End Bound    X%
    ├─ Memory Bound
    │   ├─ L1 Bound
    │   ├─ L2 Bound
    │   ├─ L3 Bound
    │   ├─ DRAM Bound
    │   └─ Store Bound
    └─ Core Bound
  Bad Speculation   X%
  ```
- Metryki `L1 Bound`, `L2 Bound`, `L3 Bound`, `DRAM Bound` to **opcjonalne metryki szczegółowe** (umożliwiają wyższą ocenę).

---

### 4.2 Zakładka **Bottom-up** — metryki PER FUNKCJA ⭐

To najważniejszy widok do wypełnienia tabeli w sprawozdaniu.

**Kroki:**

1. Kliknij zakładkę **Bottom-up** (górny pasek zakładek)
2. W lewej górnej części widoku znajdź rozwijane menu **Grouping** (domyślnie: `Function / Call Stack`)
3. Wybierz **Function** (bez call stack)
4. Zobaczysz listę funkcji posortowaną po CPU Time

**Znajdź swoje funkcje:**
- `k1_sequential_division`
- `k2_parallel_division`
- `k3_sequential_sieve`
- `k3a_sequential_sieve_local`
- `k4_parallel_sieve_functional`
- `k4a_parallel_sieve_functional_fs`
- `k5_parallel_sieve_domain`

**Kolumny do odczytania** (kliknij prawym na nagłówek kolumny → **Add/Remove columns** jeśli brakuje):
- `CPU Time` — czas CPU (może różnić się od Elapsed dla kodu równoległego)
- `Clockticks` — cykle
- `Instructions Retired` — instrukcje
- `Retiring` — %
- `Front-End Bound` — %
- `Back-End Bound` — %
- `Memory Bound` — %
- `Core Bound` — %

> [!TIP]
> Kliknij nagłówek kolumny aby posortować. Kliknij nazwę funkcji aby przejść do widoku **source** z adnotacjami hot-spot per linia kodu — przydatne do zrzutów ekranu w sprawozdaniu.

---

### 4.3 Zakładka **Platform** → CPU Utilization Histogram

**Effective Physical Core Utilization:**

1. Kliknij zakładkę **Platform** (lub szukaj zakładki **CPU Usage**)
2. Poszukaj sekcji **Effective CPU Utilization Histogram**
   - Oś X: liczba aktywnych rdzeni fizycznych (0 → max)
   - Oś Y: % czasu
3. Idealny wynik: słupek przy `max` rdzeni jest najwyższy → pełne zrównoważenie
4. Nierówne słupki = niezrównoważenie wątków

> [!NOTE]
> Alternatywnie: w zakładce **Bottom-up** → filtruj po funkcji → kolumna **Effective CPU Utilization** lub **CPU Usage by Utilization**.

---

### 4.4 Zrzuty ekranu do sprawozdania — co screenshotować

| # | Co | Gdzie |
|---|---|---|
| 1 | Drzewo Top-down (Retiring, FE, BE, Mem, Core) | Summary → Top-down tree |
| 2 | Lista funkcji z metrykami per k1–k5 | Bottom-up → Grouping: Function |
| 3 | CPU Utilization Histogram dla k4/k5 | Platform → Histogram |
| 4 | Źródło z adnotacją hot-line dla k3 i k5 | Bottom-up → klik na funkcję → Source |

---

## 5. Plan sesji pomiarowych — jakie instancje uruchomić

Zgodnie z zadaniem 3 sprawozdania należy przebadać **3 zakresy** dla `max = 10^8`:

| Sesja VTune | Parametry | Opis |
|---|---|---|
| A | `2 100000000` | `<2, max>` |
| B | `50000000 100000000` | `<max/2, max>` |
| C | `2 50000000` | `<2, max/2>` |

Dla każdej sesji:
1. Zmień **Application parameters** w Configure Analysis
2. Uruchom **Start** → zapisz wyniki
3. VTune tworzy osobne wyniki dla każdego uruchomienia — możesz je porównywać

> [!IMPORTANT]
> Twój kod już wykonuje wielokrotne powtórzenia (`repeats=5`, `repeats_div=3`) w jednym uruchomieniu.  
> To **wystarczy** dla VTune — nie musisz ręcznie wielokrotnie klikać Start.

---

## 6. Jak wypełnić tabelę w sprawozdaniu

Dla każdej funkcji z Bottom-up odczytaj i wpisz:

| Wariant | Zakres | Elapsed [s] | Instructions | Clockticks | Retiring % | FE Bound % | BE Bound % | Mem Bound % | Core Bound % | Prędkość [Mliczb/s] | Przyspieszenie | Efektywność |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| k1 | 2–10^8 | … | … | … | … | … | … | … | … | … | — | — |
| k2 | 2–10^8 | … | … | … | … | … | … | … | … | … | S=Tk1/Tk2 | S/p |
| k3 | 2–10^8 | … | … | … | … | … | … | … | … | … | — | — |
| … | … | … | … | … | … | … | … | … | … | … | … | … |

**Wzory do wyliczenia:**
```
Prędkość [Mliczb/s]  = (n - m + 1) / Elapsed / 1 000 000
Przyspieszenie S     = T_sekwencyjne_najlepsze / T_równoległe
Efektywność E        = S / liczba_rdzeni_fizycznych
```
> Jako `T_sekwencyjne_najlepsze` użyj czasu **k3a** (najszybszy sekwencyjny — z lokalnością).

---

## 7. Dodatkowe metryki pamięci (opcjonalne — wyższa ocena)

W Bottom-up, dla funkcji z wysokim `Memory Bound`:

1. Kliknij **Add/Remove columns** (ikona ⚙ przy nagłówkach)
2. Dodaj kolumny z grupy **Memory Access**:
   - `L1 Bound` — ograniczenie przez L1 cache
   - `L2 Bound` — ograniczenie przez L2 cache
   - `L3 Bound` — ograniczenie przez L3 cache
   - `DRAM Bound` — ograniczenie przez RAM
   - `DTLB Overhead` — ograniczenie przez TLB

Te wartości pozwalają napisać np.:  
*"k3 charakteryzuje się DRAM Bound = 42%, co wynika z braku lokalności dostępu — tablica result[] nie mieści się w żadnym poziomie cache dla n=10^8."*

---

## 8. Eksport wyników

1. **File → Export → CSV** — dane tabelaryczne do Excela
2. **File → Export → Report** — PDF/HTML ze wszystkimi widokami
3. Ręczne screenshoty: `Ctrl+Print Screen` lub narzędzie systemowe

---

## Szybka ściąga — kolejność działań

```
[1] Kompiluj: gcc -O3 -fopenmp -g -o primes_omp primes.c -lm
[2] VTune → New Project
[3] Configure Analysis → Microarchitecture Exploration
[4] Parametry: "./primes_omp 2 100000000"
[5] ▶ Start
[6] Summary → odczytaj Elapsed, Instructions, Clockticks, Top-down %
[7] Bottom-up → Grouping: Function → odczytaj metryki per k1-k5
[8] Platform → CPU Utilization Histogram → screenshot
[9] Powtórz [3-8] dla zakresów: "50000000 100000000" i "2 50000000"
[10] Wypełnij tabelę sprawozdania
```
