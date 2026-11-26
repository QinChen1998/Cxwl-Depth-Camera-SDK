#pragma once
namespace RvCaptureStatus {
// Informational flags are independent bits. Preserve them, including their
// combination, while requiring a valid frame and exact raw-status agreement.
inline bool allowedWarnings(unsigned warnings) { return (warnings & ~0x18u) == 0u; }
inline bool accept(unsigned raw,unsigned warnings) {
 return allowedWarnings(warnings) && raw == (1u | warnings);
}
}
