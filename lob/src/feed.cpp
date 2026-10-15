#include "feed.h"
#include <cstdio>
#include <cstring>
#include <zlib.h>

FeedReader::FeedReader(const std::string& path, bool raw) : raw_(raw) {
  if (raw) {
    handle_ = static_cast<void*>(std::fopen(path.c_str(), "rb"));
    buf_.resize(1u << 20);  // 1 MiB block: ~1 fread per MiB, not 2 per message
  } else {
    handle_ = static_cast<void*>(gzopen(path.c_str(), "rb"));
  }
}

FeedReader::~FeedReader() {
  if (!handle_) return;
  if (raw_) std::fclose(static_cast<FILE*>(handle_));
  else      gzclose(static_cast<gzFile>(handle_));
}

// Collect exactly n bytes from a gz stream; one gzread may return short.
static bool read_exact_gz(gzFile gz, uint8_t* dst, int n) {
  int got = 0;
  while (got < n) {
    int r = gzread(gz, dst + got, n - got);
    if (r <= 0) return false;
    got += r;
  }
  return true;
}

// raw path: guarantee >= n bytes are available at buf_[pos_], refilling in blocks.
bool FeedReader::ensure(size_t n) {
  if (avail_ >= n) return true;
  if (buf_.size() < n) buf_.resize(n);
  if (avail_ && pos_) std::memmove(buf_.data(), buf_.data() + pos_, avail_);  // compact
  pos_ = 0;
  while (avail_ < n) {
    size_t got = std::fread(buf_.data() + avail_, 1, buf_.size() - avail_,
                            static_cast<FILE*>(handle_));
    if (got == 0) return false;  // EOF / error
    avail_ += got;
  }
  return true;
}

bool FeedReader::next(NasdaqRawMsg& out) {
  if (!handle_) return false;

  if (raw_) {
    if (!ensure(2)) return false;
    uint16_t len = be16(buf_.data() + pos_);
    if (len == 0) { pos_ += 2; avail_ -= 2; return next(out); }  // padding
    if (!ensure(static_cast<size_t>(len) + 2)) return false;     // compaction may move
    len = be16(buf_.data() + pos_);
    if (out.payload.size() < len) out.payload.resize(len);
    std::memcpy(out.payload.data(), buf_.data() + pos_ + 2, len);
    pos_ += static_cast<size_t>(len) + 2;
    avail_ -= static_cast<size_t>(len) + 2;

    out.len = len;
    out.type = char(out.payload[0]);
    out.locate = (len >= 3) ? be16(out.payload.data() + 1) : 0;
    ++n_;
    return true;
  }

  uint8_t hdr[2];  // stack: length prefix, no heap
  if (!read_exact_gz(static_cast<gzFile>(handle_), hdr, 2)) return false;
  uint16_t len = be16(hdr);
  if (len == 0) return next(out);  // padding record: skip
  if (out.payload.size() < len) out.payload.resize(len);
  if (!read_exact_gz(static_cast<gzFile>(handle_), out.payload.data(), len)) return false;

  out.len = len;
  out.type = (len > 0) ? char(out.payload[0]) : 0;
  out.locate = (len >= 3) ? be16(out.payload.data() + 1) : 0;
  ++n_;
  return true;
}

bool gunzip_to_file(const std::string& gz_path, const std::string& out_path) {
  gzFile gz = gzopen(gz_path.c_str(), "rb");
  if (!gz) return false;
  FILE* fp = std::fopen(out_path.c_str(), "wb");
  if (!fp) { gzclose(gz); return false; }

  std::vector<uint8_t> buf(1 << 20);
  int r = 0;
  bool ok = true;
  while ((r = gzread(gz, buf.data(), static_cast<unsigned>(buf.size()))) > 0) {
    if (std::fwrite(buf.data(), 1, static_cast<size_t>(r), fp) != static_cast<size_t>(r)) {
      ok = false;
      break;
    }
  }
  if (r < 0) ok = false;

  std::fclose(fp);
  gzclose(gz);
  return ok;
}
