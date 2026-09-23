---
name: rp2350-development
description: Developing, optimizing and testing firmware on RP2350 / Pico 2 and derivative boards, in C (pico-sdk), Rust (rp235x-hal) or Arduino (arduino-pico), on both Cortex-M33 and Hazard3 RISC-V cores. Covers board identification and flashing, silicon errata and performance behaviour, and the experiment discipline that makes results trustworthy. Use when bringing up an unfamiliar RP2350 board, writing or debugging its firmware, chasing throughput, or running an overclock campaign.
---

# RP2350 / Pico 2 development

Three kinds of knowledge, which fail independently and in different ways.

| | answers | rule type | how it fails |
|---|---|---|---|
| **[Board](reference/board.md)** | which object is in front of me | premises, verified once per hardware | compiles cleanly, then drives the wrong pins or addresses |
| **[Silicon](reference/silicon.md)** | what RP2350 does on any board | invariants and errata | presents as something else entirely |
| **[Method](reference/method.md)** | how I know any of it is true | evidence discipline | you get a number and believe it |

**The dependency runs one way.** A wrong board premise silently invalidates every
silicon conclusion built on it — and those conclusions will still *look* sound,
because they follow correctly from a false start. No amount of careful
measurement finds that; only method does.

## Reading order

1. **Establish board identity first**, before writing code. Package, real flash
   size, exposed pins, how to get back into BOOTSEL. Cheap to verify, expensive
   to assume.
2. **Treat silicon rules as fixed constraints** to design around, not surprises
   to debug. Each presents as a different fault: a glitching servo pair reads as
   a power problem, an EEPROM commit as a scheduler bug.
3. **Apply method continuously.** It tells you nothing about the hardware; it
   tells you whether your conclusions survive.

## The two checks that catch the most

**Compute the physical floor.** Cycles per operation should be flat across clock
frequency. If it is, the clock is a pure multiplier, and any measurement beyond
the floor is a broken measurement rather than a fast chip.

**Label every claim** measured, derived, or read from source. An unmeasured
figure quoted without qualification becomes a fact in the next person's hands.

## Checklist

- [ ] Board, package and revision established from vendor documentation or the
      physical part — not inferred from an undocumented register
- [ ] Real flash size declared in the build, not inherited from a board header
- [ ] Both ISAs build warning-free, if source is shared
- [ ] Expected image visibly flashed, identity verified in the boot record
- [ ] Correctness suite and independent oracle pass **at this operating point**
- [ ] Serial capture complete, ordered, identity-matched
- [ ] Performance from comparable windows, with spread and sample count reported
- [ ] Result and failure class logged with artifact hashes
- [ ] Every claim labelled measured, derived, or read from source
- [ ] Rejected code restored exactly
- [ ] Device returned to a validated stock configuration after risky testing
