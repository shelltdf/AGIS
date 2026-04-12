#include "map_engine/archive_file_io.h"
#include "map_engine/io_read_channel.h"
#include "map_engine/local_file_io.h"
#include "map_engine/remote_file_io.h"

#include <fstream>
#include <future>

namespace {

constexpr std::streamoff kIoMaxReadAllBytes =
    static_cast<std::streamoff>(256) * 1024 * 1024;  // 256 MiB  guard，防误读巨型文件撑爆内存

}  // namespace

std::future<IoResult> IoReadAsync(std::shared_ptr<IoReadChannel> channel) {
  return std::async(std::launch::async, [ch = std::move(channel)]() mutable { return ch->readAllBlocking(); });
}

LocalFileIo::LocalFileIo(std::filesystem::path path) : path_(std::move(path)) {}

IoResult LocalFileIo::readAllBlocking() {
  IoResult r;
  std::ifstream in(path_, std::ios::binary);
  if (!in) {
    r.error = "LocalFileIo: open failed";
    return r;
  }
  in.seekg(0, std::ios::end);
  const std::streamoff sz = in.tellg();
  if (sz < 0) {
    r.error = "LocalFileIo: tellg failed";
    return r;
  }
  if (sz > kIoMaxReadAllBytes) {
    r.error = "LocalFileIo: file too large for readAll (see kIoMaxReadAllBytes)";
    return r;
  }
  in.seekg(0, std::ios::beg);
  r.bytes.resize(static_cast<size_t>(sz));
  if (sz > 0 && !in.read(reinterpret_cast<char*>(r.bytes.data()), sz)) {
    r.bytes.clear();
    r.error = "LocalFileIo: read failed";
    return r;
  }
  r.ok = true;
  return r;
}

RemoteFileIo::RemoteFileIo(std::string urlUtf8, IoRemoteOptions options)
    : url_(std::move(urlUtf8)), options_(options) {}

IoResult RemoteFileIo::readAllBlocking() {
  IoResult r;
  (void)options_;
  r.error = "RemoteFileIo: HTTP client not wired (use cacheDir/useDiskCache when implemented)";
  return r;
}

ArchiveEntryIo::ArchiveEntryIo(std::filesystem::path archivePath, std::string entryPathUtf8)
    : archivePath_(std::move(archivePath)), entryUtf8_(std::move(entryPathUtf8)) {}

IoResult ArchiveEntryIo::readAllBlocking() {
  IoResult r;
  (void)archivePath_;
  (void)entryUtf8_;
  r.error = "ArchiveEntryIo: archive backend not wired";
  return r;
}
