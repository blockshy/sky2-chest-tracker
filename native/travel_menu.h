// 传送菜单的即时刷新适配层。此文件只在 exploration.cpp 的 tracker 命名空间内包含。
// 所有游戏函数均由原生地图输入更新线程调用；渲染/输入线程只提交 TravelRefreshState 请求。
// 这里的偏移及 ABI 仅适用于主模块已经核对完整 SHA-256 的游戏版本。
#pragma once

struct NativeTravelApi {
    using Execute = uint32_t* (*)(uintptr_t, uint32_t*, const char*, uintptr_t, uintptr_t, uint32_t);
    using Unary = void (*)(uintptr_t);
    using Select = void (*)(uintptr_t, int32_t, int32_t, bool);
    Execute executeScript = nullptr;
    Unary buildDisplay = nullptr;
    Unary clearList = nullptr;
    Unary buildSpotList = nullptr;
    Unary buildAreaList = nullptr;
    Select selectList = nullptr;
    Unary onSpotSelection = nullptr;
    Unary onAreaSelection = nullptr;
};
// 集中保存已验证的原生入口，方便本地合成测试替换；不保存跨帧的游戏对象指针。
static NativeTravelApi g_travelApi{};
// 仅由原生更新线程访问，不缓存游戏指针。极少见的菜单字段写入失败时，阻止空列表的
// 原状态继续运行，直到能够提交原生退出请求；一旦提交便立即清除此保护。
static bool g_travelClosePending = false;

struct TravelDisplayIdentity {
    uint32_t type = 0, id = 0;
    bool valid = false;
};
struct TravelDisplayRange { uintptr_t data = 0; uint64_t count = 0; };
struct TravelListView {
    uintptr_t object = 0, items = 0;
    uint64_t count = 0;
    int32_t selected = 0;
};
struct TravelMenuContext {
    uintptr_t menu = 0, manager = 0, savedata = 0;
    int32_t depth = 0, state = 0;
    uint32_t region = 0, area = 0;
    float scale = 0;
    TravelDisplayRange display{};
    TravelListView spots{}, areas{};
    TravelDisplayIdentity selected{}, selectedSpot{}, selectedArea{};
};

// 写入对象均已在同一原生调用栈中验证。只修改菜单指针、运行时 UI 字段及状态机请求，
// 不写保存数据或全局旗标；保留 SEH 仅约束本模块额外 memcpy 的访问故障。
template<class T> static bool WriteTravel(uintptr_t address, const T& value) noexcept {
    if (address < 0x10000) return false;
    __try { std::memcpy(reinterpret_cast<void*>(address), &value, sizeof(value)); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
static bool DisplayRange(uintptr_t manager, TravelDisplayRange& result, bool allowEmpty = false) noexcept {
    uint64_t capacity = 0;
    return Read(manager + 0xB0, result.data) && Read(manager + 0xB8, result.count) &&
        Read(manager + 0xC0, capacity) && result.count <= 1024 && capacity >= result.count && capacity <= 4096 &&
        (result.count ? result.data >= 0x10000 : allowEmpty && (!result.data || result.data >= 0x10000));
}
static bool DisplayAddress(const TravelDisplayRange& range, uintptr_t address) noexcept {
    return address >= range.data && (address - range.data) % 0x40 == 0 &&
        (address - range.data) / 0x40 < range.count;
}
static bool DisplayIdentity(const TravelDisplayRange& range, uintptr_t address,
                            TravelDisplayIdentity& result) noexcept {
    result = {};
    if (!address) return true;
    if (!DisplayAddress(range, address) || !Read(address + 0x08, result.type) ||
        !Read(address + 0x10, result.id) || result.type > 1 || result.id > 1000) return false;
    result.valid = true;
    return true;
}
static uintptr_t FindDisplay(const TravelDisplayRange& range,
                             const TravelDisplayIdentity& identity) noexcept {
    if (!identity.valid) return 0;
    uintptr_t found = 0;
    for (uint64_t i = 0; i < range.count; ++i) {
        const uintptr_t address = range.data + i * 0x40;
        TravelDisplayIdentity candidate{};
        if (!DisplayIdentity(range, address, candidate)) return 0;
        if (candidate.type == identity.type && candidate.id == identity.id) {
            // 身份重复代表表结构不符合预期，不能猜测应绑定其中哪个指针。
            if (found) return 0;
            found = address;
        }
    }
    return found;
}
static bool FindRuntimeSpot(uintptr_t manager, uint32_t id, TravelSpotState& result) noexcept {
    uintptr_t data = 0;
    uint64_t count = 0;
    if (!Read(manager + 0xE0, data) || !Read(manager + 0xE8, count) || data < 0x10000 ||
        !count || count > 1001) return false;
    unsigned found = 0;
    for (uint64_t i = 0; i < count; ++i) {
        TravelSpotState row{};
        if (!Read(data + i * sizeof(row), row)) return false;
        if (row.id == id) { result = row; ++found; }
    }
    return found == 1;
}
struct NativeTravelTable {
    uintptr_t rows = 0;
    uint32_t stride = 0, count = 0;
};
struct NativeTravelTables {
    NativeTravelTable areas{}, spots{};
    bool specialScene = false;
};
struct NativeTravelRow {
    uint32_t region = 0, area = 0;
    uint8_t flags = 0;
};
static bool ReadTravelTable(uintptr_t file, uintptr_t buffer, uintptr_t headers,
                            uintptr_t indexOffset, uint32_t expectedStride,
                            NativeTravelTable& table) noexcept {
    uint32_t index = 0, offset = 0;
    if (!Read(file + indexOffset, index) || index > 1024) return false;
    const uintptr_t header = headers + static_cast<uintptr_t>(index) * 0x50;
    if (!Read(header + 0x44, offset) || !Read(header + 0x48, table.stride) ||
        table.stride != expectedStride || !Read(header + 0x4C, table.count) ||
        !table.count || table.count > 1024 || offset > 0x1000000) return false;
    table.rows = buffer + offset;
    // 对本版本记录长度作精确约束，并验证首尾记录均可读；每个实际匹配的记录稍后还
    // 会完整读取。以上边界也保证原生使用的32位 stride*index 不会溢出。
    unsigned char record[0x98]{};
    return table.rows >= buffer && ReadMemory(table.rows, record, table.stride) &&
        ReadMemory(table.rows + static_cast<uintptr_t>(table.count - 1) * table.stride,
                   record, table.stride);
}
static bool ReadTravelTables(NativeTravelTables& tables) noexcept {
    uintptr_t owner = 0, holder = 0, file = 0, buffer = 0, headers = 0, sceneRoot = 0, scene = 0;
    uint32_t sceneType = 0;
    if (!Read(g_explorationBase + 0xC5D778, owner) || owner < 0x10000 ||
        !Read(owner + 0xF0, holder) || holder < 0x10000 || !Read(holder + 8, file) || file < 0x10000 ||
        !Read(file + 0x10, buffer) || buffer < 0x10000 ||
        !Read(file + 0x20, headers) || headers < 0x10000 ||
        !ReadTravelTable(file, buffer, headers, 0x28, 0x38, tables.areas) ||
        !ReadTravelTable(file, buffer, headers, 0x2C, 0x98, tables.spots) ||
        !Read(g_explorationBase + 0xC60E08, sceneRoot) || sceneRoot < 0x10000 ||
        !Read(sceneRoot + 0x648, scene) || (scene && !Read(scene + 0x98, sceneType))) return false;
    tables.specialScene = scene && sceneType == 6;
    return true;
}
static bool FindNativeTravelRow(const NativeTravelTable& table, uint32_t id,
                                bool spot, NativeTravelRow& row) noexcept {
    for (uint32_t i = 0; i < table.count; ++i) {
        const uintptr_t address = table.rows + static_cast<uintptr_t>(i) * table.stride;
        uint32_t candidate = 0;
        if (!Read(address, candidate)) return false;
        if (candidate != id) continue;
        unsigned char record[0x98]{};
        if (!ReadMemory(address, record, table.stride)) return false;
        // 原生静态表允许相同ID的备用记录，其查找顺序是首个匹配项；不能自行合并，
        // 否则对筛选条件的预测可能与紧接着调用的原生构建函数不同。
        std::memcpy(&row.region, record + 4, sizeof(row.region));
        if (spot) std::memcpy(&row.area, record + 8, sizeof(row.area));
        row.flags = record[spot ? 0x58 : 0x28];
        return true;
    }
    return false;
}
// 区分“结构错误”和“合法但列表已空”：统一补显允许整个未到访区域出现，关闭后
// 该区域自然会消失。这是正常的开关结果，不能误判为适配失效并永久停用功能。
enum class TravelCandidateState { Invalid, Empty, Ready };
static TravelCandidateState InspectTravelCandidates(const TravelMenuContext& context,
                                                     const TravelDisplayRange& range) noexcept {
    NativeTravelTables tables{};
    if (!ReadTravelTables(tables)) return TravelCandidateState::Invalid;
    uint64_t mainCount = 0, areaCount = 0;
    for (uint64_t i = 0; i < range.count; ++i) {
        const uintptr_t address = range.data + i * 0x40;
        TravelDisplayIdentity identity{};
        NativeTravelRow row{};
        uint32_t displayFlags = 0;
        if (!DisplayIdentity(range, address, identity) || !Read(address + 0x0C, displayFlags) ||
            !FindNativeTravelRow(identity.type ? tables.spots : tables.areas,
                                  identity.id, identity.type == 1, row)) return TravelCandidateState::Invalid;
        if (identity.type == 1) {
            // 3DF330 的主列表先按场景/区域聚合标志筛选，再排除表中 bit3 隐藏项；
            // 不能仅因“某个入口真实到访”就断言该入口一定进入主列表。
            const bool admitted = tables.specialScene ? (row.flags & 0x80) != 0 :
                row.area == 0 || (displayFlags & 4) != 0;
            if (admitted && !(row.flags & 8)) ++mainCount;
            // 3DE790 子列表只按静态表 areaID 过滤。它对找不到ID的项直接解引用空
            // 指针，因此必须验证所有 type=1 项有静态记录，而不只是当前区域的项。
            if (context.area && row.area == context.area) ++areaCount;
        } else if (!tables.specialScene || (row.flags & 8)) {
            ++mainCount;
        }
    }
    // 原生构建函数末尾立即触发选择回调，空列表会在函数返回前崩溃。必须在调用
    // 之前按其完整筛选规则证明非空，不能依赖构建后的 count 检查补救。
    return (!range.count || (context.state >= 4 && !mainCount) || (context.state == 5 && !areaCount)) ?
        TravelCandidateState::Empty : TravelCandidateState::Ready;
}
static bool ReadTravelList(uintptr_t object, uintptr_t menu, bool areaList,
                           TravelListView& result) noexcept {
    uintptr_t vtable = 0, owner = 0, callback = 0, callbackTable = 0, callbackCode = 0, callbackOwner = 0;
    if (object < 0x10000 || !Read(object, vtable) ||
        vtable != g_explorationBase + (areaList ? 0xB0DC28 : 0xB0DC88) ||
        !Read(object + 0xF8, owner) || owner != menu || !Read(object + 0x138, callback) ||
        !Read(callback, callbackTable) || callbackTable != g_explorationBase + 0xB0DB48 ||
        !Read(callback + 8, callbackCode) ||
        callbackCode != g_explorationBase + (areaList ? 0x3EB210 : 0x3EB2D0) ||
        !Read(callback + 0x10, callbackOwner) || callbackOwner != menu) return false;
    result.object = object;
    if (!Read(object + 0x38, result.items) || !Read(object + 0x40, result.count) ||
        !Read(object + 0x70, result.selected) || result.count > 1024) return false;
    return !result.count || (result.items >= 0x10000 && result.selected >= 0 &&
        static_cast<uint64_t>(result.selected) < result.count);
}
static bool ListDisplay(const TravelListView& list, uint64_t index, uintptr_t& display) noexcept {
    uintptr_t item = 0, vtable = 0;
    return index < list.count && Read(list.items + index * 8, item) && Read(item, vtable) &&
        vtable == g_explorationBase + 0xB0DCF8 && Read(item + 0x28, display);
}
static bool ListIdentity(const TravelListView& list, const TravelDisplayRange& range,
                         TravelDisplayIdentity& identity) noexcept {
    identity = {};
    if (!list.count) return true;
    uintptr_t display = 0;
    return ListDisplay(list, static_cast<uint64_t>(list.selected), display) &&
        DisplayIdentity(range, display, identity) && identity.valid;
}

static bool ReadTravelMenu(uintptr_t menu, TravelMenuContext& context) noexcept {
    context.menu = menu;
    uintptr_t reverse = 0, self = 0, spotList = 0, areaList = 0, selected = 0, previous = 0, leaveCallback = 0;
    int32_t updateDepth = 0, type = 0, frames[12]{};
    uint8_t active = 0, travel = 0, worldMenu = 0, worldManager = 0;
    if (!Read(menu + 8, context.manager) || !Read(context.manager + 0x28, reverse) || reverse != menu ||
        !Read(menu + 0x18, self) || self != menu || !Read(menu + 0xF0, leaveCallback) || leaveCallback != 0 ||
        !Read(menu + 0xE8, context.depth) ||
        !Read(menu + 0xEC, updateDepth) || updateDepth != context.depth || context.depth < 0 ||
        context.depth > 2 || !ReadMemory(menu + 0xB8, frames, sizeof(frames)) ||
        !Read(menu + 0x279, active) || active != 1 || !Read(menu + 0x27A, travel) || travel != 0 ||
        !Read(menu + 0x27B, worldMenu) || worldMenu != 0 || !Read(menu + 0x3C8, type) || type != 0 ||
        !Read(context.manager + 0x30A, worldManager) || worldManager != 0 ||
        !Read(context.manager + 0xF8, context.region) || context.region < 1 || context.region > 9 ||
        !Read(context.manager + 0x104, context.scale) || !std::isfinite(context.scale) ||
        context.scale < 0.001f || context.scale > 100.0f ||
        !Read(g_explorationBase + 0xC60E58, context.savedata) || context.savedata < 0x10000) return false;
    context.state = frames[context.depth * 3];
    // 仅接受正常浏览栈：3；3→4；3→5；3→4→5。确认、加载、退出及切图一律延期。
    if (frames[0] != 3 || frames[1] != 3 || context.state < 3 || context.state > 5 ||
        frames[context.depth * 3 + 1] != context.state || frames[context.depth * 3 + 2] <= 0 ||
        (context.state == 3 && context.depth != 0) ||
        (context.state == 4 && context.depth != 1) ||
        (context.state == 5 && context.depth < 1) ||
        (context.depth == 2 && (frames[3] != 4 || frames[4] != 4))) return false;
    // 只有纯地图浏览允许原生尚无任何目的地；活动列表依然必须证明非空。
    // 这让全未到访地区也能提交首次开启请求，而不重新引入真实入口锚点。
    if (!DisplayRange(context.manager, context.display, context.state == 3) || !Read(menu + 0x190, selected) ||
        !DisplayIdentity(context.display, selected, context.selected) || !Read(menu + 0x198, previous) ||
        (previous && !DisplayAddress(context.display, previous)) ||
        !Read(menu + 0x1C8, spotList) || !Read(menu + 0x1C0, areaList) ||
        !ReadTravelList(spotList, menu, false, context.spots) ||
        !ReadTravelList(areaList, menu, true, context.areas)) return false;
    if (context.state >= 4 && (!context.spots.count && context.state == 4)) return false;
    // 未显示的列表可能保留上次地图的旧引用，只清理它，不读取其中的显示项身份。
    if ((context.state == 4 || context.depth == 2) &&
        !ListIdentity(context.spots, context.display, context.selectedSpot)) return false;
    if (context.state == 5) {
        if (!context.areas.count || !ListIdentity(context.areas, context.display, context.selectedArea)) return false;
        // 子列表中的光标已把 menu+190 改为具体 spot。由全部旧列表条目证明唯一 area，
        // 不能把此刻选择项的 ID 当作 area，也不能用父列表的任意默认选择猜测。
        for (uint64_t i = 0; i < context.areas.count; ++i) {
            uintptr_t address = 0;
            TravelDisplayIdentity identity{};
            TravelSpotState spot{};
            if (!ListDisplay(context.areas, i, address) ||
                !DisplayIdentity(context.display, address, identity) || !identity.valid || identity.type != 1 ||
                !FindRuntimeSpot(context.manager, identity.id, spot) || !spot.area || spot.area > 33 ||
                (context.area && context.area != spot.area)) return false;
            context.area = spot.area;
        }
    }
    // 所有旧上下文和静态表校验均在修改选择或销毁GUI之前完成。不支持的结构保持
    // 请求待处理，原生输入继续运行。不再要求存在真实到访锚点：统一规则下，整个
    // 活动区域都可能是临时补显；上下文身份改由对象互指、区域和菜单状态共同验证。
    const auto candidates = InspectTravelCandidates(context, context.display);
    return candidates == TravelCandidateState::Ready ||
        (context.state == 3 && candidates == TravelCandidateState::Empty);
}

static bool RunTravelScript(const TravelMenuContext& context) noexcept {
    uint32_t result = 0;
    // 原生脚本本身的异常不能吞掉。本模块只验证调用前提，不把访问异常当作成功刷新。
    if (g_travelApi.executeScript(context.savedata, &result, "system.MapJumpState", 0, 0, 0) != &result)
        return false;
    uintptr_t reverse = 0, manager = 0, savedata = 0;
    uint32_t region = 0;
    int32_t depth = 0, current = 0, next = 0;
    uint8_t active = 0, travel = 0;
    // MapJumpState 是一次真实的原生状态重算。只检查它仍属于同一菜单/存档对象，
    // 不以“已有到访点”为门槛，也不额外执行带副作用的传送确认脚本来探测权限。
    const uintptr_t frame = context.menu + 0xB8 + static_cast<uintptr_t>(context.depth) * 12;
    return Read(context.manager + 0x28, reverse) && reverse == context.menu &&
        Read(context.menu + 8, manager) && manager == context.manager &&
        Read(g_explorationBase + 0xC60E58, savedata) && savedata == context.savedata &&
        Read(context.manager + 0xF8, region) && region == context.region &&
        Read(context.menu + 0xE8, depth) && depth == context.depth &&
        Read(frame, current) && current == context.state && Read(frame + 4, next) && next == context.state &&
        Read(context.menu + 0x279, active) && active == 1 && Read(context.menu + 0x27A, travel) && travel == 0;
}

static TravelCandidateState PrepareDisplay(const TravelMenuContext& context, TravelDisplayRange& range) noexcept {
    if (!DisplayRange(context.manager, range, true)) return TravelCandidateState::Invalid;
    uintptr_t root = 0;
    uint32_t flags = 0;
    if (!Read(context.manager + 0x70, root) || !Read(root + 0xA0, flags)) return TravelCandidateState::Invalid;
    for (uint64_t i = 0; i < range.count; ++i) {
        const uintptr_t address = range.data + i * 0x40;
        TravelDisplayIdentity identity{};
        uint8_t modeled = 0;
        uintptr_t ui = 0;
        unsigned char tween[0x3C]{};
        if (!DisplayIdentity(range, address, identity) || !Read(address + 0x21, modeled) || modeled > 1)
            return TravelCandidateState::Invalid;
        if (modeled && (!Read(address, ui) || !Read(ui + 0xA0, flags) ||
            !ReadMemory(ui + 0x148, tween, sizeof(tween)))) return TravelCandidateState::Invalid;
    }
    return InspectTravelCandidates(context, range);
}
static bool ShowDisplay(const TravelMenuContext& context, const TravelDisplayRange& range) noexcept {
    const float scale = 6.0f / context.scale;
    const uint64_t zero64 = 0;
    const uint32_t zero32 = 0;
    for (uint64_t i = 0; i < range.count; ++i) {
        const uintptr_t address = range.data + i * 0x40;
        uintptr_t ui = 0;
        uint8_t modeled = 0;
        uint32_t flags = 0;
        if (!Read(address + 0x21, modeled)) return false;
        if (!modeled) continue;
        if (!Read(address, ui) || !Read(ui + 0xA0, flags) || !WriteTravel(ui + 0xA0, flags & ~1u)) return false;
        // 精确复用原生打开地图时的图标比例初始化。只操作 scale tween，保留位置、旋转、
        // 相机缩放和地图层级，不调用会把玩家滚动位置重置的地图切换函数。
        for (uintptr_t group = 0; group <= 0x20; group += 0x10) {
            if (!WriteTravel(ui + 0x148 + group, scale) || !WriteTravel(ui + 0x14C + group, scale) ||
                !WriteTravel(ui + 0x150 + group, scale)) return false;
        }
        if (!WriteTravel(ui + 0x178, zero64) || !WriteTravel(ui + 0x180, zero32)) return false;
    }
    uintptr_t root = 0;
    uint32_t flags = 0;
    return Read(context.manager + 0x70, root) && Read(root + 0xA0, flags) &&
        WriteTravel(root + 0xA0, flags & ~1u);
}
static bool RestoreListSelection(uintptr_t menu, uintptr_t object, bool areaList,
                                  const TravelDisplayRange& range,
                                  const TravelDisplayIdentity& previous) noexcept {
    TravelListView list{};
    if (!ReadTravelList(object, menu, areaList, list) || !list.count) return false;
    int32_t selected = 0;
    unsigned matches = 0;
    for (uint64_t i = 0; i < list.count; ++i) {
        uintptr_t display = 0;
        TravelDisplayIdentity identity{};
        if (!ListDisplay(list, i, display) || !DisplayIdentity(range, display, identity) || !identity.valid)
            return false;
        if (previous.valid && identity.type == previous.type && identity.id == previous.id) {
            selected = static_cast<int32_t>(i);
            ++matches;
        }
    }
    if (matches > 1) selected = 0;
    g_travelApi.selectList(object, selected, -1, false);
    // SelectList 仅布局；独立调用对应原生回调，令 menu+190 与详情栏使用同一个新选择。
    (areaList ? g_travelApi.onAreaSelection : g_travelApi.onSpotSelection)(menu);
    uintptr_t current = 0;
    return Read(menu + 0x190, current) && DisplayAddress(range, current);
}

static bool RebuildTravelLists(const TravelMenuContext& context,
                               const TravelDisplayRange& range) noexcept {
    const uintptr_t zero = 0;
    if (context.state == 3) {
        const uintptr_t selection = FindDisplay(range, context.selected);
        return WriteTravel(context.menu + 0x190, selection) && WriteTravel(context.menu + 0x198, zero);
    }
    // 总列表也要重建：子列表取消时原生会访问其选中项，不能把隐藏父列表留成悬空引用。
    g_travelApi.buildSpotList(context.spots.object);
    if (!RestoreListSelection(context.menu, context.spots.object, false, range, context.selectedSpot)) return false;
    if (context.state == 5) {
        // 关闭辅助可能使一个城镇只剩入口，原生会移除 type=0 聚合图标。
        // 子列表构建只读取一次 menu+190+0x10 的 areaID，因此用有界栈上下文提供该整数，
        // 不伪造可传送项。构建末尾原生回调会绑定新列表项；返回后还会检查地址不逃逸。
        alignas(8) unsigned char areaContext[0x40]{};
        std::memcpy(areaContext + 0x10, &context.area, sizeof(context.area));
        const uintptr_t temporary = reinterpret_cast<uintptr_t>(areaContext);
        if (!WriteTravel(context.menu + 0x190, temporary)) return false;
        g_travelApi.buildAreaList(context.areas.object);
        // 即使原生回调未按约执行，也先移走临时地址，绝不把栈指针留给下一帧。
        uintptr_t current = 0;
        if (!Read(context.menu + 0x190, current) || !DisplayAddress(range, current)) {
            WriteTravel(context.menu + 0x190, zero);
            WriteTravel(context.menu + 0x198, zero);
            return false;
        }
        if (!RestoreListSelection(context.menu, context.areas.object, true, range, context.selectedArea)) return false;
    }
    return WriteTravel(context.menu + 0x198, zero);
}

static bool RequestNativeMapClose(uintptr_t menu) noexcept {
    int32_t depth = 0, current = 0;
    uint8_t travel = 1;
    uintptr_t owner = 0, callback = 0, leaveCallback = 0;
    const uintptr_t zero = 0;
    const int32_t closeState = 17;
    // 状态17在+27A!=0时会走原生传送分支，故必须再次证明没有确认中的传送。
    // +F0是旧状态离开回调；当前版本普通地图为nullptr，未知回调不能在清空列表后触发。
    if (!Read(menu + 0xE8, depth) || depth < 0 || depth > 2 ||
        !Read(menu + 0x27A, travel) || travel != 0 || !Read(menu + 0xF0, leaveCallback) || leaveCallback ||
        !Read(menu + 0x18, owner) || owner != menu || !Read(menu + 0xF8, callback) ||
        !Read(menu + 0xB8 + static_cast<uintptr_t>(depth) * 12, current) || current < 3 || current > 5 ||
        !WriteTravel(menu + 0x190, zero) || !WriteTravel(menu + 0x198, zero)) return false;
    // 与原生3E48A0..3E48C3相同：保留原退出请求通知，再提交 next=17。
    if (callback) reinterpret_cast<void (*)(uintptr_t, int32_t, int32_t)>(callback)(owner, current, closeState);
    return WriteTravel(menu + 0xBC + static_cast<uintptr_t>(depth) * 12, closeState);
}
static bool FailTravelRefresh(uintptr_t menu, const TravelRefreshTicket& ticket, const char* reason) noexcept {
    // 即使原生挂钩仍装着，也不允许日后正常打开地图时继续解锁；用户界面同时标为不可用。
    g_travelUnlock.store(false, std::memory_order_relaxed);
    g_travelAvailable.store(false);
    g_travelRefresh.Cancel(ticket);
    g_travelClosePending = !RequestNativeMapClose(menu);
    Log(reason);
    return true;
}

// 返回 true 要求汇编桥跳过本帧原状态输入。成功刷新也跳过这一帧，防止同帧按下确认时
// 选择已因关闭辅助而移到别的地点。原生 Menu::Update 仍继续执行自己的动画/后续工作。
extern "C" bool Sky2BeforeMapRefresh(uintptr_t menu) noexcept {
    if (g_travelClosePending) {
        g_travelClosePending = !RequestNativeMapClose(menu);
        return true;
    }
    // 回访成功提交后须跳过本帧旧菜单输入；不与同帧的目的地列表重建交叉执行。
    // 原生关闭失败的恢复分支优先，避免接管已经处于故障收尾中的菜单。
    if (BeforeRevisitNativeBrowse(menu)) return true;
    if (!g_travelAvailable.load(std::memory_order_relaxed) || !g_travelRefresh.ReadStatus().pending) return false;
    TravelMenuContext context{};
    if (!ReadTravelMenu(menu, context)) return false;
    const auto ticket = g_travelRefresh.TryBegin(true);
    if (!ticket) return false;
    g_travelUnlock.store(ticket.Enabled(), std::memory_order_relaxed);
    if (!RunTravelScript(context)) {
        // 尚未销毁GUI，但无法证明缓存与显示一致，也不能继续接受传送确认。
        return FailTravelRefresh(menu, ticket,
            "Exploration: native travel-state validation failed; closing map and disabling travel assistance.");
    }
    const uintptr_t zero = 0;
    // 先移走所有旧引用，再释放其依赖的显示数组，防止 native callbacks 访问已释放项。
    if (!WriteTravel(menu + 0x190, zero) || !WriteTravel(menu + 0x198, zero))
        return FailTravelRefresh(menu, ticket,
            "Exploration: selection reset failed before UI rebuild; requesting native map close.");
    g_travelApi.clearList(context.spots.object);
    g_travelApi.clearList(context.areas.object);
    // 许可精确包围本次同步 Build，不覆盖前面的脚本执行，避免把场景初始化当作
    // 已完成剧情重算。保留外层值兼容同步重入；没有任何游戏指针保存到下一帧。
    const bool previousBuildPermit = g_travelExplicitBuild;
    g_travelExplicitBuild = true;
    g_travelApi.buildDisplay(context.manager);
    g_travelExplicitBuild = previousBuildPermit;
    TravelDisplayRange display{};
    const auto candidates = PrepareDisplay(context, display);
    if (candidates == TravelCandidateState::Empty) {
        // 关闭功能使未到访区域或整个清单消失时，不调用必读第0项的原生列表构建。
        // 若原生重算在开启时也得到合法空结果，同样无需判为结构损坏或永久停用。
        // 沿已验证的退出动画正常关图；功能仍可再次开启，新到达的切换请求仍保留。
        // 仅保存“尚待提交退出”布尔量，绝不跨帧缓存菜单/列表指针。
        g_travelClosePending = !RequestNativeMapClose(menu);
        g_travelRefresh.Complete(ticket);
        Log(ticket.Enabled() ?
            "Exploration: unvisited destinations enabled; requesting normal close for empty travel list." :
            "Exploration: unvisited destinations disabled; requesting normal close for empty travel list.");
        return true;
    }
    if (candidates == TravelCandidateState::Ready && ShowDisplay(context, display) &&
        RebuildTravelLists(context, display)) {
        g_travelRefresh.Complete(ticket);
        Log(ticket.Enabled() ? "Exploration: travel destinations refreshed immediately (enabled)." :
            "Exploration: travel destinations refreshed immediately (disabled).");
        return true;
    }
    // 后置验证失败时，空列表并不安全：原生子列表取消回调会解引用当前选择。
    // 请求已验证的原生关闭状态17，由原生动画结束后转18，再由 manager 销毁 Menu。
    // 绝不在当前 Menu::Update 的调用栈中直接 delete，也不从此分支执行地图传送。
    return FailTravelRefresh(menu, ticket,
        "Exploration: travel UI validation failed; requesting native map close and disabling travel assistance.");
}

static bool InstallTravelRefresh(uintptr_t base) noexcept {
    const unsigned char script[] = {0x40,0x53,0x55,0x56,0x57,0x41,0x56,0x41,0x57,0x48,0x83,0xEC,0x58,0x48,0x8B,0x05};
    const unsigned char display[] = {0x48,0x8B,0xC4,0x48,0x89,0x58,0x10,0x48,0x89,0x70,0x18,0x48,0x89,0x78,0x20,0x55};
    const unsigned char listBuild[] = {0x48,0x8B,0xC4,0x48,0x89,0x58,0x10,0x48,0x89,0x68,0x18,0x48,0x89,0x70,0x20,0x57};
    const unsigned char listClear[] = {0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x6C,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x57};
    const unsigned char listSelect[] = {0x48,0x89,0x5C,0x24,0x18,0x55,0x48,0x83,0xEC,0x30,0x8B,0x41,0x40,0x41,0x0F,0xB6};
    const unsigned char areaCallback[] = {0x40,0x53,0x48,0x83,0xEC,0x20,0x48,0x8B,0x81,0xC0,0x01,0,0,0x33,0xD2,0x48};
    const unsigned char spotCallback[] = {0x40,0x53,0x48,0x83,0xEC,0x20,0x48,0x8B,0x81,0xC8,0x01,0,0,0x48,0x8B,0xD9};
    const unsigned char browse[] = {0x40,0x55,0x53,0x56,0x57,0x41,0x56,0x41,0x57,0x48,0x8D,0x6C,0x24,0xD1,0x48,0x81,0xEC,0xA8,0,0,0};
    const unsigned char spots[] = {0x48,0x8B,0xC4,0x48,0x89,0x58,0x18,0x48,0x89,0x68,0x20,0x56,0x57,0x41,0x54,0x41};
    const unsigned char areas[] = {0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x6C,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x48};
    const unsigned char close[] = {0x40,0x53,0x48,0x83,0xEC,0x30,0x48,0x63,0x81,0xEC,0,0,0,0x48,0x8B,0xD9};
    if (!Matches(base+0x4CC460, script) || !Matches(base+0x3D4A70, display) ||
        !Matches(base+0x3DF330, listBuild) || !Matches(base+0x3DE790, listBuild) ||
        !Matches(base+0x525DD0, listClear) || !Matches(base+0x5261B0, listSelect) ||
        !Matches(base+0x3EB210, areaCallback) || !Matches(base+0x3EB2D0, spotCallback) ||
        !Matches(base+0x3E4650, browse) || !Matches(base+0x3E54B0, spots) || !Matches(base+0x3E5BB0, areas) ||
        !Matches(base+0x3E86B0, close)) return false;
    g_travelApi = {reinterpret_cast<NativeTravelApi::Execute>(base+0x4CC460),
        reinterpret_cast<NativeTravelApi::Unary>(base+0x3D4A70), reinterpret_cast<NativeTravelApi::Unary>(base+0x525DD0),
        reinterpret_cast<NativeTravelApi::Unary>(base+0x3DF330), reinterpret_cast<NativeTravelApi::Unary>(base+0x3DE790),
        reinterpret_cast<NativeTravelApi::Select>(base+0x5261B0), reinterpret_cast<NativeTravelApi::Unary>(base+0x3EB2D0),
        reinterpret_cast<NativeTravelApi::Unary>(base+0x3EB210)};
    // Build 入口也纳入同一组：显式刷新与普通重新开图均经过统一的最终状态评估。
    void* targets[] = {reinterpret_cast<void*>(base+0x3E4650), reinterpret_cast<void*>(base+0x3E54B0),
        reinterpret_cast<void*>(base+0x3E5BB0), reinterpret_cast<void*>(base+0x3D4A70)};
    void* shims[] = {reinterpret_cast<void*>(&Sky2MapBrowseShim), reinterpret_cast<void*>(&Sky2SpotListShim),
        reinterpret_cast<void*>(&Sky2AreaListShim), reinterpret_cast<void*>(&Sky2BuildTravelShim)};
    void** originals[] = {&Sky2NextMapBrowse, &Sky2NextSpotList, &Sky2NextAreaList, &Sky2NextBuildTravel};
    constexpr unsigned hookCount = sizeof(targets) / sizeof(targets[0]);
    unsigned created = 0, enabled = 0;
    for (; created < hookCount; ++created)
        if (MH_CreateHook(targets[created], shims[created], originals[created]) != MH_OK) break;
    if (created == hookCount) {
        for (; enabled < hookCount; ++enabled) if (MH_EnableHook(targets[enabled]) != MH_OK) break;
        if (enabled == hookCount) return true;
    }
    // 安装任一步失败时回滚本次创建的挂钩，不留下只有部分状态能刷新的半安装版本。
    while (enabled) MH_DisableHook(targets[--enabled]);
    while (created) MH_RemoveHook(targets[--created]);
    return false;
}
