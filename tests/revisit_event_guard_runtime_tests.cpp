// 用自行分配的内存调用真实生产保护函数；不连接游戏、不安装挂钩、不读取真实存档。
// 直接包含实现只用于此独立测试目标，链接时不得再次加入该 cpp。
#include "../native/revisit_event_guard.cpp"
#include <cstdio>
#include <vector>

namespace tracker { void Log(const char*) noexcept {} }
static unsigned failures = 0;
static void Check(bool ok, const char* name) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", name); ++failures; }
}
template<class T> static void Put(std::vector<uint8_t>& bytes, size_t at, T value) {
    std::memcpy(bytes.data() + at, &value, sizeof(value));
}
static uintptr_t Address(std::vector<uint8_t>& bytes) { return reinterpret_cast<uintptr_t>(bytes.data()); }

struct Fixture {
    std::vector<uint8_t> image = std::vector<uint8_t>(0xC70000);
    std::vector<uint8_t> tables = std::vector<uint8_t>(0x120);
    std::vector<uint8_t> holder = std::vector<uint8_t>(0x20);
    std::vector<uint8_t> file = std::vector<uint8_t>(0x40);
    std::vector<uint8_t> headers = std::vector<uint8_t>(80);
    std::vector<uint8_t> rows = std::vector<uint8_t>(571 * 120);
    std::vector<uint8_t> field = std::vector<uint8_t>(0x1C00);
    std::vector<uint8_t> sceneData = std::vector<uint8_t>(0xA0);
    std::vector<uint8_t> root = std::vector<uint8_t>(0xE80);
    std::vector<uint8_t> placeHolder = std::vector<uint8_t>(0x20);
    std::vector<uint8_t> placeFile = std::vector<uint8_t>(0x40);
    std::vector<uint8_t> placeHeaders = std::vector<uint8_t>(0x50);
    // 主地点同 ID 的旧地区行放在前面，终章地区行在后面，验证查表不能只取首行。
    std::vector<uint8_t> places = std::vector<uint8_t>(4 * 0xA8);
    std::vector<uint8_t> savedata = std::vector<uint8_t>(0x11200);
    // 参数是测试进程的临时区，邻接字节作为越界写哨兵；绝不指向游戏对象。
    std::vector<uint8_t> callerStack = std::vector<uint8_t>(0x80, 0xA5);
    explicit Fixture(const tracker::revisit_eventguard::SpecialChest& chest) {
        using namespace tracker::revisit_eventguard;
        tracker::g_revisitGuardBase = Address(image);
        tracker::g_revisitGuardInstalled.store(true);
        tracker::SetRevisitEventGuardActive(true);
        tracker::SetExperimentalRevisitTripActive(false);
        Put(image, 0xC5D778, Address(tables));
        Put(tables, 0x108, Address(holder)); Put(holder, 8, Address(file));
        Put(file, 0x10, Address(rows)); Put(file, 0x20, Address(headers));
        Put(headers, 0x48, uint32_t{120}); Put(headers, 0x4C, uint32_t{571});
        Put(rows, chest.row * 120, reinterpret_cast<uintptr_t>(chest.scene.data()));
        Put(rows, chest.row * 120 + 8, reinterpret_cast<uintptr_t>(chest.name.data()));
        Put(rows, chest.row * 120 + 0x10, chest.scriptParameter);
        Put(image, 0xC60E08, Address(field)); Put(image, 0xC60E58, Address(savedata));
        std::memcpy(field.data() + 0x170, chest.scene.data(), chest.scene.size());
        Put(field, 0x190, static_cast<uint32_t>(chest.scene.size()));
        Put(field, 0x648, Address(sceneData));
        Put(sceneData, 8, reinterpret_cast<uintptr_t>(chest.scene.data()));
        const auto region = SceneRegion(chest.scene);
        Put(sceneData, 0x98, region);
        Put(sceneData, 0, uint32_t{1850011});
        Put(field, 0x108, Address(root)); Put(root, 0xE77, uint8_t{1});
        Put(root, 0x808, uint32_t{1850001});
        Put(root, 0x810, reinterpret_cast<uintptr_t>(chest.scene.data()));
        Put(root, 0x8A0, region);
        Put(tables, 0x60, Address(placeHolder)); Put(placeHolder, 8, Address(placeFile));
        Put(placeFile, 0x10, Address(places)); Put(placeFile, 0x20, Address(placeHeaders));
        Put(placeHeaders, 0x48, uint32_t{0xA8}); Put(placeHeaders, 0x4C, uint32_t{4});
        for (unsigned i = 0; i < 4; ++i) {
            Put(places, i * 0xA8, i < 2 ? uint32_t{1850001} : uint32_t{1850011});
            Put(places, i * 0xA8 + 8, reinterpret_cast<uintptr_t>(chest.scene.data()));
            Put(places, i * 0xA8 + 0x98, i == 1 && region == 8 ? uint32_t{7} : i == 3 ? uint32_t{0} : region);
        }
        Put(savedata, 0x11100 + 12 * 4, region == 8 ? kNinthChapter : kEighthChapter);
        for (auto flag : kRequiredStoryFlags) savedata[0x100 + flag / 8] |= static_cast<uint8_t>(1u << (flag % 8));
        if (region == 8)
            for (auto flag : kRequiredGloriousStoryFlags)
                savedata[0x100 + flag / 8] |= static_cast<uint8_t>(1u << (flag % 8));
        Put(callerStack, 0x4C, kIntegerTag | chest.scriptParameter);
    }
    void Run(uint32_t row, uintptr_t returnOffset = tracker::revisit_eventguard::kTBoxStartReturnRva,
             uintptr_t parameterOffset = 0x4C) {
        tracker::Sky2GuardTBoxScriptStart(Address(image) + returnOffset, Address(rows) + row * 120,
            reinterpret_cast<uintptr_t>("TBoxProcess"), Address(callerStack) + parameterOffset,
            1, Address(callerStack));
    }
    uint32_t Argument() const {
        uint32_t value = 0; std::memcpy(&value, callerStack.data() + 0x4C, sizeof(value)); return value;
    }
};

int main() {
    using namespace tracker::revisit_eventguard;
    // 复现荣耀号实测的 root1850001/region7 + floor1850011/region0；这两个地点
    // 必须都能在真实形状的 t_place 中完整匹配，不能仅凭地区 0 或场景前缀放行。
    for (auto rawRegion : {0u, 7u, 8u}) {
        const auto& gloriousChest = kSpecialChests[6];
        Fixture fixture(gloriousChest);
        Put(fixture.sceneData, 0x98, rawRegion);
        Put(fixture.root, 0x8A0, uint32_t{7});
        Put(fixture.places, 3 * 0xA8 + 0x98, rawRegion);
        const auto oldSave = fixture.savedata, oldRows = fixture.rows, oldPlaces = fixture.places;
        fixture.Run(gloriousChest.row);
        Check(fixture.Argument() == kIntegerTag, "glorious final root and terrain region normalized");
        Check(fixture.savedata == oldSave && fixture.rows == oldRows && fixture.places == oldPlaces,
              "normalization leaves save chest and place tables unchanged");
    }
    for (unsigned scenario = 0; scenario < 9; ++scenario) {
        const auto& gloriousChest = kSpecialChests[6];
        Fixture fixture(gloriousChest);
        Put(fixture.sceneData, 0x98, uint32_t{0}); Put(fixture.root, 0x8A0, uint32_t{7});
        switch (scenario) {
        case 0: Put(fixture.root, 0xE77, uint8_t{0}); break;
        case 1: Put(fixture.root, 0x810, reinterpret_cast<uintptr_t>("mp8500_02")); break;
        case 2: Put(fixture.root, 0x898, uint8_t{1}); break;
        case 3: Put(fixture.root, 0x8A0, uint32_t{0}); break;
        case 4: Put(fixture.root, 0x808, uint32_t{1850999}); break;
        case 5: Put(fixture.places, 1 * 0xA8 + 0x98, uint32_t{8}); break;
        case 6: Put(fixture.places, 3 * 0xA8 + 0x98, uint32_t{8}); break;
        case 7: Put(fixture.places, 3 * 0xA8 + 8, reinterpret_cast<uintptr_t>("mp8500_02")); break;
        case 8: Put(fixture.places, 3 * 0xA8 + 0x90, uint8_t{1}); break;
        }
        const auto before = fixture.callerStack, oldSave = fixture.savedata, oldPlaces = fixture.places;
        fixture.Run(gloriousChest.row);
        Check(fixture.callerStack == before, "unverified glorious root or terrain rejected");
        Check(fixture.savedata == oldSave && fixture.places == oldPlaces, "rejected normalization never writes persistent state");
    }
    for (const auto& chest : kSpecialChests) {
        Fixture fixture(chest);
        const auto oldSave = fixture.savedata, oldRows = fixture.rows;
        auto wantedStack = fixture.callerStack;
        Put(wantedStack, 0x4C, kIntegerTag);
        fixture.Run(chest.row);
        Check(fixture.callerStack == wantedStack, "exactly one temporary parameter normalized");
        Check(fixture.savedata == oldSave, "save and story flags unchanged");
        Check(fixture.rows == oldRows, "table and reward bytes unchanged");
    }
    const auto& chest = kSpecialChests[0];
    // 每个拒绝场景从新夹具开始，确认不是上一个用例留下的状态导致偶然通过。
    for (unsigned scenario = 0; scenario < 12; ++scenario) {
        Fixture fixture(chest);
        auto caller = kTBoxStartReturnRva;
        uintptr_t parameterOffset = 0x4C;
        switch (scenario) {
        case 0: tracker::SetRevisitEventGuardActive(false); break;
        case 1: tracker::g_revisitGuardInstalled.store(false); break;
        case 2: caller += 1; break;
        case 3: parameterOffset = 0x50; break;
        case 4: Put(fixture.savedata, 0x11100 + 12 * 4, kIntegerTag | 7u); break;
        case 5: fixture.savedata[0x100 + 16045 / 8] &= static_cast<uint8_t>(~(1u << (16045 % 8))); break;
        case 6: Put(fixture.field, 0x1BC8, uint32_t{1}); break;
        case 7: Put(fixture.sceneData, 8, reinterpret_cast<uintptr_t>("mp6011")); break;
        case 8: Put(fixture.headers, 0x4C, uint32_t{570}); break;
        case 9: Put(fixture.rows, chest.row * 120 + 0x10, uint32_t{1440}); break;
        case 10: Put(fixture.rows, chest.row * 120 + 8, reinterpret_cast<uintptr_t>("unknown")); break;
        case 11: Put(fixture.callerStack, 0x4C, chest.scriptParameter); break;
        }
        const auto before = fixture.callerStack, oldSave = fixture.savedata, oldRows = fixture.rows;
        fixture.Run(chest.row, caller, parameterOffset);
        Check(fixture.callerStack == before, "invalid context leaves arguments untouched");
        Check(fixture.savedata == oldSave && fixture.rows == oldRows, "invalid context leaves persistent bytes untouched");
    }
    // 荣耀号参数 1440 在生产读取链中必须同时满足终章与已结束的主线条件；仅通过
    // 纯规则不足以证明真实桥已接入，因此在合成场景/存档/表内额外逐项验证。
    for (unsigned scenario = 0; scenario < 3; ++scenario) {
        const auto& gloriousChest = kSpecialChests[6];
        Fixture fixture(gloriousChest);
        if (scenario == 0) Put(fixture.savedata, 0x11100 + 12 * 4, kEighthChapter);
        if (scenario == 1) fixture.savedata[0x100 + 25040 / 8] &= static_cast<uint8_t>(~(1u << (25040 % 8)));
        if (scenario == 2) Put(fixture.sceneData, 0x98, uint32_t{6});
        const auto before = fixture.callerStack, oldSave = fixture.savedata, oldRows = fixture.rows;
        fixture.Run(gloriousChest.row);
        Check(fixture.callerStack == before, "glorious unfinished or mismatched context unchanged");
        Check(fixture.savedata == oldSave && fixture.rows == oldRows, "glorious save and table remain read only");
    }
    {
        Fixture fixture(chest);
        Put(fixture.savedata, 0x11100 + 12 * 4, kNinthChapter);
        fixture.Run(chest.row);
        Check(fixture.Argument() == kIntegerTag, "chapter nine prologue reaches production guard");
    }
    // 与标准版共用本测试源：实验构建仍不影响普通早期流程；只有原生消费者明确
    // 开始的行程能够扩大七个精确宝箱的保护，返程前关闭后立即恢复剧情原参数。
    for (const auto& special : kSpecialChests) {
        Fixture fixture(special);
        std::memset(fixture.savedata.data() + 0x100, 0, 4096);
        Put(fixture.savedata, 0x11100 + 12 * 4, kIntegerTag | 1u);
        const auto before = fixture.callerStack, oldSave = fixture.savedata, oldRows = fixture.rows;
        fixture.Run(special.row);
        Check(fixture.callerStack == before, "experimental build alone never rewrites early story chest");
        tracker::SetExperimentalRevisitTripActive(true, 1);
        Check(tracker::ExperimentalRevisitTripActive() == tracker::revisit_policy::kUnrestricted,
              "standard build cannot arm experimental state");
        fixture.Run(special.row);
        Check(fixture.Argument() == (tracker::revisit_policy::kUnrestricted ? kIntegerTag :
                                      kIntegerTag | special.scriptParameter),
              "active experimental trip reaches real precise chest guard");
        Check(fixture.savedata == oldSave && fixture.rows == oldRows,
              "experimental chest protection never modifies flags or table");
        fixture.callerStack = before;
        tracker::SetExperimentalRevisitTripActive(false);
        fixture.Run(special.row);
        Check(fixture.callerStack == before, "return disarms protection before original story resumes");
    }
    for (const uint32_t invalidChapter : {1u, kIntegerTag | 10u, 0xC0000001u}) {
        Fixture fixture(chest);
        tracker::SetExperimentalRevisitTripActive(true, 1);
        Put(fixture.savedata, 0x11100 + 12 * 4, invalidChapter);
        const auto before = fixture.callerStack;
        fixture.Run(chest.row);
        Check(fixture.callerStack == before, "active trip rejects invalid chapter type or number");
    }
    // 即使保护处于实验行程内，来源地址、地点身份和真实表行仍然不可省略。
    for (unsigned scenario = 0; scenario < 5; ++scenario) {
        Fixture fixture(chest);
        tracker::SetExperimentalRevisitTripActive(true, 1);
        Put(fixture.savedata, 0x11100 + 12 * 4, kIntegerTag | 1u);
        auto caller = kTBoxStartReturnRva;
        switch (scenario) {
        case 0: ++caller; break;
        case 1: Put(fixture.root, 0x808, uint32_t{9999999}); break;
        case 2: Put(fixture.rows, chest.row * 120 + 8, reinterpret_cast<uintptr_t>("OtherChest")); break;
        case 3: Put(fixture.rows, chest.row * 120 + 0x10, uint32_t{1440}); break;
        case 4: Put(fixture.field, 0x1BC8, uint32_t{1}); break;
        }
        const auto before = fixture.callerStack;
        fixture.Run(chest.row, caller);
        Check(fixture.callerStack == before, "experimental trip preserves technical guard boundaries");
    }
    {
        // 不调用协调层或任何UI刷新，直接让生产开箱回调读到另一合法早期章节。
        // 旧 token 必须就地消失；之后读回原章节也不得把旧行程自动复活。
        Fixture fixture(chest);
        std::memset(fixture.savedata.data() + 0x100, 0, 4096);
        tracker::SetExperimentalRevisitTripActive(true, 1);
        Put(fixture.savedata, 0x11100 + 12 * 4, kIntegerTag | 4u);
        const auto before = fixture.callerStack;
        fixture.Run(chest.row);
        Check(fixture.callerStack == before && !tracker::ExperimentalRevisitTripActive(),
              "hidden UI chapter switch invalidates trip inside real guard callback");
        Put(fixture.savedata, 0x11100 + 12 * 4, kIntegerTag | 1u);
        fixture.Run(chest.row);
        Check(fixture.callerStack == before && !tracker::ExperimentalRevisitTripActive(),
              "returning to original chapter does not revive old experimental token");
        tracker::SetExperimentalRevisitTripActive(true);
        Check(!tracker::ExperimentalRevisitTripActive(), "default chapter can never arm trip");
        for (uint32_t invalid : {10u, kIntegerTag | 1u}) {
            tracker::SetExperimentalRevisitTripActive(true, invalid);
            Check(!tracker::ExperimentalRevisitTripActive(), "invalid chapter can never arm trip");
        }
        tracker::SetExperimentalRevisitTripActive(true, 1);
        Check(!tracker::ExperimentalRevisitTripActive(10), "invalid observed chapter grants no protection");
        Check(tracker::ExperimentalRevisitTripActive(1) == tracker::revisit_policy::kUnrestricted,
              "incomplete chapter sample alone does not replace valid trip identity");
        tracker::SetExperimentalRevisitTripActive(false);
    }
    {
        Fixture fixture(chest);
        // 合法调用点的无效行指针也不能使读取故障逃逸到调用方。
        const auto before = fixture.callerStack;
        tracker::Sky2GuardTBoxScriptStart(Address(fixture.image) + kTBoxStartReturnRva, 1,
            1, Address(fixture.callerStack) + 0x4C, 1, Address(fixture.callerStack));
        Check(fixture.callerStack == before, "invalid row address rejected");
        // 换成不可读页验证生产 SEH，所有页面都属于本测试进程。
        void* noAccess = VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_NOACCESS);
        Check(noAccess != nullptr, "allocate guarded page");
        if (noAccess) {
            Put(fixture.image, 0xC60E08, reinterpret_cast<uintptr_t>(noAccess));
            fixture.Run(chest.row);
            Check(fixture.callerStack == before, "unreadable scene rejected");
            VirtualFree(noAccess, 0, MEM_RELEASE);
        }
    }
    return failures ? 1 : 0;
}
