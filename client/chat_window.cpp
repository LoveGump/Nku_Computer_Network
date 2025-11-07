#include "chat_window.h"
#include <commctrl.h>

using namespace chatproto;

/**
 * 运行聊天窗口主循环
 * @param hInst 应用实例句柄
 * @param nCmdShow 显示命令
 */
int ChatWindow::run(HINSTANCE hInst, int nCmdShow) {
    const wchar_t CLASS_NAME[] = L"ChatClientWin32OOP";
    WNDCLASSW wc{};
    wc.lpfnWndProc = ChatWindow::WndProcThunk; // 窗口过程函数
    wc.hInstance = hInst;// 实例句柄
    wc.lpszClassName = CLASS_NAME; // 窗口类名
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);// 设置默认光标

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

/**
 * 窗口过程静态包装函数
 * @param hwnd 窗口句柄
 * @param msg 消息类型
 * @param wParam 消息参数
 * @param lParam 消息参数
 */
LRESULT CALLBACK ChatWindow::WndProcThunk(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    ChatWindow* self = nullptr; // 当前窗口实例指针
    if (msg == WM_NCCREATE) {
        // 如果是创建窗口消息，获取实例指针
        auto cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = static_cast<ChatWindow*>(cs->lpCreateParams);
        // 将实例指针存储在窗口数据中
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)self);
        self->hwnd_ = hwnd;
    } else {
        // 否则从窗口数据中获取实例指针
        self = reinterpret_cast<ChatWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    // 如果未找到实例指针，则调用默认窗口过程
    if (!self) return DefWindowProcW(hwnd, msg, wParam, lParam);
    // 调用实例的窗口过程方法
    return self->WndProc(hwnd, msg, wParam, lParam);
}

/**
 * 窗口过程实例方法
 * @param hwnd 窗口句柄
 * @param msg 消息类型
 * @param wParam 消息参数
 * @param lParam 消息参数
 */
LRESULT ChatWindow::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        // 如果创建窗口，初始化控件
        HFONT hFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT); // 默认字体
        // 连接参数输入框
        // 地址WS_CHILD 子窗口 | WS_VISIBLE 可见 | WS_BORDER 有边框 | ES_AUTOHSCROLL 自动水平滚动
        hAddr_ = CreateWindowW(L"EDIT", L"127.0.0.1", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
            10, 10, 180, 24, hwnd, (HMENU)IDC_ADDR, GetModuleHandleW(nullptr), nullptr);
        // 端口
        hPort_ = CreateWindowW(L"EDIT", L"5000", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
            200, 10, 60, 24, hwnd, (HMENU)IDC_PORT, GetModuleHandleW(nullptr), nullptr);
        // 昵称
        hNick_ = CreateWindowW(L"EDIT", L"User", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
            270, 10, 120, 24, hwnd, (HMENU)IDC_NICK, GetModuleHandleW(nullptr), nullptr);
        // 连接按钮
        hConnect_ = CreateWindowW(L"BUTTON", L"连接", WS_CHILD | WS_VISIBLE,
            400, 10, 70, 24, hwnd, (HMENU)IDC_CONNECT, GetModuleHandleW(nullptr), nullptr);

        // 聊天记录
        hChat_ = CreateWindowW(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY | WS_VSCROLL,
            10, 44, 460, 260, hwnd, (HMENU)IDC_CHATLOG, GetModuleHandleW(nullptr), nullptr);
        // 输入框
        hInput_ = CreateWindowW(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL | ES_WANTRETURN,
            10, 310, 380, 24, hwnd, (HMENU)IDC_INPUT, GetModuleHandleW(nullptr), nullptr);
        // 发送按钮
        hSend_ = CreateWindowW(L"BUTTON", L"发送", WS_CHILD | WS_VISIBLE,
            400, 310, 70, 24, hwnd, (HMENU)IDC_SEND, GetModuleHandleW(nullptr), nullptr);

        SendMessageW(hAddr_, WM_SETFONT, (WPARAM)hFont, TRUE);
        SendMessageW(hPort_, WM_SETFONT, (WPARAM)hFont, TRUE);
        SendMessageW(hNick_, WM_SETFONT, (WPARAM)hFont, TRUE);
        SendMessageW(hConnect_, WM_SETFONT, (WPARAM)hFont, TRUE);
        SendMessageW(hChat_, WM_SETFONT, (WPARAM)hFont, TRUE);
        SendMessageW(hInput_, WM_SETFONT, (WPARAM)hFont, TRUE);
        SendMessageW(hSend_, WM_SETFONT, (WPARAM)hFont, TRUE);

        // 将修改窗口的过程设置为自定义的 InputProc，保留默认的过程
        oldInputProc_ = (WNDPROC)SetWindowLongPtrW(hInput_, GWLP_WNDPROC, (LONG_PTR)ChatWindow::InputProcThunk);
        SetPropW(hInput_, L"ChatWindowThis", this);

        // 设置网络回调：跨线程 -> PostMessage
        client_.setAppendCallback([this](const std::wstring& w){
            auto p = new std::wstring(w);
            // 消息类型为 WM_CHAT_APPEND，参数为动态分配的字符串指针
            PostMessageW(hwnd_, WM_CHAT_APPEND, 0, (LPARAM)p);
        });
        // 连接状态变化回调
        client_.setStateCallback([this](bool connected){
            PostMessageW(hwnd_, WM_CONN_STATE, (WPARAM)(connected ? 1 : 0), 0);
        });
        break;
    }
    case WM_SIZE: {
        // 调整控件大小和位置
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
        // 处理按钮点击事件
        int id = LOWORD(wParam);
        if (id == IDC_CONNECT) {
            // 如果是连接按钮，切换连接状态
            onConnectToggle();
        } else if (id == IDC_SEND) {
            // 如果是发送按钮，发送消息
            onSend();
        }
        break;
    }
    case WM_CLOSE: {
        // 关闭窗口时断开连接并销毁窗口
        client_.disconnect();
        DestroyWindow(hwnd);
        break;
    }
    case WM_DESTROY: {
        // 窗口销毁时退出消息循环
        PostQuitMessage(0);
        break;
    }
    case WM_CHAT_APPEND: {
        // 追加聊天内容消息处理
        std::wstring* p = reinterpret_cast<std::wstring*>(lParam);
        if (p) {
            appendText(*p);
            delete p;
        }
        return 0;
    }
    case WM_CONN_STATE: {
        bool connected = (wParam != 0);
        updateUiForConnected(connected);
        return 0;
    }
    }
    // 默认窗口过程处理其他消息
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

/**
 * 将消息转发到 ChatWindow 实例的成员函数 InputProc
 * @param hwnd 窗口句柄
 * @param msg 消息类型
 * @param wParam 消息参数
 * @param lParam 消息参数
 */
LRESULT CALLBACK ChatWindow::InputProcThunk(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    // 获取 ChatWindow 实例指针
    ChatWindow* self = reinterpret_cast<ChatWindow*>(GetPropW(hwnd, L"ChatWindowThis"));
    if (!self) return DefWindowProcW(hwnd, msg, wParam, lParam);
    return self->InputProc(hwnd, msg, wParam, lParam);
}

/**
 * 输入框窗口过程实例方法
 * @param hwnd 窗口句柄
 * @param msg 消息类型
 * @param wParam 消息参数
 * @param lParam 消息参数
 */
LRESULT ChatWindow::InputProc(HWND, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_KEYDOWN && wParam == VK_RETURN) {
        // 回车键发送消息
        onSend();
        return 0;
    }
    // 其他消息调用原始窗口函数
    return CallWindowProcW(oldInputProc_, hInput_, msg, wParam, lParam);
}

/**
 * 追加聊天文本到聊天记录框
 * @param text 要追加的文本
 */
void ChatWindow::appendText(const std::wstring& text) {
    DWORD len = GetWindowTextLengthW(hChat_); // 获取当前文本长度
    SendMessageW(hChat_, EM_SETSEL, (WPARAM)len, (LPARAM)len); // 设置选择范围到文本末尾
    SendMessageW(hChat_, EM_REPLACESEL, FALSE, (LPARAM)text.c_str()); // 插入新文本
    SendMessageW(hChat_, EM_SCROLLCARET, 0, 0); // 滚动到光标位置
}

/**
 * 发送按钮处理
 */
void ChatWindow::onSend() {
    if (!client_.isConnected()) return;
    // 获取输入框内容
    int len = GetWindowTextLengthW(hInput_); // 获取文本长度
    if (len <= 0) return;
    std::wstring buffer; 
    buffer.resize(len + 1);
    int got = GetWindowTextW(hInput_, buffer.data(), len + 1); // 读取文本
    if (got <= 0) return;
    buffer.resize(got);
    std::wstring text = buffer;
    // 清空输入框并发送消息
    SetWindowTextW(hInput_, L"");
    client_.sendText(text);
}

/**
 * 连接/断开按钮处理
 */
void ChatWindow::onConnectToggle() {
    if (!client_.isConnected()) {
        // 如果未连接，则执行连接逻辑；否则执行断开逻辑
        wchar_t addrW[256]; 
        GetWindowTextW(hAddr_, addrW, 256);
        wchar_t portW[16]; 
        GetWindowTextW(hPort_, portW, 16);
        wchar_t nickW[64]; 
        GetWindowTextW(hNick_, nickW, 64);
        if (client_.connectTo(addrW, portW, nickW)) {
            // 连接成功，更新按钮文本
            updateUiForConnected(true);
        }
    } else {
        // 断开连接，更新按钮文本
        client_.disconnect();
        updateUiForConnected(false);
    }
}

void ChatWindow::updateUiForConnected(bool connected) {
    // 切换按钮文本
    SetWindowTextW(hConnect_, connected ? L"断开" : L"连接");
    // 连接参数在连接后禁用，断开后启用
    EnableWindow(hAddr_, !connected);
    EnableWindow(hPort_, !connected);
    EnableWindow(hNick_, !connected);
    // 发送与输入框仅在连接后启用
    EnableWindow(hSend_, connected);
    EnableWindow(hInput_, connected);
}
