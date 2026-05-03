#include "waveform.h"

#include <math.h>
#include <stddef.h>

/* ── extract_phase ───────────────────────────────────────────────────────── */
void extract_phase(const WaveformSample *samples, int n, int phase, double *out)
{
    const WaveformSample *p = samples;          /* pointer walks the array   */
    double *dst = out;

    for (int i = 0; i < n; i++, p++, dst++) {
        switch (phase) {
            case 0:  *dst = p->phase_A_voltage; break;
            case 1:  *dst = p->phase_B_voltage; break;
            default: *dst = p->phase_C_voltage; break;
        }
    }
}

/* ── compute_rms ─────────────────────────────────────────────────────────── */
/*  V_RMS = sqrt( (1/N) * Σ v[i]² )                                          */
double compute_rms(const double *voltages, int n)
{
    if (n <= 0) return 0.0;

    double sum_sq = 0.0;
    const double *p = voltages;

    for (int i = 0; i < n; i++, p++)
        sum_sq += (*p) * (*p);

    return sqrt(sum_sq / n);
}

/* ── compute_peak_to_peak ────────────────────────────────────────────────── */
/*  V_pp = V_max − V_min                                                       */
double compute_peak_to_peak(const double *voltages, int n)
{
    if (n <= 0) return 0.0;

    const double *p = voltages;
    double vmax = *p;
    double vmin = *p;
    p++;

    for (int i = 1; i < n; i++, p++) {
        if (*p > vmax) vmax = *p;
        if (*p < vmin) vmin = *p;
    }

    return vmax - vmin;
}

/* ── compute_dc_offset ───────────────────────────────────────────────────── */
/*  V_DC = (1/N) * Σ v[i]  — should be ~0 V for a clean AC signal            */
double compute_dc_offset(const double *voltages, int n)
{
    if (n <= 0) return 0.0;

    double sum = 0.0;
    const double *p = voltages;

    for (int i = 0; i < n; i++, p++)
        sum += *p;

    return sum / n;
}

/* ── count_clipped ───────────────────────────────────────────────────────── */
/*  Count samples where |v| >= limit (sensor saturation threshold)            */
int count_clipped(const double *voltages, int n, double limit)
{
    int count = 0;
    const double *p = voltages;

    for (int i = 0; i < n; i++, p++) {
        double abs_v = (*p < 0.0) ? -(*p) : *p;   /* avoid fabs() dependency */
        if (abs_v >= limit)
            count++;
    }

    return count;
}

/* ── check_compliance ────────────────────────────────────────────────────── */
/*  EN 50160: 207 V <= V_RMS <= 253 V (±10 % of 230 V nominal)               */
int check_compliance(double rms, double nominal)
{
    double low  = nominal * 0.90;
    double high = nominal * 1.10;
    return (rms >= low && rms <= high) ? 1 : 0;
}

/* ── compute_std_dev ─────────────────────────────────────────────────────── */
/*  Two-pass algorithm:                                                         */
/*    Pass 1: compute mean                                                      */
/*    Pass 2: accumulate Σ (v[i] − mean)²                                     */
/*  σ = sqrt( (1/N) * Σ (v[i] − mean)² )                                     */
double compute_std_dev(const double *voltages, int n, double *variance_out)
{
    if (n <= 0) {
        if (variance_out) *variance_out = 0.0;
        return 0.0;
    }

    /* Pass 1 — mean */
    double mean = compute_dc_offset(voltages, n);

    /* Pass 2 — sum of squared deviations */
    double sum_sq_dev = 0.0;
    const double *p = voltages;

    for (int i = 0; i < n; i++, p++) {
        double dev = *p - mean;
        sum_sq_dev += dev * dev;
    }

    double variance = sum_sq_dev / n;
    if (variance_out) *variance_out = variance;
    return sqrt(variance);
}

/* ── sort_by_magnitude ───────────────────────────────────────────────────── */
/*  Insertion sort on absolute values — O(n²) but clear and verifiable.       */
/*  Chosen because the dataset is small (1 000 samples) and insertion sort    */
/*  is stable and easy to trace in a viva.                                    */
void sort_by_magnitude(double *voltages, int n)
{
    for (int i = 1; i < n; i++) {
        double key     = *(voltages + i);
        double abs_key = (key < 0.0) ? -key : key;
        int    j       = i - 1;

        while (j >= 0) {
            double abs_j = (*(voltages + j) < 0.0)
                           ? -(*(voltages + j))
                           :  *(voltages + j);
            if (abs_j <= abs_key) break;
            *(voltages + j + 1) = *(voltages + j);
            j--;
        }
        *(voltages + j + 1) = key;
    }
}

/* ── compute_status_flags ────────────────────────────────────────────────── */
uint8_t compute_status_flags(const PhaseResult *r)
{
    uint8_t flags = 0u;

    if (r->clipped_count > 0)
        flags |= FLAG_CLIPPING;

    if (!r->compliant)
        flags |= FLAG_OUT_OF_BAND;

    double abs_dc = (r->dc_offset < 0.0) ? -(r->dc_offset) : r->dc_offset;
    if (abs_dc > DC_ALERT_THRESHOLD)
        flags |= FLAG_HIGH_DC_OFFSET;

    return flags;
}

/* ── analyse_phase ───────────────────────────────────────────────────────── */
/*  Convenience wrapper: run every metric on a flat voltage array and fill    */
/*  a PhaseResult in one call.                                                */
void analyse_phase(const double *voltages, int n, PhaseResult *result)
{
    result->rms           = compute_rms(voltages, n);
    result->peak_to_peak  = compute_peak_to_peak(voltages, n);
    result->dc_offset     = compute_dc_offset(voltages, n);
    result->std_dev       = compute_std_dev(voltages, n, &result->variance);
    result->clipped_count = count_clipped(voltages, n, CLIP_THRESHOLD);
    result->compliant     = check_compliance(result->rms, NOMINAL_VOLTAGE);
    result->status        = compute_status_flags(result);
}
