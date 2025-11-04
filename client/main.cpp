
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <commctrl.h>
#include <string>
#include <thread>
#include <atomic>
#include <cstdio>

#include "common/protocol.h"

#pragma comment(lib, "Ws2_32.lib")

using namespace chatproto;

// Control IDs
#define IDC_CHATLOG   1001
#define IDC_INPUT     1002
#define IDC_SEND      1003
#define IDC_ADDR      1004
#define IDC_PORT      1005
#define IDC_NICK      1006
#define IDC_CONNECT   1007

#define WM_CHAT_APPEND (WM_APP + 1)

struct AppState {
    HWND hwnd{};
    HWND hChat{};
    HWND hInput{};
    HWND hAddr{};
    HWND hPort{};
    HWND hNick{};
    HWND hConnect{};
    HWND hSend{};
    SOCKET sock{INVALID_SOCKET};
    std::thread recvThread;
    std::atomic<bool> connected{false};
    std::atomic<bool> disconnecting{false};
};

static AppState g_app;
static WNDPROC g_OldInputProc = nullptr;

// Forward declarations
void SendCurrentInput(AppState& app);

LRESULT CALLBACK InputProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_KEYDOWN && wParam == VK_RETURN) {
        SendCurrentInput(g_app);
        return 0;
    }
    return CallWindowProcW(g_OldInputProc, hwnd, msg, wParam, lParam);
}

void AppendText(HWND hEdit, const std::wstring& text) {
    // Move caret to end and replace selection
    DWORD len = GetWindowTextLengthW(hEdit);
    SendMessageW(hEdit, EM_SETSEL, (WPARAM)len, (LPARAM)len);
    SendMessageW(hEdit, EM_REPLACESEL, FALSE, (LPARAM)text.c_str());
    // Auto scroll to bottom
    SendMessageW(hEdit, EM_SCROLLCARET, 0, 0);
}

void ReceiverThread(SOCKET s, HWND hwnd) {
    while (true) {
        MsgType t; std::string p;
        if (!recvFrame(s, t, p)) break;
        if (t == MsgType::USER_JOIN) {
            std::wstring msg = L"[加入] ";
            msg += utf8_to_utf16(p);
            msg += L"\r\n";
            PostMessageW(hwnd, WM_CHAT_APPEND, 0, (LPARAM)new std::wstring(std::move(msg)));
        } else if (t == MsgType::USER_LEAVE) {
            std::wstring msg = L"[离开] ";
            msg += utf8_to_utf16(p);
            msg += L"\r\n";
            PostMessageW(hwnd, WM_CHAT_APPEND, 0, (LPARAM)new std::wstring(std::move(msg)));
        } else if (t == MsgType::SERVER_BROADCAST) {
            // payload: from + '\n' + text
            size_t pos = p.find('\n');
            std::string from = pos == std::string::npos ? std::string() : p.substr(0, pos);
            std::string text = pos == std::string::npos ? p : p.substr(pos + 1);
            std::wstring line = L"<" + utf8_to_utf16(from) + L"> " + utf8_to_utf16(text) + L"\r\n";
            PostMessageW(hwnd, WM_CHAT_APPEND, 0, (LPARAM)new std::wstring(std::move(line)));
        }
    }
    if (!g_app.disconnecting.load()) {
        PostMessageW(hwnd, WM_CHAT_APPEND, 0, (LPARAM)new std::wstring(L"[系统] 已断开连接\r\n"));
    }
}

bool ConnectToServer(AppState& app) {
    wchar_t addrW[256]; GetWindowTextW(app.hAddr, addrW, 256);
    wchar_t portW[16]; GetWindowTextW(app.hPort, portW, 16);
    wchar_t nickW[64]; GetWindowTextW(app.hNick, nickW, 64);
    std::wstring waddr(addrW), wport(portW), wnick(nickW);
    if (waddr.empty()) waddr = L"127.0.0.1";
    if (wport.empty()) wport = L"5000";
    if (wnick.empty()) wnick = L"User";

    std::string addr = utf16_to_utf8(waddr);
    std::string port = utf16_to_utf8(wport);
    std::string nick = utf16_to_utf8(wnick);

    addrinfo hints{}; hints.ai_family = AF_INET; hints.ai_socktype = SOCK_STREAM; hints.ai_protocol = IPPROTO_TCP;
    addrinfo* res = nullptr;
    if (getaddrinfo(addr.c_str(), port.c_str(), &hints, &res) != 0) {
        AppendText(app.hChat, L"[错误] 解析地址失败\r\n");
        return false;
    }

    SOCKET s = INVALID_SOCKET;
    for (addrinfo* p = res; p; p = p->ai_next) {
        s = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (s == INVALID_SOCKET) continue;
        if (connect(s, p->ai_addr, (int)p->ai_addrlen) == SOCKET_ERROR) {
            closesocket(s); s = INVALID_SOCKET; continue;
        }
        break;
    }
    freeaddrinfo(res);

    if (s == INVALID_SOCKET) {
        AppendText(app.hChat, L"[错误] 无法连接服务器\r\n");
        return false;
    }

    // Send HELLO
    if (!sendFrame(s, MsgType::HELLO, nick)) {
        closesocket(s);
        AppendText(app.hChat, L"[错误] 发送 HELLO 失败\r\n");
        return false;
    }

    app.sock = s;
    app.connected.store(true);
    app.disconnecting.store(false);
    AppendText(app.hChat, L"[系统] 已连接\r\n");
    app.recvThread = std::thread(ReceiverThread, s, app.hwnd);
    return true;
}

void Disconnect(AppState& app) {
    if (!app.connected.load()) return;
    app.disconnecting.store(true);
    // Append local leave message with nickname
    wchar_t nickW[64]; GetWindowTextW(app.hNick, nickW, 64);
    std::wstring wnick = nickW;
    if (wnick.empty()) wnick = L"User";
    // Try to notify server with BYE before closing
    {
        std::string nickUtf8 = utf16_to_utf8(wnick);
        sendFrame(app.sock, MsgType::BYE, nickUtf8);
    }
    std::wstring msg = L"[离开] ";
    msg += wnick;
    msg += L"\r\n";
    AppendText(app.hChat, msg);
    app.connected.store(false);
    shutdown(app.sock, SD_BOTH);
    closesocket(app.sock);
    if (app.recvThread.joinable()) app.recvThread.join();
    app.sock = INVALID_SOCKET;
    app.disconnecting.store(false);
}

void SendCurrentInput(AppState& app) {
    if (!app.connected.load()) return;
    int len = GetWindowTextLengthW(app.hInput);
    if (len <= 0) return;
    std::wstring text;
    text.reserve(len);
    std::wstring buffer; buffer.resize(len + 1);
    int got = GetWindowTextW(app.hInput, buffer.data(), len + 1);
    if (got <= 0) return;
    buffer.resize(got);
    text = buffer;
    // Clear input
    SetWindowTextW(app.hInput, L"");
    // Send
    std::string utf8 = utf16_to_utf8(text);
    sendFrame(app.sock, MsgType::CHAT, utf8);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        HFONT hFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
        g_app.hwnd = hwnd;
        // Address, Port, Nick
        g_app.hAddr = CreateWindowW(L"EDIT", L"127.0.0.1", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
            10, 10, 180, 24, hwnd, (HMENU)IDC_ADDR, GetModuleHandleW(nullptr), nullptr);
        g_app.hPort = CreateWindowW(L"EDIT", L"5000", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
            200, 10, 60, 24, hwnd, (HMENU)IDC_PORT, GetModuleHandleW(nullptr), nullptr);
        g_app.hNick = CreateWindowW(L"EDIT", L"User", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
            270, 10, 120, 24, hwnd, (HMENU)IDC_NICK, GetModuleHandleW(nullptr), nullptr);
        g_app.hConnect = CreateWindowW(L"BUTTON", L"连接", WS_CHILD | WS_VISIBLE,
            400, 10, 70, 24, hwnd, (HMENU)IDC_CONNECT, GetModuleHandleW(nullptr), nullptr);

        // Chat log
        g_app.hChat = CreateWindowW(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY | WS_VSCROLL,
            10, 44, 460, 260, hwnd, (HMENU)IDC_CHATLOG, GetModuleHandleW(nullptr), nullptr);
        // Input
        g_app.hInput = CreateWindowW(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL | ES_WANTRETURN,
            10, 310, 380, 24, hwnd, (HMENU)IDC_INPUT, GetModuleHandleW(nullptr), nullptr);
        g_app.hSend = CreateWindowW(L"BUTTON", L"发送", WS_CHILD | WS_VISIBLE,
            400, 310, 70, 24, hwnd, (HMENU)IDC_SEND, GetModuleHandleW(nullptr), nullptr);

        SendMessageW(g_app.hAddr, WM_SETFONT, (WPARAM)hFont, TRUE);
        SendMessageW(g_app.hPort, WM_SETFONT, (WPARAM)hFont, TRUE);
        SendMessageW(g_app.hNick, WM_SETFONT, (WPARAM)hFont, TRUE);
        SendMessageW(g_app.hConnect, WM_SETFONT, (WPARAM)hFont, TRUE);
        SendMessageW(g_app.hChat, WM_SETFONT, (WPARAM)hFont, TRUE);
        SendMessageW(g_app.hInput, WM_SETFONT, (WPARAM)hFont, TRUE);
        SendMessageW(g_app.hSend, WM_SETFONT, (WPARAM)hFont, TRUE);
        // Subclass input to capture Enter key
        g_OldInputProc = (WNDPROC)SetWindowLongPtrW(g_app.hInput, GWLP_WNDPROC, (LONG_PTR)InputProc);
        break;
    }
    case WM_SIZE: {
        int w = LOWORD(lParam), h = HIWORD(lParam);
        MoveWindow(g_app.hAddr, 10, 10, 180, 24, TRUE);
        MoveWindow(g_app.hPort, 200, 10, 60, 24, TRUE);
        MoveWindow(g_app.hNick, 270, 10, 120, 24, TRUE);
        MoveWindow(g_app.hConnect, w - 80, 10, 70, 24, TRUE);
        MoveWindow(g_app.hChat, 10, 44, w - 20, h - 44 - 44, TRUE);
        MoveWindow(g_app.hInput, 10, h - 30, w - 20 - 80, 24, TRUE);
        MoveWindow(g_app.hSend, w - 80, h - 30, 70, 24, TRUE);
        break;
    }
    case WM_COMMAND: {
        int id = LOWORD(wParam);
        if (id == IDC_CONNECT) {
            if (!g_app.connected.load()) {
                if (ConnectToServer(g_app)) {
                    SetWindowTextW(g_app.hConnect, L"断开");
                }
            } else {
                Disconnect(g_app);
                SetWindowTextW(g_app.hConnect, L"连接");
            }
        } else if (id == IDC_SEND) {
            SendCurrentInput(g_app);
        } else if (id == IDC_INPUT) {
            if (HIWORD(wParam) == EN_MAXTEXT) {
                // ignore
            }
        }
        break;
    }
    case WM_CLOSE: {
        Disconnect(g_app);
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
            AppendText(g_app.hChat, *p);
            delete p;
        }
        return 0;
    }
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

int APIENTRY wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int nCmdShow) {
    WSADATA wsa; WSAStartup(MAKEWORD(2,2), &wsa);

    const wchar_t CLASS_NAME[] = L"ChatClientWin32";
    WNDCLASSW wc{};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = CLASS_NAME;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);

    RegisterClassW(&wc);

    HWND hwnd = CreateWindowExW(0, CLASS_NAME, L"Chat Client", WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 640, 420,
        nullptr, nullptr, hInst, nullptr);

    ShowWindow(hwnd, nCmdShow);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    WSACleanup();
    return 0;
}
