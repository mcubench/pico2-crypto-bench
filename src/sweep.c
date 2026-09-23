/*
 * RP2350 overclock sweep, v2.0.0
 *
 * One independent frequency ladder per crypto library. At every step the
 * firmware reports:
 *
 *   cycles per operation      absolute, and relative to the 150 MHz baseline,
 *                             so memory-bound scaling is visible on the console
 *   XIP cache hit rate        measured from XIP_CTR_HIT / XIP_CTR_ACC rather
 *                             than inferred
 *   single vs dual core       the same operation run on one core and on both,
 *                             with the achieved scaling factor
 *   VSYS                      a sagging supply looks like an unstable core
 *   QMI divider and RXDELAY   both selectable, RXDELAY optionally scanned
 *
 * Die temperature is not reported: the sensor reads full scale on this board.
 *
 * WARNING: core voltages above 1.30 V are outside the RP2350 datasheet
 * operating range. Use a dev board, never a device holding real keys.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "pico/stdlib.h"
#include "pico/rand.h"
#include "pico/sha256.h"
#include "pico/multicore.h"
#include "hardware/clocks.h"
#include "hardware/vreg.h"
#include "hardware/watchdog.h"
#include "hardware/structs/watchdog.h"
#include "hardware/structs/qmi.h"
#include "hardware/regs/qmi.h"
#include "hardware/regs/addressmap.h"
#include "hardware/uart.h"
#include "hardware/adc.h"
#include "hardware/sync.h"

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
#include "progress.h"
#include "mlk_runner.h"
#include "rsa2048_test_key.h"
#include "version.h"

/* ---- parameters --------------------------------------------------------- */

#ifndef SWEEP_START_KHZ
#define SWEEP_START_KHZ     150000u
#endif
#ifndef SWEEP_MAX_KHZ
#define SWEEP_MAX_KHZ       600000u
#endif
#ifndef QMI_SCK_MAX_MHZ
#define QMI_SCK_MAX_MHZ     115u
#endif
#ifndef QMI_SCK_MIN_MHZ
#define QMI_SCK_MIN_MHZ     100u
#endif

#define WATCHDOG_MS         6000u
#define BULK_BYTES          4096
#define TIMED_US            120000u
#define DUAL_MIN_GAIN_X100  105u     /* <= 1.05x means core 1 added no useful work */
#define RAMTEST_WORDS       2048
#define NCORES              2
#define MAX_STEPS           160
#define FLASH_CRC_BYTES     (32u * 1024u)

#define MAGIC       0x53575034u   /* "SWP4" */
#define SC_MAGIC    0
#define SC_POS      1
#define SC_RES0     2
#define SC_RES1     3
#define MAX_SUITES  9

/* ---- shared state ------------------------------------------------------- */

static mbedtls_entropy_context  g_entropy;
static mbedtls_ctr_drbg_context g_drbg[NCORES];

static uint8_t  g_buf[BULK_BYTES];
static uint8_t  g_msg[32];
static uint32_t g_ram[RAMTEST_WORDS];

static mbedtls_ecdsa_context g_mb_ec[NCORES];
static uint8_t g_mb_sig[NCORES][MBEDTLS_ECDSA_MAX_LEN];
static size_t  g_mb_siglen[NCORES];
static mbedtls_ecp_group     g_mb_grp;
static mbedtls_mpi           g_mb_d;
static mbedtls_ecp_point     g_mb_Q, g_mb_R;
static mbedtls_rsa_context   g_rsa;

static uint8_t g_pm_a_priv[32], g_pm_a_pub[64], g_pm_b_pub[64];
static uint8_t g_ue_a_priv[32], g_ue_a_pub[64], g_ue_b_pub[64];
static uint8_t g_pm_sig[NCORES][64];
static uint8_t g_ue_sig[NCORES][64];

static const uint8_t k_x_priv[32] = {
    0x77,0x07,0x6d,0x0a,0x73,0x18,0xa5,0x7d,0x3c,0x16,0xc1,0x72,0x51,0xb2,0x66,0x45,
    0xdf,0x4c,0x2f,0x87,0xeb,0xc0,0x99,0x2a,0xb1,0x77,0xfb,0xa5,0x1d,0xb9,0x2c,0x2a
};
static const uint8_t k_x_pub[32] = {
    0xde,0x9e,0xdb,0x7d,0x7b,0x7d,0xc1,0xb4,0xd3,0x5b,0x61,0xc2,0xec,0xe4,0x35,0x37,
    0x3f,0x83,0x43,0xc8,0x5b,0x78,0x67,0x4d,0xad,0xfc,0x7e,0x14,0x6f,0x88,0x2b,0x4f
};
static uint8_t g_ed_sk[64], g_ed_pk[32];

static uint8_t  ref_sha[32];
static uint8_t  ref_mb_point[65];
static uint8_t  ref_rsa_sig[256];
static uint8_t  ref_pm_ecdh[32];
static uint8_t  ref_ue_ecdh[32];
static uint8_t  ref_x25519[32];
static uint8_t  ref_ed_sig[64];
static uint8_t  ref_blake[64];
static uint32_t ref_torture;
static uint32_t ref_ram;
static uint32_t ref_flash_crc;

/* ML-KEM: deterministic references. keypair_derand and enc_derand take explicit
 * coins, so every output is byte-exact and a single flipped bit fails. One
 * SHA-256 over ek||dk||ct||ss per level keeps the reference compact. */
static uint8_t  mlk_pk[MLK_MAX_PK], mlk_sk[MLK_MAX_SK];
static uint8_t  mlk_ct[MLK_MAX_CT], mlk_ss[MLK_SS], mlk_ss2[MLK_SS];
static uint8_t  ref_mlk[3][32];
static int      mlk_cur;
static const uint8_t mlk_kp_coins[64] = {
    0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f,0x10,
    0x11,0x12,0x13,0x14,0x15,0x16,0x17,0x18,0x19,0x1a,0x1b,0x1c,0x1d,0x1e,0x1f,0x20,
    0x21,0x22,0x23,0x24,0x25,0x26,0x27,0x28,0x29,0x2a,0x2b,0x2c,0x2d,0x2e,0x2f,0x30,
    0x31,0x32,0x33,0x34,0x35,0x36,0x37,0x38,0x39,0x3a,0x3b,0x3c,0x3d,0x3e,0x3f,0x40 };
/* ML-DSA: deterministic key generation and signing, so signatures are
 * byte-exact and a single flipped bit fails the check. */
static uint8_t  mld_pk[MLD_MAX_PK], mld_sk[MLD_MAX_SK], mld_sig[MLD_MAX_SIG];
static uint8_t  ref_mld[3][32];
static int      mld_cur;
static const uint8_t mld_seed[32] = {
    0x10,0x11,0x12,0x13,0x14,0x15,0x16,0x17,0x18,0x19,0x1a,0x1b,0x1c,0x1d,0x1e,0x1f,
    0x20,0x21,0x22,0x23,0x24,0x25,0x26,0x27,0x28,0x29,0x2a,0x2b,0x2c,0x2d,0x2e,0x2f };
static const uint8_t mld_rnd[32] = {
    0x30,0x31,0x32,0x33,0x34,0x35,0x36,0x37,0x38,0x39,0x3a,0x3b,0x3c,0x3d,0x3e,0x3f,
    0x40,0x41,0x42,0x43,0x44,0x45,0x46,0x47,0x48,0x49,0x4a,0x4b,0x4c,0x4d,0x4e,0x4f };

static const uint8_t mlk_enc_coins[32] = {
    0xa0,0xa1,0xa2,0xa3,0xa4,0xa5,0xa6,0xa7,0xa8,0xa9,0xaa,0xab,0xac,0xad,0xae,0xaf,
    0xb0,0xb1,0xb2,0xb3,0xb4,0xb5,0xb6,0xb7,0xb8,0xb9,0xba,0xbb,0xbc,0xbd,0xbe,0xbf };

static int        g_vsel = 0;
static progress_t g_prog;          /* mirrored to flash after every step */
static bool       g_journal = true;
static volatile uint32_t g_sink[NCORES];

#define CORE() (get_core_num() & 1u)
#define RNG0 mbedtls_ctr_drbg_random, &g_drbg[0]
#define FAIL(msg) do { snprintf(err, n, msg); return false; } while (0)

/* Progress markers through setup, so a stall names the stage it stalled in
 * rather than leaving a silent console. */
static void stage(const char *what)
{
    printf("   setup: %s\n", what);
    fflush(stdout);
}

/* ---- RNG hooks: per-core DRBG so both cores can sign concurrently -------- */

static int uecc_rng(uint8_t *dest, unsigned size)
{
    return mbedtls_ctr_drbg_random(&g_drbg[CORE()], dest, size) == 0 ? 1 : 0;
}

int randombytes(uint8_t *out, size_t outlen);
int randombytes(uint8_t *out, size_t outlen)
{
    return mbedtls_ctr_drbg_random(&g_drbg[CORE()], out, outlen) == 0 ? 0 : -1;
}

int32_t psa_generate_random(uint8_t *output, size_t output_size);
int32_t psa_generate_random(uint8_t *output, size_t output_size)
{
    return mbedtls_ctr_drbg_random(&g_drbg[CORE()], output, output_size) == 0 ? 0 : -148;
}

/* ---- helpers ------------------------------------------------------------ */

static uint32_t torture(uint32_t rounds)
{
    uint32_t a = 0x12345678, b = 0x9e3779b9, c = 0xdeadbeef;
    for (uint32_t i = 0; i < rounds; i++) {
        uint64_t p = (uint64_t)a * b + c;
        a = (uint32_t)p ^ (uint32_t)(p >> 32);
        b = (b << 7) | (b >> 25);
        b += a;
        c ^= a + b + i;
        c = (c >> 11) | (c << 21);
    }
    return a ^ b ^ c;
}

static uint32_t ram_march(void)
{
    uint32_t x = 0x1234abcd;
    for (int i = 0; i < RAMTEST_WORDS; i++) {
        x ^= x << 13; x ^= x >> 17; x ^= x << 5;
        g_ram[i] = x;
    }
    uint32_t acc = 0;
    for (int i = RAMTEST_WORDS - 1; i >= 0; i--) acc = (acc << 1) ^ g_ram[i];
    return acc;
}

/* ---- QMI timing --------------------------------------------------------- */

/* Divider 2 is the floor: the bootrom never uses 1, and at low core clocks a
 * smaller divider buys nothing since flash is far from its limit anyway. */
static uint32_t qmi_div_for(uint32_t sys_khz, uint32_t *sck_khz_out)
{
    uint32_t div = (sys_khz + (QMI_SCK_MAX_MHZ * 1000u) - 1u) / (QMI_SCK_MAX_MHZ * 1000u);
    if (div < 2) div = 2;
    if (div > 255) div = 255;
    *sck_khz_out = sys_khz / div;
    return div;
}

/* Both of these run from SRAM: they retime the flash port, so the core must not
 * be fetching instructions through it while they execute. */
static void __no_inline_not_in_flash_func(qmi_apply)(uint32_t div, uint32_t rxdelay)
{
    uint32_t t = qmi_hw->m[0].timing;
    t &= ~(QMI_M0_TIMING_CLKDIV_BITS | QMI_M0_TIMING_RXDELAY_BITS);
    t |= (div << QMI_M0_TIMING_CLKDIV_LSB) & QMI_M0_TIMING_CLKDIV_BITS;
    t |= (rxdelay << QMI_M0_TIMING_RXDELAY_LSB) & QMI_M0_TIMING_RXDELAY_BITS;
    qmi_hw->m[0].timing = t;
    __compiler_memory_barrier();
    for (volatile int i = 0; i < 64; i++) { }
}

/* CRC32 of a fixed flash region read through the cache-bypass alias, so it
 * actually exercises the QMI at the current timing instead of hitting cache.
 * Entirely SRAM-resident: a bad RXDELAY corrupts data reads but cannot stop
 * instruction fetch, which is what makes scanning RXDELAY survivable. */
static uint32_t __no_inline_not_in_flash_func(flash_crc)(void)
{
    const volatile uint8_t *p = (const volatile uint8_t *)XIP_NOCACHE_NOALLOC_BASE;
    uint32_t crc = 0xffffffffu;
    for (uint32_t i = 0; i < FLASH_CRC_BYTES; i++) {
        crc ^= p[i];
        for (int b = 0; b < 8; b++)
            crc = (crc >> 1) ^ (0xedb88320u & (uint32_t)(-(int32_t)(crc & 1u)));
    }
    return ~crc;
}

static uint32_t __no_inline_not_in_flash_func(flash_crc_at)(uint32_t div, uint32_t rxdelay)
{
    qmi_apply(div, rxdelay);
    return flash_crc();
}

static void repin_peri(void)
{
    clock_configure(clk_peri, 0,
                    CLOCKS_CLK_PERI_CTRL_AUXSRC_VALUE_CLKSRC_PLL_USB,
                    48 * MHZ, 48 * MHZ);
#ifdef PICO_DEFAULT_UART
    uart_init(uart_default, PICO_DEFAULT_UART_BAUD_RATE);
#endif
}

/* "Safe" means the lowest clock of this sweep, which is not necessarily the
 * 150 MHz default: at an undervolted 0.85 V, 150 MHz may not run at all. */
static uint32_t g_floor_khz = SWEEP_START_KHZ;

static void goto_safe_clock(uint32_t rxdelay)
{
    set_sys_clock_khz(g_floor_khz, false);
    repin_peri();
    uint32_t sck;
    qmi_apply(qmi_div_for(g_floor_khz, &sck), rxdelay);
    sleep_ms(2);
}

static bool nearest_achievable(uint32_t want, uint32_t tol, uint32_t *got)
{
    uint vco, p1, p2;
    if (check_sys_clock_khz(want, &vco, &p1, &p2)) { *got = want; return true; }
    for (uint32_t d = 1000; d <= tol; d += 1000) {
        if (check_sys_clock_khz(want + d, &vco, &p1, &p2)) { *got = want + d; return true; }
        if (want > d && check_sys_clock_khz(want - d, &vco, &p1, &p2)) { *got = want - d; return true; }
    }
    return false;
}

/* ---- frequency ladders --------------------------------------------------- */

static uint32_t g_steps[MAX_STEPS];
static int      g_nsteps;

static void ladder_add(uint32_t mhz, uint32_t lo, uint32_t hi, bool aligned)
{
    if (mhz < lo || mhz > hi) return;
    uint32_t target;
    if (!nearest_achievable(mhz * 1000u, 6000, &target)) return;
    if (aligned) {
        uint32_t sck;
        (void)qmi_div_for(target, &sck);
        if (sck < QMI_SCK_MIN_MHZ * 1000u) return;   /* strictly worse: skip */
    }
    for (int i = 0; i < g_nsteps; i++) if (g_steps[i] == target) return;
    if (g_nsteps < MAX_STEPS) g_steps[g_nsteps++] = target;
}

static int cmp_u32(const void *a, const void *b)
{
    uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b;
    return (x > y) - (x < y);
}

/* mode 0: the plain 50/20/4 ladder.
 * mode 1: the same, minus every step whose SCK falls below the floor, plus the
 *         exact divider sweet spots f = d * SCK_MAX where SCK lands on the
 *         ceiling with nothing wasted. */
/* Underclock rungs. Geometric rather than linear: the interesting range spans
 * 18 to 150 MHz, an eight-fold ratio, so equal absolute steps would waste most
 * of the run at the top. 18 MHz is the practical PLL floor -- the VCO minimum
 * of 750 MHz over the maximum post-divide of 49 puts the hard limit at
 * 15.3 MHz, and 18 is the lowest clean setting below that. */
static const uint16_t k_under[] = { 18, 24, 36, 48, 60, 75, 100, 125 };

static void ladder_build(uint32_t lo_mhz, uint32_t hi_mhz, int mode)
{
    g_nsteps = 0;
    bool aligned = (mode == 1);

    for (size_t i = 0; i < sizeof(k_under) / sizeof(k_under[0]); i++)
        ladder_add(k_under[i], lo_mhz, hi_mhz, false);   /* window is unreachable here */

    for (uint32_t f = 150; f <= hi_mhz + 4; ) {
        ladder_add(f, lo_mhz, hi_mhz, aligned);
        f = (f < 400) ? f + 50 : (f < 500 ? f + 20 : f + 4);
    }
    if (aligned)
        for (uint32_t d = 2; d <= 8; d++)
            ladder_add(d * QMI_SCK_MAX_MHZ, lo_mhz, hi_mhz, true);

    qsort(g_steps, (size_t)g_nsteps, sizeof(g_steps[0]), cmp_u32);
}

/* ---- suites -------------------------------------------------------------- */

static bool chk_core(char *err, size_t n)
{
    if (torture(150000) != ref_torture) FAIL("ALU torture");
    if (ram_march() != ref_ram)         FAIL("SRAM march");
    return true;
}
static void tm_core(void) { g_sink[CORE()] = torture(50000); }

static bool chk_mbedtls(char *err, size_t n)
{
    uint8_t d[32];
    mbedtls_sha256(g_buf, BULK_BYTES, d, 0);
    if (memcmp(d, ref_sha, 32)) FAIL("SHA-256");
    {
        uint8_t pbuf[65]; size_t olen = 0;
        if (mbedtls_ecp_mul(&g_mb_grp, &g_mb_R, &g_mb_d, &g_mb_Q, RNG0)) FAIL("ecp_mul error");
        if (mbedtls_ecp_point_write_binary(&g_mb_grp, &g_mb_R, MBEDTLS_ECP_PF_UNCOMPRESSED,
                                           &olen, pbuf, sizeof(pbuf)))   FAIL("ecp_mul write");
        if (olen != 65 || memcmp(pbuf, ref_mb_point, 65))                FAIL("ecp_mul result");
    }
    if (mbedtls_ecdsa_write_signature(&g_mb_ec[0], MBEDTLS_MD_SHA256, g_msg, 32,
                                      g_mb_sig[0], sizeof(g_mb_sig[0]), &g_mb_siglen[0], RNG0))
        FAIL("ECDSA sign");
    if (mbedtls_ecdsa_read_signature(&g_mb_ec[0], g_msg, 32, g_mb_sig[0], g_mb_siglen[0]))
        FAIL("ECDSA verify");
    if (mbedtls_rsa_pkcs1_verify(&g_rsa, MBEDTLS_MD_SHA256, 32, ref_sha, ref_rsa_sig))
        FAIL("RSA-2048 verify");
    return true;
}
static void tm_mbedtls(void)
{
    uint c = CORE();
    mbedtls_ecdsa_write_signature(&g_mb_ec[c], MBEDTLS_MD_SHA256, g_msg, 32,
                                  g_mb_sig[c], sizeof(g_mb_sig[c]), &g_mb_siglen[c],
                                  mbedtls_ctr_drbg_random, &g_drbg[c]);
}

static bool chk_p256m(char *err, size_t n)
{
    uint8_t s[32];
    if (p256_ecdh_shared_secret(s, g_pm_a_priv, g_pm_b_pub))    FAIL("ECDH error");
    if (memcmp(s, ref_pm_ecdh, 32))                             FAIL("ECDH result");
    if (p256_ecdsa_sign(g_pm_sig[0], g_pm_a_priv, g_msg, 32))   FAIL("sign");
    if (p256_ecdsa_verify(g_pm_sig[0], g_pm_a_pub, g_msg, 32))  FAIL("verify");
    return true;
}
static void tm_p256m(void) { p256_ecdsa_sign(g_pm_sig[CORE()], g_pm_a_priv, g_msg, 32); }

static bool chk_uecc(char *err, size_t n)
{
    uint8_t s[32];
    if (!uECC_shared_secret(g_ue_b_pub, g_ue_a_priv, s, uECC_secp256r1()))  FAIL("ECDH error");
    if (memcmp(s, ref_ue_ecdh, 32))                                        FAIL("ECDH result");
    if (!uECC_sign(g_ue_a_priv, g_msg, 32, g_ue_sig[0], uECC_secp256r1())) FAIL("sign");
    if (!uECC_verify(g_ue_a_pub, g_msg, 32, g_ue_sig[0], uECC_secp256r1()))FAIL("verify");
    return true;
}
static void tm_uecc(void) { uECC_sign(g_ue_a_priv, g_msg, 32, g_ue_sig[CORE()], uECC_secp256r1()); }

static bool chk_monocypher(char *err, size_t n)
{
    uint8_t o32[32], o64[64];
    crypto_x25519(o32, k_x_priv, k_x_pub);
    if (memcmp(o32, ref_x25519, 32)) FAIL("X25519");
    crypto_ed25519_sign(o64, g_ed_sk, g_msg, 32);
    if (memcmp(o64, ref_ed_sig, 64))                   FAIL("Ed25519 sign");
    if (crypto_ed25519_check(o64, g_ed_pk, g_msg, 32)) FAIL("Ed25519 verify");
    crypto_blake2b(o64, 64, g_buf, BULK_BYTES);
    if (memcmp(o64, ref_blake, 64)) FAIL("BLAKE2b");
    return true;
}
static void tm_monocypher(void) { uint8_t s[64]; crypto_ed25519_sign(s, g_ed_sk, g_msg, 32); }

static bool hw_digest(uint8_t out[32])
{
    pico_sha256_state_t st; sha256_result_t res;
    if (pico_sha256_start_blocking(&st, SHA256_BIG_ENDIAN, true)) return false;
    pico_sha256_update_blocking(&st, g_buf, BULK_BYTES);
    pico_sha256_finish(&st, &res);
    memcpy(out, res.bytes, 32);
    return true;
}
static bool chk_sha_hw(char *err, size_t n)
{
    uint8_t d[32];
    for (int i = 0; i < 4; i++) {
        if (!hw_digest(d))          FAIL("engine start");
        if (memcmp(d, ref_sha, 32)) FAIL("digest mismatch");
    }
    return true;
}
static void tm_sha_hw(void) { uint8_t d[32]; hw_digest(d); }

/* Runs one level end to end deterministically and digests every output. */
static int mlk_digest(void)
{
    const mlk_variant_t *v = &mlk_variants[mlk_cur];
    mbedtls_sha256_context c;
    if (v->keypair_derand(mlk_pk, mlk_sk, mlk_kp_coins))        return -1;
    if (v->enc_derand(mlk_ct, mlk_ss, mlk_pk, mlk_enc_coins))   return -2;
    if (v->dec(mlk_ss2, mlk_ct, mlk_sk))                        return -3;
    if (memcmp(mlk_ss, mlk_ss2, MLK_SS))                        return -4;
    mbedtls_sha256_init(&c);
    mbedtls_sha256_starts(&c, 0);
    mbedtls_sha256_update(&c, mlk_pk, v->pk_bytes);
    mbedtls_sha256_update(&c, mlk_sk, v->sk_bytes);
    mbedtls_sha256_update(&c, mlk_ct, v->ct_bytes);
    mbedtls_sha256_update(&c, mlk_ss, MLK_SS);
    mbedtls_sha256_finish(&c, ref_mlk[mlk_cur]);
    mbedtls_sha256_free(&c);
    return 0;
}

static bool chk_mlkem(char *err, size_t n)
{
    uint8_t saved[3][32];
    memcpy(saved, ref_mlk, sizeof(saved));
    for (int i = 0; i < 3; i++) {
        mlk_cur = i;
        int rc = mlk_call_on_core1(mlk_digest);
        if (rc) { snprintf(err, n, "%s rc%d", mlk_variants[i].name, rc); return false; }
        if (memcmp(saved[i], ref_mlk[i], 32)) {
            snprintf(err, n, "%s mismatch", mlk_variants[i].name);
            memcpy(ref_mlk, saved, sizeof(saved));
            return false;
        }
    }
    return true;
}

/* Encapsulation at whichever level mlk_cur selects. */
static int mlk_enc_cur(void)
{
    const mlk_variant_t *v = &mlk_variants[mlk_cur];
    return v->enc_derand(mlk_ct, mlk_ss, mlk_pk, mlk_enc_coins);
}
static void tm_mlkem(void) { }   /* unused: this suite times on core 1 */

/* One level end to end: deterministic keygen, deterministic sign, verify,
 * then a digest over every output. */
static int mld_digest(void)
{
    const mld_variant_t *v = &mld_variants[mld_cur];
    mbedtls_sha256_context c;
    if (v->keypair_det(mld_pk, mld_sk, mld_seed))                       return -1;
    if (v->sign_det(mld_sig, g_msg, 32, (const uint8_t *)MLD_PRE_EMPTY_CTX,
                    MLD_PRE_EMPTY_LEN, mld_rnd, mld_sk))                return -2;
    if (v->verify(mld_sig, g_msg, 32, NULL, 0, mld_pk))                 return -3;
    mbedtls_sha256_init(&c);
    mbedtls_sha256_starts(&c, 0);
    mbedtls_sha256_update(&c, mld_pk, v->pk_bytes);
    mbedtls_sha256_update(&c, mld_sk, v->sk_bytes);
    mbedtls_sha256_update(&c, mld_sig, v->sig_bytes);
    mbedtls_sha256_finish(&c, ref_mld[mld_cur]);
    mbedtls_sha256_free(&c);
    return 0;
}

static bool chk_mldsa(char *err, size_t n)
{
    uint8_t saved[3][32];
    memcpy(saved, ref_mld, sizeof(saved));
    for (int i = 0; i < 3; i++) {
        mld_cur = i;
        int rc = mlk_call_on_core1(mld_digest);
        if (rc) { snprintf(err, n, "%s rc%d", mld_variants[i].name, rc); return false; }
        if (memcmp(saved[i], ref_mld[i], 32)) {
            snprintf(err, n, "%s mismatch", mld_variants[i].name);
            memcpy(ref_mld, saved, sizeof(saved));
            return false;
        }
    }
    return true;
}

/* The timed private-key operation: signing at whichever level mld_cur selects. */
static int mld_sign_cur(void)
{
    const mld_variant_t *v = &mld_variants[mld_cur];
    return v->sign_det(mld_sig, g_msg, 32, (const uint8_t *)MLD_PRE_EMPTY_CTX,
                       MLD_PRE_EMPTY_LEN, mld_rnd, mld_sk);
}
static int mld_keygen_cur(void)
{
    const mld_variant_t *v = &mld_variants[mld_cur];
    return v->keypair_det(mld_pk, mld_sk, mld_seed);
}
static void tm_mldsa(void) { }   /* unused: this suite times on core 1 */

/* ---- suite: on-chip TRNG ------------------------------------------------ */

static uint8_t g_trng_buf[256];

/* Entropy has no stored answer to compare against, so the gate is a liveness
 * check: the source delivered, and consecutive blocks differ. The full
 * SP 800-90B continuous suite is available with -DTRNG_FULL_HEALTH=1, but it is
 * off by default because it is slower than the source it measures and this
 * suite exists to report raw throughput. */
static bool chk_trng(char *err, size_t n)
{
    /* Left in the fast configuration: the hardware's von Neumann, CRNGT and
     * autocorrelation checks stay bypassed so the timed figure is the raw
     * source rate. The gate below is a liveness check only. */
    trng_bench_start(false, 0);
#if TRNG_FULL_HEALTH
    int rc = trng_bench_health();
#else
    int rc = trng_bench_liveness();
#endif
    if (rc == 0) return true;
    static const char *const why[] = { "ok", "source stalled", "stuck output",
                                       "repetition count", "adaptive proportion",
                                       "bit balance" };
    int i = -rc;
    snprintf(err, n, "%s", (i >= 1 && i <= 5) ? why[i] : "unknown");
    return false;
}
static void tm_trng(void) { trng_bench_read(g_trng_buf, sizeof(g_trng_buf)); }

typedef struct {
    const char *name;
    const char *timed_label;
    bool (*check)(char *err, size_t n);
    void (*timed)(void);
    bool dual_safe;     /* false where the hardware serialises anyway */
} suite_t;

static const suite_t k_suites[MAX_SUITES] = {
    { "core (ALU + SRAM)", "torture x50k", chk_core,       tm_core,       true  },
    { "Mbed TLS",          "ECDSA sign",   chk_mbedtls,    tm_mbedtls,    true  },
    { "p256-m",            "ECDSA sign",   chk_p256m,      tm_p256m,      true  },
    { "micro-ecc",         "ECDSA sign",   chk_uecc,       tm_uecc,       true  },
    { "Monocypher",        "Ed25519 sign", chk_monocypher, tm_monocypher, true  },
    { "SHA-256 hardware",  "hash 4 KiB",   chk_sha_hw,     tm_sha_hw,     false },
    { "ML-KEM 512/768/1024","encaps per level", chk_mlkem, tm_mlkem,  false },
    { "ML-DSA 44/65/87",   "sign per level",   chk_mldsa, tm_mldsa,  false },
    { "TRNG (on-chip)",    "256 B raw",        chk_trng,  tm_trng,   false },
};
#define SUITE_MLKEM 6
#define SUITE_MLDSA 7
#define SUITE_TRNG  8

/* ---- timing: single core and both cores --------------------------------- */

static volatile bool      g_go;
static void (* volatile   g_worker)(void);
static volatile uint32_t  g_ops1;
static volatile bool      g_c1_done;

static void core1_entry(void)
{
    void (*f)(void) = g_worker;
    uint32_t n = 0;
    while (g_go) { f(); n++; }
    g_ops1 = n;
    __compiler_memory_barrier();
    g_c1_done = true;
    while (true) tight_loop_contents();
}

/* Returns microseconds per operation on one core. */
static uint64_t time_single(void (*fn)(void), uint32_t *hit, uint32_t *acc)
{
    fn();
    bench_xip_reset();
    uint32_t iters = 0;
    uint64_t us;
    absolute_time_t t0 = get_absolute_time();
    do {
        fn();
        iters++;
        us = (uint64_t)absolute_time_diff_us(t0, get_absolute_time());
    } while (us < TIMED_US);
    bench_xip_read(hit, acc);
    return us / iters;
}

/* Returns microseconds per operation aggregated across both cores, i.e. the
 * elapsed time divided by the total number of operations completed. Perfect
 * scaling halves it relative to the single-core figure.
 *
 * Core 1 is launched only for the measurement window and reset immediately
 * afterwards, so it is never running while the clock or the flash timing is
 * being changed. */
static uint64_t time_dual(void (*fn)(void), uint32_t *total_ops,
                          uint32_t *core1_ops, bool *core1_completed)
{
    g_worker  = fn;
    g_ops1    = 0;
    g_c1_done = false;
    g_go      = true;
    __compiler_memory_barrier();

    multicore_reset_core1();
    multicore_launch_core1(core1_entry);

    uint32_t n0 = 0;
    uint64_t us;
    absolute_time_t t0 = get_absolute_time();
    do {
        fn();
        n0++;
        us = (uint64_t)absolute_time_diff_us(t0, get_absolute_time());
    } while (us < TIMED_US);

    g_go = false;
    __compiler_memory_barrier();

    absolute_time_t bail = make_timeout_time_ms(2000);
    while (!g_c1_done && absolute_time_diff_us(get_absolute_time(), bail) > 0)
        tight_loop_contents();

    uint32_t c1_ops = g_ops1;
    bool c1_completed = g_c1_done;
    multicore_reset_core1();

    uint32_t tot = n0 + c1_ops;
    if (total_ops) *total_ops = tot;
    if (core1_ops) *core1_ops = c1_ops;
    if (core1_completed) *core1_completed = c1_completed;
    return tot ? us / tot : 0;
}

/* ---- results in watchdog scratch[0..3] (the SDK owns [4..7]) ------------- */

/* Exact results live in RAM. They are also mirrored into scratch[2..3] as
 * MHz/4, four per word, purely so a watchdog reset does not lose them; values
 * recovered that way are accurate to 4 MHz and marked with '~'. */
static uint16_t g_result[MAX_SUITES];
static bool     g_result_approx[MAX_SUITES];

static void result_set(int suite, uint16_t mhz)
{
    g_result[suite] = mhz;
    g_result_approx[suite] = false;
    if (suite >= 8) return;          /* only 8 mirror slots; kept in RAM only */
    uint reg = (suite < 4) ? SC_RES0 : SC_RES1;
    uint shift = (uint)(suite % 4) * 8u;
    uint32_t w = watchdog_hw->scratch[reg];
    w &= ~(0xffu << shift);
    w |= ((uint32_t)((mhz / 4u) & 0xffu)) << shift;
    watchdog_hw->scratch[reg] = w;
}
static uint16_t result_get(int suite) { return g_result[suite]; }

static void results_load_from_scratch(void)
{
    for (int i = 0; i < MAX_SUITES && i < 8; i++) {
        uint reg = (i < 4) ? SC_RES0 : SC_RES1;
        uint shift = (uint)(i % 4) * 8u;
        uint16_t v = (uint16_t)(((watchdog_hw->scratch[reg] >> shift) & 0xffu) * 4u);
        g_result[i] = v;
        g_result_approx[i] = (v != 0);
    }
}
static void pos_set(int suite, uint32_t pos_mhz, uint32_t max_mhz, int vsel)
{
    watchdog_hw->scratch[SC_POS] =
        ((uint32_t)(pos_mhz & 0x3ffu))       |
        ((uint32_t)(max_mhz & 0x3ffu) << 10) |
        ((uint32_t)(vsel    & 0xfu)   << 20) |
        ((uint32_t)(suite   & 0xfu)   << 24);
}

/* ---- prompts ------------------------------------------------------------ */

typedef struct { const char *label; enum vreg_voltage v; uint16_t mv; } vopt_t;
static const vopt_t k_volts[] = {
    { "0.85 V (SDK minimum)", VREG_VOLTAGE_0_85, 850 },
    { "0.90 V",            VREG_VOLTAGE_0_90,  900 },
    { "0.95 V",            VREG_VOLTAGE_0_95,  950 },
    { "1.00 V",            VREG_VOLTAGE_1_00, 1000 },
    { "1.05 V",            VREG_VOLTAGE_1_05, 1050 },
    { "1.10 V (default)",  VREG_VOLTAGE_1_10, 1100 },
    { "1.20 V",            VREG_VOLTAGE_1_20, 1200 },
    { "1.30 V (spec max)", VREG_VOLTAGE_1_30, 1300 },
    { "1.35 V",            VREG_VOLTAGE_1_35, 1350 },
    { "1.40 V",            VREG_VOLTAGE_1_40, 1400 },
    { "1.50 V",            VREG_VOLTAGE_1_50, 1500 },
    { "1.60 V (ceiling)",  VREG_VOLTAGE_1_60, 1600 },
};
#define N_VOLTS ((int)(sizeof(k_volts) / sizeof(k_volts[0])))

static uint32_t prompt_uint(const char *label, uint32_t def, uint32_t lo, uint32_t hi)
{
    printf("\n %s [%lu-%lu, Enter for %lu]: ",
           label, (unsigned long)lo, (unsigned long)hi, (unsigned long)def);
    fflush(stdout);
    uint32_t v = 0; bool any = false;
    while (true) {
        int c = getchar();
        if (c == '\r' || c == '\n') break;
        if (c < '0' || c > '9') continue;
        if (v > (hi / 10) + 1) continue;
        v = v * 10 + (uint32_t)(c - '0'); any = true;
        printf("%c", c); fflush(stdout);
    }
    if (!any) v = def;
    if (v < lo) v = lo;
    if (v > hi) v = hi;
    printf("  -> %lu\n", (unsigned long)v);
    return v;
}

/* Reads a decimal choice terminated by Enter. It must accumulate digits rather
 * than act on the first keypress: with twelve voltage options, a single-character
 * read makes "12" unreachable because the "1" commits immediately. */
static int prompt_choice(const char *label, const char *const *opts, int n, int def)
{
    printf("\n %s\n", label);
    for (int i = 0; i < n; i++) printf("   %2d) %s\n", i + 1, opts[i]);
    printf(" Choose [1-%d], then Enter (Enter alone = %d): ", n, def + 1);
    fflush(stdout);

    uint32_t v = 0;
    bool any = false;
    while (true) {
        int c = getchar();
        if (c == '\r' || c == '\n') break;
        if (c == 0x7f || c == '\b') {                  /* backspace */
            if (any) {
                v /= 10;
                if (!v) any = false;
                printf("\b \b");
                fflush(stdout);
            }
            continue;
        }
        if (c < '0' || c > '9') continue;
        if (v > 99u) continue;                          /* no runaway input */
        v = v * 10u + (uint32_t)(c - '0');
        any = true;
        printf("%c", c);
        fflush(stdout);
    }

    int sel = any ? (int)v - 1 : def;
    if (sel < 0 || sel >= n) sel = def;
    printf("  -> %d) %s\n", sel + 1, opts[sel]);
    return sel;
}

/* ---- RXDELAY selection -------------------------------------------------- */

/* modes */
#define RXD_KEEP 0
#define RXD_AUTO 1
#define RXD_SCAN 2
#define RXD_FORCE 3

static int      g_rxd_mode;
static uint32_t g_rxd_force;
static uint32_t g_rxd_boot;

static bool journal_config_valid(const progress_t *p)
{
    if (p->vsel >= N_VOLTS || p->ladder_mode > 1 || p->rxd_mode > RXD_FORCE)
        return false;
    if (p->rxd_mode == RXD_FORCE && p->rxd_force > 7)
        return false;
    if (p->start_mhz < 18 || p->start_mhz > 600)
        return false;
    return p->max_mhz >= p->start_mhz && p->max_mhz <= 800;
}

/* Configuration fields are invariants of a run, but g_prog lives in RAM and
 * is zeroed by every reset. Reassert all of them before a record can be saved,
 * including after watchdog recovery. */
static void journal_config_set(progress_t *p, int vsel, int ladder_mode,
                               uint32_t start_mhz, uint32_t max_mhz)
{
    p->vsel        = (uint8_t)vsel;
    p->ladder_mode = (uint8_t)ladder_mode;
    p->rxd_mode    = (uint8_t)g_rxd_mode;
    p->rxd_force   = (uint16_t)g_rxd_force;
    p->start_mhz   = (uint16_t)start_mhz;
    p->max_mhz     = (uint16_t)max_mhz;
}

/* Auto: one extra delay cycle per ~50 MHz of SCK, which is the usual shape of
 * the requirement. Clamped to the 3-bit field. */
static uint32_t rxd_auto_for(uint32_t sck_khz)
{
    uint32_t r = 1u + (sck_khz / 50000u);
    return r > 7u ? 7u : r;
}

/* Scan: try every RXDELAY at this divider and see which ones read flash
 * correctly. Runs entirely from SRAM, so a wrong value corrupts data but
 * cannot stop instruction fetch. Returns the chosen value and the passing
 * range; chooses the middle of the widest passing run for margin. */
static uint32_t rxd_scan(uint32_t div, uint32_t *lo_out, uint32_t *hi_out, bool *ok)
{
    bool pass[8];
    for (uint32_t r = 0; r < 8; r++)
        pass[r] = (flash_crc_at(div, r) == ref_flash_crc);

    uint32_t best_lo = 0, best_hi = 0, best_len = 0;
    for (uint32_t r = 0; r < 8; r++) {
        if (!pass[r]) continue;
        uint32_t e = r;
        while (e + 1 < 8 && pass[e + 1]) e++;
        if (e - r + 1 > best_len) { best_len = e - r + 1; best_lo = r; best_hi = e; }
        r = e;
    }
    *ok = best_len > 0;
    *lo_out = best_lo; *hi_out = best_hi;
    return best_lo + (best_len ? (best_len - 1) / 2 : 0);
}

static uint32_t rxd_for(uint32_t div, uint32_t sck_khz, char *note, size_t n)
{
    switch (g_rxd_mode) {
    case RXD_FORCE: snprintf(note, n, "%lu", (unsigned long)g_rxd_force); return g_rxd_force;
    case RXD_AUTO: {
        uint32_t r = rxd_auto_for(sck_khz);
        snprintf(note, n, "%lu", (unsigned long)r);
        return r;
    }
    case RXD_SCAN: {
        uint32_t lo, hi; bool ok;
        uint32_t r = rxd_scan(div, &lo, &hi, &ok);
        if (!ok) { snprintf(note, n, "none!"); return g_rxd_boot; }
        snprintf(note, n, "%lu[%lu-%lu]", (unsigned long)r, (unsigned long)lo, (unsigned long)hi);
        return r;
    }
    default: snprintf(note, n, "%lu", (unsigned long)g_rxd_boot); return g_rxd_boot;
    }
}

/* ops per second from microseconds per operation, two decimals, no floats. */
static void print_rate(uint64_t us)
{
    if (!us) { printf("        -   "); return; }
    uint64_t r100 = 100000000ull / us;
    printf(" %8llu.%02llu", r100 / 100, r100 % 100);
}

/* ---- one suite ---------------------------------------------------------- */

static void run_suite(int idx, uint32_t max_khz)
{
    const suite_t *s = &k_suites[idx];

    printf("\n---------------------------------------------------------------------------------\n");
    printf(" SUITE %d/%d: %s   (timed: %s)\n", idx + 1, MAX_SUITES, s->name, s->timed_label);
    printf("---------------------------------------------------------------------------------\n");
    printf("  sysMHz div rxd    SCK  VSYS      1c cyc/op     1c ops/s  rel  XIPhit"
           "     2c cyc/op     2c ops/s  gain  res\n");

    uint16_t best = 0;
    uint64_t base_cyc = 0;

    for (int i = 0; i < g_nsteps; i++) {
        uint32_t target = g_steps[i];
        if (target > max_khz) break;

        uint32_t sck_khz;
        uint32_t div = qmi_div_for(target, &sck_khz);
        char rxnote[16];
        uint32_t rxd = rxd_for(div, sck_khz, rxnote, sizeof(rxnote));

        printf("  %6lu %3lu %-6s %5lu",
               (unsigned long)(target / 1000), (unsigned long)div, rxnote,
               (unsigned long)(sck_khz / 1000));
        fflush(stdout);

        pos_set(idx, target / 1000, max_khz / 1000, g_vsel);

        /* Journal the attempt to flash before touching the clock, and do it at
         * the sweep floor: programming flash while overclocked is exactly how
         * you turn a hung sweep into a corrupted one. The bootrom's flash
         * routines also reset the QMI timing, so the qmi_apply below must come
         * afterwards -- which it does. */
        if (g_journal) {
            goto_safe_clock(g_rxd_boot);
            g_prog.suite   = (uint8_t)idx;
            g_prog.cur_mhz = (uint16_t)(target / 1000);
            for (int i = 0; i < MAX_SUITES && i < PROGRESS_MAX_SUITES; i++)
                g_prog.result[i] = result_get(i);
            g_prog.seq++;
            progress_save(&g_prog);
        }

        /* Work per step is roughly constant in cycles, so wall time scales as
         * 1/f: at 75 MHz a step already needs more than the watchdog's ~8 s
         * ceiling. Underclocking cannot hang the part anyway, so the watchdog
         * guards the overclock direction only. */
        bool use_wdog = (target >= SWEEP_START_KHZ);   /* overclock direction only */
        if (use_wdog) watchdog_enable(WATCHDOG_MS, true);
        else          hw_clear_bits(&watchdog_hw->ctrl, WATCHDOG_CTRL_ENABLE_BITS);

        qmi_apply(div, rxd);
        if (!set_sys_clock_khz(target, false)) {
            printf("   set_sys_clock refused\n"); fflush(stdout);
            continue;
        }
        repin_peri();
        if (use_wdog) watchdog_update();

        uint16_t vs = bench_vsys_mv();
        printf(" %u.%02uV", vs / 1000, (vs % 1000) / 10);
        fflush(stdout);

        uint64_t mhz_pre = target / 1000u;

        char err[32] = {0};
        if (!s->check(err, sizeof(err))) {
            printf("   FAIL cold (%s)\n", err); fflush(stdout);
            break;
        }
        if (use_wdog) watchdog_update();

        uint32_t hit = 0, acc = 0;
        uint64_t us1 = 0;

        if (idx == SUITE_MLKEM) {
            /* Three key lengths, measured and reported separately. */
            printf("\n");
            for (int lv = 0; lv < 3; lv++) {
                mlk_cur = lv;
                if (mlk_call_on_core1(mlk_digest)) {   /* refresh pk/sk at this level */
                    printf("        %-12s setup failed\n", mlk_variants[lv].name);
                    continue;
                }
                bench_xip_reset();
                uint64_t u = mlk_time_on_core1(mlk_enc_cur, TIMED_US);
                uint32_t h, a;
                bench_xip_read(&h, &a);
                uint64_t c = u * mhz_pre;
                printf("        %-12s encaps %8llu.%02lluk", mlk_variants[lv].name,
                       c / 1000, (c % 1000) / 10);
                print_rate(u);
                printf(" ops/s   %3lu.%01lu%%\n",
                       (unsigned long)(bench_xip_hit_permille(h, a) / 10),
                       (unsigned long)(bench_xip_hit_permille(h, a) % 10));
                fflush(stdout);
                if (use_wdog) watchdog_update();
                if (lv == 1) { us1 = u; hit = h; acc = a; }   /* 768 drives rel */
            }
            printf("       ");
        } else if (idx == SUITE_TRNG) {
            bench_xip_reset();
            trng_bench_start(false, 0);          /* raw: all hardware tests bypassed */
            us1 = time_single(tm_trng, &hit, &acc);
            {
                uint64_t bps = us1 ? (uint64_t)sizeof(g_trng_buf) * 1000000ull / us1 : 0;
                printf("  [%llu.%02llu kB/s raw]", bps / 1000, (bps % 1000) / 10);
            }
            fflush(stdout);
        } else if (idx == SUITE_MLDSA) {
            /* Private-key operations, per level: deterministic keygen and sign. */
            printf("\n");
            bench_xip_reset();
            for (int lv = 0; lv < 3; lv++) {
                mld_cur = lv;
                if (mlk_call_on_core1(mld_keygen_cur)) {
                    printf("        %-12s setup failed\n", mld_variants[lv].name);
                    continue;
                }
                uint64_t ug = mlk_time_on_core1(mld_keygen_cur, TIMED_US);
                if (use_wdog) watchdog_update();
                uint64_t us_ = mlk_time_on_core1(mld_sign_cur, TIMED_US);
                if (use_wdog) watchdog_update();
                uint64_t cg = ug * mhz_pre, cs = us_ * mhz_pre;
                printf("        %-12s keygen %8llu.%02lluk", mld_variants[lv].name,
                       cg / 1000, (cg % 1000) / 10);
                print_rate(ug);
                printf(" ops/s   sign %8llu.%02lluk", cs / 1000, (cs % 1000) / 10);
                print_rate(us_);
                printf(" ops/s\n");
                fflush(stdout);
                if (lv == 1) us1 = us_;            /* 65 sign drives rel */
            }
            bench_xip_read(&hit, &acc);
            printf("       ");
        } else {
            us1 = time_single(s->timed, &hit, &acc);
        }
        watchdog_update();

        uint64_t mhz  = target / 1000u;
        uint64_t cyc1 = us1 * mhz;
        if (!base_cyc) base_cyc = cyc1;
        uint32_t rel  = (uint32_t)((cyc1 * 100u) / base_cyc);
        uint32_t hp   = bench_xip_hit_permille(hit, acc);

        printf("  %8llu.%02lluk", cyc1 / 1000, (cyc1 % 1000) / 10);
        print_rate(us1);
        printf(" %u.%02ux %3lu.%01lu%%", rel / 100, rel % 100,
               (unsigned long)(hp / 10), (unsigned long)(hp % 10));
        fflush(stdout);

        bool dual_failed = false;
        uint32_t dual_gain = 0, dual_c1_ops = 0;
        bool dual_c1_completed = false;
        if (s->dual_safe) {
            uint32_t tot = 0;
            uint64_t us2 = time_dual(s->timed, &tot, &dual_c1_ops,
                                     &dual_c1_completed);
            uint64_t cyc2 = us2 * mhz;
            uint32_t gain = cyc2 ? (uint32_t)((cyc1 * 100u) / cyc2) : 0;
            dual_gain = gain;
            dual_failed = !dual_c1_completed || !dual_c1_ops || !us2 ||
                          gain <= DUAL_MIN_GAIN_X100;
            printf("  %8llu.%02lluk", cyc2 / 1000, (cyc2 % 1000) / 10);
            print_rate(us2);
            printf(" %u.%02ux", gain / 100, gain % 100);
        } else {
            printf("  %14s %13s %5s", "single-core", "-", "-");
        }
        fflush(stdout);
        if (use_wdog) watchdog_update();

        if (dual_failed) {
            printf("  FAIL dual (core1_done=%u core1_ops=%lu gain=%u.%02ux; need >1.05x)\n",
                   dual_c1_completed ? 1u : 0u, (unsigned long)dual_c1_ops,
                   dual_gain / 100, dual_gain % 100);
            printf("\n  %lu MHz failed dual-core liveness/scaling.\n",
                   (unsigned long)mhz);
            fflush(stdout);
            break;
        }

        if (!s->check(err, sizeof(err))) {
            printf("  FAIL hot (%s)\n", err);
            printf("\n  %lu MHz passed cold and failed under load -- marginal.\n",
                   (unsigned long)mhz);
            fflush(stdout);
            break;
        }

        printf("  PASS\n"); fflush(stdout);
        best = (uint16_t)mhz;
        result_set(idx, best);
    }

    goto_safe_clock(g_rxd_boot);
    hw_clear_bits(&watchdog_hw->ctrl, WATCHDOG_CTRL_ENABLE_BITS);

    if (best) printf("\n  %s: highest passing %u MHz\n", s->name, best);
    else      printf("\n  %s: no passing frequency\n", s->name);

    printf("  RUNNING TOTALS:");
    for (int i = 0; i <= idx; i++) printf("  %s=%u", k_suites[i].name, result_get(i));
    printf("\n"); fflush(stdout);
}

/* ---- setup -------------------------------------------------------------- */

/* Returns 0 on success, or a step number so a failure names itself. */
static int setup_all(void)
{
    stage("seeding DRBGs");
    mbedtls_entropy_init(&g_entropy);
    for (int c = 0; c < NCORES; c++) {
        mbedtls_ctr_drbg_init(&g_drbg[c]);
        const char *pers = c ? "sweep-c1" : "sweep-c0";
        if (mbedtls_ctr_drbg_seed(&g_drbg[c], mbedtls_entropy_func, &g_entropy,
                                  (const unsigned char *)pers, 8)) return 1;
    }
    uECC_set_rng(uecc_rng);

    for (int i = 0; i < BULK_BYTES; i++) g_buf[i] = (uint8_t)i;
    for (int i = 0; i < 32; i++) g_msg[i] = (uint8_t)(0xA0 + i);

    for (int c = 0; c < NCORES; c++) {
        mbedtls_ecdsa_init(&g_mb_ec[c]);
        if (mbedtls_ecdsa_genkey(&g_mb_ec[c], MBEDTLS_ECP_DP_SECP256R1, RNG0)) return 2;
    }
    stage("Mbed TLS keys");
    mbedtls_ecp_group_init(&g_mb_grp);
    mbedtls_mpi_init(&g_mb_d);
    mbedtls_ecp_point_init(&g_mb_Q);
    mbedtls_ecp_point_init(&g_mb_R);
    if (mbedtls_ecp_group_load(&g_mb_grp, MBEDTLS_ECP_DP_SECP256R1)) return 3;
    if (mbedtls_ecp_gen_keypair(&g_mb_grp, &g_mb_d, &g_mb_Q, RNG0))  return 4;

    {
        mbedtls_mpi N, P, Q, D, E;
        mbedtls_mpi_init(&N); mbedtls_mpi_init(&P); mbedtls_mpi_init(&Q);
        mbedtls_mpi_init(&D); mbedtls_mpi_init(&E);
        mbedtls_rsa_init(&g_rsa);
        mbedtls_mpi_read_string(&N, 16, RSA_N);
        mbedtls_mpi_read_string(&P, 16, RSA_P);
        mbedtls_mpi_read_string(&Q, 16, RSA_Q);
        mbedtls_mpi_read_string(&D, 16, RSA_D);
        mbedtls_mpi_read_string(&E, 16, RSA_E);
        if (mbedtls_rsa_import(&g_rsa, &N, &P, &Q, &D, &E)) return 5;
        if (mbedtls_rsa_complete(&g_rsa))                   return 6;
        mbedtls_rsa_set_padding(&g_rsa, MBEDTLS_RSA_PKCS_V15, MBEDTLS_MD_SHA256);
    }
    stage("p256-m keys");
    {
        uint8_t b_priv[32];
        if (p256_gen_keypair(g_pm_a_priv, g_pm_a_pub)) return 7;
        if (p256_gen_keypair(b_priv, g_pm_b_pub))      return 8;
    }
    stage("micro-ecc keys");
    {
        uint8_t b_priv[32];
        if (!uECC_make_key(g_ue_a_pub, g_ue_a_priv, uECC_secp256r1())) return 9;
        if (!uECC_make_key(g_ue_b_pub, b_priv, uECC_secp256r1()))      return 10;
    }
    {
        uint8_t seed[32];
        for (int i = 0; i < 32; i++) seed[i] = (uint8_t)i;
        crypto_ed25519_key_pair(g_ed_sk, g_ed_pk, seed);
    }

    mbedtls_sha256(g_buf, BULK_BYTES, ref_sha, 0);
    {
        size_t olen = 0;
        if (mbedtls_ecp_mul(&g_mb_grp, &g_mb_R, &g_mb_d, &g_mb_Q, RNG0)) return 11;
        if (mbedtls_ecp_point_write_binary(&g_mb_grp, &g_mb_R, MBEDTLS_ECP_PF_UNCOMPRESSED,
                                           &olen, ref_mb_point, sizeof(ref_mb_point))) return 12;
    }
    mbedtls_rsa_pkcs1_sign(&g_rsa, RNG0, MBEDTLS_MD_SHA256, 32, ref_sha, ref_rsa_sig);
    p256_ecdh_shared_secret(ref_pm_ecdh, g_pm_a_priv, g_pm_b_pub);
    uECC_shared_secret(g_ue_b_pub, g_ue_a_priv, ref_ue_ecdh, uECC_secp256r1());
    crypto_x25519(ref_x25519, k_x_priv, k_x_pub);
    crypto_ed25519_sign(ref_ed_sig, g_ed_sk, g_msg, 32);
    crypto_blake2b(ref_blake, 64, g_buf, BULK_BYTES);
    ref_torture = torture(150000);
    ref_ram     = ram_march();
    stage("flash CRC reference");
    ref_flash_crc = flash_crc();      /* at the safe clock and bootrom timing */

    stage("ML-KEM references (core 1)");
    for (int i = 0; i < 3; i++) {
        mlk_cur = i;
        int rc = mlk_call_on_core1(mlk_digest);
        if (rc) { printf(" [ML-KEM %s rc=%d core1_dead=%d]\n",
                         mlk_variants[i].name, rc, (int)mlk_runner_core1_dead);
                  return 20 + i; }
    }
    mlk_cur = 1;
    if (mlk_call_on_core1(mlk_digest)) return 23;

    stage("ML-DSA references (core 1)");
    for (int i = 0; i < 3; i++) {
        mld_cur = i;
        int rc = mlk_call_on_core1(mld_digest);
        if (rc) { printf(" [ML-DSA %s rc=%d core1_dead=%d]\n",
                         mld_variants[i].name, rc, (int)mlk_runner_core1_dead);
                  return 30 + i; }
    }
    mld_cur = 1;
    if (mlk_call_on_core1(mld_digest)) return 33;

    stage("done");
    return 0;                       /* 0 = success; anything else is a step */
}

/* ---- main --------------------------------------------------------------- */

int main(void)
{
    set_sys_clock_khz(SWEEP_START_KHZ, false);
    stdio_init_all();
    repin_peri();
    adc_init();

    g_rxd_boot = bench_qmi_rxdelay();

#if LIB_PICO_STDIO_USB
    for (int i = 0; i < 120 && !stdio_usb_connected(); i++) sleep_ms(100);
    sleep_ms(300);
#endif

    bool resuming = false;          /* watchdog reset: state from scratch regs */
    bool resuming_flash = false;    /* power cycle: state from the flash journal */
    bool resuming_config = false;   /* watchdog state matched a valid journal */
    int  hung_suite = -1;
    uint32_t hung_mhz = 0, max_mhz = SWEEP_MAX_KHZ / 1000u, start_mhz = SWEEP_START_KHZ / 1000u;
    int vsel = -1, ladder_mode = 0;
    bool have_journal = progress_load(&g_prog);
    bool journal_ok = have_journal && journal_config_valid(&g_prog);

    if (watchdog_caused_reboot() && watchdog_hw->scratch[SC_MAGIC] == MAGIC) {
        uint32_t w = watchdog_hw->scratch[SC_POS];
        resuming   = true;
        hung_mhz   = w & 0x3ffu;
        max_mhz    = (w >> 10) & 0x3ffu;
        vsel       = (int)((w >> 20) & 0xfu);
        hung_suite = (int)((w >> 24) & 0xfu);

        /* run_suite writes the journal immediately after SC_POS and before it
         * touches the clock, so an exact match is the same attempted step. */
        if (journal_ok && !g_prog.complete && g_prog.suite == hung_suite &&
            g_prog.cur_mhz == hung_mhz && g_prog.vsel == vsel &&
            g_prog.max_mhz == max_mhz) {
            start_mhz   = g_prog.start_mhz;
            ladder_mode = g_prog.ladder_mode;
            g_rxd_mode  = g_prog.rxd_mode;
            g_rxd_force = g_prog.rxd_force;
            for (int i = 0; i < MAX_SUITES && i < PROGRESS_MAX_SUITES; i++) {
                g_result[i] = g_prog.result[i];
                g_result_approx[i] = false;
            }
            resuming_config = true;
        }
    }

    bench_print_build_info("crypto_sweep");
    bench_print_runtime_state();
    printf(" journal   : flash sector at the end of the device, survives power loss\n");
    printf(" sweep     : %d suites | SCK window %u-%u MHz | wdog %u ms | 1-core + 2-core\n",
           MAX_SUITES, QMI_SCK_MIN_MHZ, QMI_SCK_MAX_MHZ, WATCHDOG_MS);
    printf("=====================================================================\n");

    if (resuming) {
        printf("\n *** HUNG: suite %d (%s) at %lu MHz -- watchdog reset.\n",
               hung_suite + 1,
               (hung_suite >= 0 && hung_suite < MAX_SUITES) ? k_suites[hung_suite].name : "?",
               (unsigned long)hung_mhz);
        printf(" *** That suite ends there; continuing with the next one.\n");
        if (resuming_config) {
            printf(" *** Voltage, range, ladder, and RXDELAY restored from flash journal.\n");
        } else {
            results_load_from_scratch();
            printf(" *** No matching valid journal; RXDELAY/ladder use safe defaults.\n");
        }
    } else if (have_journal && journal_ok && !g_prog.complete &&
               g_prog.suite < MAX_SUITES) {
        /* Survived a power cycle after a hang. The record identifies the suite
         * and frequency that were active, so end that suite there and continue
         * with the next one instead of repeating the same unstable work. */
        printf("\n *** Unfinished sweep found in flash.\n");
        printf("     stopped in suite %d (%s) at %u MHz\n",
               g_prog.suite + 1, k_suites[g_prog.suite].name, g_prog.cur_mhz);
        printf("     settings: %s, %u-%u MHz, ladder %u, RXDELAY mode %u\n",
               k_volts[g_prog.vsel < N_VOLTS ? g_prog.vsel : 0].label,
               g_prog.start_mhz, g_prog.max_mhz, g_prog.ladder_mode, g_prog.rxd_mode);
        printf("     results so far:");
        for (int i = 0; i < MAX_SUITES; i++)
            printf("  %s=%u", k_suites[i].name, g_prog.result[i]);
        printf("\n");

        static const char *const res_opts[] = {
            "resume: end the recorded suite at that step, continue with the next",
            "start over: discard the saved run and ask again",
        };
        if (prompt_choice("Unfinished run:", res_opts, 2, 0) == 0) {
            resuming_flash = true;
            hung_suite  = g_prog.suite;
            hung_mhz    = g_prog.cur_mhz;
            vsel        = g_prog.vsel;
            start_mhz   = g_prog.start_mhz;
            max_mhz     = g_prog.max_mhz;
            ladder_mode = g_prog.ladder_mode;
            g_rxd_mode  = g_prog.rxd_mode;
            g_rxd_force = g_prog.rxd_force;
            for (int i = 0; i < MAX_SUITES && i < PROGRESS_MAX_SUITES; i++)
                g_result[i] = g_prog.result[i];
            printf("\n Resuming after suite %d.\n", hung_suite + 1);
            fflush(stdout);
        } else {
            progress_erase();
            memset(&g_prog, 0, sizeof(g_prog));
        }
    } else if (have_journal && !g_prog.complete && g_prog.suite < MAX_SUITES) {
        printf("\n *** Saved sweep has invalid settings (%u-%u MHz, voltage index %u).\n",
               g_prog.start_mhz, g_prog.max_mhz, g_prog.vsel);
        printf(" *** Refusing unsafe resume; starting a new run.\n");
    }

    if (!resuming && !resuming_flash) {
        static const char *volt_opts[N_VOLTS];
        for (int i = 0; i < N_VOLTS; i++) volt_opts[i] = k_volts[i].label;
        vsel = prompt_choice("Core voltage "
                             "(below 1.10 V undervolts; above 1.30 V is outside the datasheet):",
                             volt_opts, N_VOLTS, 5);

        static const char *lad_opts[] = {
            "standard 50/20/4 ladder",
            "divider-aligned: only steps whose flash SCK lands in the window",
        };
        ladder_mode = prompt_choice("Frequency ladder:", lad_opts, 2, 0);

        static const char *rxd_opts[] = {
            "keep the bootrom RXDELAY",
            "auto: scale RXDELAY with SCK",
            "scan: test all 8 values per step, pick the safest",
            "force a fixed value",
        };
        g_rxd_mode = prompt_choice("Flash RXDELAY:", rxd_opts, 4, 0);
        if (g_rxd_mode == RXD_FORCE) g_rxd_force = prompt_uint("RXDELAY value", 2, 0, 7);

        start_mhz = prompt_uint("Start clock, MHz (18 = PLL floor; below 150 underclocks)",
                                SWEEP_START_KHZ / 1000u, 18, 600);
        max_mhz   = prompt_uint("MAXIMUM clock, MHz (hard ceiling)",
                                SWEEP_MAX_KHZ / 1000u, start_mhz, 800);
        for (int i = 0; i < MAX_SUITES; i++) result_set(i, 0);

        memset(&g_prog, 0, sizeof(g_prog));
        progress_erase();
    }
    if (vsel < 0 || vsel >= N_VOLTS) vsel = 0;
    if (max_mhz < start_mhz) max_mhz = start_mhz;
    g_vsel = vsel;
    journal_config_set(&g_prog, vsel, ladder_mode, start_mhz, max_mhz);

    /* Order matters when undervolting: get the clock down first, then lower the
     * voltage. Doing it the other way round runs 150 MHz at 0.85 V, which will
     * not survive. */
    g_floor_khz = (start_mhz * 1000u < SWEEP_START_KHZ) ? start_mhz * 1000u : SWEEP_START_KHZ;
    if (g_floor_khz != SWEEP_START_KHZ) {
        uint32_t t;
        if (nearest_achievable(g_floor_khz, 6000, &t)) g_floor_khz = t;
        uint32_t sck;
        qmi_apply(qmi_div_for(g_floor_khz, &sck), g_rxd_boot);
        set_sys_clock_khz(g_floor_khz, false);
        repin_peri();
        sleep_ms(5);
    }

    if (k_volts[vsel].mv > 1300) vreg_disable_voltage_limit();
    vreg_set_voltage(k_volts[vsel].v);
    sleep_ms(20);

    printf(" Applying %s at %lu MHz, then capturing references...\n",
           k_volts[vsel].label, (unsigned long)(g_floor_khz / 1000u));
    fflush(stdout);

    watchdog_hw->scratch[SC_MAGIC] = MAGIC;
    pos_set(0, start_mhz, max_mhz, vsel);

    {
        static const char *const step_name[] = {
            "ok", "CTR_DRBG seed", "Mbed TLS ECDSA keygen", "ECP group load",
            "ECP keypair", "RSA import", "RSA complete",
            "p256-m keypair A", "p256-m keypair B",
            "micro-ecc keypair A", "micro-ecc keypair B",
            "ECP mul reference", "ECP point encode" };
        int rc = setup_all();
        if (rc != 0) {
            const char *what = "?";
            if (rc >= 1 && rc <= 12)      what = step_name[rc];
            else if (rc >= 20 && rc < 24) what = "ML-KEM reference (core 1)";
            else if (rc >= 30 && rc < 34) what = "ML-DSA reference (core 1)";
            printf("\n SETUP FAILED at step %d: %s   core1_dead=%d\n",
                   rc, what, (int)mlk_runner_core1_dead);
            while (true) { sleep_ms(3000); printf(" setup failed: step %d (%s)\n", rc, what); }
        }
    }

    ladder_build(start_mhz, max_mhz, ladder_mode);

    printf("\n Core voltage: %s\n", k_volts[vsel].label);
    printf(" Ladder: %s, %d steps from %lu to %lu MHz\n",
           ladder_mode ? "divider-aligned" : "standard",
           g_nsteps, (unsigned long)start_mhz, (unsigned long)max_mhz);
    printf(" Flash RXDELAY mode: %d (bootrom value was %lu)\n",
           g_rxd_mode, (unsigned long)g_rxd_boot);
    if (start_mhz < SWEEP_START_KHZ / 1000u)
        printf(" Underclock rungs below 150 MHz: 18/24/36/48/60/75/100/125 MHz.\n"
               " Watchdog is off below 150 MHz -- a step there can take far longer\n"
               " than its ~8 s ceiling, and underclocking cannot hang the part.\n");
    bench_print_state_line("  REF-STATE");
    printf(" reference flash CRC32 = 0x%08lx over %lu KiB\n",
           (unsigned long)ref_flash_crc, (unsigned long)(FLASH_CRC_BYTES / 1024));
    fflush(stdout);

    /* Both reset paths identify the suite that hung. Move past it so an
     * unstable frequency does not produce an endless reset/reinsert loop. */
    int first = (resuming || resuming_flash) ? hung_suite + 1 : 0;
    for (int i = first; i < MAX_SUITES; i++) run_suite(i, max_mhz * 1000u);

    printf("\n=====================================================================\n");
    printf(" SUMMARY -- %s, %lu-%lu MHz, firmware v%s\n",
           k_volts[vsel].label, (unsigned long)start_mhz, (unsigned long)max_mhz,
           BENCH_VERSION_STRING);
    printf("=====================================================================\n");
    printf("   %-22s  highest passing\n", "suite");
    for (int i = 0; i < MAX_SUITES; i++) {
        uint16_t r = result_get(i);
        if (r) printf("   %-22s  %s%u MHz\n", k_suites[i].name,
                      g_result_approx[i] ? "~" : "", r);
        else   printf("   %-22s  none / not run\n", k_suites[i].name);
    }
    printf("\n A suite reading exactly %lu MHz hit the cap, not a limit.\n",
           (unsigned long)max_mhz);
    printf(" SHA-256 hardware has one engine, and ML-KEM needs a 48 KiB stack on\n"
           " core 1, so neither has a dual-core figure.\n");
    printf(" Below 150 MHz the flash SCK window is unreachable at any divider, so\n"
           " the divider sits at its floor of 2 and no * marker is shown.\n");
    printf(" ML-KEM and ML-DSA report each parameter set separately; their rel\n"
           " columns follow the 768 and 65 rows. ML-DSA shows the two private-key\n"
           " operations, deterministic key generation and signing.\n");
    printf(" cyc/op is in thousands of cycles. rel is against this suite's first\n"
           " step, so >1.00 means the operation costs more cycles at higher clock\n"
           " -- that is the flash divider, not the core. gain is 1-core cyc/op\n"
           " divided by 2-core cyc/op, where 2.00 would be perfect scaling.\n");
    printf(" Dual-safe steps fail if core 1 does not finish work or gain is <=1.05x.\n");

    watchdog_hw->scratch[SC_MAGIC] = 0;
    if (g_journal) {
        goto_safe_clock(g_rxd_boot);
        for (int i = 0; i < MAX_SUITES && i < PROGRESS_MAX_SUITES; i++)
            g_prog.result[i] = result_get(i);
        g_prog.complete = 1;
        g_prog.seq++;
        progress_save(&g_prog);
        printf(" Run marked complete in flash; the next boot will start fresh.\n");
    }
    while (true) tight_loop_contents();
}
