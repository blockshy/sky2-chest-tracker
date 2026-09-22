// Windows x64 运行时回归测试：直接执行生产 C++ helper 的实际地址读取和条件判断。
// 所有“游戏对象”都由本测试在自己的进程中分配；不启动、不读取、不修改游戏或存档。
// 此文件必须单独编译为测试程序，不再额外编译 exploration.cpp。链接生产 MASM 和 MinHook
// 只用于满足生产文件的外部符号；本测试从不调用 InstallExploration 或任何挂钩安装 API。
#include "../native/exploration.cpp"
#include <array>
#include <initializer_list>
#include <limits>

namespace tracker {
// 日志输出不属于本测试范围；保留生产 helper 的真实调用路径，仅替换日志落盘端点。
void Log(const char*) noexcept {}
}

namespace {
constexpr size_t kFlagsGlobalOffset = 0xC60E58;
constexpr size_t kSyntheticImageSize = 0xD00000;
constexpr uintptr_t kMapCallerOffset = 0x3F139D;

struct TestResult {
    unsigned checks = 0, failures = 0;
    void Check(bool value, const char* label) {
        ++checks;
        if (!value) { ++failures; std::printf("FAIL: %s\n", label); }
    }
};

// VirtualAlloc 仅操作当前测试进程。独立的不可访问页用于验证生产 SEH 读取保护，
// 不依赖随机地址是否恰好可读，也不把损坏地址指向其他应用或真实游戏对象。
class OwnMemory {
    void* data_ = nullptr;
public:
    explicit OwnMemory(size_t size, DWORD protection = PAGE_READWRITE) noexcept
        : data_(VirtualAlloc(nullptr, size, MEM_RESERVE | MEM_COMMIT, protection)) {}
    ~OwnMemory() { if (data_) VirtualFree(data_, 0, MEM_RELEASE); }
    OwnMemory(const OwnMemory&) = delete;
    OwnMemory& operator=(const OwnMemory&) = delete;
    uintptr_t Address() const noexcept { return reinterpret_cast<uintptr_t>(data_); }
};

template<class T, size_t N>
void Put(std::array<unsigned char, N>& buffer, size_t offset, const T& value) {
    // 所有偏移均为下面固定测试结构的一部分；用 memcpy 避免未对齐写或严格别名问题。
    std::memcpy(buffer.data() + offset, &value, sizeof(value));
}

template<class T, size_t N>
bool Same(const std::array<T, N>& left, const std::array<T, N>& right) {
    return std::memcmp(left.data(), right.data(), sizeof(T) * N) == 0;
}

// 每次测试都构建独立的原生表和运行时数组。所有活动行具有真实布局的静态记录，
// 并在比较时覆盖整块内存，防止只检查目标 visible 而漏掉保存位、灰态或邻接字段写入。
struct TravelFixture {
    std::array<unsigned char, 0x100> manager{};
    std::array<unsigned char, 0x1100> flags{};
    std::array<tracker::TravelSpotState, 5> spots{};
    std::array<tracker::TravelAreaState, 3> areas{};
    std::array<unsigned char, 0x100> tableRoot{};
    std::array<unsigned char, 0x20> tableHolder{};
    std::array<unsigned char, 0x38> tableFile{};
    std::array<unsigned char, 0xA0> tableHeaders{};
    std::array<unsigned char, 0x700> tableBuffer{};
    std::array<unsigned char, 0x650> sceneRoot{};
    uintptr_t image = 0;

    explicit TravelFixture(uintptr_t syntheticImage) : image(syntheticImage) {}
    uintptr_t Manager() const { return reinterpret_cast<uintptr_t>(manager.data()); }
    template<class T> void Global(size_t offset, const T& value) {
        std::memcpy(reinterpret_cast<void*>(image + offset), &value, sizeof(value));
    }
    void SetFlag(uint32_t flag, bool value) {
        auto& byte = flags[0x100 + flag / 8];
        const auto mask = static_cast<unsigned char>(1u << (flag % 8));
        byte = value ? static_cast<unsigned char>(byte | mask) : static_cast<unsigned char>(byte & ~mask);
    }
    void StaticSpot(unsigned index, uint32_t id, uint32_t region, uint32_t area, uint8_t bits = 0) {
        const size_t row = 0x100 + index * 152;
        Put(tableBuffer, row, id); Put(tableBuffer, row + 4, region);
        Put(tableBuffer, row + 8, area); Put(tableBuffer, row + 0x58, bits);
    }
    void StaticArea(unsigned index, uint32_t id, uint32_t region) {
        const size_t row = 0x10 + index * 56;
        Put(tableBuffer, row, id); Put(tableBuffer, row + 4, region);
    }
    void Reset(uint32_t id = 731, uint32_t region = 3, uint32_t area = 47) {
        manager.fill(0xA5); flags.fill(0x5A);
        tableRoot.fill(0); tableHolder.fill(0); tableFile.fill(0); tableHeaders.fill(0);
        tableBuffer.fill(0); sceneRoot.fill(0);
        const uint32_t otherRegion = region == 7 ? 8u : 7u;
        spots = {{{id, area, region, 0, 0, 1, 0x91},
                  {997, area, region, 0, 0, 0, 0x92},
                  {998, 90, otherRegion, 1, 1, 0, 0x93},
                  {995, 90, otherRegion, 0, 1, 0, 0x94},
                  {996, 90, otherRegion, 1, 1, 0, 0x95}}};
        // area=0时无相应聚合组；第一个有效组仍用于检验其他无关行必须保持原样。
        areas = {{{area ? area : 48u, region, 0, 0, {0x81, 0x82}},
                  {90, otherRegion, 1, 0, {0x83, 0x84}},
                  {91, otherRegion, 0, 1, {0x85, 0x86}}}};
        Put(manager, 0xE0, reinterpret_cast<uintptr_t>(spots.data())); Put(manager, 0xE8, uint64_t{3});
        Put(manager, 0xC8, reinterpret_cast<uintptr_t>(areas.data())); Put(manager, 0xD0, uint64_t{2});
        Put(manager, 0xF8, region);
        Global(kFlagsGlobalOffset, reinterpret_cast<uintptr_t>(flags.data()));
        Global(0xC5D778, reinterpret_cast<uintptr_t>(tableRoot.data()));
        Put(tableRoot, 0xF0, reinterpret_cast<uintptr_t>(tableHolder.data()));
        Put(tableHolder, 8, reinterpret_cast<uintptr_t>(tableFile.data()));
        Put(tableFile, 0x10, reinterpret_cast<uintptr_t>(tableBuffer.data()));
        Put(tableFile, 0x20, reinterpret_cast<uintptr_t>(tableHeaders.data()));
        Put(tableFile, 0x28, uint32_t{0}); Put(tableFile, 0x2C, uint32_t{1});
        Put(tableHeaders, 0x44, uint32_t{0x10}); Put(tableHeaders, 0x48, uint32_t{56});
        Put(tableHeaders, 0x4C, uint32_t{2});
        Put(tableHeaders, 0x94, uint32_t{0x100}); Put(tableHeaders, 0x98, uint32_t{152});
        Put(tableHeaders, 0x9C, uint32_t{3});
        for (unsigned i = 0; i < 3; ++i) StaticSpot(i, spots[i].id, spots[i].region, spots[i].area);
        for (unsigned i = 0; i < 2; ++i) StaticArea(i, areas[i].id, areas[i].region);
        Global(0xC60E08, reinterpret_cast<uintptr_t>(sceneRoot.data()));
        SetFlag(6000 + id, false); SetFlag(6500 + id, false);
        tracker::g_explorationBase = image;
        tracker::g_travelUnlock.store(true);
        tracker::g_travelExplicitBuild = false;
    }

    void VerifyCall(TestResult& result, uint32_t spotMask, uint32_t areaMask, const char* label,
                    uintptr_t callerOffset = 0x3DAA88, bool legacyRegistration = false) {
        const auto beforeManager = manager; const auto beforeFlags = flags;
        const auto beforeSpots = spots; const auto beforeAreas = areas;
        const auto beforeRoot = tableRoot; const auto beforeHolder = tableHolder;
        const auto beforeFile = tableFile; const auto beforeHeaders = tableHeaders;
        const auto beforeBuffer = tableBuffer; const auto beforeScene = sceneRoot;
        uintptr_t beforeGlobal = 0, afterGlobal = 0;
        std::memcpy(&beforeGlobal, reinterpret_cast<void*>(image + kFlagsGlobalOffset), sizeof(beforeGlobal));
        if (legacyRegistration) tracker::Sky2AfterRegisterSpot(Manager(), spots[0].id);
        else result.Check(!tracker::Sky2BeforeBuildTravel(Manager(), image + callerOffset),
                          "构建前辅助始终返回false，保留原生显示构建");
        std::memcpy(&afterGlobal, reinterpret_cast<void*>(image + kFlagsGlobalOffset), sizeof(afterGlobal));
        auto expectedSpots = beforeSpots; auto expectedAreas = beforeAreas;
        for (size_t i = 0; i < spots.size(); ++i) if (spotMask & (1u << i)) expectedSpots[i].visible = 1;
        for (size_t i = 0; i < areas.size(); ++i) if (areaMask & (1u << i)) expectedAreas[i].visible = 1;
        result.Check(Same(spots, expectedSpots) && Same(areas, expectedAreas), label);
        result.Check(Same(manager, beforeManager) && Same(flags, beforeFlags) && beforeGlobal == afterGlobal &&
                     Same(tableRoot, beforeRoot) && Same(tableHolder, beforeHolder) && Same(tableFile, beforeFile) &&
                     Same(tableHeaders, beforeHeaders) && Same(tableBuffer, beforeBuffer) && Same(sceneRoot, beforeScene),
                     "只改获准点及分组的visible，保持原始到访位、通知位、静态表和管理器完全不变");
    }
};

void TestTravel(TestResult& result, uintptr_t image, uintptr_t inaccessible) {
    TravelFixture f(image);
    // 覆盖此前未支持的户外、交通、楼层编号以及任意合成编号；分类不再影响候选许可。
    for (uint32_t id : {1u, 33u, 60u, 113u, 155u, 731u, 999u, 1000u}) {
        f.Reset(id);
        f.VerifyCall(result, 1, 1, "任意表内合法点按原生最终状态补显，同时显示未到访分组");
        f.VerifyCall(result, 0, 0, "重复构建幂等，不二次修改已显示对象");
    }
    for (uint32_t region = 1; region <= 9; ++region) {
        f.Reset(731, region);
        f.VerifyCall(result, 1, 1, "当前地区1至9统一评估，不再维护逐点白名单");
    }
    const auto reject = [&](const char* label, auto prepare) {
        f.Reset(); prepare(); f.VerifyCall(result, 0, 0, label);
    };
    reject("功能关闭保持原状", [&] { tracker::g_travelUnlock.store(false); });
    reject("管理器当前地区不同不补显", [&] { Put(f.manager, 0xF8, uint32_t{2}); });
    for (uint32_t value : {0u, 10u})
        reject("管理器地区不在已适配范围时拒绝", [&] { Put(f.manager, 0xF8, value); });
    reject("运行时目标地区与静态表不符拒绝", [&] { ++f.spots[0].region; });
    reject("运行时目标归属与静态表不符拒绝", [&] { ++f.spots[0].area; });
    reject("静态目标地区不符拒绝", [&] { f.StaticSpot(0, 731, 4, 47); });
    reject("静态目标分组不符拒绝", [&] { f.StaticSpot(0, 731, 3, 48); });
    reject("目标最终被原生灰化时不反向解锁", [&] { f.spots[0].blocked = 1; });
    reject("目标最终未登记时不补显", [&] { f.spots[0].registered = 0; });
    reject("已显示目标保持不变，也不凭空补出其分组", [&] { f.spots[0].visible = 1; });
    reject("内部隐藏位bit3拒绝", [&] { f.StaticSpot(0, 731, 3, 47, 8); });
    reject("所属分组被原生灰化时拒绝", [&] { f.areas[0].blocked = 1; });
    reject("静态分组地区不一致拒绝", [&] { f.StaticArea(0, 47, 4); });
    reject("运行时及静态分组都属其他地区时仍拒绝当前候选", [&] {
        f.areas[0].region = 4; f.StaticArea(0, 47, 4);
    });
    reject("非零分组不存在时不凭空创建", [&] {
        f.areas[0].id = 48; f.StaticArea(0, 48, 3);
    });
    reject("运行时点编号重复使整批原样退出", [&] {
        f.spots[3] = f.spots[0]; Put(f.manager, 0xE8, uint64_t{4});
    });
    reject("运行时分组编号重复使整批原样退出", [&] {
        f.areas[2] = f.areas[0]; Put(f.manager, 0xD0, uint64_t{3});
    });
    reject("目标静态记录缺失拒绝", [&] { f.StaticSpot(0, 730, 3, 47); });
    reject("分组静态记录缺失拒绝", [&] { f.StaticArea(0, 48, 3); });
    reject("后部无关点静态记录缺失时前部候选也不得部分提交", [&] { f.StaticSpot(2, 999, 7, 90); });
    reject("后部无关分组静态记录缺失时不得部分提交", [&] { f.StaticArea(1, 91, 7); });
    for (uint8_t value : {uint8_t{2}, uint8_t{255}}) {
        reject("异常目标可见值拒绝整批", [&] { f.spots[0].visible = value; });
        reject("异常目标灰态拒绝整批", [&] { f.spots[0].blocked = value; });
        reject("异常目标登记值拒绝整批", [&] { f.spots[0].registered = value; });
        reject("后部异常状态不得导致先写有效前部点", [&] { f.spots[2].registered = value; });
        reject("异常分组可见值拒绝整批", [&] { f.areas[0].visible = value; });
        reject("异常分组灰态拒绝整批", [&] { f.areas[0].blocked = value; });
    }
    for (uint32_t id : {0u, 1001u})
        reject("运行时异常点编号拒绝整批", [&] { f.spots[0].id = id; });
    reject("运行时零分组编号不能当作area0独立点", [&] { f.areas[0].id = 0; });
    for (uint64_t count : {uint64_t{0}, uint64_t{1002}})
        reject("点位数组空或超容量拒绝", [&] { Put(f.manager, 0xE8, count); });
    reject("分组数组超容量拒绝", [&] { Put(f.manager, 0xD0, uint64_t{65}); });
    reject("非零分组候选在无分组数组时拒绝", [&] { Put(f.manager, 0xD0, uint64_t{0}); });
    reject("不可读点位数组由SEH拒绝", [&] { Put(f.manager, 0xE0, inaccessible); });
    reject("不可读分组数组由SEH拒绝", [&] { Put(f.manager, 0xC8, inaccessible); });
    reject("不可读静态表根对象由SEH拒绝", [&] { f.Global(0xC5D778, inaccessible); });
    reject("不可读静态表缓冲区由SEH拒绝", [&] { Put(f.tableFile, 0x10, inaccessible); });
    reject("不可读静态表头由SEH拒绝", [&] { Put(f.tableFile, 0x20, inaccessible); });
    reject("不可读场景对象由SEH拒绝", [&] { Put(f.sceneRoot, 0x648, inaccessible); });
    reject("静态表步长不匹配拒绝", [&] { Put(f.tableHeaders, 0x98, uint32_t{151}); });
    reject("静态表为空拒绝", [&] { Put(f.tableHeaders, 0x9C, uint32_t{0}); });
    reject("静态表数量异常拒绝", [&] { Put(f.tableHeaders, 0x9C, uint32_t{1025}); });
    reject("静态表索引异常拒绝", [&] { Put(f.tableFile, 0x2C, uint32_t{1025}); });
    reject("静态表偏移超界拒绝", [&] { Put(f.tableHeaders, 0x94, uint32_t{0x1000001}); });

    f.Reset(); f.areas[0].visible = 1;
    f.VerifyCall(result, 1, 0, "已经显示的分组不重复修改，只补其目标点");
    f.Reset(); f.spots[1].registered = 1;
    f.VerifyCall(result, 3, 1, "同组多个独立合格点同次补显，分组只需显示一次");
    f.Reset(); f.spots[1].registered = 1; f.spots[1].blocked = 1;
    f.VerifyCall(result, 1, 1, "同组其他点被灰化不会覆盖当前点许可，也不会被连带放开");
    for (uint8_t bits : {uint8_t{1}, uint8_t{4}, uint8_t{0x80}, uint8_t{0xF7}}) {
        f.Reset(); f.StaticSpot(0, 731, 3, 47, bits);
        f.VerifyCall(result, 1, 1, "静态标志的其他位由原生构建处理，不误判为隐藏");
    }
    // 静态表允许同ID备用入口：必须采用首条匹配，不能合并或借后条解除隐藏。
    f.Reset(); f.StaticSpot(3, 731, 9, 62, 8); Put(f.tableHeaders, 0x9C, uint32_t{4});
    f.VerifyCall(result, 1, 1, "静态重复ID采用原生首匹配记录，后条差异不改变许可");
    f.Reset(); f.StaticSpot(0, 731, 3, 47, 8); f.StaticSpot(3, 731, 3, 47);
    Put(f.tableHeaders, 0x9C, uint32_t{4});
    f.VerifyCall(result, 0, 0, "首匹配静态记录隐藏时不能借备用记录绕过");

    f.Reset(731, 3, 0);
    f.VerifyCall(result, 1, 0, "area0独立点无需补显无关分组");
    f.Reset(731, 3, 0);
    Put(f.manager, 0xD0, uint64_t{0}); Put(f.manager, 0xC8, uintptr_t{0});
    f.VerifyCall(result, 1, 0, "area0点在没有运行时分组数组时仍可显示");
    for (bool visited : {false, true}) {
        f.Reset(); f.SetFlag(6000 + 731, visited); f.SetFlag(6500 + 731, !visited);
        f.VerifyCall(result, 1, 1, "统一候选不依赖真实到访或新点通知值，并保持原始位图不变");
    }
    f.Reset(); f.Global(kFlagsGlobalOffset, inaccessible);
    f.VerifyCall(result, 1, 1, "构建前统一判断不读取或试写保存数据管理器");
}

void TestTravelBuild(TestResult& result, uintptr_t image, uintptr_t inaccessible) {
    TravelFixture f(image);
    for (uintptr_t caller : {uintptr_t{0x3DAA88}, uintptr_t{0x3DB001}, uintptr_t{0x3DB3AA}}) {
        f.Reset(); f.VerifyCall(result, 1, 1, "三个完整原生脚本后的构建调用源均许可补显", caller);
    }
    for (uintptr_t caller : {uintptr_t{0}, uintptr_t{0x29EE97}, uintptr_t{0x3DAA87}, uintptr_t{0x3DAA89}}) {
        f.Reset(); f.VerifyCall(result, 0, 0, "初始化、未知及相邻错误调用地址不得补显", caller);
    }
    f.Reset(); tracker::g_travelExplicitBuild = true;
    f.VerifyCall(result, 1, 1, "显式刷新同步许可允许非原生调用地址完成统一评估", 0x29EE97);
    tracker::g_travelExplicitBuild = false;
    f.Reset(); tracker::g_travelExplicitBuild = true; tracker::g_travelUnlock.store(false);
    f.VerifyCall(result, 0, 0, "显式同步许可不能绕过功能关闭", 0x29EE97);
    tracker::g_travelExplicitBuild = false;
    f.Reset(); f.VerifyCall(result, 0, 0, "旧登记后桥仅为ABI兼容，不提前补显未完成剧情计算的数据", 0, true);
    f.spots[0].blocked = 1;
    f.VerifyCall(result, 0, 0, "原生后续设置的最终灰态始终优先");
    f.Reset();
    result.Check(!tracker::Sky2BeforeBuildTravel(inaccessible, image + 0x3DAA88),
                 "不可读管理器保持原生构建执行，不进入部分修改");
}

struct MapFixture {
    std::array<unsigned char, 0x118> map{};
    std::array<unsigned char, 3 * 0x50> chunks{};
    std::array<unsigned char, 0x80> node{};
    uintptr_t image = 0, caller = 0, argumentNode = 0, argumentChunk = 0;
    float originalAlpha = 0.125f;

    explicit MapFixture(uintptr_t syntheticImage) : image(syntheticImage) {}
    uintptr_t Map() const { return reinterpret_cast<uintptr_t>(map.data()); }
    void Reset() {
        map.fill(0xA1); chunks.fill(0xB2); node.fill(0xC3);
        const auto start = reinterpret_cast<uintptr_t>(chunks.data());
        // 使用中间行而非第一行，覆盖行首对齐、数组边界和索引计算。
        argumentChunk = start + 0x50;
        argumentNode = reinterpret_cast<uintptr_t>(node.data());
        caller = image + kMapCallerOffset;
        Put(map, 0x108, start); Put(map, 0x110, uint64_t{3});
        Put(map, 0xA8, uint8_t{1}); Put(map, 0xF8, 0.625f);
        Put(chunks, 0x50 + 0x20, uint32_t{17});
        Put(chunks, 0x50 + 0x28, argumentNode);
        Put(chunks, 0x50 + 0x48, uint8_t{1});
        tracker::g_explorationBase = image;
        tracker::g_mapReveal.store(true);
    }
    void VerifyCall(TestResult& result, float expected, const char* label) {
        const auto beforeMap = map;
        const auto beforeNode = node;
        const auto beforeChunks = chunks;
        const float actual = tracker::Sky2MapAlpha(nullptr, Map(), argumentNode, originalAlpha, caller, argumentChunk);
        result.Check(actual == expected, label);
        result.Check(Same(map, beforeMap) && Same(chunks, beforeChunks) && Same(node, beforeNode),
                     "地图全显只返回 alpha，不修改地图、区块或渲染节点内存");
    }
};

void TestMap(TestResult& result, uintptr_t image, uintptr_t inaccessible) {
    MapFixture fixture(image);
    fixture.Reset();
    fixture.VerifyCall(result, 0.625f, "真实区块与节点匹配时采用当前楼层 alpha");
    const auto reject = [&](const char* label, auto prepare) {
        fixture.Reset(); prepare();
        fixture.VerifyCall(result, fixture.originalAlpha, label);
    };
    reject("地图全显关闭时保留原 alpha", [&] { tracker::g_mapReveal.store(false); });
    reject("非指定调用点保留原 alpha", [&] { ++fixture.caller; });
    reject("节点参数与区块节点不匹配时保留原 alpha", [&] { ++fixture.argumentNode; });
    reject("区块参数位于行内部时保留原 alpha", [&] { ++fixture.argumentChunk; });
    reject("区块参数位于数组之外时保留原 alpha", [&] { fixture.argumentChunk += 2 * 0x50; });
    reject("脚本禁用地图时保留原 alpha", [&] { Put(fixture.map, 0xA8, uint8_t{0}); });
    reject("脚本禁用区块时保留原 alpha", [&] { Put(fixture.chunks, 0x98, uint8_t{0}); });
    reject("异常地图开关值不会被视为启用", [&] { Put(fixture.map, 0xA8, uint8_t{2}); });
    reject("异常区块开关值不会被视为启用", [&] { Put(fixture.chunks, 0x98, uint8_t{2}); });
    reject("区块 ID 超上限时保留原 alpha", [&] { Put(fixture.chunks, 0x70, uint32_t{204}); });
    reject("空区块数组保留原 alpha", [&] { Put(fixture.map, 0x110, uint64_t{0}); });
    reject("区块计数超上限时保留原 alpha", [&] { Put(fixture.map, 0x110, uint64_t{205}); });
    reject("无效低地址区块数组保留原 alpha", [&] { Put(fixture.map, 0x108, uintptr_t{0}); });
    reject("不可读取区块由 SEH 保留原 alpha", [&] {
        Put(fixture.map, 0x108, inaccessible); fixture.argumentChunk = inaccessible;
    });
    for (float alpha : {0.0f, -1.0f, 1.25f, std::numeric_limits<float>::infinity(),
                        std::numeric_limits<float>::quiet_NaN()}) {
        reject("非法楼层透明度保留原 alpha", [&] { Put(fixture.map, 0xF8, alpha); });
    }
    fixture.Reset();
    const auto before = fixture.map;
    result.Check(tracker::Sky2MapAlpha(nullptr, inaccessible, fixture.argumentNode, fixture.originalAlpha,
                                     fixture.caller, fixture.argumentChunk) == fixture.originalAlpha &&
                 Same(fixture.map, before), "不可读取的地图对象由 SEH 安全拒绝");
}
}

#include "travel_menu_runtime_fixture.h"

int main() {
    OwnMemory image(kSyntheticImageSize), inaccessible(4096, PAGE_NOACCESS);
    if (!image.Address() || !inaccessible.Address()) {
        std::puts("FAIL: 无法分配当前测试进程的合成内存");
        return 1;
    }
    TestResult result;
    TestTravel(result, image.Address(), inaccessible.Address());
    TestTravelBuild(result, image.Address(), inaccessible.Address());
    TestMap(result, image.Address(), inaccessible.Address());
    TestTravelMenu(result, image.Address(), inaccessible.Address());
    tracker::g_explorationBase = 0;
    tracker::g_travelUnlock.store(false);
    tracker::g_mapReveal.store(false);
    std::printf("%u runtime checks, %u failure(s)\n", result.checks, result.failures);
    return result.failures ? 1 : 0;
}
