#include "stdafx.h"

struct pts
{
	char key{-1};
	char x{ -1 };
	char y{ -1 };
};

constexpr short PORT = 4000;
constexpr int BUFSIZE = 30;
bool b_shutdown = false; // 서버에 연결된 말이 하나도 없을때

HINSTANCE g_hInst;
LPCTSTR lpszClass = L"GS_Client";
LPCTSTR lpszWindowName = L"GS_Client";

char t_buf[BUFSIZE];
char buf;
Image* BackGround = nullptr;
Image* Knight = nullptr;
std::array<struct pts, 10> v;
int kn_x = 0, kn_y = 0;
int kn_sx = 80, kn_sy = 80; // 말의 x, y 크기

std::string SERVER_ADDR; // 내 컴퓨터를 뜻함 "127.0.0.1"
SOCKET server_s;
SOCKADDR_IN server_a; // 전역으로 안빼도 상관x
WSADATA WSAData; // 전역으로 안빼도 상관x
WSABUF wsabuf[1];
WSAOVERLAPPED over;
HWND hWnd;
DWORD recv_flag;
DWORD send_flag;

void CALLBACK send_callback(DWORD err, DWORD send_size, LPWSAOVERLAPPED pover, DWORD send_flag);
void CALLBACK recv_callback(DWORD err, DWORD recv_size, LPWSAOVERLAPPED pover, DWORD recv_flag);

LRESULT CALLBACK WndProc(HWND hWnd, UINT iMessage, WPARAM wParam, LPARAM lparam);

int WINAPI WinMain(HINSTANCE hinstance, HINSTANCE hPrevInstance, LPSTR lpszCmdParam, int nCmdShow)
{
	std::cout << "서버 주소 입력(xxx.xxx.xxx.xxx): ";
	std::cin >> SERVER_ADDR;

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

void print_error(const char* msg, int err_no)
{
	WCHAR* msg_buf;
	FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM, NULL, err_no,
		MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), reinterpret_cast<LPWSTR>(&msg_buf), 0, NULL);
	std::cout << msg;
	std::wcout << L":에러: " << msg_buf;
	while (true)
		LocalFree(msg_buf);
}

void do_recv()
{
	wsabuf[0].buf = t_buf;
	wsabuf[0].len = BUFSIZE;
	recv_flag = 0;
	ZeroMemory(&over, sizeof(over));
	int res = WSARecv(server_s, wsabuf, 1, nullptr, &send_flag, &over, recv_callback);
	if (0 != res) {
		int err_no = WSAGetLastError();
		if (WSA_IO_PENDING != err_no)
			print_error("WSARecv", WSAGetLastError());
	}
}

void do_send(char buf)
{
	wsabuf[0].buf = &buf;
	wsabuf[0].len = 1;
	ZeroMemory(&over, sizeof(over));
	WSASend(server_s, wsabuf, 1, nullptr, 0, &over, send_callback);
}

void send_callback(DWORD err, DWORD send_size, LPWSAOVERLAPPED pover, DWORD send_flag)
{
	//? 여기서 뭘 해야할까
	/*wsabuf[0].buf = t_buf;
	wsabuf[0].len = BUFSIZE;
	recv_flag = 0;
	ZeroMemory(pover, sizeof(pover));
	int res = WSARecv(server_s, wsabuf, 1, nullptr, &recv_flag, pover, recv_callback);
	if (0 != res) {
		int err_no = WSAGetLastError();
		if (WSA_IO_PENDING != err_no)
			print_error("WSARecv", WSAGetLastError());
	}*/
}

// 여기가 이상한듯...
void recv_callback(DWORD err, DWORD recv_size, LPWSAOVERLAPPED pover, DWORD recv_flag)
{
	// recv callback에서 패킷 잘린거 이어받기.
	// 화면에 그리기.
	// 다시 리시브 받기.

	// 패킷 구조: 패킷크기, id, x, y.
	// 패킷을 구분해서 출력하자.
	int p_size = 0;
	while (recv_size > p_size) {
		// 내가처리한 패킷크기보다 받은게 크면 동작
		int m_size = t_buf[0 + p_size];
		std::cout << "Player [" << static_cast<int>(t_buf[1 + p_size]) << "]: ";
		for (DWORD i = 0; i < m_size - 3; ++i)
			std::cout << int((t_buf[i + p_size + 2])) << " "; // id랑 패킷크기 건너뜀
		std::cout << std::endl;
		p_size = p_size + m_size;
	}
	
	// 처리 수정해야 함
	/*for (int i = 0; i < 10; ++i)
	{
		v[i].key = t_buf[3 * i];
		v[i].x = t_buf[3 * i + 1];
		v[i].y = t_buf[3 * i + 2];
	}*/

	// 그리기.7
	InvalidateRect(hWnd, NULL, false);

	// recv 열어두기.
	do_recv();
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

	switch (iMsg)
	{
	case WM_CREATE:
		SetTimer(hwnd, 1, 100, NULL);

		// 초기 위치 받아야 함
		std::wcout.imbue(std::locale("korean"));
		WSAStartup(MAKEWORD(2, 0), &WSAData);

		server_s = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, nullptr, 0, WSA_FLAG_OVERLAPPED);
		
		server_a.sin_family = AF_INET;
		server_a.sin_port = htons(PORT);
		inet_pton(AF_INET, SERVER_ADDR.c_str(), &server_a.sin_addr);
		connect(server_s, reinterpret_cast<sockaddr*>(&server_a), sizeof(server_a));
		
		BackGround = new Image();
		BackGround->img.Load(TEXT("board.png"));
		Knight = new Image();
		Knight->img.Load(TEXT("knight.png"));

		// 먼저 recv 열어두기.
		do_recv();
		break;

	case WM_TIMER:
		if (wParam == 1)
		{
			SleepEx(0, TRUE);
		}
		break;

	case WM_LBUTTONDOWN:
		InvalidateRect(hwnd, NULL, false);
		break;

	case WM_KEYDOWN:
	{
		// send
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
		case 'E':
			buf = 'E';
			break;
		default:
			buf = '0';
		}
		do_send(buf);
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
		for (pts& p : v)
		{
			if(-1 != p.key )
				Knight->img.TransparentBlt(mdc, p.x * kn_sx, p.y * kn_sy, kn_sx, kn_sy, MAGENTA);
		}

		// // // // // // // // // //
		BitBlt(hdc, 0, 0, WIDTHMAX, HEIGHTMAX, mdc, 0, 0, SRCCOPY);

		DeleteDC(mdc);
		DeleteObject(hbitmap);

		EndPaint(hwnd, &ps);
		break;

	case WM_DESTROY:
		KillTimer(hwnd, 1);
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

