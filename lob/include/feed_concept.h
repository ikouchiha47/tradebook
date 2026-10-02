#pragma once  // header guard: paste once per .cpp (same as feed.h)
#include <concepts>  // C++20: `concept`, `requires`, `convertible_to`. This header IS the docs for those three
#include "feed.h"  // RawMsg + FeedReader (the concrete file feed this concept must also accept)

// Static polymorphism: any Feed that can next(RawMsg&) and reports open_ok().
// No vtable, no virtual — resolved at compile time like a Rust trait bound.
// Compare: Go `interface{ Next() }` would dispatch via itable per call;
// here the loop below monomorphizes per Feed type (zero-cost).

// template <typename F> 
// concept Feed = requires(F& f, typename F::message_type& m) { 
//   { f.open_ok() } -> std::convertible_to<bool>;
//   { f.next(m) } -> std::convertible_to<bool>;
// };
//
// Generic replay: works for file feed, UDP feed, or test feed — no base class.
// Args:
//  `F& feed` = borrow the feed (no copy; feeds are non-copyable owners).
//  `OnMsg&&` = forwarding reference: accepts lambdas/fns cheaply, preserves value-ness.
// Returns count delivered
//
// template <Feed F, typename OnMsg>
// uint64_t replay(F& feed, uint64_t limit, OnMsg&& on_msg) {
//   typename F::message_type m;
//   uint64_t n = 0;
//   while (n < limit && feed.next(m)) {
//     on_msg(m);  // call the callback with the message. `on_msg` itself inlines — zero-cost layering
//     ++n;
//   }
//   return n;
// }
//

