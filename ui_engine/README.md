# ui_engine（`agis_ui_engine`）

跨平台抽象 GUI 库（`agis::ui`：`App`、`Widget`、`IGuiPlatform` 等）。由 [`gis-desktop-win32/CMakeLists.txt`](../gis-desktop-win32/CMakeLists.txt) 构建为静态或动态库。

对外包含方式不变：`#include "ui_engine/app.h"` 等。

---

## 模块目录（先分模块，再各模块 `include` + `src`）

| 模块 | 公开头路径 | 源文件 | 说明 |
|------|------------|--------|------|
| **core** | `core/include/ui_engine/` | `core/src/` | `App`、`Widget`、`platform_gui`、`export`、`app_launch_params`、`ui_types`、`widget_core` |
| **widgets** | `widgets/include/ui_engine/` | `widgets/src/` | 复合控件、`MainFrame`、`widgets_all.h` |
| **gdiplus** | `gdiplus/include/ui_engine/` | `gdiplus/src/` | Windows GDI+ 绘制辅助（仅 Win32 编 `gdiplus_ui.cpp`） |
| **platform** | `platform/include/platform/`（**仅库内 PRIVATE**） | `platform/src/` | `platform_factory`、各 OS 的 `Platform*` 实现；头文件与 `.cpp` 分离，不进入 `AGIS_UI_ENGINE_PUBLIC_INCLUDES` |
| **demo** | — | `demo/src/` | `ui_engine_demo` 可执行文件（非库的一部分）；声明与实现均在 `src/`（`ui_engine_demo.h` + `ui_engine_demo*.cpp`），不单独 `include/`，亦不作安装头 |

CMake 变量 **`AGIS_UI_ENGINE_PUBLIC_INCLUDES`**：`core` / `widgets` / `gdiplus` 的 `*/include` 路径，供 `agis_ui_engine` 与消费者使用。
