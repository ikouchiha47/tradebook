#include "book.h"
#include "itch.h"
#include <cstdint>

NasdaqBookParser::NasdaqBookParser() = default;
NasdaqBookParser::~NasdaqBookParser() = default;

void NasdaqBookParser::erase_order(const Order &o) {
  // here order side can only be B or S
  auto& map = (o.side == 'B') ? book_.bids : book_.asks;
  auto lvl = map.find(o.price);

  if(lvl != map.end()) {
    lvl->second.total -= o.qty;
    if(--lvl->second.count == 0) map.erase(lvl);
  }

  book_.orders.erase(o.ref);
}

void NasdaqBookParser::refresh() {
  if(!book_.bids.empty()) {
    auto it = book_.bids.rbegin(); // highest from back

    book_.cached.bid_px = it->first; // key
    book_.cached.bid_sz = it->second.total;
  } else  {
    book_.cached.bid_sz = 0;
    book_.cached.bid_px = 0;
  }

  if(!book_.asks.empty()) {
    auto it = book_.asks.begin(); // lowest ask

    book_.cached.ask_px = it->first;
    book_.cached.ask_sz = it->second.total;
  } else {
    book_.cached.ask_px = 0;
    book_.cached.ask_sz = 0;
  }

  // correctness check
  // Best bid ≥ best ask is impossible in a real marke
  if (book_.cached.bid_px != 0 && book_.cached.ask_px != 0 &&
      book_.cached.bid_px >= book_.cached.ask_px) {

    onCross();
  }}

bool NasdaqBookParser::add(const AddOrder& new_order) {
  if (new_order.side != 'B' && new_order.side != 'S')
    return false;

  Order order = {
    .ref = new_order.ref,
    .price = static_cast<int64_t>(new_order.price_ticks),
    .qty = new_order.shares,
    .side = new_order.side,
  };

  auto it = book_.orders.find(order.ref);
  if (it != book_.orders.end()) erase_order(it->second);

  Level* lvl = nullptr;
  // Level& lvl; // cannot be null, but same point to map
  // & .. ref is the object
  // * .. points to the object
  if(order.side == 'B') {
    lvl = &(book_.bids[order.price]);
  } else if(order.side == 'S') {
    lvl = &(book_.asks[order.price]);
  }
  
  if(lvl == nullptr) return false;

  book_.orders[new_order.ref] = order;

  lvl->count += 1;
  lvl->total += order.qty;

  this->refresh();
  return true;
}

bool NasdaqBookParser::remove(uint64_t ref) {
  auto it = book_.orders.find(ref);
  
  if (it == book_.orders.end()) {
    // bookkeep
    this->onNoRef();
    return false;
  }

  erase_order(it->second);
  this->refresh();

  return true;
}


bool NasdaqBookParser::reduce(uint64_t ref, uint32_t quantity) {
  auto it = book_.orders.find(ref);
  
  if (it == book_.orders.end()) {
    // bookkeep
    this->onNoRef();
    return false;
  }

  Order& order  = it->second;

  if (quantity >= order.qty) {
    if (quantity > order.qty) this->onClamp();

    this->erase_order(order);

  } else {
    auto& map = (order.side == 'B') ? book_.bids : book_.asks;
    map[order.price].total -= quantity;
    order.qty -= quantity;
  }
  
  this->refresh();

  return true;
}

bool NasdaqBookParser::replace(const Replace &rep) {
  auto it = book_.orders.find(rep.old_ref);
  if(it == book_.orders.end()) {
    onNoRef();
    return false;
  }

  char side = it->second.side;
  erase_order(it->second);

  AddOrder new_order{};
  new_order.ref = rep.new_ref;
  new_order.side = side;
  new_order.price_ticks = static_cast<uint64_t>(rep.price);
  new_order.shares = rep.shares;

  return this->add(new_order);
}

Depth NasdaqBookParser::depth(size_t n) const {
  Depth d;

  d.asks.reserve(n);
  d.bids.reserve(n);

  for(auto it = book_.bids.rbegin(); it != book_.bids.rend() && d.bids.size() < n; ++it) {
    d.bids.push_back({
        it->first,
        it->second.total,
        it->second.count
        });
  }

  for(auto it = book_.asks.begin(); it != book_.asks.end() && d.asks.size() < n; ++it) {
    d.asks.push_back({
        it->first,
        it->second.total,
        it->second.count
        });
  }

  return d;
}
