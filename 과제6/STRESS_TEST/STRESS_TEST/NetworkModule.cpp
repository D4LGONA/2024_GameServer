#define _WINSOCK_DEPRECATED_NO_WARNINGS

#include <WinSock2.h>
#include <winsock.h>
#include <Windows.h>
#include <iostream>
#include <thread>
#include <vector>
#include <unordered_set>
#include <mutex>
#include <atomic>
#include <chrono>
#include <queue>
#include <array>
#include <memory>
#include "..\..\SERVER\SERVER\protocol.h"

using namespace std;
using namespace chrono;

extern HWND		hWnd;
std::string s;


const static int MAX_TEST = 5000; // 최대동접
constexpr int MAX_CLIENTS = MAX_USER * 2 + MAX_NPC; // 최대세션은 2배
const static int INVALID_ID = -1;
const static int MAX_PACKET_SIZE = 255;
const static int MAX_BUFF_SIZE = 255;

#pragma comment (lib, "ws2_32.lib")

HANDLE g_hiocp;

enum OPTYPE { OP_SEND, OP_RECV, OP_DO_MOVE };

high_resolution_clock::time_point last_connect_time;

struct OverlappedEx {
	WSAOVERLAPPED over;
	WSABUF wsabuf;
	unsigned char IOCP_buf[MAX_BUFF_SIZE];
	OPTYPE event_type;
	int event_target;
};

struct CLIENT {
	int id;
	int x;
	int y;
	atomic_bool connected;

	SOCKET client_socket;
	OverlappedEx recv_over;
	unsigned char packet_buf[MAX_PACKET_SIZE];
	int prev_packet_data;
	int curr_packet_size;
	high_resolution_clock::time_point last_move_time;
};

array<int, MAX_CLIENTS> client_map; // 매핑테이블이라네요..
array<CLIENT, MAX_CLIENTS> g_clients;
atomic_int num_connections;
atomic_int client_to_close;
atomic_int active_clients;

int			global_delay;				// ms단위, 1000이 넘으면 클라이언트 증가 종료

vector <thread*> worker_threads;
thread test_thread;

float point_cloud[MAX_TEST * 2];

// 나중에 NPC까지 추가 확장 용
struct ALIEN {
	int id;
	int x, y;
	int visible_count;
};

void error_display(const char* msg, int err_no)
{
	WCHAR* lpMsgBuf;
	FormatMessage(
		FORMAT_MESSAGE_ALLOCATE_BUFFER |
		FORMAT_MESSAGE_FROM_SYSTEM,
		NULL, err_no,
		MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		(LPTSTR)&lpMsgBuf, 0, NULL);
	std::cout << msg;
	std::wcout << L"에러" << lpMsgBuf << std::endl;

	MessageBox(hWnd, lpMsgBuf, L"ERROR", 0);
	LocalFree(lpMsgBuf);
	// while (true);
}

void DisconnectClient(int ci)
{
	bool status = true;
	if (true == atomic_compare_exchange_strong(&g_clients[ci].connected, &status, false)) {
		closesocket(g_clients[ci].client_socket);
		active_clients--;
	}
	// cout << "Client [" << ci << "] Disconnected!\n";
}

void SendPacket(int cl, void* packet)
{
	int psize = reinterpret_cast<unsigned char*>(packet)[0];
	int ptype = reinterpret_cast<unsigned char*>(packet)[1];
	OverlappedEx* over = new OverlappedEx;
	over->event_type = OP_SEND;
	memcpy(over->IOCP_buf, packet, psize);
	ZeroMemory(&over->over, sizeof(over->over));
	over->wsabuf.buf = reinterpret_cast<CHAR*>(over->IOCP_buf);
	over->wsabuf.len = psize;
	int ret = WSASend(g_clients[cl].client_socket, &over->wsabuf, 1, NULL, 0,
		&over->over, NULL);
	if (0 != ret) {
		int err_no = WSAGetLastError();
		if (WSA_IO_PENDING != err_no)
			error_display("Error in SendPacket:", err_no);
	}
	// std::cout << "Send Packet [" << ptype << "] To Client : " << cl << std::endl;
}

void ProcessPacket(int ci, unsigned char packet[]) // ci 세션에서 패킷이 도착했다.
{
	switch (packet[1]) {
	case SC_MOVE_PLAYER: { // 어떤 패킷인지 확인하고 무브 패킷 처리해야 함
		SC_MOVE_PLAYER_PACKET* move_packet = reinterpret_cast<SC_MOVE_PLAYER_PACKET*>(packet); // 타입캐스팅
		if (move_packet->id < MAX_CLIENTS) { // 오버플로우가 아닌 경우
			int my_id = client_map[move_packet->id]; // 누가 움직였는지 확인
			if (-1 != my_id) { // 접속되어있는 내가아닌애라면 x, y좌표를 업데이트 한다
				g_clients[my_id].x = move_packet->x;
				g_clients[my_id].y = move_packet->y;
			}
			if (ci == my_id) { // 내가 움직였다
				if (0 != move_packet->move_time) {
					auto d_ms = duration_cast<milliseconds>(high_resolution_clock::now().time_since_epoch()).count() - move_packet->move_time; // 시간측정

					if (global_delay < d_ms) global_delay++; // 클라마다 딜레이가 다 다르기 때문에 평균을 내는 용도,
					else if (global_delay > d_ms) global_delay--; // 딜레이를 모으는것은 메모리낭비/오버헤드큼/예전의 딜레이는 알필요없음
				}
			}
		}
	}
					   break;
	case SC_ADD_PLAYER: break;
	case SC_REMOVE_PLAYER: break;
	case SC_LOGIN_INFO: // 로그인 패킷은 처리해야 함
	{
		g_clients[ci].connected = true;
		active_clients++;
		SC_LOGIN_INFO_PACKET* login_packet = reinterpret_cast<SC_LOGIN_INFO_PACKET*>(packet);
		int my_id = ci; // 이 세션에서 패킷이 날라온 것.
		client_map[login_packet->id] = my_id;
		g_clients[my_id].id = login_packet->id;
		g_clients[my_id].x = login_packet->x; // 세션을 열심히 업데이트 해줌
		g_clients[my_id].y = login_packet->y; // 세션이 활성화 됨

		//cs_packet_teleport t_packet;
		//t_packet.size = sizeof(t_packet);
		//t_packet.type = CS_TELEPORT;
		//SendPacket(my_id, &t_packet);
	}
	break;
	default: MessageBox(hWnd, L"Unknown Packet Type", L"ERROR", 0);
		while (true);
	}
}

void Worker_Thread() // 패킷 처리하는 스레드
{
	while (true) {
		DWORD io_size;
		unsigned long long ci;
		OverlappedEx* over;
		BOOL ret = GetQueuedCompletionStatus(g_hiocp, &io_size, &ci,
			reinterpret_cast<LPWSAOVERLAPPED*>(&over), INFINITE);
		// std::cout << "GQCS :";
		int client_id = static_cast<int>(ci);
		if (FALSE == ret) {
			int err_no = WSAGetLastError();
			if (64 == err_no) DisconnectClient(client_id);
			else {
				// error_display("GQCS : ", WSAGetLastError());
				DisconnectClient(client_id);
			}
			if (OP_SEND == over->event_type) delete over;
		}
		if (0 == io_size) {
			DisconnectClient(client_id);
			continue;
		}
		if (OP_RECV == over->event_type) {
			//std::cout << "RECV from Client :" << ci;
			//std::cout << "  IO_SIZE : " << io_size << std::endl;
			unsigned char* buf = g_clients[ci].recv_over.IOCP_buf;
			unsigned psize = g_clients[ci].curr_packet_size;
			unsigned pr_size = g_clients[ci].prev_packet_data;
			while (io_size > 0) { // 패킷 재조립
				if (0 == psize) psize = buf[0];
				if (io_size + pr_size >= psize) {
					// 지금 패킷 완성 가능
					unsigned char packet[MAX_PACKET_SIZE];
					memcpy(packet, g_clients[ci].packet_buf, pr_size);
					memcpy(packet + pr_size, buf, psize - pr_size);
					ProcessPacket(static_cast<int>(ci), packet);
					io_size -= psize - pr_size;
					buf += psize - pr_size;
					psize = 0; pr_size = 0;
				}
				else {
					memcpy(g_clients[ci].packet_buf + pr_size, buf, io_size);
					pr_size += io_size;
					io_size = 0;
				}
			}
			g_clients[ci].curr_packet_size = psize;
			g_clients[ci].prev_packet_data = pr_size;
			DWORD recv_flag = 0;
			int ret = WSARecv(g_clients[ci].client_socket,
				&g_clients[ci].recv_over.wsabuf, 1,
				NULL, &recv_flag, &g_clients[ci].recv_over.over, NULL);
			if (SOCKET_ERROR == ret) {
				int err_no = WSAGetLastError();
				if (err_no != WSA_IO_PENDING)
				{
					//error_display("RECV ERROR", err_no);
					DisconnectClient(client_id);
				}
			}
		}
		else if (OP_SEND == over->event_type) { 
			if (io_size != over->wsabuf.len) {
				// std::cout << "Send Incomplete Error!\n";
				DisconnectClient(client_id);
			}
			delete over;
		}
		else if (OP_DO_MOVE == over->event_type) {
			// Not Implemented Yet
			delete over;
		}
		else {
			std::cout << "Unknown GQCS event!\n";
			while (true);
		}
	}
}

constexpr int DELAY_LIMIT = 100;
constexpr int DELAY_LIMIT2 = 150;
constexpr int ACCEPT_DELY = 50;

void Adjust_Number_Of_Client()
{
	static int delay_multiplier = 1;
	static int max_limit = MAXINT;
	static bool increasing = true;

	if (active_clients >= MAX_TEST) return;
	if (num_connections >= MAX_CLIENTS) return;

	auto duration = high_resolution_clock::now() - last_connect_time;
	if (ACCEPT_DELY * delay_multiplier > duration_cast<milliseconds>(duration).count()) return; // connect를 한 후 충분한 시간이 지났나?

	int t_delay = global_delay;
	if (DELAY_LIMIT2 < t_delay) {
		if (true == increasing) {
			max_limit = active_clients;
			increasing = false;
		}
		if (100 > active_clients) return;
		if (ACCEPT_DELY * 10 > duration_cast<milliseconds>(duration).count()) return;
		last_connect_time = high_resolution_clock::now();
		DisconnectClient(client_to_close); // delay가 너무 크면 클라 개수를 줄이기
		client_to_close++;
		return;
	}
	else
		if (DELAY_LIMIT < t_delay) {
			delay_multiplier = 10;
			return;
		}
	if (max_limit - (max_limit / 20) < active_clients) return;

	increasing = true;
	last_connect_time = high_resolution_clock::now();
	g_clients[num_connections].client_socket = WSASocketW(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, WSA_FLAG_OVERLAPPED);

	SOCKADDR_IN ServerAddr;
	ZeroMemory(&ServerAddr, sizeof(SOCKADDR_IN));
	ServerAddr.sin_family = AF_INET;
	ServerAddr.sin_port = htons(PORT_NUM);
	ServerAddr.sin_addr.s_addr = inet_addr(s.c_str());


	int Result = WSAConnect(g_clients[num_connections].client_socket, (sockaddr*)&ServerAddr, sizeof(ServerAddr), NULL, NULL, NULL, NULL);
	if (0 != Result) {
		error_display("WSAConnect : ", GetLastError());
	}

	g_clients[num_connections].curr_packet_size = 0;
	g_clients[num_connections].prev_packet_data = 0;
	ZeroMemory(&g_clients[num_connections].recv_over, sizeof(g_clients[num_connections].recv_over));
	g_clients[num_connections].recv_over.event_type = OP_RECV;
	g_clients[num_connections].recv_over.wsabuf.buf =
		reinterpret_cast<CHAR*>(g_clients[num_connections].recv_over.IOCP_buf);
	g_clients[num_connections].recv_over.wsabuf.len = sizeof(g_clients[num_connections].recv_over.IOCP_buf);

	DWORD recv_flag = 0;
	CreateIoCompletionPort(reinterpret_cast<HANDLE>(g_clients[num_connections].client_socket), g_hiocp, num_connections, 0);

	CS_LOGIN_PACKET l_packet;

	int temp = num_connections;
	sprintf_s(l_packet.name, "%d", temp);
	l_packet.size = sizeof(l_packet);
	l_packet.type = CS_LOGIN;
	SendPacket(num_connections, &l_packet);


	int ret = WSARecv(g_clients[num_connections].client_socket, &g_clients[num_connections].recv_over.wsabuf, 1,
		NULL, &recv_flag, &g_clients[num_connections].recv_over.over, NULL);
	if (SOCKET_ERROR == ret) {
		int err_no = WSAGetLastError();
		if (err_no != WSA_IO_PENDING)
		{
			error_display("RECV ERROR", err_no);
			goto fail_to_connect;
		}
	}
	num_connections++;
fail_to_connect:
	return;
}

void Test_Thread() // ai가 돌아가는 스레드
{
	while (true) {
		//Sleep(max(20, global_delay));
		Adjust_Number_Of_Client(); // 동접조절, 0부터 천천히 올리면서 조절함.

		for (int i = 0; i < num_connections; ++i) { // ai 돌리는 루프
			if (false == g_clients[i].connected) continue;
			if (g_clients[i].last_move_time + 1s > high_resolution_clock::now()) continue; // 1초에 한번만 보내도록
			g_clients[i].last_move_time = high_resolution_clock::now();
			CS_MOVE_PACKET my_packet;
			my_packet.size = sizeof(my_packet);
			my_packet.type = CS_MOVE;
			switch (rand() % 4) {
			case 0: my_packet.direction = 0; break;
			case 1: my_packet.direction = 1; break;
			case 2: my_packet.direction = 2; break;
			case 3: my_packet.direction = 3; break;
			}
			my_packet.move_time = static_cast<unsigned>(duration_cast<milliseconds>(high_resolution_clock::now().time_since_epoch()).count());
			SendPacket(i, &my_packet);
		}
	}
}

void InitializeNetwork() // 세션 초기화, iocp 서버 운영
{
	std::cout << "ip주소 입력: ";
	std::cin >> s;

	for (auto& cl : g_clients) {
		cl.connected = false;
		cl.id = INVALID_ID;
	}

	for (auto& cl : client_map) cl = -1;
	num_connections = 0;
	last_connect_time = high_resolution_clock::now();

	WSADATA	wsadata;
	WSAStartup(MAKEWORD(2, 2), &wsadata);

	g_hiocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, 0, NULL, 0);

	for (int i = 0; i < 6; ++i)
		worker_threads.push_back(new std::thread{ Worker_Thread }); // 패킷처리

	test_thread = thread{ Test_Thread }; // ai 스레드
}

void ShutdownNetwork()
{
	test_thread.join();
	for (auto pth : worker_threads) {
		pth->join();
		delete pth;
	}
}

void Do_Network()
{
	return;
}

void GetPointCloud(int* size, float** points)
{
	int index = 0;
	for (int i = 0; i < num_connections; ++i)
		if (true == g_clients[i].connected) {
			point_cloud[index * 2] = static_cast<float>(g_clients[i].x);
			point_cloud[index * 2 + 1] = static_cast<float>(g_clients[i].y);
			index++;
		}

	*size = index;
	*points = point_cloud;
}

