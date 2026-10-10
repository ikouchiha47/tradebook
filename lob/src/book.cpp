#include "book.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

// ===========================================================================
// PriceLadder
// ===========================================================================
void PriceLadder::add_order(int64_t px, uint32_t qty) {
  if (in_hot(px)) {
    Level& l = hot_[index(px)];
    if (l.count == 0) occ_.set(index(px));  // 0 -> 1 occupancy bit
    l.total += qty;
    l.count += 1;
  } else {
    Level& l = cold_[px];
    l.total += qty;
    l.count += 1;
  }
}

void PriceLadder::sub_share(int64_t px, uint32_t qty) {
  Level* l = find(px);        // order still rests: count unchanged
  if (l) l->total -= qty;
}

bool PriceLadder::remove_order(int64_t px, uint32_t qty) {
  if (in_hot(px)) {
    Level& l = hot_[index(px)];
    l.total -= qty;

    if (--l.count == 0) {
      occ_.clear(index(px));  // 1 -> 0 occupancy bit

      l.total = 0;
      return true;            // level emptied
    }

    return false;
  }

  auto it = cold_.find(px);
  if (it == cold_.end()) return false;

  it->second.total -= qty;
  if (--it->second.count == 0) {
    cold_.erase(it);
    return true;
  }
  return false;
}

Level* PriceLadder::find(int64_t px) {
  if (in_hot(px)) {
    Level& l = hot_[index(px)];
    return l.count ? &l : nullptr;
  }
  auto it = cold_.find(px);
  return it == cold_.end() ? nullptr : &it->second;
}

int64_t PriceLadder::highest() const {
  int slot = occ_.highest();

  int64_t best = (slot < 0) ? 0 : base_ + static_cast<int64_t>(slot) * inc_;
  if (!cold_.empty()) {
    int64_t cp = cold_.rbegin()->first;
    best = (best == 0) ? cp : std::max(best, cp);
  }

  return best;
}

int64_t PriceLadder::lowest() const {
  int slot = occ_.lowest();

  int64_t best = (slot < 0) ? 0 : base_ + static_cast<int64_t>(slot) * inc_;
  if (!cold_.empty()) {
    int64_t cp = cold_.begin()->first;
    best = (best == 0) ? cp : std::min(best, cp);
  }

  return best;
}

void PriceLadder::top(bool is_bid, size_t n, std::vector<LevelRow>& out) const {
  int s = is_bid ? occ_.highest() : occ_.lowest();  // best occupied slot
  while (s >= 0 && out.size() < n) {
    out.push_back({base_ + static_cast<int64_t>(s) * inc_, hot_[s].total, hot_[s].count});
    s = is_bid ? occ_.prev_occupied(s) : occ_.next_occupied(s);  // jump to next occupied
  }
}

// ===========================================================================
// NasdaqBookParser<Book>
// ===========================================================================
template <class Book>
void NasdaqBookParser<Book>::erase_order(const Order& o) {
  book_.remove_order(o.price, o.side, o.qty);
  book_.orders.erase(o.ref);
}

template <class Book>
void NasdaqBookParser<Book>::refresh() {
  const int64_t bp = book_.best_bid();
  if (bp != 0) {
    Level* l = book_.find_level(bp, 'B');
    book_.cached.bid_px = bp;
    book_.cached.bid_sz = l ? l->total : 0;
  } else {
    book_.cached.bid_px = 0;
    book_.cached.bid_sz = 0;
  }

  const int64_t ap = book_.best_ask();
  if (ap != 0) {
    Level* l = book_.find_level(ap, 'S');
    book_.cached.ask_px = ap;
    book_.cached.ask_sz = l ? l->total : 0;
  } else {
    book_.cached.ask_px = 0;
    book_.cached.ask_sz = 0;
  }

  if (book_.cached.bid_px != 0 && book_.cached.ask_px != 0 &&
      book_.cached.bid_px >= book_.cached.ask_px) {
    onCross();
  }
}

template <class Book>
bool NasdaqBookParser<Book>::add(const AddOrder& o) {
  if (o.side != 'B' && o.side != 'S') return false;

  Order order{o.ref, static_cast<int64_t>(o.price_ticks), o.shares, o.side};

  auto it = book_.orders.find(order.ref);
  if (it != book_.orders.end()) erase_order(it->second);  // dup ref: unwind old

  book_.orders[order.ref] = order;
  book_.add_order(order.price, order.side, order.qty);

  refresh();
  return true;
}

template <class Book>
bool NasdaqBookParser<Book>::reduce(uint64_t ref, uint32_t quantity) {
  auto it = book_.orders.find(ref);
  if (it == book_.orders.end()) {
    onNoRef();
    return false;
  }

  Order& order = it->second;
  if (quantity >= order.qty) {
    if (quantity > order.qty) onClamp();
    erase_order(order);
  } else {
    book_.sub_share(order.price, order.side, quantity);
    order.qty -= quantity;
  }

  refresh();
  return true;
}

template <class Book>
bool NasdaqBookParser<Book>::remove(uint64_t ref) {
  auto it = book_.orders.find(ref);
  if (it == book_.orders.end()) {
    onNoRef();
    return false;
  }

  erase_order(it->second);
  refresh();

  return true;
}

template <class Book>
bool NasdaqBookParser<Book>::replace(const Replace& rep) {
  auto it = book_.orders.find(rep.old_ref);
  if (it == book_.orders.end()) {
    onNoRef();
    return false;
  }

  const char side = it->second.side;
  erase_order(it->second);

  AddOrder a{};
  a.ref = rep.new_ref;
  a.side = side;
  a.price_ticks = static_cast<uint64_t>(rep.price);
  a.shares = rep.shares;

  return add(a);
}

template <class Book>
Depth NasdaqBookParser<Book>::depth(size_t n) const {
  Depth d;
  d.bids.reserve(n);
  d.asks.reserve(n);
  book_.top('B', n, d.bids);
  book_.top('S', n, d.asks);

  return d;
}

// Instantiate the one body for both books.
template class NasdaqBookParser<NasdqBook>;
template class NasdaqBookParser<NasdqBookWithLadder>;
