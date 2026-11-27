/*
 * Дочерний процесс — Windows-версия (windows.h)
 *
 * Запускается родительским процессом (parent.exe) через CreateProcess.
 * argv[1] содержит адрес виртуальной памяти, по которому этот процесс
 * должен записать ответное сообщение (адрес передан через командную
 * строку, как требует задание).
 *
 * Логика:
 *   1. открыть объект разделяемой памяти (OpenFileMapping) и отобразить
 *      его по адресу из командной строки — MapViewOfFileEx, поэтому
 *      виртуальный адрес страницы совпадает с адресом в родительском
 *      процессе, а физически это одна и та же память;
 *   2. открыть события синхронизации (OpenEvent);
 *   3. сигнализировать "дочерний процесс запущен";
 *   4. ждать "сообщение готово", прочитать сообщение;
 *   5. записать ответ по адресу из командной строки и сигнализировать
 *      "ответ готов".
 */

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define SHM_NAME "Local\\sp_vm_exchange"
#define EV_CHILD_READY  "Local\\sp_vm_child_ready"
#define EV_MESSAGE_READY "Local\\sp_vm_message_ready"
#define EV_REPLY_READY  "Local\\sp_vm_reply_ready"

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
    uintptr_t reply_addr;
    HANDLE child_ready;
    HANDLE message_ready;
    HANDLE reply_ready;

    SetConsoleOutputCP(CP_UTF8);
    setbuf(stdout, NULL);
    printf("=== Дочерний процесс (pid %lu) ===\n",
           (unsigned long)GetCurrentProcessId());

    /* 1. адрес страницы ответа получен через командную строку */
    if (argc < 2) {
        fprintf(stderr,
                "[child] эту программу не запускают самостоятельно:\n"
                "[child] её запускает родительский процесс (parent.exe),\n"
                "[child] передавая адрес страницы ответа через командную "
                "строку.\n"
                "[child] запустите parent — дочерний процесс создастся "
                "автоматически.\n");
        return 1;
    }
    reply_addr = (uintptr_t)strtoull(argv[1], NULL, 0);
    if (reply_addr == 0) {
        fprintf(stderr, "[child] некорректный адрес: %s\n", argv[1]);
        return 1;
    }
    printf("[child] адрес из командной строки: 0x%llx\n",
           (unsigned long long)reply_addr);

    GetSystemInfo(&sys_info);
    page_size = sys_info.dwPageSize;
    /* выравнивание по гранулярности аллокации (64 КБ), как у родителя */
    stride = (SIZE_T)sys_info.dwAllocationGranularity;
    if (stride < (SIZE_T)page_size) {
        stride = (SIZE_T)page_size;
    }
    map_len = 2 * stride;

    /* 2. общая страница по адресу из командной строки */
    mapping = OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, SHM_NAME);
    if (mapping == NULL) {
        fprintf(stderr, "[child] OpenFileMapping: %lu\n",
                (unsigned long)GetLastError());
        return 1;
    }
    view = (char *)MapViewOfFileEx(mapping, FILE_MAP_ALL_ACCESS,
                                   0, 0, map_len,
                                   (LPVOID)(reply_addr - (uintptr_t)stride));
    if (view == NULL) {
        fprintf(stderr, "[child] MapViewOfFileEx: %lu\n",
                (unsigned long)GetLastError());
        return 1;
    }
    CloseHandle(mapping);

    request = view;              /* сообщение родителя */
    reply = view + stride;       /* сюда пишем ответ */

    printf("[child] страница запроса отображена @ %p\n", (void *)request);
    printf("[child] страница ответа   отображена @ %p "
           "(совпадает с адресом родителя)\n", (void *)reply);

    /* 3. события синхронизации */
    child_ready = OpenEventA(EVENT_MODIFY_STATE, FALSE, EV_CHILD_READY);
    message_ready = OpenEventA(SYNCHRONIZE | EVENT_MODIFY_STATE, FALSE,
                               EV_MESSAGE_READY);
    reply_ready = OpenEventA(EVENT_MODIFY_STATE, FALSE, EV_REPLY_READY);
    if (child_ready == NULL || message_ready == NULL ||
        reply_ready == NULL) {
        fprintf(stderr, "[child] OpenEvent: %lu\n",
                (unsigned long)GetLastError());
        return 1;
    }

    /* 4. родитель может начинать обмен */
    SetEvent(child_ready);

    /* 5. ждем сообщение и читаем память */
    printf("[child] WaitForSingleObject (msg_ready)...\n");
    WaitForSingleObject(message_ready, INFINITE);
    printf("[child] прочитано из памяти другого процесса: \"%s\"\n",
           request);

    /* 6. ответ записываем по адресу из командной строки */
    snprintf(reply, page_size, "Ответ из дочернего процесса %lu!",
             (unsigned long)GetCurrentProcessId());
    printf("[child] записан ответ: \"%s\"\n", reply);
    printf("[child] SetEvent (reply_ready)\n");
    SetEvent(reply_ready);

    /* 7. освобождение ресурсов */
    UnmapViewOfFile(view);
    CloseHandle(child_ready);
    CloseHandle(message_ready);
    CloseHandle(reply_ready);
    return 0;
}
