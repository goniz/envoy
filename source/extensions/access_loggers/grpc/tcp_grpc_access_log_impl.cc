#include "source/extensions/access_loggers/grpc/tcp_grpc_access_log_impl.h"

#include "envoy/data/accesslog/v3/accesslog.pb.h"
#include "envoy/extensions/access_loggers/grpc/v3/als.pb.h"

#include "source/common/common/assert.h"
#include "source/common/config/utility.h"
#include "source/common/network/utility.h"
#include "source/common/stream_info/utility.h"
#include "source/extensions/access_loggers/grpc/grpc_access_log_utils.h"

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {
namespace TcpGrpc {

TcpGrpcAccessLog::ThreadLocalLogger::ThreadLocalLogger(GrpcCommon::GrpcAccessLoggerSharedPtr logger)
    : logger_(std::move(logger)) {}

TcpGrpcAccessLog::TcpGrpcAccessLog(AccessLog::FilterPtr&& filter,
                                   const TcpGrpcAccessLogConfig config,
                                   ThreadLocal::SlotAllocator& tls,
                                   GrpcCommon::GrpcAccessLoggerCacheSharedPtr access_logger_cache,
                                   Stats::Scope& scope)
    : Common::ImplBase(std::move(filter)), scope_(scope),
      config_(std::make_shared<const TcpGrpcAccessLogConfig>(std::move(config))),
      tls_slot_(tls.allocateSlot()), access_logger_cache_(std::move(access_logger_cache)) {
  Config::Utility::checkTransportVersion(config_->common_config());
  // Note that &scope might have died by the time when this callback is called on each thread.
  // This is supposed to be fixed by https://github.com/envoyproxy/envoy/issues/18066.
  tls_slot_->set([config = config_, access_logger_cache = access_logger_cache_,
                  &scope = scope_](Event::Dispatcher&) {
    return std::make_shared<ThreadLocalLogger>(access_logger_cache->getOrCreateLogger(
        config->common_config(), Common::GrpcAccessLoggerType::TCP, scope));
  });
}

void TcpGrpcAccessLog::emitLog(const Http::RequestHeaderMap&, const Http::ResponseHeaderMap&,
                               const Http::ResponseTrailerMap&,
                               const StreamInfo::StreamInfo& stream_info) {
  // Common log properties.
  envoy::data::accesslog::v3::TCPAccessLogEntry log_entry;
  GrpcCommon::Utility::extractCommonAccessLogProperties(*log_entry.mutable_common_properties(),
                                                        stream_info, config_->common_config());

  envoy::data::accesslog::v3::ConnectionProperties& connection_properties =
      *log_entry.mutable_connection_properties();
  connection_properties.set_received_bytes(stream_info.bytesReceived());
  connection_properties.set_sent_bytes(stream_info.bytesSent());

  // request_properties->set_request_body_bytes(stream_info.bytesReceived());
  tls_slot_->getTyped<ThreadLocalLogger>().logger_->log(std::move(log_entry));
}

} // namespace TcpGrpc
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
