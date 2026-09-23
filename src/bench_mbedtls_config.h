/*
 * Minimal Mbed TLS configuration for the RP2350 crypto benchmark.
 * Selected via PICO_MBEDTLS_CONFIG_FILE in CMakeLists.txt.
 */
#ifndef BENCH_MBEDTLS_CONFIG_H
#define BENCH_MBEDTLS_CONFIG_H

/* Entropy comes from the RP2350 TRNG via pico_rand + pico_mbedtls's
 * mbedtls_hardware_poll(). No filesystem/OS entropy on bare metal. */
#define MBEDTLS_NO_PLATFORM_ENTROPY
#define MBEDTLS_ENTROPY_HARDWARE_ALT
#define MBEDTLS_ENTROPY_C
#define MBEDTLS_CTR_DRBG_C

/* Symmetric primitives and hashes */
#define MBEDTLS_AES_C
#define MBEDTLS_CIPHER_C
#define MBEDTLS_CIPHER_MODE_CBC
#define MBEDTLS_MD_C
#define MBEDTLS_SHA224_C
#define MBEDTLS_SHA256_C

/* Uncomment to route SHA-256 through the RP2350 hardware accelerator
 * instead of the software implementation. Rebuild to compare. */
/* #define MBEDTLS_SHA256_ALT */

/* Bignum / public key */
#define MBEDTLS_BIGNUM_C

/* Hand-written assembly for the bignum inner loops. Mbed TLS 3.6 has an Arm
 * path in bn_mul.h but NO RISC-V path, so with this ON you are measuring "best
 * available library code", not a like-for-like ISA comparison. Comment it out
 * for a pure-C build on both sides. */
#ifndef BENCH_NO_MBEDTLS_ASM
#define MBEDTLS_HAVE_ASM
#endif

/* Fast modular reduction for the NIST curves. Without this, every P-256/P-384
 * field reduction falls back to a generic division and the curve benchmarks
 * measure mostly division, not curve arithmetic. */
#define MBEDTLS_ECP_NIST_OPTIM
#define MBEDTLS_OID_C
#define MBEDTLS_ASN1_PARSE_C
#define MBEDTLS_ASN1_WRITE_C
#define MBEDTLS_ECP_C
#define MBEDTLS_ECDSA_C
#define MBEDTLS_ECDH_C
#define MBEDTLS_RSA_C
#define MBEDTLS_PKCS1_V15
#define MBEDTLS_GENPRIME

#define MBEDTLS_ECP_DP_SECP256R1_ENABLED
#define MBEDTLS_ECP_DP_SECP384R1_ENABLED
#define MBEDTLS_ECP_DP_CURVE25519_ENABLED

/* Readable error codes if something goes wrong */
#define MBEDTLS_ERROR_C

#endif /* BENCH_MBEDTLS_CONFIG_H */
