/*
 * File Unlocker (C++ / Win32)
 * Finds and kills processes locking a file, then deletes it.
 *
 * Build (MinGW):
 *   windres resource.rc -O coff -o resource.res
 *   g++ -std=c++17 -O2 -mwindows -municode -DUNICODE -D_UNICODE
 *       -o FileUnlocker.exe main.cpp resource.res
 *       -lrstrtmgr -lpsapi -lcomctl32 -lcomdlg32 -lshell32
 */

#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <psapi.h>
#include <restartmanager.h>
#include <shellapi.h>
#include <string>
#include <vector>

#pragma comment(lib, "rstrtmgr.lib")
#pragma comment(lib, "psapi.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(linker, "\"/manifestdependency:type='win32' \
name='Microsoft.Windows.Common-Controls' version='6.0.0.0' \
processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

// ── Control IDs ──────────────────────────────
#define IDC_EDIT_PATH       101
#define IDC_BTN_BROWSE      102
#define IDC_BTN_SCAN        103
#define IDC_LIST            104
#define IDC_BTN_KILL        105
#define IDC_BTN_KILL_FORCE  106
#define IDC_BTN_DELETE      107
#define IDC_STATUS          108
#define IDC_LBL_PATH        109
#define IDC_LBL_HINT        110
#define IDC_BTN_REFRESH     111

// ── Colors ───────────────────────────────────
#define CLR_BG       RGB(15,  17,  23)
#define CLR_SURFACE  RGB(26,  29,  39)
#define CLR_BORDER   RGB(42,  45,  62)
#define CLR_ACCENT   RGB(79, 142, 247)
#define CLR_RED      RGB(224, 92,  92)
#define CLR_TEXT     RGB(232, 234, 240)
#define CLR_SUBTEXT  RGB(124, 128, 160)
#define CLR_SUCCESS  RGB(76,  175, 130)
#define CLR_WARNING  RGB(240, 160,  74)

// ── Process Info ─────────────────────────────
struct ProcInfo {
    DWORD        pid;
    std::wstring name;
    std::wstring user;
    std::wstring appType;
};

// ── Globals ──────────────────────────────────
HWND  g_hWnd    = nullptr;
HWND  g_hPath   = nullptr;
HWND  g_hList   = nullptr;
HWND  g_hStatus = nullptr;
HWND  g_hKill   = nullptr;
HWND  g_hKillF  = nullptr;
HWND  g_hDelete = nullptr;
HFONT g_hFontUI   = nullptr;
HFONT g_hFontMono = nullptr;
HFONT g_hFontBold = nullptr;
HBRUSH g_hBrushBG  = nullptr;
HBRUSH g_hBrushSurf= nullptr;
COLORREF g_statusColor = CLR_SUBTEXT;
std::vector<ProcInfo> g_procs;

#define WM_SCAN_DONE (WM_USER + 1)

// ── Get process owner ─────────────────────────
static std::wstring GetProcessUser(DWORD pid) {
    HANDLE hProc = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, pid);
    if (!hProc) return L"N/A";
    HANDLE hTok = nullptr;
    if (!OpenProcessToken(hProc, TOKEN_QUERY, &hTok)) {
        CloseHandle(hProc);
        return L"N/A";
    }
    DWORD sz = 0;
    GetTokenInformation(hTok, TokenUser, nullptr, 0, &sz);
    std::vector<BYTE> buf(sz);
    std::wstring result = L"N/A";
    if (GetTokenInformation(hTok, TokenUser, buf.data(), sz, &sz)) {
        auto* pu = reinterpret_cast<PTOKEN_USER>(buf.data());
        WCHAR name[256]={}, dom[256]={};
        DWORD nLen=256, dLen=256;
        SID_NAME_USE use;
        if (LookupAccountSidW(nullptr, pu->User.Sid, name, &nLen, dom, &dLen, &use))
            result = std::wstring(dom) + L"\\" + name;
    }
    CloseHandle(hTok);
    CloseHandle(hProc);
    return result;
}

// ── Find locking processes via Restart Manager ─
static std::vector<ProcInfo> FindLockingProcesses(const std::wstring& path) {
    std::vector<ProcInfo> result;
    DWORD session = 0;
    WCHAR key[CCH_RM_SESSION_KEY+1] = {};
    if (RmStartSession(&session, 0, key) != ERROR_SUCCESS) return result;

    LPCWSTR files[] = { path.c_str() };
    if (RmRegisterResources(session, 1, files, 0, nullptr, 0, nullptr) != ERROR_SUCCESS) {
        RmEndSession(session);
        return result;
    }

    DWORD reason = 0;
    UINT  needed = 0, got = 0;
    RmGetList(session, &needed, &got, nullptr, &reason);
    if (needed > 0) {
        std::vector<RM_PROCESS_INFO> info(needed);
        got = needed;
        if (RmGetList(session, &needed, &got, info.data(), &reason) == ERROR_SUCCESS) {
            for (UINT i = 0; i < got; i++) {
                ProcInfo pi;
                pi.pid  = info[i].Process.dwProcessId;
                pi.name = info[i].strAppName[0] ? info[i].strAppName : L"(unknown)";

                // Try to get real exe name
                HANDLE hP = OpenProcess(
                    PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pi.pid);
                if (hP) {
                    WCHAR exePath[MAX_PATH] = {};
                    if (GetModuleFileNameExW(hP, nullptr, exePath, MAX_PATH)) {
                        std::wstring ep(exePath);
                        auto pos = ep.rfind(L'\\');
                        if (pos != std::wstring::npos)
                            pi.name = ep.substr(pos + 1);
                    }
                    CloseHandle(hP);
                }

                pi.user = GetProcessUser(pi.pid);

                switch (info[i].ApplicationType) {
                    case RmUnknownApp:  pi.appType = L"Unknown";   break;
                    case RmMainWindow:  pi.appType = L"Desktop";   break;
                    case RmOtherWindow: pi.appType = L"Window";    break;
                    case RmService:     pi.appType = L"Service";   break;
                    case RmExplorer:    pi.appType = L"Explorer";  break;
                    case RmConsole:     pi.appType = L"Console";   break;
                    case RmCritical:    pi.appType = L"[Critical]";break;
                    default:            pi.appType = L"-";         break;
                }
                result.push_back(pi);
            }
        }
    }
    RmEndSession(session);
    return result;
}

// ── Kill a process ────────────────────────────
static bool KillProc(DWORD pid) {
    HANDLE h = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
    if (!h) return false;
    bool ok = TerminateProcess(h, 1) != FALSE;
    WaitForSingleObject(h, 3000);
    CloseHandle(h);
    return ok;
}

// ── Status bar ───────────────────────────────
static void SetStatus(const wchar_t* msg, COLORREF color) {
    g_statusColor = color;
    SetWindowTextW(g_hStatus, msg);
    InvalidateRect(g_hStatus, nullptr, TRUE);
}

// ── Populate ListView ─────────────────────────
static void PopulateList() {
    ListView_DeleteAllItems(g_hList);
    for (int i = 0; i < (int)g_procs.size(); i++) {
        const auto& p = g_procs[i];
        LVITEM lvi = {};
        lvi.mask     = LVIF_TEXT;
        lvi.iItem    = i;
        std::wstring pidStr = std::to_wstring(p.pid);
        lvi.pszText  = const_cast<LPWSTR>(pidStr.c_str());
        ListView_InsertItem(g_hList, &lvi);
        ListView_SetItemText(g_hList, i, 1, const_cast<LPWSTR>(p.name.c_str()));
        ListView_SetItemText(g_hList, i, 2, const_cast<LPWSTR>(p.user.c_str()));
        ListView_SetItemText(g_hList, i, 3, const_cast<LPWSTR>(p.appType.c_str()));
    }
}

// ── Scan thread ───────────────────────────────
struct ScanParam { HWND hWnd; std::wstring path; };

static DWORD WINAPI ScanThread(LPVOID lp) {
    auto* param = reinterpret_cast<ScanParam*>(lp);
    auto* result = new std::vector<ProcInfo>(FindLockingProcesses(param->path));
    PostMessageW(param->hWnd, WM_SCAN_DONE, 0, reinterpret_cast<LPARAM>(result));
    delete param;
    return 0;
}

static void StartScan() {
    WCHAR buf[MAX_PATH*2] = {};
    GetWindowTextW(g_hPath, buf, MAX_PATH*2);
    std::wstring path = buf;
    // strip surrounding quotes (drag-drop)
    if (!path.empty() && path.front()==L'"') path.erase(0,1);
    if (!path.empty() && path.back() ==L'"') path.pop_back();

    if (path.empty()) {
        SetStatus(L"[!] Please enter a file path first.", CLR_WARNING);
        return;
    }
    if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) {
        SetStatus(L"[!] File not found. Check the path.", CLR_WARNING);
        return;
    }

    SetStatus(L"Scanning...", CLR_SUBTEXT);
    ListView_DeleteAllItems(g_hList);
    EnableWindow(g_hKill,  FALSE);
    EnableWindow(g_hKillF, FALSE);

    auto* p = new ScanParam{ g_hWnd, path };
    HANDLE hT = CreateThread(nullptr, 0, ScanThread, p, 0, nullptr);
    if (hT) CloseHandle(hT);
}

static void BrowseFile() {
    WCHAR buf[MAX_PATH*2] = {};
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner   = g_hWnd;
    ofn.lpstrFilter = L"All Files\0*.*\0\0";
    ofn.lpstrFile   = buf;
    ofn.nMaxFile    = MAX_PATH*2;
    ofn.lpstrTitle  = L"Select the locked file";
    ofn.Flags       = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (GetOpenFileNameW(&ofn)) {
        SetWindowTextW(g_hPath, buf);
        StartScan();
    }
}

static void KillSelected(bool force) {
    int count = ListView_GetItemCount(g_hList);
    std::vector<int> sel;
    for (int i = 0; i < count; i++)
        if (ListView_GetItemState(g_hList,i,LVIS_SELECTED)&LVIS_SELECTED)
            sel.push_back(i);
    if (sel.empty()) return;

    std::wstring names;
    for (int i : sel) {
        WCHAR tmp[256]={};
        ListView_GetItemText(g_hList,i,1,tmp,256);
        names += L"  - ";
        names += tmp;
        names += L"\n";
    }
    std::wstring msg = (force ? L"Force-kill" : L"Terminate");
    msg += L" the following processes?\n\n" + names;
    if (MessageBoxW(g_hWnd, msg.c_str(), L"Confirm", MB_YESNO|MB_ICONWARNING) != IDYES)
        return;

    bool anyFailed = false;
    for (int i = (int)sel.size()-1; i >= 0; i--) {
        int idx = sel[i];
        if (idx < (int)g_procs.size()) {
            if (KillProc(g_procs[idx].pid)) {
                g_procs.erase(g_procs.begin()+idx);
                ListView_DeleteItem(g_hList, idx);
            } else {
                anyFailed = true;
            }
        }
    }

    if (anyFailed) {
        MessageBoxW(g_hWnd,
            L"Some processes could not be terminated.\n"
            L"Try 'Force Kill', or run as Administrator.",
            L"Partial Failure", MB_OK|MB_ICONWARNING);
    } else {
        MessageBoxW(g_hWnd, L"Selected processes terminated successfully.", L"Done", MB_OK|MB_ICONINFORMATION);
    }

    int remain = ListView_GetItemCount(g_hList);
    if (remain == 0) {
        SetStatus(L"[OK] All locking processes terminated. You can now delete the file.", CLR_SUCCESS);
        EnableWindow(g_hKill,  FALSE);
        EnableWindow(g_hKillF, FALSE);
    } else {
        std::wstring s = std::to_wstring(remain);
        s = L"[!] " + s + L" process(es) still locking the file.";
        SetStatus(s.c_str(), CLR_WARNING);
    }
}

static void DoDeleteFile() {
    WCHAR buf[MAX_PATH*2]={};
    GetWindowTextW(g_hPath, buf, MAX_PATH*2);
    std::wstring path = buf;
    if (!path.empty()&&path.front()==L'"') path.erase(0,1);
    if (!path.empty()&&path.back() ==L'"') path.pop_back();

    if (path.empty() || GetFileAttributesW(path.c_str())==INVALID_FILE_ATTRIBUTES) {
        MessageBoxW(g_hWnd, L"File not found.", L"Warning", MB_OK|MB_ICONWARNING);
        return;
    }
    if (ListView_GetItemCount(g_hList) > 0) {
        MessageBoxW(g_hWnd,
            L"There are still processes locking this file.\n"
            L"Please terminate them first.",
            L"Warning", MB_OK|MB_ICONWARNING);
        return;
    }
    std::wstring q = L"Permanently delete this file?\n\n" + path;
    if (MessageBoxW(g_hWnd, q.c_str(), L"Confirm Delete", MB_YESNO|MB_ICONQUESTION)!=IDYES)
        return;

    if (DeleteFileW(path.c_str())) {
        MessageBoxW(g_hWnd, L"File deleted successfully!", L"Success", MB_OK|MB_ICONINFORMATION);
        SetWindowTextW(g_hPath, L"");
        SetStatus(L"[OK] File deleted.", CLR_SUCCESS);
    } else {
        DWORD err = GetLastError();
        std::wstring e = L"Delete failed. Error code: " + std::to_wstring(err);
        MessageBoxW(g_hWnd, e.c_str(), L"Error", MB_OK|MB_ICONERROR);
    }
}

// ── Create a child window helper ─────────────
static HWND MakeCtrl(const wchar_t* cls, const wchar_t* txt,
                     DWORD style, int x, int y, int w, int h, UINT id, HWND hParent) {
    return CreateWindowExW(0, cls, txt, WS_CHILD|WS_VISIBLE|style,
                           x, y, w, h, hParent, (HMENU)(UINT_PTR)id, nullptr, nullptr);
}

// ── Window procedure ─────────────────────────
static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {

    case WM_CREATE: {
        g_hBrushBG   = CreateSolidBrush(CLR_BG);
        g_hBrushSurf = CreateSolidBrush(CLR_SURFACE);

        g_hFontUI   = CreateFontW(-13,0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,0,0,
                                   CLEARTYPE_QUALITY,0,L"Segoe UI");
        g_hFontMono = CreateFontW(-12,0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,0,0,
                                   CLEARTYPE_QUALITY,0,L"Consolas");
        g_hFontBold = CreateFontW(-14,0,0,0,FW_SEMIBOLD,0,0,0,DEFAULT_CHARSET,0,0,
                                   CLEARTYPE_QUALITY,0,L"Segoe UI");

        // Path label
        HWND hLbl = MakeCtrl(L"STATIC", L"File Path:", SS_LEFT, 20,80,68,20, IDC_LBL_PATH, hWnd);
        SendMessageW(hLbl, WM_SETFONT, (WPARAM)g_hFontUI, TRUE);

        // Path edit
        g_hPath = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                    WS_CHILD|WS_VISIBLE|ES_AUTOHSCROLL,
                    92, 76, 474, 28, hWnd, (HMENU)IDC_EDIT_PATH, nullptr, nullptr);
        SendMessageW(g_hPath, WM_SETFONT, (WPARAM)g_hFontMono, TRUE);

        // Buttons top
        auto MakeBtn = [&](const wchar_t* t, int x, int y, int w, int h, UINT id) {
            HWND hB = MakeCtrl(L"BUTTON", t, BS_PUSHBUTTON, x,y,w,h, id, hWnd);
            SendMessageW(hB, WM_SETFONT, (WPARAM)g_hFontUI, TRUE);
            return hB;
        };
        MakeBtn(L"Browse...", 572, 76,  72, 28, IDC_BTN_BROWSE);
        MakeBtn(L"Scan",      650, 76,  70, 28, IDC_BTN_SCAN);

        // Status
        g_hStatus = MakeCtrl(L"STATIC",
            L"Enter or drag a file path, then click [Scan].",
            SS_LEFT, 20, 114, 720, 18, IDC_STATUS, hWnd);
        SendMessageW(g_hStatus, WM_SETFONT, (WPARAM)g_hFontUI, TRUE);

        // ListView
        g_hList = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEW, L"",
            WS_CHILD|WS_VISIBLE|LVS_REPORT|LVS_SHOWSELALWAYS,
            20, 140, 744, 282, hWnd, (HMENU)IDC_LIST, nullptr, nullptr);
        SendMessageW(g_hList, WM_SETFONT, (WPARAM)g_hFontUI, TRUE);
        ListView_SetExtendedListViewStyle(g_hList,
            LVS_EX_FULLROWSELECT|LVS_EX_GRIDLINES|LVS_EX_DOUBLEBUFFER);

        struct { const wchar_t* name; int w; } cols[] = {
            {L"PID",72},{L"Process Name",220},{L"User",196},{L"Type",120}
        };
        for (int i = 0; i < 4; i++) {
            LVCOLUMNW c={};
            c.mask=LVCF_TEXT|LVCF_WIDTH|LVCF_FMT;
            c.fmt=(i==0)?LVCFMT_CENTER:LVCFMT_LEFT;
            c.cx=cols[i].w;
            c.pszText=const_cast<LPWSTR>(cols[i].name);
            ListView_InsertColumn(g_hList,i,&c);
        }

        // Bottom buttons
        g_hKill  = MakeBtn(L"[X] Terminate Selected", 20,  436, 160, 32, IDC_BTN_KILL);
        g_hKillF = MakeBtn(L"[!!] Force Kill",        188, 436, 120, 32, IDC_BTN_KILL_FORCE);
        g_hDelete= MakeBtn(L"[D] Delete File",        316, 436, 110, 32, IDC_BTN_DELETE);
        MakeBtn(L"Refresh",                            680, 436,  80, 32, IDC_BTN_REFRESH);

        HWND hHint = MakeCtrl(L"STATIC",
            L"Run as Administrator to unlock more processes.",
            SS_LEFT, 20, 476, 500, 18, IDC_LBL_HINT, hWnd);
        SendMessageW(hHint, WM_SETFONT, (WPARAM)g_hFontUI, TRUE);

        EnableWindow(g_hKill,  FALSE);
        EnableWindow(g_hKillF, FALSE);
        return 0;
    }

    case WM_ERASEBKGND: {
        HDC hdc = (HDC)wParam;
        RECT rc; GetClientRect(hWnd, &rc);
        FillRect(hdc, &rc, g_hBrushBG);
        // Header bar
        RECT hdr = {0,0,rc.right,60};
        FillRect(hdc, &hdr, g_hBrushSurf);
        // Separator line
        RECT sep = {0,60,rc.right,61};
        HBRUSH hLine = CreateSolidBrush(CLR_BORDER);
        FillRect(hdc, &sep, hLine);
        DeleteObject(hLine);
        // Title text
        HFONT old = (HFONT)SelectObject(hdc, g_hFontBold);
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, CLR_TEXT);
        RECT tr={20,14,500,40};
        DrawTextW(hdc, L"File Unlocker  -  Find & Kill File-Locking Processes", -1, &tr, DT_LEFT|DT_SINGLELINE);
        SelectObject(hdc, old);
        return 1;
    }

    case WM_CTLCOLORSTATIC: {
        HDC hdc=(HDC)wParam; HWND hCtl=(HWND)lParam;
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, (hCtl==g_hStatus) ? g_statusColor : CLR_SUBTEXT);
        return (LRESULT)g_hBrushBG;
    }

    case WM_CTLCOLOREDIT: {
        HDC hdc=(HDC)wParam;
        SetBkColor(hdc, CLR_SURFACE);
        SetTextColor(hdc, CLR_TEXT);
        return (LRESULT)g_hBrushSurf;
    }

    case WM_PAINT: { PAINTSTRUCT ps; BeginPaint(hWnd,&ps); EndPaint(hWnd,&ps); return 0; }

    case WM_NOTIFY: {
        auto* pnm = reinterpret_cast<NMHDR*>(lParam);
        if (pnm->hwndFrom==g_hList && pnm->code==LVN_ITEMCHANGED) {
            int s = ListView_GetSelectedCount(g_hList);
            EnableWindow(g_hKill,  s>0);
            EnableWindow(g_hKillF, s>0);
        }
        return 0;
    }

    case WM_COMMAND: {
        switch (LOWORD(wParam)) {
            case IDC_BTN_BROWSE:     BrowseFile();        break;
            case IDC_BTN_SCAN:
            case IDC_BTN_REFRESH:    StartScan();         break;
            case IDC_BTN_KILL:       KillSelected(false); break;
            case IDC_BTN_KILL_FORCE: KillSelected(true);  break;
            case IDC_BTN_DELETE:     DoDeleteFile();      break;
        }
        return 0;
    }

    case WM_SCAN_DONE: {
        auto* res = reinterpret_cast<std::vector<ProcInfo>*>(lParam);
        g_procs = *res;
        delete res;
        PopulateList();
        if (g_procs.empty()) {
            SetStatus(L"[OK] No process is locking this file. You can delete it freely.", CLR_SUCCESS);
        } else {
            std::wstring s = L"[!] " + std::to_wstring(g_procs.size()) + L" process(es) locking this file.";
            SetStatus(s.c_str(), CLR_WARNING);
        }
        EnableWindow(g_hKill,  FALSE);
        EnableWindow(g_hKillF, FALSE);
        return 0;
    }

    case WM_SIZE: {
        int w=LOWORD(lParam), h=HIWORD(lParam);
        if (g_hPath)   SetWindowPos(g_hPath,  nullptr, 92,  76, w-252, 28,  SWP_NOZORDER);
        if (g_hList)   SetWindowPos(g_hList,  nullptr, 20,  140,w- 36, h-206,SWP_NOZORDER);
        HWND hBr=GetDlgItem(hWnd,IDC_BTN_BROWSE);
        HWND hSc=GetDlgItem(hWnd,IDC_BTN_SCAN);
        HWND hRf=GetDlgItem(hWnd,IDC_BTN_REFRESH);
        if(hBr) SetWindowPos(hBr,nullptr,w-164,76, 72,28,SWP_NOZORDER);
        if(hSc) SetWindowPos(hSc,nullptr,w- 88,76, 70,28,SWP_NOZORDER);
        int by=h-62;
        if(g_hKill)   SetWindowPos(g_hKill,  nullptr, 20, by,160,32,SWP_NOZORDER);
        if(g_hKillF)  SetWindowPos(g_hKillF, nullptr,188, by,120,32,SWP_NOZORDER);
        if(g_hDelete) SetWindowPos(g_hDelete, nullptr,316, by,110,32,SWP_NOZORDER);
        if(hRf)       SetWindowPos(hRf,       nullptr,w-96,by, 80,32,SWP_NOZORDER);
        HWND hHint=GetDlgItem(hWnd,IDC_LBL_HINT);
        if(hHint) SetWindowPos(hHint,nullptr,20,h-32,500,18,SWP_NOZORDER);
        InvalidateRect(hWnd,nullptr,TRUE);
        return 0;
    }

    case WM_DROPFILES: {
        HDROP hDrop=(HDROP)wParam;
        WCHAR buf[MAX_PATH*2]={};
        if (DragQueryFileW(hDrop,0,buf,MAX_PATH*2)) {
            SetWindowTextW(g_hPath,buf);
            StartScan();
        }
        DragFinish(hDrop);
        return 0;
    }

    case WM_DESTROY:
        DeleteObject(g_hFontUI); DeleteObject(g_hFontMono); DeleteObject(g_hFontBold);
        DeleteObject(g_hBrushBG); DeleteObject(g_hBrushSurf);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

// ── WinMain ───────────────────────────────────
int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int nShow) {
    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_LISTVIEW_CLASSES };
    InitCommonControlsEx(&icc);

    WNDCLASSEXW wc = {};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW|CS_VREDRAW;
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInst;
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = CreateSolidBrush(CLR_BG);
    wc.lpszClassName = L"FileUnlocker";
    wc.hIcon         = LoadIcon(nullptr, IDI_APPLICATION);
    RegisterClassExW(&wc);

    g_hWnd = CreateWindowExW(
        WS_EX_ACCEPTFILES,
        L"FileUnlocker",
        L"File Unlocker - Find & Kill Locking Processes",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 784, 530,
        nullptr, nullptr, hInst, nullptr);

    ShowWindow(g_hWnd, nShow);
    UpdateWindow(g_hWnd);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return (int)msg.wParam;
}
