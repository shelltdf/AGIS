#pragma once

#include "map_engine/map_layer_driver.h"

#include <string>

class Map;

std::wstring GisDriverKindToTag(MapLayerDriverKind kind);
MapLayerDriverKind GisDriverKindFromTag(const std::wstring& s);

bool SaveGisProjectXml(const Map& doc, const std::wstring& path);
bool LoadGisProjectXml(Map& doc, const std::wstring& path, std::wstring* err);
