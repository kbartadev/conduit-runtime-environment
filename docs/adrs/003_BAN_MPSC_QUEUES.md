# ADR 003: Ban on MPSC and MPMC Queues

## Status

Accepted

## Context

Standard thread-pool architectures use Multi-Producer Single-Consumer (MPSC) or Multi-Producer Multi-Consumer (MPMC) queues to distribute work.

## Decision

MPSC and MPMC queues are explicitly banned from the CONDUIT architecture. Only Single-Producer Single-Consumer (SPSC) queues (`conduit`) are allowed.

## Consequences

### Positives

By avoiding concurrent writes to the same atomic head and tail indices, CONDUIT eliminates False Sharing and MESI cache invalidation storms. Throughput becomes entirely deterministic.

### Negatives

Requires explicit topological routing using `round_robin_switch` for Fan-Out and `round_robin_poller` for Fan-In.# ADR 003: Ban on MPSC and MPMC Queues

## Status

Accepted

## Context

Standard thread-pool architectures use Multi-Producer Single-Consumer (MPSC) or Multi-Producer Multi-Consumer (MPMC) queues to distribute work.

## Decision

MPSC and MPMC queues are explicitly banned from the CONDUIT architecture. Only Single-Producer Single-Consumer (SPSC) queues (`conduit`) are allowed.

## Consequences

### Positives

By avoiding concurrent writes to the same atomic head and tail indices, CONDUIT eliminates False Sharing and MESI cache invalidation storms. Throughput becomes entirely deterministic.

### Negatives

Requires explicit topological routing using `round_robin_switch` for Fan-Out and `round_robin_poller` for Fan-In.