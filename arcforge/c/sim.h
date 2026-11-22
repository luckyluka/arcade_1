/* sim.h -- M4.1c: difflib similarity, byte-compatible with Python 3.12.
 *
 * The oracle calls two shapes of difflib (docs/AI_INTERFACE.md pins
 * them):
 *
 *   close(name, options)   difflib.get_close_matches(str(name),
 *                          list(options), n=3, cutoff=0.6) -- seq1 is
 *                          the CANDIDATE, seq2 the QUERY;
 *   the E_UNKNOWN_RECIPE fix threshold
 *                          SequenceMatcher(None, name, suggestion)
 *                          .ratio() > 0.85 -- the OPPOSITE argument
 *                          order (query as seq1), because ratio() is
 *                          asymmetric in CPython's implementation.
 *
 * Both come out of one routine: af_ratio(seq1, seq2) reproduces
 * SequenceMatcher(None, seq1, seq2).ratio() -- find_longest_match with
 * CPython's exact tie-breaks (earliest in a, then earliest in b), the
 * recursive matching-block queue, and the autojunk purge on b (b >= 200
 * bytes) so long inputs behave the same way.
 *
 * No external libraries (M4 contract). Operands are bytes: the
 * identifier vocabulary is 7-bit ASCII, where bytes and code points
 * coincide (a non-ASCII name is the one documented divergence).
 */
#ifndef ARCFORGE_SIM_H
#define ARCFORGE_SIM_H

#include <stddef.h>

/* SequenceMatcher(None, seq1, seq2).ratio() -- 1.0 for two empty
 * sequences, 2.0*matches/(len(seq1)+len(seq2)) otherwise. */
double af_ratio(const char *seq1, const char *seq2);

/* difflib.get_close_matches(word, possibilities, 3, 0.6): candidates
 * with ratio(candidate, word) >= 0.6, best first -- ratio descending,
 * ties by the LARGER string first (heapq.nlargest over (ratio, string)
 * tuples), at most outcap entries. Writes indices into `possibilities`
 * and returns the count. The quick_ratio prefilters are upper bounds on
 * ratio, so skipping them cannot drop a candidate ratio would keep. */
size_t af_close_matches(const char *word, const char *const *poss,
                        size_t nposs, size_t *out, size_t outcap);

#endif /* ARCFORGE_SIM_H */
