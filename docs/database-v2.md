# TeleportSystem 数据库存储结构 v2 设计

> 状态: 设计定稿, 待实施
> 日期: 2026-09-18 (修订: 2026-09-19)
> 目标版本: v0.19.0
> 对应 TODO: `src/ltps/database/StorageManager.h:15-18`

## 1. 背景与目标

### 1.1 现状

5 个 Storage 全部采用「单大键 + 全量刷盘」模式:

| Storage | 大键 | 值内容 | 规模增长 |
|---|---|---|---|
| `PermissionStorage` | `permission` | `{默认权限位掩码, 全部玩家权限表}` | 随玩家数线性增长 |
| `SettingStorage` | `rule` | 全部玩家设置 | 随玩家数线性增长 |
| `HomeStorage` | `home` | 全部玩家的所有家 | 随玩家数 × 家数增长 |
| `WarpStorage` | `warp` | 全部公共传送点 (JSON 数组) | 随传送点数增长 |
| `DeathStorage` | `death` | 全部玩家死亡记录 | 随玩家数 × maxDeathInfos 增长 |

问题:

1. **写放大**: 后台协程每 60 秒无条件 `postWriteBack()` (`StorageManager.cc:19-31`),
   每个 Storage 把整个内存数据集序列化后重写大键 — 即使服务器空闲、即使只改了一条记录。
   LevelDB 大值覆写产生新 SST 条目 + 压实压力, 值越大写放大越严重。
2. **数据竞争**: `postWriteBack` 在线程池线程执行, 而内存容器由游戏线程修改, 全量序列化存在竞争。
3. **用户标识符是 RealName**: 玩家改名后数据成为孤儿; 离线服存在同名风险。

### 1.2 目标

- Key 粒度细化为单条记录: `<库分组>:<业务组>:<用户唯一标识符>:<数据唯一标识符> => <数据>`
- 放弃全量内存驻留, 改为**按需读 (点查) + 写穿 (原子 batch)**
- 用户标识符迁移到 **uuid**, 采用「启动尽力解析 + 进服懒迁移」两级策略
- 完善的版本协议与迁移机制, **防止旧版本插件加载/破坏新版本数据**

## 2. 总体设计决策

| # | 决策 | 结论 |
|---|---|---|
| D1 | 数据库目录 | **新开 `database_v2` 目录**; 旧库 `leveldb` 只读迁移后**原样保留**(不改名/不删除); 新旧库同时存在 = abort |
| D2 | 版本标记 | `meta:schema` 全局 schema 版本, 迁移完成的唯一判据, 前向防御 |
| D3 | Key 模型 | 首段为**库分组**: `meta` / `data` / `legacy` / `index`; 保留分组与业务分组天然隔离 |
| D4 | 运行时模型 | 无全量内存; 点查按需读 + WriteBatch 写穿; 高频枚举靠维护型索引记录 |
| D5 | 用户标识符 | `mce::UUID::asString()`; 启动迁移经 `ll::service::PlayerInfo` 尽力解析, 进服懒迁移兜底 |
| D6 | 刷盘机制 | 删除 60 秒定时器与 `writeBack` 全量刷盘, 变更点即时原子落库 |
| D7 | Key 常量 | 所有模板 key 与 iterPrefix 前缀为 `static constexpr std::string_view` 常量, `fmt::format` 实例化; 常量命名 **k + 大驼峰** |
| D8 | 代码组织 | 新代码一律 **pImpl**; 公共头不暴露 nlohmann / leveldb / fmt 细节 |
| D9 | 名称校验 | 抽象为 **Validator 组件**, 行为由 **options** 控制, 替换散落在各 Module 的手工校验 |
| D10 | i18n | LL i18n 框架, **source-text-as-key**; 源语言**一次性整体切换为 en_US** (错误串天然默认英文), `lang/zh_CN.json` 翻译回中文, 译文以 LL scan 盘点为准; 详见 §5.4 |
| D11 | 语言标准 | 回退 **C++20**: 移除 `_HAS_CXX23`; `std::expected` → **`ll::Expected`** (错误类型固定 `ll::Error`), 详见 §5.5 |

### 2.1 为什么必须新开目录 (D1)

**已发布的旧版本插件无法被追溯地加上版本检查** — 它只认固定路径 `getModDir()/leveldb`,
不读任何 marker。因此 `meta:schema` 防不住旧插件, 唯一可靠的物理防线是目录隔离:

- 新插件运行期**只写 `database_v2`**, 对旧目录仅在启动迁移时**只读**访问, 之后原样保留;
- 旧库天然就是回滚点与备份, 无需设计备份键; 降级时旧插件照常读旧库 (迁移时刻快照);
- 状态歧义 (新旧并存) 一律 abort, 由人工显式决定留哪一个 —— 不做任何自动覆盖/清理。

跨库无原子性 → 迁移协议依赖「marker 最后写 + 失败即 abort」不变式, 见 §3.3。

## 3. 数据库目录与版本协议

### 3.1 目录布局

```
plugins/TeleportSystem/
├── leveldb/            # 旧库; 迁移时只读, 迁移后**原样保留** (不改名, 不动它)
└── database_v2/        # v2 库, 运行期唯一读写目标
```

目录名常量: `keys::kDatabaseV2Dir = "database_v2"`, `keys::kLegacyV1DirName = "leveldb"`。

### 3.2 启动判定: 只认「留一个」

迁移必须一次性成功; 状态歧义一律 abort, 由人工明确留哪一个:

| leveldb | database_v2 | 动作 |
|---|---|---|
| 无 | 无 | **全新安装**: 创建 v2, 写入 marker (含 version) |
| 无 | 有 | **正常加载**: 校验 marker (version / 完整性) |
| 有 | 无 | **迁移**: 创建 v2 → 逐 Storage 迁移 → 写 marker; 旧库原样保留 |
| 有 | 有 | **abort**: 拒绝猜测哪个是权威; 提示删除其一后重启 |

marker (`meta:schema`) 校验细则:

| 状态 | 动作 |
|---|---|
| `version == 2` | 正常加载 |
| `version > 2` | **abort**: 数据由更新版本插件写入, 请升级插件; 零写入 |
| `version < 2` | 预留: 链式迁移位 |
| 存在但解析失败 | **abort**: 数据损坏, 人工处置 (绝不清空) |
| 缺失 + 库为空 | 全新安装, 走初始化流程 (同样写入带 version 的 marker) |
| 缺失 + 库非空 | **abort**: 迁移半途失败或损坏; 提示显式删除 `database_v2` 后重启重迁移 |

> **marker 在任何路径都会写入 (含全新安装)**, `version` 字段始终存在 —— 向后兼容只依赖它。
> 不做自动清空重做: 半成品状态一律停下等人工处置, 避免"静默毁数据"。

### 3.3 启动流程

协议分布在三个时点 (构造期 Storage 尚未注册, 不能在其中调用 migrateFromV1):

```
① StorageManager 构造函数 (数据库先于 Storage 注册就绪):
    v2Exists = exists(modDir / "database_v2")
    v1Exists = exists(modDir / "leveldb")

    if v2Exists && v1Exists:  abort (明确留一个)          # §3.2
    v2 = open(modDir / "database_v2")                    # 不存在则创建
    if v1Exists:  mNeedMigration = true; return          # 只有旧库 → 待迁移

    meta = v2.get(keys::kMetaSchema)                     # 只有新库
    if !meta:
        if !v2.empty():  abort (迁移半途失败/损坏)        # 不自动清理
        return                                           # 空库 = 全新安装
    if 解析失败:        abort (损坏)
    if version > 当前:  abort (前向防御)
    mMigrated = true

② registerStorage<T>(): make_unique<T>(*mDatabase, ...)  # 注入 v2 句柄

③ StorageManager::initialize() (TeleportSystem::load, 模块 enable 之前):
    if !mMigrated:
        try:
            if mNeedMigration:
                v1 = open(modDir / "leveldb")            # 只读使用, 不写不删
                for storage in storages: storage.migrateFromV1(v1, v2)
                # 出块即析构: 旧库句柄不驻留
            for storage in storages: storage.migrateLegacyFiles(v2)      # permission.json 等
            v2.set(keys::kMetaSchema, {version: 2, migratedAt: now(), ...})  # 最后写
            mMigrated = true
        catch: log fatal + 提示「旧库未动, 删除 database_v2 后重启重试」; 抛出 → load() 返回 false
    emplaceListener<PlayerJoinEvent>(懒迁移)              # 须早于各模块的监听器
```

> 注: `KeyValueDB` 无 `close()`, 释放句柄 = 析构。旧库句柄仅在迁移块内存活, 出块即释放。

不变式: **marker 最后写**。迁移中途失败 → 流程中止且 `load()` 返回 false (插件不加载),
磁盘上留下旧库(未动) + 半成品 v2 → 下次启动命中「新旧都在 → abort」或「无 marker + 非空 → abort」,
人工删除 `database_v2` 即可重试。旧库从头到尾只读。

迁移在 mod load 阶段同步执行 (10^4~10^5 键规模约几十 ms, 可接受)。

### 3.4 降级 / 回滚 / 分叉语义

- **降级**: 旧库 `leveldb/` 从未被改动 → 旧版插件直接可用, 数据完整 (迁移时刻的快照)。
- **回滚**: 停服 → 删除 `database_v2/` → 装回旧版插件即可 (旧库原样)。
- **分叉**: 降级期间旧插件写入 `leveldb/` 后再次升级 → **新旧都在 → abort**,
  由管理员显式决定: 删旧库(放弃降级期改动) 或 删新库(从头重迁移, 含降级期改动)。

## 4. Key 设计

### 4.1 库分组

首段为**库分组** (非插件名), 划分数据的性质:

| 分组 | 内容 | 用户段 |
|---|---|---|
| `meta` | 全局元数据 (schema 版本等) | 无 |
| `data` | 业务数据, 已完成 uuid 化 | uuid; 全局数据用保留符 `-` |
| `legacy` | **过渡态**业务数据, 用户段为 realName, 待进服懒迁移 | realName (转义后) |
| `index` | 维护型索引记录 | uuid 或 `-` |

`legacy` 独立分组的收益: `data:` 分组内用户段**恒为 uuid** (读路径无二义性);
懒迁移只扫 `legacy:` 前缀; 过渡态存量随玩家回服单调收缩, 最终可整组清空。

### 4.2 键布局

值一律为 JSON 文本 (记录 = struct 经 `json_utils::struct2json`; 索引 = 字符串数组)。
尾段无意义时省略 (如每玩家单条的 setting)。

**meta:**

| Key | 值 |
|---|---|
| `meta:schema` | `{"version": 2, ...}` |

**data:**

| Key | 值 | 说明 |
|---|---|---|
| `data:home:<uuid>:<homeName>` | Home | 单条家园 |
| `data:warp:-:<warpName>` | Warp | 单条公共传送点 (全局) |
| `data:death:<uuid>:<ts>[-n]` | DeathInfo | `<ts>` = `{:%Y%m%d%H%M%S}`; 同秒冲突加 `-n` 后缀 |
| `data:setting:<uuid>` | SettingData | 每玩家一条 |
| `data:permission:<uuid>` | `{"perms": <mask>}` | 玩家权限 |
| `data:permission:-:default` | `{"perms": <mask>}` | 默认权限 |
| `data:user:<uuid>:profile` | `{name, xuid, migratedAt}` | 用户档案; **存在即懒迁移完成标记**; name 始终保持最新已知值 |

**legacy (过渡态, 由启动迁移写入 / 懒迁移消费):**

| Key | 值 |
|---|---|
| `legacy:home:<realName>:<homeName>` | Home |
| `legacy:death:<realName>:<ts>[-n]` | DeathInfo |
| `legacy:setting:<realName>` | SettingData |
| `legacy:permission:<realName>` | `{"perms": <mask>}` |

(warp 与 permission-default 无用户段, 启动迁移直接落 `data:`, 不经过 legacy。)

**index:**

| Key | 值 | 说明 |
|---|---|---|
| `index:home:<uuid>` | `["base", ...]` | 玩家家园名索引 |
| `index:warp` | `["spawn", ...]` | 全局传送点名索引 |
| `index:death:<uuid>` | `["20260919143005", ...]` | 死亡时间戳索引 (有序, 截断至 maxDeathInfos) |
| `index:ref:name:<realName>` | `<uuid>` | 名字 → uuid 反查 (管理 GUI 按名字查离线玩家) |

### 4.3 Key 常量 (`database/StorageKeys.h`)

所有模板 key 与前缀为 `inline constexpr std::string_view`, **k + 大驼峰**命名,
`fmt::format` 实例化; iterPrefix 的前缀同样定义常量, 禁止散落的字符串字面量:

```cpp
namespace ltps::keys {

// ── meta ──────────────────────────────────────────────
inline constexpr std::string_view kMetaSchema = "meta:schema";

// ── data 模板 key (fmt::format 占位) ──────────────────
inline constexpr std::string_view kDataHome              = "data:home:{}:{}";   // <uuid> <homeName>
inline constexpr std::string_view kDataWarp              = "data:warp:-:{}";    // <warpName>
inline constexpr std::string_view kDataDeath             = "data:death:{}:{}";  // <uuid> <ts>
inline constexpr std::string_view kDataSetting           = "data:setting:{}";   // <uuid>
inline constexpr std::string_view kDataPermission        = "data:permission:{}";
inline constexpr std::string_view kDataPermissionDefault = "data:permission:-:default";
inline constexpr std::string_view kDataUserProfile       = "data:user:{}:profile";

// ── legacy 模板 key (过渡态) ──────────────────────────
inline constexpr std::string_view kLegacyHome       = "legacy:home:{}:{}";      // <realName> <homeName>
inline constexpr std::string_view kLegacyDeath      = "legacy:death:{}:{}";
inline constexpr std::string_view kLegacySetting    = "legacy:setting:{}";
inline constexpr std::string_view kLegacyPermission = "legacy:permission:{}";

// ── index 模板 key ────────────────────────────────────
inline constexpr std::string_view kIndexHome    = "index:home:{}";
inline constexpr std::string_view kIndexWarp    = "index:warp";
inline constexpr std::string_view kIndexDeath   = "index:death:{}";
inline constexpr std::string_view kIndexNameRef = "index:ref:name:{}";          // <realName> → uuid

// ── iterPrefix 前缀常量 ───────────────────────────────
inline constexpr std::string_view kPrefixData           = "data:";
inline constexpr std::string_view kPrefixDataHome       = "data:home:";
inline constexpr std::string_view kPrefixDataHomeUser   = "data:home:{}:";      // <uuid>
inline constexpr std::string_view kPrefixDataWarp       = "data:warp:-:";
inline constexpr std::string_view kPrefixDataDeathUser  = "data:death:{}:";     // <uuid>
inline constexpr std::string_view kPrefixLegacy         = "legacy:";
inline constexpr std::string_view kPrefixIndex          = "index:";

// ── 目录 ──────────────────────────────────────────────
inline constexpr std::string_view kDatabaseV2Dir   = "database_v2";
inline constexpr std::string_view kLegacyV1DirName = "leveldb";   // 旧库 (迁移后原样保留)

} // namespace ltps::keys
```

使用示例:

```cpp
auto key = fmt::format(keys::kDataHome, uuid.asString(), key_utils::escapeSegment(homeName));
db.set(key, json.dump());

for (auto&& [k, v] : db_utils::iterPrefix(db, keys::kPrefixDataWarp)) { /* 立即拷贝 v */ }
```

### 4.4 段转义规则

- 所有段统一经 `key_utils::escapeSegment`: `\` → `\\`, `:` → `\:` (单射, 不同名称不撞键);
- **加载时不需要从 key 反解数据** — 值 JSON 内含全部字段, key 只需满足「前缀可扫 + 单射」;
- uuid 段为 `asString()` 36 字符 (无冒号, 转义后不变), 与 LL PlayerInfo 存法一致
  (`PlayerInfo.cpp:69`); realName (legacy 段) 按同规则转义;
- 业务侧名称合法性由 Validator 前置把关 (§5.3), 转义是最后一道单射保障, 二者互补。

### 4.5 排序性质

LevelDB 按字节序排列 → 同一用户的记录物理连续; `{:%Y%m%d%H%M%S}` 时间戳键字典序 = 时间序,
死亡记录枚举天然有序, 淘汰最旧 = 删索引首元素对应的键。

## 5. 代码组织与命名规范

### 5.1 pImpl (D8)

新增/重构的有状态类一律 pImpl: 公共头只留 `std::unique_ptr<Impl> mImpl` 与导出接口
(`TPSAPI`), nlohmann / fmt / 内部容器全部收进 `.cc`:

- 适用: `StorageManager` (重构)、各 Storage 门面、`Migrator`、`NameValidator`;
- 不适用: 纯常量 (`StorageKeys.h`) 与自由函数工具 (`key_utils::escapeSegment`、
  `db_utils::iterPrefix`) — 保持 namespace 函数;
- 配合现有 `TPS_DISALLOW_COPY_AND_MOVE` 宏; pImpl 同时稳定 TPSAPI 导出面, 降低编译依赖。

### 5.2 常量命名 (D7)

- key 模板、前缀、目录名: `inline constexpr std::string_view`, **k + 大驼峰** (`kDataHome`);
- 集中定义于 `database/StorageKeys.h`, 全项目唯一出处;
- 实例化统一 `fmt::format(keys::kXxx, ...)`。

### 5.3 NameValidator (D9)

替换散落在 Module 里的手工校验 (`string_utils::isLengthValid` + 非空判断, 如
`HomeModule.cc:94,343,428,486`), 行为由 options 控制:

```cpp
// ltps/utils/NameValidator.h
enum class NameValidation { Ok, Empty, TooLong, IllegalChar };

struct ValidatorOptions {
    size_t             maxLength   = 0;      // 0 = 不限; 通常取自配置 (home.nameLength 等)
    bool               allowEmpty  = false;
    std::string_view   forbiddenChars{"\x00"};  // 默认仅禁控制字符; ':' 由转义处理, 无需禁止
};

class NameValidator final {                  // pImpl
public:
    TPSAPI explicit NameValidator(ValidatorOptions options);
    TPSNDAPI NameValidation validate(std::string_view name) const;
private:
    std::unique_ptr<Impl> mImpl;
};
```

- 每个业务 (home / warp) 在 Module init 时按配置构造自己的 Validator 实例;
- 返回错误码, 由调用点映射为消息 (§5.4 错误规范: `makeI18nStringError` 或 `_trl`),
  Validator 本身不含文案;
- 校验通过后才进入 key 转义与写库路径。

### 5.4 i18n 规范 (D10)

#### 5.4.1 LL i18n 语义 (已从 LL 26.40 源码验证)

- `_tr` / `_trl` 为 **source-text-as-key**: 字面量本身即翻译键
  (`I18n.h:130-148`, `i18n::getInstance().get(Fmt.sv(), locale)`);
- `_tr` 无 locale → 取**服务器系统 locale** (`I18n.cpp:9-13`); `_trl(locale)` → 指定 locale
  (业务代码传 `player.getLocaleCode()`);
- 回退链 (`I18n.cpp:46-52, 117-133`), 决定源语言选型:

| 情形 | 结果 |
|---|---|
| key 未出现在任何翻译文件 | 返回**源文本** |
| key 已登记, locale 精确命中 | 对应译文 |
| key 已登记, locale 未命中 | 尝试 **en-us** 条目 → 仍无则取 **map 中第一个语言** (非源文本!) |

- **LL 不会自动加载插件的翻译文件** — LL 全源码无任何 `I18n::load` 调用点,
  插件必须自行加载; 现状项目未打包任何翻译文件, 所有 `_trl` 实为 no-op (全员看中文源文本)。

#### 5.4.2 源语言决策: 迁移为 en_US

存量 268 处中文字面量 (25 文件) 迁移为英文源文本, `zh_CN.json` 把英文翻回中文。理由:

1. **默认 en_US 零成本**: 错误串/日志/异常 `what()` 无 locale 上下文, 直接输出源文本 →
   英文源使一切未被翻译覆盖的路径 (新串漏翻、文件缺失/损坏、无 locale 场景) 天然落到 en_US,
   与「错误输出默认 en_US」一致; 中源方案则必须人工维护全量 en_US 翻译才能兜住同一目标;
2. **回退链第三档的坑对称存在, 但成本不对称**: 只发 zh_CN.json 时 en-us 玩家命中
   `strings.begin()` 兜底会看到中文 → 必须同时发 **en_US.json 恒等映射**; 英文源方案中
   该文件是 `key→key` **脚本可生成**, 中文源方案中对应文件是 268 条**人工翻译**;
3. **统一现有双标**: 现 Result 错误串已是英文 ("Home name repeated" 等), GUI/聊天串是中文;
   迁移后全源码单一语言;
4. **生态一致**: LL core 自身全部英文源 (`makeI18nStringError<"Path not found: {}">`,
   `I18n.cpp:77`); 国际贡献者可读;
5. **一次性整体切换, 无混排窗口**: 全部字面量 (业务消息与错误串) 直接换成 en_US,
   不做"改到哪迁到哪"的渐进式; v2 重构本就要触碰全部调用点, 顺路完成;
6. **盘点即翻译清单**: `LL_I18N_COLLECT_STRINGS` 编译宏 (`I18n.h:20,92-111`) 在编译期收集
   **全部 `_tr` / `_trl` 字面量**及源码位置 (它们都是 `FixedString` 模板参数, 无遗漏) —
   实施末尾跑一遍 scan, 以输出为清单人工撰写 `zh_CN.json`, 无需其他提取机制。

#### 5.4.3 翻译文件与加载

```
plugins/TeleportSystem/lang/
├── en_US.json    # 恒等映射 {英文: 英文}, 由 scan 输出/zh_CN key 集生成, 必须存在 (§5.4.1 回退链第三档)
└── zh_CN.json    # {英文: 中文}, 仅收录业务消息; 错误串默认不收录 → 恒为英文
```

- `TeleportSystem::load()` 中显式 `ll::i18n::getInstance().load(getSelf().getModDir() / "lang")`
  (目录模式: 文件名 stem 即 locale, `I18n.cpp:79-88`); 加载失败记 error 不阻断启动 (退化为英文);
- 打包: xmake 安装规则补充 `lang/` 目录 (与 `config/` 同级);
- **完整性约束**: `zh_CN.json` 的每个 key 必须存在于 `en_US.json` (CI/测试断言, 见 §10)。

#### 5.4.4 错误与业务消息的分层

| 类别 | 形态 | 翻译 |
|---|---|---|
| **错误** (Result / 异常 / 日志) | `ll::makeI18nStringError<"Home name repeated">()` → `ll::Unexpected`; `Result` 别名改为 **`ll::Expected<T>`** (§5.5) | 默认不翻译 → en_US; `I18nStringError` 持有 key+args, 日后想本地化只需向 zh_CN.json 补条目, 零代码改动 (`I18n.h:75-84`) |
| **业务消息** (GUI 标题/按钮/表单、聊天提示、命令反馈) | `"..."_tr(...)` (无玩家上下文) / `"..."_trl(player.getLocaleCode(), ...)` | zh_CN.json 收录 |
| **边界转换** | 命令/GUI 层拿到 `Result` 错误后 `err.message(locale)` 输出给玩家; 日志走 `err.message()` | - |

- Result 错误构造**一步到位**: 存量 18 处 `std::unexpected("...")` 在 §5.5 的 C++20 回退中
  统一替换为 `makeI18nStringError` (错误串已是英文, 字面量平移), 新代码同样;
- Validator 错误码映射 (§5.3): 调用点按 `NameValidation` 枚举选择对应英文字面量
  (`"Name is empty"` / `"Name too long ({}/{})"` / `"Name contains illegal characters"`),
  经 `makeI18nStringError` 或 `_trl` 输出;
- 同串异义限制: source-text-as-key 无法区分同一英文串在不同上下文的不同译法 →
  文案设计时保证**串全局唯一语义**, 需要区分时改写措辞。

### 5.5 语言标准: 回退 C++20, Expected 换 LL 实现 (D11)

现状: `xmake.lua:52` 已是 `set_languages("c++20")`, 但靠 `_HAS_CXX23=1` 宏
(`xmake.lua:57`) 在 MSVC STL 上强开 C++23 库特性以使用 `std::expected`
(配套还有 `-Wno-c++2b-extensions`, `xmake.lua:40`)。回退动作:

- **xmake.lua**: 删除 `_HAS_CXX23=1` 定义与 `-Wno-c++2b-extensions` 选项;
- **Global.h**: `Result` 别名由 `std::expected<T, E = std::string>` 改为
  **`ll::Expected<T>`** (= `nonstd::expected<T, ll::Error>`, `Expected.h:17-18`;
  错误类型固定, **E 模板参数删除** — 现无 `Result<T, 自定义E>` 用法, 无损);
  移除 `<expected>` include, 各 `.cc` 同步清理;
- **错误构造**: 18 处 `std::unexpected("...")` (6 文件) → `ll::makeI18nStringError<"...">()`;
- **错误消费**: `res.error()` 按 string 使用处 → `res.error().message()`; 命令层可直接用
  `ll::Error::log(CommandOutput&, locale, ...)` / `log(Logger&, locale, ...)` 重载
  (`Expected.h`) 输出本地化错误;
- 注意 `ll::Error` **move-only** (禁拷贝), 按值传递错误的旧签名需改引用/移动;
  `nonstd::expected` (expected-lite) 的 `value()/error()/value_or()/operator bool`
  与 `std::expected` 用法兼容, 调用面基本无感;
- 其余 C++23 依赖 (若有) 随编译错误逐个清理 — 该改动**独立且机械, 放在阶段 1 最先做**,
  为后续所有新代码定调。

## 6. 运行时读写模型

### 6.1 底层能力 (已从 LL 26.40 源码验证)

| 事实 | 出处 | 意义 |
|---|---|---|
| 默认构造带 10-bit bloom filter | `KeyValueDB.cpp:88` | 点查 miss 几乎纯内存判定; hit 1~2 次块读, 库常驻 OS 缓存 → **µs 级** |
| `WriteOptions.sync = false` (默认值, LL 未改) | `KeyValueDB.cpp:37` | 写 = WAL 追加 (OS 缓冲) + memtable 插入, 无 fsync → **游戏线程写穿无卡顿**; 进程崩溃不丢 (WAL 重放), 断电可能丢最后几笔 |
| `WriteBatch` → `leveldb::DB::Write` 原子提交 | `KeyValueDB.cpp:131` | 多键操作不可分割 |
| `iter()` 惰性协程, 一致性快照, 与并发写安全共存 | `KeyValueDB.cpp:133-143` | yield 的 `string_view` **仅在下一步之前有效, 必须立即拷贝** |
| LevelDB 内部线程安全 (并发读 + 单写互斥) | leveldb 语义 | 内存容器删除后, 现存数据竞争随之消失 |

### 6.2 写路径: 变更点写穿

**删除 60 秒定时器协程、`postWriteBack` 全量刷盘 — 一切变更在 mutation 点即时落库:**

```
addHome(uuid, home):                         # Validator 已前置通过
    batch = { SET fmt(kDataHome, uuid, esc(name)), SET fmt(kIndexHome, uuid) (追加后) }
    db.write(batch)                          # 单 batch 原子

removeHome / renameHome:
    batch = { DEL 旧记录, SET 新记录(改名时), SET 更新后索引 }

addDeathInfo(uuid, info):                    # 含截断
    batch = { SET 记录, SET 索引; 超限时 DEL 最旧记录 }

lazyMigrate(uuid, name):                     # §7.2
    batch = { SET data 新键, DEL legacy 旧键, SET profile, SET kIndexNameRef, 合并索引 }
```

索引与记录**同 batch 更新**, 结构上不可能脱节; 另提供 `/tpsadmin rebuild-index`
(全扫重建所有索引) 作为运维兜底。

### 6.3 读路径: 按需点查

```
hasHome / getHome / getSettingData / hasPermission → db.get(fmt(...))           # O(1), µs 级
getHomeCount(uuid)          → 点查 index:home:<uuid>, 取长度                    # O(1)
getHomes(uuid) (GUI 列表)   → 点查索引 + N 次点查记录 (N ≤ 几十)
getWarps / queryWarp        → 点查 index:warp + 过滤 + 点查记录
getDeathInfos(uuid)         → 点查 index:death:<uuid> + 点查记录 (天然时间有序)
```

**正常运行期不需要全量 `iter()`**, 它只剩三个低频用途: 启动迁移、每玩家一次的懒迁移、
管理面板跨玩家枚举 / rebuild-index (可派发线程池)。

### 6.4 API 波及面

- 返回 `const&` 的接口 (`getHomes/getAllHomes/getWarps`) 改为**按值返回**;
- 参数从 `RealName` 换为 `mce::UUID` (在线玩家取 `player.getUuid()`;
  管理 GUI 按名字操作离线玩家时经 `index:ref:name:` / `PlayerInfo` 解析);
- `IStorage` 接口收缩: `load/writeBack` 移除, 保留 `migrateFromV1(v1db, v2db)` 与
  `ensureUserMigrated(uuid, name)` 钩子; Storage 类退化为「db 之上的类型化门面」(pImpl),
  无业务状态成员;
- 调用点集中在 HomeGUI / HomeOperatorGUI / WarpGUI / WarpCommand / DeathGUI / DeathCommand /
  各 Module 命令处理, 机械性修改。

## 7. 数据迁移

### 7.1 启动迁移 v1 → v2 (结构迁移, name → uuid 尽力解析)

对每个 v1 大键, 单个 WriteBatch 写入 v2 库:

```
v1["home"] = { "<realName>": [Home...], ... }
  对每个 realName:
      PlayerInfo::fromName(name) 命中且 entry.name == name
          → 记录写 data:home:<uuid>:<name> + 写 profile + 写 index:ref:name
      未命中 (LL 缓存无此人 / 已改名)
          → 记录写 legacy:home:<name>:<name> (过渡态), 等进服懒迁移
  同时构建 index:home:<用户段> 索引
```

- `ll::service::PlayerInfo` 自带持久化 LevelDB, 启动全量加载, 覆盖「LL 安装后进过服且未改名」
  的全部玩家 (`PlayerInfo.cpp:21,44-48`) → 大多数数据启动即落定 uuid 键, legacy 组存量最小;
- warp / permission-default 直接落 `data:`;
- **值字节尽量原样搬运** (v1 单条记录 JSON 与 v2 字段一致时免重序列化);
- `PermissionStorage` 旧 json 文件兼容链保留: 启动迁移时若存在 `permission.json`,
  按现有 `_tryLoadLegacyPermissionFile` 逻辑读入并直接写 v2 记录, 原文件照旧改名 `.old`;
- 全部完成后执行 §3.3 的改名与 marker 写入。

### 7.2 进服懒迁移 legacy → data (权威路径, 每玩家至多一次)

```
PlayerJoinEvent (StorageManager 中央监听, 早于各 Module 的 join 处理):
    uuid = player.getUuid(); name = player.getRealName()
    if db.has(fmt(keys::kDataUserProfile, uuid)):
        if profile.name != name:                  # 改名 → 刷新档案与反查
            batch{ SET profile{name,...}, SET index:ref:name:<新名> }
        return
    ensureUserMigrated(uuid, name):
        iterPrefix(db, keys::kPrefixLegacy) 收集用户段 == escape(name) 的所有记录
        单 WriteBatch:
            SET data:<组>:<uuid>:<id> = 旧值原文   # 不重序列化
            DEL legacy:<组>:<name>:<id>
            SET data:user:<uuid>:profile = {name, xuid, migratedAt}
            SET index:ref:name:<name> = uuid
            SET/合并 index:home / index:death
```

- **原子 + 幂等**: profile 与搬迁同 batch; 崩溃重进服重跑即可;
- **同步执行**: legacy 组扫描在过渡期后规模很小; 最坏 (首启后第一次大规模回服) 为
  全库扫描几 ms~几十 ms, 在 join 事件内同步完成, 玩家能发命令前数据已就位;
  不依赖监听器注册顺序 — 按 uuid 的读写入口内部调用幂等 `ensureUserMigrated` 兜底
  (profile 点查命中即返回, 常态开销一次 `has()`);
- 模拟玩家不跳过 (其数据同样真实), 仅 `initPlayerSetting` 等默认值创建沿用现有跳过逻辑;
- 过渡期结束后 (`iterPrefix(kPrefixLegacy)` 为空) 可记录日志提示 legacy 组已清空。

### 7.3 已知边界与兜底

| 场景 | 行为 | 兜底 |
|---|---|---|
| 离线服同名撞号 | legacy 键数据「先进服者得」 | uuid 化后不再发生; 属 v1 数据固有歧义 |
| 玩家改过名 (遗留 v1 数据) | 旧数据在旧名下, join 按新名扫不到 → 孤儿 | `/ltps admin attach <旧名> <玩家名>` 手动搬运 (拿权威 uuid); 管理 GUI 枚举 legacy 组可发现残留 |
| **改名后旧名反查索引残留** | 已修复: 改名时**在同一 batch 删除旧 `index:ref:name:`**; `resolveUuid` 拿到 ref 后**必须与 profile.name 交叉校验**, 不符即视为陈旧并回退 PlayerInfo | 保证名字被他人回收后, 按名字的管理操作不会落到改名前的玩家 uuid 上 |
| **名字回收后的 legacy 归属** | 名字被新玩家回收时, 旧名下的 legacy 记录会判给新玩家 (name-keyed 数据的固有歧义, 无法从数据判别) | 迁移时打 **warn 日志** (名字/条数/uuid); 数据始终可经 `/ltps admin attach` 人工改判 |
| 降级期间旧插件写入 | 新建的空 `leveldb/` 与 v2 分叉 | 启动存在性检测告警 + 可选 `reimport-legacy` (§3.4) |
| 玩家永不回服 | 数据停留在 legacy 组 | 可读可枚举, 无需处理 |

## 8. 枚举性能与 iter()

### 8.1 iter() 能力评估

复杂度为 **O(全库键数)** (LL 未暴露 Seek, 只能 `SeekToFirst` 全量走), 非 O(前缀匹配数)。
库常驻 OS 缓存, 顺序迭代吞吐约 1~5M keys/s:

| 全库键数 | 单次全扫 (热数据) | 场景 |
|---|---|---|
| 1 千 | < 1 ms | 小服 |
| 1 万 | ~2-5 ms | 典型规模 |
| 10 万 | ~20-50 ms | 大型长期服 |
| 100 万 | 数百 ms | 必须异步或改方案 |

结论: **迁移/修复等低频场景完全抗得住** (可派发线程池); 高频 GUI 枚举经 §6.2 索引记录
已降为 1~2 次点查, 不依赖 iter()。

### 8.2 iterPrefix 抽象与 LL 上游

- 插件内封装 `db_utils::iterPrefix(db, prefix)`: 现阶段实现 = 全扫 + 前缀过滤 (立即拷贝);
  prefix 一律传 `keys::kPrefix*` 常量;
- LL 的 `has()` 内部已示范 Seek 用法 (`KeyValueDB.cpp:111-119`), 可给 LL 提 PR 增加原生
  `iterPrefix` (Seek(prefix) + Next 至前缀不匹配, ~10 行) → 枚举降为 O(匹配数);
  LL 支持后仅替换 `db_utils` 内部实现, 调用点零改动;
- 同理可考虑上游暴露 `WriteOptions.sync` 供断电零丢失场景选用。

## 9. 实施步骤

| 阶段 | 内容 | 涉及 |
|---|---|---|
| 1. 基础设施 | **C++20 回退先行** (§5.5: 去 `_HAS_CXX23`、`Result` → `ll::Expected`、18 处 `std::unexpected` 替换); `database/StorageKeys.h` (常量全集)、`key_utils` (转义)、`db_utils::iterPrefix`、`NameValidator` + options、marker 协议、StorageManager pImpl 重构 (双库开启 / 启动判定 / 改名, §3.3); **i18n 接线**: `TeleportSystem::load()` 加载 `lang/` 目录、xmake 打包规则 | `src/ltps/database/`, `src/ltps/utils/`, `TeleportSystem.cc`, `Global.h`, `xmake.lua` |
| 2. 启动迁移 | 各 Storage `migrateFromV1` + PlayerInfo uuid 解析 + legacy 组落位 + 索引构建 + legacy json 链 | `database/`, `modules/*/` |
| 3. Storage 改造 | 写穿 + 按需读 + uuid 签名 (顺序: Setting → Permission → Death → Warp → Home), 删除定时器/全量刷盘/内存容器; Module 接入 Validator; 同步修改 GUI/Command 调用点; **全部中文字面量一次性直换 en_US** (不留混排窗口; 错误串 `makeI18nStringError`, 业务消息 `_tr/_trl`) | `modules/*/`, `database/IStorage.*` |
| 4. 懒迁移与管理命令 | 中央 PlayerJoinEvent 监听 + `ensureUserMigrated`; `/tpsadmin rebuild-index`, `attach` (可选 `reimport-legacy`) | `database/`, `base/` |
| 5. i18n 收尾 | 开 `LL_I18N_COLLECT_STRINGS` 编译跑一遍 **scan**, 收集全部 `_tr/_trl` 字面量清单; 据清单人工撰写 `lang/zh_CN.json`; 由 key 集生成恒等 `lang/en_US.json` | `lang/`, `scripts/` |
| 6. 测试与文档 | §10 用例; CHANGELOG 记录目录变化、降级/回滚操作说明 | `test/`, `CHANGELOG.md` |
| 7. 可选上游 | LL `iterPrefix` / `sync` 选项 PR | LeviLamina |

## 10. 测试计划

`TPS_TEST` 模式 (`test/TestMain.cc`) 下用临时目录构造真实 LevelDB:

1. **单元**: 转义单射性; 常量 key `fmt::format` 实例化快照; `iterPrefix` 与全扫过滤结果一致;
   Validator 各 options 组合 (长度/空名/非法字符);
2. **启动迁移**: v1 夹具 (5 大键, 含 PlayerInfo 命中/未命中两类玩家) → 断言 data/legacy/index
   键布局、profile 与反查正确; 旧库未被改动; 新旧库并存 → abort; 新库无 marker 且非空 → abort;
3. **懒迁移**: `ensureUserMigrated` 跑两遍结果一致; legacy 键全部搬入 data 且旧键删除;
   改名档案刷新; legacy 组清空检测;
4. **一致性**: 随机增删改序列 → `rebuild-index` 重建结果与现有索引逐项相等;
5. **版本防御**: marker version=3 → 拒绝加载且零写入;
6. **i18n 完整性**: `en_US.json` key 集 ⊇ `zh_CN.json` key 集 (恒等兜底不缺项);
   翻译文件缺失/损坏 → 启动不阻断且输出退化为英文源文本;
   `LL_I18N_COLLECT_STRINGS` 盘点结果与翻译文件 key 集比对 (查缺脚本);
7. **演练**: 拷贝真实服务器 `leveldb` 目录跑迁移, 对比各 Storage 条目数;
   模拟降级 (移除 `database_v2` 可见性、以旧路径启动) 验证空库 + 告警路径;
   回滚操作 (改名回 `leveldb`) 后旧数据完整;
   zh_CN / en_US 客户端各进服一次, 抽查 GUI 与错误输出语言正确。

## 11. 风险与已知限制

- **断电丢失窗口**: `sync=false` 下 WAL 未 fsync, 断电可能丢最后几笔写入
  (对比现状丢 ≤60s 已大幅收窄); 需零丢失则依赖 LL 上游暴露 sync 选项;
- **降级体验变化**: 降级后旧插件面对空库 (显式信号), 回滚需按 §3.4 人工改名 —
  换取的是杜绝静默数据分叉; CHANGELOG 必须写明回滚步骤;
- **改名孤儿**: §7.3, 依赖管理命令兜底, 覆盖极小众场景;
- **枚举复杂度**: `iterPrefix` 上游化之前, 跨玩家全量枚举为 O(全库), 10^6 键规模需异步化;
- **LL API 依赖**: KeyValueDB 接口在 LL 26.x 内稳定; 若上游新增前缀迭代 API, 仅替换
  `db_utils` 实现;
- **字面量一次性翻译的语义漂移**: 中→英直换是语义改写, 需逐条 review; `zh_CN.json` 以 scan
  清单人工撰写, 与英文源串的对应关系在评审时核对;
- **同串异义**: source-text-as-key 的固有限制 (§5.4.4), 依赖文案规约而非机制保证;
- **漏翻退化方向**: 新增业务串未及时进 zh_CN.json 时中文玩家看到英文 — 可接受的退化方向
  (反向即英文玩家看中文, 不可接受), 这也是选 en_US 源的核心理由。

## 12. 实施修订记录 (2026-09-19 落地时确认)

设计稿进入实现后与代码不一致的条目, 以本节为准:

| # | 设计稿 | 实际实现 |
|---|---|---|
| 1 | §5.3 `NameValidator` 置于 `utils/` | 落在 **`src/ltps/validator/NameValidator.h/.cc`**; `forbiddenChars` 默认值必须写成 `std::string_view{"\x00", 1}` (写成 `{"\x00"}` 会因 `strlen` 得到空视图, 静默失效) |
| 2 | §5.4.3 语言文件源目录 | 源文件在仓库 **`assets/lang/`**, 构建期拷到 `bin/TeleportSystem/lang/` (xmake `after_build`, 跳过空占位文件); 运行时加载 `getModDir()/"lang"` |
| 3 | §5.5 错误构造统一用 `makeI18nStringError` | `PriceCalculate.cc` 的 exprtk 运行时字符串改用 **`ll::makeStringError(std::string)`** (`Expected.h:107`); 其余 17 处用模板版. 另: LL 无 `fmt::formatter<ll::Error>`, 全部 `.error()` 必须改 `.message()` |
| 4 | §4.2 death 索引存时间戳 | 索引存**完整记录 id (含 `-n` 后缀)**, 否则同秒并发死亡时截断会删错记录 |
| 5 | §7.2 各 Storage 自扫 legacy | legacy 扫描**只在 StorageManager 做一次**, 按业务组分桶后经 `migrateUser(batch, uuid, name, records)` 分发; **profile 无条件写入** (它是负缓存, 否则每次读都会全扫) |
| 6 | §6.4 管理路径未定义 uuid 缺失行为 | `grantPermissionByName` 等解析不到 uuid 时**回退写 `legacy:permission:<realName>`**, 等玩家下次进服由懒迁移收编; `/ltps perm list player` 同理读 legacy |
| 7 | §6.4 `getSettingData` 返回 error | 改为**无记录返回默认 `SettingData`**: 新玩家是常态路径, 原语义会让 `DeathModule` 的 `->` 与 `TpaRequest` 的 `.value()` 常态化 UB/抛异常 |
| 8 | §6.4 事件载荷 | `HomeEvents.h` 中持有 `Home const&` 的成员全部改**按值持有** (`getHomes()` 改为按值返回后, 发布点绑定的是临时量) |
| 9 | §5.4.1 Cooldown 未提及 | `Cooldown` 键由 RealName 改 **uuid 字符串** (内存态, 无数据迁移), 避免改名重置冷却 |
| 10 | §5.1 pImpl 适用范围 | `StorageManager` / 各 Storage / `NameValidator` 用 pImpl; `StorageKeys.h`、`DbUtils.h` 保持常量与 namespace 函数. Storage 构造改为**注入 `KeyValueDB&`**, 删除 `IStorage::getDatabase()` 单例穿透与 `IStorage.cc` |
| 11 | §5.4.2 命令注册 | 管理命令并入 `base/BaseCommand.cc` 的 `admin` 二级重载: `/ltps admin rebuild-index`、`/ltps admin attach <旧名> <玩家名>` (不另开命令根) |
| 12 | §7.2 改名处理不完整 | 改名分支需**删除旧名反查索引** (`index:ref:name:<旧名>`), 否则名字被他人回收后按旧名操作会落到原玩家 uuid; `resolveUuid` 增加 **profile.name 交叉校验**, 不符即弃用该 ref 并回退 `PlayerInfo`. legacy 迁移增加 warn 日志 (名字回收归属歧义) |
| 13 | §3.1–§3.4 目录协议 | **取消旧库改名** (`legacy_leveldb` 方案废弃): 旧库 `leveldb` 迁移后原样保留, 旧库句柄只在迁移块内存活并即时析构。判定简化为「留一个」: 无库/只有新库→正常; 只有旧库→迁移; **新旧并存→abort**; 新库无 marker 且非空→abort。迁移中途失败 → 中止并让 `load()` 返回 false (插件不加载), 不自动清空重做。marker (含 `version`) 在**所有路径**(含全新安装)都会写入 |
