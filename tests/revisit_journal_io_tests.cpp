// 在独立临时目录验证真实Windows文件I/O；测试不接触游戏目录或用户保存数据。
#include "revisit_journal.h"
#include <Windows.h>
#include <cstdio>
#include <fstream>
#include <filesystem>
namespace fs=std::filesystem;
using namespace tracker;
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
    RevisitReturnRecord read{}; std::vector<RevisitReturnRecord> history;
    check(ReadRevisitRecordFile(folder,read)==RevisitRecordRead::Missing,"initial record missing");
    check(ReadRevisitRecordHistory(folder,history) && history.empty(),"initial history empty");
    check(WriteRevisitRecordFile(folder,a),"first atomic write succeeds");
    check(ReadRevisitRecordFile(folder,read)==RevisitRecordRead::Valid && read.ticket==1,"first record reads back");
    auto b=a;b.ticket=2;b.createdUnixSeconds+=1;b.point.xyz[0]+=100;
    check(WriteRevisitRecordFile(folder,b),"next departure persists");
    check(ReadRevisitRecordHistory(folder,history) && history.size()==2 && history[0].ticket==2 &&
        history[1].ticket==1 && history[1].point.xyz[0]==a.point.xyz[0],"older autosave return anchor preserved exactly");
    auto conflict=a;conflict.point.xyz[0]+=50;
    check(!WriteRevisitRecordFile(folder,conflict),"same ticket cannot overwrite older return point");
    check(ReadRevisitRecordFile(folder,read)==RevisitRecordRead::Valid && read.ticket==2,"failed collision preserves latest");
    const auto latest=root/L"revisit-return.dat";
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
    { std::ofstream file(root/L"revisit-history"/L"third-party.dat");file<<"untouched"; }
    check(ReadRevisitRecordHistory(folder,history) && history.size()==2,"foreign history name ignored");
    { std::ofstream file(root/L"revisit-history"/L"0000000000000004.dat");file<<"invalid"; }
    check(!ReadRevisitRecordHistory(folder,history),"malformed owned-format history reported");
    check(fs::file_size(root/L"revisit-history"/L"third-party.dat")==9,"foreign history retained");
    // 仅清理本测试明确创建的普通文件和目录；不用递归删除，也不扫描其它工作区。
    fs::remove(latest);
    for (const wchar_t* name:{L"0000000000000001.dat",L"0000000000000002.dat",L"0000000000000004.dat",L"third-party.dat"})
        fs::remove(root/L"revisit-history"/name);
    check(fs::remove(root/L"revisit-history"),"fixture archive cleanup");
    check(fs::remove(root),"fixture root cleanup");
    std::printf("%u revisit journal I/O failure(s)\n",failures);
    return failures ? 1 : 0;
}
