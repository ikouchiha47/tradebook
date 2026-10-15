#include "feed.h"
#include <cstring>
#include <zlib.h>

// ---------------------------------------------------------------------------
// GZSource / RawSource
// ---------------------------------------------------------------------------
GZSource::GZSource(const std::string& path) { gz_ = gzopen(path.c_str(), "rb"); }

GZSource::~GZSource() {
  if (gz_) gzclose(static_cast<gzFile>(gz_));
}

size_t GZSource::read(uint8_t* dst, size_t n) {
  int r = gzread(static_cast<gzFile>(gz_), dst, static_cast<unsigned>(n));
  return r > 0 ? static_cast<size_t>(r) : 0;
}

RawSource::RawSource(const std::string& path) : in_(path, std::ios::binary) {}

size_t RawSource::read(uint8_t* dst, size_t n) {
  in_.read(reinterpret_cast<char*>(dst), static_cast<std::streamsize>(n));
  return static_cast<size_t>(in_.gcount());
}

#if 0  // BlockBuf disabled — LinearBuf is the one we keep
// ---------------------------------------------------------------------------
// BlockBuf<Source>  (std::streambuf)
// ---------------------------------------------------------------------------
template <class Source>
BlockBuf<Source>::BlockBuf(const std::string& path) : src_(path), buf_(kBlock) {
  char* base = reinterpret_cast<char*>(buf_.data());
  setg(base, base, base);
}

template <class Source>
bool BlockBuf<Source>::refill(size_t need) {
  size_t have = size();
  char* base = reinterpret_cast<char*>(buf_.data());
  if (have && gptr() != base) std::memmove(base, gptr(), have);  // compact leftover
  setg(base, base, base + have);
  if (buf_.size() < need) buf_.resize(need);
  while (have < need) {
    size_t got = src_.read(buf_.data() + have, buf_.size() - have);
    if (got == 0) { eof_ = true; return have >= need; }
    have += got;
    setg(base, base, base + have);
  }
  return true;
}

template <class Source>
bool BlockBuf<Source>::want(size_t n) {
  if (size() >= n) return true;
  refill(n);
  return size() >= n;
}

template <class Source>
std::streambuf::int_type BlockBuf<Source>::underflow() {
  if (gptr() < egptr()) return std::streambuf::traits_type::to_int_type(*gptr());
  if (!refill(1)) return std::streambuf::traits_type::eof();
  return std::streambuf::traits_type::to_int_type(*gptr());
}
#endif

// ---------------------------------------------------------------------------
// LinearBuf<Source>  (plain block + integer cursor)
// ---------------------------------------------------------------------------
template <class Source>
LinearBuf<Source>::LinearBuf(const std::string& path) : src_(path), buf_(kBlock) {}

template <class Source>
bool LinearBuf<Source>::refill(size_t need) {
  if (avail_ && pos_) std::memmove(buf_.data(), buf_.data() + pos_, avail_);
  pos_ = 0;  // always reset: with avail_==0 the memmove is skipped, pos_ would stay stale

  if (buf_.size() < need) buf_.resize(need);
  while (avail_ < need) {
    size_t got = src_.read(buf_.data() + avail_, buf_.size() - avail_);

    if (got == 0) { eof_ = true; return avail_ >= need; }
    avail_ += got;
  }
  return true;
}

template <class Source>
bool LinearBuf<Source>::want(size_t n) {
  if (avail_ >= n) return true;
  refill(n);
  return avail_ >= n;
}

// ---------------------------------------------------------------------------
// FeedReader<Buf> — framing only
// ---------------------------------------------------------------------------
template <class Buf>
bool FeedReader<Buf>::next(NasdaqRawMsg& out) {
  if (!buf_.want(2)) return false;  // EOF: nothing more to frame

  uint16_t len = be16(buf_.data());
  if (len == 0) { buf_.consume(2); return next(out); }  // padding record: skip
  if (!buf_.want(static_cast<size_t>(len) + 2)) return false;  // truncated tail

  len = be16(buf_.data());                                     // re-read (may have moved)

  if (out.payload.size() < len) out.payload.resize(len);  // grow only if bigger

  std::memcpy(out.payload.data(), buf_.data() + 2, len);
  buf_.consume(static_cast<size_t>(len) + 2);

  out.len = len;
  out.type = char(out.payload[0]);
  out.locate = (len >= 3) ? be16(out.payload.data() + 1) : 0;
  ++n_;

  return true;
}

// template class BlockBuf<GZSource>;
// template class BlockBuf<RawSource>;
template class LinearBuf<GZSource>;
template class LinearBuf<RawSource>;
// template class FeedReader<BlockBuf<GZSource>>;
// template class FeedReader<BlockBuf<RawSource>>;
template class FeedReader<LinearBuf<GZSource>>;
template class FeedReader<LinearBuf<RawSource>>;
