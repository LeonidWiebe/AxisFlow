# Сборка AxisFlow (Qt6 + pixi + FreeCAD из исходников)

## Конфигурация найдена:
- Qt6:        C:\projects\FreeCAD111\.pixi\envs\default\Library\lib\cmake\Qt6
- Coin4:      C:\projects\FreeCAD111\.pixi\envs\default\Library\lib\Coin4.lib
- FreeCAD src: C:\projects\FreeCAD111\src
- FreeCAD build: C:\projects\FreeCAD111\build\release

---

## Шаг 1 — Открой Developer Command Prompt

Пуск → Visual Studio 2022 → **Developer Command Prompt for VS 2022**
(не PowerShell — обычный cmd с настроенным MSVC)

---

## Шаг 2 — Войди в pixi окружение

```bat
cd C:\projects\FreeCAD111
pixi shell
```

Если pixi shell не работает в cmd, используй:
```bat
pixi run cmd
```

---

## Шаг 3 — Configure + Build + Install (три команды)

```bat
cd C:\path\to\AxisFlow

cmake -B build -G "Visual Studio 17 2022" -A x64

cmake --build build --config RelWithDebInfo

cmake --install build --config RelWithDebInfo
```

Всё. CMakeLists.txt уже содержит жёстко прописанные пути под твою машину.

---

## Результат

После install файлы окажутся в:
```
%APPDATA%\FreeCAD\Mod\AxisFlow\AxisFlow.dll
%APPDATA%\FreeCAD\Mod\AxisFlow\InitGui.py
```

Запусти FreeCAD → выбери верстак AxisFlow → кнопка Activate AxisFlow.

---

## Если нужно поменять пути (другая машина)

В CMakeLists.txt три переменные вверху:
```cmake
set(PIXI_ENV      "C:/projects/FreeCAD111/.pixi/envs/default/Library")
set(FREECAD_SRC   "C:/projects/FreeCAD111/src")
set(FREECAD_BUILD "C:/projects/FreeCAD111/build/release")
```
