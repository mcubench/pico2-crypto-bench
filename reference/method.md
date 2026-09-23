# Method

Evidence discipline. None of this tells you anything about the hardware; it tells
you whether your conclusions survive. It is also the only thing that catches a
wrong board premise, because conclusions built on one still look sound.

## A bounded loop

1. Verify SDK, CMake, both toolchains, `picotool`, serial and permissions *before*
   editing.
2. Build both ISAs, warnings as errors.
3. Flash only after both build, unless deliberately isolating one architecture.
4. Capture serial for a **finite** time; return nonzero on fault, failed test,
   reset, timeout, wrong image or truncated output.
5. Diagnose one failure, make **one attributable change**, repeat.
6. Archive the log and firmware identity before the next run.

Wrap build, flash and capture in scripts that print the effective configuration:
architecture, source identity, clock, voltage, flash divider, board, flags.

## Make device output machine-verifiable

Emit structured records — `BOOT`, `TEST:PASS`, `TEST:FAIL`, `BENCHMARK`,
`PROGRESS`, `FAULT`. A strict host monitor should require exactly one `BOOT` with a
fresh run ID, the expected identity, all correctness suites before any benchmark,
ordered progress with self-consistent counters, enough complete windows to support
the claim, and immediate failure on malformed output, reset, duplicate `BOOT` or
`FAULT`.

**Embed a source identity in every image and check it in the boot record.** A
filename, build directory, USB port or successful flash exit status is *not* proof
the expected firmware is running — a flashing tool can report success without
replacing or rebooting the image.

## Correctness before speed

- Small known-answer tests **plus** a larger independent oracle covering optimized
  paths, boundaries and rare branches.
- **Generate expected data independently** of the implementation under test. Never
  hand-type a vector; assert against a published reference.
- Test error latches, timeouts, partial transfers, counter wrap, multicore
  protocols, cancellation, exact end-of-work accounting.
- **Re-run the oracle at every operating point.** A clean boot and a plausible
  benchmark do not exclude silent computation errors. In flight, re-check a known
  answer every N iterations and stop on mismatch — negligible cost, and the only
  thing that makes overclocked numbers trustworthy.
- Keep a complete generic fallback behind every common-case shortcut.

## Test without hardware where you can

**Compile the real firmware sources against an emulated peripheral.** In C++,
operator overloading on the register struct types lets unmodified C firmware run
against a behavioural model. This catches data-path, padding and register-order
bugs with no device and no flash cycle.

**Abstract I/O behind a two-function vtable** (`get_byte`, `put_bytes`) so the same
protocol code runs on-device over USB and in a harness over a pty.

**Test serial protocols over a pty pair** — `socat PTY,link=/tmp/a PTY,link=/tmp/b`
gives two real device nodes, so the genuine host-side library and the real firmware
protocol code talk with no hardware involved.

**Audit dispatch coverage.** Parse every advertised command against the actual
dispatch table; commands get silently deleted by unrelated edits.

## Record and provenance

For every attempt including failures: objective and the single changed variable;
source identity and artifact hashes; board/package/revision/ISA; compiler and SDK
config; clock, PLL tuple, voltage request *and readback*, flash divider;
correctness gates, capture duration, window count, statistics; log path and
checksum; decision (retain / reject / retry / recover / inconclusive); exact next
action.

**State the provenance of every claim** — separate what was run on hardware, what
was derived from a schematic or datasheet, and what came from reading library
sources. An unmeasured figure quoted without qualification becomes a fact in the
next person's hands.

Commit between candidates. **Keep negative results**; they stop the next person
repeating an expensive dead end.

## Measurement statistics

Report **medians, ranges and median absolute deviation**, not a final sample. Use
synchronized windows when comparing concurrent workers, exclude warm-up
consistently, keep cadence identical across candidates, and compare only matching
source identities. Repeat any new winner and every stability boundary with
byte-identical artifacts. Treat changes near noise as neutral unless repeatable.

**Separate latency from throughput.** Timing across a link, each sample is
`elapsed = latency + work / rate`. Dividing work by elapsed folds the latency into
the rate and understates it — badly when the work item is short. A least-squares
fit of elapsed against work separates them: **slope is 1/rate, intercept is
latency**, and the intercept is diagnostic in its own right.

The fit is unbiased at any sample count; only its spread changes — roughly ±10% at
8 samples, ±5% at 20, ±2.5% at 50. **Report the sample count** so confidence is
visible rather than implied. Clamp a negative intercept to zero: it is noise, not a
negative delay.

**Filter stale samples.** Data queued from a previous work item gets attributed to
the current one and can inflate a rate by 100% or more. Verify each sample belongs
to the work item you think it does.

**Check against the physical floor.** Anything faster than the hardware can
possibly go is a broken measurement. This catches more real errors than any other
single check, and it costs one division.
