# Waysheet

Desktop-приложение для учёта путевых листов (C++20, Dear ImGui, SQLite).

## Что уже есть

- Русскоязычный интерфейс на вкладках:
  - `Дашборд`
  - `Автомобили`
  - `Водители`
  - `Маршруты`
  - `Путевой лист`
  - `Настройки` (в конце списка)
- Управление БД из интерфейса:
  - создать новую,
  - открыть существующую,
  - сохранить копию (`Save As`),
  - сделать быстрый бэкап с датой и временем в имени файла,
  - закрыть текущую.
- Настройки интерфейса:
  - переключение темы,
  - масштаб шрифта для мониторов с высоким разрешением.
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
- `fa-solid-900.ttf` — иконки Font Awesome.

CMake автоматически копирует runtime-шрифты в `build/Release/fonts`.

## Системные зависимости

### Windows

- CMake 3.20+
- C++20 compiler (MSVC/Visual Studio Build Tools)

### Arch Linux

```bash
sudo pacman -S --needed base-devel cmake sqlite glfw-wayland glfw-x11 mesa wayland xorg-xwayland
```

Проект использует `FetchContent` для GLFW, Dear ImGui и ImGuiFileDialog. При первой GUI-сборке CMake скачивает архивы с GitHub, поэтому нужен доступ к сети.

## Сборка (Windows, PowerShell)

```powershell
cmake -S . -B build
cmake --build build --config Release
```

Запуск:

```powershell
.\build\Release\waysheet.exe
```

## Сборка (Arch Linux / Linux)

GUI-сборку лучше держать в отдельном каталоге, чтобы не смешивать её с Windows build:

```bash
cmake -S . -B build-linux -DCMAKE_BUILD_TYPE=Release
cmake --build build-linux --config Release
```

Запуск:

```bash
./build-linux/waysheet
```

Установка в систему или пользовательский prefix:

```bash
cmake --install build-linux
```

Для установки без root можно указать prefix, например:

```bash
cmake --install build-linux --prefix "$HOME/.local"
```

При установке CMake копирует:

- `waysheet` в `bin/`,
- `aPlist.desktop` в `share/applications/`,
- `aPlist.svg` в `share/icons/hicolor/scalable/apps/`,
- шрифты в `share/aPList/fonts/`.

Особенности Linux-запуска:

- нужен доступ к графической сессии Wayland или X11 (`WAYLAND_DISPLAY` или `DISPLAY`);
- установленное приложение ищет шрифты рядом с build-бинарником и в install-директориях `share/aPList/fonts`;
- если запуск идет из sandbox/CI без графического окружения, GUI может завершиться на инициализации GLFW;
- локальные runtime-файлы `waysheet.db`, `recent_dbs.txt`, `ui_settings.ini`, `imgui.ini` создаются рядом с рабочим каталогом запуска и не должны попадать в git.

## Стек

- C++20
- CMake 3.20+
- Dear ImGui + GLFW + OpenGL3
- ImGuiFileDialog для выбора файлов БД
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
