// Copyright 2021 gRPC authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef GRPC_CORE_EXT_TRANSPORT_BINDER_WIRE_FORMAT_WIRE_READER_IMPL_H
#define GRPC_CORE_EXT_TRANSPORT_BINDER_WIRE_FORMAT_WIRE_READER_IMPL_H

#include <grpc/impl/codegen/port_platform.h>

#include <memory>
#include <utility>

#include "absl/container/flat_hash_map.h"
#include "absl/synchronization/notification.h"
#include "src/core/ext/transport/binder/utils/transport_stream_receiver.h"
#include "src/core/ext/transport/binder/wire_format/binder.h"
#include "src/core/ext/transport/binder/wire_format/wire_reader.h"
#include "src/core/ext/transport/binder/wire_format/wire_writer.h"

namespace grpc_binder {

class WireReaderImpl : public WireReader {
 public:
  WireReaderImpl(
      std::shared_ptr<TransportStreamReceiver> transport_stream_receiver,
      bool is_client, std::function<void()> on_destruct_callback = nullptr);
  ~WireReaderImpl() override;

  void Orphan() override { Unref(); }

  /// Setup the transport between endpoint binders.
  ///
  /// The client and the server both call SetupTransport() when constructing
  /// transport.
  ///
  /// High-level overview of transaction setup:
  /// 0. Client obtains an |endpoint_binder| from the server (in the Android
  /// setting, this can be achieved by "binding" to the server APK).
  /// 1. Client creates a binder |client_binder| and hook its on-transaction
  /// callback to client's ProcessTransaction(). Client then sends
  /// |client_binder| through |endpoint_binder| to server.
  /// 2. Server receives |client_binder| via |endpoint_binder|.
  /// 3. Server creates a binder |server_binder| and hook its on-transaction
  /// callback to server's ProcessTransaction(). Server then sends
  /// |server_binder| through |client_binder| back to the client.
  /// 4. Client receives |server_binder| via |client_binder|'s on-transaction
  /// callback.
  ///
  /// The parameter \p binder here means different things for client nad server.
  /// For client, \p binder refers to |endpoint_binder|, and for server, \p
  /// binder refers to |client_binder|. That is, for server-side transport
  /// setup, we assume that the first half of SETUP_TRANSPORT (up to step 2) is
  /// already done somewhere else (see test/end2end/binder_transport_test.cc for
  /// how it's handled in the testing environment).
  std::shared_ptr<WireWriter> SetupTransport(
      std::unique_ptr<Binder> binder) override;

  absl::Status ProcessTransaction(transaction_code_t code,
                                  const ReadableParcel* parcel);

  /// Send SETUP_TRANSPORT request through \p binder.
  ///
  /// This is the one half (for client it's the first half, and for server it's
  /// the second) of the SETUP_TRANSPORT negotiation process. First, a new
  /// binder is created. We take its "receiving" part and construct the
  /// transaction receiver with it, and sends the "sending" part along with the
  /// SETUP_TRANSPORT message through \p binder.
  void SendSetupTransport(Binder* binder);

  /// Recv SETUP_TRANSPORT request.
  ///
  /// This is the other half of the SETUP_TRANSPORT process. We wait for
  /// in-coming SETUP_TRANSPORT request with the "sending" part of a binder from
  /// the other end. For client, the message is coming from the trasnaction
  /// receiver we just constructed in SendSetupTransport(). For server, we
  /// assume that this step is already completed.
  // TODO(waynetu): In the testing environment, we still use this method (on
  // another WireReader instance) for server-side transport setup, and thus it
  // is marked as public. Try moving this method back to private, and hopefully
  // we can also avoid moving |other_end_binder_| out in the implementation.
  std::unique_ptr<Binder> RecvSetupTransport();

 private:
  absl::Status ProcessStreamingTransaction(transaction_code_t code,
                                           const ReadableParcel* parcel);
  absl::Status ProcessStreamingTransactionImpl(transaction_code_t code,
                                               const ReadableParcel* parcel,
                                               int* cancellation_flags);

  std::shared_ptr<TransportStreamReceiver> transport_stream_receiver_;
  absl::Notification connection_noti_;
  bool connected_ = false;
  // NOTE: other_end_binder_ will be moved out when RecvSetupTransport() is
  // called. Be cautious not to access it afterward.
  std::unique_ptr<Binder> other_end_binder_;
  absl::flat_hash_map<transaction_code_t, int32_t> expected_seq_num_;
  absl::flat_hash_map<transaction_code_t, std::string> message_buffer_;
  std::unique_ptr<TransactionReceiver> tx_receiver_;
  bool is_client_;
  // When WireReaderImpl gets destructed, call on_destruct_callback_. This is
  // mostly for decrementing the reference count of its transport.
  std::function<void()> on_destruct_callback_;

  // ACK every 16k bytes.
  static constexpr int64_t kFlowControlAckBytes = 16 * 1024;
  int64_t num_incoming_bytes_ = 0;
  int64_t num_acknowledged_bytes_ = 0;

  // Used to send ACK.
  std::shared_ptr<WireWriter> wire_writer_;
};

}  // namespace grpc_binder

#endif  // GRPC_CORE_EXT_TRANSPORT_BINDER_WIRE_FORMAT_WIRE_READER_IMPL_H
