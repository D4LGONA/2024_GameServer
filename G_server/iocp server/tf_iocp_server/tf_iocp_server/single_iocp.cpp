#include <iostream>
#include <array> // emplace erase 안하니까 map 안씀
#include <WS2tcpip.h>
#include <MSWSock.h>
#include <thread>
#include <mutex>
#include <vector>
#include "protocol.h" // 클라랑 서버랑 공유해야하는 내용

#pragma comment(lib, "WS2_32.lib")
#pragma comment(lib, "MSWSock.lib")
using namespace std;
constexpr int MAX_USER = 10;

enum COMP_TYPE { OP_ACCEPT, OP_RECV, OP_SEND };
enum class S_STATE {ST_FREE, ST_ALLOC, ST_INGAME};
class OVER_EXP {
public:
	WSAOVERLAPPED _over;
	WSABUF _wsabuf;
	char _send_buf[BUF_SIZE];
	COMP_TYPE _comp_type;
	SOCKET _client_socket;
	OVER_EXP() // recv에서 사용
	{
		_wsabuf.len = BUF_SIZE;
		_wsabuf.buf = _send_buf;
		_comp_type = OP_RECV;
		ZeroMemory(&_over, sizeof(_over));
	}
	OVER_EXP(unsigned char* packet)
	{
		_wsabuf.len = packet[0];
		_wsabuf.buf = _send_buf;
		ZeroMemory(&_over, sizeof(_over));
		_comp_type = OP_SEND;
		memcpy(_send_buf, packet, packet[0]); // 완료될때까지 보관.
	}
};

class SESSION {
	OVER_EXP _recv_over;

public:
	std::atomic<S_STATE> in_use; // 이 세션이 로그인 되어 있는지?
	int _id;
	SOCKET _socket;
	short	x, y;
	char	_name[NAME_SIZE];

	int		_prev_remain;
public:
	SESSION() : _socket(0), in_use(S_STATE::ST_FREE)
	{
		_id = -1;
		x = y = 0;
		_name[0] = 0;
		_prev_remain = 0;
	}

	~SESSION() {}

	void do_recv()
	{
		DWORD recv_flag = 0;
		memset(&_recv_over._over, 0, sizeof(_recv_over._over));
		_recv_over._wsabuf.len = BUF_SIZE - _prev_remain; // 버퍼가 넘치지 않게..
		_recv_over._wsabuf.buf = _recv_over._send_buf + _prev_remain; // 버퍼의 앞부분에 지난번에 짤린 조각이
		WSARecv(_socket, &_recv_over._wsabuf, 1, 0, &recv_flag,
			&_recv_over._over, 0);
	}

	void do_send(void* packet)
	{
		OVER_EXP* sdata = new OVER_EXP{ reinterpret_cast<unsigned char*>(packet) };
		WSASend(_socket, &sdata->_wsabuf, 1, 0, 0, &sdata->_over, 0);
	}

	void send_login_info_packet() // 로그인패킷의 보답.
	{
		SC_LOGIN_INFO_PACKET p; // 얘가 날라가서 센드에서 복사해서 사용
		p.id = _id;
		p.size = sizeof(SC_LOGIN_INFO_PACKET);
		p.type = SC_LOGIN_INFO;
		p.x = x;
		p.y = y;
		do_send(&p);
	}

	void send_move_packet(int c_id);
};

array<SESSION, MAX_USER> clients;
HANDLE g_hiocp;

void SESSION::send_move_packet(int c_id) // 화면에 그려진 오브젝트들중 누가 이동하는지?
{
	SC_MOVE_PLAYER_PACKET p;
	p.id = c_id;
	p.size = sizeof(SC_MOVE_PLAYER_PACKET);
	p.type = SC_MOVE_PLAYER;
	p.x = clients[c_id].x;
	p.y = clients[c_id].y;
	do_send(&p);
}

int get_new_client_id() // 새 유저 만들기
{
	for (int i = 0; i < MAX_USER; ++i) // array여서 빈 세션이 있으면 그 아이디 이용
		if (clients[i].in_use == S_STATE::ST_FREE)
			return i;
	return -1;
}

void process_packet(int c_id, char* packet) // 서버가 처리하는 일!
{
	switch (packet[1]) { // packet[1] : 패킷의 타입. 어떤 패킷인지에 따라 다르게 동작.
	case CS_LOGIN: {
		CS_LOGIN_PACKET* p = reinterpret_cast<CS_LOGIN_PACKET*>(packet);
		strcpy_s(clients[c_id]._name, p->name);
		clients[c_id].in_use = S_STATE::ST_INGAME;
		clients[c_id].send_login_info_packet();

		// 나의 존재를 알리기!
		for (auto& pl : clients) {
			if (S_STATE::ST_INGAME != pl.in_use) continue; // 오프라인인 애 생략
			if (pl._id == c_id) continue; // 나 생략
			SC_ADD_PLAYER_PACKET add_packet; // 다른 친구들에게 내가 들어왔단걸 알리기!
			add_packet.id = c_id;
			strcpy_s(add_packet.name, p->name);
			add_packet.size = sizeof(add_packet);
			add_packet.type = SC_ADD_PLAYER;
			add_packet.x = clients[c_id].x;
			add_packet.y = clients[c_id].y;
			pl.do_send(&add_packet);
		}
		for (auto& pl : clients) {
			if (S_STATE::ST_INGAME != pl.in_use) continue;
			if (pl._id == c_id) continue;
			SC_ADD_PLAYER_PACKET add_packet;
			add_packet.id = pl._id;
			strcpy_s(add_packet.name, pl._name);
			add_packet.size = sizeof(add_packet);
			add_packet.type = SC_ADD_PLAYER;
			add_packet.x = pl.x;
			add_packet.y = pl.y;
			clients[c_id].do_send(&add_packet);
		}
		break;
	}
	case CS_MOVE: {
		CS_MOVE_PACKET* p = reinterpret_cast<CS_MOVE_PACKET*>(packet);
		short x = clients[c_id].x; // 기존 좌표를 읽어서 direction에 맞춰 이동
		short y = clients[c_id].y;
		switch (p->direction) {
		case 0: if (y > 0) y--; break;
		case 1: if (y < W_HEIGHT - 1) y++; break;
		case 2: if (x > 0) x--; break;
		case 3: if (x < W_WIDTH - 1) x++; break;
		}
		clients[c_id].x = x;
		clients[c_id].y = y;
		for (auto& pl : clients)
			if (S_STATE::ST_INGAME == pl.in_use)
				pl.send_move_packet(c_id);
		break;
	}
	}
}

void disconnect(int c_id) // 접속종료
{
	for (auto& pl : clients) {
		if (pl.in_use != S_STATE::ST_INGAME) continue;
		if (pl._id == c_id) continue;
		SC_REMOVE_PLAYER_PACKET p;
		p.id = c_id;
		p.size = sizeof(p);
		p.type = SC_REMOVE_PLAYER;
		pl.do_send(&p);
	}
	closesocket(clients[c_id]._socket);
	clients[c_id].in_use = S_STATE::ST_FREE;
}

void Initialize()
{
	for (int i = 0; i < clients.size(); ++i)
		clients[i]._id = i;
}

void worker(SOCKET server)
{
	while (true) {
		DWORD num_bytes;
		ULONG_PTR key;
		WSAOVERLAPPED* over = nullptr;
		BOOL ret = GetQueuedCompletionStatus(g_hiocp, &num_bytes, &key, &over, INFINITE);
		OVER_EXP* ex_over = reinterpret_cast<OVER_EXP*>(over);
		if (FALSE == ret) {
			if (ex_over->_comp_type == OP_ACCEPT) {
				cout << "Accept Error";
				exit(-1);
			}
			else { // send recv 에러가 나면 서버는 안죽고 그 클라만 삭제
				cout << "GQCS Error on client[" << key << "]\n";
				disconnect(static_cast<int>(key));
				if (ex_over->_comp_type == OP_SEND) delete ex_over;
				continue;
			}
		}

		switch (ex_over->_comp_type) {
		case OP_ACCEPT: { // 초기화 해줘야 함
			// accept를 여러개 돌릴수 있음. 그러면 get_new_client_id를 atomic하게 만들어야
			int client_id = get_new_client_id();
			SOCKET c_socket = ex_over->_client_socket;
			if (client_id != -1) { 
				// 요 clients가 Data race
				// 근데 같컴에선 패킷이 띄엄띄엄 옴
				// 그래서 문제를 찾을 수 없다
				clients[client_id].in_use = S_STATE::ST_ALLOC;
				clients[client_id].x = 0; 
				clients[client_id].y = 0;
				clients[client_id]._id = client_id;
				clients[client_id]._name[0] = 0;
				clients[client_id]._prev_remain = 0;
				clients[client_id]._socket = c_socket; 
				CreateIoCompletionPort(reinterpret_cast<HANDLE>(c_socket),
					g_hiocp, client_id, 0);
				clients[client_id].do_recv();
			}
			else {
				cout << "Max user exceeded.\n";
				closesocket(c_socket);
			}
			c_socket = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
			ZeroMemory(&ex_over->_over, sizeof(ex_over->_over));
			ex_over->_client_socket = c_socket;
			int addr_size = sizeof(SOCKADDR_IN);
			AcceptEx(server, c_socket, ex_over->_send_buf, 0, addr_size + 16, addr_size + 16, 0, &ex_over->_over);
			break;
		}
		case OP_RECV: {
			int remain_data = num_bytes + clients[key]._prev_remain;
			char* p = ex_over->_send_buf;
			while (remain_data > 0) { // 패킷 잘린거 처리.
				int packet_size = p[0];
				if (packet_size <= remain_data) {
					process_packet(static_cast<int>(key), p);
					p = p + packet_size;
					remain_data = remain_data - packet_size;
				}
				else break;
			}
			clients[key]._prev_remain = remain_data;
			if (remain_data > 0)
				memcpy(ex_over->_send_buf, p, remain_data);
			clients[key].do_recv();
			break;
		}
		case OP_SEND:
			delete ex_over;
			break;
		}
	}
}

int main()
{
	

	WSADATA WSAData;
	WSAStartup(MAKEWORD(2, 2), &WSAData);
	SOCKET server = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
	SOCKADDR_IN server_addr;
	memset(&server_addr, 0, sizeof(server_addr));
	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(PORT_NUM);
	server_addr.sin_addr.S_un.S_addr = INADDR_ANY;
	bind(server, reinterpret_cast<sockaddr*>(&server_addr), sizeof(server_addr));
	listen(server, SOMAXCONN);
	SOCKADDR_IN cl_addr;
	int addr_size = sizeof(cl_addr);
	int client_id = 0;

	g_hiocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, 0, 0, 0);
	CreateIoCompletionPort(reinterpret_cast<HANDLE>(server), g_hiocp, 9999, 0);
	SOCKET c_socket = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
	OVER_EXP a_over;
	a_over._comp_type = OP_ACCEPT;
	a_over._client_socket = c_socket;
	AcceptEx(server, c_socket, a_over._send_buf, 0, addr_size + 16, addr_size + 16, 0, &a_over._over);

	Initialize();

	// 멀티스레드에서 돌려야 한다
	int num_threads = std::thread::hardware_concurrency();
	std::vector<std::thread> worker_threads;
	for (int i = 0; i < num_threads; ++i)
		worker_threads.emplace_back(worker, server);
	for (auto& w : worker_threads)
		w.join();
	
	closesocket(server);
	WSACleanup();
}
