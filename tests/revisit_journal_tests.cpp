// 文件格式必须拒绝部分写入、旧构建、损坏与不合法坐标；不读取任何真实游戏数据。
#include "revisit_journal.h"
#include "game_version.h"
#include <cstdio>
#include <cstdlib>
#include <limits>
using namespace tracker;
namespace {
// 独立计算合成样本的 CRC，确保拒绝旧构建确实来自 EXE 指纹，而非碰巧损坏的校验码。
void RefreshFixtureChecksum(RevisitRecordBytes& bytes) noexcept {
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < bytes.size() - 4; ++i) {
        crc ^= bytes[i];
        for (unsigned bit = 0; bit < 8; ++bit)
            crc = (crc & 1) ? (crc >> 1) ^ 0xEDB88320u : crc >> 1;
    }
    crc ^= 0xFFFFFFFFu;
    for (unsigned i = 0; i < 4; ++i) bytes[140 + i] = static_cast<uint8_t>(crc >> (i * 8));
}
}
int main() {
    unsigned failures=0;
    const auto check=[&](bool ok,const char* name) { if (!ok) { ++failures; std::printf("FAIL %s\n",name); } };
    RevisitReturnRecord record{};
    std::memcpy(record.point.scene,"mp5600_03",sizeof("mp5600_03"));
    record.point.region=7; record.point.chapter=9; record.point.place=1560003;
    record.point.mapPlace=1560003; record.point.xyz[0]=12.5f; record.point.xyz[1]=-1.25f;
    record.point.xyz[2]=-988.875f; record.point.yawRadians=3.14f; record.point.progressSignature=0x1234567890ABCDEF;
    record.createdUnixSeconds=1780000000; record.ticket=0x987654321FEDCBA;
    RevisitRecordBytes bytes{};
    check(EncodeRevisitRecord(record,bytes),"valid record encodes");
    // 用格式化输出独立还原磁盘指纹，避免测试复用生产解析器而共同保留旧版本常量。
    char encodedHash[65]{};
    for (size_t i = 0; i < 32; ++i) std::snprintf(encodedHash + i * 2, 3, "%02x", bytes[16 + i]);
    check(std::strcmp(encodedHash, sky2::game_version::kSupportedExeSha256) == 0,
          "new record contains the currently audited executable hash");
    RevisitReturnRecord decoded{};
    check(DecodeRevisitRecord(bytes.data(),bytes.size(),decoded),"valid record decodes");
    check(decoded.point.xyz[0]==12.5f && decoded.point.xyz[2]==-988.875f &&
        decoded.point.yawRadians==3.14f && decoded.ticket==record.ticket &&
        decoded.point.progressSignature==record.point.progressSignature,"coordinates and identity values remain exact");
    auto previousBuild = bytes;
    for (size_t i = 0; i < 32; ++i) {
        const char hex[] = {sky2::game_version::kPreviousExeSha256[i * 2],
                            sky2::game_version::kPreviousExeSha256[i * 2 + 1], '\0'};
        previousBuild[16 + i] = static_cast<uint8_t>(std::strtoul(hex, nullptr, 16));
    }
    RefreshFixtureChecksum(previousBuild);
    const auto preservedPreviousBuild = previousBuild;
    decoded.ticket = 0x1234;
    check(!DecodeRevisitRecord(previousBuild.data(), previousBuild.size(), decoded),
          "previous executable hash is rejected even with a valid CRC");
    check(previousBuild == preservedPreviousBuild && decoded.ticket == 0x1234,
          "rejecting a historical record changes neither its bytes nor the output record");
    // 同一受支持游戏构建内，早期记录和第 8/9 章使用相同字段格式；这不表示
    // 不同 EXE 构建的历史记录可以执行，跨构建指纹拒绝已在上面单独验证。
    const auto existingLateChapterBytes=bytes;
    for (uint32_t chapter=0;chapter<=9;++chapter) {
        record.point.chapter=chapter;
        check(EncodeRevisitRecord(record,bytes),"all actual chapters encode without a format change");
        check(DecodeRevisitRecord(bytes.data(),bytes.size(),decoded) && decoded.point.chapter==chapter,
            "all actual chapters round trip exactly");
    }
    check(bytes==existingLateChapterBytes,"existing final-chapter record bytes stay unchanged");
    for (uint32_t chapter : {10u,0x40000000u,0xFFFFFFFFu}) {
        record.point.chapter=chapter;
        check(!ValidRevisitReturnPoint(record.point) && !EncodeRevisitRecord(record,bytes),
            "unknown or still-tagged chapter cannot enter persistent return history");
    }
    record.point.chapter=9;
    check(EncodeRevisitRecord(record,bytes),"restore valid record before corruption checks");
    for (size_t i=0;i<bytes.size();++i) {
        auto corrupted=bytes; corrupted[i]^=0x01;
        check(!DecodeRevisitRecord(corrupted.data(),corrupted.size(),decoded),"every single byte corruption rejected");
    }
    for (size_t size=0;size<bytes.size();++size)
        check(!DecodeRevisitRecord(bytes.data(),size,decoded),"every truncation rejected");
    check(!DecodeRevisitRecord(nullptr,bytes.size(),decoded),"null input rejected");
    record.point.xyz[1]=std::numeric_limits<float>::quiet_NaN();
    check(!EncodeRevisitRecord(record,bytes),"NaN coordinate rejected");
    record.point.xyz[1]=std::numeric_limits<float>::infinity();
    check(!EncodeRevisitRecord(record,bytes),"infinite coordinate rejected");
    record.point.xyz[1]=0; record.point.yawRadians=360;
    check(!EncodeRevisitRecord(record,bytes),"degrees cannot masquerade as radians");
    record.point.yawRadians=0; std::memcpy(record.point.scene,"../../savedata",sizeof("../../savedata"));
    check(!EncodeRevisitRecord(record,bytes),"non-scene input rejected");
    std::printf("%u revisit journal failure(s)\n",failures);
    return failures?1:0;
}
