# Power Quality Waveform Analyser

**UGMFGT-15-1 Programming for Engineers**
University of the West of England — Academic Year 2025–26

A command-line C tool that reads a 3-phase industrial power-quality log file,
computes key waveform metrics (RMS, peak-to-peak, DC offset, clipping,
tolerance compliance), and writes a structured plain-text report.

---

## Repository structure

```
.
├── main.c            Entry point — argument handling and pipeline
├── waveform.c        Analysis functions (RMS, DC offset, clipping, etc.)
├── waveform.h        WaveformSample struct, PhaseResult struct, declarations
├── io.c              CSV loader, results writer, batch-processing
├── io.h              I/O function declarations
├── CMakeLists.txt    CMake build configuration (C99, links -lm)
├── power_quality_log.csv   Input dataset (1 000 rows, 5 000 samples/s)
└── README.md         This file
```

---

## How to compile and run

### Option A — CLion (recommended IDE)

1. Open CLion → **File > Open** → select this directory.
2. CLion detects `CMakeLists.txt` and configures the project automatically.
3. Click **Build > Build Project** (or press `Ctrl+F9` / `Cmd+F9`).
4. Open the **Run/Debug Configurations** dropdown → **Edit Configurations**.
5. Set **Program arguments** to `power_quality_log.csv`.
6. Click **Run**.

The report is written to `results.txt` in the working directory.

### Option B — cmake + make (terminal)

```bash
mkdir build && cd build
cmake ..
make
./power_quality_analyser ../power_quality_log.csv
```

### Option C — single gcc command (no CMake)

```bash
gcc -std=c99 -Wall -Wextra -o power_quality_analyser \
    main.c waveform.c io.c -lm

./power_quality_analyser power_quality_log.csv
```

---

## Usage

```
power_quality_analyser <csv-file>
power_quality_analyser <directory>      # batch mode — processes all *.csv files
```

**Single file:** reads `<csv-file>`, writes `results.txt` in the current directory.

**Batch mode:** when the argument is a directory, every `.csv` file inside is
processed and a corresponding `<basename>_results.txt` is written beside it.

---

## Expected output (Phase A reference values)

| Metric          | Expected value          |
|-----------------|-------------------------|
| RMS voltage     | ~229.7 V (COMPLIANT)    |
| Peak-to-peak    | ~650.0 V                |
| DC offset       | ~0.00 V                 |
| Clipped samples | 20 total across 3 phases|
| Frequency range | 50.000 – 50.048 Hz      |
| Power factor    | 0.950 – 0.962           |
| THD             | 2.00 – 2.18 %           |

---

## GitHub repository

<https://github.com/ZaidenxThiha/PPA-Power-Quality-Waveform-Analyser>

---

## References

- Kernighan, B.W. & Ritchie, D.M. (1988). *The C Programming Language* (2nd ed.). Prentice Hall.
- Chacon, S. & Straub, B. (2014). *Pro Git*. Apress.
- BS EN 50160:2010 — Voltage characteristics of electricity supplied by public electricity networks.
