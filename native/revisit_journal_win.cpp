// 只读写固定名称的 Mod 返程记录。拒绝链接与损坏文件，避免无意覆盖同名其它数据。
// 游戏存档及 Steam Cloud 文件不在此模块的访问范围。
#include "revisit_journal.h"
#include <Windows.h>
#include <bcrypt.h>
#include <cstdio>
#include <algorithm>

namespace tracker {
namespace {
constexpr wchar_t kRecordName[] = L"\\revisit-return.dat";
constexpr wchar_t kHistoryName[] = L"\\revisit-history";
bool PlainFolder(const std::wstring& folder) noexcept {
    const DWORD attributes=GetFileAttributesW(folder.c_str());
    return attributes!=INVALID_FILE_ATTRIBUTES && (attributes&FILE_ATTRIBUTE_DIRECTORY) &&
        !(attributes&FILE_ATTRIBUTE_REPARSE_POINT);
}
RevisitRecordRead ReadFilePath(const std::wstring& path, RevisitReturnRecord& record) noexcept {
    HANDLE file=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,
        FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
    if (file==INVALID_HANDLE_VALUE) {
        const auto error=GetLastError();
        return error==ERROR_FILE_NOT_FOUND ? RevisitRecordRead::Missing : RevisitRecordRead::IoError;
    }
    BY_HANDLE_FILE_INFORMATION info{};
    RevisitRecordBytes bytes{}; DWORD read=0;
    const bool plain=GetFileInformationByHandle(file,&info) &&
        !(info.dwFileAttributes&(FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DIRECTORY)) &&
        info.nFileSizeHigh==0 && info.nFileSizeLow==bytes.size();
    const bool complete=plain && ReadFile(file,bytes.data(),static_cast<DWORD>(bytes.size()),&read,nullptr) && read==bytes.size();
    CloseHandle(file);
    if (!plain) return RevisitRecordRead::Invalid;
    if (!complete) return RevisitRecordRead::IoError;
    return DecodeRevisitRecord(bytes.data(),bytes.size(),record) ? RevisitRecordRead::Valid : RevisitRecordRead::Invalid;
}
// 历史记录不使用 REPLACE_EXISTING。相同票据只能对应完全相同的内容，任何冲突均
// 中止新出发，既不覆盖未知文件，也不会抹去另一份游戏自动存档需要的旧出发点。
bool PreserveHistory(const std::wstring& folder, const RevisitReturnRecord& record,
                     const RevisitRecordBytes& bytes) {
    const auto history=folder+kHistoryName;
    if (!CreateDirectoryW(history.c_str(),nullptr) && GetLastError()!=ERROR_ALREADY_EXISTS) return false;
    if (!PlainFolder(history)) return false;
    wchar_t name[40]{};
    swprintf_s(name,L"\\%016llx.dat",record.ticket);
    const auto path=history+name;
    RevisitReturnRecord existing{}; RevisitRecordBytes oldBytes{};
    const auto old=ReadFilePath(path,existing);
    if (old==RevisitRecordRead::Valid)
        return EncodeRevisitRecord(existing,oldBytes) && oldBytes==bytes;
    if (old!=RevisitRecordRead::Missing) return false;
    // 最终文件仅在完整写入和刷新后出现；中途退出留下的 .tmp 不会被历史扫描识别。
    const auto temporary=path+L".tmp";
    HANDLE file=CreateFileW(temporary.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,
        FILE_ATTRIBUTE_NORMAL|FILE_FLAG_WRITE_THROUGH,nullptr);
    if (file==INVALID_HANDLE_VALUE) return false;
    DWORD written=0;
    const bool complete=WriteFile(file,bytes.data(),static_cast<DWORD>(bytes.size()),&written,nullptr) &&
        written==bytes.size() && FlushFileBuffers(file);
    CloseHandle(file);
    if (!complete || !MoveFileExW(temporary.c_str(),path.c_str(),MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(temporary.c_str()); return false;
    }
    RevisitReturnRecord verified{}; RevisitRecordBytes verifiedBytes{};
    return ReadFilePath(path,verified)==RevisitRecordRead::Valid &&
        EncodeRevisitRecord(verified,verifiedBytes) && verifiedBytes==bytes;
}
}
RevisitRecordRead ReadRevisitRecordFile(const std::wstring& folder, RevisitReturnRecord& record) noexcept {
    try {
        if (!PlainFolder(folder)) return RevisitRecordRead::IoError;
        return ReadFilePath(folder+kRecordName,record);
    } catch (...) { return RevisitRecordRead::IoError; }
}
bool WriteRevisitRecordFile(const std::wstring& folder, const RevisitReturnRecord& record) noexcept {
    try {
        RevisitRecordBytes bytes{};
        if (!PlainFolder(folder) || !EncodeRevisitRecord(record,bytes)) return false;
        const std::wstring target=folder+kRecordName;
        RevisitReturnRecord existing{};
        const auto old=ReadFilePath(target,existing);
        if (old!=RevisitRecordRead::Missing && old!=RevisitRecordRead::Valid) return false;
        // 兼容只有主记录的早期格式：更新主记录之前，同时保留旧记录和本次记录。
        RevisitRecordBytes oldBytes{};
        if (old==RevisitRecordRead::Valid &&
            (!EncodeRevisitRecord(existing,oldBytes) || !PreserveHistory(folder,existing,oldBytes))) return false;
        if (!PreserveHistory(folder,record,bytes)) return false;
        uint64_t nonce=0;
        if (BCryptGenRandom(nullptr,reinterpret_cast<PUCHAR>(&nonce),sizeof(nonce),BCRYPT_USE_SYSTEM_PREFERRED_RNG)<0) return false;
        wchar_t suffix[64]{};
        swprintf_s(suffix,L"\\revisit-return-%lu-%016llx.tmp",GetCurrentProcessId(),nonce);
        const std::wstring temporary=folder+suffix;
        HANDLE file=CreateFileW(temporary.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,
            FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_WRITE_THROUGH,nullptr);
        if (file==INVALID_HANDLE_VALUE) return false;
        DWORD written=0;
        const bool writtenAll=WriteFile(file,bytes.data(),static_cast<DWORD>(bytes.size()),&written,nullptr) &&
            written==bytes.size() && FlushFileBuffers(file);
        CloseHandle(file);
        // 仅删除刚由本函数CREATE_NEW成功创建的临时文件；从不删除原返程记录。
        if (!writtenAll) { DeleteFileW(temporary.c_str()); return false; }
        RevisitReturnRecord latest{};
        const auto current=ReadFilePath(target,latest);
        RevisitRecordBytes originalBytes{},latestBytes{};
        const bool unchanged=current==old && (old==RevisitRecordRead::Missing ||
            (EncodeRevisitRecord(existing,originalBytes) && EncodeRevisitRecord(latest,latestBytes) && originalBytes==latestBytes));
        if (!unchanged || !PlainFolder(folder)) { DeleteFileW(temporary.c_str()); return false; }
        if (!MoveFileExW(temporary.c_str(),target.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)) {
            DeleteFileW(temporary.c_str()); return false;
        }
        RevisitReturnRecord verified{}; RevisitRecordBytes verifiedBytes{};
        return ReadFilePath(target,verified)==RevisitRecordRead::Valid &&
            EncodeRevisitRecord(verified,verifiedBytes) && verifiedBytes==bytes;
    } catch (...) { return false; }
}
bool ReadRevisitRecordHistory(const std::wstring& folder,
                              std::vector<RevisitReturnRecord>& records) noexcept {
    try {
        records.clear();
        if (!PlainFolder(folder)) return false;
        RevisitReturnRecord current{};
        const auto state=ReadRevisitRecordFile(folder,current);
        if (state==RevisitRecordRead::Invalid || state==RevisitRecordRead::IoError) return false;
        if (state==RevisitRecordRead::Valid) records.push_back(current);
        const auto history=folder+kHistoryName;
        const auto attributes=GetFileAttributesW(history.c_str());
        if (attributes==INVALID_FILE_ATTRIBUTES)
            return GetLastError()==ERROR_FILE_NOT_FOUND || GetLastError()==ERROR_PATH_NOT_FOUND;
        if (!PlainFolder(history)) return false;
        WIN32_FIND_DATAW entry{};
        HANDLE search=FindFirstFileW((history+L"\\*.dat").c_str(),&entry);
        if (search==INVALID_HANDLE_VALUE) return GetLastError()==ERROR_FILE_NOT_FOUND;
        bool ok=true;
        do {
            // 仅识别16位小写十六进制票据名；第三方文件不读取、不删除、不修改。
            const std::wstring name=entry.cFileName;
            if (name.size()!=20 || name.substr(16)!=L".dat") continue;
            if (name.find_first_not_of(L"0123456789abcdef",0)<16) continue;
            RevisitReturnRecord record{};
            if (ReadFilePath(history+L"\\"+name,record)!=RevisitRecordRead::Valid) { ok=false; continue; }
            wchar_t expected[24]{}; swprintf_s(expected,L"%016llx.dat",record.ticket);
            if (name!=expected) { ok=false; continue; }
            const auto previous=std::find_if(records.begin(),records.end(),[&](const auto& value) {
                return value.ticket==record.ticket;
            });
            if (previous==records.end()) records.push_back(record);
            else {
                RevisitRecordBytes a{},b{};
                if (!EncodeRevisitRecord(*previous,a) || !EncodeRevisitRecord(record,b) || a!=b) ok=false;
            }
        } while (FindNextFileW(search,&entry));
        if (GetLastError()!=ERROR_NO_MORE_FILES) ok=false;
        FindClose(search);
        std::sort(records.begin(),records.end(),[](const auto& a,const auto& b) {
            return a.createdUnixSeconds!=b.createdUnixSeconds ? a.createdUnixSeconds>b.createdUnixSeconds : a.ticket>b.ticket;
        });
        return ok;
    } catch (...) { records.clear(); return false; }
}
}
