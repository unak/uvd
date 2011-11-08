/*
 * Copyright (c) 2011  NAKAMURA Usaku <usa@garbagecollect.jp>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#define UNICODE
#include <windows.h>
#include <map>
#include <set>

using namespace std;


static const WCHAR WINDOW_CLASS[] = L"uvd:U'sa:2011";

static const int TIMER_POLLING = 1;
static const int POLLING_ELAPSE = 250; // ms
static const int RESTORE_DELAY = 250; // ms

static const WCHAR TIP_FORMAT[] = L"Desktop %"; // タスクトレイアイコンのチップヘルプフォーマット

static const int WM_USER_TASKTRAY = WM_USER + 0; // タスクトレイアイコンからの通知メッセージ

static const int ID_TASKTRAY = 1; // タスクトレイアイコンのID

static const int HOTKEY_1 = 1; // ホットキー
static const int HOTKEY_2 = 2;
static const int HOTKEY_3 = 3;
static const int HOTKEY_4 = 4;
static const int HOTKEY_CTRL_1 = 11;
static const int HOTKEY_CTRL_2 = 12;
static const int HOTKEY_CTRL_3 = 13;
static const int HOTKEY_CTRL_4 = 14;


static CRITICAL_SECTION csList; // ウィンドウリスト操作排他制御

struct WindowInfo {
    int desktop;
    UINT state;
    HWND prev; // Zオーダー保存用
    HWND next; // Zオーダー保存用

    WindowInfo(int d, UINT s, HWND p, HWND n) {
        desktop = d;
        state = s;
        prev = p;
        next = n;
    }

    WindowInfo(const WindowInfo* info) {
        desktop = info->desktop;
        state = info->state;
        prev = info->prev;
        next = info->next;
    }
};
typedef map<HWND, WindowInfo*> WindowList;
static WindowList* windowList; // ウィンドウリスト

static int currentDesktop; // 現在のデスクトップ番号

static HWND hwndThis;
static int WM_TASKBAR_CREATED;


static inline void StartTimer();
static inline void StopTimer();


//  ------------------------------------
//      タスクトレイにアイコン追加
//  ------------------------------------
static BOOL
AddTaskTrayIcon(HWND hWnd)
{
    NOTIFYICONDATA nid;
    ZeroMemory(&nid, sizeof(nid));
    nid.cbSize = sizeof(nid);
    nid.hWnd = hWnd;
    nid.uID = ID_TASKTRAY;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_USER_TASKTRAY;
    nid.hIcon = static_cast<HICON>(
        LoadImage(GetModuleHandle(0),
                  MAKEINTRESOURCE(0x100 + currentDesktop),
                  IMAGE_ICON, 16, 16,
                  LR_DEFAULTSIZE | LR_SHARED));
    wsprintf(nid.szTip, TIP_FORMAT, currentDesktop);

    while (TRUE) {
        if (Shell_NotifyIcon(NIM_ADD, &nid)) {
            break;
        }
        if (GetLastError() != ERROR_TIMEOUT) {
            return FALSE;
        }

        if (Shell_NotifyIcon(NIM_MODIFY, &nid)) {
            break;
        }

        Sleep(1000);
    }

    return TRUE;
}

//  ------------------------------------
//      タスクトレイのアイコン変更
//  ------------------------------------
static BOOL
ModTaskTrayIcon(HWND hWnd)
{
    NOTIFYICONDATA nid;
    ZeroMemory(&nid, sizeof(nid));
    nid.cbSize = sizeof(nid);
    nid.hWnd = hWnd;
    nid.uID = ID_TASKTRAY;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_USER_TASKTRAY;
    nid.hIcon = static_cast<HICON>(
        LoadImage(GetModuleHandle(0),
                  MAKEINTRESOURCE(0x100 + currentDesktop),
                  IMAGE_ICON, 16, 16,
                  LR_DEFAULTSIZE | LR_SHARED));
    wsprintf(nid.szTip, TIP_FORMAT, currentDesktop);

    return Shell_NotifyIcon(NIM_MODIFY, &nid);
}

//  ------------------------------------
//      タスクトレイからアイコン削除
//  ------------------------------------
static BOOL
RemoveTaskTrayIcon(HWND hWnd)
{
    NOTIFYICONDATA nid;
    ZeroMemory(&nid, sizeof(nid));
    nid.cbSize = sizeof(nid);
    nid.hWnd = hWnd;
    nid.uID = ID_TASKTRAY;

    return Shell_NotifyIcon(NIM_DELETE, &nid);
}

//  ------------------------------------
//      ウィンドウリスト検索
//  ------------------------------------
static inline WindowInfo*
FindFromList(HWND hWnd, const WindowList* list = windowList)
{
    WindowList::iterator p = windowList->find(hWnd);
    if (p == windowList->end()) {
        return 0;
    }
    return p->second;
}

//  ------------------------------------
//      ウィンドウリストクリア
//  ------------------------------------
static inline void
ClearList()
{
    for (WindowList::iterator p = windowList->begin(); p != windowList->end(); ++p) {
        delete p->second;
    }
    windowList->clear();
}

#if 0
#include <cstdio>
#pragma warning(disable:4996)
//  ------------------------------------
//      デバッグ用ウィンドウリストダンプ
//  ------------------------------------
static void
DumpList()
{
    FILE* fp = fopen("debug.log", "a");
    if (fp) {
        fseek(fp, 0, SEEK_END);
        fputs("-- start --\n", fp);
        for (WindowList::iterator p = windowList->begin(); p != windowList->end(); ++p) {
            char buf[1024];
            GetWindowTextA(p->first, buf, sizeof(buf));
            fprintf(fp, "%p : %d %2d %p %p %s\n", p->first, p->second->desktop, p->second->state, p->second->prev, p->second->next, buf);
        }
        fputs("--  end  --\n", fp);
        fclose(fp);
    }
}
#else
static inline void
DumpList()
{
}
#endif

//  ------------------------------------
//      「前」のウィンドウ取得
//  ------------------------------------
static inline HWND
GetWindowPrev(HWND hWnd)
{
    HWND prev = GetWindow(hWnd, GW_HWNDPREV);
    while (prev) {
        HWND root = prev;
        while (GetParent(root)) {
            root = GetParent(root);
        }
        if (root != hWnd) {
            prev = root;
            break;
        }
        prev = GetWindow(prev, GW_HWNDPREV);
    }
    return prev;
}

//  ------------------------------------
//      「次」のウィンドウ取得
//  ------------------------------------
static inline HWND
GetWindowNext(HWND hWnd)
{
    HWND next = GetWindow(hWnd, GW_HWNDNEXT);
    while (GetParent(next)) {
        next = GetParent(next);
    }
    return next;
}

//  ------------------------------------
//      グローバルにアクティブなウィンドウの取得
//  ------------------------------------
static HWND
GetGlobalActiveWindow()
{
    HWND hWnd;

    GUITHREADINFO gti;
    ZeroMemory(&gti, sizeof(gti));
    gti.cbSize = sizeof(gti);
    if (GetGUIThreadInfo(0, &gti)) {
        if (gti.flags & GUI_CARETBLINKING) {
            hWnd = gti.hwndCaret;
        }
        else {
            hWnd = gti.hwndFocus;
        }
    }
    else {
        hWnd = GetFocus();
    }

    while (GetParent(hWnd)) {
        hWnd = GetParent(hWnd);
    }

    return hWnd;
}

//  ------------------------------------
//      仮ウィンドウリスト作成コールバック
//  ------------------------------------
static BOOL CALLBACK
ListupWindows(HWND hWnd, LPARAM lParam)
{
    set<HWND>* windows = reinterpret_cast<set<HWND>*>(lParam);
    if (!GetParent(hWnd)) {
        WINDOWINFO wi;
        ZeroMemory(&wi, sizeof(wi));
        wi.cbSize = sizeof(wi);
        if (GetWindowInfo(hWnd, &wi) && (wi.dwStyle & WS_VISIBLE) &&
            !(wi.dwExStyle & (WS_EX_TOOLWINDOW | WS_EX_TOPMOST))) {
            windows->insert(hWnd);
        }
    }
    return TRUE;
}

//  ------------------------------------
//      デスクトップ変更
//  ------------------------------------
static void
ChangeDesktop(int n, HWND top = 0, BOOL timer = TRUE)
{
    if (n == currentDesktop) {
        return;
    }

    if (timer) {
        StopTimer();
    }

    EnterCriticalSection(&csList);

    // 最小化/復元アニメーションを一旦止める
    ANIMATIONINFO ai;
    ZeroMemory(&ai, sizeof(ai));
    ai.cbSize = sizeof(ai);
    BOOL changedAnimation = FALSE;
    if (SystemParametersInfo(SPI_GETANIMATION, sizeof(ai), &ai, 0)) {
        int prev = ai.iMinAnimate;
        ai.iMinAnimate = 0;
        if (SystemParametersInfo(SPI_SETANIMATION, sizeof(ai), &ai, 0)) {
            changedAnimation = TRUE;
            ai.iMinAnimate = prev;
        }
    }

    // Zオーダーを復元するため、まず表示すべきウィンドウを収集する
    WindowList tmp;
    HWND start = 0;
    for (WindowList::iterator p = windowList->begin(); p != windowList->end(); ++p) {
        if (n == 0 || p->second->desktop == n) {
            tmp[p->first] = p->second;
            if ((!start || p->first == start) && p->second->prev && FindFromList(p->second->prev)) {
                start = p->second->prev;
            }
            else if (!start || !p->second->prev) {
                start = p->first;
            }
        }
        else {
            ShowWindow(p->first, SW_MINIMIZE);
        }
    }

    // その上で、Zオーダー順に復元していく
    HWND prev = 0;
    for (HWND now = start; now; ) {
        WindowInfo* info = FindFromList(now, &tmp);
        if (!info) {
            break;
        }
        if (now == top) {
            now = info->prev;
            continue;
        }
        tmp.erase(now);
        ShowWindow(now, info->state);
        if (prev) {
            SetWindowPos(now, prev, 0, 0, 0, 0, SWP_DEFERERASE | SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOSENDCHANGING | SWP_NOSIZE);
        }
        if (!top) {
            top = now;
        }
        prev = now;
        now = info->prev;
    }
    for (WindowList::iterator p = tmp.begin(); p != tmp.end(); ++p) {
        if (p->first == top) {
            continue;
        }
        ShowWindow(p->first, p->second->state);
        if (prev) {
            SetWindowPos(p->first, prev, 0, 0, 0, 0, SWP_DEFERERASE | SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOSENDCHANGING | SWP_NOSIZE);
        }
        if (!top) {
            top = p->first;
        }
        prev = p->first;
    }
    tmp.clear();
    if (top) {
        WindowInfo* info = FindFromList(top);
        if (info) {
            ShowWindow(top, info->state);
            prev = top;
        }
    }
    if (top) {
        SetForegroundWindow(top);
    }

    // 最小化/復元アニメーションを戻す
    if (changedAnimation) {
        SystemParametersInfo(SPI_SETANIMATION, sizeof(ai), &ai, 0);
    }

    currentDesktop = n;
    LeaveCriticalSection(&csList);

    ModTaskTrayIcon(hwndThis);

    if (timer) {
        StartTimer();
    }
}

//  ------------------------------------
//      ウィンドウリスト更新
//  ------------------------------------
static void CALLBACK
CheckWindows(HWND hWnd, UINT uMsg, UINT_PTR id, DWORD dwTime)
{
    StopTimer();

    set<HWND> tmp;
    EnumWindows(ListupWindows, reinterpret_cast<LPARAM>(&tmp));

    EnterCriticalSection(&csList);
    HWND hwndNext = 0;
    int nextDesktop = 0;
    BOOL loggingForDebug = FALSE;
    for (set<HWND>::iterator p = tmp.begin(); p != tmp.end(); ++p) {
        if (*p == hWnd) {
            continue;
        }
        WINDOWPLACEMENT wp;
        ZeroMemory(&wp, sizeof(wp));
        wp.length = sizeof(wp);
        WindowInfo *info = FindFromList(*p);
        if (GetWindowPlacement(*p, &wp)) {
            if (!info) {
                (*windowList)[*p] = new WindowInfo(currentDesktop, wp.showCmd, GetWindowPrev(*p), GetWindowNext(*p));
                loggingForDebug = TRUE;
            }
            else {
                if (info->desktop == currentDesktop) {
                    info->state = wp.showCmd;
                    info->prev = GetWindowPrev(*p);
                    info->next = GetWindowNext(*p);
                }
                else {
                    if (wp.showCmd == SW_NORMAL ||
                        wp.showCmd == SW_MAXIMIZE ||
                        wp.showCmd == SW_SHOWNOACTIVATE ||
                        wp.showCmd == SW_SHOW ||
                        wp.showCmd == SW_SHOWNA ||
                        wp.showCmd == SW_RESTORE ||
                        wp.showCmd == SW_SHOWDEFAULT) {
                        hwndNext = *p;
                        nextDesktop = info->desktop;
                    }
                }
            }
        }
    }
    WindowList* newList = new WindowList();
    for (WindowList::iterator p = windowList->begin(); p != windowList->end(); ++p) {
        if (tmp.find(p->first) != tmp.end()) {
            (*newList)[p->first] = new WindowInfo(p->second);
        }
    }
    if (newList->size() != windowList->size()) {
        loggingForDebug = TRUE;
    }
    ClearList();
    delete windowList;
    windowList = newList;
    if (loggingForDebug) {
        DumpList();
    }
    LeaveCriticalSection(&csList);

    if (nextDesktop != 0) {
        ChangeDesktop(nextDesktop, hwndNext);
    }

    StartTimer();
}

//  ------------------------------------
//      ウィンドウを指定デスクトップに移動
//  ------------------------------------
static void
MoveWindowToDesktop(HWND hWnd, int n)
{
    StopTimer();

    WindowInfo* info = FindFromList(hWnd);
    if (!info || info->desktop == n) {
        return;
    }

    info->desktop = n;
    ChangeDesktop(n, hWnd, FALSE);

    StartTimer();
}

//  ------------------------------------
//      タスクトレイアイコンからのメニュー表示
//  ------------------------------------
static void
ShowMenu(HWND hWnd)
{
    HMENU hMenu = LoadMenu(GetModuleHandle(0), MAKEINTRESOURCE(0x100));
    if (!hMenu) {
        return;
    }

    HMENU hMenuPopup = GetSubMenu(hMenu, 0);

    POINT pos;
    GetCursorPos(&pos);
    TrackPopupMenu(hMenuPopup, 0, pos.x, pos.y, 0, hWnd, NULL);

    DestroyMenu(hMenu);
}

//  ------------------------------------
//      ウィンドウリスト作成タイマー開始
//  ------------------------------------
static inline void
StartTimer()
{
    SetTimer(hwndThis, TIMER_POLLING, POLLING_ELAPSE, CheckWindows);
}

//  ------------------------------------
//      ウィンドウリスト作成タイマー終了
//  ------------------------------------
static inline void
StopTimer()
{
    KillTimer(hwndThis, TIMER_POLLING);
}

//  ------------------------------------
//      Mainウィンドウプロシージャ
//  ------------------------------------
LRESULT CALLBACK
MainWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    hwndThis = hWnd;
    switch (uMsg) {
      case WM_CREATE:
        windowList = new WindowList();
        WM_TASKBAR_CREATED = RegisterWindowMessage(L"TaskbarCreated");
        InitializeCriticalSection(&csList);
        currentDesktop = 1;
        AddTaskTrayIcon(hWnd);
        StartTimer();
        return 0;

      case WM_HOTKEY:
        {
            int id = static_cast<int>(wParam);
            if (id >= HOTKEY_1 && id <= HOTKEY_4) {
                ChangeDesktop(id - HOTKEY_1 + 1);
            }
            else if (id >= HOTKEY_CTRL_1 && id <= HOTKEY_CTRL_4) {
                MoveWindowToDesktop(GetGlobalActiveWindow(), id - HOTKEY_CTRL_1 + 1);
            }
        }
        return 0;

      case WM_COMMAND:
        switch (LOWORD(wParam)) {
          case IDCLOSE:
            DestroyWindow(hWnd);
            return 0;
        }
        break;

      case WM_USER_TASKTRAY:
        switch (lParam) {
          case WM_RBUTTONUP:
            // ポップアップを出す際はSetForegroundWindow()が必要
            SetForegroundWindow(hWnd);
            ShowMenu(hWnd);
            return 0;

          default:
            if (uMsg == WM_TASKBAR_CREATED) {
                // タスクバーが再生成されたらアイコンを再表示する
                RemoveTaskTrayIcon(hWnd); // いちおう古い方を消す
                AddTaskTrayIcon(hWnd);
            }
            break;
        }
        break;

      case WM_DESTROY:
        ChangeDesktop(0, 0, FALSE);
        InitializeCriticalSection(&csList);
        ClearList();
        delete windowList;
        LeaveCriticalSection(&csList);
        RemoveTaskTrayIcon(hWnd);
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProc(hWnd, uMsg, wParam, lParam);
}

//  ------------------------------------
//      アプリケーションの初期化
//  ------------------------------------
static BOOL
InitApplication(HINSTANCE hInst)
{
    // メインウィンドウ(常に不可視)を登録
    WNDCLASS ws;
    ws.style = 0;
    ws.lpfnWndProc = MainWndProc;
    ws.cbClsExtra = 0;
    ws.cbWndExtra = 0;
    ws.hInstance = hInst;
    ws.hIcon = 0;
    ws.hCursor = 0;
    ws.hbrBackground = 0;
    ws.lpszMenuName = 0;
    ws.lpszClassName = WINDOW_CLASS;
    return RegisterClass(&ws);
}

//  ------------------------------------
//      インスタンスの初期化
//  ------------------------------------
static HWND
InitInstance(HINSTANCE hInst)
{
    return CreateWindow(WINDOW_CLASS,
                        L"uvd",
                        WS_POPUPWINDOW | WS_CAPTION,
                        CW_USEDEFAULT,
                        CW_USEDEFAULT,
                        200,
                        100,
                        0,
                        0,
                        hInst,
                        0);
}

//  ------------------------------------
//      入り口
//  ------------------------------------
int WINAPI
WinMain(HINSTANCE hInst, HINSTANCE hPrevInst, LPSTR lpCmdLine, int nCmdShow)
{
    HANDLE hMutex = CreateMutex(0, FALSE, WINDOW_CLASS);
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(hMutex);
        return 1;
    }
    else if (!hMutex) {
        return 1;
    }

    if (!hPrevInst && !InitApplication(hInst)) {
        CloseHandle(hMutex);
        return 1;
    }
    HWND hWnd = InitInstance(hInst);
    if (!hWnd) {
        CloseHandle(hMutex);
        return 1;
    }
    //ShowWindow(hWnd, nCmdShow);	// for debug
    ShowWindow(hWnd, SW_HIDE);

    RegisterHotKey(hWnd, HOTKEY_1, MOD_ALT, '1');
    RegisterHotKey(hWnd, HOTKEY_2, MOD_ALT, '2');
    RegisterHotKey(hWnd, HOTKEY_3, MOD_ALT, '3');
    RegisterHotKey(hWnd, HOTKEY_4, MOD_ALT, '4');
    RegisterHotKey(hWnd, HOTKEY_CTRL_1, MOD_ALT | MOD_CONTROL, '1');
    RegisterHotKey(hWnd, HOTKEY_CTRL_2, MOD_ALT | MOD_CONTROL, '2');
    RegisterHotKey(hWnd, HOTKEY_CTRL_3, MOD_ALT | MOD_CONTROL, '3');
    RegisterHotKey(hWnd, HOTKEY_CTRL_4, MOD_ALT | MOD_CONTROL, '4');

    MSG msg;
    while (GetMessage(&msg, 0, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    UnregisterHotKey(hWnd, HOTKEY_1);
    UnregisterHotKey(hWnd, HOTKEY_2);
    UnregisterHotKey(hWnd, HOTKEY_3);
    UnregisterHotKey(hWnd, HOTKEY_4);
    UnregisterHotKey(hWnd, HOTKEY_CTRL_1);
    UnregisterHotKey(hWnd, HOTKEY_CTRL_2);
    UnregisterHotKey(hWnd, HOTKEY_CTRL_3);
    UnregisterHotKey(hWnd, HOTKEY_CTRL_4);

    CloseHandle(hMutex);

    return static_cast<int>(msg.wParam);
}
