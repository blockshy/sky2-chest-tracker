// 文件格式必须拒绝部分写入、旧构建、损坏与不合法坐标；不读取任何真实游戏数据。
#include "revisit_journal.h"
#include <cstdio>
#include <limits>
using namespace tracker;
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
    RevisitReturnRecord decoded{};
    check(DecodeRevisitRecord(bytes.data(),bytes.size(),decoded),"valid record decodes");
    check(decoded.point.xyz[0]==12.5f && decoded.point.xyz[2]==-988.875f &&
        decoded.point.yawRadians==3.14f && decoded.ticket==record.ticket &&
        decoded.point.progressSignature==record.point.progressSignature,"coordinates and identity values remain exact");
    // 放宽章节范围不能改变磁盘格式：早期记录使用相同字段编码，原第8/9章字节
    // 仍应完全兼容，避免升级后误把已有返程文件视为损坏或丢失恢复候选。
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
