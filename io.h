#ifndef IO_H
#define IO_H

#include "waveform.h"

/* ── CSV loader ─────────────────────────────────────────────────────────── */

/*
 * load_csv — open filename, skip the header row, parse every data row into a
 *   dynamically-allocated WaveformSample array, and return it.
 *
 *   filename  : path to power_quality_log.csv (or any compatible CSV)
 *   count_out : receives the number of rows successfully parsed
 *   Returns   : heap-allocated array — caller must free() it.
 *               Returns NULL on file-not-found or allocation failure;
 *               *count_out is set to 0 in that case.
 */
WaveformSample *load_csv(const char *filename, int *count_out);

/* ── Results writer ─────────────────────────────────────────────────────── */

/*
 * write_results — produce a plain-text report at out_path covering every
 *   computed metric plus per-sample clipping locations.
 *
 *   out_path : destination file (typically "results.txt")
 *   samples  : the original sample array (used to print clipping timestamps)
 *   n        : number of samples
 *   pa/pb/pc : PhaseResult structs for Phase A, B, C
 *   Returns  : 0 on success, -1 on I/O error.
 */
int write_results(const char  *out_path,
                  const WaveformSample *samples, int n,
                  const PhaseResult    *pa,
                  const PhaseResult    *pb,
                  const PhaseResult    *pc);

/* ── Batch processing (Merit / Distinction extension) ───────────────────── */

/*
 * process_directory — scan dirpath for every *.csv file, run the full
 *   analysis pipeline on each one, and write a matching *_results.txt file
 *   beside it.
 *
 *   Returns : number of files successfully processed, -1 on opendir failure.
 */
int process_directory(const char *dirpath);

#endif /* IO_H */
