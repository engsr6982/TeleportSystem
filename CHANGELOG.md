# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [0.16.0] - 2026-01-26

- 修复 tpa 多请求时接受/拒绝无响应 #33
- 适配 LeviLamina v1.9.x

## [0.15.1] - 2025-11-08

- Fix crash caused by players accepting or denying expired TPA requests (#32, #28)

## [0.15.0] - 2025-11-06

- adapt LeviLamina v1.7.0

## [0.14.1] - 2025-10-25

### Fixed

- 修复 tpa 功能在关闭经济系统后依然需要经济的问题 #21
- 重写 TPA 调度，修复 Tpa 请求调度问题 #28 #30 #31

## [0.14.0] - 2025-10-04

### Changed

- 适配 LeviLamina v1.6.x

### Fixed

- 修复返回历史死亡点总是返回第一个死亡点的问题

## [0.13.0] - 2025-09-23

### 其它更改

- 适配 LeviLamina v1.5.x

## [0.12.1] - 2025-08-23

### 🐛 问题修复

- 修复 `tpr.enable` 配置无效
- 修复更新家园坐标时未同步维度
- 修复随机传送禁用维度无效

## [0.12.0] - 2025-7-16

### 🧹 其他改动

- 适配 LeviLamina v1.4.1

## [0.11.1] - 2025-7-16

### 🐛 问题修复

- 修复 LegacyMoneny 离线接口调用函数错误导致栈溢出

## [0.11.0] - 2025-7-16

### 🐛 问题修复

- 修复经济系统未热重载问题 #24

## [0.11.0-rc.2] - 2025-7-4

### 🐛 问题修复

- 修复 HomeGUI 选择家园时出现崩溃 #23
- 修复当经济系统关闭时依然需要经济的问题 #21
- 修复玩家没有死亡记录时打开死亡点 GUI 时引发的异常 #22

## [0.11.0-rc.1] - 2025-7-3

### Changed

- 重构插件
- 移除 Pr 功能
- 移除 Menu 组件

## [0.10.0] - 2025-6-22

### Changed

- Adapt LeviLamina v1.3.1

## [0.9.0] - 2025-03-05

### Changed

- Adapt LeviLamina 1.2.0-rc.1

## [0.8.0] - 2025-03-05

### Changed

- 适配 LeviLamina 1.1.0

## [0.7.0-rc.1] - 2025-01-21

### Changed

- 适配 LeviLamina 1.0.0-rc.3

## [0.6.0] - 2024-10-23

### Added

- Tpr、Tpa 支持冷却时间

## [0.5.0] - 2024-10-11

### Changed

- 重构经济系统
- Warp 功能支持模糊搜索

## [0.4.4] - 2024-10-8

### Fixed

- 修复 Tpa 模块异常

### Changed

- 重构 Tpa 模块(请求、请求池、表单)

## [0.4.3] - 2024-9-16

### Fixed

- Fixed #8

## [0.4.2] - 2024-9-9

### Changed

- 重构 TpaRequestPool、TpaAskForm
- 完善 Tpa 传送旋转角、接受&拒绝提示

### Fixed

- 修复亿些 Tpa 的 Bug(具体忘了)

## [0.4.1] - 2024-9-6

### Fixed

- 修复 Tpr 功能部分情况下不可用

## [0.4.0] - 2024-9-6

### Added

- 支持对子功能限制维度

### Changed

- 更新 LeviLamina、LegacyMoney 依赖版本
- 重构 Tpa、Config、Command 代码

## [0.3.0] - 2024-8-25

### Added

- 添加权限 `HomeCoutUnlimited` [#5](https://github.com/engsr6982/TeleportSystem/issues/5)

## [0.2.4] - 2024-8-19

### Changed

- 玩家睡觉中禁止 TPA、Home、Warp 传送

## [0.2.3] - 2024-8-16

### Fixed

- 修复死亡点坐标记录错误
- 修复返回死亡点弹窗弹窗时机错误

## [0.2.2] - 2024-7-27

### Fixed

fix: fix manifest.json #3

## [0.2.1] - 2024-7-24

### Changed

- Adaptable LeviLamina 0.13.4

## [0.2.0] - 2024-7-17

### Changed

- Adaptable LeviLamina 0.13.x

## [0.1.0] - 2024-05-25

### Changed

- 完成从`LSE-TPSystem`的移植
- 调整了目录结构
