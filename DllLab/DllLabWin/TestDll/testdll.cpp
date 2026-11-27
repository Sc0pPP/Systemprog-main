#include <windows.h>
#include <cstdio>
#include <cstring>

typedef void (*HelloFunc)();

void run(const char* file, const char* name)
{
    try
    {
        HMODULE lib = LoadLibraryA(file);
        if (!lib)
            throw "не найден файл библиотеки";

        HelloFunc hello = (HelloFunc)GetProcAddress(lib, name);
        if (!hello)
        {
            FreeLibrary(lib);
            throw "в библиотеке нет функции";
        }
        hello();
        FreeLibrary(lib);
    }
    catch (const char* error)
    {
        printf("Ошибка: %s\n", error);
    }
}

int main()
{
    SetConsoleOutputCP(CP_UTF8);

    /* библиотека ищется рядом с собственным исполняемым файлом,
     * а не в текущем каталоге (как в macOS-версии) */
    char lib[MAX_PATH], missing[MAX_PATH];
    GetModuleFileNameA(NULL, lib, MAX_PATH);
    char* slash = strrchr(lib, '\\');
    if (!slash)
        slash = strrchr(lib, '/');
    if (slash)
        slash[1] = '\0';
    else
        lib[0] = '\0';
    snprintf(missing, sizeof(missing), "%sMissing.dll", lib);
    strcat(lib, "TestFunc.dll");

    printf("--- Тест 1: вызов Hello ---\n");
    run(lib, "Hello");

    printf("\n--- Тест 2: нет файла библиотеки ---\n");
    run(missing, "Hello");

    printf("\n--- Тест 3: нет функции в библиотеке ---\n");
    run(lib, "Goodbye");

    return 0;
}
