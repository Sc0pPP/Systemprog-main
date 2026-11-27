#include <windows.h>
#include <cstdio>

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID reserved)
{
    (void)hModule;
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH)
        printf("библиотека загружена\n");
    if (reason == DLL_PROCESS_DETACH)
        printf("библиотека выгружена\n");
    return TRUE;
}
