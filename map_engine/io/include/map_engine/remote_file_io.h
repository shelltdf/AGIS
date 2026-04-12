#pragma once

#include "map_engine/io_read_channel.h"

#include <filesystem>
#include <string>

/**
 * 远程资源选项：可将下载落盘到 ``cacheDir`` 下做本地缓冲，重复访问命中缓存；
 * 未配置 ``cacheDir`` 时由实现决定仅内存或临时文件（当前占位实现未接 HTTP）。
 */
struct IoRemoteOptions {
  std::filesystem::path cacheDir{};
  bool useDiskCache{true};
};

/**
 * 远程文件（HTTP/HTTPS 等）读取占位：后续可接 WinHTTP/curl，并配合 ``IoRemoteOptions`` 写缓存。
 */
class RemoteFileIo final : public IoReadChannel {
 public:
  explicit RemoteFileIo(std::string urlUtf8, IoRemoteOptions options = {});

  IoResult readAllBlocking() override;

  const std::string& urlUtf8() const { return url_; }
  const IoRemoteOptions& options() const { return options_; }

 private:
  std::string url_;
  IoRemoteOptions options_;
};
