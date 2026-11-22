/* sim.c -- M4.1c: difflib.SequenceMatcher / get_close_matches port.
 *
 * The oracle is /usr/lib/python3.12/difflib.py. Every step below is a
 * direct translation, because ratio() is asymmetric in the
 * IMPLEMENTATION (378 of 3000 random pairs differ by argument order):
 * find_longest_match breaks ties by earliest-in-a then earliest-in-b,
 * and that depends on which string is seq1. Pinned vectors live in
 * tests/similarity_vectors.json.
 *
 * Boundary: isjunk is None (the oracle never passes a junk predicate),
 * so bjunk is always empty and the two junk-extension loops in
 * find_longest_match never fire. autojunk IS active: b >= 200 bytes
 * purges elements more frequent than len(b)//100+1 out of b2j (they can
 * no longer START a match, only extend one) -- the identifier cap keeps
 * the whole corpus below that threshold, but the rule is ported anyway
 * so long inputs agree too.
 */
#include "sim.h"

#include <stdlib.h>
#include <string.h>

int af_sim_oom = 0;

typedef struct {
    const unsigned char *a, *b;   /* seq1, seq2 (bytes) */
    size_t la, lb;
    int *idx[256];                /* b2j: ascending indices into b   */
    size_t nidx[256];
    int *j1, *j2;                 /* scratch for find_longest_match   */
} sm;

static void sm_free(sm *s) {
    for (int c = 0; c < 256; c++) free(s->idx[c]);
    free(s->j1);
    free(s->j2);
    memset(s, 0, sizeof *s);
}

/* __chain_b(): index b, then purge popular elements when autojunk and
 * len(b) >= 200. Returns 0, or -1 on out-of-memory. */
static int sm_init(sm *s, const char *a, const char *b) {
    memset(s, 0, sizeof *s);
    s->a = (const unsigned char *)a;
    s->b = (const unsigned char *)b;
    s->la = strlen(a);
    s->lb = strlen(b);

    size_t cnt[256];
    memset(cnt, 0, sizeof cnt);
    for (size_t i = 0; i < s->lb; i++) cnt[s->b[i]]++;

    size_t ntest = s->lb / 100 + 1;
    int autojunk = s->lb >= 200;

    for (int c = 0; c < 256; c++) {
        if (!cnt[c] || (autojunk && cnt[c] > ntest)) continue;
        s->idx[c] = (int *)malloc(cnt[c] * sizeof(int));
        if (!s->idx[c]) return -1;
    }
    for (size_t i = 0; i < s->lb; i++) {
        unsigned char c = s->b[i];
        if (s->idx[c]) s->idx[c][s->nidx[c]++] = (int)i;
    }
    size_t scratch = s->lb ? s->lb : 1;
    s->j1 = (int *)calloc(scratch, sizeof(int));
    s->j2 = (int *)calloc(scratch, sizeof(int));
    if (!s->j1 || !s->j2) return -1;
    return 0;
}

/* find_longest_match(alo, ahi, blo, bhi): (i, j, k) with a[i..i+k) ==
 * b[j..j+k), earliest in a then earliest in b among the longest. */
static void flm(const sm *s, size_t alo, size_t ahi, size_t blo, size_t bhi,
                size_t *oi, size_t *oj, size_t *ok) {
    size_t besti = alo, bestj = blo, bestsize = 0;
    int *prev = s->j1, *cur = s->j2;
    memset(prev, 0, s->lb * sizeof(int));
    memset(cur, 0, s->lb * sizeof(int));

    for (size_t i = alo; i < ahi; i++) {
        const int *js = s->idx[s->a[i]];
        size_t nj = js ? s->nidx[s->a[i]] : 0;
        for (size_t t = 0; t < nj; t++) {
            size_t j = (size_t)js[t];
            if (j < blo) continue;
            if (j >= bhi) break;              /* indices ascend */
            size_t k = (j > 0 ? (size_t)prev[j - 1] : 0) + 1;
            cur[j] = (int)k;
            if (k > bestsize) {
                besti = i - k + 1;
                bestj = j - k + 1;
                bestsize = k;
            }
        }
        int *tmp = prev;
        prev = cur;
        cur = tmp;
        memset(cur, 0, s->lb * sizeof(int));
    }

    /* extend by non-junk elements (bjunk is empty: isjunk is None) */
    while (besti > alo && bestj > blo &&
           s->a[besti - 1] == s->b[bestj - 1]) {
        besti--;
        bestj--;
        bestsize++;
    }
    while (besti + bestsize < ahi && bestj + bestsize < bhi &&
           s->a[besti + bestsize] == s->b[bestj + bestsize]) {
        bestsize++;
    }
    /* the two junk-extension loops never fire (bjunk empty) */
    *oi = besti;
    *oj = bestj;
    *ok = bestsize;
}

typedef struct {
    size_t alo, ahi, blo, bhi;
} rng;

typedef struct {
    size_t i, j, k;
} blk;

static int blk_cmp(const void *pa, const void *pb) {
    const blk *x = (const blk *)pa, *y = (const blk *)pb;
    if (x->i != y->i) return x->i < y->i ? -1 : 1;
    if (x->j != y->j) return x->j < y->j ? -1 : 1;
    if (x->k != y->k) return x->k < y->k ? -1 : 1;
    return 0;
}

/* get_matching_blocks(): the recursive queue, iterative, then sorted and
 * collapsed. Returns the total matched length (the dummy block has k=0
 * and contributes nothing). */
static int matching_blocks(const sm *s, size_t *total) {
    size_t qcap = 16, qn = 0;
    size_t bcap = 16, bn = 0;
    blk *bl = (blk *)malloc(bcap * sizeof *bl);
    rng *q = (rng *)malloc(qcap * sizeof *q);
    if (!bl || !q) { free(bl); free(q); return -1; }

    q[qn++] = (rng){0, s->la, 0, s->lb};
    while (qn) {
        rng r = q[--qn];
        size_t i, j, k;
        flm(s, r.alo, r.ahi, r.blo, r.bhi, &i, &j, &k);
        if (!k) continue;
        if (bn == bcap) {
            bcap *= 2;
            blk *nb = (blk *)realloc(bl, bcap * sizeof *bl);
            if (!nb) goto oom;
            bl = nb;
        }
        bl[bn++] = (blk){i, j, k};
        if (r.alo < i && r.blo < j) {
            if (qn == qcap) {
                qcap *= 2;
                rng *nq = (rng *)realloc(q, qcap * sizeof *nq);
                if (!nq) goto oom;
                q = nq;
            }
            q[qn++] = (rng){r.alo, i, r.blo, j};
        }
        if (i + k < r.ahi && j + k < r.bhi) {
            if (qn == qcap) {
                qcap *= 2;
                rng *nq = (rng *)realloc(q, qcap * sizeof *nq);
                if (!nq) goto oom;
                q = nq;
            }
            q[qn++] = (rng){i + k, r.ahi, j + k, r.bhi};
        }
    }
    free(q);
    q = NULL;

    qsort(bl, bn, sizeof *bl, blk_cmp);

    /* collapse adjacent blocks (CPython's non_adjacent walk, with the
     * (la, lb, 0) dummy folded in as the starting i1=j1=k1=0 state) */
    size_t i1 = 0, j1 = 0, k1 = 0, out = 0, sum = 0;
    for (size_t t = 0; t < bn; t++) {
        if (i1 + k1 == bl[t].i && j1 + k1 == bl[t].j) {
            k1 += bl[t].k;
        } else {
            if (k1) { out++; sum += k1; }
            i1 = bl[t].i;
            j1 = bl[t].j;
            k1 = bl[t].k;
        }
    }
    if (k1) { out++; sum += k1; }
    (void)out;
    free(bl);
    *total = sum;
    return 0;

oom:
    free(bl);
    free(q);
    return -1;
}

double af_ratio(const char *seq1, const char *seq2) {
    sm s;
    if (sm_init(&s, seq1, seq2)) {
        af_sim_oom = 1;
        sm_free(&s);
        return 0.0;
    }
    size_t matches = 0;
    if (matching_blocks(&s, &matches)) {
        af_sim_oom = 1;
        sm_free(&s);
        return 0.0;
    }
    size_t length = s.la + s.lb;
    sm_free(&s);
    if (!length) return 1.0;
    return 2.0 * (double)matches / (double)length;
}

/* heapq.nlargest(3, [(ratio, x)]) -- ratio descending, ties by the
 * LARGER string first, stable for full ties. Insertion sort: options are
 * a handful of registry names, and stability needs no extra work. */
size_t af_close_matches(const char *word, const char *const *poss,
                        size_t nposs, size_t *out, size_t outcap) {
    if (!nposs || !outcap) return 0;
    double *rat = (double *)malloc(nposs * sizeof(double));
    size_t *ord = (size_t *)malloc(nposs * sizeof(size_t));
    if (!rat || !ord) {
        free(rat);
        free(ord);
        af_sim_oom = 1;
        return 0;
    }
    size_t nc = 0;
    for (size_t i = 0; i < nposs; i++) {
        double r = af_ratio(poss[i], word);   /* seq1 = candidate */
        if (r >= 0.6) {
            rat[nc] = r;
            ord[nc] = i;
            nc++;
        }
    }
    for (size_t i = 1; i < nc; i++) {
        double r = rat[i];
        size_t o = ord[i];
        size_t j = i;
        while (j > 0) {
            double pr = rat[j - 1];
            size_t po = ord[j - 1];
            int before;
            if (pr != r)
                before = pr > r;
            else {
                int c = strcmp(poss[po], poss[o]);
                before = c > 0;      /* larger string first */
                if (c == 0) before = 1;  /* stable: keep input order */
            }
            if (before) break;
            rat[j] = pr;
            ord[j] = po;
            j--;
        }
        rat[j] = r;
        ord[j] = o;
    }
    size_t n = nc < outcap ? nc : outcap;
    for (size_t i = 0; i < n; i++) out[i] = ord[i];
    free(rat);
    free(ord);
    return n;
}
