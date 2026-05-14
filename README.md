# NekoTaskManager
一个简易的甘城猫猫风格任务管理器，**Nya~**


## 下载

### 下载地址

- **NekoTaskManager(x64) v1.2.0**（Latest 最新版）  
  [📥 前往下载页面](https://github.com/NeuroSaki987/NekoTaskManager/releases/tag/v1.2.0)

- **NekoTaskManager(x64) v1.0 Beta**（内核驱动版本）  
  功能一般，经过稳定性测试  
  [📥 前往下载页面](https://github.com/NeuroSaki987/NekoTaskManager/releases/tag/v1.1_Test)

- **NekoTaskManager(x64) v1.3.5 test**（内核驱动版本）  
  功能强大，但驱动为屎山代码，极易崩溃，仅供测试使用  
  [📥 前往下载页面](https://github.com/NeuroSaki987/NekoTaskManager/releases/tag/v1.3.5_test)
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
  - 驱动设计问题
  - 由于安全性和防滥用，暂不提供驱动源码

