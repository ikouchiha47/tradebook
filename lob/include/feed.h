#pragma once
#include <cstdint>
#include <string>
#include <vector>

// Framing only: split the BinaryFILE stream into raw messages.
// One concrete RAII owner for the gz handle; parsing is free functions.
// Full contract: docs/feed.md
struct NasdaqRawMsg {
  char type = 0;                 // payload[0]: ITCH letter
  uint16_t locate = 0;           // BE16(payload[1..2]): stock index, 0 = global
  uint16_t len = 0;              // length-prefix value: payload size in bytes
  std::vector<uint8_t> payload;  // message bytes; buffer reused across next() calls
};

class FeedReader {
  public:
    using messsage_type = NasdaqRawMsg;

    // raw=false → gzip `.gz` (zlib gzread); raw=true → pre-decompressed framed `.bin`.
    explicit FeedReader(const std::string& path, bool raw = false);
    ~FeedReader();  // closes iff opened
    // Non-copyable: two owners would double-close one handle.
    FeedReader(const FeedReader&) = delete;
    FeedReader& operator=(const FeedReader&) = delete;
    bool open_ok() const { return handle_ != nullptr; }
    // Fill caller's struct in place; false on EOF/error. No alloc after warmup.
    bool next(NasdaqRawMsg& out);
    uint64_t messages_read() const { return n_; }

  private:
    bool ensure(size_t n);    // raw path: make n bytes available in buf_ (block read)
    void* handle_ = nullptr;  // gzFile or FILE*, erased so this header stays zlib-free
    bool raw_ = false;
    uint64_t n_ = 0;
    std::vector<uint8_t> buf_;  // raw block buffer: ~1 fread per block, not per message
    size_t pos_ = 0, avail_ = 0;
};

// One-shot: gunzip `gz_path` into `out_path`, keeping the SAME 2B-len-prefixed framing,
// so a later `FeedReader(out, /*raw=*/true)` replays it with no zlib. False on error.
bool gunzip_to_file(const std::string& gz_path, const std::string& out_path);

// 2B big-endian decode. Value arithmetic: correct on any host endianness.
inline uint16_t be16(const uint8_t* p) {
  return (uint16_t(p[0]) << 8) | uint16_t(p[1]);
}
