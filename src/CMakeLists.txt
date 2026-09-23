cmake_minimum_required(VERSION 3.13)

if (NOT PICO_BOARD)
    set(PICO_BOARD pico2)
endif()

# Raspberry Pi Pico 2 has 4 MiB of QSPI flash.  Declare this explicitly because
# crypto_sweep journals progress in the final sector.
set(PICO_FLASH_SIZE_BYTES 4194304)

set(PICO_MBEDTLS_CONFIG_FILE "bench_mbedtls_config.h")

include(pico_sdk_import.cmake)

project(crypto_shootout C CXX ASM)
set(CMAKE_C_STANDARD 11)

pico_sdk_init()


# ---- build identification --------------------------------------------------
string(TIMESTAMP BUILD_STAMP "%Y%m%d-%H%M%S" UTC)
string(MD5 _bid "${BUILD_STAMP}-${PICO_PLATFORM}-${CMAKE_C_COMPILER}")
string(SUBSTRING "${_bid}" 0 8 BUILD_ID)

if (NOT DEFINED BENCH_SOURCE_ID)
    set(BENCH_SOURCE_ID "unknown")
endif()
set(BUILD_OPTIONS "source=${BENCH_SOURCE_ID} NIST_OPTIM=1")
if (BENCH_NO_MBEDTLS_ASM)
    string(APPEND BUILD_OPTIONS " MBEDTLS_ASM=0")
else()
    string(APPEND BUILD_OPTIONS " MBEDTLS_ASM=1")
endif()
if (PICO_PLATFORM MATCHES "riscv")
    string(APPEND BUILD_OPTIONS " uECC=C")
elseif(UECC_NO_ASM)
    string(APPEND BUILD_OPTIONS " uECC=C(asm-disabled)")
elseif(UECC_NO_UMAAL)
    string(APPEND BUILD_OPTIONS " uECC=asm(no-UMAAL)")
else()
    string(APPEND BUILD_OPTIONS " uECC=asm/UMAAL")
endif()
string(APPEND BUILD_OPTIONS " board=${PICO_BOARD} platform=${PICO_PLATFORM}")

configure_file(${CMAKE_CURRENT_LIST_DIR}/build_info.h.in
               ${CMAKE_CURRENT_BINARY_DIR}/generated/build_info.h @ONLY)


# ---- ML-KEM (mlkem-native, monolithic multi-level build) -------------------
set(MLKEM_SRC
    ${CMAKE_CURRENT_LIST_DIR}/lib/mlkem/mlkem_native_all.c
    ${CMAKE_CURRENT_LIST_DIR}/lib/mlkem/mlkem_bench.c
    ${CMAKE_CURRENT_LIST_DIR}/mlk_runner.c
    ${CMAKE_CURRENT_LIST_DIR}/trng_bench.c
    ${CMAKE_CURRENT_LIST_DIR}/progress.c)
set(MLKEM_INC
    ${CMAKE_CURRENT_LIST_DIR}/lib/mlkem
    ${CMAKE_CURRENT_LIST_DIR}/lib/mlkem/mlkem_native)

# ---- ML-DSA (mldsa-native, monolithic multi-level, reduced-RAM mode) -------
list(APPEND MLKEM_SRC
    ${CMAKE_CURRENT_LIST_DIR}/lib/mldsa/mldsa_native_all.c
    ${CMAKE_CURRENT_LIST_DIR}/lib/mldsa/mldsa_bench.c)
list(APPEND MLKEM_INC
    ${CMAKE_CURRENT_LIST_DIR}/lib/mldsa
    ${CMAKE_CURRENT_LIST_DIR}/lib/mldsa/mldsa_native)

add_executable(crypto_shootout
    main.c
    binfo.c
    ${MLKEM_SRC}
    lib/p256-m/p256-m.c
    lib/micro-ecc/uECC.c
    lib/monocypher/monocypher.c
    lib/monocypher/monocypher-ed25519.c
)

target_include_directories(crypto_shootout PRIVATE
    ${MLKEM_INC}
    ${CMAKE_CURRENT_BINARY_DIR}/generated
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/lib/p256-m
    ${CMAKE_CURRENT_LIST_DIR}/lib/micro-ecc
    ${CMAKE_CURRENT_LIST_DIR}/lib/monocypher
)

# p256-m.c as shipped inside Mbed TLS is wrapped in the PSA driver guard.
# Define it for that file only, so the rest of Mbed TLS is unaffected.
set_source_files_properties(lib/p256-m/p256-m.c PROPERTIES
    COMPILE_DEFINITIONS "MBEDTLS_PSA_P256M_DRIVER_ENABLED")

# micro-ecc: only P-256, and use the Cortex-M assembly (incl. UMAAL) on Arm.
set(UECC_COMMON
    uECC_SUPPORTS_secp160r1=0
    uECC_SUPPORTS_secp192r1=0
    uECC_SUPPORTS_secp224r1=0
    uECC_SUPPORTS_secp256r1=1
    uECC_SUPPORTS_secp256k1=0
    uECC_SUPPORT_COMPRESSED_POINT=0
    uECC_SQUARE_FUNC=1
)
if (PICO_PLATFORM MATCHES "riscv")
    # uECC_arch_other: portable C, no assembly available for RV32.
    target_compile_definitions(crypto_shootout PRIVATE
        ${UECC_COMMON} uECC_PLATFORM=0 uECC_OPTIMIZATION_LEVEL=2)
elseif(UECC_NO_ASM)
    # Arm, but with micro-ecc's assembly disabled: -DUECC_NO_ASM=1
    target_compile_definitions(crypto_shootout PRIVATE
        ${UECC_COMMON} uECC_PLATFORM=0 uECC_OPTIMIZATION_LEVEL=2)
elseif(UECC_NO_UMAAL)
    # Arm Thumb-2 assembly, but without the UMAAL multiply/square variant
    target_compile_definitions(crypto_shootout PRIVATE
        ${UECC_COMMON} uECC_PLATFORM=5 uECC_OPTIMIZATION_LEVEL=3 uECC_ARM_USE_UMAAL=0)
else()
    # uECC_arm_thumb2 with the UMAAL multiply/square assembly.
    target_compile_definitions(crypto_shootout PRIVATE
        ${UECC_COMMON} uECC_PLATFORM=5 uECC_OPTIMIZATION_LEVEL=3 uECC_ARM_USE_UMAAL=1)
endif()

if (BENCH_NO_MBEDTLS_ASM)
    target_compile_definitions(crypto_shootout PRIVATE BENCH_NO_MBEDTLS_ASM=1)
endif()

target_link_libraries(crypto_shootout
    pico_stdlib
    hardware_flash
    pico_multicore
    pico_unique_id
    hardware_vreg
    hardware_watchdog
    hardware_adc
    pico_rand
    pico_sha256
    pico_mbedtls
)

target_compile_options(crypto_shootout PRIVATE -O2)

# Optional: run entirely from SRAM to take the XIP flash cache out of the
# measurement. -DBENCH_IN_RAM=1
if (BENCH_IN_RAM)
    pico_set_binary_type(crypto_shootout copy_to_ram)
    target_compile_definitions(crypto_shootout PRIVATE BENCH_IN_RAM=1)
endif()

pico_enable_stdio_usb(crypto_shootout 1)
pico_enable_stdio_uart(crypto_shootout 1)

pico_add_extra_outputs(crypto_shootout)

# ---- overclock sweep -------------------------------------------------------
add_executable(crypto_sweep
    sweep.c
    binfo.c
    ${MLKEM_SRC}
    lib/p256-m/p256-m.c
    lib/micro-ecc/uECC.c
    lib/monocypher/monocypher.c
    lib/monocypher/monocypher-ed25519.c
)

target_include_directories(crypto_sweep PRIVATE
    ${MLKEM_INC}
    ${CMAKE_CURRENT_BINARY_DIR}/generated
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/lib/p256-m
    ${CMAKE_CURRENT_LIST_DIR}/lib/micro-ecc
    ${CMAKE_CURRENT_LIST_DIR}/lib/monocypher
)

if (PICO_PLATFORM MATCHES "riscv")
    target_compile_definitions(crypto_sweep PRIVATE
        ${UECC_COMMON} uECC_PLATFORM=0 uECC_OPTIMIZATION_LEVEL=2)
elseif(UECC_NO_ASM)
    target_compile_definitions(crypto_sweep PRIVATE
        ${UECC_COMMON} uECC_PLATFORM=0 uECC_OPTIMIZATION_LEVEL=2)
elseif(UECC_NO_UMAAL)
    target_compile_definitions(crypto_sweep PRIVATE
        ${UECC_COMMON} uECC_PLATFORM=5 uECC_OPTIMIZATION_LEVEL=3 uECC_ARM_USE_UMAAL=0)
else()
    target_compile_definitions(crypto_sweep PRIVATE
        ${UECC_COMMON} uECC_PLATFORM=5 uECC_OPTIMIZATION_LEVEL=3 uECC_ARM_USE_UMAAL=1)
endif()

target_link_libraries(crypto_sweep
    pico_stdlib
    hardware_flash
    pico_unique_id
    pico_multicore
    pico_rand
    pico_sha256
    pico_mbedtls
    hardware_vreg
    hardware_watchdog
    hardware_clocks
    hardware_adc
)

target_compile_options(crypto_sweep PRIVATE -O2)

# The sweep must run from flash: measuring QMI divider behaviour is the point.
pico_enable_stdio_usb(crypto_sweep 1)
pico_enable_stdio_uart(crypto_sweep 1)
pico_add_extra_outputs(crypto_sweep)

if (BENCH_IN_RAM)
    target_compile_definitions(crypto_shootout PRIVATE BENCH_CODE_LOC="SRAM")
else()
    target_compile_definitions(crypto_shootout PRIVATE BENCH_CODE_LOC="XIP-flash")
endif()
# The sweep always runs from flash: QMI timing is what it measures.
target_compile_definitions(crypto_sweep PRIVATE BENCH_CODE_LOC="XIP-flash")

# mbed TLS allocates heavily; both cores sign concurrently in the dual-core
# measurement, so malloc must be mutex-protected.
target_compile_definitions(crypto_sweep PRIVATE PICO_USE_MALLOC_MUTEX=1)

# The RP2350 main stack lives in the 4 KiB SCRATCH_Y bank and cannot grow past
# it, but ML-KEM-1024 needs ~20 KiB in a single frame. ML-KEM therefore runs on
# core 1 with an explicitly supplied stack (see mlk_on_core1).
foreach(T crypto_shootout crypto_sweep)
    target_compile_definitions(${T} PRIVATE PICO_STACK_SIZE=0x1000)
endforeach()

# -DTRNG_FULL_HEALTH=1 swaps the TRNG suite's liveness gate for the full
# SP 800-90B continuous suite. Off by default: it is slower than the source it
# measures, and the suite exists to report raw throughput.
if (TRNG_FULL_HEALTH)
    foreach(T crypto_shootout crypto_sweep)
        target_compile_definitions(${T} PRIVATE TRNG_FULL_HEALTH=1)
    endforeach()
endif()
