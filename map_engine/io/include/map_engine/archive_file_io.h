#pragma once

#include "map_engine/io_read_channel.h"

#include <filesystem>
#include <string>

/**
 * 压缩包内单条目读取占位（zip 等）：``archivePath`` 为包路径，``entryPathUtf8`` 为包内逻辑路径。
 * 后续可接 minizip/libzip 或 GDAL VSI 虚拟文件系统。
 */
class ArchiveEntryIo final : public IoReadChannel {
 public:
  ArchiveEntryIo(std::filesystem::path archivePath, std::string entryPathUtf8);

  IoResult readAllBlocking() override;

  const std::filesystem::path& archivePath() const { return archivePath_; }
  const std::string& entryPathUtf8() const { return entryUtf8_; }

 private:
  std::filesystem::path archivePath_;
  std::string entryUtf8_;
};
