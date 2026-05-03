#include "waveform.h"
#include "io.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── is_directory ────────────────────────────────────────────────────────── */
#ifdef _WIN32
#include <windows.h>
static int is_directory(const char *path)
{
    DWORD attrs = GetFileAttributesA(path);
    return (attrs != INVALID_FILE_ATTRIBUTES &&
            (attrs & FILE_ATTRIBUTE_DIRECTORY));
}
#else
#include <dirent.h>
static int is_directory(const char *path)
{
    DIR *d = opendir(path);
    if (d) { closedir(d); return 1; }
    return 0;
}
#endif

/* ── print_usage ─────────────────────────────────────────────────────────── */
static void print_usage(const char *prog)
{
    fprintf(stderr, "Usage:\n");
    fprintf(stderr, "  %s <csv-file>      Analyse a single CSV file\n", prog);
    fprintf(stderr, "  %s <directory>     Batch-analyse all *.csv files\n", prog);
}

/* ── run_single_file ─────────────────────────────────────────────────────── */
static int run_single_file(const char *csv_path)
{
    /* ── 1. Load data ── */
    int count = 0;
    WaveformSample *samples = load_csv(csv_path, &count);
    if (!samples || count == 0) {
        free(samples);
        return 1;
    }

    printf("Loaded %d samples from '%s'\n", count, csv_path);

    /* ── 2. Allocate per-phase voltage buffers ── */
    double *va = malloc((size_t)count * sizeof(double));
    double *vb = malloc((size_t)count * sizeof(double));
    double *vc = malloc((size_t)count * sizeof(double));

    if (!va || !vb || !vc) {
        fprintf(stderr, "Error: malloc failed for phase buffers\n");
        free(va); free(vb); free(vc);
        free(samples);
        return 1;
    }

    /* ── 3. Extract each phase column using pointer traversal ── */
    extract_phase(samples, count, 0, va);
    extract_phase(samples, count, 1, vb);
    extract_phase(samples, count, 2, vc);

    /* ── 4. Run full analysis on each phase ── */
    PhaseResult pa, pb, pc;
    analyse_phase(va, count, &pa);
    analyse_phase(vb, count, &pb);
    analyse_phase(vc, count, &pc);

    /* ── 5. Print summary to stdout ── */
    printf("\n");
    printf("=============================================================\n");
    printf("  POWER QUALITY ANALYSIS SUMMARY\n");
    printf("=============================================================\n");
    printf("  %-10s  %8s  %10s  %8s  %7s  %s\n",
           "Phase", "RMS (V)", "Pk-Pk (V)", "DC (V)", "Clips", "Status");
    printf("  %-10s  %8s  %10s  %8s  %7s  %s\n",
           "-----", "-------", "---------", "------", "-----", "------");

    const PhaseResult *phases[3] = {&pa, &pb, &pc};
    const char        *labels[3] = {"A", "B", "C"};

    for (int i = 0; i < 3; i++) {
        const PhaseResult *r = phases[i];
        printf("  %-10s  %8.2f  %10.2f  %8.4f  %7d  %s\n",
               labels[i],
               r->rms,
               r->peak_to_peak,
               r->dc_offset,
               r->clipped_count,
               r->compliant ? "COMPLIANT" : "NON-COMPLIANT");
    }

    int total_clips = pa.clipped_count + pb.clipped_count + pc.clipped_count;
    printf("\n  Total clipped samples : %d\n", total_clips);
    printf("  Std dev  A/B/C       : %.4f / %.4f / %.4f V\n",
           pa.std_dev, pb.std_dev, pc.std_dev);

    /* Decode status flags for each phase */
    printf("\n  Status flags (hex)   :");
    for (int i = 0; i < 3; i++) {
        printf("  Phase %s = 0x%02X", labels[i], phases[i]->status);
    }
    printf("\n");
    printf("=============================================================\n\n");

    /* ── 6. Write results.txt ── */
    const char *out_path = "results.txt";
    int rc = write_results(out_path, samples, count, &pa, &pb, &pc);
    if (rc == 0)
        printf("Report written to '%s'\n", out_path);

    /* ── 7. Free all heap memory ── */
    free(va);
    free(vb);
    free(vc);
    free(samples);

    return rc == 0 ? 0 : 1;
}

/* ── main ────────────────────────────────────────────────────────────────── */
int main(int argc, char *argv[])
{
    if (argc != 2) {
        print_usage(argv[0]);
        return 1;
    }

    const char *path = argv[1];

    if (is_directory(path)) {
        /* Batch mode */
        printf("Batch mode: scanning directory '%s'\n\n", path);
        int n = process_directory(path);
        if (n < 0) return 1;
        printf("\nBatch complete: %d file(s) processed.\n", n);
        return 0;
    }

    /* Input validation: single-file argument must end in .csv */
    size_t plen = strlen(path);
    if (plen < 4 || strcmp(path + plen - 4, ".csv") != 0) {
        fprintf(stderr, "Error: '%s' is not a .csv file\n", path);
        print_usage(argv[0]);
        return 1;
    }

    /* Single-file mode */
    return run_single_file(path);
}
