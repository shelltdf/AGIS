#pragma once

#include "map_engine/map_data_source.h"

#include <string>

class Map;

std::wstring GisDataSourceKindToTag(MapDataSourceKind kind);
MapDataSourceKind GisDataSourceKindFromTag(const std::wstring& s);

bool SaveGisProjectXml(const Map& doc, const std::wstring& path);
bool LoadGisProjectXml(Map& doc, const std::wstring& path, std::wstring* err);
