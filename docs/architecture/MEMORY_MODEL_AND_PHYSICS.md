# Memory Model and Hardware Physics

## Atomic Memory Ordering

AXIOM uses Acquire-Release semantics to minimize CPU pipeline stalls.

- **Allocation:** `compare\_exchange\_weak` with `memory\_order\_release` on success and `memory\_order\_acquire` on failure.
- **Deallocation:** Writes to `next\_index` occur before the atomic store to ensure global visibility.

## ABA Protection

The pool uses a 64-bit atomic state:
- `[63:32] Tag` - Incrementing counter to prevent ABA issues.
- `[31:0] Index` - The free-list pointer.

## False Sharing

All atomic counters are padded using `std::hardware\_destructive\_interference\_size` to ensure they reside on distinct L1 cache lines, preventing MESI protocol thrashing.