#include "chat_window.h"
#include <commctrl.h>

using namespace chatproto;

int ChatWindow::run(HINSTANCE hInst, int nCmdShow) {
    const wchar_t CLASS_NAME[] = L"ChatClientWin32OOP";
    WNDCLASSW wc{};
    wc.lpfnWndProc = ChatWindow::WndProcThunk;
    wc.hInstance = hInst;
    wc.lpszClassName = CLASS_NAME;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);

    RegisterClassW(&wc);

    HWND hwnd = CreateWindowExW(0, CLASS_NAME, L"Chat Client", WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 640, 420,
        nullptr, nullptr, hInst, this);

    ShowWindow(hwnd, nCmdShow);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return 0;
}

LRESULT CALLBACK ChatWindow::WndProcThunk(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    ChatWindow* self = nullptr;
    if (msg == WM_NCCREATE) {
        auto cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = static_cast<ChatWindow*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)self);
        self->hwnd_ = hwnd;
    } else {
        self = reinterpret_cast<ChatWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    if (!self) return DefWindowProcW(hwnd, msg, wParam, lParam);
    return self->WndProc(hwnd, msg, wParam, lParam);
}

LRESULT ChatWindow::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        HFONT hFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
        // Address, Port, Nick, Connect
        hAddr_ = CreateWindowW(L"EDIT", L"127.0.0.1", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
            10, 10, 180, 24, hwnd, (HMENU)IDC_ADDR, GetModuleHandleW(nullptr), nullptr);
        hPort_ = CreateWindowW(L"EDIT", L"5000", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
            200, 10, 60, 24, hwnd, (HMENU)IDC_PORT, GetModuleHandleW(nullptr), nullptr);
        hNick_ = CreateWindowW(L"EDIT", L"User", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
            270, 10, 120, 24, hwnd, (HMENU)IDC_NICK, GetModuleHandleW(nullptr), nullptr);
        hConnect_ = CreateWindowW(L"BUTTON", L"连接", WS_CHILD | WS_VISIBLE,
            400, 10, 70, 24, hwnd, (HMENU)IDC_CONNECT, GetModuleHandleW(nullptr), nullptr);

        // Chat log
        hChat_ = CreateWindowW(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY | WS_VSCROLL,
            10, 44, 460, 260, hwnd, (HMENU)IDC_CHATLOG, GetModuleHandleW(nullptr), nullptr);
        // Input
        hInput_ = CreateWindowW(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL | ES_WANTRETURN,
            10, 310, 380, 24, hwnd, (HMENU)IDC_INPUT, GetModuleHandleW(nullptr), nullptr);
        hSend_ = CreateWindowW(L"BUTTON", L"发送", WS_CHILD | WS_VISIBLE,
            400, 310, 70, 24, hwnd, (HMENU)IDC_SEND, GetModuleHandleW(nullptr), nullptr);

        SendMessageW(hAddr_, WM_SETFONT, (WPARAM)hFont, TRUE);
        SendMessageW(hPort_, WM_SETFONT, (WPARAM)hFont, TRUE);
        SendMessageW(hNick_, WM_SETFONT, (WPARAM)hFont, TRUE);
        SendMessageW(hConnect_, WM_SETFONT, (WPARAM)hFont, TRUE);
        SendMessageW(hChat_, WM_SETFONT, (WPARAM)hFont, TRUE);
        SendMessageW(hInput_, WM_SETFONT, (WPARAM)hFont, TRUE);
        SendMessageW(hSend_, WM_SETFONT, (WPARAM)hFont, TRUE);

        // 子类化输入框，回车即发送
        oldInputProc_ = (WNDPROC)SetWindowLongPtrW(hInput_, GWLP_WNDPROC, (LONG_PTR)ChatWindow::InputProcThunk);
        SetPropW(hInput_, L"ChatWindowThis", this);

        // 设置网络回调：跨线程 -> PostMessage
        client_.setAppendCallback([this](const std::wstring& w){
            auto p = new std::wstring(w);
            PostMessageW(hwnd_, WM_CHAT_APPEND, 0, (LPARAM)p);
        });
        break;
    }
    case WM_SIZE: {
        int w = LOWORD(lParam), h = HIWORD(lParam);
        MoveWindow(hAddr_, 10, 10, 180, 24, TRUE);
        MoveWindow(hPort_, 200, 10, 60, 24, TRUE);
        MoveWindow(hNick_, 270, 10, 120, 24, TRUE);
        MoveWindow(hConnect_, w - 80, 10, 70, 24, TRUE);
        MoveWindow(hChat_, 10, 44, w - 20, h - 44 - 44, TRUE);
        MoveWindow(hInput_, 10, h - 30, w - 20 - 80, 24, TRUE);
        MoveWindow(hSend_, w - 80, h - 30, 70, 24, TRUE);
        break;
    }
    case WM_COMMAND: {
        int id = LOWORD(wParam);
        if (id == IDC_CONNECT) {
            onConnectToggle();
        } else if (id == IDC_SEND) {
            onSend();
        }
        break;
    }
    case WM_CLOSE: {
        client_.disconnect();
        DestroyWindow(hwnd);
        break;
    }
    case WM_DESTROY: {
        PostQuitMessage(0);
        break;
    }
    case WM_CHAT_APPEND: {
        std::wstring* p = reinterpret_cast<std::wstring*>(lParam);
        if (p) {
            appendText(*p);
            delete p;
        }
        return 0;
    }
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT CALLBACK ChatWindow::InputProcThunk(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    ChatWindow* self = reinterpret_cast<ChatWindow*>(GetPropW(hwnd, L"ChatWindowThis"));
    if (!self) return DefWindowProcW(hwnd, msg, wParam, lParam);
    return self->InputProc(hwnd, msg, wParam, lParam);
}

LRESULT ChatWindow::InputProc(HWND, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_KEYDOWN && wParam == VK_RETURN) {
        onSend();
        return 0;
    }
    return CallWindowProcW(oldInputProc_, hInput_, msg, wParam, lParam);
}

void ChatWindow::appendText(const std::wstring& text) {
    DWORD len = GetWindowTextLengthW(hChat_);
    SendMessageW(hChat_, EM_SETSEL, (WPARAM)len, (LPARAM)len);
    SendMessageW(hChat_, EM_REPLACESEL, FALSE, (LPARAM)text.c_str());
    SendMessageW(hChat_, EM_SCROLLCARET, 0, 0);
}

void ChatWindow::onSend() {
    if (!client_.isConnected()) return;
    int len = GetWindowTextLengthW(hInput_);
    if (len <= 0) return;
    std::wstring buffer; buffer.resize(len + 1);
    int got = GetWindowTextW(hInput_, buffer.data(), len + 1);
    if (got <= 0) return;
    buffer.resize(got);
    std::wstring text = buffer;
    SetWindowTextW(hInput_, L"");
    client_.sendText(text);
}

void ChatWindow::onConnectToggle() {
    if (!client_.isConnected()) {
        wchar_t addrW[256]; GetWindowTextW(hAddr_, addrW, 256);
        wchar_t portW[16]; GetWindowTextW(hPort_, portW, 16);
        wchar_t nickW[64]; GetWindowTextW(hNick_, nickW, 64);
        if (client_.connectTo(addrW, portW, nickW)) {
            SetWindowTextW(hConnect_, L"断开");
        }
    } else {
        client_.disconnect();
        SetWindowTextW(hConnect_, L"连接");
    }
}
