/* Thin wrapper around the vendored mlkem-native monolithic multilevel build,
 * so the benchmark can treat ML-KEM-512/768/1024 as a uniform table. */
#ifndef MLKEM_BENCH_H
#define MLKEM_BENCH_H

#include <stdint.h>
#include <stddef.h>
#include "mlkem_native_all.h"

#define MLK_MAX_PK   MLKEM1024_PUBLICKEYBYTES
#define MLK_MAX_SK   MLKEM1024_SECRETKEYBYTES
#define MLK_MAX_CT   MLKEM1024_CIPHERTEXTBYTES
#define MLK_SS       MLKEM_BYTES

typedef struct {
    const char *name;
    int   level;
    size_t pk_bytes, sk_bytes, ct_bytes;
    int (*keypair_derand)(uint8_t *pk, uint8_t *sk, const uint8_t *coins);
    int (*keypair)(uint8_t *pk, uint8_t *sk);
    int (*enc_derand)(uint8_t *ct, uint8_t *ss, const uint8_t *pk, const uint8_t *coins);
    int (*enc)(uint8_t *ct, uint8_t *ss, const uint8_t *pk);
    int (*dec)(uint8_t *ss, const uint8_t *ct, const uint8_t *sk);
} mlk_variant_t;

extern const mlk_variant_t mlk_variants[3];

#endif
