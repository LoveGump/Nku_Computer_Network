#pragma once

#include <windows.h>
#include <string>
#include "chat_client.h"

// 简单 Win32 窗口封装，组合 ChatClientNetwork
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
    WNDPROC oldInputProc_{};

    ChatClientNetwork client_{};
};

#define IDC_CHATLOG   1001
#define IDC_INPUT     1002
#define IDC_SEND      1003
#define IDC_ADDR      1004
#define IDC_PORT      1005
#define IDC_NICK      1006
#define IDC_CONNECT   1007

#define WM_CHAT_APPEND (WM_APP + 1)
