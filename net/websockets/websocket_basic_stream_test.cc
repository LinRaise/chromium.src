// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Tests for WebSocketBasicStream. Note that we do not attempt to verify that
// frame parsing itself functions correctly, as that is covered by the
// WebSocketFrameParser tests.

#include "net/websockets/websocket_basic_stream.h"

#include "base/basictypes.h"
#include "base/port.h"
#include "net/base/capturing_net_log.h"
#include "net/base/test_completion_callback.h"
#include "net/socket/socket_test_util.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace net {
namespace {

// TODO(ricea): Add tests for
// - Empty frames (data & control)
// - Non-NULL masking key
// - A frame larger than kReadBufferSize;

const char kSampleFrame[] = "\x81\x06Sample";
const size_t kSampleFrameSize = arraysize(kSampleFrame) - 1;
const char kPartialLargeFrame[] =
    "\x81\x7F\x00\x00\x00\x00\x7F\xFF\xFF\xFF"
    "chromiunum ad pasco per loca insanis pullum manducat frumenti";
const size_t kPartialLargeFrameSize = arraysize(kPartialLargeFrame) - 1;
const size_t kLargeFrameHeaderSize = 10;
const char kMultipleFrames[] = "\x81\x01X\x81\x01Y\x81\x01Z";
const size_t kMultipleFramesSize = arraysize(kMultipleFrames) - 1;
// This frame encodes a payload length of 7 in two bytes, which is always
// invalid.
const char kInvalidFrame[] = "\x81\x7E\x00\x07Invalid";
const size_t kInvalidFrameSize = arraysize(kInvalidFrame) - 1;
// Control frames must have the FIN bit set. This one does not.
const char kPingFrameWithoutFin[] = "\x09\x00";
const size_t kPingFrameWithoutFinSize = arraysize(kPingFrameWithoutFin) - 1;
// Control frames must have a payload of 125 bytes or less. This one has
// a payload of 126 bytes.
const char k126BytePong[] =
    "\x8a\x7e\x00\x7eZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZ"
    "ZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZ";
const size_t k126BytePongSize = arraysize(k126BytePong) - 1;
const char kCloseFrame[] = "\x88\x09\x03\xe8occludo";
const size_t kCloseFrameSize = arraysize(kCloseFrame) - 1;
const char kWriteFrame[] = "\x81\x85\x00\x00\x00\x00Write";
const size_t kWriteFrameSize = arraysize(kWriteFrame) - 1;
const WebSocketMaskingKey kNulMaskingKey = {{'\0', '\0', '\0', '\0'}};

// A masking key generator function which generates the identity mask,
// ie. "\0\0\0\0".
WebSocketMaskingKey GenerateNulMaskingKey() { return kNulMaskingKey; }

// Base class for WebSocketBasicStream test fixtures.
class WebSocketBasicStreamTest : public ::testing::Test {
 protected:
  scoped_ptr<WebSocketBasicStream> stream_;
  CapturingNetLog net_log_;
};

// A fixture for tests which only perform normal socket operations.
class WebSocketBasicStreamSocketTest : public WebSocketBasicStreamTest {
 protected:
  WebSocketBasicStreamSocketTest()
      : histograms_("a"), pool_(1, 1, &histograms_, &factory_) {}

  virtual ~WebSocketBasicStreamSocketTest() {
    // stream_ has a reference to socket_data_ (via MockTCPClientSocket) and so
    // should be destroyed first.
    stream_.reset();
  }

  scoped_ptr<ClientSocketHandle> MakeTransportSocket(MockRead reads[],
                                                     size_t reads_count,
                                                     MockWrite writes[],
                                                     size_t writes_count) {
    socket_data_.reset(
        new StaticSocketDataProvider(reads, reads_count, writes, writes_count));
    socket_data_->set_connect_data(MockConnect(SYNCHRONOUS, OK));
    factory_.AddSocketDataProvider(socket_data_.get());

    scoped_ptr<ClientSocketHandle> transport_socket(new ClientSocketHandle);
    scoped_refptr<MockTransportSocketParams> params;
    transport_socket->Init("a",
                           params,
                           MEDIUM,
                           CompletionCallback(),
                           &pool_,
                           bound_net_log_.bound());
    return transport_socket.Pass();
  }

  void SetHttpReadBuffer(const char* data, size_t size) {
    http_read_buffer_ = new GrowableIOBuffer;
    http_read_buffer_->SetCapacity(size);
    memcpy(http_read_buffer_->data(), data, size);
    http_read_buffer_->set_offset(size);
  }

  void CreateStream(MockRead reads[],
                    size_t reads_count,
                    MockWrite writes[],
                    size_t writes_count) {
    stream_ = WebSocketBasicStream::CreateWebSocketBasicStreamForTesting(
        MakeTransportSocket(reads, reads_count, writes, writes_count),
        http_read_buffer_,
        sub_protocol_,
        extensions_,
        &GenerateNulMaskingKey);
  }

  template <size_t N>
  void CreateReadOnly(MockRead (&reads)[N]) {
    CreateStream(reads, N, NULL, 0);
  }

  void CreateNullStream() { CreateStream(NULL, 0, NULL, 0); }

  scoped_ptr<SocketDataProvider> socket_data_;
  MockClientSocketFactory factory_;
  ClientSocketPoolHistograms histograms_;
  MockTransportClientSocketPool pool_;
  CapturingBoundNetLog(bound_net_log_);
  ScopedVector<WebSocketFrame> frames_;
  TestCompletionCallback cb_;
  scoped_refptr<GrowableIOBuffer> http_read_buffer_;
  std::string sub_protocol_;
  std::string extensions_;
};

// A test fixture for the common case of tests that only perform a single read.
class WebSocketBasicStreamSocketSingleReadTest
    : public WebSocketBasicStreamSocketTest {
 protected:
  void CreateRead(const MockRead& read) {
    reads_[0] = read;
    CreateStream(reads_, 1U, NULL, 0);
  }

  MockRead reads_[1];
};

// A test fixture for tests that perform chunked reads.
class WebSocketBasicStreamSocketChunkedReadTest
    : public WebSocketBasicStreamSocketTest {
 protected:
  // Specify the behaviour if there aren't enough chunks to use all the data. If
  // LAST_FRAME_BIG is specified, then the rest of the data will be
  // put in the last chunk. If LAST_FRAME_NOT_BIG is specified, then the last
  // frame will be no bigger than the rest of the frames (but it can be smaller,
  // if not enough data remains).
  enum LastFrameBehaviour {
    LAST_FRAME_BIG,
    LAST_FRAME_NOT_BIG
  };

  // Prepares a read from |data| of |data_size|, split into |number_of_chunks|,
  // each of |chunk_size| (except that the last chunk may be larger or
  // smaller). All reads must be either SYNCHRONOUS or ASYNC (not a mixture),
  // and errors cannot be simulated. Once data is exhausted, further reads will
  // return 0 (ie. connection closed).
  void CreateChunkedRead(IoMode mode,
                         const char data[],
                         size_t data_size,
                         int chunk_size,
                         int number_of_chunks,
                         LastFrameBehaviour last_frame_behaviour) {
    reads_.reset(new MockRead[number_of_chunks]);
    const char* start = data;
    for (int i = 0; i < number_of_chunks; ++i) {
      int len = chunk_size;
      const bool is_last_chunk = (i == number_of_chunks - 1);
      if ((last_frame_behaviour == LAST_FRAME_BIG && is_last_chunk) ||
          static_cast<int>(data + data_size - start) < len) {
        len = static_cast<int>(data + data_size - start);
      }
      reads_[i] = MockRead(mode, start, len);
      start += len;
    }
    CreateStream(reads_.get(), number_of_chunks, NULL, 0);
  }

  scoped_ptr<MockRead[]> reads_;
};

// Test fixture for write tests.
class WebSocketBasicStreamSocketWriteTest
    : public WebSocketBasicStreamSocketTest {
 protected:
  // All write tests use the same frame, so it is easiest to create it during
  // test creation.
  virtual void SetUp() OVERRIDE { PrepareWriteFrame(); }

  // Creates a WebSocketFrame with a wire format matching kWriteFrame and adds
  // it to |frames_|.
  void PrepareWriteFrame() {
    scoped_ptr<WebSocketFrame> frame(
        new WebSocketFrame(WebSocketFrameHeader::kOpCodeText));
    const size_t payload_size =
        kWriteFrameSize - (WebSocketFrameHeader::kBaseHeaderSize +
                           WebSocketFrameHeader::kMaskingKeyLength);
    frame->data = new IOBuffer(payload_size);
    memcpy(frame->data->data(),
           kWriteFrame + kWriteFrameSize - payload_size,
           payload_size);
    WebSocketFrameHeader& header = frame->header;
    header.final = true;
    header.masked = true;
    header.payload_length = payload_size;
    frames_.push_back(frame.release());
  }

  // Creates a stream that expects the listed writes.
  template <size_t N>
  void CreateWriteOnly(MockWrite (&writes)[N]) {
    CreateStream(NULL, 0, writes, N);
  }
};

TEST_F(WebSocketBasicStreamSocketTest, ConstructionWorks) {
  CreateNullStream();
}

TEST_F(WebSocketBasicStreamSocketSingleReadTest, SyncReadWorks) {
  CreateRead(MockRead(SYNCHRONOUS, kSampleFrame, kSampleFrameSize));
  int result = stream_->ReadFrames(&frames_, cb_.callback());
  EXPECT_EQ(OK, result);
  ASSERT_EQ(1U, frames_.size());
  EXPECT_EQ(GG_UINT64_C(6), frames_[0]->header.payload_length);
  EXPECT_TRUE(frames_[0]->header.final);
}

TEST_F(WebSocketBasicStreamSocketSingleReadTest, AsyncReadWorks) {
  CreateRead(MockRead(ASYNC, kSampleFrame, kSampleFrameSize));
  int result = stream_->ReadFrames(&frames_, cb_.callback());
  ASSERT_EQ(ERR_IO_PENDING, result);
  EXPECT_EQ(OK, cb_.WaitForResult());
  ASSERT_EQ(1U, frames_.size());
  EXPECT_EQ(GG_UINT64_C(6), frames_[0]->header.payload_length);
  // Don't repeat all the tests from SyncReadWorks; just enough to be sure the
  // frame was really read.
}

// ReadFrames will not return a frame whose header has not been wholly received.
TEST_F(WebSocketBasicStreamSocketChunkedReadTest, HeaderFragmentedSync) {
  CreateChunkedRead(
      SYNCHRONOUS, kSampleFrame, kSampleFrameSize, 1, 2, LAST_FRAME_BIG);
  int result = stream_->ReadFrames(&frames_, cb_.callback());
  ASSERT_EQ(OK, result);
  ASSERT_EQ(1U, frames_.size());
  EXPECT_EQ(GG_UINT64_C(6), frames_[0]->header.payload_length);
}

// The same behaviour applies to asynchronous reads.
TEST_F(WebSocketBasicStreamSocketChunkedReadTest, HeaderFragmentedAsync) {
  CreateChunkedRead(
      ASYNC, kSampleFrame, kSampleFrameSize, 1, 2, LAST_FRAME_BIG);
  int result = stream_->ReadFrames(&frames_, cb_.callback());
  ASSERT_EQ(ERR_IO_PENDING, result);
  EXPECT_EQ(OK, cb_.WaitForResult());
  ASSERT_EQ(1U, frames_.size());
  EXPECT_EQ(GG_UINT64_C(6), frames_[0]->header.payload_length);
}

// If it receives an incomplete header in a synchronous call, then has to wait
// for the rest of the frame, ReadFrames will return ERR_IO_PENDING.
TEST_F(WebSocketBasicStreamSocketTest, HeaderFragmentedSyncAsync) {
  MockRead reads[] = {MockRead(SYNCHRONOUS, kSampleFrame, 1),
                      MockRead(ASYNC, kSampleFrame + 1, kSampleFrameSize - 1)};
  CreateReadOnly(reads);
  int result = stream_->ReadFrames(&frames_, cb_.callback());
  ASSERT_EQ(ERR_IO_PENDING, result);
  EXPECT_EQ(OK, cb_.WaitForResult());
  ASSERT_EQ(1U, frames_.size());
  EXPECT_EQ(GG_UINT64_C(6), frames_[0]->header.payload_length);
}

// An extended header should also return ERR_IO_PENDING if it is not completely
// received.
TEST_F(WebSocketBasicStreamSocketTest, FragmentedLargeHeader) {
  MockRead reads[] = {
      MockRead(SYNCHRONOUS, kPartialLargeFrame, kLargeFrameHeaderSize - 1),
      MockRead(SYNCHRONOUS, ERR_IO_PENDING)};
  CreateReadOnly(reads);
  EXPECT_EQ(ERR_IO_PENDING, stream_->ReadFrames(&frames_, cb_.callback()));
}

// A frame that does not arrive in a single read should be broken into separate
// frames.
TEST_F(WebSocketBasicStreamSocketSingleReadTest, LargeFrameFirstChunk) {
  CreateRead(MockRead(SYNCHRONOUS, kPartialLargeFrame, kPartialLargeFrameSize));
  EXPECT_EQ(OK, stream_->ReadFrames(&frames_, cb_.callback()));
  ASSERT_EQ(1U, frames_.size());
  EXPECT_FALSE(frames_[0]->header.final);
  EXPECT_EQ(kPartialLargeFrameSize - kLargeFrameHeaderSize,
            static_cast<size_t>(frames_[0]->header.payload_length));
}

// If only the header of a data frame arrives, we should not receive a frame and
// be told to wait. WebSocketBasicStream does two reads in this case, as after
// the first read it has no frames to return.
TEST_F(WebSocketBasicStreamSocketTest, HeaderOnlyChunk) {
  MockRead reads[] = {
      MockRead(SYNCHRONOUS, kPartialLargeFrame, kLargeFrameHeaderSize),
      MockRead(SYNCHRONOUS, ERR_IO_PENDING)};
  CreateReadOnly(reads);
  EXPECT_EQ(ERR_IO_PENDING, stream_->ReadFrames(&frames_, cb_.callback()));
  ASSERT_EQ(0U, frames_.size());
}

// If the header and the body of a data frame arrive seperately, we should only
// see one frame.
TEST_F(WebSocketBasicStreamSocketTest, HeaderBodySeparated) {
  MockRead reads[] = {
      MockRead(SYNCHRONOUS, kPartialLargeFrame, kLargeFrameHeaderSize),
      MockRead(ASYNC,
               kPartialLargeFrame + kLargeFrameHeaderSize,
               kPartialLargeFrameSize - kLargeFrameHeaderSize)};
  CreateReadOnly(reads);
  EXPECT_EQ(ERR_IO_PENDING, stream_->ReadFrames(&frames_, cb_.callback()));
  EXPECT_EQ(OK, cb_.WaitForResult());
  ASSERT_EQ(1U, frames_.size());
  EXPECT_EQ(kPartialLargeFrameSize - kLargeFrameHeaderSize,
            frames_[0]->header.payload_length);
}

// If the header and body of a data frame arrive separately, the frame we see
// should have the opcode from the header (not Continuation).
TEST_F(WebSocketBasicStreamSocketTest, HeaderBodySeparatedOpCodeNotLost) {
  MockRead reads[] = {
      MockRead(ASYNC, kPartialLargeFrame, kLargeFrameHeaderSize),
      MockRead(ASYNC,
               kPartialLargeFrame + kLargeFrameHeaderSize,
               kPartialLargeFrameSize - kLargeFrameHeaderSize)};
  CreateReadOnly(reads);
  EXPECT_EQ(ERR_IO_PENDING, stream_->ReadFrames(&frames_, cb_.callback()));
  EXPECT_EQ(OK, cb_.WaitForResult());
  ASSERT_EQ(1U, frames_.size());
  EXPECT_EQ(WebSocketFrameHeader::kOpCodeText, frames_[0]->header.opcode);
}

// Every frame has a header with a correct payload_length field.
TEST_F(WebSocketBasicStreamSocketChunkedReadTest, LargeFrameTwoChunks) {
  const size_t kChunkSize = 16;
  CreateChunkedRead(ASYNC,
                    kPartialLargeFrame,
                    kPartialLargeFrameSize,
                    kChunkSize,
                    2,
                    LAST_FRAME_NOT_BIG);
  TestCompletionCallback cb[2];

  ASSERT_EQ(ERR_IO_PENDING, stream_->ReadFrames(&frames_, cb[0].callback()));
  EXPECT_EQ(OK, cb[0].WaitForResult());
  ASSERT_EQ(1U, frames_.size());
  EXPECT_EQ(kChunkSize - kLargeFrameHeaderSize,
            frames_[0]->header.payload_length);

  frames_.clear();
  ASSERT_EQ(ERR_IO_PENDING, stream_->ReadFrames(&frames_, cb[1].callback()));
  EXPECT_EQ(OK, cb[1].WaitForResult());
  ASSERT_EQ(1U, frames_.size());
  EXPECT_EQ(kChunkSize, frames_[0]->header.payload_length);
}

// Only the final frame of a fragmented message has |final| bit set.
TEST_F(WebSocketBasicStreamSocketChunkedReadTest, OnlyFinalChunkIsFinal) {
  static const size_t kFirstChunkSize = 4;
  CreateChunkedRead(ASYNC,
                    kSampleFrame,
                    kSampleFrameSize,
                    kFirstChunkSize,
                    2,
                    LAST_FRAME_BIG);
  TestCompletionCallback cb[2];

  ASSERT_EQ(ERR_IO_PENDING, stream_->ReadFrames(&frames_, cb[0].callback()));
  EXPECT_EQ(OK, cb[0].WaitForResult());
  ASSERT_EQ(1U, frames_.size());
  ASSERT_FALSE(frames_[0]->header.final);

  frames_.clear();
  ASSERT_EQ(ERR_IO_PENDING, stream_->ReadFrames(&frames_, cb[1].callback()));
  EXPECT_EQ(OK, cb[1].WaitForResult());
  ASSERT_EQ(1U, frames_.size());
  ASSERT_TRUE(frames_[0]->header.final);
}

// All frames after the first have their opcode changed to Continuation.
TEST_F(WebSocketBasicStreamSocketChunkedReadTest, ContinuationOpCodeUsed) {
  const size_t kFirstChunkSize = 3;
  const int kChunkCount = 3;
  // The input data is one frame with opcode Text, which arrives in three
  // separate chunks.
  CreateChunkedRead(ASYNC,
                    kSampleFrame,
                    kSampleFrameSize,
                    kFirstChunkSize,
                    kChunkCount,
                    LAST_FRAME_BIG);
  TestCompletionCallback cb[kChunkCount];

  ASSERT_EQ(ERR_IO_PENDING, stream_->ReadFrames(&frames_, cb[0].callback()));
  EXPECT_EQ(OK, cb[0].WaitForResult());
  ASSERT_EQ(1U, frames_.size());
  EXPECT_EQ(WebSocketFrameHeader::kOpCodeText, frames_[0]->header.opcode);

  // This test uses a loop to verify that the opcode for every frames generated
  // after the first is converted to Continuation.
  for (int i = 1; i < kChunkCount; ++i) {
    frames_.clear();
    ASSERT_EQ(ERR_IO_PENDING, stream_->ReadFrames(&frames_, cb[i].callback()));
    EXPECT_EQ(OK, cb[i].WaitForResult());
    ASSERT_EQ(1U, frames_.size());
    EXPECT_EQ(WebSocketFrameHeader::kOpCodeContinuation,
              frames_[0]->header.opcode);
  }
}

// Multiple frames that arrive together should be parsed correctly.
TEST_F(WebSocketBasicStreamSocketSingleReadTest, ThreeFramesTogether) {
  CreateRead(MockRead(SYNCHRONOUS, kMultipleFrames, kMultipleFramesSize));

  ASSERT_EQ(OK, stream_->ReadFrames(&frames_, cb_.callback()));
  ASSERT_EQ(3U, frames_.size());
  EXPECT_TRUE(frames_[0]->header.final);
  EXPECT_TRUE(frames_[1]->header.final);
  EXPECT_TRUE(frames_[2]->header.final);
}

// ERR_CONNECTION_CLOSED must be returned on close.
TEST_F(WebSocketBasicStreamSocketSingleReadTest, SyncClose) {
  CreateRead(MockRead(SYNCHRONOUS, "", 0));

  EXPECT_EQ(ERR_CONNECTION_CLOSED,
            stream_->ReadFrames(&frames_, cb_.callback()));
}

TEST_F(WebSocketBasicStreamSocketSingleReadTest, AsyncClose) {
  CreateRead(MockRead(ASYNC, "", 0));

  ASSERT_EQ(ERR_IO_PENDING, stream_->ReadFrames(&frames_, cb_.callback()));
  EXPECT_EQ(ERR_CONNECTION_CLOSED, cb_.WaitForResult());
}

// The result should be the same if the socket returns
// ERR_CONNECTION_CLOSED. This is not expected to happen on an established
// connection; a Read of size 0 is the expected behaviour. The key point of this
// test is to confirm that ReadFrames() behaviour is identical in both cases.
TEST_F(WebSocketBasicStreamSocketSingleReadTest, SyncCloseWithErr) {
  CreateRead(MockRead(SYNCHRONOUS, ERR_CONNECTION_CLOSED));

  EXPECT_EQ(ERR_CONNECTION_CLOSED,
            stream_->ReadFrames(&frames_, cb_.callback()));
}

TEST_F(WebSocketBasicStreamSocketSingleReadTest, AsyncCloseWithErr) {
  CreateRead(MockRead(ASYNC, ERR_CONNECTION_CLOSED));

  ASSERT_EQ(ERR_IO_PENDING, stream_->ReadFrames(&frames_, cb_.callback()));
  EXPECT_EQ(ERR_CONNECTION_CLOSED, cb_.WaitForResult());
}

TEST_F(WebSocketBasicStreamSocketSingleReadTest, SyncErrorsPassedThrough) {
  // ERR_INSUFFICIENT_RESOURCES here represents an arbitrary error that
  // WebSocketBasicStream gives no special handling to.
  CreateRead(MockRead(SYNCHRONOUS, ERR_INSUFFICIENT_RESOURCES));

  EXPECT_EQ(ERR_INSUFFICIENT_RESOURCES,
            stream_->ReadFrames(&frames_, cb_.callback()));
}

TEST_F(WebSocketBasicStreamSocketSingleReadTest, AsyncErrorsPassedThrough) {
  CreateRead(MockRead(ASYNC, ERR_INSUFFICIENT_RESOURCES));

  ASSERT_EQ(ERR_IO_PENDING, stream_->ReadFrames(&frames_, cb_.callback()));
  EXPECT_EQ(ERR_INSUFFICIENT_RESOURCES, cb_.WaitForResult());
}

// If we get a frame followed by a close, we should receive them separately.
TEST_F(WebSocketBasicStreamSocketChunkedReadTest, CloseAfterFrame) {
  // The chunk size equals the data size, so the second chunk is 0 size, closing
  // the connection.
  CreateChunkedRead(SYNCHRONOUS,
                    kSampleFrame,
                    kSampleFrameSize,
                    kSampleFrameSize,
                    2,
                    LAST_FRAME_NOT_BIG);

  EXPECT_EQ(OK, stream_->ReadFrames(&frames_, cb_.callback()));
  EXPECT_EQ(1U, frames_.size());
  frames_.clear();
  EXPECT_EQ(ERR_CONNECTION_CLOSED,
            stream_->ReadFrames(&frames_, cb_.callback()));
}

// Synchronous close after an async frame header is handled by a different code
// path.
TEST_F(WebSocketBasicStreamSocketTest, AsyncCloseAfterIncompleteHeader) {
  MockRead reads[] = {MockRead(ASYNC, kSampleFrame, 1U),
                      MockRead(SYNCHRONOUS, "", 0)};
  CreateReadOnly(reads);

  ASSERT_EQ(ERR_IO_PENDING, stream_->ReadFrames(&frames_, cb_.callback()));
  ASSERT_EQ(ERR_CONNECTION_CLOSED, cb_.WaitForResult());
}

// When Stream::Read returns ERR_CONNECTION_CLOSED we get the same result via a
// slightly different code path.
TEST_F(WebSocketBasicStreamSocketTest, AsyncErrCloseAfterIncompleteHeader) {
  MockRead reads[] = {MockRead(ASYNC, kSampleFrame, 1U),
                      MockRead(SYNCHRONOUS, ERR_CONNECTION_CLOSED)};
  CreateReadOnly(reads);

  ASSERT_EQ(ERR_IO_PENDING, stream_->ReadFrames(&frames_, cb_.callback()));
  ASSERT_EQ(ERR_CONNECTION_CLOSED, cb_.WaitForResult());
}

// If there was a frame read at the same time as the response headers (and the
// handshake succeeded), then we should parse it.
TEST_F(WebSocketBasicStreamSocketTest, HttpReadBufferIsUsed) {
  SetHttpReadBuffer(kSampleFrame, kSampleFrameSize);
  CreateNullStream();

  EXPECT_EQ(OK, stream_->ReadFrames(&frames_, cb_.callback()));
  ASSERT_EQ(1U, frames_.size());
  ASSERT_TRUE(frames_[0]->data);
  EXPECT_EQ(GG_UINT64_C(6), frames_[0]->header.payload_length);
}

// Check that a frame whose header partially arrived at the end of the response
// headers works correctly.
TEST_F(WebSocketBasicStreamSocketSingleReadTest,
       PartialFrameHeaderInHttpResponse) {
  SetHttpReadBuffer(kSampleFrame, 1);
  CreateRead(MockRead(ASYNC, kSampleFrame + 1, kSampleFrameSize - 1));

  ASSERT_EQ(ERR_IO_PENDING, stream_->ReadFrames(&frames_, cb_.callback()));
  EXPECT_EQ(OK, cb_.WaitForResult());
  ASSERT_EQ(1U, frames_.size());
  ASSERT_TRUE(frames_[0]->data);
  EXPECT_EQ(GG_UINT64_C(6), frames_[0]->header.payload_length);
  EXPECT_EQ(WebSocketFrameHeader::kOpCodeText, frames_[0]->header.opcode);
}

// Check that a control frame which partially arrives at the end of the response
// headers works correctly.
TEST_F(WebSocketBasicStreamSocketSingleReadTest,
       PartialControlFrameInHttpResponse) {
  const size_t kPartialFrameBytes = 3;
  SetHttpReadBuffer(kCloseFrame, kPartialFrameBytes);
  CreateRead(MockRead(ASYNC,
                      kCloseFrame + kPartialFrameBytes,
                      kCloseFrameSize - kPartialFrameBytes));

  ASSERT_EQ(ERR_IO_PENDING, stream_->ReadFrames(&frames_, cb_.callback()));
  EXPECT_EQ(OK, cb_.WaitForResult());
  ASSERT_EQ(1U, frames_.size());
  EXPECT_EQ(WebSocketFrameHeader::kOpCodeClose, frames_[0]->header.opcode);
  EXPECT_EQ(kCloseFrameSize - 2, frames_[0]->header.payload_length);
  EXPECT_EQ(
      0,
      memcmp(frames_[0]->data->data(), kCloseFrame + 2, kCloseFrameSize - 2));
}

// Check that a control frame which partially arrives at the end of the response
// headers works correctly. Synchronous version (unlikely in practice).
TEST_F(WebSocketBasicStreamSocketSingleReadTest,
       PartialControlFrameInHttpResponseSync) {
  const size_t kPartialFrameBytes = 3;
  SetHttpReadBuffer(kCloseFrame, kPartialFrameBytes);
  CreateRead(MockRead(SYNCHRONOUS,
                      kCloseFrame + kPartialFrameBytes,
                      kCloseFrameSize - kPartialFrameBytes));

  ASSERT_EQ(OK, stream_->ReadFrames(&frames_, cb_.callback()));
  ASSERT_EQ(1U, frames_.size());
  EXPECT_EQ(WebSocketFrameHeader::kOpCodeClose, frames_[0]->header.opcode);
}

// Check that an invalid frame results in an error.
TEST_F(WebSocketBasicStreamSocketSingleReadTest, SyncInvalidFrame) {
  CreateRead(MockRead(SYNCHRONOUS, kInvalidFrame, kInvalidFrameSize));

  EXPECT_EQ(ERR_WS_PROTOCOL_ERROR,
            stream_->ReadFrames(&frames_, cb_.callback()));
}

TEST_F(WebSocketBasicStreamSocketSingleReadTest, AsyncInvalidFrame) {
  CreateRead(MockRead(ASYNC, kInvalidFrame, kInvalidFrameSize));

  ASSERT_EQ(ERR_IO_PENDING, stream_->ReadFrames(&frames_, cb_.callback()));
  EXPECT_EQ(ERR_WS_PROTOCOL_ERROR, cb_.WaitForResult());
}

// A control frame without a FIN flag is invalid and should not be passed
// through to higher layers. RFC6455 5.5 "All control frames ... MUST NOT be
// fragmented."
TEST_F(WebSocketBasicStreamSocketSingleReadTest, ControlFrameWithoutFin) {
  CreateRead(
      MockRead(SYNCHRONOUS, kPingFrameWithoutFin, kPingFrameWithoutFinSize));

  ASSERT_EQ(ERR_WS_PROTOCOL_ERROR,
            stream_->ReadFrames(&frames_, cb_.callback()));
  EXPECT_TRUE(frames_.empty());
}

// A control frame over 125 characters is invalid. RFC6455 5.5 "All control
// frames MUST have a payload length of 125 bytes or less". Since we use a
// 125-byte buffer to assemble fragmented control frames, we need to detect this
// error before attempting to assemble the fragments.
TEST_F(WebSocketBasicStreamSocketSingleReadTest, OverlongControlFrame) {
  CreateRead(MockRead(SYNCHRONOUS, k126BytePong, k126BytePongSize));

  EXPECT_EQ(ERR_WS_PROTOCOL_ERROR,
            stream_->ReadFrames(&frames_, cb_.callback()));
  EXPECT_TRUE(frames_.empty());
}

// A control frame over 125 characters should still be rejected if it is split
// into multiple chunks.
TEST_F(WebSocketBasicStreamSocketChunkedReadTest, SplitOverlongControlFrame) {
  const size_t kFirstChunkSize = 16;
  CreateChunkedRead(SYNCHRONOUS,
                    k126BytePong,
                    k126BytePongSize,
                    kFirstChunkSize,
                    2,
                    LAST_FRAME_BIG);

  EXPECT_EQ(ERR_WS_PROTOCOL_ERROR,
            stream_->ReadFrames(&frames_, cb_.callback()));
  EXPECT_TRUE(frames_.empty());
}

TEST_F(WebSocketBasicStreamSocketChunkedReadTest,
       AsyncSplitOverlongControlFrame) {
  const size_t kFirstChunkSize = 16;
  CreateChunkedRead(ASYNC,
                    k126BytePong,
                    k126BytePongSize,
                    kFirstChunkSize,
                    2,
                    LAST_FRAME_BIG);

  ASSERT_EQ(ERR_IO_PENDING, stream_->ReadFrames(&frames_, cb_.callback()));
  EXPECT_EQ(ERR_WS_PROTOCOL_ERROR, cb_.WaitForResult());
  // The caller should not call ReadFrames() again after receiving an error
  // other than ERR_IO_PENDING.
  EXPECT_TRUE(frames_.empty());
}

// In the synchronous case, ReadFrames assembles the whole control frame before
// returning.
TEST_F(WebSocketBasicStreamSocketChunkedReadTest, SyncControlFrameAssembly) {
  const size_t kChunkSize = 3;
  CreateChunkedRead(
      SYNCHRONOUS, kCloseFrame, kCloseFrameSize, kChunkSize, 3, LAST_FRAME_BIG);

  ASSERT_EQ(OK, stream_->ReadFrames(&frames_, cb_.callback()));
  ASSERT_EQ(1U, frames_.size());
  EXPECT_EQ(WebSocketFrameHeader::kOpCodeClose, frames_[0]->header.opcode);
}

// In the asynchronous case, the callback is not called until the control frame
// has been completely assembled.
TEST_F(WebSocketBasicStreamSocketChunkedReadTest, AsyncControlFrameAssembly) {
  const size_t kChunkSize = 3;
  CreateChunkedRead(
      ASYNC, kCloseFrame, kCloseFrameSize, kChunkSize, 3, LAST_FRAME_BIG);

  ASSERT_EQ(ERR_IO_PENDING, stream_->ReadFrames(&frames_, cb_.callback()));
  ASSERT_EQ(OK, cb_.WaitForResult());
  ASSERT_EQ(1U, frames_.size());
  EXPECT_EQ(WebSocketFrameHeader::kOpCodeClose, frames_[0]->header.opcode);
}

// Check that writing a frame all at once works.
TEST_F(WebSocketBasicStreamSocketWriteTest, WriteAtOnce) {
  MockWrite writes[] = {MockWrite(SYNCHRONOUS, kWriteFrame, kWriteFrameSize)};
  CreateWriteOnly(writes);

  EXPECT_EQ(OK, stream_->WriteFrames(&frames_, cb_.callback()));
}

// Check that completely async writing works.
TEST_F(WebSocketBasicStreamSocketWriteTest, AsyncWriteAtOnce) {
  MockWrite writes[] = {MockWrite(ASYNC, kWriteFrame, kWriteFrameSize)};
  CreateWriteOnly(writes);

  ASSERT_EQ(ERR_IO_PENDING, stream_->WriteFrames(&frames_, cb_.callback()));
  EXPECT_EQ(OK, cb_.WaitForResult());
}

// Check that writing a frame to an extremely full kernel buffer (so that it
// ends up being sent in bits) works. The WriteFrames() callback should not be
// called until all parts have been written.
TEST_F(WebSocketBasicStreamSocketWriteTest, WriteInBits) {
  MockWrite writes[] = {MockWrite(SYNCHRONOUS, kWriteFrame, 4),
                        MockWrite(ASYNC, kWriteFrame + 4, 4),
                        MockWrite(ASYNC, kWriteFrame + 8, kWriteFrameSize - 8)};
  CreateWriteOnly(writes);

  ASSERT_EQ(ERR_IO_PENDING, stream_->WriteFrames(&frames_, cb_.callback()));
  EXPECT_EQ(OK, cb_.WaitForResult());
}

TEST_F(WebSocketBasicStreamSocketTest, GetExtensionsWorks) {
  extensions_ = "inflate-uuencode";
  CreateNullStream();

  EXPECT_EQ("inflate-uuencode", stream_->GetExtensions());
}

TEST_F(WebSocketBasicStreamSocketTest, GetSubProtocolWorks) {
  sub_protocol_ = "cyberchat";
  CreateNullStream();

  EXPECT_EQ("cyberchat", stream_->GetSubProtocol());
}

}  // namespace
}  // namespace net
