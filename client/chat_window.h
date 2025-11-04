#pragma once

#include <windows.h>
#include <string>
#include "chat_client.h"

// client GUI 窗口类
class ChatWindow {
public:
    int run(HINSTANCE hInst, int nCmdShow);

private:
    static LRESULT CALLBACK WndProcThunk(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK InputProcThunk(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT InputProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    void appendText(const std::wstring& text);
    void onSend();
    void onConnectToggle();

private:
    // 控件句柄
    HWND hwnd_{};
    HWND hChat_{};
    HWND hInput_{};
    HWND hAddr_{};
    HWND hPort_{};
    HWND hNick_{};
    HWND hConnect_{};
    HWND hSend_{};
    WNDPROC oldInputProc_{}; // 原始输入框窗口过程

    ChatClientNetwork client_{}; // 聊天客户端
};

// 聊天历史
#define IDC_CHATLOG   1001
// 输入框
#define IDC_INPUT     1002
// 发送按钮
#define IDC_SEND      1003
// 地址输入框
#define IDC_ADDR      1004
// 端口输入框
#define IDC_PORT      1005
// 昵称输入框
#define IDC_NICK      1006
// 连接按钮
#define IDC_CONNECT   1007

// 消息：追加聊天内容
#define WM_CHAT_APPEND (WM_APP + 1)
