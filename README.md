# FileUnlocker
Windows 文件解锁工具 - 终止占用进程并删除被锁定的文件。提供图形界面，支持浏览/拖放文件               
A Windows tool to delete files locked by other processes. Terminate locking processes and delete stubborn files with GUI.

# FileUnlocker - 文件解锁工具

本项目可以帮助您删除您在Windows操作系统上被其他进程占用的文件，可直接帮您终止引用进程并删除文件  
如果您不想查看源码，想直接使用的话请下载 `bin` 文件夹里的 `FileUnlocker.exe` 文件
This tool helps you delete files that are locked by other processes on Windows. It can terminate the locking processes and then delete the file.  
If you just want to use the tool without looking at the source code, please download `FileUnlocker.exe` from the `bin` folder.

## 使用方法 / Usage

1. 双击 `FileUnlocker.exe`，系统弹出 UAC 提示 → 点「是/Yes」  
   Double-click `FileUnlocker.exe`, when UAC prompt appears → click **Yes**.

2. 输入被锁定文件的路径（或点「Browse…」/ 直接拖放文件到窗口）  
   Enter the path of the locked file (or click **Browse…** / drag & drop the file into the window).

3. 点击「Scan」，查看占用进程列表  
   Click **Scan** to see the list of processes locking the file.

4. 选中进程 → 点「Terminate Selected」（一般情况）或「Force Kill」（无法终止时）  
   Select a process → click **Terminate Selected** (normal case) or **Force Kill** (if normal termination fails).

5. 状态栏显示"所有占用进程已终止"后，点「Delete File」  
   After the status bar shows "All locking processes terminated", click **Delete File**.

## 注意事项 / Important Notes

- 终止进程前请确认该进程不是系统关键进程  
  Before terminating a process, please make sure it is not a critical system process.

- 强制终止可能导致进程未保存的数据丢失  
  Force killing a process may cause loss of unsaved data in that process.

- 极少数受 Windows 保护的进程（如系统内核组件）即使管理员也无法终止  
  A few Windows‑protected processes (e.g., system kernel components) cannot be terminated even by an administrator.
