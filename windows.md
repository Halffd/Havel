You need to replace Linux-specific code with Windows equivalents.

Major Porting Tasks
1. Input System (evdev → Win32 Raw Input)
Replace EventListener with Windows Raw Input API:

#ifdef _WIN32
#include <windows.h>

void EventListener::Start() {
    RAWINPUTDEVICE rid[2];
    
    // Keyboard
    rid[0].usUsagePage = 0x01;
    rid[0].usUsage = 0x06;
    rid[0].dwFlags = RIDEV_INPUTSINK;
    rid[0].hwndTarget = hwnd;
    
    // Mouse
    rid[1].usUsagePage = 0x01;
    rid[1].usUsage = 0x02;
    rid[1].dwFlags = RIDEV_INPUTSINK;
    rid[1].hwndTarget = hwnd;
    
    RegisterRawInputDevices(rid, 2, sizeof(RAWINPUTDEVICE));
}
#endif

2. Window Management (X11 → Win32 API)
Replace WindowManager with EnumWindows + GetWindowText:

#ifdef _WIN32
BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM lParam) {
    char title[256];
    GetWindowTextA(hwnd, title, sizeof(title));
    // ... process window
    return TRUE;
}

void WindowManager::GetWindows() {
    EnumWindows(EnumWindowsProc, 0);
}
#endif

3. Audio (PulseAudio → WASAPI)
Replace AudioManager with Windows Core Audio:

#ifdef _WIN32
#include <mmdeviceapi.h>
#include <endpointvolume.h>

void AudioManager::SetVolume(float volume) {
    IMMDeviceEnumerator* deviceEnumerator;
    CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, 
                     CLSCTX_INPROC_SERVER, __uuidof(IMMDeviceEnumerator),
                     (void**)&deviceEnumerator);
    // ... WASAPI volume control
}
#endif

4. Process Management (fork/exec → CreateProcess)
Replace Launcher::runDetached:

#ifdef _WIN32
void Launcher::runDetached(const std::string& cmd) {
    STARTUPINFO si = {sizeof(si)};
    PROCESS_INFORMATION pi;
    CreateProcessA(NULL, (LPSTR)cmd.c_str(), NULL, NULL, FALSE,
                   DETACHED_PROCESS, NULL, NULL, &si, &pi);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
}
#endif

5. File System (/proc → Win32)
Replace /proc usage with Windows API:

#ifdef _WIN32
std::string ProcessManager::getProcessExecutablePath(DWORD pid) {
    HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    char path[MAX_PATH];
    GetModuleFileNameExA(hProcess, NULL, path, MAX_PATH);
    CloseHandle(hProcess);
    return path;
}
#endif