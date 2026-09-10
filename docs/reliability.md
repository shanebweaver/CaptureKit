# Investigating CaptureKit reliability

The aim is to contain recoverable capture failures, prevent invalid memory access,
and preserve enough evidence to explain failures in the host app. Suppressing a
fatal exception is not evidence that the process is safe to continue.

## Interpreting the reported Partner Center failures

The September 2026 report supplied during this review contains:

| Reported bucket | Hits | Affected devices | What it establishes |
| --- | ---: | ---: | --- |
| XAML `CUIElement::GetProtectedCursor`, `0xC0000005` | 2 | 2 | An access violation surfaced in XAML. The complete stack is needed to connect it to the app or library. |
| Store `0xC000000D`, labeled memory failure | 2 | 2 | The visible code is invalid parameter. That label alone does not establish an out-of-memory failure. |
| Composition `0x8007000E`, out of memory | 1 | 1 | Allocation failed in the composition path. The allocating component need not be the component retaining memory. |
| Uncategorized | 1,382 | 212 | No detailed failure pattern is available. Neither the failure type nor its connection to CaptureKit is established. |

Do not add affected-device counts across buckets: one device can appear in several.
Uncategorized events must not be assumed to be crashes or attributed to missing
symbols based on this report alone. The supplied explanation explicitly says this
category does not establish a Partner Center setup problem.

Microsoft's [Health Report documentation](https://learn.microsoft.com/en-us/windows/apps/publish/analyze-app-performance/health-report)
describes filtering by app version, OS, architecture, and device configuration.
Its current MSIX crash-rate metric is the percentage of daily active reporting
devices with at least one crash; this differs from an application's own failures
per capture attempt or capture hour.

## Evidence to collect next

1. Export the same date range with app/package version, OS build, architecture,
   failure code, full bucket name, and complete stack where available. Preserve
   the exact binaries and matching PDBs for that release. This repo packages native
   PDBs when present; that does not verify symbols for a particular Store release.
2. Compare failures to usage: affected devices / active devices, failed capture
   attempts / attempts, and failures / capture hours. More activity can create
   more failure hits with an unchanged failure rate. Geography is a grouping
   dimension, not a demonstrated cause.
3. At the host-app level, keep a bounded sequence of capture lifecycle events:
   session identifier, operation, elapsed time, result/HRESULT, dimensions, target
   kind, frame rate, active-session count, and app/CaptureKit version. Capture
   start, first frame, pause/resume, stop, finalization, and disposal are useful.
4. For resource-pressure investigations, sample private bytes, GDI/USER handles,
   outstanding image/texture counts, and GPU memory usage/budget where available.
   Include GPU adapter/driver and selected encoder information. Avoid per-frame
   logging; use bounded, periodic samples and aggregate counters.
5. Preserve recent events before failure through the host's existing diagnostic
   mechanism. An in-memory buffer alone does not survive a process crash. Keep
   reporting best-effort and avoid allocations while handling allocation failures.

These are proposed diagnostic additions. This change does not add a telemetry
collector or upload anything. Native `ReportBoundaryException` currently writes
to `OutputDebugStringW`; managed callback reporting uses `Trace`. Neither is, by
itself, a durable production diagnostic record or a Partner Center attachment.

## Defects addressed in this pass

- Screenshot C ABI calls now contain C++ allocation and other exceptions and
  return their existing null/false failure values. Monitor enumeration catches
  exceptions inside the Win32 callback and saves a nonallocating HRESULT first.
- Combining screenshot handles borrows source pixels instead of moving them
  out of still-live handles. Repeated calls, duplicate inputs, and failed combines
  preserve the source images without creating extra full-size source copies.
- Native composition validates source lengths and dimensions, computes bounds
  in 64 bits, and uses size-based row offsets. PNG byte counts are checked before
  narrowing to the UINT sizes required by WIC.
- PNG save balances every successful COM initialization, including S_FALSE.
  [Microsoft documents this requirement](https://learn.microsoft.com/en-us/windows/win32/api/combaseapi/nf-combaseapi-coinitializeex).
- Managed bitmap creation validates the BGRA buffer before copying into native
  memory, respects row stride, and disposes partially constructed bitmaps if
  locking or copying fails. Invalid caller data raises a managed argument error.

These defects are established by source inspection and regression tests. No
causal link to the supplied Partner Center buckets has been demonstrated. The
host must still handle returned failures and expected managed exceptions.

## Regression and stress coverage

`ScreenshotSafetyTests` covers malformed source buffers/dimensions, extreme
coordinates, negative desktop coordinates, repeated combinations, failed and
duplicate-input combinations, exception containment, WIC size limits, and COM
balance after repeated PNG-save failures. `DisplayCaptureServiceTests` covers
oversized/undersized buffers, extreme dimensions, pixel row order, and capture
interop loading.

Further host-app stress runs should exercise rapid start/stop and screenshot
repetition, multiple high-resolution monitors, closing a capture target,
disconnecting a display, sleep/wake, audio device changes, and disk-full/write
failures. Measure whether resources return toward their post-warmup baseline.
Use [MSVC AddressSanitizer](https://learn.microsoft.com/en-us/cpp/sanitizers/asan)
for native out-of-bounds and lifetime bugs; it does not establish freedom from
thread races. Device/driver failure recovery and XAML teardown need separate
host integration tests and production stacks.
