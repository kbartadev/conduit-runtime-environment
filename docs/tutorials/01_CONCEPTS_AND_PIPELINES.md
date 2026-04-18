# Tutorial 1: C++20 Concepts and Pipelines

Welcome to AXIOM. If you are used to Object-Oriented Programming (OOP) with virtual functions and `dynamic\_cast`, you need to unlearn those patterns. AXIOM relies strictly on compile-time polymorphism.

## The CRTP Handler

Instead of inheriting from a virtual interface, handlers inherit from themselves using the Curiously Recurring Template Pattern (CRTP). This allows the compiler to inline every method call.

~~~cpp
#include <axiom_conduit/core.hpp>

struct logger\_stage : axiom::handler\_base<logger\_stage> {
		// Overload resolution happens at compile time
		void on(axiom::event\_ptr<my\_event>\& ev) {
		// Implementation
	}
};
~~~

## C++20 Concepts for Heterogeneous Dispatch

When you build a pipeline, AXIOM uses C++20 fold expressions to evaluate whether a handler can accept an event. If a handler does not have a matching `on()` method, the compiler silently bypasses it with zero runtime cost.

~~~cpp
axiom::pipeline<auth\_stage, db\_stage, logger\_stage> pipe(auth, db, log);

// If 'ev' is an auth\_event, 'db\_stage' is mathematically skipped.
pipe.dispatch(ev);
~~~

