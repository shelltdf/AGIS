# 详细设计：图层模型

## 1. 概念

- **Layer**：抽象基类；子类包括 **RasterLayer**、**VectorLayer**、**TileLayer**、**PointCloudLayer**、**ModelLayer**、**ObliquePhotogrammetryLayer**、**GaussianSplatLayer**、**ImageSequenceLayer**、**VideoStreamLayer** 等（分阶段实现）。
- **LayerGroup**：树节点，可包含子组与图层。

## 2. 驱动标识

- 枚举或字符串 **`driverId`**：`gdal-raster`、`ogr-vector`、`tms-xyz`、`wms`、`las`、`gltf`、`3dgs`、`mjpeg` 等；未实现驱动在 UI 中灰显或隐藏。

## 3. 生命周期

1. 用户 **添加图层** → 工厂根据驱动创建实例 → 异步打开数据源（大文件时显示进度）。  
2. **失败** → Log 错误，不加入树或标记为错误状态。  
3. **移除** → 释放句柄与 GPU 资源（若已分配）。

## 4. 与渲染

- Layer 暴露 **bounds**（可选）、**CRS**、**opacity**、**visible**。  
- 2D 遍历可见图层自下而上绘制；3D 按深度与材质排序（后续细化）。
