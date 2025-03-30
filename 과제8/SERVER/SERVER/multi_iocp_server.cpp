#include <iostream>
#include <array>
#include <WS2tcpip.h>
#include <MSWSock.h>
#include <thread>
#include <vector>
#include <mutex>
#include <unordered_set>
#include <queue>
#include <random>
#include <concurrent_priority_queue.h>
#include <sqlext.h> 
#include <string>
#include <codecvt>
#include <sstream>
#include "protocol.h"

#pragma comment(lib, "WS2_32.lib")
#pragma comment(lib, "MSWSock.lib")
using namespace std;

random_device rd;
mt19937 dre(rd());
uniform_int_distribution<int>uid{ 0, W_WIDTH - 1 }; // 정사각형이니..

constexpr int SECTOR_SIZE = 40; // 40 * 40
constexpr int VIEW_RANGE = 7;
constexpr int NPC_START = 0;
constexpr int USER_START = MAX_NPC;

thread_local SQLHDBC hdbc = SQL_NULL_HDBC;
thread_local SQLHSTMT hstmt = SQL_NULL_HSTMT;

void disp_error(SQLHANDLE hHandle, SQLSMALLINT hType, RETCODE RetCode)
{
	SQLSMALLINT iRec = 0;
	SQLINTEGER iError;
	WCHAR wszMessage[1000];
	WCHAR wszState[SQL_SQLSTATE_SIZE + 1];
	if (RetCode == SQL_INVALID_HANDLE) { // 핸들이 없는 경우
		fwprintf(stderr, L"Invalid handle!\n");
		return;
	}
	// 준 타입의 준 핸들에 대한 에러를 문자열로 출력
	// 에러가 여러개일 수 있어서 while 루프를 돌면서 출력하는 것임
	while (SQLGetDiagRec(hType, hHandle, ++iRec, wszState, &iError, wszMessage,
		(SQLSMALLINT)(sizeof(wszMessage) / sizeof(WCHAR)), (SQLSMALLINT*)NULL) == SQL_SUCCESS) {
		// Hide data truncated..
		if (wcsncmp(wszState, L"01004", 5)) {
			fwprintf(stderr, L"[%5.5s] %s (%d)\n", wszState, wszMessage, iError);
		}
	}
}

struct BLOCK {
	short dx;
	short dy;
};

const BLOCK Nears[] = {
	{-1, -1}, {-1, 0}, {-1, 1},
	{0, -1}, {0, 0}, {0, 1},
	{1, -1}, {1, 0}, {1, 1}
};

enum COMP_TYPE { OP_ACCEPT, OP_RECV, OP_SEND, OP_RANDOM_MOVE };
class OVER_EXP {
public:
	WSAOVERLAPPED _over;
	WSABUF _wsabuf;
	char _send_buf[BUF_SIZE];
	COMP_TYPE _comp_type;
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

bool is_npc(int a)
{
	return a < MAX_NPC;
}

wstring strtowstr(string s)
{
	std::wstring_convert<std::codecvt_utf8<wchar_t>, wchar_t> converter;
	std::wstring wstr = converter.from_bytes(s);
	return wstr;
}

enum S_STATE { ST_FREE, ST_ALLOC, ST_INGAME };
class SESSION {
	OVER_EXP _recv_over;

public:
	mutex _s_lock;
	S_STATE _state;
	atomic_bool _active; // 기본 false. 플레이어가 깨우는 것
	int _id;
	SOCKET _socket;
	short	x, y;
	char	_name[NAME_SIZE];
	chrono::system_clock::time_point _rm_time;
	unordered_set<int> view_list;
	mutex _vl_l;
	short sector_x, sector_y;

	int		_prev_remain;
	int		_last_move_time;
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
		if (true == is_npc(_id)) {
			cout << "Send to npc error" << endl; //
			return;
		}
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
	void send_login_fail_packet();
	void send_move_packet(int c_id);
	void send_add_object_packet(int c_id);
	void send_remove_object_packet(int c_id)
	{
		if (true == is_npc(_id)) return; // 락킹, 패킷만들기 전에 리턴할것.
		_vl_l.lock();
		view_list.erase(c_id);
		_vl_l.unlock();

		SC_REMOVE_PLAYER_PACKET p;
		p.id = c_id;
		p.size = sizeof(p);
		p.type = SC_REMOVE_PLAYER;
		do_send(&p);
	}
	void do_random_move();
	void heart_beat()
	{
		do_random_move();
	}
};

enum EVENT_TYPE { EV_RANDOM_MOVE, EV_HEAL, EV_ATTACK };
struct EVENT {
	int obj_id;
	chrono::system_clock::time_point wakeup_time;
	EVENT_TYPE e_type;
	int target_id;
public:
	bool operator<(const EVENT& other) const {
		return wakeup_time > other.wakeup_time;
	}
};

bool setupDBConnection() {
	SQLHENV henv;
	SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &henv);
	SQLSetEnvAttr(henv, SQL_ATTR_ODBC_VERSION, (SQLPOINTER*)SQL_OV_ODBC3, 0);

	SQLAllocHandle(SQL_HANDLE_DBC, henv, &hdbc);
	SQLRETURN ret = SQLConnect(hdbc, (SQLWCHAR*)L"hw8_2022184015", SQL_NTS, (SQLWCHAR*)NULL, 0, NULL, 0);
	if (ret == SQL_SUCCESS || ret == SQL_SUCCESS_WITH_INFO) {
		SQLAllocHandle(SQL_HANDLE_STMT, hdbc, &hstmt);
		SQLFreeHandle(SQL_HANDLE_ENV, henv); // 환경 핸들은 더 이상 필요 없으므로 해제
		return true;
	}
	else {
		SQLFreeHandle(SQL_HANDLE_DBC, hdbc);
		SQLFreeHandle(SQL_HANDLE_ENV, henv);
		hdbc = SQL_NULL_HDBC;
		return false;
	}
}

void cleanupDBConnection() {
	if (hstmt != SQL_NULL_HSTMT) {
		SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
		hstmt = SQL_NULL_HSTMT;
	}
	if (hdbc != SQL_NULL_HDBC) {
		SQLDisconnect(hdbc);
		SQLFreeHandle(SQL_HANDLE_DBC, hdbc);
		hdbc = SQL_NULL_HDBC;
	}
}

concurrency::concurrent_priority_queue<EVENT> g_event_queue;

array<array<unordered_set<int>, W_HEIGHT / SECTOR_SIZE>, W_WIDTH / SECTOR_SIZE> g_ObjectListSector;
mutex g_SectorLock;

array<SESSION, MAX_NPC + MAX_USER> objects; // 같은 컨테이너에서 위치를 다르게 해서
//인덱스만으로 모든 npc 검사할수 있게 만들 것

bool can_see(int a, int b)
{
	int dist = (objects[a].x - objects[b].x) * (objects[a].x - objects[b].x) +
		(objects[a].y - objects[b].y) * (objects[a].y - objects[b].y);
	return dist <= VIEW_RANGE * VIEW_RANGE;

	/*if (abs(objects[a].x - objects[b].x) > VIEW_RANGE) return false;
	return (abs(objects[a].y - objects[b].y) > VIEW_RANGE);*/
}

void add_timer(int key, EVENT_TYPE ev, int time) // 이딴게.. todo?
{
	EVENT evt;
	evt.e_type = ev;
	evt.obj_id = key;
	evt.wakeup_time = chrono::system_clock::now() + chrono::milliseconds(time);
	switch (ev)
	{
	case EV_RANDOM_MOVE:
		evt.target_id = key;
		break;
	default:
		break;
	}

	g_event_queue.push(evt);
}

SOCKET g_s_socket, g_c_socket;
OVER_EXP g_a_over;

void SESSION::send_login_fail_packet()
{
	SC_LOGIN_INFO_PACKET p;
	p.id = _id;
	p.size = sizeof(SC_LOGIN_INFO_PACKET);
	p.type = SC_LOGIN_FAIL;
	p.x = -1;
	p.y = -1;
	do_send(&p);
}

void SESSION::send_move_packet(int c_id)
{
	if (true == is_npc(_id)) return;

	SC_MOVE_PLAYER_PACKET p;
	p.id = c_id;
	p.size = sizeof(SC_MOVE_PLAYER_PACKET);
	p.type = SC_MOVE_PLAYER;
	p.x = objects[c_id].x;
	p.y = objects[c_id].y;
	p.move_time = objects[c_id]._last_move_time;
	do_send(&p);
}

void SESSION::send_add_object_packet(int c_id)
{
	if (true == is_npc(_id)) return;

	_vl_l.lock();
	view_list.insert(c_id);
	_vl_l.unlock();

	SC_ADD_PLAYER_PACKET add_packet;
	add_packet.id = c_id;
	strcpy_s(add_packet.name, objects[c_id]._name);
	add_packet.size = sizeof(add_packet);
	add_packet.type = SC_ADD_PLAYER;
	add_packet.x = objects[c_id].x;
	add_packet.y = objects[c_id].y;
	do_send(&add_packet);
}

void SESSION::do_random_move()
{
	// 플레이어중에 보이는애
	unordered_set<int> old_vl;
	for (const auto& n : Nears) {
		short sx = sector_x + n.dx;
		short sy = sector_y + n.dy;
		// new viewlist 만들기!

		if (sx >= 0 && sx < W_WIDTH / SECTOR_SIZE && sy >= 0 && sy < W_HEIGHT / SECTOR_SIZE) {
			lock_guard<mutex> ll{ g_SectorLock };
			for (auto& i : g_ObjectListSector[sx][sy]) {
				if (objects[i]._state != ST_INGAME) continue;
				if (false == can_see(_id, i)) continue;
				if (false == is_npc(i)) old_vl.insert(i);
			}
		}
	}

	switch (rand() % 4) {
	case 0: if (x > 0) x--; break;
	case 1: if (x < W_WIDTH - 1) x++; break;
	case 2: if (y > 0) y--; break;
	case 3: if (x < W_HEIGHT - 1) y++; break;
	}

	if (x / SECTOR_SIZE != sector_x or y / SECTOR_SIZE != sector_y)
	{
		lock_guard<mutex> ll{ g_SectorLock }; // sector 이동
		g_ObjectListSector[sector_x][sector_y].erase(_id);
		sector_x = x / SECTOR_SIZE;
		sector_y = y / SECTOR_SIZE;
		g_ObjectListSector[sector_x][sector_y].insert(_id);
	}

	unordered_set<int> new_vl;

	for (const auto& n : Nears) {
		short sx = sector_x + n.dx;
		short sy = sector_y + n.dy;
		// new viewlist 만들기!

		if (sx >= 0 && sx < W_WIDTH / SECTOR_SIZE && sy >= 0 && sy < W_HEIGHT / SECTOR_SIZE) {
			lock_guard<mutex> ll{ g_SectorLock };
			for (auto& i : g_ObjectListSector[sx][sy]) {
				if (objects[i]._state != ST_INGAME) continue;
				if (false == can_see(_id, i)) continue;
				if (false == is_npc(i)) new_vl.insert(i);
			}
		}
	}

	/*for (int i = MAX_NPC; i < MAX_NPC + MAX_USER; ++i) {
		if (objects[i]._state != ST_INGAME) continue;
		if (true == can_see(i, _id)) new_vl.insert(i);
	}*/

	// 들어온 애
	for (auto& pl : new_vl) {
		if (0 == old_vl.count(pl))
			objects[pl].send_add_object_packet(_id);
		else
			objects[pl].send_move_packet(_id);
	}

	// 멀어진 애
	for (auto& pl : old_vl) {
		if (0 == new_vl.count(pl))
			objects[pl].send_remove_object_packet(_id);
	}
}

int get_new_client_id()
{
	for (int i = MAX_NPC; i < MAX_USER + MAX_NPC; ++i) { // 0번부터가 아니도록
		lock_guard <mutex> ll{ objects[i]._s_lock };
		if (objects[i]._state == ST_FREE)
			return i;
	}
	return -1;
}

void process_packet(int c_id, char* packet)
{
	switch (packet[1]) {
	case CS_LOGIN: {
		CS_LOGIN_PACKET* p = reinterpret_cast<CS_LOGIN_PACKET*>(packet);

		string str(p->name);
		wstring ws = strtowstr(str);
		std::wstring query = L"SELECT * FROM user_table WHERE user_id = '" + ws + L"';";
		SQLCloseCursor(hstmt);
		// 여기서 디비작업해야함
		SQLRETURN ret = SQLExecDirect(hstmt, (SQLWCHAR*)query.c_str(), SQL_NTS);
		if (ret == SQL_SUCCESS || ret == SQL_SUCCESS_WITH_INFO) {
			SQLLEN cbID = 0, cbx = 0, cby = 0;
			ret = SQLBindCol(hstmt, 1, SQL_C_CHAR, objects[c_id]._name, 20, &cbID);
			ret = SQLBindCol(hstmt, 2, SQL_C_LONG, &objects[c_id].x, 10, &cbx);
			ret = SQLBindCol(hstmt, 3, SQL_C_LONG, &objects[c_id].y, 10, &cby);
			for (int i = 0; ; i++) { // 데이터가 몇개 날라올지 몰라서 무한반복하는듯
				ret = SQLFetch(hstmt);
				if (ret != SQL_SUCCESS && ret != SQL_SUCCESS_WITH_INFO) break;
			}
			strcpy_s(objects[c_id]._name, p->name);
		}
		else
		{
			disp_error(hstmt, SQL_HANDLE_STMT, ret);
			objects[c_id].send_login_fail_packet();
			break;
		}

		objects[c_id].send_login_info_packet();
		{
			lock_guard<mutex> ll{ objects[c_id]._s_lock };
			objects[c_id]._state = ST_INGAME;
		}
		{
			lock_guard<mutex> ll{ g_SectorLock }; // sector에 추가하는 부분
			objects[c_id].sector_x = objects[c_id].x / SECTOR_SIZE;
			objects[c_id].sector_y = objects[c_id].y / SECTOR_SIZE;
			g_ObjectListSector[objects[c_id].sector_x][objects[c_id].sector_y].insert(c_id);
		}
		// 들어왔을때 처리
		for (const auto& n : Nears) {
			short sx = objects[c_id].sector_x + n.dx;
			short sy = objects[c_id].sector_y + n.dy;

			if (sx >= 0 && sx < W_WIDTH / SECTOR_SIZE && sy >= 0 && sy < W_HEIGHT / SECTOR_SIZE) {
				lock_guard<mutex> ll{ g_SectorLock };
				for (auto& i : g_ObjectListSector[sx][sy]) {
					if (false == can_see(c_id, i)) continue;
					if (true == is_npc(i) && objects[i]._active == false) {
						bool expt = false;
						if (true == atomic_compare_exchange_strong(&objects[i]._active, &expt, true))
							add_timer(objects[i]._id, EV_RANDOM_MOVE, 1000);
					}
				}
			}
		}

		for (auto& pl : objects) {
			{
				lock_guard<mutex> ll(pl._s_lock);
				if (ST_INGAME != pl._state) continue;
			}
			if (pl._id == c_id) continue;
			if (false == can_see(pl._id, c_id)) continue;
			pl.send_add_object_packet(c_id);
			objects[c_id].send_add_object_packet(pl._id);
		}
		break;
	}
	case CS_MOVE: {
		CS_MOVE_PACKET* p = reinterpret_cast<CS_MOVE_PACKET*>(packet);
		objects[c_id]._last_move_time = p->move_time;
		int x = objects[c_id].x;
		int y = objects[c_id].y;
		switch (p->direction) {
		case 0: if (y > 0) y--; break;
		case 1: if (y < W_HEIGHT - 1) y++; break;
		case 2: if (x > 0) x--; break;
		case 3: if (x < W_WIDTH - 1) x++; break;
		}
		objects[c_id].x = x;
		objects[c_id].y = y;

		if (!is_npc(c_id)) {
			string s = objects[c_id]._name;
			wstring name = strtowstr(s);
			wstring query = L"UPDATE user_table SET user_x = " + to_wstring(x) + L", user_y = " + to_wstring(y) + L" WHERE user_id = '" + name + L"';";
			// SQL 문장 실행
			SQLCloseCursor(hstmt);
			SQLRETURN ret = SQLExecDirect(hstmt, (SQLWCHAR*)query.c_str(), SQL_NTS);
			if (ret == SQL_SUCCESS || ret == SQL_SUCCESS_WITH_INFO)
			{
			}
			else
				break;
		}
		
		
		if (objects[c_id].x / SECTOR_SIZE != objects[c_id].sector_x or
			objects[c_id].y / SECTOR_SIZE != objects[c_id].sector_y)
		{
			lock_guard<mutex> ll{ g_SectorLock }; // sector 이동
			g_ObjectListSector[objects[c_id].sector_x][objects[c_id].sector_y].erase(c_id);
			objects[c_id].sector_x = objects[c_id].x / SECTOR_SIZE;
			objects[c_id].sector_y = objects[c_id].y / SECTOR_SIZE;
			g_ObjectListSector[objects[c_id].sector_x][objects[c_id].sector_y].insert(c_id);
		}

		objects[c_id]._vl_l.lock();
		unordered_set<int> old_viewlist = objects[c_id].view_list;
		objects[c_id]._vl_l.unlock();
		unordered_set<int> new_viewlist;

		// 주변 섹터 탐색(8방향 - 나중에 시야범위 늘렸을때 문제가 될 수 있으니!)
		for (const auto& n : Nears) {
			short sx = objects[c_id].sector_x + n.dx;
			short sy = objects[c_id].sector_y + n.dy;

			// new viewlist 만들기!

			if (sx >= 0 && sx < W_WIDTH / SECTOR_SIZE && sy >= 0 && sy < W_HEIGHT / SECTOR_SIZE) {
				lock_guard<mutex> ll{ g_SectorLock };
				for (auto& i : g_ObjectListSector[sx][sy]) {
					if (objects[i]._state != ST_INGAME) continue;
					if (false == can_see(c_id, i)) continue;
					if (i == c_id) continue;
					new_viewlist.insert(i);
					if (true == is_npc(i) && objects[i]._active == false) {
						bool expt = false;
						if (true == atomic_compare_exchange_strong(&objects[i]._active, &expt, true))
							add_timer(objects[i]._id, EV_RANDOM_MOVE, 1000);
					}
				}
			}
		}

		objects[c_id].send_move_packet(c_id);

		for (int p_id : new_viewlist) {
			if (0 == old_viewlist.count(p_id)) { // new엔 있고 old엔 없다 - 새로 들어왔다
				objects[c_id].send_add_object_packet(p_id);
				objects[p_id].send_add_object_packet(c_id);
			}
			else {
				objects[p_id].send_move_packet(c_id);
			}
		}

		for (int p_id : old_viewlist) {
			if (0 == new_viewlist.count(p_id)) {
				objects[c_id].send_remove_object_packet(p_id);
				objects[p_id].send_remove_object_packet(c_id);
			}
		}

	}
	}
}

void disconnect(int c_id)
{
	for (auto& pl : objects) {
		{
			lock_guard<mutex> ll(pl._s_lock);
			if (ST_INGAME != pl._state) continue;
		}
		if (pl._id == c_id) continue;
		if(true == can_see(pl._id, c_id))
			pl.send_remove_object_packet(c_id);
	}
	closesocket(objects[c_id]._socket);

	lock_guard<mutex> ll(objects[c_id]._s_lock);
	objects[c_id]._state = ST_FREE;
	{
		lock_guard<mutex> ll(g_SectorLock);
		g_ObjectListSector[objects[c_id].sector_x][objects[c_id].sector_y].erase(c_id);
	}
}

bool player_exist(int npc_id)
{
	for (int i = USER_START; i < USER_START + MAX_USER; ++i) { // 모든 플레이어에 대해 검색
		if (ST_INGAME != objects[i]._state) continue;
		if (true == can_see(npc_id, i)) return true;
	}
	return false;
}

void worker_thread(HANDLE h_iocp)
{
	// db핸들생성
	while (true) {
		if (setupDBConnection()) break;
	}


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
					lock_guard<mutex> ll(objects[client_id]._s_lock);
					objects[client_id]._state = ST_ALLOC;
				}
				objects[client_id].x = 0;
				objects[client_id].y = 0;
				objects[client_id]._id = client_id;
				objects[client_id]._name[0] = 0;
				objects[client_id]._prev_remain = 0;
				objects[client_id]._socket = g_c_socket;
				CreateIoCompletionPort(reinterpret_cast<HANDLE>(g_c_socket),
					h_iocp, client_id, 0);
				objects[client_id].do_recv();
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
			int remain_data = num_bytes + objects[key]._prev_remain;
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
			objects[key]._prev_remain = remain_data;
			if (remain_data > 0) {
				memcpy(ex_over->_send_buf, p, remain_data);
			}
			objects[key].do_recv();
			break;
		}
		case OP_SEND:
			delete ex_over;
			break;
		case OP_RANDOM_MOVE: // 시야처리
			if (true == player_exist(key)) {// 몬스터 주위에 플레이어가 있을때만 이동.
				objects[key].do_random_move();
				add_timer(key, EV_RANDOM_MOVE, 1000); // 1초 후에 다시 이동하도록.
			}
			else {
				objects[key]._active = false;
			}
			delete ex_over;
			break;
		}
	}
	// 여기서 클린해야하나?
	cleanupDBConnection();
}

void initialize_npc()
{
	for (int i = 0; i < MAX_NPC; ++i) {
		objects[i].x = uid(dre);
		objects[i].y = uid(dre);
		{
			lock_guard<mutex> ll{ g_SectorLock }; // sector 이동
			objects[i].sector_x = objects[i].x / SECTOR_SIZE;
			objects[i].sector_y = objects[i].y / SECTOR_SIZE;
			g_ObjectListSector[objects[i].sector_x][objects[i].sector_y].insert(i);
		}
		objects[i]._id = i;
		sprintf_s(objects[i]._name, "N%d", i);
		objects[i]._state = ST_INGAME;
		objects[i]._rm_time = chrono::system_clock::now();
		objects[i]._active = false;
	}
}

void do_timer(HANDLE h_iocp)
{
	using namespace chrono;
	while (true) {
		EVENT ev;
		auto current_time = system_clock::now();
		if (true == g_event_queue.try_pop(ev)) {
			if (ev.wakeup_time < system_clock::now()) {
				OVER_EXP* ov = new OVER_EXP;
				ov->_comp_type = OP_RANDOM_MOVE;
				PostQueuedCompletionStatus(h_iocp, 1, ev.obj_id, &ov->_over);
			}
			else
				g_event_queue.push(ev);
		}
	}
}

int main()
{
	HANDLE h_iocp;
	setlocale(LC_ALL, "korean");

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
	h_iocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, 0, 0, 0);
	CreateIoCompletionPort(reinterpret_cast<HANDLE>(g_s_socket), h_iocp, 9999, 0);
	g_c_socket = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
	g_a_over._comp_type = OP_ACCEPT;
	AcceptEx(g_s_socket, g_c_socket, g_a_over._send_buf, 0, addr_size + 16, addr_size + 16, 0, &g_a_over._over);

	initialize_npc();
	thread ai_thread{ do_timer, h_iocp }; // ai 스레드 과부하.
	vector <thread> worker_threads;
	int num_threads = std::thread::hardware_concurrency();
	for (int i = 0; i < num_threads; ++i)
		worker_threads.emplace_back(worker_thread, h_iocp);
	for (auto& th : worker_threads)
		th.join();
	ai_thread.join();
	closesocket(g_s_socket);
	WSACleanup();
}
