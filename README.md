# NekoTaskManager
一个简易的甘城猫猫风格任务管理器，**Nya~**

## 下载
 - Release
 - **NekoTaskManager(x64) v1.1** 常规版本
 - **NekoTaskManager(x64) v1.0 Beta** 添加内核驱动版本，可靠性未经测试，驱动未签名

## 项目结构

```text
NekoTaskManager/
├─ NekoTaskManager.sln
├─ NekoTaskManager.vcxproj
├─ NekoTaskManager.vcxproj.filters
├─ README.md
├─ assets/
│  └─ fonts/
├─ include/
│  ├─ App.h
│  ├─ Common.h
│  ├─ FontManager.h
│  ├─ ProcessManager.h
│  ├─ SystemMetrics.h
│  └─ Theme.h
├─ src/
│  ├─ main.cpp
│  ├─ App.cpp
│  ├─ FontManager.cpp
│  ├─ ProcessManager.cpp
│  ├─ SystemMetrics.cpp
│  └─ Theme.cpp
└─ resources/
   ├─ NekoTaskManager.rc
   ├─ app.manifest
   ├─ resource.h
   └─ neko.ico
```

## 构建方式（Visual Studio 2022）

1. 安装 **Visual Studio 2022**，勾选 **Desktop development with C++**
2. 直接打开 `NekoTaskManager.sln`
3. 选择 `Release | x64`
4. 生成并运行


## 功能问题
  - 保护进程的内核绕过（Beta版正在测试中）
  - GPU 指标基于 `\GPU Engine(*)\Utilization Percentage` 的 PDH 聚合。可能导致GPU计数器为0（好像事实就是这样）
