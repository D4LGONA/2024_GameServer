#include <iostream>
#include <array>
#include <WS2tcpip.h>
#include <vector>
#include <unordered_set>
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

class OVER_EXP { // 패킷 내에 들어가 있는 정보들
public:
    WSABUF _wsabuf;
    char _send_buf[BUF_SIZE];
    COMP_TYPE _comp_type;

    OVER_EXP() {
        _wsabuf.len = BUF_SIZE;
        _wsabuf.buf = _send_buf;
        _comp_type = OP_RECV;
    }

    // 패킷을 복사하는 대신 참조 사용하여 메모리 할당 및 해제 오버헤드 줄임
    OVER_EXP(WSABUF& wsabuf) : _wsabuf(wsabuf) {
        _comp_type = OP_SEND;
    }
};

enum S_STATE { ST_FREE, ST_ALLOC, ST_INGAME };

class SESSION {
    OVER_EXP _recv_over;

public:
    S_STATE _state;
    SOCKET _socket;
    int _id;
    short x, y;
    char _name[NAME_SIZE];
    int _prev_remain;
    int _last_move_time;
    std::unordered_set<int> view_list;

    SESSION() {
        _id = -1;
        _socket = INVALID_SOCKET;
        x = y = 0;
        _name[0] = 0;
        _state = ST_FREE;
        _prev_remain = 0;
    }

    ~SESSION() {}

    bool do_recv() {
        DWORD recv_flag = 0;
        DWORD recv_size = 0;
        _recv_over._wsabuf.len = BUF_SIZE - _prev_remain;
        _recv_over._wsabuf.buf = _recv_over._send_buf + _prev_remain;

        // 필요한 경우에만 WSARecv 호출하도록 변경
        int result = WSARecv(_socket, &_recv_over._wsabuf, 1, &recv_size, &recv_flag, nullptr, nullptr);
        int error = WSAGetLastError();
        if (result == SOCKET_ERROR) {
            return false;
        }

        // 패킷 처리
        process_packet(_id, _recv_over._send_buf);

        return true;
    }

    void do_send(void* packet) {
        WSABUF wsabuf;
        wsabuf.len = reinterpret_cast<char*>(packet)[0];
        wsabuf.buf = reinterpret_cast<char*>(packet);
        OVER_EXP* sdata = new OVER_EXP(wsabuf);
        WSASend(_socket, &sdata->_wsabuf, 1, 0, 0, nullptr, nullptr);
        delete sdata; // 메모리 누수 방지
    }

    void send_login_info_packet() {
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
    void send_remove_player_packet(int c_id) {
        view_list.erase(c_id);

        SC_REMOVE_PLAYER_PACKET p;
        p.id = c_id;
        p.size = sizeof(p);
        p.type = SC_REMOVE_PLAYER;
        do_send(&p);
    }
};

std::array<SESSION, MAX_USER> clients;

SOCKET g_s_socket;
OVER_EXP g_a_over;

bool can_see(int a, int b) {
    int dist = (clients[a].x - clients[b].x) * (clients[a].x - clients[b].x) +
        (clients[a].y - clients[b].y) * (clients[a].y - clients[b].y);
    return dist <= VIEW_RANGE * VIEW_RANGE;
}

void SESSION::send_move_packet(int c_id) {
    SC_MOVE_PLAYER_PACKET p;
    p.id = c_id;
    p.size = sizeof(SC_MOVE_PLAYER_PACKET);
    p.type = SC_MOVE_PLAYER;
    p.x = clients[c_id].x;
    p.y = clients[c_id].y;
    p.move_time = clients[c_id]._last_move_time;
    do_send(&p);
}

void SESSION::send_add_player_packet(int c_id) {
    view_list.insert(c_id);

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
        if (clients[i]._state == ST_FREE)
            return i;
    }
    return -1;
}

void process_packet(int c_id, char* packet) {
    switch (packet[1]) {
    case CS_LOGIN: {
        if (clients[c_id]._state == ST_INGAME) break;
        CS_LOGIN_PACKET* p = reinterpret_cast<CS_LOGIN_PACKET*>(packet);
        strcpy_s(clients[c_id]._name, p->name);
        clients[c_id]._state = ST_INGAME;
        clients[c_id].x = uid(dre);
        clients[c_id].y = uid(dre);
        clients[c_id].send_login_info_packet();
        for (auto& pl : clients) {
            if (ST_INGAME != pl._state) continue;
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

        unordered_set<int> old_viewlist = clients[c_id].view_list;
        unordered_set<int> new_viewlist;
        for (auto& pl : clients) {
            if (pl._state != ST_INGAME) continue;
            if (pl._id == c_id) continue;
            if (false == can_see(c_id, pl._id)) continue;
            new_viewlist.insert(pl._id);
        }

        clients[c_id].send_move_packet(c_id);

        for (int p_id : new_viewlist) {
            if (0 == old_viewlist.count(p_id)) { // 새로 들어온 플레이어
                clients[c_id].send_add_player_packet(p_id);
                clients[p_id].send_add_player_packet(c_id);
            }
            else {
                clients[p_id].send_move_packet(c_id);
            }
        }

        for (int p_id : old_viewlist) {
            if (0 == new_viewlist.count(p_id)) { // 떠난 플레이어
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

void disconnect(int c_id) {
    for (auto& pl : clients) {
        if (ST_INGAME != pl._state) continue;
        if (pl._id == c_id) continue;
        pl.send_remove_player_packet(c_id);
    }
    closesocket(clients[c_id]._socket);
    clients[c_id]._state = ST_FREE;
}

int main() {
    WSADATA WSAData;
    WSAStartup(MAKEWORD(2, 2), &WSAData);
    g_s_socket = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, nullptr, 0, 0);
    SOCKADDR_IN server_addr;
    memset(&server_addr, 0, sizeof(server_addr)); // 메모리 초기화
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT_NUM);
    server_addr.sin_addr.S_un.S_addr = INADDR_ANY;
    bind(g_s_socket, reinterpret_cast<sockaddr*>(&server_addr), sizeof(server_addr));
    unsigned long noblock = 1;
    int nRet = ioctlsocket(g_s_socket, FIONBIO, &noblock);
    listen(g_s_socket, SOMAXCONN);
    int addr_size = sizeof(SOCKADDR_IN);

    // 서버 메인 루프
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
                for (auto& c : clients) {
                    if (c._id == client_id) continue;
                    if (c._state == ST_INGAME)
                        c.send_add_player_packet(client_id); // 수정된 부분
                }
            }
        }

        for (auto& c : clients) {
            if (c._state == ST_INGAME || c._state == ST_ALLOC) {
                c.do_recv();
            }
        }
    }

    closesocket(g_s_socket);
    WSACleanup();
    return 0;
}
