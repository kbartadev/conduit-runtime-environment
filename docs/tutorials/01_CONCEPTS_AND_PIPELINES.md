# Tutorial 1: C++20 Concepts and Pipelines

Welcome to CONDUIT. If you are used to Object-Oriented Programming (OOP) with virtual functions and `dynamic_cast`, you need to unlearn those patterns. CONDUIT relies strictly on compile-time polymorphism.

## The CRTP Handler

Instead of inheriting from a virtual interface, handlers inherit from themselves using the Curiously Recurring Template Pattern (CRTP). This allows the compiler to inline every method call.

~~~cpp
#include <conduit/core.hpp>

struct logger_stage : cre::handler_base<logger_stage> {
	// Overload resolution happens at compile time
	void on(cre::event_ptr<my_event>& ev) {
		// Implementation
	}
};
~~~

## C++20 Concepts for Heterogeneous Dispatch

When you build a pipeline, CONDUIT uses C++20 fold expressions to evaluate whether a handler can accept an event. If a handler does not have a matching `on()` method, the compiler silently bypasses it with zero runtime cost.

~~~cpp
cre::pipeline<aut_stage, db_stage, logger_stage> pipe(auth, db, log);

// If 'ev' is an auth_event, 'db_stage' is mathematically skipped.
pipe.dispatch(ev);
~~~

