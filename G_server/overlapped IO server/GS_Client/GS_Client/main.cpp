#include "stdafx.h"

constexpr short PORT = 4000;
constexpr int BUFSIZE = 2;

HINSTANCE g_hInst;
LPCTSTR lpszClass = L"GS_Client";
LPCTSTR lpszWindowName = L"GS_Client";

Image* BackGround = nullptr;
Image* Knight = nullptr;
int kn_x = 0, kn_y = 0; // 말의 x, y 좌표
int kn_sx = 80, kn_sy = 80; // 말의 x, y 크기

std::string SERVER_ADDR; // 내 컴퓨터를 뜻함 "127.0.0.1"
SOCKET server_s;
SOCKADDR_IN server_a;
WSADATA WSAData;

LRESULT CALLBACK WndProc(HWND hWnd, UINT iMessage, WPARAM wParam, LPARAM lparam);

int WINAPI WinMain(HINSTANCE hinstance, HINSTANCE hPrevInstance, LPSTR lpszCmdParam, int nCmdShow)
{
	std::cout << "서버 주소 입력(xxx.xxx.xxx.xxx): ";
	std::cin >> SERVER_ADDR;

	HWND hWnd;
	MSG Message;
	WNDCLASSEX WndClass;
	g_hInst = hinstance;

	WndClass.cbSize = sizeof(WndClass);
	WndClass.style = CS_HREDRAW | CS_VREDRAW;
	WndClass.lpfnWndProc = (WNDPROC)WndProc;
	WndClass.cbClsExtra = 0;
	WndClass.cbWndExtra = 0;
	WndClass.hInstance = hinstance;
	WndClass.hIcon = LoadIcon(NULL, IDI_APPLICATION);
	WndClass.hCursor = LoadCursor(NULL, IDC_ARROW);
	WndClass.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH);
	WndClass.lpszMenuName = NULL;
	WndClass.lpszClassName = lpszClass;
	WndClass.hIconSm = LoadIcon(NULL, IDI_APPLICATION);
	RegisterClassEx(&WndClass);

	hWnd = CreateWindow(lpszClass, lpszWindowName, WS_OVERLAPPEDWINDOW, 0, 0, WIDTHMAX, HEIGHTMAX, NULL, (HMENU)NULL, hinstance, NULL);
	ShowWindow(hWnd, nCmdShow);
	UpdateWindow(hWnd);

	while (GetMessage(&Message, 0, 0, 0))
	{
		TranslateMessage(&Message);
		DispatchMessage(&Message);
	}
	return Message.wParam;
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT iMsg, WPARAM wParam, LPARAM lParam)
{
	PAINTSTRUCT ps;
	HDC hdc, mdc;
	HBITMAP hbitmap;
	static HBITMAP hBitmap;
	HBRUSH hbrush, oldbrush;
	HPEN hpen, oldpen;
	RECT rt;
	static POINT mousept;

	switch (iMsg)
	{
	case WM_CREATE:
		std::wcout.imbue(std::locale("korean"));
		WSAStartup(MAKEWORD(2, 0), &WSAData);

		server_s = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, nullptr, 0, 0);
		
		server_a.sin_family = AF_INET;
		server_a.sin_port = htons(PORT);
		inet_pton(AF_INET, SERVER_ADDR.c_str(), &server_a.sin_addr);
		connect(server_s, reinterpret_cast<sockaddr*>(&server_a), sizeof(server_a));
		
		BackGround = new Image();
		BackGround->img.Load(TEXT("board.png"));
		Knight = new Image();
		Knight->img.Load(TEXT("knight.png"));
		break;

	case WM_TIMER:
		InvalidateRect(hwnd, NULL, false);
		break;

	case WM_LBUTTONDOWN:
		mousept = { LOWORD(lParam), HIWORD(lParam) };
		InvalidateRect(hwnd, NULL, false);
		break;

	case WM_KEYDOWN:
	{
		char buf;
		switch (wParam)
		{
		case VK_UP:
			buf = 'U';
			break;
		case VK_DOWN:
			buf = 'D';
			break;
		case VK_LEFT:
			buf = 'L';
			break;
		case VK_RIGHT:
			buf = 'R';
			break;
		}

		WSABUF wsabuf[1];
		wsabuf[0].buf = &buf;
		wsabuf[0].len = 1;
		DWORD sent_size;
		WSASend(server_s, wsabuf, 1, &sent_size, 0, nullptr, nullptr);

		// x y값을 recv
		char buf2[BUFSIZE];

		wsabuf[0].buf = buf2;
		wsabuf[0].len = BUFSIZE;
		DWORD recv_size;
		DWORD recv_flag = 0;
		WSARecv(server_s, wsabuf, 1, &recv_size, &recv_flag, nullptr, nullptr);
		kn_x = int(buf2[0]);
		kn_y = int(buf2[1]);

		InvalidateRect(hwnd, NULL, false);
		break;
	}

	case WM_KEYUP:
		InvalidateRect(hwnd, NULL, false);
		break;

	case WM_PAINT:
		GetClientRect(hwnd, &rt);
		hdc = BeginPaint(hwnd, &ps);
		mdc = CreateCompatibleDC(hdc);
		hbitmap = CreateCompatibleBitmap(hdc, rt.right, rt.bottom);
		SelectObject(mdc, (HBITMAP)hbitmap);
		hbrush = CreateSolidBrush(RGB(255, 255, 255));
		oldbrush = (HBRUSH)SelectObject(mdc, hbrush);
		Rectangle(mdc, 0, 0, rt.right, rt.bottom);
		SelectObject(mdc, oldbrush);
		DeleteObject(hbrush);
		// // // // // // // // // // // //

		BackGround->img.Draw(mdc, 0, 0, 640, 640);
		Knight->img.TransparentBlt(mdc, kn_x * kn_sx, kn_y * kn_sy, kn_sx, kn_sy, MAGENTA);

		// // // // // // // // // //
		BitBlt(hdc, 0, 0, WIDTHMAX, HEIGHTMAX, mdc, 0, 0, SRCCOPY);

		DeleteDC(mdc);
		DeleteObject(hbitmap);

		EndPaint(hwnd, &ps);
		break;

	case WM_DESTROY:
		closesocket(server_s);
		WSACleanup();
		delete BackGround;
		delete Knight;
		DeleteObject(hBitmap);
		PostQuitMessage(0);
		break;

	}
	return DefWindowProc(hwnd, iMsg, wParam, lParam);
}

