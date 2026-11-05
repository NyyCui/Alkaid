/** @file data_store.h 应用私有数据与选定文本文件，所有方法在同一后台线程调用。 */
#ifndef ALKAID_DATA_STORE_H
#define ALKAID_DATA_STORE_H
#include "package_types.h"
#include "file_access.h"
#include <atomic>
struct sqlite3;
namespace alkaid {
struct DataLease {
  std::atomic_bool active{true};
  RuntimeClock::time_point deadline;
  bool valid() const { return active.load() && RuntimeClock::now() < deadline; }
};
class DataStore {
public:
  static constexpr std::size_t MAX_TEXT = 128 * 1024;
  static constexpr std::size_t CHUNK_BYTES = 64 * 1024;
  static constexpr uint64_t MAX_STAGED_BYTES = 8ULL * 1024 * 1024 * 1024;
  static constexpr uint64_t MAX_PRIVATE_BYTES = 64ULL * 1024 * 1024;
  explicit DataStore(std::filesystem::path root, bool config = false) : root_(std::move(root)), config_(config) {}
  ~DataStore();
  static bool validParams(const std::string &method, const Json &params);
  RuntimeResult<Json> call(const std::string &method, const Json &params, uint64_t session, const DataLease &lease);
  RuntimeResult<Json> grant(const std::filesystem::path &path, bool writable, uint64_t session, const DataLease &lease);
  void revoke(uint64_t session);
  void release(const std::string &handle);
  void expireStreams();
  void releaseTransfer(const std::string &id) { releaseStream(id); }
private:
  struct Grant {
    uint64_t session;
    bool writable;
    std::filesystem::path path;
    std::vector<FileAccess::Handle> pins;
    FileAccess::Handle file;
    std::string contentHash;
    BY_HANDLE_FILE_INFORMATION snapshot{};
  };
  struct Stream {
    uint64_t session = 0, offset = 0, size = 0, index = 0;
    bool writing = false, first = true, privateFile = false;
    std::string handle, name, encoding, carry, revision;
    FileAccess::Handle file;
    std::filesystem::path temporary;
    RuntimeClock::time_point touched = RuntimeClock::now();
  };
  RuntimeResult<Json> streamCall(const std::string &method, const Json &params, uint64_t session, const DataLease &lease);
  RuntimeResult<Json> beginStream(const std::string &method, const Json &params, uint64_t session, const DataLease &lease);
  RuntimeResult<Json> readStream(Stream &stream);
  RuntimeResult<Json> appendStream(Stream &stream, const Json &params);
  RuntimeResult<Json> commitStream(Stream &stream, const DataLease &lease);
  RuntimeResult<Json> commitPrivate(Stream &stream, const DataLease &lease);
  RuntimeResult<Json> privateSnapshot(const std::string &name);
  void releaseStream(const std::string &id);
  std::optional<RuntimeError> unchanged(Grant &grant);
  std::optional<RuntimeError> initialize();
  RuntimeResult<Json> privateCall(const std::string &method, const Json &params, const DataLease &lease);
  RuntimeResult<Json> read(Grant &grant);
  RuntimeResult<Json> write(Grant &grant, const std::string &text, const DataLease &lease);
  std::filesystem::path root_;
  const bool config_;
  sqlite3 *db_ = nullptr;
  std::vector<FileAccess::Handle> pins_;
  std::map<std::string, Grant> handles_;
  std::map<std::string, Stream> streams_;
};
}
#endif // ALKAID_DATA_STORE_H
