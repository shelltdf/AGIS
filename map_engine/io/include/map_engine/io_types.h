#pragma once

#include <cstdint>
#include <string>
#include <vector>

/** 调用方期望的调度方式（具体通道可在阻塞 API 之外另提供异步封装）。 */
enum class IoAccessMode : std::uint8_t { kBlocking = 0, kAsync = 1 };

/** 数据来源类别，便于统一日志与重试策略。 */
enum class IoSourceKind : std::uint8_t { kLocalFile = 0, kRemoteUrl = 1, kArchiveEntry = 2 };

/** 一次读操作的结果；大文件场景后续可改为路径 + 句柄，避免整文件进内存。 */
struct IoResult {
  bool ok{false};
  std::string error;
  std::vector<std::uint8_t> bytes;
};
