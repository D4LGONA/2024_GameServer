#include <iostream>
#include <array>
#include <WS2tcpip.h>
#include <mutex>
#include <thread>
#include <unordered_set>
#include <list>
#include <random>
#include "protocol.h"

#pragma comment(lib, "WS2_32.lib")
using namespace std;

constexpr int VIEW_RANGE = 9;

random_device rd;
default_random_engine dre(rd());
uniform_int_distribution<int> uid{ 0, 400 };

enum COMP_TYPE { OP_ACCEPT, OP_RECV, OP_SEND };
void process_packet(int c_id, char* packet);
void disconnect(int c_id);

class OVER_EXP {
public:
    WSABUF _wsabuf;
    char _send_buf[BUF_SIZE];
    COMP_TYPE _comp_type;

    OVER_EXP()
    {
        _wsabuf.len = BUF_SIZE;
        _wsabuf.buf = _send_buf;
        _comp_type = OP_RECV;
    }

    OVER_EXP(char* packet)
    {
        _wsabuf.len = packet[0];
        _wsabuf.buf = _send_buf;
        _comp_type = OP_SEND;
        memcpy(_send_buf, packet, packet[0]);
    }
};

enum S_STATE { ST_FREE, ST_ALLOC, ST_INGAME };

class SESSION {
    OVER_EXP _recv_over;
    mutex lc;
public:
    mutex _s_lock;
    S_STATE _state;
    int _id;
    SOCKET _socket;
    short x, y;
    char _name[NAME_SIZE];
    unordered_set<int> view_list;
    mutex vl_l;

    int _prev_remain;
    int _last_move_time;
    WSAEVENT _event; // 소켓 이벤트

public:
    SESSION()
    {
        _id = -1;
        _socket = 0;
        x = y = 0;
        _name[0] = 0;
        _state = ST_FREE;
        _prev_remain = 0;
        _event = WSACreateEvent(); // 이벤트 생성
    }

    ~SESSION()
    {
        WSACloseEvent(_event); // 이벤트 닫기
    }

    void do_recv()
    {
        DWORD recv_flag = 0;
        DWORD recv_size;
        _recv_over._wsabuf.len = BUF_SIZE - _prev_remain;
        _recv_over._wsabuf.buf = _recv_over._send_buf + _prev_remain;

        // 이벤트 대기
        WSAEventSelect(_socket, _event, FD_READ); // 소켓 이벤트 설정

        // 이벤트 대기 및 처리
        DWORD event_res = WSAWaitForMultipleEvents(1, &_event, FALSE, WSA_INFINITE, FALSE);
        if (event_res == WSA_WAIT_FAILED) {
            cerr << "WSAWaitForMultipleEvents failed: " << WSAGetLastError() << endl;
            return;
        }
        if (event_res == WSA_WAIT_TIMEOUT) {
            cerr << "WSAWaitForMultipleEvents timed out." << endl;
            return;
        }
        WSANETWORKEVENTS network_events;
        if (WSAEnumNetworkEvents(_socket, _event, &network_events) == SOCKET_ERROR) {
            cerr << "WSAEnumNetworkEvents failed: " << WSAGetLastError() << endl;
            return;
        }
        if (network_events.lNetworkEvents & FD_READ) {
            int res = WSARecv(_socket, &_recv_over._wsabuf, 1, &recv_size, &recv_flag, nullptr, 0);
            if (res == SOCKET_ERROR) {
                return;
            }

            // 패킷 재조립
            int remain_data = recv_size + _prev_remain;
            char* p = _recv_over._send_buf;
            while (remain_data > 0) {
                int packet_size = p[0];
                if (packet_size <= remain_data) {
                    process_packet(static_cast<int>(_id), p);
                    p = p + packet_size;
                    remain_data = remain_data - packet_size;
                }
                else break;
            }
            _prev_remain = remain_data;
            if (remain_data > 0) {
                memcpy(_recv_over._send_buf, p, remain_data);
            }
        }
    }

    void do_send(void* packet)
    {
        DWORD send_flag = 0;
        DWORD send_size;
        OVER_EXP* sdata = new OVER_EXP{ reinterpret_cast<char*>(packet) };
        int res = WSASend(_socket, &sdata->_wsabuf, 1, &send_size, send_flag, nullptr, 0);
        delete sdata; // 메모리 누수 방지
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
    void send_remove_player_packet(int c_id)
    {
        vl_l.lock();
        view_list.erase(c_id);
        vl_l.unlock();
        SC_REMOVE_PLAYER_PACKET p;
        p.id = c_id;
        p.size = sizeof(p);
        p.type = SC_REMOVE_PLAYER;
        do_send(&p);
    }
};

array<SESSION, MAX_USER> clients;
SOCKET g_s_socket;

bool can_see(int a, int b)
{
    int dist = (clients[a].x - clients[b].x) * (clients[a].x - clients[b].x) +
        (clients[a].y - clients[b].y) * (clients[a].y - clients[b].y);
    return dist <= VIEW_RANGE * VIEW_RANGE;
}

void SESSION::send_move_packet(int c_id)
{
    SC_MOVE_PLAYER_PACKET p;
    p.id = c_id;
    p.size = sizeof(SC_MOVE_PLAYER_PACKET);
    p.type = SC_MOVE_PLAYER;
    p.x = clients[c_id].x;
    p.y = clients[c_id].y;
    p.move_time = clients[c_id]._last_move_time;
    do_send(&p);
}

void SESSION::send_add_player_packet(int c_id)
{
    vl_l.lock();
    view_list.insert(c_id);
    vl_l.unlock();

    SC_ADD_PLAYER_PACKET add_packet;
    add_packet.id = c_id;
    strcpy_s(add_packet.name, clients[c_id]._name);
    add_packet.size = sizeof(add_packet);
    add_packet.type = SC_ADD_PLAYER;
    add_packet.x = clients[c_id].x;
    add_packet.y = clients[c_id].y;
    do_send(&add_packet);
}

int get_new_client_id() {
    for (int i = 0; i < MAX_USER; ++i) {
        lock_guard <mutex> ll(clients[i]._s_lock);
        if (clients[i]._state == ST_FREE)
            return i;
    }
    return -1;
}

void process_packet(int c_id, char* packet)
{
    switch (packet[1]) {
    case CS_LOGIN: {
        CS_LOGIN_PACKET* p = reinterpret_cast<CS_LOGIN_PACKET*>(packet);
        for (auto& c : clients) {
            if (c._id == c_id) continue;
            if (c._state == ST_INGAME)
                c.send_add_player_packet(c_id); // 수정된 부분
        }
        strcpy_s(clients[c_id]._name, p->name);
        clients[c_id].x = uid(dre);
        clients[c_id].y = uid(dre);
        clients[c_id].send_login_info_packet();

        {
            lock_guard<mutex> ll{ clients[c_id]._s_lock };
            clients[c_id]._state = ST_INGAME;
        }
        for (auto& pl : clients) {
            {
                lock_guard<mutex> ll(pl._s_lock);
                if (ST_INGAME != pl._state) continue;
            }
            if (pl._id == c_id) continue;
            pl.send_add_player_packet(c_id);
            clients[c_id].send_add_player_packet(pl._id);
        }
        break;
    }
    case CS_MOVE: {
        CS_MOVE_PACKET* p = reinterpret_cast<CS_MOVE_PACKET*>(packet);
        clients[c_id]._last_move_time = p->move_time;
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

        // 시야처리
        clients[c_id].vl_l.lock();
        unordered_set<int> old_viewlist = clients[c_id].view_list;
        clients[c_id].vl_l.unlock();
        unordered_set<int> new_viewlist;
        for (auto& pl : clients) {
            if (pl._state != ST_INGAME) continue;
            if (pl._id == c_id) continue;
            if (false == can_see(c_id, pl._id)) continue;
            new_viewlist.insert(pl._id);
        }

        clients[c_id].send_move_packet(c_id);

        for (int p_id : new_viewlist) {
            if (0 == old_viewlist.count(p_id)) { // new엔 있고 old엔 없다 - 새로 들어왔다
                clients[c_id].send_add_player_packet(p_id);
                clients[p_id].send_add_player_packet(c_id);
            }
            else {
                clients[p_id].send_move_packet(c_id);
            }
        }

        for (int p_id : old_viewlist) {
            if (0 == new_viewlist.count(p_id)) {
                clients[c_id].send_remove_player_packet(p_id);
                clients[p_id].send_remove_player_packet(c_id);
            }
        }
        break;
    }
    case CS_REMOVE:
    {
        CS_REMOVE_PACKET* p = reinterpret_cast<CS_REMOVE_PACKET*>(packet);
        disconnect(c_id);
        break;
    }
    }
}

void disconnect(int c_id)
{
    for (auto& pl : clients) {
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
}

void worker_thread(int c_id)
{
    while (clients[c_id]._state != ST_FREE) {
        clients[c_id].do_recv();
    }
}

int main()
{
    int timeout_ms = 50; // 5초

    WSADATA WSAData;
    WSAStartup(MAKEWORD(2, 2), &WSAData);
    g_s_socket = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, nullptr, 0, WSA_FLAG_OVERLAPPED);
    SOCKADDR_IN server_addr;
    memset(&server_addr, 0, sizeof(server_addr)); // 메모리 초기화
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT_NUM);
    server_addr.sin_addr.S_un.S_addr = INADDR_ANY;
    bind(g_s_socket, reinterpret_cast<sockaddr*>(&server_addr), sizeof(server_addr));
    listen(g_s_socket, SOMAXCONN);
    int addr_size = sizeof(server_addr);
    list<thread> worker_threads;
    while (true) {
        SOCKET client = WSAAccept(g_s_socket, reinterpret_cast<sockaddr*>(&server_addr), &addr_size, nullptr, 0);
        if (client != INVALID_SOCKET) { // 연결 성공
            int client_id = get_new_client_id();
            if (client_id != -1) {
                clients[client_id]._state = ST_ALLOC;
                clients[client_id].x = 0;
                clients[client_id].y = 0;
                clients[client_id]._id = client_id;
                clients[client_id]._name[0] = 0;
                clients[client_id]._prev_remain = 0; // 요건 뭐지?
                clients[client_id]._socket = client;

                worker_threads.emplace_back(worker_thread, client_id);
            }
        }

    }

    closesocket(g_s_socket);
    WSACleanup();
}
