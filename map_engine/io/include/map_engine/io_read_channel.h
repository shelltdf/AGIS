#pragma once

#include "map_engine/io_types.h"

#include <future>
#include <memory>

/**
 * 统一读通道抽象：本地路径、远程 URL、压缩包内条目等均可实现此接口。
 * 阻塞读为基线；``IoReadAsync`` 用 ``std::async`` 包装，便于渐进替换为线程池 / IOCP。
 */
class IoReadChannel {
 public:
  virtual ~IoReadChannel() = default;
  virtual IoResult readAllBlocking() = 0;
};

/** 在线程中执行 ``readAllBlocking``；调用方需保证通道在 future 完成前有效（故使用 ``shared_ptr``）。 */
std::future<IoResult> IoReadAsync(std::shared_ptr<IoReadChannel> channel);
