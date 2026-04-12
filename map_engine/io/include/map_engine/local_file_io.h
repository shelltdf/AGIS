#pragma once

#include "map_engine/io_read_channel.h"

#include <filesystem>

/** 本地文件阻塞读取（整文件读入 ``IoResult::bytes``）。 */
class LocalFileIo final : public IoReadChannel {
 public:
  explicit LocalFileIo(std::filesystem::path path);

  IoResult readAllBlocking() override;

  const std::filesystem::path& path() const { return path_; }

 private:
  std::filesystem::path path_;
};
