/*
 * RP2350 cryptographic library shootout
 *
 * Runs the same primitives through several implementations in one pass so the
 * numbers are directly comparable: same clock, same build, same binary.
 *
 *   Mbed TLS 3.6.6   generic portable C, plus an Arm asm bignum path
 *   p256-m           minimal constant-time P-256 (bundled with Mbed TLS)
 *   micro-ecc        embedded ECC, hand-written Cortex-M assembly (UMAAL)
 *   Monocypher       X25519 / Ed25519 / BLAKE2b
 *   RP2350 hardware  the on-chip SHA-256 accelerator
 *
 * Builds unchanged for Arm Cortex-M33 and RISC-V Hazard3.
 */

#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"
#include "pico/rand.h"
#include "pico/sha256.h"
#include "hardware/clocks.h"

#include "mbedtls/version.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/sha256.h"
#include "mbedtls/ecp.h"
#include "mbedtls/ecdsa.h"
#include "mbedtls/rsa.h"

#include "p256-m.h"
#include "uECC.h"
#include "monocypher.h"
#include "monocypher-ed25519.h"

#include "mlkem_bench.h"
#include "mldsa_bench.h"
#include "trng_bench.h"
#include "pico/rand.h"
#include "mlk_runner.h"
#include "rsa2048_test_key.h"
#include "version.h"
#include "hardware/adc.h"

#define BENCH_MIN_US    750000u
#define BENCH_MAX_ITERS 100000u
#define BULK_BYTES      4096

#if defined(__riscv)
#define ARCH_NAME "RISC-V (Hazard3)"
#else
#define ARCH_NAME "Arm (Cortex-M33)"
#endif

#if uECC_OPTIMIZATION_LEVEL >= 3 && !defined(__riscv)
#define UECC_FLAVOUR "asm"
#else
#define UECC_FLAVOUR "C"
#endif

/* ---- shared state ------------------------------------------------------- */

static mbedtls_entropy_context  g_entropy;
static mbedtls_ctr_drbg_context g_drbg;
#define RNG mbedtls_ctr_drbg_random, &g_drbg

static uint8_t g_buf[BULK_BYTES];
static uint8_t g_hash[32];

/* Mbed TLS */
static mbedtls_ecdsa_context g_mb_ec;
static mbedtls_ecdsa_context g_mb_ec384;
static uint8_t g_mb_sig384[MBEDTLS_ECDSA_MAX_LEN];
static size_t  g_mb_siglen384;
static uint8_t g_mb_sig[MBEDTLS_ECDSA_MAX_LEN];
static size_t  g_mb_siglen;
static mbedtls_ecp_group g_mb_grp, g_mb_grp25519;
static mbedtls_mpi g_mb_d, g_mb_d25519;
static mbedtls_ecp_point g_mb_Q, g_mb_R, g_mb_Q25519, g_mb_R25519;
static mbedtls_rsa_context g_rsa;
static uint8_t g_rsasig[256];

/* p256-m */
static uint8_t g_pm_priv[32], g_pm_pub[64], g_pm_sig[64], g_pm_secret[32];

/* micro-ecc */
static uint8_t g_ue_priv[32], g_ue_pub[64], g_ue_sig[64], g_ue_secret[32];

/* Monocypher */
static uint8_t g_mc_sk[32], g_mc_pk[32], g_mc_shared[32];
static uint8_t g_ed_sk[64], g_ed_pk[32], g_ed_sig[64];

/* ---- RNG glue ----------------------------------------------------------- */

/* The copy of p256-m vendored inside Mbed TLS defines p256_generate_random()
 * itself as a wrapper over the PSA hook below. psa_status_t is int32_t and
 * PSA_SUCCESS is 0. */
int32_t psa_generate_random(uint8_t *output, size_t output_size);
int32_t psa_generate_random(uint8_t *output, size_t output_size)
{
    return mbedtls_ctr_drbg_random(&g_drbg, output, output_size) == 0 ? 0 : -148;
}

static int uecc_rng(uint8_t *dest, unsigned size)
{
    return mbedtls_ctr_drbg_random(&g_drbg, dest, size) == 0 ? 1 : 0;
}

/* ---- ML-KEM ------------------------------------------------------------- */

static uint8_t mlk_pk[MLK_MAX_PK], mlk_sk[MLK_MAX_SK];
static uint8_t mlk_ct[MLK_MAX_CT], mlk_ss[MLK_SS], mlk_ss2[MLK_SS];
static int     mlk_cur;   /* which variant the timed function should run */

/* mlkem-native asks the application for randomness. */
int randombytes(uint8_t *out, size_t outlen);
int randombytes(uint8_t *out, size_t outlen)
{
    return mbedtls_ctr_drbg_random(&g_drbg, out, outlen) == 0 ? 0 : -1;
}

static int b_mlk_keygen(void)
{
    const mlk_variant_t *v = &mlk_variants[mlk_cur];
    return v->keypair(mlk_pk, mlk_sk);
}
static int b_mlk_enc(void)
{
    const mlk_variant_t *v = &mlk_variants[mlk_cur];
    return v->enc(mlk_ct, mlk_ss, mlk_pk);
}
static int b_mlk_dec(void)
{
    const mlk_variant_t *v = &mlk_variants[mlk_cur];
    return v->dec(mlk_ss2, mlk_ct, mlk_sk);
}

/* ---- ML-DSA -------------------------------------------------------------- */

static uint8_t mld_pk[MLD_MAX_PK], mld_sk[MLD_MAX_SK], mld_sig[MLD_MAX_SIG];
static int     mld_cur;
static const uint8_t mld_seed[32] = {
    0x10,0x11,0x12,0x13,0x14,0x15,0x16,0x17,0x18,0x19,0x1a,0x1b,0x1c,0x1d,0x1e,0x1f,
    0x20,0x21,0x22,0x23,0x24,0x25,0x26,0x27,0x28,0x29,0x2a,0x2b,0x2c,0x2d,0x2e,0x2f };
static const uint8_t mld_rnd[32] = {
    0x30,0x31,0x32,0x33,0x34,0x35,0x36,0x37,0x38,0x39,0x3a,0x3b,0x3c,0x3d,0x3e,0x3f,
    0x40,0x41,0x42,0x43,0x44,0x45,0x46,0x47,0x48,0x49,0x4a,0x4b,0x4c,0x4d,0x4e,0x4f };

/* Private-key operations: key generation and signing. Both deterministic. */
static int b_mld_keygen(void)
{
    return mld_variants[mld_cur].keypair_det(mld_pk, mld_sk, mld_seed);
}
static int b_mld_sign(void)
{
    return mld_variants[mld_cur].sign_det(mld_sig, g_hash, 32,
                                          (const uint8_t *)MLD_PRE_EMPTY_CTX,
                                          MLD_PRE_EMPTY_LEN, mld_rnd, mld_sk);
}
/* Public-key side, for context. */
static int b_mld_verify(void)
{
    return mld_variants[mld_cur].verify(mld_sig, g_hash, 32, NULL, 0, mld_pk);
}

/* ---- random number generation ------------------------------------------- */

static uint8_t g_rng_buf[1024];

/* Raw source rate: every hardware health check bypassed, nothing in the loop
 * but the register reads. */
static int b_trng_raw(void)
{
    trng_bench_start(false, 0);
    return trng_bench_read(g_rng_buf, sizeof(g_rng_buf)) ? 0 : -1;
}
static int b_pico_rand(void)
{
    for (size_t i = 0; i < sizeof(g_rng_buf); i += 8) {
        uint64_t r = get_rand_64();
        memcpy(g_rng_buf + i, &r, 8);
    }
    return 0;
}
static int b_ctr_drbg(void)
{
    return mbedtls_ctr_drbg_random(&g_drbg, g_rng_buf, sizeof(g_rng_buf));
}

/* ---- harness ------------------------------------------------------------ */

typedef int (*bench_fn)(void);

typedef struct {
    const char *group;
    const char *lib;
    bench_fn    fn;
    uint32_t    bytes;      /* non-zero => also report throughput */
} bench_case;

static uint64_t g_result[8];    /* us/op for the current group */

static void print_group(const bench_case *cases, int n)
{
    uint64_t best = 0;
    for (int i = 0; i < n; i++)
        if (g_result[i] && (best == 0 || g_result[i] < best)) best = g_result[i];

    printf("\n %s\n", cases[0].group);
    for (int i = 0; i < n; i++) {
        printf("   %-22s", cases[i].lib);
        if (!g_result[i]) { printf("  failed\n"); continue; }

        uint64_t us = g_result[i];
        if (us >= 1000) printf("%8llu.%03llu ms", us / 1000, us % 1000);
        else            printf("%8llu.%03llu us", us, 0ull);

        if (cases[i].bytes) {
            printf("  %7llu KiB/s", (uint64_t)cases[i].bytes * 1000000ull / us / 1024ull);
        } else {
            uint64_t ops_x100 = 100000000ull / us;
            printf("  %8llu.%02llu ops/s", ops_x100 / 100, ops_x100 % 100);
        }

        /* A relative factor only means something when the rows are competing
         * implementations of the same operation. Single-row groups are just a
         * measurement, so no comparison is printed. */
        if (n > 1) {
            uint64_t rel_x100 = us * 100ull / best;
            if (rel_x100 == 100) printf("   <-- fastest\n");
            else                 printf("   %llu.%02llux\n", rel_x100 / 100, rel_x100 % 100);
        } else {
            printf("\n");
        }
    }
}

static void run_group(const bench_case *cases, int n)
{
    for (int i = 0; i < n; i++) {
        g_result[i] = 0;
        if (cases[i].fn() != 0) continue;

        uint32_t iters = 0;
        uint64_t us;
        absolute_time_t t0 = get_absolute_time();
        do {
            if (cases[i].fn() != 0) { iters = 0; break; }
            iters++;
            us = (uint64_t)absolute_time_diff_us(t0, get_absolute_time());
        } while (us < BENCH_MIN_US && iters < BENCH_MAX_ITERS);

        if (iters) g_result[i] = us / iters;
    }
    print_group(cases, n);
}

/* ---- SHA-256 ------------------------------------------------------------ */

static int b_sha_mbed(void) { return mbedtls_sha256(g_buf, BULK_BYTES, g_hash, 0); }

static int b_sha_hw(void)
{
    pico_sha256_state_t st;
    sha256_result_t res;
    int rc = pico_sha256_start_blocking(&st, SHA256_BIG_ENDIAN, true);
    if (rc) return rc;
    pico_sha256_update_blocking(&st, g_buf, BULK_BYTES);
    pico_sha256_finish(&st, &res);
    return 0;
}

static int b_blake2b(void)
{
    uint8_t out[64];
    crypto_blake2b(out, sizeof(out), g_buf, BULK_BYTES);
    return 0;
}

/* ---- P-256 sign --------------------------------------------------------- */

static int b_sign_mbed(void)
{
    return mbedtls_ecdsa_write_signature(&g_mb_ec, MBEDTLS_MD_SHA256, g_hash, 32,
                                         g_mb_sig, sizeof(g_mb_sig), &g_mb_siglen, RNG);
}
static int b_sign_p256m(void)
{
    return p256_ecdsa_sign(g_pm_sig, g_pm_priv, g_hash, 32);
}
static int b_sign_uecc(void)
{
    return uECC_sign(g_ue_priv, g_hash, 32, g_ue_sig, uECC_secp256r1()) ? 0 : -1;
}

static int b_sign384_mbed(void)
{
    return mbedtls_ecdsa_write_signature(&g_mb_ec384, MBEDTLS_MD_SHA256, g_hash, 32,
                                         g_mb_sig384, sizeof(g_mb_sig384), &g_mb_siglen384, RNG);
}
static int b_verify384_mbed(void)
{
    return mbedtls_ecdsa_read_signature(&g_mb_ec384, g_hash, 32, g_mb_sig384, g_mb_siglen384);
}

/* ---- P-256 verify ------------------------------------------------------- */

static int b_verify_mbed(void)
{
    return mbedtls_ecdsa_read_signature(&g_mb_ec, g_hash, 32, g_mb_sig, g_mb_siglen);
}
static int b_verify_p256m(void)
{
    return p256_ecdsa_verify(g_pm_sig, g_pm_pub, g_hash, 32);
}
static int b_verify_uecc(void)
{
    return uECC_verify(g_ue_pub, g_hash, 32, g_ue_sig, uECC_secp256r1()) ? 0 : -1;
}

/* ---- ECDH P-256 --------------------------------------------------------- */

static int b_ecdh_mbed(void)
{
    return mbedtls_ecp_mul(&g_mb_grp, &g_mb_R, &g_mb_d, &g_mb_Q, RNG);
}
static int b_ecdh_p256m(void)
{
    return p256_ecdh_shared_secret(g_pm_secret, g_pm_priv, g_pm_pub);
}
static int b_ecdh_uecc(void)
{
    return uECC_shared_secret(g_ue_pub, g_ue_priv, g_ue_secret, uECC_secp256r1()) ? 0 : -1;
}

/* ---- P-256 keygen ------------------------------------------------------- */

static int b_keygen_mbed(void)
{
    mbedtls_ecdsa_context c;
    mbedtls_ecdsa_init(&c);
    int rc = mbedtls_ecdsa_genkey(&c, MBEDTLS_ECP_DP_SECP256R1, RNG);
    mbedtls_ecdsa_free(&c);
    return rc;
}
static int b_keygen_p256m(void)
{
    uint8_t priv[32], pub[64];
    return p256_gen_keypair(priv, pub);
}
static int b_keygen_uecc(void)
{
    uint8_t priv[32], pub[64];
    return uECC_make_key(pub, priv, uECC_secp256r1()) ? 0 : -1;
}

/* ---- X25519 ------------------------------------------------------------- */

static int b_x25519_mbed(void)
{
    return mbedtls_ecp_mul(&g_mb_grp25519, &g_mb_R25519, &g_mb_d25519, &g_mb_Q25519, RNG);
}
static int b_x25519_mc(void)
{
    crypto_x25519(g_mc_shared, g_mc_sk, g_mc_pk);
    return 0;
}

/* ---- Ed25519 (Monocypher) ----------------------------------------------- */

static int b_ed_sign(void)
{
    crypto_ed25519_sign(g_ed_sig, g_ed_sk, g_hash, 32);
    return 0;
}
static int b_ed_verify(void)
{
    return crypto_ed25519_check(g_ed_sig, g_ed_pk, g_hash, 32);
}

/* ---- RSA ---------------------------------------------------------------- */

static int b_rsa_sign(void)
{
    return mbedtls_rsa_pkcs1_sign(&g_rsa, RNG, MBEDTLS_MD_SHA256, 32, g_hash, g_rsasig);
}
static int b_rsa_verify(void)
{
    return mbedtls_rsa_pkcs1_verify(&g_rsa, MBEDTLS_MD_SHA256, 32, g_hash, g_rsasig);
}

/* ---- setup -------------------------------------------------------------- */

static int setup(void)
{
    int rc;

    mbedtls_entropy_init(&g_entropy);
    mbedtls_ctr_drbg_init(&g_drbg);
    if ((rc = mbedtls_ctr_drbg_seed(&g_drbg, mbedtls_entropy_func, &g_entropy,
                                    (const unsigned char *)"rp2350", 6))) return rc;

    for (int i = 0; i < BULK_BYTES; i++) g_buf[i] = (uint8_t)i;
    mbedtls_sha256(g_buf, BULK_BYTES, g_hash, 0);

    uECC_set_rng(uecc_rng);

    /* Mbed TLS P-256 */
    mbedtls_ecdsa_init(&g_mb_ec);
    if ((rc = mbedtls_ecdsa_genkey(&g_mb_ec, MBEDTLS_ECP_DP_SECP256R1, RNG))) return rc;
    if ((rc = b_sign_mbed())) return rc;

    mbedtls_ecdsa_init(&g_mb_ec384);
    if ((rc = mbedtls_ecdsa_genkey(&g_mb_ec384, MBEDTLS_ECP_DP_SECP384R1, RNG))) return rc;
    if ((rc = b_sign384_mbed())) return rc;

    mbedtls_ecp_group_init(&g_mb_grp);
    mbedtls_mpi_init(&g_mb_d);
    mbedtls_ecp_point_init(&g_mb_Q);
    mbedtls_ecp_point_init(&g_mb_R);
    if ((rc = mbedtls_ecp_group_load(&g_mb_grp, MBEDTLS_ECP_DP_SECP256R1))) return rc;
    if ((rc = mbedtls_ecp_gen_keypair(&g_mb_grp, &g_mb_d, &g_mb_Q, RNG))) return rc;

    mbedtls_ecp_group_init(&g_mb_grp25519);
    mbedtls_mpi_init(&g_mb_d25519);
    mbedtls_ecp_point_init(&g_mb_Q25519);
    mbedtls_ecp_point_init(&g_mb_R25519);
    if ((rc = mbedtls_ecp_group_load(&g_mb_grp25519, MBEDTLS_ECP_DP_CURVE25519))) return rc;
    if ((rc = mbedtls_ecp_gen_keypair(&g_mb_grp25519, &g_mb_d25519, &g_mb_Q25519, RNG))) return rc;

    /* p256-m */
    if ((rc = p256_gen_keypair(g_pm_priv, g_pm_pub))) return rc;
    if ((rc = p256_ecdsa_sign(g_pm_sig, g_pm_priv, g_hash, 32))) return rc;

    /* micro-ecc */
    if (!uECC_make_key(g_ue_pub, g_ue_priv, uECC_secp256r1())) return -1;
    if (!uECC_sign(g_ue_priv, g_hash, 32, g_ue_sig, uECC_secp256r1())) return -2;

    /* Monocypher */
    mbedtls_ctr_drbg_random(&g_drbg, g_mc_sk, 32);
    crypto_x25519_public_key(g_mc_pk, g_mc_sk);
    uint8_t seed[32];
    mbedtls_ctr_drbg_random(&g_drbg, seed, 32);
    crypto_ed25519_key_pair(g_ed_sk, g_ed_pk, seed);
    crypto_ed25519_sign(g_ed_sig, g_ed_sk, g_hash, 32);

    /* RSA-2048 */
    mbedtls_mpi N, P, Q, D, E;
    mbedtls_mpi_init(&N); mbedtls_mpi_init(&P); mbedtls_mpi_init(&Q);
    mbedtls_mpi_init(&D); mbedtls_mpi_init(&E);
    mbedtls_rsa_init(&g_rsa);
    rc  = mbedtls_mpi_read_string(&N, 16, RSA_N);
    rc |= mbedtls_mpi_read_string(&P, 16, RSA_P);
    rc |= mbedtls_mpi_read_string(&Q, 16, RSA_Q);
    rc |= mbedtls_mpi_read_string(&D, 16, RSA_D);
    rc |= mbedtls_mpi_read_string(&E, 16, RSA_E);
    if (rc) return rc;
    if ((rc = mbedtls_rsa_import(&g_rsa, &N, &P, &Q, &D, &E))) return rc;
    if ((rc = mbedtls_rsa_complete(&g_rsa))) return rc;
    if ((rc = mbedtls_rsa_set_padding(&g_rsa, MBEDTLS_RSA_PKCS_V15, MBEDTLS_MD_SHA256))) return rc;
    mbedtls_mpi_free(&N); mbedtls_mpi_free(&P); mbedtls_mpi_free(&Q);
    mbedtls_mpi_free(&D); mbedtls_mpi_free(&E);

    return 0;
}

/* ---- table -------------------------------------------------------------- */

static const bench_case c_sha[] = {
    { "Hash 4 KiB (throughput)", "SHA-256 Mbed TLS sw", b_sha_mbed, BULK_BYTES },
    { "Hash 4 KiB (throughput)", "SHA-256 RP2350 hw",  b_sha_hw,   BULK_BYTES },
    { "Hash 4 KiB (throughput)", "BLAKE2b Monocypher", b_blake2b, BULK_BYTES },
};
static const bench_case c_keygen[] = {
    { "P-256 key generation", "Mbed TLS",  b_keygen_mbed,  0 },
    { "P-256 key generation", "p256-m",    b_keygen_p256m, 0 },
    { "P-256 key generation", "micro-ecc", b_keygen_uecc,  0 },
};
static const bench_case c_sign[] = {
    { "ECDSA P-256 sign",     "Mbed TLS",  b_sign_mbed,  0 },
    { "ECDSA P-256 sign",     "p256-m",    b_sign_p256m, 0 },
    { "ECDSA P-256 sign",     "micro-ecc", b_sign_uecc,  0 },
};
static const bench_case c_verify[] = {
    { "ECDSA P-256 verify",   "Mbed TLS",  b_verify_mbed,  0 },
    { "ECDSA P-256 verify",   "p256-m",    b_verify_p256m, 0 },
    { "ECDSA P-256 verify",   "micro-ecc", b_verify_uecc,  0 },
};
static const bench_case c_ecdh[] = {
    { "ECDH P-256",           "Mbed TLS",  b_ecdh_mbed,  0 },
    { "ECDH P-256",           "p256-m",    b_ecdh_p256m, 0 },
    { "ECDH P-256",           "micro-ecc", b_ecdh_uecc,  0 },
};
static const bench_case c_x[] = {
    { "X25519",               "Mbed TLS",   b_x25519_mbed, 0 },
    { "X25519",               "Monocypher", b_x25519_mc,   0 },
};
static const bench_case c_p384_sign[] = {
    { "ECDSA P-384 sign",    "Mbed TLS",   b_sign384_mbed,   0 },
};
static const bench_case c_p384_verify[] = {
    { "ECDSA P-384 verify",  "Mbed TLS",   b_verify384_mbed, 0 },
};
static const bench_case c_ed_sign[] = {
    { "Ed25519 sign",        "Monocypher", b_ed_sign,   0 },
};
static const bench_case c_ed_verify[] = {
    { "Ed25519 verify",      "Monocypher", b_ed_verify, 0 },
};
static const bench_case c_rsa_sign[] = {
    { "RSA-2048 sign (CRT)", "Mbed TLS",   b_rsa_sign,   0 },
};
static const bench_case c_rsa_verify[] = {
    { "RSA-2048 verify",     "Mbed TLS",   b_rsa_verify, 0 },
};

#define RUN(x) run_group(x, (int)(sizeof(x) / sizeof((x)[0])))

static void run_all(void)
{
    bench_print_runtime_state();

    RUN(c_sha);
    RUN(c_keygen);
    RUN(c_sign);
    RUN(c_verify);
    RUN(c_ecdh);
    RUN(c_p384_sign);
    RUN(c_p384_verify);
    RUN(c_x);
    RUN(c_ed_sign);
    RUN(c_ed_verify);
    /* ML-KEM. Timed on core 1 because level 1024 needs ~20 KiB of stack, which
     * does not fit the 4 KiB main stack. Reported per level and per operation
     * rather than as one comparison group: a relative factor across security
     * levels would only ever say "512 is fastest", which is true by
     * construction and not a useful comparison. */
    printf("\n ML-KEM (mlkem-native, portable C, run on core 1)\n");
    for (int i = 0; i < 3; i++) {
        mlk_cur = i;
        const mlk_variant_t *v = &mlk_variants[i];
        if (mlk_call_on_core1(b_mlk_keygen) || mlk_call_on_core1(b_mlk_enc)) {
            printf("   %-22s setup failed\n", v->name);
            continue;
        }
        uint64_t tk = mlk_time_on_core1(b_mlk_keygen, BENCH_MIN_US);
        uint64_t te = mlk_time_on_core1(b_mlk_enc,    BENCH_MIN_US);
        uint64_t td = mlk_time_on_core1(b_mlk_dec,    BENCH_MIN_US);
        printf("   %-12s keygen %7llu.%03llu ms (%6llu.%02llu ops/s)\n",
               v->name, tk / 1000, tk % 1000,
               (100000000ull / tk) / 100, (100000000ull / tk) % 100);
        printf("   %-12s encap  %7llu.%03llu ms (%6llu.%02llu ops/s)\n", "",
               te / 1000, te % 1000,
               (100000000ull / te) / 100, (100000000ull / te) % 100);
        printf("   %-12s decap  %7llu.%03llu ms (%6llu.%02llu ops/s)\n", "",
               td / 1000, td % 1000,
               (100000000ull / td) / 100, (100000000ull / td) % 100);
        printf("   %-13s pk %4u B   sk %4u B   ct %4u B   ss %u B\n",
               "", (unsigned)v->pk_bytes, (unsigned)v->sk_bytes,
               (unsigned)v->ct_bytes, (unsigned)MLK_SS);
    }

    /* ML-DSA. Private-key operations first, since those are what a signing
     * device actually spends its time on. Run on core 1 for stack reasons. */
    printf("\n ML-DSA (mldsa-native, reduced-RAM C, run on core 1)\n");
    for (int i = 0; i < 3; i++) {
        mld_cur = i;
        const mld_variant_t *v = &mld_variants[i];
        if (mlk_call_on_core1(b_mld_keygen) || mlk_call_on_core1(b_mld_sign)) {
            printf("   %-12s setup failed\n", v->name);
            continue;
        }
        if (mlk_call_on_core1(b_mld_verify)) {
            printf("   %-12s signature did not verify\n", v->name);
            continue;
        }
        uint64_t tk = mlk_time_on_core1(b_mld_keygen, BENCH_MIN_US);
        uint64_t ts = mlk_time_on_core1(b_mld_sign,   BENCH_MIN_US);
        uint64_t tv = mlk_time_on_core1(b_mld_verify, BENCH_MIN_US);
        printf("   %-12s keygen  %7llu.%03llu ms (%6llu.%02llu ops/s)  [private]\n",
               v->name, tk / 1000, tk % 1000,
               (100000000ull / tk) / 100, (100000000ull / tk) % 100);
        printf("   %-12s sign    %7llu.%03llu ms (%6llu.%02llu ops/s)  [private]\n", "",
               ts / 1000, ts % 1000,
               (100000000ull / ts) / 100, (100000000ull / ts) % 100);
        printf("   %-12s verify  %7llu.%03llu ms (%6llu.%02llu ops/s)\n", "",
               tv / 1000, tv % 1000,
               (100000000ull / tv) / 100, (100000000ull / tv) % 100);
        printf("   %-12s pk %4u B   sk %4u B   sig %4u B\n", "",
               (unsigned)v->pk_bytes, (unsigned)v->sk_bytes, (unsigned)v->sig_bytes);
    }

    /* Random number generation. The raw TRNG is the entropy source; pico_rand
     * and CTR_DRBG are the deterministic generators seeded from it, and are
     * what the crypto above actually consumes. */
    {
        const bench_case rng[] = {
            { "Random bytes (1 KiB)", "TRNG raw (on-chip)", b_trng_raw,  1024 },
            { "Random bytes (1 KiB)", "pico_rand PRNG",     b_pico_rand, 1024 },
            { "Random bytes (1 KiB)", "Mbed TLS CTR_DRBG",  b_ctr_drbg,  1024 },
        };
        run_group(rng, 3);
    }

    RUN(c_rsa_sign);
    RUN(c_rsa_verify);

    printf("\n Relative factors compare implementations of the SAME operation.\n");
    printf(" Groups with one row are a measurement only, not a comparison.\n");
    printf("\n done.\n");
}

int main(void)
{
    stdio_init_all();
    adc_init();
    adc_set_temp_sensor_enabled(true);
#if LIB_PICO_STDIO_USB
    for (int i = 0; i < 100 && !stdio_usb_connected(); i++) sleep_ms(100);
    sleep_ms(300);
#endif

    bench_print_build_info("crypto_shootout");

    int rc = setup();
    if (rc != 0) {
        while (true) { printf("setup failed: %d (-0x%04x)\n", rc, (unsigned)(-rc)); sleep_ms(2000); }
    }

    while (true) {
        run_all();
        printf("\n press any key to run again...\n");
        getchar();
    }
}
