# Waysheet

Desktop-приложение для учёта путевых листов (C++20, Dear ImGui, SQLite).

## Что уже есть

- Русскоязычный интерфейс на вкладках:
  - `Дашборд`
  - `Автомобили`
  - `Водители`
  - `Маршруты`
  - `Путевой лист`
  - `База данных` (в конце списка)
- Управление БД из интерфейса:
  - создать новую,
  - открыть существующую,
  - сохранить копию (`Save As`),
  - закрыть текущую.
- Автоматическое открытие последней использованной БД при старте.
- Единый стиль работы со списками:
  - верхняя панель действий,
  - слева список,
  - справа редактор выделенной записи.
- Автосохранение при изменении данных.
- Для путевого листа:
  - список участков/маршрутов и редактор участка,
  - добавление с автофокусом,
  - виджет календаря для выбора даты.

## Шрифты и иконки

Проект использует локальные шрифты из каталога `fonts/`:

- `NotoSans-Regular.ttf` — основной шрифт (кириллица),
- `fa-solid-900.ttf` / `fontawesome-webfont.ttf` — иконки Font Awesome,
- `NotoEmoji-Regular.ttf` — подготовлен для emoji-символов.

CMake автоматически копирует runtime-шрифты в `build/Release/fonts`.

## Сборка (Windows, PowerShell)

```powershell
cmake -S . -B build -DWAYSHEET_ENABLE_IMGUI=ON
cmake --build build --config Release
```

Запуск:

```powershell
.\build\Release\waysheet.exe
```

## Режим без GUI

```powershell
cmake -S . -B build -DWAYSHEET_ENABLE_IMGUI=OFF
cmake --build build --config Release
```

## Стек

- C++20
- CMake 3.20+
- Dear ImGui + GLFW + OpenGL3
- SQLite3 (с fallback stub-режимом)

## Основные файлы

- [src/app.cpp](<src/app.cpp>)
- [src/database.cpp](<src/database.cpp>)
- [src/database.hpp](<src/database.hpp>)
- [CMakeLists.txt](<CMakeLists.txt>)
- [plan.md](<plan.md>)

## Ближайшие шаги

- Экспорт/печать путевых листов.
- Расширенная аналитика на дашборде.
- Дополнительные проверки валидации и регрессионные сценарии.
