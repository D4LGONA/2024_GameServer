#include <iostream>
#include <array>
#include <WS2tcpip.h>
#include <MSWSock.h>
#include <thread>
#include <vector>
#include <mutex>
#include <unordered_set>
#include <concurrent_priority_queue.h>
#include "protocol.h"

#include "include/lua.hpp"

#pragma comment(lib, "WS2_32.lib")
#pragma comment(lib, "MSWSock.lib")
#pragma comment(lib, "lua54.lib")
using namespace std;

constexpr int SECTOR_SIZE = 40; // 40 * 40
constexpr int VIEW_RANGE = 5;

struct BLOCK {
	short dx;
	short dy;
};

const BLOCK Nears[] = {
	{-1, -1}, {-1, 0}, {-1, 1},
	{0, -1}, {0, 0}, {0, 1},
	{1, -1}, {1, 0}, {1, 1}
};

enum EVENT_TYPE { EV_SEND_HELLO, EV_RANDOM_MOVE, EV_SEND_BYE};

struct TIMER_EVENT {
	int obj_id;
	chrono::system_clock::time_point wakeup_time;
	EVENT_TYPE event_id;
	int target_id;
	constexpr bool operator < (const TIMER_EVENT& L) const
	{
		return (wakeup_time > L.wakeup_time);
	}
};
concurrency::concurrent_priority_queue<TIMER_EVENT> timer_queue;

enum COMP_TYPE { OP_ACCEPT, OP_RECV, OP_SEND, OP_NPC_MOVE, OP_AI_HELLO, OP_AI_BYE };
class OVER_EXP {
public:
	WSAOVERLAPPED _over;
	WSABUF _wsabuf;
	char _send_buf[BUF_SIZE];
	COMP_TYPE _comp_type;
	int _ai_target_obj;
	OVER_EXP()
	{
		_wsabuf.len = BUF_SIZE;
		_wsabuf.buf = _send_buf;
		_comp_type = OP_RECV;
		ZeroMemory(&_over, sizeof(_over));
	}
	OVER_EXP(char* packet)
	{
		_wsabuf.len = packet[0];
		_wsabuf.buf = _send_buf;
		ZeroMemory(&_over, sizeof(_over));
		_comp_type = OP_SEND;
		memcpy(_send_buf, packet, packet[0]);
	}
};

enum S_STATE { ST_FREE, ST_ALLOC, ST_INGAME };
class SESSION {
	OVER_EXP _recv_over;

public:
	mutex _s_lock;
	S_STATE _state;
	atomic_bool	_is_active;		// 주위에 플레이어가 있는가?
	int _id;
	SOCKET _socket;
	short	x, y;
	char	_name[NAME_SIZE];
	int		_prev_remain;
	unordered_set <int> _view_list;
	mutex	_vl;
	int		last_move_time;
	lua_State*	_L;
	mutex	_ll;
	int ai_move_count = 0;
	short sector_x, sector_y;
public:
	SESSION()
	{
		_id = -1;
		_socket = 0;
		x = y = 0;
		_name[0] = 0;
		_state = ST_FREE;
		_prev_remain = 0;
	}

	~SESSION() {}

	void do_recv()
	{
		DWORD recv_flag = 0;
		memset(&_recv_over._over, 0, sizeof(_recv_over._over));
		_recv_over._wsabuf.len = BUF_SIZE - _prev_remain;
		_recv_over._wsabuf.buf = _recv_over._send_buf + _prev_remain;
		WSARecv(_socket, &_recv_over._wsabuf, 1, 0, &recv_flag,
			&_recv_over._over, 0);
	}

	void do_send(void* packet)
	{
		OVER_EXP* sdata = new OVER_EXP{ reinterpret_cast<char*>(packet) };
		WSASend(_socket, &sdata->_wsabuf, 1, 0, 0, &sdata->_over, 0);
	}
	void send_login_info_packet()
	{
		SC_LOGIN_INFO_PACKET p;
		p.id = _id;
		p.size = sizeof(SC_LOGIN_INFO_PACKET);
		p.type = SC_LOGIN_INFO;
		p.x = x;
		p.y = y;
		do_send(&p);
	}
	void send_move_packet(int c_id);
	void send_add_player_packet(int c_id);
	void send_chat_packet(int c_id, const char* mess);
	void send_remove_player_packet(int c_id)
	{
		_vl.lock();
		if (_view_list.count(c_id))
			_view_list.erase(c_id);
		else {
			_vl.unlock();
			return;
		}
		_vl.unlock();
		SC_REMOVE_OBJECT_PACKET p;
		p.id = c_id;
		p.size = sizeof(p);
		p.type = SC_REMOVE_OBJECT;
		do_send(&p);
	}
};

HANDLE h_iocp;
array<SESSION, MAX_USER + MAX_NPC> clients;

// NPC 구현 첫번째 방법
//  NPC클래스를 별도 제작, NPC컨테이너를 따로 생성한다.
//  장점 : 깔끔하다, 군더더기가 없다.
//  단점 : 플레이어와 NPC가 따로논다. 똑같은 역할을 수행하는 함수를 여러개씩 중복 작성해야 한다.
//         예) bool can_see(int from, int to)
//                 => bool can_see_p2p()
//				    bool can_see_p2n()
//					bool can_see_n2n()

// NPC 구현 두번째 방법  <===== 실습에서 사용할 방법.
//   clients 컨테이너에 NPC도 추가한다.
//   장점 : 플레이어와 NPC를 동일하게 취급할 수 있어서, 프로그래밍 작성 부하가 줄어든다.
//   단점 : 사용하지 않는 멤버들로 인한 메모리 낭비.

// NPC 구현 세번째 방법  (실제로 많이 사용되는 방법)
//   클래스 상속기능을 사용한다.
//     SESSION은 NPC클래스를 상속받아서 네트워크 관련 기능을 추가한 형태로 정의한다.
//       clients컨테이너를 objects컨테이너로 변경하고, 컨테이너는 NPC의 pointer를 저장한다.
//      장점 : 메모리 낭비가 없다, 함수의 중복작성이 필요없다.
//          (포인터로 관리되므로 player id의 중복사용 방지를 구현하기 쉬워진다 => Data Race 방지를 위한 추가 구현이 필요)
//      단점 : 포인터가 사용되고, reinterpret_cast가 필요하다. (별로 단점이 안니다).

SOCKET g_s_socket, g_c_socket;
OVER_EXP g_a_over;
array<array<unordered_set<int>, W_HEIGHT / SECTOR_SIZE>, W_WIDTH / SECTOR_SIZE> g_ObjectListSector;
mutex g_SectorLock;

bool is_pc(int object_id)
{
	return object_id < MAX_USER;
}

bool is_npc(int object_id)
{
	return !is_pc(object_id);
}

bool can_see(int from, int to)
{
	if (abs(clients[from].x - clients[to].x) > VIEW_RANGE) return false;
	return abs(clients[from].y - clients[to].y) <= VIEW_RANGE;
}

void SESSION::send_move_packet(int c_id)
{
	SC_MOVE_OBJECT_PACKET p;
	p.id = c_id;
	p.size = sizeof(SC_MOVE_OBJECT_PACKET);
	p.type = SC_MOVE_OBJECT;
	p.x = clients[c_id].x;
	p.y = clients[c_id].y;
	p.move_time = clients[c_id].last_move_time;
	do_send(&p);
}

void SESSION::send_add_player_packet(int c_id)
{
	SC_ADD_OBJECT_PACKET add_packet;
	add_packet.id = c_id;
	strcpy_s(add_packet.name, clients[c_id]._name);
	add_packet.size = sizeof(add_packet);
	add_packet.type = SC_ADD_OBJECT;
	add_packet.x = clients[c_id].x;
	add_packet.y = clients[c_id].y;
	_vl.lock();
	_view_list.insert(c_id);
	_vl.unlock();
	do_send(&add_packet);
}

void SESSION::send_chat_packet(int p_id, const char* mess)
{
	SC_CHAT_PACKET packet;
	packet.id = p_id;
	packet.size = sizeof(packet);
	packet.type = SC_CHAT;
	strcpy_s(packet.mess, mess);
	do_send(&packet);
}

int get_new_client_id()
{
	for (int i = 0; i < MAX_USER; ++i) {
		lock_guard <mutex> ll{ clients[i]._s_lock };
		if (clients[i]._state == ST_FREE)
			return i;
	}
	return -1;
}

void add_timer(int key, EVENT_TYPE ev, int time, int from)
{
	TIMER_EVENT evt;
	evt.event_id = ev; // 어떤 이벤트가
	evt.obj_id = key; // 누구한테
	evt.wakeup_time = chrono::system_clock::now() + chrono::milliseconds(time); // 얼마뒤에
	evt.target_id = from; // 누구에 의해
	
	timer_queue.push(evt); // 타이머 큐에 넣기
}

//void WakeUpNPC(int npc_id, int waker) // 새로 시야에 들어온 친구한테 보내는 듯
//{
//	OVER_EXP* exover = new OVER_EXP; // 시야에 있으면 hello를 일단 보내고
//	exover->_comp_type = OP_AI_HELLO;
//	exover->_ai_target_obj = waker;
//	PostQueuedCompletionStatus(h_iocp, 1, npc_id, &exover->_over);
//
//	if (clients[npc_id]._is_active) return; // 작동중이지 않은 npc이면 리턴
//	bool old_state = false;
//	if (false == atomic_compare_exchange_strong(&clients[npc_id]._is_active, &old_state, true))
//		return; 
//	TIMER_EVENT ev{ npc_id, chrono::system_clock::now(), EV_RANDOM_MOVE, 0 }; // 아니라면 랜덤무브 동작
//	timer_queue.push(ev);
//}

void process_packet(int c_id, char* packet)
{
	switch (packet[1]) {
	case CS_LOGIN: {
		CS_LOGIN_PACKET* p = reinterpret_cast<CS_LOGIN_PACKET*>(packet);
		strcpy_s(clients[c_id]._name, p->name);
		{
			lock_guard<mutex> ll{ clients[c_id]._s_lock };
			clients[c_id].x = rand() % W_WIDTH;
			clients[c_id].y = rand() % W_HEIGHT;
			clients[c_id]._state = ST_INGAME;
		}
		clients[c_id].send_login_info_packet();
		{
			clients[c_id].sector_x = clients[c_id].x / SECTOR_SIZE;
			clients[c_id].sector_y = clients[c_id].y / SECTOR_SIZE;
			lock_guard<mutex> ll{ g_SectorLock }; // sector에 추가하는 부분
			g_ObjectListSector[clients[c_id].sector_x][clients[c_id].sector_y].insert(c_id);
		}
		for (const auto& n : Nears) {
			short sx = clients[c_id].sector_x + n.dx;
			short sy = clients[c_id].sector_y + n.dy;

			if (sx >= 0 && sx < W_WIDTH / SECTOR_SIZE && sy >= 0 && sy < W_HEIGHT / SECTOR_SIZE) {
				lock_guard<mutex> ll{ g_SectorLock };
				for (auto& i : g_ObjectListSector[sx][sy]) {
					if (false == can_see(c_id, i)) continue;
					if (true == is_npc(i) && clients[i]._is_active == false) {
						bool expt = false;
						if (true == atomic_compare_exchange_strong(&clients[i]._is_active, &expt, true))
							add_timer(clients[i]._id, EV_RANDOM_MOVE, 1000, c_id); // 로그인 시에 들어가는건 ok 아닌가?
					}
				}
			}
		}
	}
	case CS_MOVE: {
		CS_MOVE_PACKET* p = reinterpret_cast<CS_MOVE_PACKET*>(packet);
		clients[c_id].last_move_time = p->move_time;
		short x = clients[c_id].x;
		short y = clients[c_id].y;
		switch (p->direction) {
		case 0: if (y > 0) y--; break;
		case 1: if (y < W_HEIGHT - 1) y++; break;
		case 2: if (x > 0) x--; break;
		case 3: if (x < W_WIDTH - 1) x++; break;
		}
		clients[c_id].x = x;
		clients[c_id].y = y;

		if (clients[c_id].x / SECTOR_SIZE != clients[c_id].sector_x or
			clients[c_id].y / SECTOR_SIZE != clients[c_id].sector_y)
		{
			lock_guard<mutex> ll{ g_SectorLock }; // sector 이동
			g_ObjectListSector[clients[c_id].sector_x][clients[c_id].sector_y].erase(c_id);
			clients[c_id].sector_x = clients[c_id].x / SECTOR_SIZE;
			clients[c_id].sector_y = clients[c_id].y / SECTOR_SIZE;
			g_ObjectListSector[clients[c_id].sector_x][clients[c_id].sector_y].insert(c_id);
		}

		unordered_set<int> near_list;
		clients[c_id]._vl.lock();
		unordered_set<int> old_vlist = clients[c_id]._view_list;
		clients[c_id]._vl.unlock();

		// 주변 섹터 탐색(8방향 - 나중에 시야범위 늘렸을때 문제가 될 수 있으니!)
		for (const auto& n : Nears) {
			short sx = clients[c_id].sector_x + n.dx;
			short sy = clients[c_id].sector_y + n.dy;

			// new viewlist 만들기!

			if (sx >= 0 && sx < W_WIDTH / SECTOR_SIZE && sy >= 0 && sy < W_HEIGHT / SECTOR_SIZE) {
				lock_guard<mutex> ll{ g_SectorLock };
				for (auto& i : g_ObjectListSector[sx][sy]) {
					if (clients[i]._state != ST_INGAME) continue;
					if (false == can_see(c_id, i)) continue;
					if (i == c_id) continue;
					near_list.insert(i);
					if (true == is_npc(i) && clients[i]._is_active == false) {
						bool expt = false;
						if (true == atomic_compare_exchange_strong(&clients[i]._is_active, &expt, true))
							add_timer(clients[i]._id, EV_RANDOM_MOVE, 1000, c_id); // 새로 추가된 애들에 대해서만 랜덤무브를 실행한다
					}
				}
			}
		}

		/*for (auto& cl : clients) {
			if (cl._state != ST_INGAME) continue;
			if (cl._id == c_id) continue;
			if (can_see(c_id, cl._id))
				near_list.insert(cl._id);
		}*/

		clients[c_id].send_move_packet(c_id);

		for (auto& pl : near_list) { // 근처에 있는 친구들에 대해
			auto& cpl = clients[pl];
			if (is_pc(pl)) { // 플레이어라면
				cpl._vl.lock();
				if (clients[pl]._view_list.count(c_id)) { // 원래 보고있었다면
					cpl._vl.unlock();
					clients[pl].send_move_packet(c_id);
				}
				else { // 아니라면
					cpl._vl.unlock();
					clients[pl].send_add_player_packet(c_id);
				}
			}
			else add_timer(pl, EV_SEND_HELLO, 1000, c_id); // c_id가 pl을 깨운다

			if (old_vlist.count(pl) == 0)
				clients[c_id].send_add_player_packet(pl);
		}

		for (auto& pl : old_vlist)
			if (0 == near_list.count(pl)) {
				clients[c_id].send_remove_player_packet(pl);
				if (is_pc(pl))
					clients[pl].send_remove_player_packet(c_id);
			}
	}
				break;
	}
}

void disconnect(int c_id)
{
	clients[c_id]._vl.lock();
	unordered_set <int> vl = clients[c_id]._view_list;
	clients[c_id]._vl.unlock();
	for (auto& p_id : vl) {
		if (is_npc(p_id)) continue;
		auto& pl = clients[p_id];
		{
			lock_guard<mutex> ll(pl._s_lock);
			if (ST_INGAME != pl._state) continue;
		}
		if (pl._id == c_id) continue;
		pl.send_remove_player_packet(c_id);
	}
	closesocket(clients[c_id]._socket);

	lock_guard<mutex> ll(clients[c_id]._s_lock);
	clients[c_id]._state = ST_FREE;
	{
		lock_guard<mutex> ll(g_SectorLock);
		g_ObjectListSector[clients[c_id].sector_x][clients[c_id].sector_y].erase(c_id);
	}
}

void do_npc_random_move(int npc_id)
{
	SESSION& npc = clients[npc_id];
	unordered_set<int> old_vl;
	for (const auto& n : Nears) {
		short sx = npc.sector_x + n.dx;
		short sy = npc.sector_y + n.dy;
		// new viewlist 만들기!

		if (sx >= 0 && sx < W_WIDTH / SECTOR_SIZE && sy >= 0 && sy < W_HEIGHT / SECTOR_SIZE) {
			lock_guard<mutex> ll{ g_SectorLock };
			for (auto& i : g_ObjectListSector[sx][sy]) {
				if (clients[i]._state != ST_INGAME) continue;
				if (false == can_see(npc._id, i)) continue;
				if (false == is_npc(i)) old_vl.insert(i);
			}
		}
	}

	int x = npc.x;
	int y = npc.y;
	switch (rand() % 4) {
	case 0: if (x < (W_WIDTH - 1)) x++; break;
	case 1: if (x > 0) x--; break;
	case 2: if (y < (W_HEIGHT - 1)) y++; break;
	case 3:if (y > 0) y--; break;
	}
	npc.x = x;
	npc.y = y;

	if (x / SECTOR_SIZE != npc.sector_x or y / SECTOR_SIZE != npc.sector_y)
	{
		lock_guard<mutex> ll{ g_SectorLock }; // sector 이동
		g_ObjectListSector[npc.sector_x][npc.sector_y].erase(npc._id);
		npc.sector_x = x / SECTOR_SIZE;
		npc.sector_y = y / SECTOR_SIZE;
		g_ObjectListSector[npc.sector_x][npc.sector_y].insert(npc._id);
	}

	unordered_set<int> new_vl;

	for (const auto& n : Nears) {
		short sx = npc.sector_x + n.dx;
		short sy = npc.sector_y + n.dy;
		// new viewlist 만들기!

		if (sx >= 0 && sx < W_WIDTH / SECTOR_SIZE && sy >= 0 && sy < W_HEIGHT / SECTOR_SIZE) {
			lock_guard<mutex> ll{ g_SectorLock };
			for (auto& i : g_ObjectListSector[sx][sy]) {
				if (clients[i]._state != ST_INGAME) continue;
				if (false == can_see(npc._id, i)) continue;
				if (false == is_npc(i)) new_vl.insert(i);
			}
		}
	}

	for (auto pl : new_vl) {
		if (0 == old_vl.count(pl)) {
			// 플레이어의 시야에 등장
			clients[pl].send_add_player_packet(npc._id);
		}
		else {
			// 플레이어가 계속 보고 있음.
			clients[pl].send_move_packet(npc._id);
		}
	}
	///여기는 대체 뭐지 ?
	for (auto pl : old_vl) {
		if (0 == new_vl.count(pl)) {
			clients[pl]._vl.lock();
			if (0 != clients[pl]._view_list.count(npc._id)) {
				clients[pl]._vl.unlock();
				clients[pl].send_remove_player_packet(npc._id);
			}
			else {
				clients[pl]._vl.unlock();
			}
		}
	}
}

void worker_thread(HANDLE h_iocp)
{
	while (true) {
		DWORD num_bytes;
		ULONG_PTR key;
		WSAOVERLAPPED* over = nullptr;
		BOOL ret = GetQueuedCompletionStatus(h_iocp, &num_bytes, &key, &over, INFINITE);
		OVER_EXP* ex_over = reinterpret_cast<OVER_EXP*>(over);
		if (FALSE == ret) {
			if (ex_over->_comp_type == OP_ACCEPT) cout << "Accept Error";
			else {
				cout << "GQCS Error on client[" << key << "]\n";
				disconnect(static_cast<int>(key));
				if (ex_over->_comp_type == OP_SEND) delete ex_over;
				continue;
			}
		}

		if ((0 == num_bytes) && ((ex_over->_comp_type == OP_RECV) || (ex_over->_comp_type == OP_SEND))) {
			disconnect(static_cast<int>(key));
			if (ex_over->_comp_type == OP_SEND) delete ex_over;
			continue;
		}

		switch (ex_over->_comp_type) {
		case OP_ACCEPT: {
			int client_id = get_new_client_id();
			if (client_id != -1) {
				{
					lock_guard<mutex> ll(clients[client_id]._s_lock);
					clients[client_id]._state = ST_ALLOC;
				}
				clients[client_id].x = 0;
				clients[client_id].y = 0;
				clients[client_id]._id = client_id;
				clients[client_id]._name[0] = 0;
				clients[client_id]._prev_remain = 0;
				clients[client_id]._socket = g_c_socket;
				CreateIoCompletionPort(reinterpret_cast<HANDLE>(g_c_socket),
					h_iocp, client_id, 0);
				clients[client_id].do_recv();
				g_c_socket = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
			}
			else {
				cout << "Max user exceeded.\n";
			}
			ZeroMemory(&g_a_over._over, sizeof(g_a_over._over));
			int addr_size = sizeof(SOCKADDR_IN);
			AcceptEx(g_s_socket, g_c_socket, g_a_over._send_buf, 0, addr_size + 16, addr_size + 16, 0, &g_a_over._over);
			break;
		}
		case OP_RECV: {
			int remain_data = num_bytes + clients[key]._prev_remain;
			char* p = ex_over->_send_buf;
			while (remain_data > 0) {
				int packet_size = p[0];
				if (packet_size <= remain_data) {
					process_packet(static_cast<int>(key), p);
					p = p + packet_size;
					remain_data = remain_data - packet_size;
				}
				else break;
			}
			clients[key]._prev_remain = remain_data;
			if (remain_data > 0) {
				memcpy(ex_over->_send_buf, p, remain_data);
			}
			clients[key].do_recv();
			break;
		}
		case OP_SEND:
			delete ex_over;
			break;
		case OP_NPC_MOVE: {
			bool keep_alive = false;
			for (int j = 0; j < MAX_USER; ++j) { // j는 항상 플레이어
				if (clients[j]._state != ST_INGAME) continue;
				if (can_see(static_cast<int>(key), j)) { // todo: 여기도 sector 달아야함
					add_timer(key, EV_SEND_HELLO, 0, j);
					keep_alive = true;
					break;
				}
			}
			if (true == keep_alive) { // 랜덤무브 달아주기
				do_npc_random_move(static_cast<int>(key));
				add_timer(key, EV_RANDOM_MOVE, 1000, ex_over->_ai_target_obj);
				if (clients[key].ai_move_count != 0) { // bye가 필요한 시점이라면
					clients[key].ai_move_count += 1;
					if (clients[key].ai_move_count == 5)
						add_timer(key, EV_SEND_BYE, 0, ex_over->_ai_target_obj); // bye 보내기
				}
			}
			else {
				clients[key]._is_active = false;
			}
			delete ex_over;
		}
			break;
		case OP_AI_HELLO: { // 이벤트 큐에서 hello를 실행했을 때
			clients[key]._ll.lock();
			auto L = clients[key]._L;
			lua_getglobal(L, "event_player_move");
			lua_pushnumber(L, ex_over->_ai_target_obj);
			lua_pushstring(L, "HELLO");
			lua_pcall(L, 2, 0, 0);
			clients[key]._ll.unlock();
			delete ex_over;
		}
			break;

		case OP_AI_BYE: {
			clients[ex_over->_ai_target_obj].send_chat_packet(key, "BYE");
			clients[key].ai_move_count = 0;
			delete ex_over;
		}
			break;

		}
	}
}

int API_get_x(lua_State* L)
{
	int user_id =
		(int)lua_tointeger(L, -1);
	lua_pop(L, 2);
	int x = clients[user_id].x;
	lua_pushnumber(L, x);
	return 1;
}

int API_get_y(lua_State* L)
{
	int user_id =
		(int)lua_tointeger(L, -1);
	lua_pop(L, 2);
	int y = clients[user_id].y;
	lua_pushnumber(L, y);
	return 1;
}

int API_SendMessage(lua_State* L)
{
	int my_id = (int)lua_tointeger(L, -3);
	int user_id = (int)lua_tointeger(L, -2);
	char* mess = (char*)lua_tostring(L, -1);

	lua_pop(L, 4);

	clients[user_id].send_chat_packet(my_id, mess);
	clients[my_id].ai_move_count = 1;
	return 0;
}

void InitializeNPC()
{
	cout << "NPC intialize begin.\n";
	for (int i = MAX_USER; i < MAX_USER + MAX_NPC; ++i) {
		clients[i].x = rand() % W_WIDTH;
		clients[i].y = rand() % W_HEIGHT;
		{
			lock_guard<mutex> ll{ g_SectorLock }; // sector 이동
			clients[i].sector_x = clients[i].x / SECTOR_SIZE;
			clients[i].sector_y = clients[i].y / SECTOR_SIZE;
			g_ObjectListSector[clients[i].sector_x][clients[i].sector_y].insert(i);
		}
		clients[i]._id = i;
		sprintf_s(clients[i]._name, "NPC%d", i);
		clients[i]._state = ST_INGAME;

		auto L = clients[i]._L = luaL_newstate();
		luaL_openlibs(L);
		luaL_loadfile(L, "npc.lua");
		lua_pcall(L, 0, 0, 0);

		lua_getglobal(L, "set_uid");
		lua_pushnumber(L, i);
		lua_pcall(L, 1, 0, 0);
		// lua_pop(L, 1);// eliminate set_uid from stack after call

		lua_register(L, "API_SendMessage", API_SendMessage);
		lua_register(L, "API_get_x", API_get_x);
		lua_register(L, "API_get_y", API_get_y);
	}
	cout << "NPC initialize end.\n";
}

void do_timer()
{
	while (true) {
		TIMER_EVENT ev;
		auto current_time = chrono::system_clock::now();
		if (true == timer_queue.try_pop(ev)) {
			if (ev.wakeup_time > current_time) {
				timer_queue.push(ev);		// 최적화 필요
				// timer_queue에 다시 넣지 않고 처리해야 한다.
				continue;
			}
			switch (ev.event_id) {
			case EV_RANDOM_MOVE: { // 랜덤무브 이벤트
				OVER_EXP* ov = new OVER_EXP;
				ov->_comp_type = OP_NPC_MOVE;
				ov->_ai_target_obj = ev.target_id;
				PostQueuedCompletionStatus(h_iocp, 1, ev.obj_id, &ov->_over);
				break;
			}
			case EV_SEND_BYE: { // bye 전송
				OVER_EXP* ov = new OVER_EXP;
				ov->_comp_type = OP_AI_BYE;
				ov->_ai_target_obj = ev.target_id;
				PostQueuedCompletionStatus(h_iocp, 1, ev.obj_id, &ov->_over);
				break;
			}
			case EV_SEND_HELLO: { // hello 전송
				OVER_EXP* ov = new OVER_EXP;
				ov->_comp_type = OP_AI_HELLO;
				ov->_ai_target_obj = ev.target_id;
				PostQueuedCompletionStatus(h_iocp, 1, ev.obj_id, &ov->_over);
				break;
			}
			}
			continue;		// 즉시 다음 작업 꺼내기
		}
	}
}

int main()
{
	WSADATA WSAData;
	WSAStartup(MAKEWORD(2, 2), &WSAData);
	g_s_socket = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
	SOCKADDR_IN server_addr;
	memset(&server_addr, 0, sizeof(server_addr));
	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(PORT_NUM);
	server_addr.sin_addr.S_un.S_addr = INADDR_ANY;
	bind(g_s_socket, reinterpret_cast<sockaddr*>(&server_addr), sizeof(server_addr));
	listen(g_s_socket, SOMAXCONN);
	SOCKADDR_IN cl_addr;
	int addr_size = sizeof(cl_addr);

	InitializeNPC();

	h_iocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, 0, 0, 0);
	CreateIoCompletionPort(reinterpret_cast<HANDLE>(g_s_socket), h_iocp, 9999, 0);
	g_c_socket = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
	g_a_over._comp_type = OP_ACCEPT;
	AcceptEx(g_s_socket, g_c_socket, g_a_over._send_buf, 0, addr_size + 16, addr_size + 16, 0, &g_a_over._over);

	vector <thread> worker_threads;
	int num_threads = std::thread::hardware_concurrency();
	for (int i = 0; i < num_threads; ++i)
		worker_threads.emplace_back(worker_thread, h_iocp);
	thread timer_thread{ do_timer };
	timer_thread.join();
	for (auto& th : worker_threads)
		th.join();
	closesocket(g_s_socket);
	WSACleanup();
}
