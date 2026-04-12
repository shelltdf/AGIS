#include "map_engine_demo/platform.h"

#if defined(_WIN32)
#  include <windows.h>
#endif

namespace map_engine_demo {

#if !defined(_WIN32)
/** 非 Windows：当前仓库演示仅实现 Win32 ``CreatePlatform``；接入本机 Platform 后在此构造并 ``RunMapEngineDemo``。 */
inline int RunDemoEntryPosix(int argc, char** argv) {
  (void)argc;
  (void)argv;
  return 1;
}
#endif

}  // namespace map_engine_demo

#if defined(_WIN32)

int APIENTRY wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int nCmdShow) {
  auto platform = map_engine_demo::CreatePlatform(static_cast<void*>(hInst), nCmdShow);
  return map_engine_demo::RunMapEngineDemo(*platform);
}

#elif defined(__APPLE__)

int main(int argc, char** argv) {
  return map_engine_demo::RunDemoEntryPosix(argc, argv);
}

#elif defined(__linux__)

int main(int argc, char** argv) {
  return map_engine_demo::RunDemoEntryPosix(argc, argv);
}

#else

int main(int argc, char** argv) {
  return map_engine_demo::RunDemoEntryPosix(argc, argv);
}

#endif
