#include "mlkem_bench.h"

/* The generated APIs take fixed-size array parameters; the table stores them
 * through pointer-taking shims so all three levels share one call signature. */
#define SHIM(L, lvl)                                                            \
static int L##_kpd(uint8_t *pk, uint8_t *sk, const uint8_t *c)                  \
{ return mlkem##lvl##_keypair_derand(pk, sk, c); }                              \
static int L##_kp(uint8_t *pk, uint8_t *sk)                                     \
{ return mlkem##lvl##_keypair(pk, sk); }                                        \
static int L##_encd(uint8_t *ct, uint8_t *ss, const uint8_t *pk, const uint8_t *c) \
{ return mlkem##lvl##_enc_derand(ct, ss, pk, c); }                              \
static int L##_enc(uint8_t *ct, uint8_t *ss, const uint8_t *pk)                 \
{ return mlkem##lvl##_enc(ct, ss, pk); }                                        \
static int L##_dec(uint8_t *ss, const uint8_t *ct, const uint8_t *sk)           \
{ return mlkem##lvl##_dec(ss, ct, sk); }

SHIM(m512,  512)
SHIM(m768,  768)
SHIM(m1024, 1024)

const mlk_variant_t mlk_variants[3] = {
    { "ML-KEM-512",  512,  MLKEM512_PUBLICKEYBYTES,  MLKEM512_SECRETKEYBYTES,
      MLKEM512_CIPHERTEXTBYTES,  m512_kpd,  m512_kp,  m512_encd,  m512_enc,  m512_dec  },
    { "ML-KEM-768",  768,  MLKEM768_PUBLICKEYBYTES,  MLKEM768_SECRETKEYBYTES,
      MLKEM768_CIPHERTEXTBYTES,  m768_kpd,  m768_kp,  m768_encd,  m768_enc,  m768_dec  },
    { "ML-KEM-1024", 1024, MLKEM1024_PUBLICKEYBYTES, MLKEM1024_SECRETKEYBYTES,
      MLKEM1024_CIPHERTEXTBYTES, m1024_kpd, m1024_kp, m1024_encd, m1024_enc, m1024_dec },
};
