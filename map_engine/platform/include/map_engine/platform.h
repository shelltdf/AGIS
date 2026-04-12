#pragma once

#include <memory>

class MapEngine;

namespace map_engine_demo {

/**
 * 演示程序平台抽象：本机窗口、事件循环与每帧调度，供 ``RunMapEngineDemo`` 使用。
 *
 * 移植新系统时实现 ``Init`` / ``Step`` / ``Shutdown``；引擎与主循环调度由 ``RunMapEngineDemo`` 负责；
 * 本机图形栈（如 Win32 的 GDI+）放在对应平台实现的 ``Init`` / ``Shutdown`` 中。
 */
class Platform {
 public:
  virtual ~Platform() = default;

  virtual bool Init(MapEngine& engine) = 0;

  /**
   * 单帧：排空本机消息队列；队列空时执行一帧 ``DefaultMapView`` 的 cull/draw，再阻塞等待下一条消息。
   * @return 0 继续；非 0 为进程退出码。
   */
  virtual int Step(MapEngine& engine) = 0;

  /** 释放本机资源。勿依赖此处再次 ``MapEngine::Shutdown``（宿主 WM_DESTROY 通常已调用）。 */
  virtual void Shutdown(MapEngine& engine) = 0;
};

/**
 * 构造当前编译目标下的 ``Platform`` 实现。
 *
 * **Win32**：``instance`` 为 ``HINSTANCE``（按 ``void*`` 传入），``nCmdShow`` 同 ``wWinMain``。
 */
#if defined(_WIN32)
[[nodiscard]] std::unique_ptr<Platform> CreatePlatform(void* instance, int nCmdShow);
#endif

/** 引擎单例与主循环；窗口与图形栈由 ``Platform`` 完成。 */
[[nodiscard]] int RunMapEngineDemo(Platform& platform);

}  // namespace map_engine_demo
