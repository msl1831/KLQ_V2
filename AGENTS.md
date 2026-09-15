# KLQ implementation priorities

- Embedded target code prioritizes small Flash/RAM use and low execution cost. Readability is secondary.
- Prefer fixed-size buffers, packed/static tables, direct bounded loops, compile-time feature exclusion, and no heap allocation.
- Keep only comments that preserve hardware, protocol, memory-map, or safety invariants.
- Check linker-map sizes after firmware changes and avoid adding abstraction that increases the image without a measured benefit.
- Do not trade away protocol integrity, bounds checks, recoverability, power control, or motor-stop behavior for smaller code.
