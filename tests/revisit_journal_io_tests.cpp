// 在独立临时目录验证真实Windows文件I/O；测试不接触游戏目录或用户保存数据。
#include "revisit_journal.h"
#include "game_version.h"
#include <Windows.h>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <filesystem>
namespace fs=std::filesystem;
using namespace tracker;
namespace {
// 构造 CRC 正确的历史容器，避免“旧文件不生效”仅仅因为任意字节损坏。
RevisitRecordBytes PreviousBuildFixture(const RevisitRecordBytes& current) noexcept {
    auto previous=current;
    for (size_t i=0;i<32;++i) {
        const char hex[]={sky2::game_version::kPreviousExeSha256[i*2],
                          sky2::game_version::kPreviousExeSha256[i*2+1],'\0'};
        previous[16+i]=static_cast<uint8_t>(std::strtoul(hex,nullptr,16));
    }
    uint32_t crc=0xFFFFFFFFu;
    for (size_t i=0;i<140;++i) {
        crc^=previous[i];
        for (unsigned bit=0;bit<8;++bit) crc=(crc&1)?(crc>>1)^0xEDB88320u:crc>>1;
    }
    crc^=0xFFFFFFFFu;
    for (unsigned i=0;i<4;++i) previous[140+i]=static_cast<uint8_t>(crc>>(i*8));
    return previous;
}
}
int main() {
    unsigned failures=0;
    const auto check=[&](bool ok,const char* name) { if (!ok) { ++failures; std::printf("FAIL %s\n",name); } };
    const fs::path root=fs::current_path()/("journal-fixture-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(GetTickCount64()));
    if (!fs::create_directory(root)) return 2;
    const auto folder=root.wstring();
    RevisitReturnRecord a{};
    std::memcpy(a.point.scene,"mp5600_03",sizeof("mp5600_03"));
    a.point.region=7;a.point.chapter=9;a.point.place=a.point.mapPlace=1560003;
    a.point.xyz[0]=-528.5f;a.point.xyz[1]=-24;a.point.xyz[2]=-743.2f;
    a.point.yawRadians=5.74f;a.point.progressSignature=1;
    a.createdUnixSeconds=1780000000;a.ticket=1;
    // 模拟从旧版原目录升级：合法旧记录已经存在，新构建仍应获得可写的独立空间。
    RevisitRecordBytes encodedInitial{};
    check(EncodeRevisitRecord(a,encodedInitial),"initial fixture encodes");
    const auto legacyBytes=PreviousBuildFixture(encodedInitial);
    const auto legacyLatest=root/L"revisit-return.dat";
    const auto legacyHistory=root/L"revisit-history";
    check(fs::create_directory(legacyHistory),"legacy history fixture created");
    for (const auto& path:{legacyLatest,legacyHistory/L"0000000000000001.dat"}) {
        std::ofstream file(path,std::ios::binary|std::ios::trunc);
        file.write(reinterpret_cast<const char*>(legacyBytes.data()),legacyBytes.size());
    }
    RevisitReturnRecord read{}; std::vector<RevisitReturnRecord> history;
    check(ReadRevisitRecordFile(folder,read)==RevisitRecordRead::Missing,"legacy filename does not expose an old return point");
    check(ReadRevisitRecordHistory(folder,history) && history.empty(),"legacy history does not block empty current storage");
    check(WriteRevisitRecordFile(folder,a),"first atomic write succeeds");
    check(ReadRevisitRecordFile(folder,read)==RevisitRecordRead::Valid && read.ticket==1,"first record reads back");
    auto b=a;b.ticket=2;b.createdUnixSeconds+=1;b.point.xyz[0]+=100;
    check(WriteRevisitRecordFile(folder,b),"next departure persists");
    check(ReadRevisitRecordHistory(folder,history) && history.size()==2 && history[0].ticket==2 &&
        history[1].ticket==1 && history[1].point.xyz[0]==a.point.xyz[0],"older autosave return anchor preserved exactly");
    auto conflict=a;conflict.point.xyz[0]+=50;
    check(!WriteRevisitRecordFile(folder,conflict),"same ticket cannot overwrite older return point");
    check(ReadRevisitRecordFile(folder,read)==RevisitRecordRead::Valid && read.ticket==2,"failed collision preserves latest");
    const std::string buildId=sky2::game_version::kSteamBuildId;
    const std::wstring buildSuffix(buildId.begin(),buildId.end());
    const auto latest=root/(L"revisit-return-"+buildSuffix+L".dat");
    const auto currentHistory=root/(L"revisit-history-"+buildSuffix);
    check(fs::is_regular_file(latest) && fs::is_directory(currentHistory),"new storage names use the audited Steam build");
    for (const auto& path:{legacyLatest,legacyHistory/L"0000000000000001.dat"}) {
        RevisitRecordBytes preservedLegacy{};
        std::ifstream file(path,std::ios::binary);
        file.read(reinterpret_cast<char*>(preservedLegacy.data()),preservedLegacy.size());
        check(file.gcount()==static_cast<std::streamsize>(legacyBytes.size()) && preservedLegacy==legacyBytes,
              "current writes preserve legacy primary and history byte-for-byte");
    }
    // 迁移器保留旧版指纹及完整 CRC；真实 I/O 层仍须拒绝执行和覆盖这份历史记录。
    // 这里只构造测试目录中的记录，保留当前主文件副本，随后恢复以继续其它 I/O 回归。
    RevisitRecordBytes currentBytes{}, previousBytes{};
    check(EncodeRevisitRecord(b,currentBytes),"current fixture encodes before historical-file test");
    previousBytes=PreviousBuildFixture(currentBytes);
    { std::ofstream file(latest,std::ios::binary|std::ios::trunc);
      file.write(reinterpret_cast<const char*>(previousBytes.data()),previousBytes.size()); }
    check(ReadRevisitRecordFile(folder,read)==RevisitRecordRead::Invalid,
          "historical executable record cannot become an executable return point");
    check(!WriteRevisitRecordFile(folder,b),"historical primary record is not overwritten by current build");
    RevisitRecordBytes preserved{};
    { std::ifstream file(latest,std::ios::binary);
      file.read(reinterpret_cast<char*>(preserved.data()),preserved.size()); }
    check(preserved==previousBytes,"historical record and fingerprint remain byte-for-byte preserved");
    { std::ofstream file(latest,std::ios::binary|std::ios::trunc);
      file.write(reinterpret_cast<const char*>(currentBytes.data()),currentBytes.size()); }
    HANDLE locked=CreateFileW(latest.c_str(),GENERIC_READ,0,nullptr,OPEN_EXISTING,0,nullptr);
    check(locked!=INVALID_HANDLE_VALUE,"fixture lock established");
    auto c=b;c.ticket=3;
    check(!WriteRevisitRecordFile(folder,c),"locked existing file blocks departure");
    if (locked!=INVALID_HANDLE_VALUE) CloseHandle(locked);
    // 外部同名主文件拒绝覆盖，不会把损坏记录重建为空记录。
    { std::ofstream file(latest,std::ios::binary|std::ios::trunc);file<<"foreign record"; }
    check(!WriteRevisitRecordFile(folder,c),"unknown primary file never overwritten");
    check(fs::file_size(latest)==14,"unknown bytes remain unchanged");
    check(!ReadRevisitRecordHistory(folder,history),"corrupt primary record is reported");
    fs::remove(latest);
    check(WriteRevisitRecordFile(folder,b),"valid archive can restore latest alias");
    { std::ofstream file(currentHistory/L"third-party.dat");file<<"untouched"; }
    check(ReadRevisitRecordHistory(folder,history) && history.size()==2,"foreign history name ignored");
    { std::ofstream file(currentHistory/L"0000000000000004.dat");file<<"invalid"; }
    check(!ReadRevisitRecordHistory(folder,history),"malformed owned-format history reported");
    check(fs::file_size(currentHistory/L"third-party.dat")==9,"foreign history retained");
    // 仅清理本测试明确创建的普通文件和目录；不用递归删除，也不扫描其它工作区。
    fs::remove(latest);
    for (const wchar_t* name:{L"0000000000000001.dat",L"0000000000000002.dat",L"0000000000000004.dat",L"third-party.dat"})
        fs::remove(currentHistory/name);
    check(fs::remove(currentHistory),"fixture archive cleanup");
    fs::remove(legacyLatest);
    fs::remove(legacyHistory/L"0000000000000001.dat");
    check(fs::remove(legacyHistory),"legacy fixture archive cleanup");
    check(fs::remove(root),"fixture root cleanup");
    std::printf("%u revisit journal I/O failure(s)\n",failures);
    return failures ? 1 : 0;
}
