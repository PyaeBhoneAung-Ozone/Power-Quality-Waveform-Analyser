#ifndef WAVEFORM_H
#define WAVEFORM_H

#include <stdint.h>

/* ── Nominal constants (EN 50160 / UK grid) ─────────────────────────────── */
#define NOMINAL_VOLTAGE     230.0   /* V RMS                                 */
#define TOLERANCE_LOW       207.0   /* V  (−10 %)                            */
#define TOLERANCE_HIGH      253.0   /* V  (+10 %)                            */
#define CLIP_THRESHOLD      324.9   /* |V| >= this → sensor saturated        */
#define SENSOR_HARD_LIMIT   325.0   /* V  physical ceiling                   */
#define DC_ALERT_THRESHOLD  1.0     /* |DC offset| > 1 V triggers flag       */

/* ── Bitwise health-status flags (stored in PhaseResult.status) ─────────── */
#define FLAG_CLIPPING       ((uint8_t)(1u << 0))  /* bit 0: clipping found   */
#define FLAG_OUT_OF_BAND    ((uint8_t)(1u << 1))  /* bit 1: RMS out of range */
#define FLAG_HIGH_DC_OFFSET ((uint8_t)(1u << 2))  /* bit 2: |DC| > 1 V      */

/* ── WaveformSample ─────────────────────────────────────────────────────── */
/* One row of power_quality_log.csv.  All 8 CSV columns are stored here so  */
/* a single pointer can carry the full measurement context.                  */
typedef struct {
    double timestamp;        /* seconds — starts at 0.0, step 0.0002 s      */
    double phase_A_voltage;  /* V — instantaneous, nominally 325.27 V peak  */
    double phase_B_voltage;  /* V — lagging A by 120°                       */
    double phase_C_voltage;  /* V — leading A by 120°                       */
    double line_current;     /* A — typically ~3.5 A                        */
    double frequency;        /* Hz — nominally 50.0 Hz                      */
    double power_factor;     /* dimensionless 0–1, typically 0.95           */
    double thd_percent;      /* % THD, typically 2.0–2.18 %                 */
} WaveformSample;

/* ── PhaseResult ────────────────────────────────────────────────────────── */
/* Computed metrics for one phase.  Populated by analyse_phase() in         */
/* waveform.c and consumed by the report writer in io.c.                    */
typedef struct {
    double  rms;             /* V RMS                                        */
    double  peak_to_peak;    /* V — max minus min across all samples         */
    double  dc_offset;       /* V — arithmetic mean (should be ~0 for AC)   */
    double  std_dev;         /* V — [Merit] standard deviation               */
    double  variance;        /* V² — [Merit] variance                        */
    int     clipped_count;   /* number of samples where |V| >= CLIP_THRESHOLD*/
    int     compliant;       /* 1 if TOLERANCE_LOW <= rms <= TOLERANCE_HIGH  */
    uint8_t status;          /* packed health flags — see FLAG_* above        */
} PhaseResult;

/* ── Core analysis functions ─────────────────────────────────────────────── */

/*
 * compute_rms — Root-Mean-Square voltage.
 *   voltages : pointer to the first element of a double array
 *   n        : number of samples
 *   Returns  : sqrt( (1/n) * Σ v[i]² )
 */
double compute_rms(const double *voltages, int n);

/*
 * compute_peak_to_peak — span from most negative to most positive sample.
 *   Returns  : max(v) − min(v)
 */
double compute_peak_to_peak(const double *voltages, int n);

/*
 * compute_dc_offset — arithmetic mean of all samples.
 *   Returns  : (1/n) * Σ v[i]
 */
double compute_dc_offset(const double *voltages, int n);

/*
 * count_clipped — count samples whose absolute value exceeds limit.
 *   limit    : detection threshold (use CLIP_THRESHOLD)
 *   Returns  : number of clipped samples
 */
int count_clipped(const double *voltages, int n, double limit);

/*
 * check_compliance — test whether an RMS value is within the ±10 % band.
 *   rms      : computed RMS value
 *   nominal  : nominal voltage (use NOMINAL_VOLTAGE)
 *   Returns  : 1 if compliant, 0 otherwise
 */
int check_compliance(double rms, double nominal);

/* ── Merit / Distinction extensions ────────────────────────────────────── */

/*
 * compute_std_dev — two-pass standard deviation and variance.
 *   variance_out : if non-NULL, receives the variance (σ²) as a by-product
 *   Returns      : σ = sqrt( (1/n) * Σ (v[i] − mean)² )
 */
double compute_std_dev(const double *voltages, int n, double *variance_out);

/*
 * sort_by_magnitude — in-place sort of voltages array by absolute value,
 *   ascending.  Implemented with insertion sort; no qsort() used.
 *   Operates entirely through the pointer — modifies the caller's array.
 */
void sort_by_magnitude(double *voltages, int n);

/*
 * compute_status_flags — derive the uint8_t health bitmask from a fully
 *   populated PhaseResult.  Sets FLAG_CLIPPING, FLAG_OUT_OF_BAND, and
 *   FLAG_HIGH_DC_OFFSET as appropriate.
 */
uint8_t compute_status_flags(const PhaseResult *r);

/* ── Convenience helper ─────────────────────────────────────────────────── */

/*
 * extract_phase — copy one phase's voltage column from a WaveformSample
 *   array into a flat double array, using pointer arithmetic to traverse
 *   the struct array.
 *
 *   phase  : 0 = Phase A, 1 = Phase B, 2 = Phase C
 *   out    : caller-allocated buffer of at least n doubles
 */
void extract_phase(const WaveformSample *samples, int n, int phase, double *out);

/*
 * analyse_phase — run all metrics for one phase and fill a PhaseResult.
 *   voltages : flat array extracted by extract_phase()
 *   n        : number of samples
 *   result   : output struct (must not be NULL)
 */
void analyse_phase(const double *voltages, int n, PhaseResult *result);

#endif /* WAVEFORM_H */
