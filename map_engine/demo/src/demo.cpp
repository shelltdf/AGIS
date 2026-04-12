#include "map_engine_demo/platform.h"

#include "map_engine/map_engine.h"

namespace map_engine_demo {

int RunMapEngineDemo(Platform& platform) {
  MapEngine& engine = MapEngine::Instance();
  engine.Init();
  (void)engine.DefaultMapView();

  if (!platform.Init(engine)) {
    platform.Shutdown(engine);
    engine.Shutdown();
    return 1;
  }

  for (;;) {
    const int exitCode = platform.Step(engine);
    if (exitCode != 0) {
      platform.Shutdown(engine);
      engine.Shutdown();
      return exitCode;
    }
  }
}

}  // namespace map_engine_demo
