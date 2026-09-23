// 返程记录是 Mod 自己的本机文件，不修改游戏保存数据。加载后仅展示可供确认的记录，
// 不自动传送；相同剧情摘要也不代表记录一定属于当前加载的存档。
#pragma once
#include "revisit_return_point.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace tracker {
struct RevisitReturnRecord {
    RevisitReturnPoint point{};
    uint64_t createdUnixSeconds = 0;
    uint64_t ticket = 0;
};
inline constexpr size_t kRevisitRecordBytes = 144;
using RevisitRecordBytes = std::array<uint8_t, kRevisitRecordBytes>;

// 固定长度、明确小端编码、完整游戏构建标识和 CRC 用于发现旧格式/截断/损坏。
// CRC 不是安全签名：最终还必须用当前原生地图表验证场景、地区与变体是否合法。
bool EncodeRevisitRecord(const RevisitReturnRecord& record, RevisitRecordBytes& bytes) noexcept;
bool DecodeRevisitRecord(const uint8_t* bytes, size_t size, RevisitReturnRecord& record) noexcept;

enum class RevisitRecordRead { Missing, Valid, Invalid, IoError };
// 所有写入使用同目录独立临时文件，FlushFileBuffers 成功后原子替换。
// 发现已有未知/损坏记录时拒绝覆盖；清除/归档也不由此接口执行。
RevisitRecordRead ReadRevisitRecordFile(const std::wstring& folder, RevisitReturnRecord& record) noexcept;
bool WriteRevisitRecordFile(const std::wstring& folder, const RevisitReturnRecord& record) noexcept;
// 历次出发点分别保存为不可覆盖的记录，避免开始另一趟回访后丢失旧自动存档的返程点。
// 读取时只接受本模块的固定文件名和完整有效格式；返回列表按记录时间从新到旧排列。
bool ReadRevisitRecordHistory(const std::wstring& folder,
                              std::vector<RevisitReturnRecord>& records) noexcept;
}
