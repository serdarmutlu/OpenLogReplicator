// Generated by the gRPC C++ plugin.
// If you make any local change, they will be lost.
// source: OraProtoBuf.proto

#include "OraProtoBuf.pb.h"
#include "OraProtoBuf.grpc.pb.h"

#include <functional>
#include <grpcpp/impl/codegen/async_stream.h>
#include <grpcpp/impl/codegen/async_unary_call.h>
#include <grpcpp/impl/codegen/channel_interface.h>
#include <grpcpp/impl/codegen/client_unary_call.h>
#include <grpcpp/impl/codegen/client_callback.h>
#include <grpcpp/impl/codegen/message_allocator.h>
#include <grpcpp/impl/codegen/method_handler.h>
#include <grpcpp/impl/codegen/rpc_service_method.h>
#include <grpcpp/impl/codegen/server_callback.h>
#include <grpcpp/impl/codegen/server_callback_handlers.h>
#include <grpcpp/impl/codegen/server_context.h>
#include <grpcpp/impl/codegen/service_type.h>
#include <grpcpp/impl/codegen/sync_stream.h>
namespace OpenLogReplicator {
namespace pb {

static const char* RedoStreamService_method_names[] = {
  "/OpenLogReplicator.pb.RedoStreamService/RedoStream",
};

std::unique_ptr< RedoStreamService::Stub> RedoStreamService::NewStub(const std::shared_ptr< ::grpc::ChannelInterface>& channel, const ::grpc::StubOptions& options) {
  (void)options;
  std::unique_ptr< RedoStreamService::Stub> stub(new RedoStreamService::Stub(channel));
  return stub;
}

RedoStreamService::Stub::Stub(const std::shared_ptr< ::grpc::ChannelInterface>& channel)
  : channel_(channel), rpcmethod_RedoStream_(RedoStreamService_method_names[0], ::grpc::internal::RpcMethod::BIDI_STREAMING, channel)
  {}

::grpc::ClientReaderWriter< ::OpenLogReplicator::pb::Request, ::OpenLogReplicator::pb::Response>* RedoStreamService::Stub::RedoStreamRaw(::grpc::ClientContext* context) {
  return ::grpc_impl::internal::ClientReaderWriterFactory< ::OpenLogReplicator::pb::Request, ::OpenLogReplicator::pb::Response>::Create(channel_.get(), rpcmethod_RedoStream_, context);
}

void RedoStreamService::Stub::experimental_async::RedoStream(::grpc::ClientContext* context, ::grpc::experimental::ClientBidiReactor< ::OpenLogReplicator::pb::Request,::OpenLogReplicator::pb::Response>* reactor) {
  ::grpc_impl::internal::ClientCallbackReaderWriterFactory< ::OpenLogReplicator::pb::Request,::OpenLogReplicator::pb::Response>::Create(stub_->channel_.get(), stub_->rpcmethod_RedoStream_, context, reactor);
}

::grpc::ClientAsyncReaderWriter< ::OpenLogReplicator::pb::Request, ::OpenLogReplicator::pb::Response>* RedoStreamService::Stub::AsyncRedoStreamRaw(::grpc::ClientContext* context, ::grpc::CompletionQueue* cq, void* tag) {
  return ::grpc_impl::internal::ClientAsyncReaderWriterFactory< ::OpenLogReplicator::pb::Request, ::OpenLogReplicator::pb::Response>::Create(channel_.get(), cq, rpcmethod_RedoStream_, context, true, tag);
}

::grpc::ClientAsyncReaderWriter< ::OpenLogReplicator::pb::Request, ::OpenLogReplicator::pb::Response>* RedoStreamService::Stub::PrepareAsyncRedoStreamRaw(::grpc::ClientContext* context, ::grpc::CompletionQueue* cq) {
  return ::grpc_impl::internal::ClientAsyncReaderWriterFactory< ::OpenLogReplicator::pb::Request, ::OpenLogReplicator::pb::Response>::Create(channel_.get(), cq, rpcmethod_RedoStream_, context, false, nullptr);
}

RedoStreamService::Service::Service() {
  AddMethod(new ::grpc::internal::RpcServiceMethod(
      RedoStreamService_method_names[0],
      ::grpc::internal::RpcMethod::BIDI_STREAMING,
      new ::grpc::internal::BidiStreamingHandler< RedoStreamService::Service, ::OpenLogReplicator::pb::Request, ::OpenLogReplicator::pb::Response>(
          [](RedoStreamService::Service* service,
             ::grpc::ServerContext* ctx,
             ::grpc_impl::ServerReaderWriter<::OpenLogReplicator::pb::Response,
             ::OpenLogReplicator::pb::Request>* stream) {
               return service->RedoStream(ctx, stream);
             }, this)));
}

RedoStreamService::Service::~Service() {
}

::grpc::Status RedoStreamService::Service::RedoStream(::grpc::ServerContext* context, ::grpc::ServerReaderWriter< ::OpenLogReplicator::pb::Response, ::OpenLogReplicator::pb::Request>* stream) {
  (void) context;
  (void) stream;
  return ::grpc::Status(::grpc::StatusCode::UNIMPLEMENTED, "");
}


}  // namespace OpenLogReplicator
}  // namespace pb

