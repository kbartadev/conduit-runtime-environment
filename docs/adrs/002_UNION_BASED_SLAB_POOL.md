# ADR 002: Union-Based Slab Pool Memory Layout

## Status

Accepted


## Context

Lock-free free-lists require storing a next pointer (or index) inside unallocated memory blocks. Previously, CONDUIT used `reinterpret\_cast` to write the index directly into the first 4 bytes of the payload. This caused undefined behavior due to offset mismatching and strict aliasing violations.


## Decision

We enforce a `union` structure for all memory cells:
~~~cpp
union {
	alignas(Event) unsigned char payload\[sizeof(Event)];
	uint32\_t next\_index;
};
~~~

## Consequences

### Positives

- Complete elimination of `reinterpret\_cast`
- Guaranteed C++ pointer interconvertibility (`\&cell == \&payload == \&next\_index`)
- Guaranteed minimum size of 4 bytes for empty events

### Negatives
None. The layout is strictly compliant with the C++20 standard.
