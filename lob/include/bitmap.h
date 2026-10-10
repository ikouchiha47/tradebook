#pragma once

#include <cstdint>
#include <cstddef>
#include <tuple>
#include <vector>

class Occupancy {
public:
  Occupancy () = default;
  explicit Occupancy(size_t slots) {
    leaf_ = std::vector<uint64_t>((slots + 63) / 64); // 1 bit per slot
    summary_ = std::vector<uint64_t>((leaf_.size() + 63) / 64); // 1 bit per leaf
    root_ = std::vector<uint64_t>((summary_.size() + 63) / 64); // 1 bit per summ
  }

  std::tuple<size_t, size_t, size_t, size_t> addr(size_t slot);
  void set(size_t slot);
  void clear(size_t slot);

  int highest() const;
  int lowest() const;
  int prev_occupied(int slot) const;  // highest set slot < slot, or -1
  int next_occupied(int slot) const;  // lowest  set slot > slot, or -1


private:
  std::vector<uint64_t> leaf_, summary_, root_;
};
