// 回访原生适配运行时回归。仅分配测试进程自己的合成对象，绝不连接或启动游戏。
// 直接包含生产实现，确保测试覆盖真实字段读取、提交及撤回路径，而非另一份模拟实现。
bool testForestGuardReady=true;
bool testExperimentalTripActive=false;
#include "../native/revisit_native.cpp"
#include <cstdio>
#include <limits>
uint32_t testExperimentalTripChapter=UINT32_MAX;

namespace tracker {
void Log(const char*) noexcept {}
// 保护安装的真实机器码边界由独立guard测试负责；此开关验证原生适配是否每次
// 复核就绪状态，避免测试只覆盖始终成功的入场路径。
bool ForestRevisitGuardReady() noexcept { return testForestGuardReady; }
void SetExperimentalRevisitTripActive(bool value,uint32_t chapter) noexcept {
    // 模拟共享保护接口的章节令牌；真实原子失配清理另由guard运行时回归覆盖。
    testExperimentalTripActive=revisit_policy::kUnrestricted && value && chapter<=9;
    testExperimentalTripChapter=testExperimentalTripActive?chapter:UINT32_MAX;
}
bool ExperimentalRevisitTripActive(uint32_t chapter) noexcept {
    if(chapter!=UINT32_MAX && (chapter>9 || chapter!=testExperimentalTripChapter))
        SetExperimentalRevisitTripActive(false);
    return testExperimentalTripActive;
}
}
namespace {
unsigned checks=0, failures=0;
void Check(bool value,const char* label) {
    ++checks; if (!value) { ++failures; std::printf("FAIL %s\n",label); }
}
template<class T,size_t N> void Put(std::array<unsigned char,N>& memory,size_t offset,const T& value) {
    std::memcpy(memory.data()+offset,&value,sizeof(value));
}
template<size_t N> uintptr_t Address(std::array<unsigned char,N>& value) {
    return reinterpret_cast<uintptr_t>(value.data());
}
bool Allow(uint32_t,const tracker::RevisitNativeContext&,uint64_t) noexcept { return true; }
bool CancelAuthorize(uint32_t,const tracker::RevisitNativeContext&,uint64_t) noexcept {
    tracker::CancelRevisitNativeTravel(); return true;
}
bool DisableForestGuardAuthorize(uint32_t,const tracker::RevisitNativeContext&,uint64_t) noexcept {
    testForestGuardReady=false;return true;
}
void CancelClose(uintptr_t,int32_t,int32_t) noexcept { tracker::CancelRevisitNativeTravel(); }
unsigned loadCalls=0;
tracker::RevisitReturnPoint loadedPoint{};
uint32_t loadedFlags=0;
bool acceptLoad=true;
bool tripActiveAtLoad=false;
void FakeNativeLoad(uintptr_t field,const char* scene,const char* entry,const float* position,float yaw,uint32_t flags) {
    ++loadCalls;strcpy_s(loadedPoint.scene,scene);
    std::memcpy(loadedPoint.xyz,position,sizeof(loadedPoint.xyz));loadedPoint.yawRadians=yaw;loadedFlags=flags;
    tripActiveAtLoad=tracker::ExperimentalRevisitTripActive();
    Check(entry==nullptr,"custom scene loader passes no entry tag");
    if (acceptLoad) { const uint32_t busy=1;std::memcpy(reinterpret_cast<void*>(field+0x1BC8),&busy,4); }
}
struct Fixture {
    void* image=VirtualAlloc(nullptr,0xD00000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    std::array<unsigned char,0x1C00> field{};
    std::array<unsigned char,0xA0> scene{};
    std::array<unsigned char,0xF00> sceneRoot{};
    std::array<unsigned char,0x70> player{};
    std::array<unsigned char,0x130> actor{};
    std::array<unsigned char,0x12000> save{};
    std::array<unsigned char,0x320> minimap{};
    std::array<unsigned char,0x400> menu{},newMenu{};
    std::array<unsigned char,0x100> tables{};
    std::array<unsigned char,0x20> holder{};
    std::array<unsigned char,0x40> file{};
    std::array<unsigned char,0x50> headers{};
    std::array<unsigned char,0x98> row{};
    std::array<unsigned char,0x20> placeHolder{};
    std::array<unsigned char,0x40> placeFile{};
    std::array<unsigned char,0x50> placeHeaders{};
    std::array<unsigned char,0xA8*4> placeRows{};
    std::array<tracker::RuleSpot,3> ruleSpots{};
    std::array<tracker::RuleArea,3> ruleAreas{};
    char currentScene[32]="mp1000",targetScene[32]="mp6011";
    char returnScene[32]="mp1000";
    ~Fixture() { if(image) VirtualFree(image,0,MEM_RELEASE); }
    template<class T> void Global(size_t offset,const T& value) {
        std::memcpy(reinterpret_cast<unsigned char*>(image)+offset,&value,sizeof(value));
    }
    void Reset() {
        strcpy_s(currentScene,"mp1000");strcpy_s(targetScene,"mp6011");
        field.fill(0);scene.fill(0);save.fill(0);minimap.fill(0);menu.fill(0);newMenu.fill(0);
        tables.fill(0);holder.fill(0);file.fill(0);headers.fill(0);row.fill(0);
        sceneRoot.fill(0);player.fill(0);actor.fill(0);placeRows.fill(0);
        placeHolder.fill(0);placeFile.fill(0);placeHeaders.fill(0);
        tracker::base=reinterpret_cast<uintptr_t>(image);
        tracker::available.store(true);
        tracker::status={};tracker::published={};tracker::expectedContext={};
        tracker::publishedPoint={};tracker::requestedReturn={};tracker::arrivalPoint={};
        tracker::publishedPointValid=tracker::arrivalPointValid=tracker::sawLoadTransition=false;
        tracker::arrivalFrames=0;tracker::nativeLoad=&FakeNativeLoad;
        tracker::ruleProof={};
        ruleSpots={};ruleAreas={};
        loadCalls=0;loadedPoint={};loadedFlags=0;acceptLoad=true;tripActiveAtLoad=false;
        testForestGuardReady=true;
        tracker::SetExperimentalRevisitTripActive(false);
        tracker::dispatchArmed=false; tracker::handoffMenuIdentity=tracker::handoffMinimapIdentity=0;
        tracker::authorize.store(&Allow);
        Global(0xC60E08,Address(field));Global(0xC60E58,Address(save));Global(0xC5D778,Address(tables));
        Put(field,0x648,Address(scene));Put(field,0x730,Address(minimap));
        Put(field,0x108,Address(sceneRoot));Put(field,0x660,Address(player));
        Put(player,0,tracker::base+0xB05CB8);Put(player,0x60,Address(actor));
        Put(actor,0x114,float{1});Put(actor,0xE8,float{23});Put(actor,0xEC,float{-3});Put(actor,0xF0,float{54});
        Put(sceneRoot,0xE77,uint8_t{1});Put(sceneRoot,0x808,uint32_t{1101000});Put(scene,0,uint32_t{1101000});
        Put(sceneRoot,0x808+8,reinterpret_cast<uintptr_t>(currentScene));Put(sceneRoot,0x808+0x98,uint32_t{2});
        std::memcpy(field.data()+0x170,currentScene,sizeof(currentScene));
        Put(scene,8,reinterpret_cast<uintptr_t>(currentScene));Put(scene,0x98,uint32_t{2});
        Put(save,0x11100+12*4,uint32_t{0x40000008});
        for(uint32_t flag:tracker::completedFlags) save[0x100+flag/8]|=1u<<(flag%8);
        for(uint32_t flag:tracker::revisit_eventguard::kRequiredResearchStoryFlags) save[0x100+flag/8]|=1u<<(flag%8);
        for(uint32_t flag:tracker::revisit_eventguard::kRequiredGloriousStoryFlags) save[0x100+flag/8]|=1u<<(flag%8);
        for(uint32_t flag:{23070u,23014u,23015u,23043u,23045u,23047u,17052u,17053u,17054u,17055u,17056u,17151u,25022u,25023u,25051u,25084u,20064u,20067u})
            save[0x100+flag/8]|=1u<<(flag%8);
        Put(minimap,0x28,Address(menu));Put(menu,8,Address(minimap));Put(menu,0x18,Address(menu));
        Put(menu,0x279,uint8_t{1});Put(menu,0xB8,int32_t{3});Put(menu,0xBC,int32_t{3});Put(menu,0xC0,int32_t{1});
        Put(tables,0xF0,Address(holder));Put(holder,8,Address(file));Put(file,0x10,Address(row));
        Put(file,0x20,Address(headers));Put(headers,0x48,uint32_t{0x98});Put(headers,0x4C,uint32_t{1});
        Put(row,0,uint32_t{99});Put(row,4,uint32_t{6});Put(row,0x38,reinterpret_cast<uintptr_t>(targetScene));
        Put(row,0x40,uint32_t{1601100});
        Put(tables,0x60,Address(placeHolder));Put(placeHolder,8,Address(placeFile));
        Put(placeFile,0x10,Address(placeRows));Put(placeFile,0x20,Address(placeHeaders));
        Put(placeHeaders,0x48,uint32_t{0xA8});Put(placeHeaders,0x4C,uint32_t{4});
        Put(placeRows,0,uint32_t{1101000});Put(placeRows,8,reinterpret_cast<uintptr_t>(currentScene));
        Put(placeRows,0x98,uint32_t{2});
        Destination(99,6,1601100,"mp6011");
        tracker::Sky2BeforeRevisitUpdate(Address(minimap));
    }
    bool Queue(uint64_t token=1) { return tracker::QueueRevisitNativeTravel(99,token,tracker::ReadRevisitNativeContext()); }
    bool Close() { return tracker::BeforeRevisitNativeBrowse(Address(menu)); }
    void Transfer() {
        std::memcpy(minimap.data()+0x310,menu.data()+0x3C0,8);
        Put(minimap,0x28,uintptr_t{0});
    }
    bool Dispatch() { return tracker::Sky2BeforeRevisitJump(Address(field),99,tracker::base+0x29968F); }
    uint64_t MenuResult() { uint64_t result=0;std::memcpy(&result,menu.data()+0x3C0,8);return result; }
    uint32_t ResultKind() { uint32_t result=0;std::memcpy(&result,minimap.data()+0x314,4);return result; }
    void Destination(uint32_t id,uint32_t region,uint32_t place,const char* name,uint8_t variant=0) {
        std::memset(targetScene,0,sizeof(targetScene));strcpy_s(targetScene,name);
        Put(row,0,id);Put(row,4,region);Put(row,0x40,place);
        if (const auto* catalog=tracker::CatalogDestination(id,variant)) {
            Put(row,8,catalog->area);Put(row,0x58,catalog->flags);Put(row,0x60,catalog->variant);
        }
        Put(placeRows,0x1F8,place);Put(placeRows,0x200,reinterpret_cast<uintptr_t>(targetScene));
        Put(placeRows,0x1F8+0x98,region);
    }
    void Source(uint32_t region,const char* name) {
        std::memset(currentScene,0,sizeof(currentScene));strcpy_s(currentScene,name);
        std::memcpy(field.data()+0x170,currentScene,sizeof(currentScene));Put(scene,0x98,region);
        uint32_t id=1101000;
        // 普通场景使用生成目录里的真实根地点，便于区分同一大地图中的城市、
        // 关所等不同子地形；不能让测试用固定柏斯ID掩盖实际返程匹配问题。
        for(const auto& d:tracker::kTravelCatalog)
            if(d.scene && std::strcmp(d.scene,name)==0) { id=d.place;break; }
        for(const auto& d:tracker::destinations) if(std::strcmp(d.scene,name)==0) { id=d.place;break; }
        if(std::strcmp(name,"mp5600_03")==0) id=1560003;
        if(std::strcmp(name,tracker::forest::kScene)==0) id=tracker::forest::kPlace;
        Put(scene,0,id);Put(sceneRoot,0x808,id);Put(sceneRoot,0x808+0x98,region);Put(placeRows,0,id);Put(placeRows,0x98,region);
        tracker::Sky2BeforeRevisitUpdate(Address(minimap));
    }
    void SourceTerrain(uint32_t rootPlace,uint32_t currentPlace,uint8_t rootVariant=0,uint8_t currentVariant=0) {
        // 一张野外大地图的默认地点与当前道路地形通常不同。两者均在合成
        // t_place表中提供精确身份，模拟Capture实际执行的双重地点核对。
        uint32_t region=0;std::memcpy(&region,scene.data()+0x98,sizeof(region));
        Put(sceneRoot,0x808,rootPlace);Put(placeRows,0,rootPlace);Put(scene,0,currentPlace);
        Put(sceneRoot,0x808+0x90,rootVariant);Put(placeRows,0x90,rootVariant);Put(scene,0x90,currentVariant);
        Put(placeRows,0x150,currentPlace);Put(placeRows,0x150+8,reinterpret_cast<uintptr_t>(currentScene));
        Put(placeRows,0x150+0x90,currentVariant);Put(placeRows,0x150+0x98,region);
        tracker::Sky2BeforeRevisitUpdate(Address(minimap));
    }
    void ReturnTable(const tracker::RevisitReturnPoint& point) {
        strcpy_s(returnScene,point.scene);
        Put(placeRows,0xA8,point.place);Put(placeRows,0xA8+8,reinterpret_cast<uintptr_t>(returnScene));
        Put(placeRows,0xA8+0x90,static_cast<uint8_t>(point.variant));Put(placeRows,0xA8+0x98,point.region);
    }
    void Arrive(const tracker::RevisitReturnPoint& point) {
        Put(field,0x1BC8,uint32_t{0});
        std::memcpy(actor.data()+0xE8,point.xyz,sizeof(point.xyz));
        Put(actor,0x108,float{0});Put(actor,0x10C,std::sin(point.yawRadians/2));
        Put(actor,0x110,float{0});Put(actor,0x114,std::cos(point.yawRadians/2));
        Put(scene,0x90,static_cast<uint8_t>(point.variant));
        Source(std::strcmp(point.scene,tracker::forest::kScene)==0?0u:point.region,point.scene);
    }
    void Rules(uint32_t id,bool observe=true) {
        const auto* catalog=tracker::CatalogDestination(id,0);
        if (!catalog) return;
        ruleSpots[0]={id,catalog->area,catalog->region,0,0,1,0};
        ruleAreas[0]={catalog->area,catalog->region,0,0,{0,0}};
        Put(minimap,0xE0,reinterpret_cast<uintptr_t>(ruleSpots.data()));Put(minimap,0xE8,uint64_t{1});
        Put(minimap,0xC8,reinterpret_cast<uintptr_t>(ruleAreas.data()));Put(minimap,0xD0,uint64_t{catalog->area?1u:0u});
        if (observe) tracker::ObserveRevisitNativeRules(Address(minimap));
        tracker::Sky2BeforeRevisitUpdate(Address(minimap));
    }
    void ForestTable() {
        // 森林不存在t_mapjump记录。只提供它真实的t_place身份，证明自定义
        // 目的地不依赖伪造的原生Spot ID，也不会覆盖当前场景与出发点数据。
        Put(placeRows,0x1F8,tracker::forest::kPlace);
        Put(placeRows,0x200,reinterpret_cast<uintptr_t>(tracker::forest::kScene));
        Put(placeRows,0x1F8+0x90,uint8_t{0});Put(placeRows,0x1F8+0x98,uint32_t{0});
    }
};

int RunUnrestricted(Fixture& f) {
    // 与普通553项回归独立运行：实验许可仅在单独编译目标里启用，不能改变
    // 默认构建对剧情的预期。这里仍只使用本测试进程内的合成对象。
#if SKY2_HAS_FULL_TRAVEL_CATALOG
    for(uint32_t chapter=0;chapter<=9;++chapter) {
        f.Reset();std::fill_n(f.save.data()+0x100,4096,uint8_t{0});
        Put(f.save,0x11100+12*4,uint32_t{0x40000000u|chapter});
        tracker::Sky2BeforeRevisitUpdate(Address(f.minimap));
        const auto context=tracker::ReadRevisitNativeContext();
        Check(context.valid&&!context.prologueCompleted,"experimental source supports every chapter without story completion");
        size_t count=0;
        for(size_t i=0;i<tracker::RevisitDestinationCount();++i) {
            const auto id=tracker::RevisitDestinationAt(i).id;
            if(id>=1001) continue;
            ++count;
            Check(tracker::RevisitNativeTargetAvailable(id,context),"all public native IDs ignore absent runtime registry and story flags in experiment");
        }
        Check(count==155,"experimental native directory remains exactly 155 public destinations");
        for(uint32_t id:{0u,156u,157u,158u,159u,160u,161u,162u,163u,164u,167u,168u,1001u,0xFFFFFFFCu})
            Check(!tracker::RevisitNativeTargetAvailable(id,context),"experiment does not expose hidden internal or unknown IDs");
    }

    // 即使已观察的原生规则明确拒绝，实验只忽略剧情层；同一请求的菜单
    // 生命周期、字段身份、变体、坐标范围和最终消费者仍必须完整通过。
    for(unsigned denied=0;denied<5;++denied) {
        f.Reset();Put(f.save,0x11100+12*4,uint32_t{0x40000001});
        f.save[0x100+17019/8]|=1u<<(17019%8);
        const auto* row=tracker::CatalogDestination(37,0);f.Destination(37,row->region,row->place,row->scene);f.Rules(37);
        if(denied==0) f.ruleSpots[0].registered=0;
        if(denied==1) f.ruleSpots[0].blocked=1;
        if(denied==2) f.ruleAreas[0].blocked=1;
        if(denied==3) tracker::ruleProof={};
        if(denied==4) f.ruleSpots[0].visible=0;
        tracker::Sky2BeforeRevisitUpdate(Address(f.minimap));
        Check(tracker::QueueRevisitNativeTravel(37,900+denied,tracker::ReadRevisitNativeContext())&&f.Close(),
              "experiment bypasses registration gray area unvisited and before-script permission only");
        Check(!testExperimentalTripActive,"queuing or closing alone cannot enable expanded trip protection");
        f.Transfer();
        Check(!tracker::Sky2BeforeRevisitJump(Address(f.field),37,tracker::base+0x299674)&&testExperimentalTripActive,
              "expanded trip protection begins only at authorized native consumer");
        Check(testExperimentalTripChapter==1,"ordinary native consumer binds protection to its current chapter");
    }
    for(uint32_t id:{35u,36u,92u,93u,94u,95u,96u,150u,165u,166u}) {
        f.Reset();std::fill_n(f.save.data()+0x100,4096,uint8_t{0});Put(f.save,0x11100+12*4,uint32_t{0x40000001});
        const auto* row=tracker::CatalogDestination(id,0);f.Destination(id,row->region,row->place,row->scene);
        tracker::Sky2BeforeRevisitUpdate(Address(f.minimap));
        Check(tracker::QueueRevisitNativeTravel(id,1000+id,tracker::ReadRevisitNativeContext())&&f.Close(),
              "complete no-map or reviewed internal native row supports experimental ordinary handoff");
        f.Transfer();
        Check(!tracker::Sky2BeforeRevisitJump(Address(f.field),id,tracker::base+0x29968F)&&testExperimentalTripActive,
              "special native row still reaches original map-jump lookup rather than custom guessed coordinates");
    }

    // 这12个公开ID原表place与实际scene不同。真实加载函数只使用scene/XYZ；
    // 以目标scene的空submap根确定到达地区，保留原表菜单place和原生加载路径。
    struct Alias { uint32_t id,root,region;uint8_t variant; };
    const Alias aliases[]={{10,1000000,1,1},{11,1000000,1,1},{23,1105000,2,2},
        {26,1100000,2,1},{29,1100000,2,1},{52,1200000,3,1},{60,1303000,4,1},
        {62,1200000,3,1},{66,1300000,4,1},{70,1300000,4,1},{127,1400000,5,0},{129,1200000,3,1}};
    const char emptySubmap[]="";
    for(const auto& alias:aliases) {
        f.Reset();Put(f.save,0x11100+12*4,uint32_t{0x40000001});f.Source(7,"mp5600_03");
        const auto* row=tracker::CatalogDestination(alias.id,0);f.Destination(alias.id,row->region,row->place,row->scene);
        Put(f.placeRows,0x1F8,alias.root);Put(f.placeRows,0x1F8+0x98,alias.region);
        Put(f.placeRows,0x1F8+0x90,alias.variant);Put(f.placeRows,0x1F8+0x10,reinterpret_cast<uintptr_t>(emptySubmap));
        tracker::RevisitReturnPoint goal{};
        Check(tracker::ValidateDestination(alias.id,&goal)&&goal.region==alias.region&&goal.mapPlace==row->place&&
              std::strcmp(goal.scene,row->scene)==0,"alias resolves true destination scene root without rewriting original native row");
        Check(tracker::QueueRevisitNativeTravel(alias.id,1200+alias.id,tracker::ReadRevisitNativeContext())&&f.Close(),
              "every native place scene alias passes verified map-close path");
        f.Transfer();
        Check(!tracker::Sky2BeforeRevisitJump(Address(f.field),alias.id,tracker::base+0x299674)&&
              tracker::arrivalPoint.region==alias.region,"alias final consumer retains native XYZ and verified destination region");
    }
    f.Reset();const auto* alias=tracker::CatalogDestination(62,0);f.Destination(62,alias->region,alias->place,alias->scene);
    Put(f.placeRows,0x1F8,uint32_t{1200000});Put(f.placeRows,0x1F8+0x98,uint32_t{3});
    Put(f.placeRows,0x1F8+0x10,reinterpret_cast<uintptr_t>(emptySubmap));
    tracker::RevisitReturnPoint aliasGoal{};
    Check(tracker::ValidateDestination(62,&aliasGoal)&&aliasGoal.region==3,"alias chooses target region rather than menu region");
    std::memcpy(f.placeRows.data()+0xA8,f.placeRows.data()+0x1F8,0xA8);
    Put(f.placeRows,0xA8,uint32_t{1200001});
    Check(!tracker::ValidateDestination(62,&aliasGoal),"two different default roots for one scene are rejected");
    Put(f.placeRows,0xA8,uint32_t{1200000});Put(f.placeRows,0xA8+0x90,uint8_t{1});
    Check(!tracker::ValidateDestination(62,&aliasGoal),"conflicting default root types are rejected");
    Put(f.placeRows,0xA8,uint32_t{0});Put(f.placeRows,0x1F8+0x10,uintptr_t{0});
    Check(!tracker::ValidateDestination(62,&aliasGoal),"unreadable target root submap identity is not guessed");

    for(unsigned corrupt=0;corrupt<7;++corrupt) {
        f.Reset();const auto* row=tracker::CatalogDestination(2,0);f.Destination(2,row->region,row->place,row->scene);
        if(corrupt==0) Put(f.row,0x40,uint32_t{999});
        if(corrupt==1) Put(f.row,0x44,100000.0f);
        if(corrupt==2) Put(f.row,0x60,uint8_t{7});
        if(corrupt==3) Put(f.row,0x58,uint8_t{8});
        if(corrupt==4) strcpy_s(f.targetScene,"mp9999");
        if(corrupt==5) Put(f.row,0x44,std::numeric_limits<float>::quiet_NaN());
        if(corrupt==6) Put(f.row,0x50,std::numeric_limits<float>::infinity());
        Check(tracker::QueueRevisitNativeTravel(2,1400+corrupt,tracker::ReadRevisitNativeContext())&&f.Close()&&f.MenuResult()==0&&
              !testExperimentalTripActive,"unrestricted permission does not bypass actual native row coordinate or scene checks");
    }
#endif
    // 森林和荣耀号的纯地理身份可以用于早章；实际可选保护缺失仍不得进入森林。
    f.Reset();std::fill_n(f.save.data()+0x100,4096,uint8_t{0});Put(f.save,0x11100+12*4,uint32_t{0x40000001});
    f.ForestTable();tracker::Sky2BeforeRevisitUpdate(Address(f.minimap));
    tracker::RevisitReturnPoint origin{};
    Check(tracker::ReadRevisitNativeReturnPoint(tracker::ReadRevisitNativeContext(),origin),"experimental forest departure still captures exact original position");
    Check(tracker::QueueRevisitNativeTravel(tracker::forest::kTarget,1500,tracker::ReadRevisitNativeContext())&&f.Close(),
          "early forest needs guard readiness but no story completion flags");
    f.Transfer();tracker::Sky2BeforeRevisitJump(Address(f.field),tracker::forest::kTarget,tracker::base+0x299674);
    Check(loadCalls==1&&tripActiveAtLoad&&testExperimentalTripActive,"forest trip protection is active before loader initialization");
    Check(testExperimentalTripChapter==1,"forest custom loader binds protection to its current chapter");
    f.Arrive(tracker::arrivalPoint);tracker::Sky2BeforeRevisitUpdate(Address(f.minimap));tracker::Sky2BeforeRevisitUpdate(Address(f.minimap));
    Check(tracker::ReadRevisitNativeContext().valid&&tracker::ReadRevisitNativeContext().region==1,
          "early forest raw region zero retains strict reviewed identity and canonical region");
    f.Reset();Put(f.save,0x11100+12*4,uint32_t{0x40000000});f.Source(7,"mp8500_01");
    Check(tracker::ReadRevisitNativeContext().valid&&tracker::ReadRevisitNativeContext().region==8&&
          tracker::RevisitNativeTargetAvailable(tracker::kRevisitReturnTarget,tracker::ReadRevisitNativeContext()),
          "early Glorious physical region7 alias can return without late story flags");

    f.Reset();tracker::SetExperimentalRevisitTripActive(true,8);tracker::ReadRevisitNativeReturnPoint(tracker::ReadRevisitNativeContext(),origin);f.ReturnTable(origin);
    Check(tracker::QueueRevisitNativeReturn(origin,1501,tracker::ReadRevisitNativeContext())&&f.Close()&&testExperimentalTripActive,
          "queuing exact return does not clear protection while still in destination");
    f.Transfer();tracker::Sky2BeforeRevisitJump(Address(f.field),tracker::kRevisitReturnTarget,tracker::base+0x299674);
    Check(loadCalls==1&&!tripActiveAtLoad&&!testExperimentalTripActive,"return turns off experimental protection before original source initialization");
    f.Reset();tracker::SetExperimentalRevisitTripActive(true,8);tracker::ReadRevisitNativeReturnPoint(tracker::ReadRevisitNativeContext(),origin);f.ReturnTable(origin);
    tracker::QueueRevisitNativeReturn(origin,1502,tracker::ReadRevisitNativeContext());f.Close();f.Transfer();acceptLoad=false;
    tracker::Sky2BeforeRevisitJump(Address(f.field),tracker::kRevisitReturnTarget,tracker::base+0x299674);
    Check(testExperimentalTripActive,"rejected native return loader restores previous experimental trip protection");
    Check(testExperimentalTripChapter==8,"failed return restores protection only for the consumer chapter");
    f.Reset();tracker::SetExperimentalRevisitTripActive(true,7);tracker::ReadRevisitNativeReturnPoint(tracker::ReadRevisitNativeContext(),origin);f.ReturnTable(origin);
    tracker::QueueRevisitNativeReturn(origin,1505,tracker::ReadRevisitNativeContext());f.Close();f.Transfer();acceptLoad=false;
    tracker::Sky2BeforeRevisitJump(Address(f.field),tracker::kRevisitReturnTarget,tracker::base+0x299674);
    Check(!testExperimentalTripActive,"failed return cannot restore stale protection from another chapter");
    f.Reset();f.ForestTable();tracker::QueueRevisitNativeTravel(tracker::forest::kTarget,1503,tracker::ReadRevisitNativeContext());
    f.Close();f.Transfer();acceptLoad=false;
    tracker::Sky2BeforeRevisitJump(Address(f.field),tracker::forest::kTarget,tracker::base+0x299674);
    Check(!testExperimentalTripActive,"rejected forest loader does not leave expanded protection enabled");
    f.Reset();testForestGuardReady=false;
    Check(!tracker::RevisitNativeTargetAvailable(tracker::forest::kTarget,tracker::ReadRevisitNativeContext()),"experiment still refuses missing forest protection");
    Check(std::strcmp(tracker::RevisitNativeTargetReason(tracker::forest::kTarget,tracker::ReadRevisitNativeContext()),
          "迷途之森剧情保护尚未就绪")==0,"experimental forest reason identifies missing guard rather than story completion");
    f.Reset();tracker::nativeLoad=nullptr;
    Check(std::strcmp(tracker::RevisitNativeTargetReason(tracker::forest::kTarget,tracker::ReadRevisitNativeContext()),
          "场景加载接口尚未就绪")==0,"experimental forest reason identifies missing loader rather than story completion");
    Check(std::strcmp(tracker::RevisitNativeTargetReason(156,tracker::ReadRevisitNativeContext()),
          "目的地原生数据尚未通过核对")==0,"hidden experimental targets do not report unrelated story restrictions");
    f.Reset();f.ForestTable();tracker::QueueRevisitNativeTravel(tracker::forest::kTarget,1504,tracker::ReadRevisitNativeContext());
    f.Close();f.Transfer();testForestGuardReady=false;
    tracker::Sky2BeforeRevisitJump(Address(f.field),tracker::forest::kTarget,tracker::base+0x299674);
    Check(loadCalls==0&&!testExperimentalTripActive,"protection lost before forest consumer prevents loading and trip activation");

    // 请求的5秒期限、取消、忙碌分支、真实菜单及版本身份不属于目的地剧情限制。
    for(unsigned invalid=0;invalid<3;++invalid) {
        f.Reset();
        if(invalid==0) Put(f.field,0x1BC8,uint32_t{1});
        if(invalid==1) f.save[0x10B]|=0x20;
        if(invalid==2) Put(f.menu,0xB8,int32_t{6});
        tracker::Sky2BeforeRevisitUpdate(Address(f.minimap));
        Check(!f.Queue()&&!testExperimentalTripActive,"experiment still requires stable normal browse and nonbusy loader");
    }
    f.Reset();f.Queue();f.Close();f.Transfer();tracker::authorize.store(&CancelAuthorize);
    Check(f.Dispatch()&&f.ResultKind()==0&&!testExperimentalTripActive,"final cancellation cannot activate experimental trip");
    f.Reset();tracker::SetExperimentalRevisitTripActive(true,8);f.Queue();f.Close();f.Transfer();tracker::requestedAt=GetTickCount64()-6000;
    Check(f.Dispatch()&&f.ResultKind()==0&&testExperimentalTripActive,"expired request preserves previous trip state without loading");
    // 直接调用原生捕获，不经过回访协调层或UI；普通地图上换章也必须退休令牌。
    f.Reset();tracker::SetExperimentalRevisitTripActive(true,8);
    Check(tracker::Capture().valid&&testExperimentalTripActive,"same chapter capture preserves an explicitly active trip");
    Put(f.save,0x11100+12*4,uint32_t{0x40000001});
    Check(tracker::Capture().valid&&!testExperimentalTripActive,"hidden UI ordinary scene capture retires another chapter trip");
    Put(f.save,0x11100+12*4,uint32_t{0x40000008});
    Check(tracker::Capture().valid&&!testExperimentalTripActive,"loading the previous chapter cannot revive retired trip protection");
    std::printf("revisit_native_unrestricted_tests: %u checks, %u failures\n",checks,failures);
    return failures?1:0;
}
}
int main() {
    Fixture f;
    if(!f.image) return 2;
#if SKY2_UNRESTRICTED_TRAVEL
    return RunUnrestricted(f);
#else
    f.Reset();
    const auto context=tracker::ReadRevisitNativeContext();
    Check(context.valid&&context.browsing&&context.prologueCompleted&&context.chapter==8,"valid native browse context");
    Check(context.returnPointReady,"validated point capture publishes current readiness");
    Check(f.Queue()&&f.Close(),"queue and close native map");
    Check(f.MenuResult()==(99ull|(1ull<<32)),"native result contains requested id and ordinary result");
    Check(tracker::ReadRevisitNativeStatus().phase==tracker::RevisitNativePhase::ClosingMap,"closing phase before consumer");
    f.Transfer();
    Check(!f.Dispatch(),"authorized native consumer continues original");
    Check(tracker::ReadRevisitNativeStatus().phase==tracker::RevisitNativePhase::Dispatched,"dispatch is reported only at consumer");
    Check(!tracker::dispatchArmed,"successful handoff releases ownership");

    f.Reset();f.Queue();tracker::requestedAt=GetTickCount64()-6000;
    Check(f.Close()&&f.MenuResult()==0,"expired queue never writes native result");
    Check(tracker::ReadRevisitNativeStatus().phase==tracker::RevisitNativePhase::Expired,"expired queue status");

    f.Reset();f.Queue();f.Close();tracker::requestedAt=GetTickCount64()-6000;
    tracker::Sky2BeforeRevisitUpdate(Address(f.minimap));
    Check(f.MenuResult()==0&&!tracker::dispatchArmed,"expired closing result is withdrawn without teleport");

    f.Reset();f.Queue();f.Close();f.Transfer();tracker::requestedAt=GetTickCount64()-6000;
    Check(f.Dispatch(),"expiry at final consumer blocks original");
    Check(f.ResultKind()==0&&!tracker::dispatchArmed,"expired copied result cannot retry next frame");

    f.Reset();f.Queue();tracker::authorize.store(&CancelAuthorize);
    Check(f.Close()&&f.MenuResult()==0&&!tracker::dispatchArmed,"cancel during first authorization cannot be revived");

    f.Reset();f.Queue();Put(f.menu,0xF8,reinterpret_cast<uintptr_t>(&CancelClose));f.Close();
    Check(tracker::ReadRevisitNativeStatus().phase==tracker::RevisitNativePhase::Rejected&&tracker::dispatchArmed,
          "cancel during native callback retains rejected handoff identity");
    f.Transfer();Check(f.Dispatch()&&f.ResultKind()==0,"cancelled callback cannot reach native load");

    f.Reset();f.Queue();f.Close();f.Transfer();tracker::authorize.store(&CancelAuthorize);
    Check(f.Dispatch()&&f.ResultKind()==0,"cancel during final authorization cannot be revived");
    Check(tracker::ReadRevisitNativeStatus().phase==tracker::RevisitNativePhase::Rejected,"last moment cancel stays rejected");

    f.Reset();f.Queue();f.Close();tracker::CancelRevisitNativeTravel();
    Put(f.minimap,0x28,Address(f.newMenu));Put(f.newMenu,0x3C0,uint64_t{99ull|(1ull<<32)});
    tracker::Sky2BeforeRevisitUpdate(Address(f.minimap));
    uint64_t foreign=0;std::memcpy(&foreign,f.newMenu.data()+0x3C0,8);
    Check(foreign==(99ull|(1ull<<32))&&!tracker::dispatchArmed,"replaced menu result is not cleared as old handoff");

    f.Reset();f.Queue();Put(f.minimap,0x28,uintptr_t{0});tracker::Sky2BeforeRevisitUpdate(Address(f.minimap));
    Check(tracker::ReadRevisitNativeStatus().phase==tracker::RevisitNativePhase::Rejected,"closing map independently cancels queued request");

    f.Reset();f.Queue();Put(f.save,0x11100+12*4,uint32_t{0x40000009});
    Check(f.Close()&&f.MenuResult()==0,"chapter changes invalidate queued request");
    f.Reset();f.Queue();f.save[0x100+16021/8]&=static_cast<unsigned char>(~(1u<<(16021%8)));
    Check(f.Close()&&f.MenuResult()==0,"incomplete prologue cannot dispatch");
    f.Reset();f.Queue();Put(f.row,0x40,uint32_t{999});
    Check(f.Close()&&f.MenuResult()==0,"altered native destination is rejected");
    f.Reset();f.save[0x10B]|=0x20;tracker::Sky2BeforeRevisitUpdate(Address(f.minimap));
    Check(!f.Queue(),"event-only map-change branch remains unavailable");
    Check(!tracker::ReadRevisitNativeContext().returnPointReady,"busy event mode clears previous return point readiness");
    f.Reset();Put(f.player,0x60,uintptr_t{0});tracker::Sky2BeforeRevisitUpdate(Address(f.minimap));
    Check(tracker::ReadRevisitNativeContext().valid&&!tracker::ReadRevisitNativeContext().returnPointReady,
          "failed actor capture never preserves earlier ready flag");
    Put(f.player,0x60,Address(f.actor));tracker::Sky2BeforeRevisitUpdate(Address(f.minimap));
    Check(tracker::ReadRevisitNativeContext().returnPointReady,"fresh successful capture restores readiness");
    f.Reset();
    f.Global(0xC60E08,uintptr_t{0});tracker::Sky2BeforeRevisitUpdate(Address(f.minimap));
    Check(!tracker::ReadRevisitNativeContext().valid,"unavailable game objects publish invalid context instead of stale state");

    // 每个已审核原生目的地逐一走生产提交及最终消费者校验，防止只验证首个入口。
    for(const auto& destination:tracker::destinations) {
        if(destination.id==15) continue;
        f.Reset();f.Destination(destination.id,destination.region,destination.place,destination.scene);
        if(destination.region==8 || destination.id==108) { Put(f.save,0x11100+12*4,uint32_t{0x40000009});tracker::Sky2BeforeRevisitUpdate(Address(f.minimap)); }
        Check(tracker::ValidateDestination(destination.id),"reviewed native destination validates");
        Check(tracker::QueueRevisitNativeTravel(destination.id,destination.id,tracker::ReadRevisitNativeContext())&&f.Close(),
              "every reviewed destination submits normally");
        f.Transfer();
        Check(!tracker::Sky2BeforeRevisitJump(Address(f.field),destination.id,tracker::base+0x299674)&&
              tracker::ReadRevisitNativeStatus().phase==tracker::RevisitNativePhase::Dispatched,
              "every reviewed destination reaches original consumer");
    }

    // 模拟Mod全新进程，无任何之前的会话内存，只凭旧地图存档上下文恢复固定返程。
    f.Reset();f.Source(6,"mp6012");f.Destination(15,2,1101000,"mp1000");
    Check(tracker::ReadRevisitNativeStatus().phase==tracker::RevisitNativePhase::Idle&&!tracker::dispatchArmed,
          "restart begins without in-memory revisit history");
    Check(tracker::QueueRevisitNativeTravel(15,100,tracker::ReadRevisitNativeContext())&&f.Close(),
          "chapter-eight prologue save can request fixed Bose return after restart");
    f.Transfer();
    Check(!tracker::Sky2BeforeRevisitJump(Address(f.field),15,tracker::base+0x29968F),
          "restart return reaches original native consumer");

    f.Reset();f.Source(2,"mp1010");Check(f.Queue(),"ordinary source is no longer restricted to Bose city");
    f.Reset();Put(f.field,0x1BC8,uint32_t{1});tracker::Sky2BeforeRevisitUpdate(Address(f.minimap));
    Check(!f.Queue(),"active field-map change rejects new request");
    f.Reset();tracker::publishedAt=GetTickCount64()-2000;
    Check(!f.Queue(),"stale published context cannot queue after native updates stop");
    f.Reset();f.Queue();f.newMenu=f.menu;
    Put(f.newMenu,0x18,Address(f.newMenu));Put(f.minimap,0x28,Address(f.newMenu));
    Check(tracker::BeforeRevisitNativeBrowse(Address(f.newMenu))&&f.MenuResult()==0,
          "a new menu in the same scene cannot consume an old request");

    // 返程点来自与原生保存相同的 actor 字段；发布 API 不暴露对象地址，不复用旧坐标。
    f.Reset();tracker::RevisitReturnPoint anchor{};
    Check(tracker::ReadRevisitNativeReturnPoint(tracker::ReadRevisitNativeContext(),anchor)&&
          anchor.xyz[0]==23&&anchor.xyz[1]==-3&&anchor.xyz[2]==54&&anchor.yawRadians==0,
          "capture exact native-save actor position and yaw");
    Put(f.actor,0xD8,float{999});tracker::Sky2BeforeRevisitUpdate(Address(f.minimap));
    Check(tracker::ReadRevisitNativeReturnPoint(tracker::ReadRevisitNativeContext(),anchor)&&anchor.xyz[0]==23,
          "capture uses native-save E8 position rather than another transform");
    for(float yaw:{0.f,1.5707963268f,3.1415926536f,4.7123889804f,6.2830f}) {
        const float q[]={0,std::sin(yaw/2),0,std::cos(yaw/2)};float actual=0;
        Check(tracker::QuaternionYaw(q,actual)&&std::fabs(actual-yaw)<0.00001f,"quaternion yaw matches native forward rotation");
    }
    const float zeroQuat[]={0,0,0,0},nonUnit[]={0,0,0,2};float invalidYaw=0;
    Check(!tracker::QuaternionYaw(zeroQuat,invalidYaw)&&!tracker::QuaternionYaw(nonUnit,invalidYaw),"invalid quaternion fails closed");
    Put(f.actor,0x114,float{0});tracker::Sky2BeforeRevisitUpdate(Address(f.minimap));
    Check(!tracker::ReadRevisitNativeReturnPoint(tracker::ReadRevisitNativeContext(),anchor)&&anchor.scene[0]==0,
          "invalid actor clears published capture instead of reusing old point");
    f.Reset();Put(f.player,0,uintptr_t{0});tracker::Sky2BeforeRevisitUpdate(Address(f.minimap));
    Check(!tracker::ReadRevisitNativeReturnPoint(tracker::ReadRevisitNativeContext(),anchor),"unknown controller vtable cannot supply coordinates");

    // 模拟重启：只重新提供磁盘解码出的点，不保留任何上次进程内的回访状态。
    f.Reset();Put(f.save,0x11100+12*4,uint32_t{0x40000009});f.Source(7,"mp5600_03");
    Check(tracker::ReadRevisitNativeReturnPoint(tracker::ReadRevisitNativeContext(),anchor),"capture final-chapter origin outside ordinary travel menu range");
    anchor.yawRadians=5.747763f;
    f.Reset();Put(f.save,0x11100+12*4,uint32_t{0x40000009});f.Source(6,"mp6012");f.ReturnTable(anchor);
    Check(tracker::QueueRevisitNativeReturn(anchor,201,tracker::ReadRevisitNativeContext())&&f.Close(),"restart can queue exact return from retained point");
    f.Transfer();
    Check(tracker::Sky2BeforeRevisitJump(Address(f.field),tracker::kRevisitReturnTarget,tracker::base+0x299674)&&
          loadCalls==1&&loadedFlags==0x4001&&std::strcmp(loadedPoint.scene,anchor.scene)==0&&
          !std::memcmp(loadedPoint.xyz,anchor.xyz,sizeof(anchor.xyz))&&loadedPoint.yawRadians==anchor.yawRadians,
          "final native consumer receives exact scene coordinates yaw and ordinary load flags");
    Check(tracker::ReadRevisitNativeStatus().phase==tracker::RevisitNativePhase::Dispatched&&!f.Queue(202),
          "loader handoff is not arrival and blocks another request");
    Check(f.ResultKind()==0,"consumed return sentinel is removed and cannot run twice");
    f.Arrive(anchor);tracker::Sky2BeforeRevisitUpdate(Address(f.minimap));tracker::Sky2BeforeRevisitUpdate(Address(f.minimap));
    Check(tracker::ReadRevisitNativeStatus().phase==tracker::RevisitNativePhase::Arrived,"exact return arrival confirmed after stable actual position");

    // 同场景必须沿原生同图状态机，并且没有加载转换证据时不能误报已到达。
    f.Reset();tracker::ReadRevisitNativeReturnPoint(tracker::ReadRevisitNativeContext(),anchor);f.ReturnTable(anchor);
    Check(tracker::QueueRevisitNativeReturn(anchor,203,tracker::ReadRevisitNativeContext())&&f.Close(),"same-scene return queues");
    f.Transfer();tracker::Sky2BeforeRevisitJump(Address(f.field),tracker::kRevisitReturnTarget,tracker::base+0x299674);
    Check(loadedFlags==0x4021,"same-scene return uses original reposition lifecycle flag");
    tracker::sawLoadTransition=false;Put(f.field,0x1BC8,uint32_t{0});
    for(int i=0;i<4;++i) tracker::Sky2BeforeRevisitUpdate(Address(f.minimap));
    Check(tracker::ReadRevisitNativeStatus().phase==tracker::RevisitNativePhase::Dispatched,"matching preexisting position alone is not proof of arrival");
    tracker::dispatchedAt=GetTickCount64()-61000;tracker::Sky2BeforeRevisitUpdate(Address(f.minimap));
    Check(tracker::ReadRevisitNativeStatus().phase==tracker::RevisitNativePhase::ArrivalUnconfirmed,"unconfirmed load times out without losing dispatched history");
    Check(tracker::QueueRevisitNativeTravel(99,299,tracker::ReadRevisitNativeContext())==false,
          "expired dispatch cannot accept new request until the native map is reopened");

    f.Reset();tracker::ReadRevisitNativeReturnPoint(tracker::ReadRevisitNativeContext(),anchor);
    anchor.variant=1;f.ReturnTable(anchor);Put(f.placeRows,0xA8+0x90,uint8_t{0});
    Check(tracker::QueueRevisitNativeReturn(anchor,204,tracker::ReadRevisitNativeContext())&&f.Close()&&f.MenuResult()==0,
          "invalid return variant rejected before native map closes");
    f.Reset();tracker::ReadRevisitNativeReturnPoint(tracker::ReadRevisitNativeContext(),anchor);f.ReturnTable(anchor);
    tracker::QueueRevisitNativeReturn(anchor,205,tracker::ReadRevisitNativeContext());f.Close();f.Transfer();tracker::authorize.store(&CancelAuthorize);
    Check(tracker::Sky2BeforeRevisitJump(Address(f.field),tracker::kRevisitReturnTarget,tracker::base+0x299674)&&loadCalls==0,
          "cancel during final return authorization never calls native loader");
    f.Reset();tracker::ReadRevisitNativeReturnPoint(tracker::ReadRevisitNativeContext(),anchor);f.ReturnTable(anchor);
    tracker::QueueRevisitNativeReturn(anchor,206,tracker::ReadRevisitNativeContext());f.Close();f.Transfer();acceptLoad=false;
    tracker::Sky2BeforeRevisitJump(Address(f.field),tracker::kRevisitReturnTarget,tracker::base+0x299674);
    Check(tracker::ReadRevisitNativeStatus().phase==tracker::RevisitNativePhase::Rejected,"native loader that fails to arm transition cannot report success");
    f.Reset();tracker::ReadRevisitNativeReturnPoint(tracker::ReadRevisitNativeContext(),anchor);f.ReturnTable(anchor);
    f.save[0x100+22040/8]|=1u<<(22040%8);tracker::Sky2BeforeRevisitUpdate(Address(f.minimap));
    Check(anchor.progressSignature!=tracker::ReadRevisitNativeContext().progressSignature&&
          tracker::QueueRevisitNativeReturn(anchor,207,tracker::ReadRevisitNativeContext())&&f.Close(),
          "long-term return allows legitimate optional observation since the anchor was recorded");
    f.Transfer();tracker::Sky2BeforeRevisitJump(Address(f.field),tracker::kRevisitReturnTarget,tracker::base+0x299674);
    Check(loadCalls==1,"changed optional story bit does not strand the player after restart");
    f.Reset();tracker::ReadRevisitNativeReturnPoint(tracker::ReadRevisitNativeContext(),anchor);f.ReturnTable(anchor);
    tracker::QueueRevisitNativeReturn(anchor,208,tracker::ReadRevisitNativeContext());
    f.save[0x100+22041/8]|=1u<<(22041%8);
    Check(f.Close()&&f.MenuResult()==0&&loadCalls==0,"story change during short-lived queue cancels exact return");

    // 目的地剧情门槛与特殊箱保护共享；不能仅以章节靠后替代主线收尾证据。
    f.Reset();Check(!tracker::RevisitNativeTargetAvailable(165,tracker::ReadRevisitNativeContext()),"Glorious revisit remains unavailable in chapter eight");
    Put(f.save,0x11100+12*4,uint32_t{0x40000009});tracker::Sky2BeforeRevisitUpdate(Address(f.minimap));
    Check(tracker::RevisitNativeTargetAvailable(165,tracker::ReadRevisitNativeContext()),"completed final-chapter Glorious is available");
    f.save[0x100+25040/8]&=static_cast<unsigned char>(~(1u<<(25040%8)));tracker::Sky2BeforeRevisitUpdate(Address(f.minimap));
    Check(!tracker::RevisitNativeTargetAvailable(165,tracker::ReadRevisitNativeContext()),"special chest story completion is mandatory for Glorious");
    f.Reset();f.save[0x100+22047/8]&=static_cast<unsigned char>(~(1u<<(22047%8)));tracker::Sky2BeforeRevisitUpdate(Address(f.minimap));
    Check(!tracker::RevisitNativeTargetAvailable(120,tracker::ReadRevisitNativeContext()),"unfinished laboratory cannot be selected");

    f.Reset();f.Source(9,"mp6100");
    Check(tracker::RevisitNativeTargetAvailable(tracker::kRevisitReturnTarget,tracker::ReadRevisitNativeContext()),
          "reviewed laboratory exterior supports restart return");
    f.Reset();Put(f.save,0x11100+12*4,uint32_t{0x40000009});f.Source(8,"mp8500");
    Check(tracker::RevisitNativeTargetAvailable(tracker::kRevisitReturnTarget,tracker::ReadRevisitNativeContext()),
          "reviewed Glorious deck supports restart return");
    f.Source(8,"mp8509");Check(!f.Queue(),"unreviewed scene in special region remains unavailable");
    f.Reset();Put(f.save,0x11100+12*4,uint32_t{0x40000009});f.Source(6,"mp6012");
    Check(!tracker::RevisitNativeTargetAvailable(15,tracker::ReadRevisitNativeContext()),"legacy emergency Bose anchor is chapter-eight only");

    // 出发也必须观察正常加载及稳定目的地；UI 不能在 Dispatched 时就结束会话。
    f.Reset();f.Queue();f.Close();f.Transfer();f.Dispatch();
    f.Source(6,"mp6011");tracker::Sky2BeforeRevisitUpdate(Address(f.minimap));tracker::Sky2BeforeRevisitUpdate(Address(f.minimap));
    Check(tracker::ReadRevisitNativeStatus().phase==tracker::RevisitNativePhase::Arrived,"outbound scene transition confirms native destination");
    f.Reset();f.Source(6,"mp6011");f.Queue();f.Close();f.Transfer();f.Dispatch();
    for(int i=0;i<4;++i) tracker::Sky2BeforeRevisitUpdate(Address(f.minimap));
    Check(tracker::ReadRevisitNativeStatus().phase==tracker::RevisitNativePhase::Dispatched,"same-scene outbound requires busy transition evidence");
    Put(f.field,0x1BC8,uint32_t{1});tracker::Sky2BeforeRevisitUpdate(Address(f.minimap));
    Put(f.field,0x1BC8,uint32_t{0});
    for(int i=0;i<3;++i) tracker::Sky2BeforeRevisitUpdate(Address(f.minimap));
    Check(tracker::ReadRevisitNativeStatus().phase==tracker::RevisitNativePhase::Arrived,"same-scene native load can confirm after busy clears");

    f.Reset();tracker::ReadRevisitNativeReturnPoint(tracker::ReadRevisitNativeContext(),anchor);f.ReturnTable(anchor);
    tracker::QueueRevisitNativeReturn(anchor,209,tracker::ReadRevisitNativeContext());f.Close();f.Transfer();
    tracker::Sky2BeforeRevisitJump(Address(f.field),tracker::kRevisitReturnTarget,tracker::base+0x299674);
    Put(f.field,0x1BC8,uint32_t{0});Put(f.actor,0xE8,anchor.xyz[0]+5);
    for(int i=0;i<4;++i) tracker::Sky2BeforeRevisitUpdate(Address(f.minimap));
    Check(tracker::ReadRevisitNativeStatus().phase==tracker::RevisitNativePhase::Dispatched,"right scene but wrong position cannot confirm exact return");
    Put(f.actor,0xE8,anchor.xyz[0]);Put(f.actor,0x10C,float{1});Put(f.actor,0x114,float{0});
    for(int i=0;i<4;++i) tracker::Sky2BeforeRevisitUpdate(Address(f.minimap));
    Check(tracker::ReadRevisitNativeStatus().phase==tracker::RevisitNativePhase::Dispatched,"right position but wrong facing cannot confirm exact return");
    f.save[0x100+22270/8]|=1u<<(22270%8);f.Arrive(anchor);
    tracker::Sky2BeforeRevisitUpdate(Address(f.minimap));tracker::Sky2BeforeRevisitUpdate(Address(f.minimap));
    Check(tracker::ReadRevisitNativeStatus().phase==tracker::RevisitNativePhase::Arrived,"legal Reinit flag change after load does not invalidate actual arrival");

    f.Reset();tracker::ReadRevisitNativeReturnPoint(tracker::ReadRevisitNativeContext(),anchor);f.ReturnTable(anchor);
    tracker::QueueRevisitNativeReturn(anchor,210,tracker::ReadRevisitNativeContext());f.Close();f.Transfer();
    tracker::requestedAt=GetTickCount64()-6000;
    Check(tracker::Sky2BeforeRevisitJump(Address(f.field),tracker::kRevisitReturnTarget,tracker::base+0x299674)&&loadCalls==0&&f.ResultKind()==0,
          "expired exact return is withdrawn without executing load");
    f.Reset();tracker::ReadRevisitNativeReturnPoint(tracker::ReadRevisitNativeContext(),anchor);f.ReturnTable(anchor);
    tracker::QueueRevisitNativeReturn(anchor,211,tracker::ReadRevisitNativeContext());f.Close();f.Transfer();
    f.save[0x10B]|=0x20;
    Check(tracker::Sky2BeforeRevisitJump(Address(f.field),tracker::kRevisitReturnTarget,tracker::base+0x299674)&&loadCalls==0,
          "event-mode flag introduced after close blocks custom return loader");

    // 荣耀号终章的根地点region7，内部地形region0；归一不能丢失真实ID/scene校验。
    f.Reset();Put(f.save,0x11100+12*4,uint32_t{0x40000009});f.Source(8,"mp8500_01");
    Put(f.sceneRoot,0x808+0x98,uint32_t{7});Put(f.scene,0,uint32_t{1850011});Put(f.scene,0x98,uint32_t{0});
    Put(f.placeHeaders,0x4C,uint32_t{3});
    Put(f.placeRows,0xA8,uint32_t{1850001});Put(f.placeRows,0xA8+8,reinterpret_cast<uintptr_t>(f.currentScene));
    Put(f.placeRows,0xA8+0x98,uint32_t{7});
    Put(f.placeRows,0x150,uint32_t{1850011});Put(f.placeRows,0x158,reinterpret_cast<uintptr_t>(f.currentScene));
    tracker::Sky2BeforeRevisitUpdate(Address(f.minimap));
    Check(tracker::ReadRevisitNativeContext().valid&&tracker::ReadRevisitNativeContext().region==8&&
          tracker::ReadRevisitNativeContext().nativeRegion==0&&tracker::ReadRevisitNativeContext().sceneRegion==7,
          "chapter-nine Glorious duplicate root record and zero-region terrain normalize safely");
    Check(tracker::ReadRevisitNativeReturnPoint(tracker::ReadRevisitNativeContext(),anchor)&&
          anchor.place==1850001&&anchor.mapPlace==1850011&&anchor.region==8,
          "exact position remains capturable on zero-region interior terrain");
    Put(f.placeRows,0x150,uint32_t{1850099});tracker::Sky2BeforeRevisitUpdate(Address(f.minimap));
    Check(!tracker::ReadRevisitNativeContext().valid,"unmatched current terrain ID never gains validity from region normalization");

    // 普通到达只依赖可靠场景；暂时没有可保存的角色朝向不应让全清单永远锁住。
    f.Reset();f.Queue();f.Close();f.Transfer();f.Dispatch();f.Source(6,"mp6011");
    Put(f.actor,0x114,float{0});
    for(int i=0;i<3;++i) tracker::Sky2BeforeRevisitUpdate(Address(f.minimap));
    Check(tracker::ReadRevisitNativeStatus().phase==tracker::RevisitNativePhase::Arrived&&
          !tracker::ReadRevisitNativeReturnPoint(tracker::ReadRevisitNativeContext(),anchor),
          "outbound arrival does not require exact-return actor capture");
    f.Reset();f.Source(6,"mp6011");f.Queue();f.Close();f.Transfer();f.Dispatch();
    f.Global(0xC60E08,uintptr_t{0});tracker::Sky2BeforeRevisitUpdate(Address(f.minimap));
    f.Global(0xC60E08,Address(f.field));
    for(int i=0;i<3;++i) tracker::Sky2BeforeRevisitUpdate(Address(f.minimap));
    Check(!tracker::sawLoadTransition&&tracker::ReadRevisitNativeStatus().phase==tracker::RevisitNativePhase::Dispatched,
          "temporarily unreadable context is not false evidence of same-scene loading");

    // 原生Minimap Update在换图时可能停帧，因此有时看不到短暂busy；必须用已核对
    // 的原生待加载描述变化证明请求实际进入加载，不能只看scene没有变化。
    f.Reset();f.Source(6,"mp6011");f.Queue();f.Close();f.Transfer();f.Dispatch();
    std::memcpy(f.field.data()+0x194,f.currentScene,sizeof(f.currentScene));
    const float nativeTarget[3]={0,0,0};std::memcpy(f.field.data()+0x1BA8,nativeTarget,sizeof(nativeTarget));
    Put(f.field,0x1BB8,float{0});
    for(int i=0;i<3;++i) tracker::Sky2BeforeRevisitUpdate(Address(f.minimap));
    Check(tracker::ReadRevisitNativeStatus().phase==tracker::RevisitNativePhase::Arrived,
          "changed native load descriptor confirms same-scene transition when busy frame was not observed");
    f.Reset();f.Source(6,"mp6011");
    std::memcpy(f.field.data()+0x194,f.currentScene,sizeof(f.currentScene));
    std::memcpy(f.field.data()+0x1BA8,nativeTarget,sizeof(nativeTarget));Put(f.field,0x1BB8,float{0});
    f.Queue();f.Close();f.Transfer();f.Dispatch();
    for(int i=0;i<3;++i) tracker::Sky2BeforeRevisitUpdate(Address(f.minimap));
    Check(tracker::ReadRevisitNativeStatus().phase==tracker::RevisitNativePhase::Dispatched,
          "unchanged prior target descriptor cannot falsely confirm same-scene arrival");
    std::memcpy(f.actor.data()+0xE8,nativeTarget,sizeof(nativeTarget));
    for(int i=0;i<3;++i) tracker::Sky2BeforeRevisitUpdate(Address(f.minimap));
    Check(tracker::ReadRevisitNativeStatus().phase==tracker::RevisitNativePhase::Arrived,
          "returning from a different actual position proves repeated same-point native travel");
    f.Reset();f.Queue();f.Close();f.Transfer();f.Dispatch();tracker::dispatchedAt=GetTickCount64()-61000;
    Check(tracker::ReadRevisitNativeStatus().phase==tracker::RevisitNativePhase::ArrivalUnconfirmed,
          "arrival timeout also releases waiting when native update hook stops temporarily");

    // 短字符串紧贴页末仍然有效；原实现一次读取32字节会越过NUL进入不可读页。
    auto* pages=static_cast<unsigned char*>(VirtualAlloc(nullptr,8192,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    if (pages) {
        DWORD old=0;VirtualProtect(pages+4096,4096,PAGE_NOACCESS,&old);
        const char name[]="mp1000";std::memcpy(pages+4096-sizeof(name),name,sizeof(name));char sceneName[32]{};
        Check(tracker::ReadScene(reinterpret_cast<uintptr_t>(pages+4096-sizeof(name)),sceneName)&&!std::strcmp(sceneName,name),
              "scene string reader stops at terminator before inaccessible next page");
        VirtualFree(pages,0,MEM_RELEASE);
    } else Check(false,"allocate isolated page-boundary string test");

#if SKY2_HAS_FULL_TRAVEL_CATALOG
    // 真实早期道路回归：大地图根1200000的类型为1，阿伊纳街道1208000为2。
    // 原实现把当前类型2同时套用到根记录，导致valid1却poseIssue6且无法出发。
    f.Reset();Put(f.save,0x11100+12*4,uint32_t{0x40000001});
    f.save[0x100+17016/8]|=1u<<(17016%8);f.Source(3,"mp2000");f.SourceTerrain(1200000,1208000,1,2);
    const auto splitContext=tracker::ReadRevisitNativeContext();
    tracker::RevisitReturnPoint splitPoint{};
    Check(splitContext.valid&&splitContext.browsing&&splitContext.returnPointReady&&tracker::pointIssue==0,
          "real root type one and terrain type two publish a valid captured point");
    Check(tracker::ReadRevisitNativeReturnPoint(splitContext,splitPoint)&&splitPoint.place==1200000&&
          splitPoint.mapPlace==1208000&&splitPoint.variant==2&&tracker::ValidateReturnTable(splitPoint),
          "return record retains current terrain type without rewriting root identity");
    auto incorrectType=splitPoint;incorrectType.variant=1;
    Check(!tracker::ValidateReturnTable(incorrectType),"root type cannot substitute for recorded current terrain type");
    auto incorrectScene=splitPoint;strcpy_s(incorrectScene.scene,"mp1000");
    Check(!tracker::ValidateReturnTable(incorrectScene),"independent root type lookup still requires exact saved scene");
    Check(tracker::QueueRevisitNativeReturn(splitPoint,700,splitContext)&&f.Close()&&
          tracker::requestedReturn.variant==2&&tracker::requestedReturn.place==1200000,
          "real split-type return queues with original record fields unchanged");
    f.Transfer();tracker::Sky2BeforeRevisitJump(Address(f.field),tracker::kRevisitReturnTarget,tracker::base+0x299674);
    Check(loadCalls==1&&loadedFlags==0x4021&&tracker::arrivalPoint.variant==2,
          "split-type return reaches native same-scene consumer while retaining terrain arrival identity");
    tracker::Sky2BeforeRevisitUpdate(Address(f.minimap));
    Check(!tracker::ReadRevisitNativeContext().returnPointReady,"native transition clears captured-point readiness");
    Put(f.field,0x1BC8,uint32_t{0});
    for(int i=0;i<3;++i) tracker::Sky2BeforeRevisitUpdate(Address(f.minimap));
    Check(tracker::ReadRevisitNativeStatus().phase==tracker::RevisitNativePhase::Arrived&&
          tracker::ReadRevisitNativeContext().returnPointReady,"split-type exact return confirms actual position and restores capture readiness");

    // 同ID/scene/region若存在两种根类型，就没有足够证据选择其一，必须拒绝。
    // 相同类型的重复行可以合并；原本合法的荣耀号地区7/8别名另由既有回归覆盖。
    f.Reset();Put(f.save,0x11100+12*4,uint32_t{0x40000001});f.Source(3,"mp2000");f.SourceTerrain(1200000,1208000,1,2);
    tracker::ReadRevisitNativeReturnPoint(tracker::ReadRevisitNativeContext(),splitPoint);
    Put(f.placeRows,0xA8,uint32_t{1200000});Put(f.placeRows,0xA8+8,reinterpret_cast<uintptr_t>(f.currentScene));
    Put(f.placeRows,0xA8+0x98,uint32_t{3});Put(f.placeRows,0xA8+0x90,uint8_t{1});
    Check(tracker::ValidateReturnTable(splitPoint),"identical root type duplicates do not fabricate ambiguity");
    Put(f.placeRows,0xA8+0x90,uint8_t{2});tracker::Sky2BeforeRevisitUpdate(Address(f.minimap));
    Check(!tracker::ValidateReturnTable(splitPoint)&&tracker::ReadRevisitNativeContext().valid&&
          !tracker::ReadRevisitNativeContext().returnPointReady&&tracker::pointIssue==6,
          "conflicting root types in one region reject capture and clear readiness");
    Put(f.placeRows,0xA8+0x90,uint8_t{1});tracker::Sky2BeforeRevisitUpdate(Address(f.minimap));
    Check(tracker::ReadRevisitNativeContext().returnPointReady,"resolving root ambiguity requires a fresh successful capture");

    // 公共城市ID的同家族内部名称是原生按当前地形选中的落点变体。四城都
    // 存在type2分岐，必须仍按原生行的完整字段核对，而不是强制使用默认入口。
    for(uint32_t id:{1u,15u,37u,54u}) {
        f.Reset();Put(f.save,0x11100+12*4,uint32_t{0x40000001});f.Source(3,"mp2000");
        f.SourceTerrain(1200000,1208000,1,2);
        const auto* selected=tracker::CatalogDestination(id,2);
        const auto* canonical=tracker::CatalogDestination(id,0);
        Check(selected&&canonical&&selected->kind==1&&selected->variant==2&&
              tracker::PublicEntryVariant(*canonical,*selected)&&tracker::OrdinaryCatalogDestination(*selected),
              "native city branch belongs to fully matching public destination family");
        f.Destination(id,selected->region,selected->place,selected->scene,2);f.Rules(id);
        const auto cityContext=tracker::ReadRevisitNativeContext();
        Check(cityContext.returnPointReady&&tracker::RevisitNativeOrdinaryTargetAvailable(id,cityContext),
              "public city branch retains ordinary native registration and rule permission");
        Check(tracker::QueueRevisitNativeTravel(id,720+id,cityContext)&&f.Close(),
              "selected city variant passes runtime identity verification before native close");
        f.Transfer();
        Check(!tracker::Sky2BeforeRevisitJump(Address(f.field),id,tracker::base+0x299674)&&
              tracker::ReadRevisitNativeStatus().phase==tracker::RevisitNativePhase::Dispatched,
              "city variant reaches original consumer without replacing native branch selection");
    }
    const auto* cityCanonical=tracker::CatalogDestination(37,0);
    const auto* cityVariant=tracker::CatalogDestination(37,2);
    for(unsigned corrupt=0;corrupt<8;++corrupt) {
        auto changed=*cityVariant;
        if(corrupt==0) ++changed.id;
        if(corrupt==1) ++changed.region;
        if(corrupt==2) ++changed.area;
        if(corrupt==3) ++changed.place;
        if(corrupt==4) changed.scene="mp1000";
        if(corrupt==5) changed.flags|=8;
        if(corrupt==6) changed.kind=2;
        if(corrupt==7) changed.variant=0;
        Check(!tracker::PublicEntryVariant(*cityCanonical,changed),
              "city branch family rejects mismatched identity category flags or default variant");
    }
    f.Reset();Put(f.save,0x11100+12*4,uint32_t{0x40000001});f.Source(3,"mp2000");f.SourceTerrain(1200000,1208000,1,2);
    const auto* south=tracker::CatalogDestination(43,2);
    Check(south&&south->variant==0&&south->kind==0,"ordinary ID without type2 branch falls back to first row");
    f.Destination(43,south->region,south->place,south->scene);f.Rules(43);
    Check(tracker::QueueRevisitNativeTravel(43,780,tracker::ReadRevisitNativeContext())&&f.Close(),
          "ordinary fallback row uses same native selection and verification path");
    f.Reset();Put(f.save,0x11100+12*4,uint32_t{0x40000001});f.Source(3,"mp2000");f.SourceTerrain(1200000,1208000,1,2);
    f.Destination(37,cityVariant->region,cityVariant->place,cityVariant->scene,2);f.Rules(37);
    tracker::QueueRevisitNativeTravel(37,781,tracker::ReadRevisitNativeContext());Put(f.row,0x60,uint8_t{0});
    Check(f.Close()&&f.MenuResult()==0,"city variant runtime mismatch cannot silently use public default record");
    for(uint32_t id:{156u,157u,158u,159u,160u,161u,162u,163u,164u,167u,168u}) {
        f.Reset();Put(f.save,0x11100+12*4,uint32_t{0x40000001});f.Source(3,"mp2000");f.SourceTerrain(1200000,1208000,1,2);f.Rules(id);
        Check(!tracker::RevisitNativeTargetAvailable(id,tracker::ReadRevisitNativeContext()),
              "pure internal destination ID remains unavailable despite city branch support");
    }
    for(unsigned block=0;block<3;++block) {
        f.Reset();Put(f.save,0x11100+12*4,uint32_t{0x40000001});f.Source(3,"mp2000");f.SourceTerrain(1200000,1208000,1,2);f.Rules(37);
        if(block==0) f.ruleSpots[0].registered=0;
        if(block==1) f.ruleSpots[0].blocked=1;
        if(block==2) f.ruleAreas[0].blocked=1;
        tracker::Sky2BeforeRevisitUpdate(Address(f.minimap));
        Check(!tracker::RevisitNativeTargetAvailable(37,tracker::ReadRevisitNativeContext()),
              "city variant cannot bypass registration story block or parent area block");
    }

    // 所有合法章节均允许普通原生目标；“序章已经全部完成”不再是普通城市
    // 的全局前提。晚期旧图例外仍由下方原有测试验证独立完成门槛。
    for(uint32_t chapter=0;chapter<=9;++chapter) {
        f.Reset();Put(f.save,0x11100+12*4,uint32_t{0x40000000u|chapter});
        f.save[0x100+16010/8]&=static_cast<unsigned char>(~(1u<<(16010%8)));
        f.Rules(2);
        const auto early=tracker::ReadRevisitNativeContext();
        Check(early.valid&&!early.prologueCompleted&&tracker::RevisitNativeTargetAvailable(2,early),
              "ordinary native destination supports every chapter without completed prologue");
    }
    f.Reset();Put(f.save,0x11100+12*4,uint32_t{0x4000000A});f.Rules(2);
    Check(!tracker::ReadRevisitNativeContext().valid&&!tracker::RevisitNativeTargetAvailable(2,tracker::ReadRevisitNativeContext()),
          "out of range chapter never gains ordinary destination permission");

    // 前置规则覆盖每个独立原生接管分支；逐个移除必要正旗标/补上禁止旗标，
    // 确認并非仅依章节宽泛拦截。检查只读数组，测试也不执行任何游戏脚本。
    struct BeforeCase { uint32_t chapter,target;std::initializer_list<uint32_t> yes,no; };
    const BeforeCase beforeCases[]={
        {0,99,{16019},{16021}}, {0,2,{16019},{16021}},
        {1,53,{17016},{17017}}, {1,2,{17019},{17020}},
        {1,2,{17022},{17023}}, {1,46,{17023},{17024}},
        {2,2,{18002},{18305,18006}}, {2,62,{18029},{18263}}, {2,64,{18042},{18043}},
        {3,2,{19044,19049,19050},{19051,19264}}, {3,2,{19064},{19065}},
        {3,79,{19067},{19068}}, {3,2,{1073},{19072}}, {3,80,{19076},{19077}},
        {4,2,{20027},{20028}}, {4,7,{20277},{20031}}, {4,13,{20061},{20062}},
        {6,2,{22341},{22344}}, {8,131,{}, {24014}}, {8,55,{}, {24020}}
    };
    for(const auto& sample:beforeCases) {
        std::array<uint8_t,4096> flags{};
        for(const auto flag:sample.yes) flags[flag/8]|=1u<<(flag%8);
        const auto blocked=[&] { return tracker::revisit_phase::BeforeScriptBlocked(
            sample.chapter,sample.target,flags.data(),flags.size()); };
        Check(blocked(),"each native before-script branch is recognized");
        Check(tracker::revisit_phase::BeforeScriptBlocked(sample.chapter,tracker::revisit_phase::kAnyDestination,flags.data(),flags.size()),
              "any-destination query recognizes destination-specific branch");
        const auto original=flags;
        for(const auto flag:sample.yes) {
            flags=original;flags[flag/8]&=static_cast<uint8_t>(~(1u<<(flag%8)));
            Check(!blocked(),"before-script branch requires every positive condition");
        }
        for(const auto flag:sample.no) {
            flags=original;flags[flag/8]|=1u<<(flag%8);
            Check(!blocked(),"completed negative condition disables before-script branch");
        }
    }
    std::array<uint8_t,4096> earlyFlags{};
    earlyFlags[17022/8]|=1u<<(17022%8);
    Check(!tracker::revisit_phase::BeforeScriptBlocked(1,45,earlyFlags.data(),earlyFlags.size()),"chapter-one orphanage exception remains exact");
    earlyFlags.fill(0);earlyFlags[20027/8]|=1u<<(20027%8);
    for(uint32_t id:{12u,13u,14u}) Check(!tracker::revisit_phase::BeforeScriptBlocked(4,id,earlyFlags.data(),earlyFlags.size()),
                                         "chapter-four three native exemptions remain exact");
    earlyFlags.fill(0);earlyFlags[22341/8]|=1u<<(22341%8);
    Check(!tracker::revisit_phase::BeforeScriptBlocked(6,160,earlyFlags.data(),earlyFlags.size()),"chapter-six internal exemption is not generalized");
    Check(tracker::revisit_phase::BeforeScriptBlocked(1,2,nullptr,4096)&&
          tracker::revisit_phase::BeforeScriptBlocked(1,2,earlyFlags.data(),4095),"missing flags cannot authorize a skipped script");

    // 用户实际前期进度：第一章17016成立、17017未完成，只有53由剧情217
    // 接管；卢安市1201000的返程必须可用，不能因整个地区共用mp2000而误灰。
    f.Reset();Put(f.save,0x11100+12*4,uint32_t{0x40000001});
    f.save[0x100+17016/8]|=1u<<(17016%8);f.Source(3,"mp2000");f.Rules(37);
    auto earlyContext=tracker::ReadRevisitNativeContext();
    Check(earlyContext.valid&&!earlyContext.beforeScriptReturnBlocked&&tracker::RevisitNativeTargetAvailable(37,earlyContext),
          "chapter-one Ruan city is usable while separate checkpoint pre-script is active");
    tracker::RevisitReturnPoint earlyAnchor{};
    Check(tracker::ReadRevisitNativeReturnPoint(earlyContext,earlyAnchor)&&
          tracker::RevisitNativeReturnPhaseAllowed(earlyAnchor,earlyContext),"city exact return uses concrete terrain identity");
    auto checkpointAnchor=earlyAnchor;checkpointAnchor.mapPlace=1208200;
    Check(!tracker::RevisitNativeReturnPhaseAllowed(checkpointAnchor,earlyContext),"same-scene checkpoint return cannot bypass point53 story");
    auto unknownAnchor=earlyAnchor;unknownAnchor.mapPlace=1999999;
    Check(tracker::RevisitNativeReturnPhaseAllowed(unknownAnchor,earlyContext)&&
          tracker::QueueRevisitNativeReturn(unknownAnchor,600,earlyContext)&&f.Close()&&f.MenuResult()==0&&loadCalls==0,
          "default phase permission cannot bypass final static terrain identity validation");
    f.Rules(53);earlyContext=tracker::ReadRevisitNativeContext();
    Check(!tracker::RevisitNativeTargetAvailable(53,earlyContext)&&earlyContext.nativeRuleStatus[53]==tracker::RuleBeforeScript,
          "chapter-one actual story target remains unavailable");
    Check(tracker::QueueRevisitNativeReturn(earlyAnchor,601,earlyContext)&&f.Close(),"ordinary early exact return closes through existing native lifecycle");
    f.Transfer();tracker::Sky2BeforeRevisitJump(Address(f.field),tracker::kRevisitReturnTarget,tracker::base+0x299674);
    Check(loadCalls==1&&std::strcmp(loadedPoint.scene,"mp2000")==0,"early exact return dispatches native loader with saved coordinates");

    // 本机真实前期存档位于mp2000道路：root1200000、mapPlace1208000没有Spot。
    // 只有53的正向分支活跃时允许记录/返程；不能把“没有Spot”当作剧情禁止。
    f.Reset();Put(f.save,0x11100+12*4,uint32_t{0x40000001});
    f.save[0x100+17016/8]|=1u<<(17016%8);f.Source(3,"mp2000");f.SourceTerrain(1200000,1208000);f.Rules(37);
    earlyContext=tracker::ReadRevisitNativeContext();
    Check(earlyContext.valid&&!earlyContext.beforeScriptReturnBlocked&&!earlyContext.beforeScriptBlocked[0]&&
          tracker::RevisitNativeTargetAvailable(37,earlyContext)&&!tracker::RevisitNativeTargetAvailable(0,earlyContext),
          "real unmapped road permits city travel while pure default ID zero never becomes destination");
    Check(tracker::ReadRevisitNativeReturnPoint(earlyContext,earlyAnchor)&&earlyAnchor.place==1200000&&earlyAnchor.mapPlace==1208000&&
          tracker::RevisitNativeReturnPhaseAllowed(earlyAnchor,earlyContext),"real unmapped road captures independently validated root and terrain identity");
    Check(tracker::QueueRevisitNativeReturn(earlyAnchor,603,earlyContext)&&f.Close(),"unmapped road return passes real static place validation");
    f.Transfer();tracker::Sky2BeforeRevisitJump(Address(f.field),tracker::kRevisitReturnTarget,tracker::base+0x299674);
    Check(loadCalls==1&&std::strcmp(loadedPoint.scene,"mp2000")==0,"unmapped road exact return uses unchanged native loading path");
    for(uint32_t flag:{17019u,17022u}) {
        f.Reset();Put(f.save,0x11100+12*4,uint32_t{0x40000001});
        f.save[0x100+flag/8]|=1u<<(flag%8);f.Source(3,"mp2000");f.SourceTerrain(1200000,1208000);f.Rules(37);
        earlyContext=tracker::ReadRevisitNativeContext();
        Check(earlyContext.beforeScriptReturnBlocked&&earlyContext.beforeScriptBlocked[0]&&
              !tracker::RevisitNativeReturnPhaseAllowed(earlyAnchor,earlyContext)&&
              !tracker::QueueRevisitNativeReturn(earlyAnchor,604+flag,earlyContext),
              "unmapped road cannot bypass global or not-equal exemption before-script branches");
    }

    // 章节2的62在目录里保留来源地区/地点，却落地到mp2000。即使返程点自身
    // place不能匹配62，也须识别这个跨地区别名并保守拒绝，不能按坐标猜边界。
    f.Reset();Put(f.save,0x11100+12*4,uint32_t{0x40000002});
    f.save[0x100+18029/8]|=1u<<(18029%8);f.Source(3,"mp2000");f.SourceTerrain(1200000,1208000);
    earlyContext=tracker::ReadRevisitNativeContext();
    earlyAnchor.chapter=2;
    Check(earlyContext.beforeScriptReturnBlocked&&!tracker::RevisitNativeReturnPhaseAllowed(earlyAnchor,earlyContext),
          "cross-region checkpoint alias still blocks unmapped road despite permissive default branch");
    f.Reset();Put(f.save,0x11100+12*4,uint32_t{0x40000001});
    f.save[0x100+17019/8]|=1u<<(17019%8);f.Source(3,"mp2000");f.Rules(37);
    earlyContext=tracker::ReadRevisitNativeContext();earlyAnchor.chapter=1;
    Check(earlyContext.beforeScriptReturnBlocked&&!tracker::RevisitNativeTargetAvailable(37,earlyContext)&&
          !tracker::QueueRevisitNativeReturn(earlyAnchor,602,earlyContext),"all-destination story interception also blocks custom coordinate return");

    // 早期正常身在序章/研究所不属于晚期回访。原生允许的已审核ID仍按普通
    // 规则开放，不能因其同时列在晚期补箱目录中就强制要求第8/9章完成位。
    f.Reset();Put(f.save,0x11100+12*4,uint32_t{0x40000000});f.Source(6,"mp6011");f.Rules(99);
    Check(tracker::ReadRevisitNativeContext().valid&&tracker::RevisitNativeTargetAvailable(99,tracker::ReadRevisitNativeContext()),
          "normal prologue target uses native rule before late revisit chapters");
    f.save[0x100+16021/8]&=static_cast<uint8_t>(~(1u<<(16021%8)));f.Rules(99);
    Check(!tracker::RevisitNativeTargetAvailable(99,tracker::ReadRevisitNativeContext()),"prologue tutorial interception still protects ordinary original target");
    f.Reset();Put(f.save,0x11100+12*4,uint32_t{0x40000006});f.Source(9,"mp6100_01");f.Rules(120);
    Check(tracker::ReadRevisitNativeContext().valid&&tracker::RevisitNativeTargetAvailable(120,tracker::ReadRevisitNativeContext()),
          "chapter-six research scene retains original native travel eligibility");

    // 1073会在第三章改变全部目标的前置许可，必须让已有规则证明和排队请求
    // 立即失效；不能因该位不在16000起的常规摘要范围而漏过最终消费者复核。
    f.Reset();Put(f.save,0x11100+12*4,uint32_t{0x40000003});f.Rules(2);
    const auto before1073=tracker::ReadRevisitNativeContext().progressSignature;
    f.save[0x100+1073/8]|=1u<<(1073%8);tracker::Sky2BeforeRevisitUpdate(Address(f.minimap));
    Check(tracker::ReadRevisitNativeContext().progressSignature!=before1073&&
          !tracker::RevisitNativeTargetAvailable(2,tracker::ReadRevisitNativeContext()),"out-of-range story bit invalidates previous native-rule proof");

    // 请求排队和关图不等于最终许可。即使原生容器仍保留刚才的允许状态，
    // 消费者也须重新捕获旗标，拒绝17016/17017或1073在关图期间的变化。
    for(uint32_t changedFlag:{17016u,17017u,1073u}) {
        f.Reset();const uint32_t chapter=changedFlag==1073?3u:1u;
        Put(f.save,0x11100+12*4,uint32_t{0x40000000u|chapter});
        const auto* destination=tracker::CatalogDestination(2,0);
        f.Destination(2,destination->region,destination->place,destination->scene);f.Rules(2);
        Check(tracker::QueueRevisitNativeTravel(2,610+changedFlag,tracker::ReadRevisitNativeContext())&&f.Close(),
              "ordinary early target prepares native result before story race");
        f.Transfer();f.save[0x100+changedFlag/8]^=1u<<(changedFlag%8);
        Check(tracker::Sky2BeforeRevisitJump(Address(f.field),2,tracker::base+0x299674)&&f.ResultKind()==0&&
              tracker::ReadRevisitNativeStatus().phase==tracker::RevisitNativePhase::Rejected,
              "final ordinary consumer rejects changed early story condition");
    }
    f.Reset();Put(f.save,0x11100+12*4,uint32_t{0x40000001});f.Source(3,"mp2000");f.Rules(37);
    earlyContext=tracker::ReadRevisitNativeContext();
    tracker::ReadRevisitNativeReturnPoint(earlyContext,earlyAnchor);
    Check(tracker::QueueRevisitNativeReturn(earlyAnchor,620,earlyContext)&&f.Close(),"early exact return prepares native result before phase race");
    f.Transfer();f.save[0x100+17019/8]|=1u<<(17019%8);
    Check(tracker::Sky2BeforeRevisitJump(Address(f.field),tracker::kRevisitReturnTarget,tracker::base+0x299674)&&
          loadCalls==0&&f.ResultKind()==0,"custom return consumer rechecks newly active global story interception");
    for(uint32_t id:{35u,36u,92u,93u,94u,95u,96u,150u}) {
        f.Reset();Put(f.save,0x11100+12*4,uint32_t{0x40000001});f.Rules(id);
        Check(!tracker::RevisitNativeTargetAvailable(id,tracker::ReadRevisitNativeContext()),
              "unreviewed no-display special entrance stays unavailable in early chapters");
    }
    for(uint32_t id:{99u,120u}) {
        f.Reset();Put(f.save,0x11100+12*4,uint32_t{0x40000001});f.Rules(id);
        f.ruleSpots[0].registered=0;tracker::Sky2BeforeRevisitUpdate(Address(f.minimap));
        Check(!tracker::RevisitNativeTargetAvailable(id,tracker::ReadRevisitNativeContext()),
              "reviewed special original target still requires current native registration");
    }

    // 规则观察证明必须来自原生MapJumpState完成后的桥，已有数组本身不是许可。
    f.Reset();f.Rules(2,false);
    Check(!tracker::RevisitNativeTargetAvailable(2,tracker::ReadRevisitNativeContext()),
          "unobserved manager arrays cannot authorize ordinary destinations");
    f.Rules(2);
    Check(tracker::RevisitNativeTargetAvailable(2,tracker::ReadRevisitNativeContext())&&f.ruleSpots[0].visible==0,
          "registered unblocked ordinary point is allowed even if not yet visited");
    f.ruleSpots[0].registered=0;tracker::Sky2BeforeRevisitUpdate(Address(f.minimap));
    Check(!tracker::RevisitNativeTargetAvailable(2,tracker::ReadRevisitNativeContext()),"unregistered point remains closed");
    f.ruleSpots[0].registered=1;f.ruleSpots[0].blocked=1;tracker::Sky2BeforeRevisitUpdate(Address(f.minimap));
    Check(!tracker::RevisitNativeTargetAvailable(2,tracker::ReadRevisitNativeContext()),"native story blocked point remains closed");
    f.ruleSpots[0].blocked=0;f.ruleAreas[0].blocked=1;tracker::Sky2BeforeRevisitUpdate(Address(f.minimap));
    Check(!tracker::RevisitNativeTargetAvailable(2,tracker::ReadRevisitNativeContext()),"blocked parent area remains closed");
    f.ruleAreas[0].blocked=0;f.save[0x100+22040/8]|=1u<<(22040%8);tracker::Sky2BeforeRevisitUpdate(Address(f.minimap));
    Check(!tracker::RevisitNativeTargetAvailable(2,tracker::ReadRevisitNativeContext()),"story change invalidates prior rule proof until native script refreshes");
    f.Rules(2);
    Check(tracker::RevisitNativeTargetAvailable(2,tracker::ReadRevisitNativeContext()),"fresh native observation restores current rule proof");
    f.ruleSpots[1]=f.ruleSpots[0];Put(f.minimap,0xE8,uint64_t{2});tracker::Sky2BeforeRevisitUpdate(Address(f.minimap));
    Check(!tracker::RevisitNativeTargetAvailable(2,tracker::ReadRevisitNativeContext()),"duplicate runtime IDs invalidate the rule snapshot");

    f.Reset();f.Rules(55);
    Check(!tracker::RevisitNativeTargetAvailable(55,tracker::ReadRevisitNativeContext())&&
          tracker::ReadRevisitNativeContext().nativeRuleStatus[55]==tracker::RuleBeforeScript,
          "chapter-eight special before-script 55 is not bypassed");
    f.save[0x100+24020/8]|=1u<<(24020%8);f.Rules(55);
    Check(tracker::RevisitNativeTargetAvailable(55,tracker::ReadRevisitNativeContext()),"finished special before-script permits ordinary native rule");
    f.Reset();f.Rules(131);
    Check(!tracker::RevisitNativeTargetAvailable(131,tracker::ReadRevisitNativeContext()),"unfinished chapter-eight point 131 before-script stays closed");
    Put(f.save,0x11100+12*4,uint32_t{0x40000009});f.Rules(131);
    Check(tracker::RevisitNativeTargetAvailable(131,tracker::ReadRevisitNativeContext()),"chapter-nine empty before-script does not inherit chapter-eight restriction");
    f.Reset();f.Rules(165);
    Check(!tracker::RevisitNativeTargetAvailable(165,tracker::ReadRevisitNativeContext()),"ordinary registry cannot bypass reviewed Glorious story gates");
    f.Reset();f.Rules(15);
    Check(tracker::RevisitNativeTargetAvailable(15,tracker::ReadRevisitNativeContext()),"ordinary Bose travel uses native rule outside legacy emergency case");
    f.Source(6,"mp6011");f.Rules(15);f.ruleSpots[0].blocked=1;
    tracker::Sky2BeforeRevisitUpdate(Address(f.minimap));
    const auto legacyContext=tracker::ReadRevisitNativeContext();
    Check(tracker::RevisitNativeTargetAvailable(15,legacyContext)&&
          !tracker::RevisitNativeOrdinaryTargetAvailable(15,legacyContext)&&
          std::strlen(tracker::RevisitNativeTargetReason(15,legacyContext))>0,
          "legacy candidate still explains why ordinary Bose is blocked when session rejects emergency use");

    // 对所有普通静态类别走同一规则，不继续为每个城市或道路添加硬编码白名单。
    for(const auto& row:tracker::kTravelCatalog) {
        if(row.variant || tracker::FindDestination(row.id)) continue;
        f.Reset();Put(f.save,0x11100+12*4,uint32_t{0x40000009});f.Rules(row.id);
        const bool expected=row.kind==0 && !(row.flags&8) && row.scene && row.scene[0];
        Check(tracker::RevisitNativeTargetAvailable(row.id,tracker::ReadRevisitNativeContext())==expected,
              "ordinary native catalog eligibility follows structural category and native rules");
    }

    // 126菜单显示在洛连特地区，但实际目标是柏斯mp1000；到达不能沿用row.region。
    f.Reset();Put(f.save,0x11100+12*4,uint32_t{0x40000009});f.Source(1,"mp0000");
    const auto* cross=tracker::CatalogDestination(126,0);
    Check(cross&&cross->region==1&&std::strcmp(cross->scene,"mp1000")==0,"cross-region fixture matches real catalog");
    f.Destination(126,cross->region,cross->place,cross->scene);Put(f.placeRows,0x1F8+0x98,uint32_t{2});f.Rules(126);
    Check(tracker::QueueRevisitNativeTravel(126,301,tracker::ReadRevisitNativeContext())&&f.Close(),"ordinary cross-region point queues through native map close");
    f.Transfer();
    Check(!tracker::Sky2BeforeRevisitJump(Address(f.field),126,tracker::base+0x299674)&&tracker::arrivalPoint.region==2,
          "cross-region arrival uses destination t_place rather than menu display region");
    f.Source(2,"mp1000");for(int i=0;i<3;++i) tracker::Sky2BeforeRevisitUpdate(Address(f.minimap));
    Check(tracker::ReadRevisitNativeStatus().phase==tracker::RevisitNativePhase::Arrived,"ordinary cross-region arrival is confirmed");

    f.Reset();const auto* normal=tracker::CatalogDestination(2,0);f.Destination(2,normal->region,normal->place,normal->scene);f.Rules(2);
    tracker::QueueRevisitNativeTravel(2,302,tracker::ReadRevisitNativeContext());f.Close();f.Transfer();f.ruleSpots[0].blocked=1;
    Check(tracker::Sky2BeforeRevisitJump(Address(f.field),2,tracker::base+0x299674)&&f.ResultKind()==0,
          "final consumer rechecks native story block after map close");
    f.Reset();f.Destination(2,normal->region,normal->place,normal->scene);f.Rules(2);Put(f.row,0x58,uint8_t{0x80});
    tracker::QueueRevisitNativeTravel(2,303,tracker::ReadRevisitNativeContext());
    Check(f.Close()&&f.MenuResult()==0,"runtime flags changed from generated catalog fail final destination validation");
#endif
    // 迷途之森没有原生Spot行：必须以唯一t_place身份和提前安装的事件保护进入。
    // 以下仍只调用本测试进程内的生产helper与FakeNativeLoad，绝不读取游戏。
    f.Reset();f.ForestTable();
    auto forestContext=tracker::ReadRevisitNativeContext();
    Check(forestContext.forestStoryComplete&&tracker::RevisitNativeTargetAvailable(tracker::forest::kTarget,forestContext),
          "completed chapter-eight forest with guard ready is available");
    Check(tracker::QueueRevisitNativeTravel(tracker::forest::kTarget,401,forestContext)&&f.Close(),
          "custom forest target closes native map without a native spot row");
    f.Transfer();
    Check(tracker::Sky2BeforeRevisitJump(Address(f.field),tracker::forest::kTarget,tracker::base+0x299674)&&
          loadCalls==1&&loadedFlags==0x4001&&std::strcmp(loadedPoint.scene,tracker::forest::kScene)==0&&
          loadedPoint.xyz[0]==0&&loadedPoint.xyz[1]==0&&loadedPoint.xyz[2]==0&&loadedPoint.yawRadians==0,
          "forest sentinel dispatches native full loader at verified original free-action spawn");
    Check(f.ResultKind()==0&&tracker::ReadRevisitNativeStatus().phase==tracker::RevisitNativePhase::Dispatched,
          "forest sentinel is consumed but native handoff is not yet arrival");
    const auto forestGoal=tracker::arrivalPoint;
    f.Arrive(forestGoal);tracker::Sky2BeforeRevisitUpdate(Address(f.minimap));tracker::Sky2BeforeRevisitUpdate(Address(f.minimap));
    Check(tracker::ReadRevisitNativeStatus().phase==tracker::RevisitNativePhase::Arrived,
          "forest arrival requires stable exact origin and facing");
    const auto forestSource=tracker::ReadRevisitNativeContext();
    Check(forestSource.valid&&forestSource.region==1&&forestSource.place==1008100&&forestSource.mapPlace==1008100&&
          forestSource.sceneRegion==0&&forestSource.nativeRegion==0,
          "unique raw-zero forest identity normalizes only after both live records match static table");

    f.Reset();f.ForestTable();testForestGuardReady=false;
    Check(!tracker::QueueRevisitNativeTravel(tracker::forest::kTarget,402,tracker::ReadRevisitNativeContext()),
          "forest entry without installed guard is rejected before queueing");
    for (uint32_t flag:{20064u,20067u}) {
        f.Reset();f.ForestTable();f.save[0x100+flag/8]&=static_cast<unsigned char>(~(1u<<(flag%8)));
        tracker::Sky2BeforeRevisitUpdate(Address(f.minimap));
        Check(!tracker::QueueRevisitNativeTravel(tracker::forest::kTarget,403,tracker::ReadRevisitNativeContext()),
              "either missing forest completion bit blocks entry");
    }
    for (uint32_t chapter:{7u,10u}) {
        f.Reset();f.ForestTable();Put(f.save,0x11100+12*4,0x40000000u|chapter);
        tracker::Sky2BeforeRevisitUpdate(Address(f.minimap));
        Check(!tracker::RevisitNativeTargetAvailable(tracker::forest::kTarget,tracker::ReadRevisitNativeContext()),
              "forest destination is unavailable outside reviewed chapters");
    }
    for (unsigned corrupt=0;corrupt<3;++corrupt) {
        f.Reset();f.ForestTable();
        if(corrupt==0) Put(f.placeRows,0x1F8,tracker::forest::kPlace+1);
        if(corrupt==1) Put(f.placeRows,0x1F8+0x98,uint32_t{1});
        if(corrupt==2) Put(f.placeRows,0x1F8+0x90,uint8_t{1});
        Check(tracker::QueueRevisitNativeTravel(tracker::forest::kTarget,404,tracker::ReadRevisitNativeContext())&&
              f.Close()&&f.MenuResult()==0&&loadCalls==0,
              "unknown forest place region or variant refuses custom load before map close");
    }
    f.Reset();f.ForestTable();tracker::QueueRevisitNativeTravel(tracker::forest::kTarget,405,tracker::ReadRevisitNativeContext());
    f.Close();f.Transfer();testForestGuardReady=false;
    Check(tracker::Sky2BeforeRevisitJump(Address(f.field),tracker::forest::kTarget,tracker::base+0x299674)&&
          loadCalls==0&&f.ResultKind()==0,"forest guard lost after closing cannot reach loader");
    f.Reset();f.ForestTable();tracker::QueueRevisitNativeTravel(tracker::forest::kTarget,406,tracker::ReadRevisitNativeContext());
    f.Close();f.Transfer();tracker::authorize.store(&DisableForestGuardAuthorize);
    Check(tracker::Sky2BeforeRevisitJump(Address(f.field),tracker::forest::kTarget,tracker::base+0x299674)&&loadCalls==0,
          "forest guard is rechecked after final authorization callback");
    f.Reset();f.ForestTable();tracker::QueueRevisitNativeTravel(tracker::forest::kTarget,407,tracker::ReadRevisitNativeContext());
    f.Close();f.Transfer();tracker::authorize.store(&CancelAuthorize);
    Check(tracker::Sky2BeforeRevisitJump(Address(f.field),tracker::forest::kTarget,tracker::base+0x299674)&&loadCalls==0,
          "cancellation during custom forest authorization cannot be revived");
    f.Reset();f.ForestTable();tracker::QueueRevisitNativeTravel(tracker::forest::kTarget,408,tracker::ReadRevisitNativeContext());
    f.Close();f.Transfer();tracker::requestedAt=GetTickCount64()-6000;
    Check(tracker::Sky2BeforeRevisitJump(Address(f.field),tracker::forest::kTarget,tracker::base+0x299674)&&loadCalls==0,
          "expired forest handoff never executes load");

    // 原地回访仍走普通完整加载生命周期；记录指向森林时也不能绕过保护门槛。
    f.Reset();f.ForestTable();f.Source(0,tracker::forest::kScene);
    tracker::QueueRevisitNativeTravel(tracker::forest::kTarget,409,tracker::ReadRevisitNativeContext());f.Close();f.Transfer();
    tracker::Sky2BeforeRevisitJump(Address(f.field),tracker::forest::kTarget,tracker::base+0x299674);
    Check(loadCalls==1&&loadedFlags==0x4021,"same-forest custom load retains native same-scene flag");
    f.Reset();f.ForestTable();f.Source(0,tracker::forest::kScene);
    Check(tracker::ReadRevisitNativeReturnPoint(tracker::ReadRevisitNativeContext(),anchor)&&anchor.region==1,
          "exact position capture validates raw-zero forest static identity");
    testForestGuardReady=false;
    tracker::QueueRevisitNativeReturn(anchor,410,tracker::ReadRevisitNativeContext());f.Close();
    Check(f.MenuResult()==0&&loadCalls==0,"return record pointing into forest cannot bypass unavailable protection");

    // 可选保护失效时允许玩家从已加载的森林离开，不能把无自然出口场景锁死。
    f.Reset();tracker::ReadRevisitNativeReturnPoint(tracker::ReadRevisitNativeContext(),anchor);
    f.ForestTable();f.Source(0,tracker::forest::kScene);f.ReturnTable(anchor);testForestGuardReady=false;
    Check(tracker::QueueRevisitNativeReturn(anchor,411,tracker::ReadRevisitNativeContext())&&f.Close(),
          "forest source permits escape to saved ordinary scene even when optional guard is unavailable");
    f.Transfer();tracker::Sky2BeforeRevisitJump(Address(f.field),tracker::kRevisitReturnTarget,tracker::base+0x299674);
    Check(loadCalls==1&&std::strcmp(loadedPoint.scene,anchor.scene)==0,"forest emergency escape uses unchanged saved origin");
    f.Reset();f.ForestTable();f.Source(0,tracker::forest::kScene);
    Put(f.scene,0x98,uint32_t{1});Put(f.sceneRoot,0x808+0x98,uint32_t{1});Put(f.placeRows,0x98,uint32_t{1});
    tracker::Sky2BeforeRevisitUpdate(Address(f.minimap));
    Check(!tracker::ReadRevisitNativeContext().valid,"forest raw nonzero region never gains validity by matching fabricated live table");
    f.Reset();f.ForestTable();f.Source(0,tracker::forest::kScene);
    Put(f.scene,0,tracker::forest::kPlace+1);Put(f.sceneRoot,0x808,tracker::forest::kPlace+1);
    Put(f.placeRows,0,tracker::forest::kPlace+1);tracker::Sky2BeforeRevisitUpdate(Address(f.minimap));
    Check(!tracker::ReadRevisitNativeContext().valid,"forest scene name alone cannot authorize a different place identity");
    std::printf("revisit_native_runtime_tests: %u checks, %u failures\n",checks,failures);
    return failures?1:0;
#endif
}
