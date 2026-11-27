/*
 * Родительский процесс — Windows-версия (windows.h)
 *
 * ТЗ: "Разработать две программы, первая из которых создает консольный
 * процесс, а затем пишет и читает данные из виртуальной памяти этого
 * процесса. Адрес виртуальной памяти, по которому дочерний консольный
 * процесс должен записать ответное сообщение, передается этому процессу
 * через командную строку. Программы синхронизируют свой доступ к
 * виртуальной памяти, чтобы корректно передать и получить сообщения."
 *
 * Windows API из лекции (здесь все настоящие вызовы, без аналогов):
 *   CreateProcess          — создание дочернего процесса;
 *   CreateFileMapping      — объект разделяемой памяти (две страницы:
 *                            запрос и ответ);
 *   MapViewOfFileEx        — отображение по заданному виртуальному адресу
 *                            (одинаковому в обоих процессах);
 *   WriteProcessMemory     — запись в память дочернего процесса;
 *   ReadProcessMemory      — чтение из памяти дочернего процесса;
 *   CreateEvent/SetEvent/WaitForSingleObject/WaitForMultipleObjects —
 *                            события синхронизации.
 *
 * Протокол:
 *   1. родитель создает разделяемую память и события;
 *   2. родитель запускает child.exe, передавая адрес страницы ответа
 *      в командной строке;
 *   3. дочерний процесс отображает память по этому адресу и сигнализирует
 *      событие "дочерний процесс запущен";
 *   4. родитель пишет сообщение в память дочернего процесса
 *      (WriteProcessMemory) и сигнализирует "сообщение готово";
 *   5. дочерний процесс читает сообщение, пишет ответ по адресу из
 *      командной строки и сигнализирует "ответ готов";
 *   6. родитель читает ответ (ReadProcessMemory).
 *
 * Сборка: пустой проект Visual Studio, добавить файл; родителю нужен
 * child.exe рядом с собственным исполняемым файлом (каталог Debug).
 */

#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

#define SHM_NAME "Local\\sp_vm_exchange"
#define EV_CHILD_READY  "Local\\sp_vm_child_ready"
#define EV_MESSAGE_READY "Local\\sp_vm_message_ready"
#define EV_REPLY_READY  "Local\\sp_vm_reply_ready"

/* кандидаты на общий виртуальный адрес (все 64 КБ-выровнены) */
static const unsigned long long kHints[] = {
    0x300000000ULL, 0x400000000ULL, 0x500000000ULL
};

int main(int argc, char *argv[])
{
    SYSTEM_INFO sys_info;
    DWORD page_size;
    SIZE_T stride;
    SIZE_T map_len;
    HANDLE mapping;
    char *view;
    char *request;
    char *reply;
    char child_path[MAX_PATH];
    char *slash;
    char cmdline[320];
    STARTUPINFOA startup_info;
    PROCESS_INFORMATION process_info;
    HANDLE child_ready;
    HANDLE message_ready;
    HANDLE reply_ready;
    HANDLE wait_objects[2];
    DWORD wait_result;
    SIZE_T written;
    SIZE_T read_bytes;
    char message[256];
    char buffer[16384];
    DWORD exit_code;
    size_t i;

    SetConsoleOutputCP(CP_UTF8);
    setbuf(stdout, NULL);
    printf("=== Родительский процесс (pid %lu) ===\n",
           (unsigned long)GetCurrentProcessId());

    GetSystemInfo(&sys_info);
    page_size = sys_info.dwPageSize;
    /* MapViewOfFileEx требует выравнивания по гранулярности аллокации
     * (64 КБ), поэтому страница ответа лежит не через page_size, а через
     * stride — адрес отображения остается 64-КБ выровненным */
    stride = (SIZE_T)sys_info.dwAllocationGranularity;
    if (stride < (SIZE_T)page_size) {
        stride = (SIZE_T)page_size;
    }
    map_len = 2 * stride;

    /* 1. объект разделяемой памяти (аналог shm_open/macOS) */
    mapping = CreateFileMappingA(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE,
                                 0, (DWORD)map_len, SHM_NAME);
    if (mapping == NULL) {
        fprintf(stderr, "[parent] CreateFileMapping: %lu\n",
                (unsigned long)GetLastError());
        return 1;
    }

    /* 2. отображение по фиксированному адресу, общему для обоих процессов */
    view = NULL;
    for (i = 0; i < sizeof(kHints) / sizeof(kHints[0]); i++) {
        view = (char *)MapViewOfFileEx(mapping, FILE_MAP_ALL_ACCESS,
                                       0, 0, map_len, (LPVOID)kHints[i]);
        if (view != NULL) {
            break;
        }
    }
    if (view == NULL) {
        view = (char *)MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0,
                                     map_len);
    }
    if (view == NULL) {
        fprintf(stderr, "[parent] MapViewOfFileEx: %lu\n",
                (unsigned long)GetLastError());
        return 1;
    }

    request = view;              /* сюда родитель пишет сообщение */
    reply = view + stride;       /* отсюда родитель ждет ответ */

    printf("[parent] разделяемая память отображена: запрос @ %p\n",
           (void *)request);
    printf("[parent]                               ответ @ %p  <- этот "
           "адрес получит child\n", (void *)reply);

    /* 3. события синхронизации (CreateEvent) */
    child_ready = CreateEventA(NULL, FALSE, FALSE, EV_CHILD_READY);
    message_ready = CreateEventA(NULL, FALSE, FALSE, EV_MESSAGE_READY);
    reply_ready = CreateEventA(NULL, FALSE, FALSE, EV_REPLY_READY);
    if (child_ready == NULL || message_ready == NULL || reply_ready == NULL) {
        fprintf(stderr, "[parent] CreateEvent: %lu\n",
                (unsigned long)GetLastError());
        return 1;
    }

    /* 4. исполняемый файл child: рядом с собственным, либо argv[1] */
    if (argc > 1) {
        snprintf(child_path, sizeof(child_path), "%s", argv[1]);
    } else {
        GetModuleFileNameA(NULL, child_path, MAX_PATH);
        slash = strrchr(child_path, '\\');
        if (slash == NULL) {
            slash = strrchr(child_path, '/');
        }
        if (slash == NULL) {
            fprintf(stderr, "[parent] не найден путь к child.exe, "
                            "передайте его первым аргументом\n");
            return 1;
        }
        strcpy(slash + 1, "child.exe");
    }
    if (GetFileAttributesA(child_path) == INVALID_FILE_ATTRIBUTES) {
        fprintf(stderr, "[parent] не найден исполняемый файл child\n"
                        "[parent] соберите проект child и повторите, либо "
                        "передайте путь: %s <путь к child.exe>\n", argv[0]);
        return 1;
    }

    /* 5. создание консольного процесса; адрес — через командную строку */
    snprintf(cmdline, sizeof(cmdline), "\"%s\" 0x%llx",
             child_path, (unsigned long long)(uintptr_t)reply);
    memset(&startup_info, 0, sizeof(startup_info));
    startup_info.cb = sizeof(startup_info);
    memset(&process_info, 0, sizeof(process_info));

    if (!CreateProcessA(NULL, cmdline, NULL, NULL, FALSE, 0, NULL, NULL,
                        &startup_info, &process_info)) {
        fprintf(stderr, "[parent] CreateProcess: %lu\n",
                (unsigned long)GetLastError());
        return 1;
    }
    printf("[parent] создан дочерний процесс (pid %lu), передан адрес %p\n",
           (unsigned long)process_info.dwProcessId, (void *)reply);

    /* 6. ждем, пока дочерний процесс отобразит память (или завершится) */
    wait_objects[0] = child_ready;
    wait_objects[1] = process_info.hProcess;
    wait_result = WaitForMultipleObjects(2, wait_objects, FALSE, INFINITE);
    if (wait_result != WAIT_OBJECT_0) {
        fprintf(stderr, "[parent] дочерний процесс завершился до "
                        "начала обмена\n");
        return 1;
    }

    /* 7. запись сообщения в память дочернего процесса
     *    (WriteProcessMemory) + SetEvent */
    snprintf(message, sizeof(message),
             "Привет из родительского процесса %lu!",
             (unsigned long)GetCurrentProcessId());
    if (!WriteProcessMemory(process_info.hProcess, request, message,
                            strlen(message) + 1, &written)) {
        fprintf(stderr, "[parent] WriteProcessMemory: %lu\n",
                (unsigned long)GetLastError());
        return 1;
    }
    printf("[parent] записано в память другого процесса: \"%s\"\n",
           request);
    printf("[parent] SetEvent (msg_ready)\n");
    SetEvent(message_ready);

    /* 8. ждем события ответа (WaitForMultipleObjects) и читаем память
     *    (ReadProcessMemory) */
    printf("[parent] WaitForSingleObject (reply_ready)...\n");
    wait_objects[0] = reply_ready;
    wait_result = WaitForMultipleObjects(2, wait_objects, FALSE, INFINITE);
    if (wait_result != WAIT_OBJECT_0) {
        fprintf(stderr, "[parent] дочерний процесс завершился без ответа\n");
        return 1;
    }
    memset(buffer, 0, sizeof(buffer));
    if (!ReadProcessMemory(process_info.hProcess, reply, buffer,
                           page_size, &read_bytes)) {
        fprintf(stderr, "[parent] ReadProcessMemory: %lu\n",
                (unsigned long)GetLastError());
        return 1;
    }
    printf("[parent] прочитано из памяти другого процесса: \"%s\"\n",
           buffer);

    /* 9. завершение дочернего процесса и освобождение ресурсов */
    WaitForSingleObject(process_info.hProcess, INFINITE);
    GetExitCodeProcess(process_info.hProcess, &exit_code);
    printf("[parent] дочерний процесс завершился (код %lu)\n",
           (unsigned long)exit_code);

    UnmapViewOfFile(view);
    CloseHandle(mapping);
    CloseHandle(child_ready);
    CloseHandle(message_ready);
    CloseHandle(reply_ready);
    CloseHandle(process_info.hThread);
    CloseHandle(process_info.hProcess);
    printf("[parent] готово\n");
    return 0;
}
