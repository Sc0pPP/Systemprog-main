/*
 * Работа с буфером экрана — Windows-версия (windows.h)
 *
 * Те же три задания, что и в macOS-версии, но на настоящем консольном
 * API Windows из лекции:
 *
 *   Задание 1: WriteConsoleOutputAttribute / ReadConsoleOutputAttribute
 *              — установка и чтение атрибутов текста консоли;
 *   Задание 2: CreateConsoleScreenBuffer + WriteConsoleOutput (Unicode,
 *              поле WCHAR из CHAR_INFO) + SetConsoleActiveScreenBuffer —
 *              вывод содержимого файла в новый буфер экрана;
 *   Задание 3: копирование файлов на стандартное устройство вывода
 *              (CreateFile/ReadFile/WriteFile), параметр -s.
 *
 * Сборка (Visual Studio): File -> New -> Project -> Empty Project (C++),
 * добавить этот файл, собрать. Командной строкой: cl /W4 /utf-8 main_windows.c
 * Файл должен быть сохранен в кодировке UTF-8.
 *
 * Запуск:
 *   ScreenBuffer.exe            — демонстрация всех трех заданий;
 *   ScreenBuffer.exe [-s] файл… — задание 3 (копирование на stdout).
 */

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static COORD mkcoord(SHORT x, SHORT y)
{
    COORD c;
    c.X = x;
    c.Y = y;
    return c;
}

static SMALL_RECT mkrect(SHORT left, SHORT top, SHORT right, SHORT bottom)
{
    SMALL_RECT r;
    r.Left = left;
    r.Top = top;
    r.Right = right;
    r.Bottom = bottom;
    return r;
}

static void clear_screen(HANDLE hOut)
{
    CONSOLE_SCREEN_BUFFER_INFO info;
    DWORD written;
    COORD origin = mkcoord(0, 0);

    GetConsoleScreenBufferInfo(hOut, &info);
    FillConsoleOutputCharacterA(hOut, ' ',
                                (DWORD)info.dwSize.X * (DWORD)info.dwSize.Y,
                                origin, &written);
    SetConsoleCursorPosition(hOut, origin);
}

/* ------------------------------------------------------------------ */
/* Задание 1: установка и чтение атрибутов текста                     */
/* ------------------------------------------------------------------ */

static void demo_write_read_attrs(HANDLE hOut)
{
    WORD to_write[16];
    WORD read_back[16];
    DWORD written = 0;
    DWORD read_count = 0;
    int i;
    int match;

    for (i = 0; i < 7; i++) {
        to_write[i] = (WORD)(i + 1);
        to_write[8 + i] = (WORD)((i + 1) | FOREGROUND_INTENSITY);
    }
    /* цвет 0 (черный) — на белом фоне, чтобы буква была видна */
    to_write[7] = BACKGROUND_BLUE | BACKGROUND_GREEN | BACKGROUND_RED;
    to_write[15] = (WORD)(to_write[7] | FOREGROUND_INTENSITY);

    /* буквы A..P в строке 4 (координаты клеток, начиная с (2, 4)) */
    for (i = 0; i < 16; i++) {
        COORD pos = mkcoord((SHORT)(2 + i), 4);
        FillConsoleOutputCharacterA(hOut, (CHAR)('A' + i), 1, pos, &written);
    }

    /* WriteConsoleOutputAttribute: массив атрибутов -> клетки буфера */
    WriteConsoleOutputAttribute(hOut, to_write, 16, mkcoord(2, 4), &written);

    /* ReadConsoleOutputAttribute: клетки буфера -> массив атрибутов */
    ReadConsoleOutputAttribute(hOut, read_back, 16, mkcoord(2, 4), &read_count);

    printf("записано клеток: %lu, прочитано клеток: %lu\n",
           (unsigned long)written, (unsigned long)read_count);
    printf("записали атрибуты:");
    for (i = 0; i < 16; i++) {
        printf(" %02x", (unsigned)to_write[i]);
    }
    printf("\nпрочитали атрибуты:");
    for (i = 0; i < 16; i++) {
        printf(" %02x", (unsigned)read_back[i]);
    }
    printf("\n");

    match = (written == read_count);
    for (i = 0; i < 16 && match; i++) {
        match = to_write[i] == read_back[i];
    }
    printf(match ? "атрибуты совпадают: чтение/запись работают\n"
                 : "ОШИБКА: атрибуты не совпадают\n");

    Sleep(3000); /* строка с цветными буквами остается на экране */
}

/* ------------------------------------------------------------------ */
/* Задание 2: вывод содержимого файла в новый буфер экрана            */
/* ------------------------------------------------------------------ */

/* открыть файл; если это файл по умолчанию и его нет — создать образец */
static FILE *open_text_file(const char *path, int create_sample)
{
    FILE *f = fopen(path, "r");
    if (f != NULL || !create_sample) {
        return f;
    }
    f = fopen(path, "w");
    if (f == NULL) {
        return NULL;
    }
    fputs("Это образец файла text.txt.\n"
          "Буфер заполнен содержимым файла.\n"
          "CreateConsoleScreenBuffer\n"
          "WriteConsoleOutput (Unicode)\n"
          "SetConsoleActiveScreenBuffer\n", f);
    fclose(f);
    return fopen(path, "r");
}

/* UTF-8 -> UTF-16 (WCHAR), возвращает число символов с нулем */
static int utf8_to_wide(const char *utf8, wchar_t *wide, int wide_cap)
{
    return MultiByteToWideChar(CP_UTF8, 0, utf8, -1, wide, wide_cap);
}

static void write_cells_line(CHAR_INFO *cells, int width, int y,
                             const wchar_t *text, WORD attrs)
{
    int x = 2;
    while (*text != L'\0' && x < width - 1) {
        cells[y * width + x].Char.UnicodeChar = *text++;
        cells[y * width + x].Attributes = attrs;
        x++;
    }
}

static void demo_file_to_screen_buffer(HANDLE hOut)
{
    const char *path = "text.txt";
    CONSOLE_SCREEN_BUFFER_INFO info;
    COORD size;
    HANDLE hNew;
    CHAR_INFO *cells;
    FILE *f;
    int width, height, y;

    GetConsoleScreenBufferInfo(hOut, &info);
    size = info.dwSize;
    width = size.X;
    height = size.Y;

    /* CreateConsoleScreenBuffer: создаем новый буфер экрана */
    hNew = CreateConsoleScreenBuffer(GENERIC_READ | GENERIC_WRITE,
                                     FILE_SHARE_READ | FILE_SHARE_WRITE,
                                     NULL, CONSOLE_TEXTMODE_BUFFER, NULL);
    if (hNew == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "cannot create screen buffer\n");
        return;
    }
    SetConsoleScreenBufferSize(hNew, size);

    cells = (CHAR_INFO *)calloc((size_t)width * (size_t)height,
                                sizeof(CHAR_INFO));
    if (cells == NULL) {
        CloseHandle(hNew);
        return;
    }

    write_cells_line(cells, width, 0, L"Новый буфер экрана (active)",
                     FOREGROUND_RED | FOREGROUND_GREEN);

    f = open_text_file(path, 1);
    if (f != NULL) {
        char line[256];
        wchar_t wline[512];
        y = 2;
        while (y < height && fgets(line, sizeof(line), f) != NULL) {
            line[strcspn(line, "\r\n")] = '\0';
            if (utf8_to_wide(line, wline, 512) > 0) {
                write_cells_line(cells, width, y, wline, 0);
            }
            y++;
        }
        fclose(f);
        printf("показан файл: %s\n", path);
    } else {
        static const wchar_t *sample[] = {
            L"Файл text.txt не найден.",
            L"Поместите текстовый файл рядом с программой.",
            L"Это встроенный пример вывода в буфер.",
            NULL
        };
        int i;
        for (i = 0; sample[i] != NULL; i++) {
            write_cells_line(cells, width, 2 + i, sample[i], 0);
        }
    }

    /* WriteConsoleOutput: массив CHAR_INFO -> новый буфер экрана */
    {
        COORD buf_size = size;
        COORD buf_coord = mkcoord(0, 0);
        SMALL_RECT region = mkrect(0, 0, (SHORT)(width - 1),
                                   (SHORT)(height - 1));
        WriteConsoleOutputW(hNew, cells, buf_size, buf_coord, &region);
    }

    /* SetConsoleActiveScreenBuffer: новый буфер становится видимым */
    SetConsoleActiveScreenBuffer(hNew);
    Sleep(3000);
    SetConsoleActiveScreenBuffer(hOut);

    free(cells);
    CloseHandle(hNew);
}

/* ------------------------------------------------------------------ */
/* Задание 3: копирование файлов на стандартное устройство вывода     */
/* ------------------------------------------------------------------ */

static int copy_file_to_stdout(const char *path, int suppress_errors,
                               const char *progname)
{
    HANDLE in = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL,
                            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (in == INVALID_HANDLE_VALUE) {
        if (!suppress_errors) {
            fprintf(stderr, "%s: cannot open file '%s'\n", progname, path);
        }
        return 1;
    }
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    char buf[4096];
    DWORD n;
    int status = 0;

    while (ReadFile(in, buf, sizeof(buf), &n, NULL) && n > 0) {
        DWORD w;
        if (!WriteFile(out, buf, n, &w, NULL)) {
            status = 1;
            break;
        }
    }
    CloseHandle(in);
    return status;
}

/* режим утилиты: ScreenBuffer.exe [-s] файл… */
static int copy_files(int argc, char **argv)
{
    int suppress_errors = 0;
    int first_file = 1;
    int status = 0;
    int i;

    if (first_file < argc && strcmp(argv[first_file], "-s") == 0) {
        suppress_errors = 1;
        first_file++;
    }
    if (first_file >= argc) {
        fprintf(stderr, "usage: %s [-s] file...\n", argv[0]);
        return 1;
    }
    for (i = first_file; i < argc; i++) {
        if (copy_file_to_stdout(argv[i], suppress_errors, argv[0]) != 0) {
            status = 1;
        }
    }
    return status;
}

/* задание 3 в составе демо: копируем существующий файл и пропускаем
 * несуществующий, сначала без -s (ошибка видна), затем с -s (подавлена) */
static void demo_copy_files(void)
{
    const char *sample_name = "mytype_sample.txt";
    HANDLE f = CreateFileA(sample_name, GENERIC_WRITE, 0, NULL,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f != INVALID_HANDLE_VALUE) {
        const char *text = "Это образец для задания 3.\r\n"
                           "Программа копирует файлы на стандартное\r\n"
                           "устройство вывода.\r\n";
        DWORD w;
        WriteFile(f, text, (DWORD)strlen(text), &w, NULL);
        CloseHandle(f);
    }

    printf("--- без -s (ошибка видна):\n");
    {
        char *argv1[] = { (char *)"ScreenBuffer", (char *)sample_name,
                          (char *)"no_such_file.txt", NULL };
        int rc1 = copy_files(3, argv1);
        printf("--- с -s (ошибка подавлена):\n");
        {
            char *argv2[] = { (char *)"ScreenBuffer", (char *)"-s",
                              (char *)sample_name, (char *)"no_such_file.txt",
                              NULL };
            int rc2 = copy_files(4, argv2);
            printf("коды возврата: без -s = %d, с -s = %d "
                   "(1 — есть пропущенные файлы)\n", rc1, rc2);
        }
    }
    DeleteFileA(sample_name);
}

int main(int argc, char **argv)
{
    HANDLE hOut;

    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    setbuf(stdout, NULL);

    if (argc > 1) {
        return copy_files(argc, argv);
    }

    hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    clear_screen(hOut);

    printf("=== Задание 1: WriteConsoleOutputAttribute / "
           "ReadConsoleOutputAttribute ===\n");
    demo_write_read_attrs(hOut);

    printf("\n=== Задание 2: вывод файла в новый буфер экрана ===\n");
    demo_file_to_screen_buffer(hOut);

    printf("\n=== Задание 3: копирование файлов на стандартный вывод ===\n");
    demo_copy_files();

    printf("\nГотово.\n");
    return 0;
}
