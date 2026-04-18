# Tutorial 3: Clusters and Routing

A conduit is a physical pipe. A cluster is the spatial map that connects them.

## Fan-Out (Distributing Load)

To distribute work across multiple threads without lock contention, use a `round_robin_switch`. It guarantees deterministic distribution across multiple isolated tracks.

~~~cpp
axiom::conduit<work_event, 1024> track_a;
axiom::conduit<work_event, 1024> track_b;

axiom::round_robin_switch<work_event, 2, 1024> balancer;
balancer.bind_track(0, track_a);
balancer.bind_track(1, track_b);

axiom::cluster<256> router;
router.bind<work_event>(balancer);

// Sent in O(1) time. The switch alternates between track_a and track_b.
router.send(domain.make<work_event>());
~~~