#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string>

// Keep the proof executable beside the application's shared Qt DLLs. The
// network worker's stable bin path is a small, finite forwarding process.
int wmain() {
    wchar_t buffer[32768];const auto size=GetModuleFileNameW(nullptr,buffer,32768);
    if(!size||size>=32768)return 120;
    std::wstring root(buffer,size);
    for(int i=0;i<3;++i){const auto at=root.find_last_of(L"\\/");if(at==std::wstring::npos)return 120;root.resize(at);}
    const auto image=root+L"\\pgn-native-gap-evidence.exe";
    auto command=L"\""+image+L"\"";
    HANDLE job=CreateJobObjectW(nullptr,nullptr);if(!job)return 121;
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if(!SetInformationJobObject(job,JobObjectExtendedLimitInformation,&limits,sizeof(limits))){CloseHandle(job);return 121;}
    STARTUPINFOW startup{};startup.cb=sizeof(startup);startup.dwFlags=STARTF_USESTDHANDLES;
    startup.hStdInput=GetStdHandle(STD_INPUT_HANDLE);startup.hStdOutput=GetStdHandle(STD_OUTPUT_HANDLE);startup.hStdError=GetStdHandle(STD_ERROR_HANDLE);
    PROCESS_INFORMATION child{};
    if(!CreateProcessW(image.c_str(),command.data(),nullptr,nullptr,TRUE,CREATE_SUSPENDED|CREATE_NO_WINDOW,nullptr,root.c_str(),&startup,&child)){CloseHandle(job);return 122;}
    if(!AssignProcessToJobObject(job,child.hProcess)||ResumeThread(child.hThread)==DWORD(-1)){
        TerminateProcess(child.hProcess,123);WaitForSingleObject(child.hProcess,5000);CloseHandle(child.hThread);CloseHandle(child.hProcess);CloseHandle(job);return 123;
    }
    CloseHandle(child.hThread);const auto wait=WaitForSingleObject(child.hProcess,35000);DWORD code=124;
    if(wait==WAIT_OBJECT_0)GetExitCodeProcess(child.hProcess,&code);
    CloseHandle(job);WaitForSingleObject(child.hProcess,5000);CloseHandle(child.hProcess);return int(code);
}
