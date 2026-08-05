// ui.cpp — minimal Win32 DLL-picker dialog (see ui.h).

#include "ui.h"

#include <windows.h>
#include <commdlg.h>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "comdlg32.lib")

namespace ui {

namespace {

constexpr int ID_EDIT_PATH = 1000;
constexpr int ID_BTN_BROWSE = 1001;
constexpr int ID_BTN_INJECT = 1002;
constexpr int ID_BTN_CANCEL = 1003;

// Dialog state — single-use, single-threaded (launcher main thread).
struct DlgState {
    HWND        edit      = nullptr;
    std::string result;
    bool        confirmed = false;
};

static DlgState* state_from(HWND h) {
    return reinterpret_cast<DlgState*>(GetWindowLongPtrA(h, GWLP_USERDATA));
}

static void do_browse(HWND parent, DlgState* s) {
    char buf[MAX_PATH] = {};
    GetWindowTextA(s->edit, buf, MAX_PATH);

    OPENFILENAMEA ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner   = parent;
    ofn.lpstrFilter = "DLL\0*.dll\0All Files\0*.*\0";
    ofn.lpstrFile   = buf;
    ofn.nMaxFile    = MAX_PATH;
    ofn.lpstrTitle  = "Select DLL to inject";
    ofn.Flags       = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER;

    if (GetOpenFileNameA(&ofn)) {
        SetWindowTextA(s->edit, buf);
        // Move caret to end so the path tail is visible in the edit box.
        SendMessageA(s->edit, EM_SETSEL, (WPARAM)-1, (LPARAM)-1);
    }
}

static LRESULT CALLBACK wnd_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTA*>(lp);
        SetWindowLongPtrA(h, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams);
        return DefWindowProcA(h, msg, wp, lp);
    }

    DlgState* s = state_from(h);

    switch (msg) {
    case WM_COMMAND: {
        if (!s) break;
        switch (LOWORD(wp)) {
        case ID_BTN_BROWSE:
            do_browse(h, s);
            return 0;
        case ID_BTN_INJECT: {
            char buf[MAX_PATH] = {};
            GetWindowTextA(s->edit, buf, MAX_PATH);
            if (buf[0] == '\0') {
                MessageBoxA(h, "Pick a DLL first.", "Sand Launcher", MB_OK | MB_ICONWARNING);
                return 0;
            }
            DWORD attr = GetFileAttributesA(buf);
            if (attr == INVALID_FILE_ATTRIBUTES || (attr & FILE_ATTRIBUTE_DIRECTORY)) {
                MessageBoxA(h, "File not found (or is a directory).",
                            "Sand Launcher", MB_OK | MB_ICONWARNING);
                return 0;
            }
            s->result    = buf;
            s->confirmed = true;
            DestroyWindow(h);
            return 0;
        }
        case ID_BTN_CANCEL:
            DestroyWindow(h);
            return 0;
        }
        break;
    }
    case WM_CLOSE:
        DestroyWindow(h);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcA(h, msg, wp, lp);
}

static HFONT make_ui_font() {
    // Segoe UI 9pt — matches system chrome.
    LOGFONTA lf{};
    lf.lfHeight  = -12;
    lf.lfWeight  = FW_NORMAL;
    lf.lfCharSet = DEFAULT_CHARSET;
    lstrcpynA(lf.lfFaceName, "Segoe UI", LF_FACESIZE);
    return CreateFontIndirectA(&lf);
}

} // namespace

bool prompt_for_dll(const std::string& default_path, std::string& out_path) {
    DlgState state;

    HINSTANCE hi = GetModuleHandleA(nullptr);
    const char* cls = "SandInjectDlg";

    WNDCLASSA wc{};
    wc.lpfnWndProc   = wnd_proc;
    wc.hInstance     = hi;
    wc.lpszClassName = cls;
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    // Re-register tolerantly — if a prior call registered the class, reuse.
    if (!RegisterClassA(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return false;
    }

    const int W = 500, H = 165;
    const int sw = GetSystemMetrics(SM_CXSCREEN);
    const int sh = GetSystemMetrics(SM_CYSCREEN);
    const int x  = (sw - W) / 2;
    const int y  = (sh - H) / 2;

    HWND hwnd = CreateWindowExA(
        WS_EX_DLGMODALFRAME | WS_EX_TOPMOST,
        cls, "Sand Launcher - Inject",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
        x, y, W, H,
        nullptr, nullptr, hi, &state);

    if (!hwnd) return false;

    HFONT font = make_ui_font();

    auto mk = [&](const char* cls_name, const char* text, DWORD style,
                  int lx, int ly, int lw, int lh, int id, DWORD exstyle = 0) {
        HWND ch = CreateWindowExA(exstyle, cls_name, text,
                                  WS_CHILD | WS_VISIBLE | style,
                                  lx, ly, lw, lh,
                                  hwnd, (HMENU)(UINT_PTR)id, hi, nullptr);
        if (ch && font) SendMessageA(ch, WM_SETFONT, (WPARAM)font, TRUE);
        return ch;
    };

    mk("STATIC", "DLL to inject into RTSS parking:",
       SS_LEFT, 14, 12, 460, 18, 0);

    state.edit = mk("EDIT", default_path.c_str(),
                    ES_AUTOHSCROLL, 14, 34, 360, 24,
                    ID_EDIT_PATH, WS_EX_CLIENTEDGE);

    mk("BUTTON", "Browse...",
       BS_PUSHBUTTON, 382, 33, 96, 26, ID_BTN_BROWSE);

    mk("BUTTON", "Inject",
       BS_DEFPUSHBUTTON, 286, 82, 92, 28, ID_BTN_INJECT);

    mk("BUTTON", "Cancel",
       BS_PUSHBUTTON, 386, 82, 92, 28, ID_BTN_CANCEL);

    // Focus the edit box so Tab/Enter flow feels natural.
    SetFocus(state.edit);
    SendMessageA(state.edit, EM_SETSEL, 0, (LPARAM)-1);

    MSG m;
    while (GetMessageA(&m, nullptr, 0, 0)) {
        // Escape = Cancel. Catch before IsDialogMessage routes it elsewhere.
        if (m.message == WM_KEYDOWN && m.wParam == VK_ESCAPE) {
            DestroyWindow(hwnd);
            continue;
        }
        if (!IsDialogMessageA(hwnd, &m)) {
            TranslateMessage(&m);
            DispatchMessageA(&m);
        }
    }

    if (font) DeleteObject(font);

    if (state.confirmed) out_path = state.result;
    return state.confirmed;
}

} // namespace ui
