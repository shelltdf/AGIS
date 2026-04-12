#pragma once

#include "map_engine/map_layer.h"

/**
 * 视口（功能组）：将数据坐标范围 ``ViewExtent`` 与客户区像素尺寸组合，
 * 供屏幕变换、缩放锚点与拾取与现有 ``Map`` 逻辑逐步对齐。
 */
struct MapViewport {
  ViewExtent world{};
  int pixelWidth{0};
  int pixelHeight{0};
};

/** 像素宽高为正且 ``world`` 为合法范围时返回 true。 */
bool MapViewportIsValid(const MapViewport& v);
