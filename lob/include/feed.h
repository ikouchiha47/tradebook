#pragma once
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <streambuf>
#include <string>
#include <vector>

// One ITCH message, framed. payload is reused across next() calls.
struct NasdaqRawMsg {
  char type = 0;                 // payload[0]: ITCH letter
  uint16_t locate = 0;           // BE16(payload[1..2]): stock index, 0 = global
  uint16_t len = 0;              // length-prefix value: payload size in bytes
  std::vector<uint8_t> payload;  // message bytes
};

// ---------------------------------------------------------------------------
// Byte sources — one job: hand out up to n bytes (0 = EOF).
// ---------------------------------------------------------------------------
class GZSource {
 public:
  explicit GZSource(const std::string& path);
  ~GZSource();
  GZSource(const GZSource&) = delete;
  GZSource& operator=(const GZSource&) = delete;
  bool open_ok() const { return gz_ != nullptr; }
  size_t read(uint8_t* dst, size_t n);  // up to n bytes; 0 = EOF
 private:
  void* gz_ = nullptr;
};

class RawSource {
 public:
  explicit RawSource(const std::string& path);
  bool open_ok() const { return in_.is_open(); }
  size_t read(uint8_t* dst, size_t n);
 private:
  std::ifstream in_;
};

// ===========================================================================
// Two block buffers, ONE interface (picked at compile time, like NasdaqBook):
//   open_ok()   data()   size()   consume(n)   want(n)
// ===========================================================================

#if 0  // BlockBuf (std::streambuf) disabled — LinearBuf is the one we keep
// BlockBuf<Source> — std::streambuf-backed: get-area pointers (gptr/egptr) are the cursor.
template <class Source>
class BlockBuf : public std::streambuf {
 public:
  explicit BlockBuf(const std::string& path);
  bool open_ok() const { return src_.open_ok(); }
  const uint8_t* data() const { return reinterpret_cast<const uint8_t*>(gptr()); }
  size_t size() const { return static_cast<size_t>(egptr() - gptr()); }
  void consume(size_t n) { gbump(static_cast<int>(n)); }
  bool want(size_t n);

 private:
  std::streambuf::int_type underflow() override;
  bool refill(size_t need);
  static constexpr size_t kBlock = size_t{1} << 20;

  Source src_;
  std::vector<uint8_t> buf_;
  bool eof_ = false;
};
#endif

// LinearBuf<Source> — a plain 1 MiB block + integer cursor (pos_/avail_), same interface.
template <class Source>
class LinearBuf {
 public:
  explicit LinearBuf(const std::string& path);
  bool open_ok() const { return src_.open_ok(); }
  const uint8_t* data() const { return buf_.data() + pos_; }
  size_t size() const { return avail_; }
  void consume(size_t n) { pos_ += n; avail_ -= n; }
  bool want(size_t n);

 private:
  bool refill(size_t need);
  static constexpr size_t kBlock = size_t{1} << 20;

  Source src_;
  std::vector<uint8_t> buf_;
  size_t pos_ = 0, avail_ = 0;
  bool eof_ = false;
};

// ---------------------------------------------------------------------------
// Framing reader, templated on the buffer (static polymorphism). Framing only;
// the buffer does the bytes.
// ---------------------------------------------------------------------------
template <class Buf>
class FeedReader {
 public:
  explicit FeedReader(const std::string& path) : buf_(path) {}
  bool open_ok() const { return buf_.open_ok(); }
  bool next(NasdaqRawMsg& out);
  uint64_t messages_read() const { return n_; }

 private:
  Buf buf_;
  uint64_t n_ = 0;
};

// 2B big-endian decode. Value arithmetic: correct on any host endianness.
inline uint16_t be16(const uint8_t* p) {
  return (uint16_t(p[0]) << 8) | uint16_t(p[1]);
}
