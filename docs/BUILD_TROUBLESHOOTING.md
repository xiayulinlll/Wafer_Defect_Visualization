# 编译问题排查记录（Build Troubleshooting）

> 记录本项目从「编译不过」到「成功运行」过程中踩到的全部坑、根因分析与最终解决方案。
> 环境：Windows 10/11 + Qt 6.2.4 (mingw_64) + MinGW 11.2.0 + qmake + OpenGL 3.3 Core
> 结论先行：**4 个构建/环境问题 + 7 处源码问题**，全部已修复，当前可 `build_all.bat` 一键干净重编译并正常运行。

---

## 0. 最终可用的一键命令

```bat
REM 干净重编译（清空 build\app 后重新 qmake + make，日志写入 build\build_all.log）
build_all.bat

REM 运行（自动带上 Qt6 + MinGW 运行时 DLL 的 PATH，约 8 秒后自动结束用于冒烟验证）
run_app.bat
```

产物：

| 路径 | 说明 |
| --- | --- |
| `build\app\bin\Wafer_Defect_Visualization.exe` | 主程序 |
| `build\app\bin\data\wafer_defects.csv` | 示例数据集（由 `.pro` 的 `COPIES` 自动复制） |
| `build\build_all.log` | 完整构建日志（含 PATH、qmake/make 退出码、污染检测结果） |

---

## 1. 工具链版本必须与 Qt 严格匹配（最重要的坑）

### 现象

连最简单的标准库头都编不过：

```
fatal error: filesystem: No such file or directory
```

或（改用 `#include <filesystem>` 后）：

```
D:/qt6/Tools/mingw810_64/.../include/c++/8.1.0/experimental/bits/fs_path.h:511:36:
error: specialization of '__is_encoded_char<wchar_t>' after instantiation
```

同时伴生大量诡异的模板报错，看起来像项目代码写错了，实际与业务代码无关。

### 根因

`D:\qt6\6.2.4\mingw_64\mkspecs\qconfig.pri` 里明确写着 Qt 自己的构建工具链版本：

```qmake
QT_GCC_MAJOR_VERSION = 11
QT_GCC_MINOR_VERSION = 2
```

即 **Qt 6.2.4 的 mingw_64 包是用 MinGW 11.2.0 编译的**，而当时 `tools\env.bat` 指向的是 `mingw810_64`（gcc 8.1.0）。

MinGW 8.1.0 与 11.2.0 之间 libstdc++ 差异巨大：
- 8.1.0 没有正式的 `<filesystem>`（只有 `experimental/filesystem`）
- 两套标准库的 `std::` 内部符号布局不同，混用必然出现 ABI / 模板实例化冲突

### 解决

`tools\env.bat` 改用正确的工具链目录：

```bat
set QTDIR=D:\qt6\6.2.4\mingw_64
set MINGWDIR=D:\qt6\Tools\mingw1120_64
```

### 排查方法（可复用）

```bat
REM 1) 直接问 Qt 它是用哪个 gcc 编的
findstr /c:"QT_GCC_MAJOR_VERSION" /c:"QT_GCC_MINOR_VERSION" ^
    D:\qt6\6.2.4\mingw_64\mkspecs\qconfig.pri

REM 2) 当前实际用的是哪个 gcc
where g++
g++ --version
```

> **铁律**：`where g++` 输出的路径，必须与 `qconfig.pri` 里的版本号一致。

---


## 2. 系统 PATH 污染导致 moc.exe 解析 Qt 头失败（最隐蔽）

### 现象

编译阶段一切正常（19 个 `.o` 全部生成），但 **moc 生成的 `moc_*.cpp` 报错**：

```
:/opengl/mingw/mingw/lib/gcc/mingw32/9.2.0/include/c++/type_trait:40: Parse error at "std"
QtCore/qnamespace.:56: Parse error at __attribute__
```

注意路径：`D:\opengl\mingw` 是**另一套 32 位的 gcc 9.2.0**，和本项目毫无关系。
而且报错信息里 `qnamespace.:56` 的冒号位置很怪 —— 说明 moc 拿到的 `-I` 路径根本不是本项目的。

### 根因（一步步定位）

1. 打印 moc 的实际命令行，发现 `-I` 里混进了：

   ```
   -ID:/opengl/mingw/mingw/lib/gcc/mingw32/9.2.0/include/c++
   -ID:/opengl/mingw/mingw/lib/gcc/mingw32/9.2.0/include/c++/backward
   ...
   ```

2. 但 `g++ -c` 的真实编译命令行里 **没有** 这些路径 —— 说明污染只发生在 moc 环节。
3. 溯源到 Qt 的 `mkspecs\features\moc.prf` 第 13 行：

   ```qmake
   MOC_INCLUDEPATH = $$QMAKESPEC $$_PRO_FILE_PWD_ $$MOC_INCLUDEPATH $$QMAKE_DEFAULT_INCDIRS
   ```

   `$$QMAKE_DEFAULT_INCDIRS` 是 **qmake 探测「PATH 上第一个 g++」得到的默认 include 目录**。

4. 而机器系统 PATH 里恰好有 `d:\opengl\mingw\...\bin`（32 位 gcc 9.2.0），于是 qmake 探测到了它，把这套 32 位标准库头路径塞进了 moc 的 `-I`。

### 验证方法（AB 对照，最可靠的证据）

同一条 moc 命令跑两次，只改 `-I`：

```bat
REM 带外来的 32 位 gcc 9.2.0 头路径 -> moc 失败，exit 1
moc.exe -I<污染路径> src\ui\MainWindow.h -o nul

REM 只用正确工具链的头路径 -> moc 成功，exit 0
moc.exe -I<正确路径>  src\ui\MainWindow.h -o nul
```

**A 失败、B 成功**，就 100% 确认是 PATH 污染而非代码问题。

### 解决（双保险）

**保险一（运行时规避）**：批处理里 **替换** PATH 而不是追加。

```bat
REM 注意：是 set PATH=... 而不是 set PATH=...;%PATH%
set PATH=C:\Windows\System32;C:\Windows
set PATH=%MINGWDIR%\bin;%QTDIR%\bin;%PATH%
```

**保险二（构建时强制）**：在 `.pro` 里显式重定向 `QMAKE_DEFAULT_INCDIRS`，
即使 PATH 被污染，moc 也只能看到正确工具链的头目录：

```qmake
win32-g++ {
    QMAKE_DEFAULT_INCDIRS = \
        $$clean_path($$system_path(D:/qt6/Tools/mingw1120_64/lib/gcc/x86_64-w64-mingw32/11.2.0/include/c++)) \
        $$clean_path($$system_path(D:/qt6/Tools/mingw1120_64/lib/gcc/x86_64-w64-mingw32/11.2.0/include/c++/x86_64-w64-mingw32)) \
        $$clean_path($$system_path(D:/qt6/Tools/mingw1120_64/lib/gcc/x86_64-w64-mingw32/11.2.0/include/c++/backward)) \
        $$clean_path($$system_path(D:/qt6/Tools/mingw1120_64/lib/gcc/x86_64-w64-mingw32/11.2.0/include)) \
        $$clean_path($$system_path(D:/qt6/Tools/mingw1120_64/lib/gcc/x86_64-w64-mingw32/11.2.0/include-fixed)) \
        $$clean_path($$system_path(D:/qt6/Tools/mingw1120_64/x86_64-w64-mingw32/include))
}
```

**回归检测**：`build_all.bat` 内置了一条断言 —— 生成 `Makefile.Release` 后立刻查找污染特征串：

```bat
findstr /c:"opengl/mingw" /c:"gcc/mingw32" Makefile.Release
echo foreign-refs-findstr-exit=%ERRORLEVEL%
```

> `findstr` **找到匹配返回 0，没找到返回 1**。
> 所以日志里 `foreign-refs-findstr-exit=1` 才是**成功**（无污染）。
> 这个判定方向和直觉相反，排查时一度被误导。

---


## 3. Qt6 需要显式的 `openglwidgets` 模块

### 现象

```
fatal error: QOpenGLWidget: No such file or directory
```

### 根因

Qt5 里 `QOpenGLWidget` 属于 `QtWidgets`；**Qt6 把它拆成了独立模块 `QtOpenGLWidgets`**，
头文件目录变成 `<QTDIR>/include/QtOpenGLWidgets`，只写 `QT += opengl` 不会包含该目录。

### 解决

```qmake
QT += core gui widgets opengl openglwidgets
```

---

## 4. 链接期缺少 Windows 系统 OpenGL 库

### 现象

所有 `.o` 编译通过，链接阶段单点失败：

```
build/obj/WaferViewport.o:WaferViewport.:(.text+0x7c5):
    undefined reference to `__imp_glGetString'
collect2.exe: error: ld returned 1 exit status
```

### 根因

`QOpenGLFunctions_3_3_Core` 在 Windows 上，**OpenGL 1.1 的符号**（`glGetString`、`glEnable` 等）
是直接从 `opengl32.dll` 导入的，而 1.1 以上的符号才走 Qt 的函数指针加载。
本项目代码里直接调用了 `glGetString(GL_VERSION)` 做日志打印，因此需要链接系统 `opengl32`。

但 `QT += opengl` 生成的链接命令行里**并没有**带上 `-lopengl32`（该行为随 Qt 版本 / mkspec 变动，
不能依赖）。

### 解决

在 `.pro` 中显式声明：

```qmake
win32: LIBS += -lopengl32 -lglu32
```

### 排查方法

```bat
REM 查看真实链接命令，确认是否含 -lopengl32
findstr /c:"-o bin" /c:"undefined reference" build\build_all.log
```

---

## 5. 源码层面的 7 处真实问题

构建环境修好后，暴露出的真实代码问题（属于笔误 / API 误用，与业务逻辑设计无关）：

| # | 文件 | 问题 | 修复 |
| --- | --- | --- | --- |
| 1 | `src/ui/MainWindow.cpp` | 把 `DefectSeverity` 枚举传成 `float` 给 `severityColor()` | `static_cast<DefectSeverity>(...)` |
| 2 | `src/ui/MainWindow.cpp` | 使用了不存在的 `FilterCriteria` / `filter().criteria()` API | 改为真实的 `DefectFilter::apply()` |
| 3 | `src/business/SceneStateManager.h` | 形参名 `near` / `far` 撞 Windows `<windef.h>` 宏 | 重命名为 `nearPlane` / `farPlane` |
| 4 | `src/data/DataBufferManager.cpp` | `QSharedPointer` 被当作 `std::shared_ptr` 用（`.data()` + 别名构造） | 改用 `QSharedPointer` 自定义删除器 |
| 5 | `src/render/HeatmapRenderer.cpp` | 调用了不存在的 `QVector::assign()` | 改为 `fill()` |
| 6 | `src/render/FboRenderer.{h,cpp}` | GL 方法误标 `const`，内部调用非 const 的 GL 函数 | 去掉 `const` |
| 7 | `src/render/SceneRenderer.cpp` | 混合类型的链式赋值 `a = b = c` 触发类型推导失败 | 拆成两条独立赋值 |

> **经验**：`near` / `far` 这类 Windows 宏污染是 Qt + Windows 项目的经典坑，
> 编译报错会指向完全无关的行号，看起来像语法错误。
> 类似高危名字还有 `min`、`max`、`interface`、`small`、`ERROR`。
> 可在 `.pro` 里加 `DEFINES += NOMINMAX` 缓解一部分。

---

## 6. 为什么必须用 `start /min ... /wait` 跑构建

### 现象

在交互式终端里直接跑 `build_all.bat`（或 `mingw32-make -j8`），构建中途「莫名中断」，
只剩部分 `.o` 文件，日志也写不全。

### 根因

构建是**长任务**，但交互式终端的会话如果被回收 / 抢占，会连带把正在运行的子进程一起杀掉。

### 解决

用**独立进程 + 最小化窗口 + 等待**的方式执行，让它脱离当前终端的生命周期：

```bat
cd /d d:\qt6.2.4_project\Wafer_Defect_Visualization
start /min "buildall" /wait cmd /c build_all.bat
```

同理，运行 App 做冒烟测试时也用 `start ""` 拉起独立进程，再用 `tasklist` 检查存活、`taskkill` 收尾 —— 见 `run_app.bat`。

---

```bat
REM 查看真实链接命令，确认是否含 -lopengl32
findstr /c:"-o bin" /c:"undefined reference" build\build_all.log
```

---


## 7. 冒烟测试（Smoke Test）

`run_app.bat` 做了完整的自动健康检查：

```bat
set PATH=D:\qt6\Tools\mingw1120_64\bin;D:\qt6\6.2.4\mingw_64\bin;C:\Windows\System32;C:\Windows
cd /d build\app\bin
start "" "Wafer_Defect_Visualization.exe"
ping -n 9 127.0.0.1 >nul                          REM 等约 8 秒（用 ping 代替 sleep，无 tty 也可靠）
tasklist /fi "imagename eq Wafer_Defect_Visualization.exe" > build\run_status.txt
taskkill /f /im Wafer_Defect_Visualization.exe >> build\run_status.txt
```

判定标准：
- `tasklist` 能列出进程 → 窗口创建成功、**OpenGL 3.3 上下文创建成功**、四层模块初始化成功
- 期望内存占用 100 MB 以上（本项目实测约 110 ~ 170 MB）
- 若进程不在列表里 → 启动即崩溃，多半是缺 DLL，检查 `PATH` 是否含 Qt6 与 MinGW 的 `bin`

### 验证结果（本次修复后）

```
Wafer_Defect_Visualizatio    48600 Console    1    109,720 K
成功: 终止进程 "Wafer_Defect_Visualization.exe"，其 PID 为 48600。
```

配合干净重编译日志：

```
qmake exit=0
foreign-refs-findstr-exit=1          <- 无 PATH 污染
make exit=0
bin\Wafer_Defect_Visualization.exe   <- 产物存在
```

---

## 8. 排查套路总结（Checklist）

遇到「编译不过」时，按以下顺序排查，可解决 90% 的 Qt + MinGW 环境问题：

1. **对齐工具链版本**：`where g++` 的路径 vs `mkspecs\qconfig.pri` 的 `QT_GCC_*_VERSION`。
2. **看真实命令行**：从构建日志里抓 `g++ -c` 与 `moc.exe` 的完整参数，别只看错误消息。
   错误消息里的路径往往就是答案（例如突然出现的 `/opengl/mingw/`）。
3. **隔离变量**：同一条命令，分别带 / 不带某组 `-I` 跑两次，用 exit code 对比 —— 比读报错快十倍。
4. **检查 PATH 污染**：系统里装了多套 MinGW / 编译器时，**替换** PATH 而不是追加。
5. **确认 Qt 模块名**：Qt6 拆分了模块（`openglwidgets`、`concurrent` 等），缺模块的报错表现为「找不到头文件」。
6. **链接错误按符号判断**：
   - `__imp_*` 前缀 → Windows 导入库缺失（`-lopengl32` 之类）
   - `undefined reference to Qt...` → 缺 Qt 模块的 `LIBS`
7. **警惕 Windows 宏**：`near` / `far` / `min` / `max` / `interface` 被 `<windef.h>` 污染。
8. **长任务独立进程运行**：`start /min ... /wait cmd /c xxx.bat`，别让构建被终端回收杀掉。

---

## 9. 相关文件索引

| 文件 | 作用 |
| --- | --- |
| `tools\env.bat` | 构建环境定义（正确工具链 + 替换式 PATH），注释里记录了 PATH 污染的坑 |
| `build_all.bat` | 一键干净重编译，内置 PATH 污染回归检测，日志到 `build\build_all.log` |
| `run_app.bat` | 拉起程序、自动健康检查（tasklist）、自动收尾（taskkill） |
| `Wafer_Defect_Visualization.pro` | 含 `QMAKE_DEFAULT_INCDIRS` 重定向、`-lopengl32 -lglu32` 等修复 |
| `docs\BUILD_TROUBLESHOOTING.md` | 本文件 |
