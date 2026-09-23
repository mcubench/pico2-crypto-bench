/* Uniform view over the three ML-DSA parameter sets, matching mlkem_bench.h.
 * The *_internal entry points take explicit seed and rnd, so key generation and
 * signing are deterministic and every output can be compared byte for byte. */
#ifndef MLDSA_BENCH_H
#define MLDSA_BENCH_H

#include <stdint.h>
#include <stddef.h>
#include "mldsa_native_all.h"

#define MLD_MAX_PK   MLDSA87_PUBLICKEYBYTES
#define MLD_MAX_SK   MLDSA87_SECRETKEYBYTES
#define MLD_MAX_SIG  MLDSA87_BYTES

typedef struct {
    const char *name;
    int    level;
    size_t pk_bytes, sk_bytes, sig_bytes;
    /* deterministic key generation from a 32-byte seed */
    int (*keypair_det)(uint8_t *pk, uint8_t *sk, const uint8_t *seed);
    /* deterministic signing: pre is the FIPS 204 domain prefix, rnd 32 bytes */
    int (*sign_det)(uint8_t *sig, const uint8_t *m, size_t mlen,
                    const uint8_t *pre, size_t prelen,
                    const uint8_t *rnd, const uint8_t *sk);
    int (*verify)(const uint8_t *sig, const uint8_t *m, size_t mlen,
                  const uint8_t *ctx, size_t ctxlen, const uint8_t *pk);
} mld_variant_t;

extern const mld_variant_t mld_variants[3];

/* Domain-separation prefix for pure signing with an empty context. */
#define MLD_PRE_EMPTY_CTX   "\x00\x00"
#define MLD_PRE_EMPTY_LEN   2

#endif
