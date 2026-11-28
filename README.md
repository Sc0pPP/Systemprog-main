# Systemprog — лабораторные по системному программированию (Windows)

Три лабораторные работы на C/C++ под Windows (WinAPI). В репозитории только
Windows-сборки; macOS-версии (Xcode) исключены через .gitignore.

## Структура

```
├── ScreenBuffer/                  — лаба «буфер экрана консоли»
│   ├── main_windows.c             — корневая копия (дублирует Win-версию)
│   └── ScreenBufferWin/           — решение Visual Studio (sln + vcxproj)
├── VirtualMemory/                 — лаба «виртуальная память» (parent/child)
│   ├── child/main_windows.c
│   ├── parent/main_windows.c      — корневые копии (дублируют Win-версии)
│   └── VirtualMemoryWin/          — решение Visual Studio
└── DllLab/
    └── DllLabWin/                 — лаба «DLL во время выполнения»
        ├── DllLab.sln             — решение Visual Studio
        ├── TestFunc/              — DLL с функцией Hello (DynamicLibrary)
        └── TestDll/               — приложение: LoadLibraryA/GetProcAddress
```

## Как собрать

1. Откройте нужный `.sln` в Visual Studio 2026 (toolset v145).
2. Соберите (Ctrl+Shift+B) конфигурацию Debug или Release, платформа x64.
3. Исполняемые файлы попадают в `bin\x64\<Конфигурация>\` рядом с решением.

Важно: OutDir задан через `$(ProjectDir)..\bin\...`, поэтому `TestDll.exe`
и `TestFunc.dll` всегда оказываются в одном каталоге — и при сборке решением,
и при сборке проекта по отдельности (LoadLibraryA находит библиотеку).

## Что проверяет каждая лаба

- **ScreenBuffer** — атрибуты консольного буфера (WriteConsoleOutputAttribute),
  вывод файла в отдельный буфер экрана.
- **VirtualMemory** — межпроцессный обмен через разделяемую память:
  parent пишет в адресное пространство child (WriteProcessMemory), читает ответ
  (ReadProcessMemory); синхронизация событиями, печать в общую консоль —
  через именованный мьютекс (без слипания вывода), child ждёт подтверждения
  чтения ответа (иначе его адресное пространство уничтожается раньше —
  ERROR_PARTIAL_COPY 299).
- **DllLab** — загрузка DLL в рантайме: LoadLibraryA, GetProcAddress,
  FreeLibrary; обработка ошибок (нет файла, нет функции).

## Примечание про корневые копии

Корневые `main_windows.c` в `ScreenBuffer/`, `VirtualMemory/child/` и
`VirtualMemory/parent/` — точные дубли версий из папок `*Win/`.
Правки вносите в версии из `*Win/`, затем синхронизируйте копии.

> Совместимость: проекты используют набор инструментов **v143** (Visual Studio 2022).
