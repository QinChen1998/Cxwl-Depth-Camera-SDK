// MIT. Lightweight entry points keep one portable runtime in SDK/bin.
#include <windows.h>
#include <shellapi.h>
#include <string>
#include <vector>
#include <cstdio>
#include <conio.h>
static std::wstring quote(const wchar_t *input) {
    std::wstring out=L"\""; size_t slashes=0;
    for(const wchar_t *p=input; *p; ++p) {
        if(*p==L'\\') { ++slashes; continue; }
        if(*p==L'"') { out.append(slashes*2+1,L'\\'); out+=L'"'; }
        else { out.append(slashes,L'\\'); out+=*p; }
        slashes=0;
    }
    out.append(slashes*2,L'\\'); out+=L'"'; return out;
}
static int report_error(int code, const std::wstring &detail) {
#ifdef CXWL_GUI_LAUNCHER
    MessageBoxW(nullptr, detail.c_str(), L"RGBD SDK", MB_ICONERROR);
#else
    const int size = WideCharToMultiByte(CP_UTF8, 0, detail.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (size > 0) {
        std::string message(size, '\0');
        WideCharToMultiByte(CP_UTF8, 0, detail.c_str(), -1, message.data(), size, nullptr, nullptr);
        std::fprintf(stderr, "%s\n", message.c_str());
    }
#endif
    return code;
}
static int launch() {
    wchar_t module[32768]; const DWORD n=GetModuleFileNameW(nullptr,module,32768);
    if(!n || n>=32768) return report_error(3, L"Cannot resolve launcher path.");
    std::wstring directory(module,n); directory.resize(directory.find_last_of(L"\\/"));
    const auto relative=directory+L"\\..\\..\\SDK\\bin\\"+CXWL_SAMPLE_TARGET;
    wchar_t resolved[32768]; const DWORD length=GetFullPathNameW(relative.c_str(),32768,resolved,nullptr);
    if (!length || length >= 32768) return report_error(3, L"Cannot resolve target path: " + relative);
    if (GetFileAttributesW(resolved) == INVALID_FILE_ATTRIBUTES)
        return report_error(4, std::wstring(L"SDK example is missing. Extract the complete SDK package.\nresolved=") + resolved);
    int count=0; wchar_t **args=CommandLineToArgvW(GetCommandLineW(),&count);
    if(!args) return report_error(5, L"Cannot read launcher arguments.");
    std::wstring command=quote(resolved);
    for(int i=1;i<count;++i) { command+=L' '; command+=quote(args[i]); }
    LocalFree(args); std::vector<wchar_t> buffer(command.begin(),command.end()); buffer.push_back(0);
    STARTUPINFOW startup{}; startup.cb=sizeof(startup);
    PROCESS_INFORMATION child{};
    if(!CreateProcessW(resolved,buffer.data(),nullptr,nullptr,TRUE,0,nullptr,nullptr,&startup,&child)) {
        const DWORD error = GetLastError();
        return report_error(6, std::wstring(L"Cannot start ") + resolved + L" (Windows error " + std::to_wstring(error) + L").");
    }
    CloseHandle(child.hThread);
    const DWORD wait=WaitForSingleObject(child.hProcess,INFINITE);
    DWORD code = 0;
    const bool completed = wait == WAIT_OBJECT_0 && GetExitCodeProcess(child.hProcess, &code);
    CloseHandle(child.hProcess);
    return completed ? int(code) : report_error(7, L"Cannot obtain the SDK example exit code.");
}
#ifdef CXWL_GUI_LAUNCHER
int WINAPI wWinMain(HINSTANCE,HINSTANCE,PWSTR,int) { return launch(); }
#else
int wmain() {
    // A terminal/script already owns its console. Only an Explorer-created
    // console disappears with this launcher; never block redirected CI input.
    DWORD processes[2]{}; DWORD mode=0;
    const bool ownConsole=GetConsoleProcessList(processes,2)==1
        && GetConsoleMode(GetStdHandle(STD_INPUT_HANDLE),&mode);
    const UINT previousCodepage=GetConsoleOutputCP();
    if(previousCodepage) SetConsoleOutputCP(CP_UTF8);
    const int result=launch();
    if(ownConsole) {
        std::fputs("\n运行结束，按任意键关闭 / Press any key to close...\n",stdout);
        _getwch();
    }
    if(previousCodepage) SetConsoleOutputCP(previousCodepage);
    return result;
}
#endif
