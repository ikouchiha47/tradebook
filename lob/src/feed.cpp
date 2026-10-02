#include "feed.h"
#include <zlib.h>

FeedReader::FeedReader(const std::string& gz_path) {
  gz_ = gzopen(gz_path.c_str(), "rb");
}

FeedReader::~FeedReader() {
  if (gz_) gzclose((gzFile)gz_);
}

// Collect exactly n bytes; one gzread may return short.
static bool read_exact(gzFile gz, uint8_t* dst, int n) {
  int got = 0;
  while (got < n) {
    int r = gzread(gz, dst + got, n - got);
    if (r <= 0) return false;
    got += r;
  }
  return true;
}

bool FeedReader::next(NasdaqRawMsg& out) {
  auto* gz = (gzFile)gz_;
  if (!gz) return false;

  uint8_t hdr[2];  // stack: length prefix, no heap
  if (!read_exact(gz, hdr, 2)) return false;  // EOF: nothing more to frame

  uint16_t len = be16(hdr);
  if (len == 0) return next(out);  // padding record: skip
  if (out.payload.size() < len) out.payload.resize(len);  // grow only if this message is bigger than any seen before
  if (!read_exact(gz, out.payload.data(), len)) return false;  // truncated tail: stop

  out.len = len;
  out.type = (len > 0) ? char(out.payload[0]) : 0;
  out.locate = (len >= 3) ? be16(out.payload.data() + 1) : 0;  // corrupt-input guard
  ++n_;
  return true;
}
