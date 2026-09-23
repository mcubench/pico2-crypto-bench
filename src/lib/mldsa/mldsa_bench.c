#include "mldsa_bench.h"

#define SHIM(L, lvl)                                                           \
static int L##_kp(uint8_t *pk, uint8_t *sk, const uint8_t *seed)               \
{ return mldsa##lvl##_keypair_internal(pk, sk, seed); }                        \
static int L##_sg(uint8_t *sig, const uint8_t *m, size_t mlen,                 \
                  const uint8_t *pre, size_t prelen,                           \
                  const uint8_t *rnd, const uint8_t *sk)                       \
{ return mldsa##lvl##_signature_internal(sig, m, mlen, pre, prelen, rnd, sk, 0); } \
static int L##_vf(const uint8_t *sig, const uint8_t *m, size_t mlen,           \
                  const uint8_t *ctx, size_t ctxlen, const uint8_t *pk)        \
{ return mldsa##lvl##_verify(sig, m, mlen, ctx, ctxlen, pk); }

SHIM(d44, 44)
SHIM(d65, 65)
SHIM(d87, 87)

const mld_variant_t mld_variants[3] = {
    { "ML-DSA-44", 44, MLDSA44_PUBLICKEYBYTES, MLDSA44_SECRETKEYBYTES,
      MLDSA44_BYTES, d44_kp, d44_sg, d44_vf },
    { "ML-DSA-65", 65, MLDSA65_PUBLICKEYBYTES, MLDSA65_SECRETKEYBYTES,
      MLDSA65_BYTES, d65_kp, d65_sg, d65_vf },
    { "ML-DSA-87", 87, MLDSA87_PUBLICKEYBYTES, MLDSA87_SECRETKEYBYTES,
      MLDSA87_BYTES, d87_kp, d87_sg, d87_vf },
};
