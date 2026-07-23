#ifndef NEXUS_RPC_STATUS_H_
#define NEXUS_RPC_STATUS_H_

#include <optional>
#include <string>
#include <utility>

namespace nexus::rpc {

/**
 * Canonical status values returned by the NexusRPC v1 unary transport.
 *
 * The values are deliberately independent from HTTP and JSON-RPC error codes.
 * Protocol adapters are responsible for their own error mapping.
 */
enum class StatusCode {
  kOk = 0,
  kInvalidArgument,
  kDeadlineExceeded,
  kNotFound,
  kResourceExhausted,
  kUnsupportedCompression,
  kUnavailable,
  kInternal,
};

/** Represents a successful or failed RPC operation. */
class Status {
 public:
  Status() = default;
  Status(StatusCode code, std::string message)
      : code_(code), message_(std::move(message)) {}

  /** Returns a successful status. */
  static Status Ok() { return Status(); }

  /** Returns whether the operation succeeded. */
  bool ok() const noexcept { return code_ == StatusCode::kOk; }

  /** Returns the machine-readable status code. */
  StatusCode code() const noexcept { return code_; }

  /** Returns the human-readable diagnostic message. */
  const std::string& message() const noexcept { return message_; }

 private:
  StatusCode code_ = StatusCode::kOk;
  std::string message_;
};

/** Holds either a value or an operation status. */
template <typename T>
class Result {
 public:
  Result(Status status) : status_(std::move(status)) {}
  Result(T value) : status_(Status::Ok()), value_(std::move(value)) {}

  /** Returns whether a value is available. */
  bool ok() const noexcept { return status_.ok(); }

  /** Returns the result status. */
  const Status& status() const noexcept { return status_; }

  /** Returns the result value. The caller must first check ok(). */
  const T& value() const& { return *value_; }
  T& value() & { return *value_; }
  T&& value() && { return std::move(*value_); }

 private:
  Status status_;
  std::optional<T> value_;
};

}  // namespace nexus::rpc

#endif  // NEXUS_RPC_STATUS_H_
