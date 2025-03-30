#include <iostream>
#include <WS2tcpip.h>  // 최신거까지 지원하는 친구 이걸 쓰도록
#pragma comment (lib, "WS2_32.LIB") // 옛날옛날시절 라이브러리에서 시작한것.. 그래서 32임

constexpr short PORT = 4000;
constexpr int BUFSIZE = 2;

int kn_x, kn_y;

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

int main()
{
	std::wcout.imbue(std::locale("korean"));
	WSADATA WSAData;
	WSAStartup(MAKEWORD(2, 0), &WSAData); // 윈도우에서 반드시 해야하는 애?
	// 인터넷 소켓 프로그래밍을 하겠다? ms네트워크 안쓰고? 이거 안하면 에러가 남
	// 윈도우에서 돌릴때만 이거 적어야 함

	SOCKET server_s = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, nullptr, 0, 0); // IPPROTO_TCP 게임에선 요것만 사용
	SOCKADDR_IN server_a;
	server_a.sin_family = AF_INET;
	server_a.sin_port = htons(PORT); // 내 컴퓨터의 다른 프로그램과 충돌하지 않는 숫자를 넣어야, 2000 이상
	server_a.sin_addr.S_un.S_addr = htonl(INADDR_ANY);
	bind(server_s, reinterpret_cast<sockaddr*>(&server_a), sizeof(server_a));
	listen(server_s, SOMAXCONN);
	int addr_size = sizeof(server_a);
	SOCKET client_s = WSAAccept(server_s, reinterpret_cast<sockaddr*>(&server_a), &addr_size, nullptr, 0);
	char clientIP[INET_ADDRSTRLEN];
	inet_ntop(AF_INET, &(server_a.sin_addr), clientIP, INET_ADDRSTRLEN);
	std::cout << clientIP <<" 연결됨." << std::endl;
	while (true) {
		char buf;

		WSABUF wsabuf[1];
		wsabuf[0].buf = &buf;
		wsabuf[0].len = sizeof(int);
		DWORD recv_size;
		DWORD recv_flag = 0;
		int res = WSARecv(client_s, wsabuf, 1, &recv_size, &recv_flag, nullptr, nullptr);
		if (0 != res) {
			print_error("WSARecv", WSAGetLastError());
		}
		if (0 == recv_size) break;

		// logic
		switch (buf)
		{
		case 'U':
			if (kn_y > 0) --kn_y;
			break;
		case 'D':
			if (kn_y < 7) ++kn_y;
			break;
		case 'L':
			if (kn_x > 0) --kn_x;
			break;
		case 'R':
			if (kn_x < 7) ++kn_x;
			break;
		}

		char buf2[BUFSIZE] {char(kn_x), char(kn_y)};
		wsabuf[0].buf = buf2;
		wsabuf[0].len = BUFSIZE; 
		DWORD sent_size;
		WSASend(client_s, wsabuf, 1, &sent_size, 0, nullptr, nullptr);
	}
	closesocket(server_s);
	closesocket(client_s);
	WSACleanup();
}