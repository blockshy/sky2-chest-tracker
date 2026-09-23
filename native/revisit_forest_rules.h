// 迷途之森专用回访的静态身份与完成门槛；所有运行时入口共用，避免界面和保护不一致。
#pragma once
#include "revisit_policy.h"
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <string_view>

namespace tracker::forest {
// 此哨兵从不交给原生t_mapjump查询，必须由Mod拥有的最终换图消费者处理。
inline constexpr uint32_t kTarget=0xFFFFFFFDu;
inline constexpr char kScene[]="mp0081";
inline constexpr uint32_t kPlace=1008100u;
inline constexpr uint32_t kRegion=1u;
inline constexpr uint32_t kVariant=0u;
inline constexpr uintptr_t kEnableBuiltinRva=0x49E200u;
inline constexpr uint32_t kIntegerTag=0x40000000u;

// 精确匹配已经逐个审核的机关名称。禁止用“EV_”前缀或数字转换后的宽松匹配，
// 避免误伤其它剧情、宝箱、以后新增的机关以及相似名称的普通出入口。
inline bool ProtectedEventBox(std::string_view name) noexcept {
    if (name=="EV_Start" || name=="EV_04_34_00" || name=="EV_04_34_01") return true;
    constexpr std::string_view wrong="EV_WrongWay_", hint="EV_Hint_";
    auto numbered=[](std::string_view value,std::string_view prefix,
                     unsigned first,unsigned last) noexcept {
        if (value.size()!=prefix.size()+2 || value.substr(0,prefix.size())!=prefix) return false;
        const char a=value[prefix.size()], b=value[prefix.size()+1];
        if (a<'0' || a>'9' || b<'0' || b>'9') return false;
        const unsigned number=static_cast<unsigned>((a-'0')*10+b-'0');
        return number>=first && number<=last;
    };
    return numbered(name,wrong,0,11) || numbered(name,hint,1,25);
}

// 完成送入迷途之森及结界收尾，且已进入第8/9章；不要求玩家经历失败分支，
// 也不修改用于迷路计数、提示顺序或BP奖励的global_work值。
inline bool StoryComplete(uint32_t chapter,const uint8_t* flags,size_t size) noexcept {
    if ((chapter!=8 && chapter!=9) || !flags || size!=4096) return false;
    for (const uint32_t flag : {20064u,20067u})
        if ((flags[flag/8]&(1u<<(flag%8)))==0) return false;
    return true;
}

// 未完成的森林谜题只能在明确的实验传送行程中抑制；普通第四章流程和仅有历史记录
// 的读档均沿用原逻辑。章节、完整旗标缓冲区仍属技术校验，实验模式不能忽略。
inline bool ProtectionAllowed(uint32_t chapter,const uint8_t* flags,size_t size,
                              bool experimentalTrip=false) noexcept {
    if (chapter>9 || !flags || size!=4096) return false;
    return StoryComplete(chapter,flags,size) ||
        (revisit_policy::kUnrestricted && experimentalTrip);
}

// 两种原始地点记录都必须由调用者逐项核对表身份后才能采用展示地区1。
// 这里只接纳唯一已审核的region0场景，绝不能把任意地区0的场景放行。
inline bool MatchesPlace(uint32_t place,uint32_t region,uint32_t variant,std::string_view scene) noexcept {
    return place==kPlace && region==0 && variant==kVariant && scene==kScene;
}
}
