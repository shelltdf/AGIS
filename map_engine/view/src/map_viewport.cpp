#include "map_engine/map_viewport.h"

bool MapViewportIsValid(const MapViewport& v) {
  return v.pixelWidth > 0 && v.pixelHeight > 0 && v.world.valid();
}
