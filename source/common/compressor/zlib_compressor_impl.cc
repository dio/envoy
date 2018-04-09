#include "common/compressor/zlib_compressor_impl.h"

#include "envoy/common/exception.h"

#include "common/common/assert.h"

namespace Envoy {
namespace Compressor {

ZlibCompressorImpl::ZlibCompressorImpl() : ZlibCompressorImpl(4096) {}

ZlibCompressorImpl::ZlibCompressorImpl(uint64_t chunk_size)
    : chunk_size_{chunk_size}, initialized_{false}, chunk_char_ptr_(new unsigned char[chunk_size]),
      zstream_ptr_(new z_stream(), [](z_stream* z) {
        deflateEnd(z);
        delete z;
      }) {
  zstream_ptr_->zalloc = Z_NULL;
  zstream_ptr_->zfree = Z_NULL;
  zstream_ptr_->opaque = Z_NULL;
  zstream_ptr_->avail_out = chunk_size_;
  zstream_ptr_->next_out = chunk_char_ptr_.get();
}

void ZlibCompressorImpl::init(CompressionLevel comp_level, CompressionStrategy comp_strategy,
                              int64_t window_bits, uint64_t memory_level = 8) {
  ASSERT(initialized_ == false);
  const int result = deflateInit2(zstream_ptr_.get(), static_cast<int64_t>(comp_level), Z_DEFLATED,
                                  window_bits, memory_level, static_cast<uint64_t>(comp_strategy));
  RELEASE_ASSERT(result >= 0);
  initialized_ = true;
}

void ZlibCompressorImpl::flush(Buffer::Instance& output_buffer) {
  process(output_buffer, Z_SYNC_FLUSH);
}

uint64_t ZlibCompressorImpl::checksum() { return zstream_ptr_->adler; }

void ZlibCompressorImpl::compress(const Buffer::Instance& input_buffer,
                                  Buffer::Instance& output_buffer) {
  const uint64_t num_slices = input_buffer.getRawSlices(nullptr, 0);
  Buffer::RawSlice slices[num_slices];
  input_buffer.getRawSlices(slices, num_slices);

  for (const Buffer::RawSlice& input_slice : slices) {
    zstream_ptr_->avail_in = input_slice.len_;
    zstream_ptr_->next_in = static_cast<Bytef*>(input_slice.mem_);
    process(output_buffer, Z_NO_FLUSH);
  }
}

bool ZlibCompressorImpl::deflateNext(int64_t flush_state) {
  const int result = deflate(zstream_ptr_.get(), flush_state);
  if (result == Z_BUF_ERROR && zstream_ptr_->avail_in == 0) {
    return false; // This means that zlib needs more input, so stop here.
  }

  RELEASE_ASSERT(result == Z_OK);
  return true;
}

void ZlibCompressorImpl::process(Buffer::Instance& output_buffer, int64_t flush_state) {
  while (deflateNext(flush_state)) {
    if (zstream_ptr_->avail_out == 0) {
      updateOutput(output_buffer);
    }
  }

  if (flush_state == Z_SYNC_FLUSH) {
    updateOutput(output_buffer, true);
  }
}

void ZlibCompressorImpl::updateOutput(Buffer::Instance& output_buffer, const bool require_finish) {
  const uint64_t n_output = chunk_size_ - zstream_ptr_->avail_out;
  if (n_output > 0) {
    output_buffer.add(static_cast<void*>(chunk_char_ptr_.get()), n_output);
  }
  chunk_char_ptr_.reset(new unsigned char[chunk_size_]);
  zstream_ptr_->avail_out = chunk_size_;
  zstream_ptr_->next_out = chunk_char_ptr_.get();

  if (require_finish) {
    finish(output_buffer);
  }
}

void ZlibCompressorImpl::reset() {
  const bool result = deflateReset(zstream_ptr_.get());
  RELEASE_ASSERT(result == Z_OK);
}

void ZlibCompressorImpl::finish(Buffer::Instance& output_buffer) {
  // Once the Z_FINISH parameter is provided, deflate() will begin to complete the compressed output
  // stream. However depending on how much output space is provided, deflate() may have to be called
  // several times until it has provided the complete compressed stream, even after it has consumed
  // all of the input. The flush parameter must continue to be Z_FINISH for those subsequent calls.
  // Ref: https://zlib.net/zlib_how.html.
  int result = Z_OK;
  while (result == Z_OK || result == Z_BUF_ERROR) {
    result = deflate(zstream_ptr_.get(), Z_FINISH);
    updateOutput(output_buffer);
  }
  RELEASE_ASSERT(result == Z_STREAM_END);
}

} // namespace Compressor
} // namespace Envoy
