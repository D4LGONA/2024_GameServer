#define _CRT_SECURE_NO_WARNINGS
#include <iostream>
#include <WS2tcpip.h>
#include <unordered_map>
#include <vector>
#include <array>
#pragma comment (lib, "WS2_32.LIB")

constexpr int BUFSIZE = 256;
constexpr short PORT = 4000;
bool b_shutdown = false; // 서버에 연결된 말이 하나도 없을때
std::unordered_map<LPWSAOVERLAPPED, int> g_session_map;
std::unordered_map<int, class SESSION> g_players;
void CALLBACK send_callback(DWORD err, DWORD send_size, LPWSAOVERLAPPED pover, DWORD send_flag);
void CALLBACK recv_callback(DWORD err, DWORD recv_size, LPWSAOVERLAPPED pover, DWORD recv_flag);

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

class EXP_OVER {
public:
	WSAOVERLAPPED over;
	WSABUF wsabuf[1];
	char sendbuf[BUFSIZE]; // 버퍼 사이즈?

	// 이 생성자에서 보낼 데이터 세팅
	EXP_OVER(int s_id, char* data, int size)
	{
		ZeroMemory(&over, sizeof(over));
		wsabuf[0].buf = sendbuf;
		wsabuf[0].len = size;
		memcpy(sendbuf, data, size);
	}

};

class SESSION {
	WSABUF wsabuf[1];
	SOCKET client_s;
	WSAOVERLAPPED over;
	int x{ 0 };
	int y{ 0 };
	int session_id;
	char buf[5];
	char recvbuf[BUFSIZE];

public:
	SESSION() { std::cout << "error"; exit(0); };
	SESSION(SOCKET s, int id) : client_s{ s } 
	{ 
		g_session_map[&over] = id; 
		session_id = id;
		wsabuf[0].buf = recvbuf;
	}
	~SESSION() 
	{ 
		closesocket(client_s);
	};

	void recv()
	{
		// 굳이 할필요가 없음 버퍼를 공유하지 않아서
		wsabuf[0].buf = recvbuf;
		wsabuf[0].len = BUFSIZE;
		DWORD recv_flag = 0;
		ZeroMemory(&over, sizeof(over));
		int res = WSARecv(client_s, wsabuf, 1, nullptr, &recv_flag, &over, recv_callback);
		if (0 != res) {
			int err_no = WSAGetLastError();
			if (WSA_IO_PENDING != err_no)
				print_error("WSARecv", WSAGetLastError());
		}
	}

	void send(char* data)
	{
		// exp over 생성.
		auto b = new EXP_OVER(session_id, data, 5);

		std::cout << sizeof(b);
		int res = WSASend(client_s, b->wsabuf, 1, nullptr, 0, &b->over, send_callback);
		if (0 != res) {
			print_error("WSASend", WSAGetLastError());
		}
	}

	void move()
	{
		switch (recvbuf[0])
		{
		case 'U':
			if (y > 0) --y;
			break;
		case 'D':
			if (y < 7) ++y;
			break;
		case 'L':
			if (x > 0) --x;
			break;
		case 'R':
			if (x < 7) ++x;
			break;
		case 'E': // 프로그램 종료 조건
			g_players.erase(session_id);
			g_session_map.erase(&over);
			return;
			break;
		}
	}

	void broadcast(char* data)
	{
		for (auto& p : g_players)
			p.second.send(data);
	}

	char* makedata()
	{
		ZeroMemory(buf, sizeof(buf));
		int len = sizeof(buf); // 요기 안좋은 코딩
		buf[0] = len;
		buf[1] = char(session_id);
		buf[2] = char(x);
		buf[3] = char(y);
		return buf;
	}
};

int main()
{
	std::wcout.imbue(std::locale("korean"));
	WSADATA WSAData;
	WSAStartup(MAKEWORD(2, 0), &WSAData);

	SOCKET server_s = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, nullptr, 0, WSA_FLAG_OVERLAPPED); 
	SOCKADDR_IN server_a;
	server_a.sin_family = AF_INET;
	server_a.sin_port = htons(PORT); 
	server_a.sin_addr.S_un.S_addr = htonl(INADDR_ANY);
	bind(server_s, reinterpret_cast<sockaddr*>(&server_a), sizeof(server_a));
	listen(server_s, SOMAXCONN);
	int addr_size = sizeof(server_a);
	int id = 0;
	

	while (!b_shutdown) {
		SOCKET client_s = WSAAccept(server_s, reinterpret_cast<sockaddr*>(&server_a), &addr_size, nullptr, 0);
		g_players.try_emplace(id, client_s, id);
		// 요기만 수정하면 될 듯!
		g_players[id].broadcast(g_players[id].makedata());
		g_players[id].recv();
		id++;
	}
	g_players.clear();
	g_session_map.clear();
	closesocket(server_s);
	WSACleanup();
}

void send_callback(DWORD err, DWORD send_size, LPWSAOVERLAPPED pover, DWORD send_flag)
{
	if (0 != err){
		print_error("WSASend", WSAGetLastError());
	}
	auto b = reinterpret_cast<EXP_OVER*>(pover);
	delete b;
}

void recv_callback(DWORD err, DWORD recv_size, LPWSAOVERLAPPED pover, DWORD recv_flag) 
{
	int my_id = g_session_map[pover];
	if (0 != err) {
		print_error("WSARecv", WSAGetLastError());
	}

	// 로직 수행
	g_players[my_id].move();

	g_players[my_id].broadcast(g_players[my_id].makedata());
	g_players[my_id].recv();
}
