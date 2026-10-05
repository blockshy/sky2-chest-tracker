// 返程文件的稳定编码；不依赖 Windows 或游戏资源，公开测试可直接验证损坏边界。
#include "revisit_journal.h"
#include "game_version.h"
#include <cstring>

namespace tracker {
namespace {
constexpr uint8_t kMagic[8] = {'S','K','Y','2','R','E','T','1'};
// 持久化记录必须与实际运行的原生适配版本绑定，不能另维护一份旧版字节数组。
// 编译期严格解析统一的 SHA-256：配置长度或字符格式错误直接使构建失败，
// 不允许把非法十六进制字符静默转成 0，也不把历史迁移白名单用于执行返程。
constexpr uint8_t HashDigit(char value) noexcept {
    return value >= '0' && value <= '9' ? static_cast<uint8_t>(value - '0') :
        value >= 'a' && value <= 'f' ? static_cast<uint8_t>(value - 'a' + 10) : uint8_t{0xFF};
}
constexpr bool ValidGameHash() noexcept {
    if (sizeof(sky2::game_version::kSupportedExeSha256) != 65) return false;
    for (size_t i = 0; i < 64; ++i)
        if (HashDigit(sky2::game_version::kSupportedExeSha256[i]) == 0xFF) return false;
    return sky2::game_version::kSupportedExeSha256[64] == '\0';
}
static_assert(ValidGameHash(), "Return journal requires the audited SHA-256 in lowercase hexadecimal.");
constexpr std::array<uint8_t, 32> GameHashBytes() noexcept {
    std::array<uint8_t, 32> hash{};
    for (size_t i = 0; i < hash.size(); ++i)
        hash[i] = static_cast<uint8_t>((HashDigit(sky2::game_version::kSupportedExeSha256[i * 2]) << 4) |
            HashDigit(sky2::game_version::kSupportedExeSha256[i * 2 + 1]));
    return hash;
}
constexpr auto kGameHash = GameHashBytes();
void Put32(uint8_t* bytes, uint32_t value) noexcept {
    for (unsigned i=0; i<4; ++i) bytes[i]=static_cast<uint8_t>(value>>(i*8));
}
uint32_t Get32(const uint8_t* bytes) noexcept {
    uint32_t value=0; for (unsigned i=0; i<4; ++i) value|=uint32_t(bytes[i])<<(i*8); return value;
}
void Put64(uint8_t* bytes, uint64_t value) noexcept { Put32(bytes,uint32_t(value)); Put32(bytes+4,uint32_t(value>>32)); }
uint64_t Get64(const uint8_t* bytes) noexcept { return Get32(bytes)|(uint64_t(Get32(bytes+4))<<32); }
void PutFloat(uint8_t* bytes, float value) noexcept {
    static_assert(sizeof(float)==4); uint32_t bits=0; std::memcpy(&bits,&value,4); Put32(bytes,bits);
}
float GetFloat(const uint8_t* bytes) noexcept { const auto bits=Get32(bytes); float value=0; std::memcpy(&value,&bits,4); return value; }
uint32_t Checksum(const uint8_t* bytes, size_t size) noexcept {
    uint32_t crc=0xFFFFFFFFu;
    for (size_t i=0; i<size; ++i) {
        crc^=bytes[i];
        for (unsigned bit=0; bit<8; ++bit) crc=(crc>>1)^((0u-(crc&1u))&0xEDB88320u);
    }
    return ~crc;
}
}
bool EncodeRevisitRecord(const RevisitReturnRecord& record, RevisitRecordBytes& bytes) noexcept {
    if (!ValidRevisitReturnPoint(record.point) || !record.ticket ||
        record.createdUnixSeconds<1577836800ull || record.createdUnixSeconds>4102444800ull) return false;
    bytes.fill(0);
    std::memcpy(bytes.data(),kMagic,8); Put32(bytes.data()+8,1); Put32(bytes.data()+12,kRevisitRecordBytes);
    std::memcpy(bytes.data()+16,kGameHash.data(),kGameHash.size());
    Put64(bytes.data()+48,record.createdUnixSeconds); Put64(bytes.data()+56,record.ticket);
    std::memcpy(bytes.data()+64,record.point.scene,std::strlen(record.point.scene));
    Put32(bytes.data()+96,record.point.region); Put32(bytes.data()+100,record.point.chapter);
    Put32(bytes.data()+104,record.point.place); Put32(bytes.data()+108,record.point.mapPlace);
    Put32(bytes.data()+112,record.point.variant);
    for (unsigned i=0; i<3; ++i) PutFloat(bytes.data()+116+i*4,record.point.xyz[i]);
    PutFloat(bytes.data()+128,record.point.yawRadians); Put64(bytes.data()+132,record.point.progressSignature);
    Put32(bytes.data()+140,Checksum(bytes.data(),140)); return true;
}
bool DecodeRevisitRecord(const uint8_t* bytes, size_t size, RevisitReturnRecord& record) noexcept {
    if (!bytes || size!=kRevisitRecordBytes || std::memcmp(bytes,kMagic,8) || Get32(bytes+8)!=1 ||
        Get32(bytes+12)!=kRevisitRecordBytes || std::memcmp(bytes+16,kGameHash.data(),kGameHash.size()) ||
        Get32(bytes+140)!=Checksum(bytes,140)) return false;
    RevisitReturnRecord decoded{};
    decoded.createdUnixSeconds=Get64(bytes+48); decoded.ticket=Get64(bytes+56);
    std::memcpy(decoded.point.scene,bytes+64,32);
    decoded.point.region=Get32(bytes+96); decoded.point.chapter=Get32(bytes+100);
    decoded.point.place=Get32(bytes+104); decoded.point.mapPlace=Get32(bytes+108); decoded.point.variant=Get32(bytes+112);
    for (unsigned i=0; i<3; ++i) decoded.point.xyz[i]=GetFloat(bytes+116+i*4);
    decoded.point.yawRadians=GetFloat(bytes+128); decoded.point.progressSignature=Get64(bytes+132);
    // 再次编码要求所有保留字节和字符串终止后的空间保持规范，避免多种歧义格式。
    RevisitRecordBytes canonical{};
    if (!EncodeRevisitRecord(decoded,canonical) || std::memcmp(bytes,canonical.data(),size)) return false;
    record=decoded; return true;
}
}
