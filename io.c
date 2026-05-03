#include "io.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* POSIX directory traversal — available on Linux, macOS, and WSL */
#include <dirent.h>

/* ── Internal helpers ────────────────────────────────────────────────────── */

/* Return 1 if str ends with suffix (case-sensitive). */
static int ends_with(const char *str, const char *suffix)
{
    size_t slen = strlen(str);
    size_t xlen = strlen(suffix);
    if (xlen > slen) return 0;
    return strcmp(str + slen - xlen, suffix) == 0;
}

/* Print a flag byte as human-readable text into buf (caller provides space). */
static void flags_to_str(uint8_t flags, char *buf, size_t bufsz)
{
    buf[0] = '\0';
    if (flags == 0) {
        snprintf(buf, bufsz, "OK (no faults)");
        return;
    }
    if (flags & FLAG_CLIPPING)
        strncat(buf, "CLIPPING ", bufsz - strlen(buf) - 1);
    if (flags & FLAG_OUT_OF_BAND)
        strncat(buf, "OUT_OF_BAND ", bufsz - strlen(buf) - 1);
    if (flags & FLAG_HIGH_DC_OFFSET)
        strncat(buf, "HIGH_DC_OFFSET", bufsz - strlen(buf) - 1);
}

/* ── load_csv ────────────────────────────────────────────────────────────── */
WaveformSample *load_csv(const char *filename, int *count_out)
{
    *count_out = 0;

    FILE *fp = fopen(filename, "r");
    if (!fp) {
        fprintf(stderr, "Error: cannot open file '%s'\n", filename);
        return NULL;
    }

    /* ── Pass 1: count data rows so we can allocate exactly ── */
    char line[512];

    /* Skip header */
    if (!fgets(line, sizeof(line), fp)) {
        fprintf(stderr, "Error: '%s' is empty\n", filename);
        fclose(fp);
        return NULL;
    }

    int row_count = 0;
    while (fgets(line, sizeof(line), fp)) {
        /* Skip blank lines */
        if (line[0] == '\n' || line[0] == '\r' || line[0] == '\0') continue;
        row_count++;
    }

    if (row_count == 0) {
        fprintf(stderr, "Error: '%s' contains no data rows\n", filename);
        fclose(fp);
        return NULL;
    }

    /* ── Allocate exactly row_count structs ── */
    WaveformSample *samples = malloc((size_t)row_count * sizeof(WaveformSample));
    if (!samples) {
        fprintf(stderr, "Error: malloc failed for %d rows\n", row_count);
        fclose(fp);
        return NULL;
    }

    /* ── Pass 2: rewind and parse ── */
    rewind(fp);

    /* Skip header again */
    if (!fgets(line, sizeof(line), fp)) {
        free(samples);
        fclose(fp);
        return NULL;
    }

    int parsed = 0;
    WaveformSample *sp = samples;   /* pointer walks the allocated array */

    while (fgets(line, sizeof(line), fp) && parsed < row_count) {
        if (line[0] == '\n' || line[0] == '\r' || line[0] == '\0') continue;

        int fields = sscanf(line,
            "%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf",
            &sp->timestamp,
            &sp->phase_A_voltage,
            &sp->phase_B_voltage,
            &sp->phase_C_voltage,
            &sp->line_current,
            &sp->frequency,
            &sp->power_factor,
            &sp->thd_percent);

        if (fields != 8) {
            fprintf(stderr, "Warning: row %d has %d fields (expected 8) — skipped\n",
                    parsed + 1, fields);
            continue;
        }

        sp++;
        parsed++;
    }

    fclose(fp);

    if (parsed == 0) {
        fprintf(stderr, "Error: no valid rows parsed from '%s'\n", filename);
        free(samples);
        return NULL;
    }

    *count_out = parsed;
    return samples;
}

/* ── Internal: write one phase block to an open file ────────────────────── */
static void write_phase_block(FILE *fp,
                               const char         *label,
                               const PhaseResult  *r,
                               const WaveformSample *samples,
                               int n,
                               int phase)
{
    char flag_str[128];
    flags_to_str(r->status, flag_str, sizeof(flag_str));

    fprintf(fp, "  Phase %s\n", label);
    fprintf(fp, "  %-28s %10.4f V\n",  "RMS voltage:",        r->rms);
    fprintf(fp, "  %-28s %10.4f V\n",  "Peak-to-peak:",       r->peak_to_peak);
    fprintf(fp, "  %-28s %10.6f V\n",  "DC offset:",          r->dc_offset);
    fprintf(fp, "  %-28s %10.4f V\n",  "Std deviation:",      r->std_dev);
    fprintf(fp, "  %-28s %10.4f V^2\n","Variance:",            r->variance);
    fprintf(fp, "  %-28s %10d\n",      "Clipped samples:",    r->clipped_count);
    fprintf(fp, "  %-28s %10s\n",      "EN 50160 compliance:",
                                        r->compliant ? "COMPLIANT" : "NON-COMPLIANT");
    fprintf(fp, "  %-28s   0x%02X  (%s)\n", "Status flags:", r->status, flag_str);

    /* List timestamps of every clipped sample for this phase */
    if (r->clipped_count > 0) {
        fprintf(fp, "  Clipped sample timestamps:\n");
        const WaveformSample *sp = samples;
        int found = 0;
        for (int i = 0; i < n; i++, sp++) {
            double v = 0.0;
            switch (phase) {
                case 0: v = sp->phase_A_voltage; break;
                case 1: v = sp->phase_B_voltage; break;
                case 2: v = sp->phase_C_voltage; break;
            }
            double abs_v = (v < 0.0) ? -v : v;
            if (abs_v >= CLIP_THRESHOLD) {
                fprintf(fp, "    [%4d]  t = %.4f s   v = %+.4f V\n",
                        i, sp->timestamp, v);
                found++;
                if (found >= r->clipped_count) break;
            }
        }
    }

    fprintf(fp, "\n");
}

/* ── write_results ───────────────────────────────────────────────────────── */
int write_results(const char           *out_path,
                  const WaveformSample *samples, int n,
                  const PhaseResult    *pa,
                  const PhaseResult    *pb,
                  const PhaseResult    *pc)
{
    FILE *fp = fopen(out_path, "w");
    if (!fp) {
        fprintf(stderr, "Error: cannot write results to '%s'\n", out_path);
        return -1;
    }

    /* ── Header ── */
    fprintf(fp, "=============================================================\n");
    fprintf(fp, "  POWER QUALITY WAVEFORM ANALYSER — RESULTS REPORT\n");
    fprintf(fp, "=============================================================\n");
    fprintf(fp, "  Samples analysed : %d\n", n);
    if (n > 0) {
        fprintf(fp, "  Time window      : %.4f s — %.4f s\n",
                samples[0].timestamp, samples[n - 1].timestamp);
    }
    fprintf(fp, "  Nominal voltage  : %.1f V RMS\n", NOMINAL_VOLTAGE);
    fprintf(fp, "  Tolerance band   : %.1f V — %.1f V  (EN 50160 ±10%%)\n",
            TOLERANCE_LOW, TOLERANCE_HIGH);
    fprintf(fp, "  Clip threshold   : |V| >= %.1f V\n", CLIP_THRESHOLD);
    fprintf(fp, "\n");

    /* ── Per-phase analysis ── */
    fprintf(fp, "-------------------------------------------------------------\n");
    fprintf(fp, "  VOLTAGE ANALYSIS (per phase)\n");
    fprintf(fp, "-------------------------------------------------------------\n\n");

    write_phase_block(fp, "A", pa, samples, n, 0);
    write_phase_block(fp, "B", pb, samples, n, 1);
    write_phase_block(fp, "C", pc, samples, n, 2);

    /* ── System-wide summary ── */
    int total_clipped = pa->clipped_count + pb->clipped_count + pc->clipped_count;

    fprintf(fp, "-------------------------------------------------------------\n");
    fprintf(fp, "  SYSTEM SUMMARY\n");
    fprintf(fp, "-------------------------------------------------------------\n");
    fprintf(fp, "  Total clipped samples (all phases) : %d\n", total_clipped);
    fprintf(fp, "  Phase A compliant : %s\n", pa->compliant ? "YES" : "NO");
    fprintf(fp, "  Phase B compliant : %s\n", pb->compliant ? "YES" : "NO");
    fprintf(fp, "  Phase C compliant : %s\n", pc->compliant ? "YES" : "NO");
    fprintf(fp, "\n");

    /* ── Frequency, power factor, THD (read directly from dataset) ── */
    fprintf(fp, "-------------------------------------------------------------\n");
    fprintf(fp, "  LINE PARAMETERS (from dataset)\n");
    fprintf(fp, "-------------------------------------------------------------\n");

    /* Compute min/max/mean for frequency, power_factor, thd_percent */
    double freq_min  = samples[0].frequency,  freq_max  = samples[0].frequency,  freq_sum  = 0.0;
    double pf_min    = samples[0].power_factor,pf_max   = samples[0].power_factor,pf_sum    = 0.0;
    double thd_min   = samples[0].thd_percent, thd_max  = samples[0].thd_percent, thd_sum   = 0.0;
    double cur_min   = samples[0].line_current,cur_max  = samples[0].line_current,cur_sum   = 0.0;

    const WaveformSample *sp = samples;
    for (int i = 0; i < n; i++, sp++) {
        if (sp->frequency    < freq_min) freq_min = sp->frequency;
        if (sp->frequency    > freq_max) freq_max = sp->frequency;
        freq_sum += sp->frequency;

        if (sp->power_factor < pf_min)   pf_min   = sp->power_factor;
        if (sp->power_factor > pf_max)   pf_max   = sp->power_factor;
        pf_sum += sp->power_factor;

        if (sp->thd_percent  < thd_min)  thd_min  = sp->thd_percent;
        if (sp->thd_percent  > thd_max)  thd_max  = sp->thd_percent;
        thd_sum += sp->thd_percent;

        if (sp->line_current < cur_min)  cur_min  = sp->line_current;
        if (sp->line_current > cur_max)  cur_max  = sp->line_current;
        cur_sum += sp->line_current;
    }

    fprintf(fp, "  Frequency   : min %.3f Hz  max %.3f Hz  mean %.3f Hz\n",
            freq_min, freq_max, freq_sum / n);
    fprintf(fp, "  Power factor: min %.3f     max %.3f     mean %.3f\n",
            pf_min, pf_max, pf_sum / n);
    fprintf(fp, "  THD         : min %.2f %%   max %.2f %%   mean %.2f %%\n",
            thd_min, thd_max, thd_sum / n);
    fprintf(fp, "  Line current: min %.3f A   max %.3f A   mean %.3f A\n",
            cur_min, cur_max, cur_sum / n);
    fprintf(fp, "\n");

    /* THD assessment */
    const char *thd_assessment = "Clean supply (<5%)";
    if (thd_max >= 8.0)       thd_assessment = "EXCESSIVE (>8%) -- overheating risk";
    else if (thd_max >= 5.0)  thd_assessment = "Elevated (5-8%) -- monitor";
    fprintf(fp, "  THD assessment : %s\n\n", thd_assessment);

    fprintf(fp, "=============================================================\n");
    fprintf(fp, "  END OF REPORT\n");
    fprintf(fp, "=============================================================\n");

    fclose(fp);
    return 0;
}

/* ── process_directory ───────────────────────────────────────────────────── */
int process_directory(const char *dirpath)
{
    DIR *dir = opendir(dirpath);
    if (!dir) {
        fprintf(stderr, "Error: cannot open directory '%s'\n", dirpath);
        return -1;
    }

    int processed = 0;
    struct dirent *entry;

    while ((entry = readdir(dir)) != NULL) {
        if (!ends_with(entry->d_name, ".csv")) continue;

        /* Build full input path */
        char in_path[1024];
        snprintf(in_path, sizeof(in_path), "%s/%s", dirpath, entry->d_name);

        /* Build output path: replace .csv with _results.txt */
        char out_path[1024];
        size_t base_len = strlen(in_path) - 4;   /* strip ".csv" */
        memcpy(out_path, in_path, base_len);
        strcpy(out_path + base_len, "_results.txt");

        printf("Processing: %s\n", in_path);

        int count = 0;
        WaveformSample *samples = load_csv(in_path, &count);
        if (!samples || count == 0) {
            fprintf(stderr, "  Skipped (load failed)\n");
            free(samples);
            continue;
        }

        /* Allocate per-phase voltage buffers */
        double *va = malloc((size_t)count * sizeof(double));
        double *vb = malloc((size_t)count * sizeof(double));
        double *vc = malloc((size_t)count * sizeof(double));

        if (!va || !vb || !vc) {
            fprintf(stderr, "  Skipped (malloc failed)\n");
            free(va); free(vb); free(vc);
            free(samples);
            continue;
        }

        extract_phase(samples, count, 0, va);
        extract_phase(samples, count, 1, vb);
        extract_phase(samples, count, 2, vc);

        PhaseResult pa, pb, pc;
        analyse_phase(va, count, &pa);
        analyse_phase(vb, count, &pb);
        analyse_phase(vc, count, &pc);

        int rc = write_results(out_path, samples, count, &pa, &pb, &pc);
        if (rc == 0) {
            printf("  Report written: %s\n", out_path);
            processed++;
        }

        free(va); free(vb); free(vc);
        free(samples);
    }

    closedir(dir);
    return processed;
}
