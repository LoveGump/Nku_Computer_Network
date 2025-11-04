// OOP 版本客户端入口：委托给 ChatWindow
#include <winsock2.h>
#include "chat_window.h"

int APIENTRY wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int nCmdShow) {
    WSADATA wsa; WSAStartup(MAKEWORD(2,2), &wsa);
    ChatWindow win;
    int ret = win.run(hInst, nCmdShow);
    WSACleanup();
    return ret;
}