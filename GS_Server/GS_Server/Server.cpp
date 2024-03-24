#include <iostream>
#include <WS2tcpip.h>
#include <unordered_map>
#include <vector>
#include <array>
#pragma comment (lib, "WS2_32.LIB")

constexpr short PORT = 4000;
bool b_shutdown = false; // 서버에 연결된 말이 하나도 없을때
std::unordered_map<LPWSAOVERLAPPED, int> g_session_map;
std::unordered_map<int, class SESSION> g_players;
void CALLBACK send_callback(DWORD err, DWORD send_size, LPWSAOVERLAPPED pover, DWORD send_flag);
void CALLBACK recv_callback(DWORD err, DWORD recv_size, LPWSAOVERLAPPED pover, DWORD recv_flag);

#pragma pack(push, 1)
struct pts
{
	char key{-1};
	char x{ -1 };
	char y{ -1 };
};
#pragma pack(pop)
std::array<struct pts, 10> arr;

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

class SESSION {
	char recv_buf;
	WSABUF wsabuf[1];
	SOCKET client_s;
	WSAOVERLAPPED over;
	int x{ 0 };
	int y{ 0 };
	int key;

public:
	SESSION() { std::cout << "error"; exit(0); };
	SESSION(SOCKET s, int id) : client_s{ s } 
	{ 
		g_session_map[&over] = id; 
		key = id; 
	}
	~SESSION() 
	{ 
		closesocket(client_s);

		//if (g_players.size() == 0) // 서버 종료 조건
		//	b_shutdown = true;
	};

	void recv()
	{
		wsabuf[0].buf = &recv_buf;
		wsabuf[0].len = sizeof(recv_buf);
		DWORD recv_flag = 0;
		ZeroMemory(&over, sizeof(over));
		int res = WSARecv(client_s, wsabuf, 1, nullptr, &recv_flag, &over, recv_callback);
		if (0 != res) {
			int err_no = WSAGetLastError();
			if (WSA_IO_PENDING != err_no)
				print_error("WSARecv", WSAGetLastError());
		}
	}

	void send()
	{
		std::string sendData;
		for (const auto& pt : arr) {
			sendData.push_back(pt.key);
			sendData.push_back(pt.x);
			sendData.push_back(pt.y);
		}

		std::vector<char> buffer(sendData.begin(), sendData.end());
		wsabuf[0].buf = buffer.data();
		wsabuf[0].len = buffer.size();

		int res = WSASend(client_s, wsabuf, 1, nullptr, 0, &over, send_callback);
		if (0 != res) {
			print_error("WSASend", WSAGetLastError());
		}
	}

	void move()
	{
		switch (recv_buf)
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
			g_players.erase(key);
			g_session_map.erase(&over);
			return;
			break;
		}
	}
	
	pts GETTTER() { return { static_cast<char>(key), static_cast<char>(x), static_cast<char>(y) }; }
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
		g_players[id++].send();
	}
	g_players.clear();
	g_session_map.clear();
	closesocket(server_s);
	WSACleanup();
}

void send_callback(DWORD err, DWORD send_size, LPWSAOVERLAPPED pover, DWORD send_flag)
{
	// recv할준비?
	if (0 != err){
		print_error("WSASend", WSAGetLastError());
	}
	g_players[g_session_map[pover]].recv();
}

void recv_callback(DWORD err, DWORD recv_size, LPWSAOVERLAPPED pover, DWORD recv_flag) 
// recv시에 호출 -> 모든 세션들에게 
{
	// 받은 키로 로직 수행
	int my_id = g_session_map[pover];
	if (0 != err) {
		print_error("WSARecv", WSAGetLastError());
	}

	g_players[my_id].move();

	// 모든 세션들에게 좌표값을 send하라고 해야 하는데 이걸 어떻게 넘겨줘야 할지...\
	// 좌표값으로 된 vector 받기. 
	int cnt = 0;
	for (auto& pair : g_players)
	{
		arr[cnt++] = pair.second.GETTTER();
	}

	// vector send하라고 하기.
	for (auto& pair : g_players)
		pair.second.send();
}
