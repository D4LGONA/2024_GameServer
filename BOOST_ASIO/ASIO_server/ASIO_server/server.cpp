#include <iostream>
#include <sdkddkver.h>
#include <unordered_map> // 세션 여러개 관리.
#include <boost/asio.hpp>

using boost::asio::ip::tcp; // 하지마세요 이거
using namespace std;

constexpr int PORT = 3500;
constexpr int max_length = 1024;

class session;

unordered_map <int, session> g_clients; // unordered map > default 생성자가 없으면 컴파일이 안됨.
int g_client_id = 0;

class session
{
    int my_id_;
    tcp::socket socket_;
    char data_[max_length];

public:
    session() : socket_(nullptr) { // 이거 사용하면 안됨.
        cout << "Session Creation Error.\n"; exit(-1);
    }
    session(tcp::socket socket, int id) : socket_(std::move(socket)), my_id_(id) {
        do_read(); // 준 소켓과 아이디로 세션 생성.
    }
    void do_read() {
        socket_.async_read_some(boost::asio::buffer(data_),
            [this](boost::system::error_code ec, std::size_t length) { // handler에 lambda를 사용해서 어느 세션이 완료되었는지 알아내야
                data_[length] = 0;
                cout << "Client[" << my_id_ << "] " << data_ << endl;
                if (ec) g_clients.erase(my_id_); // 에러시에 세션을 날려버림.
                else do_write(length); }); // 아니면 반사.
    }
    void do_write(std::size_t length) {
        boost::asio::async_write(socket_, boost::asio::buffer(data_, length), // length만큼만 전송.
            [this](boost::system::error_code ec, std::size_t /*length*/) {
                if (!ec) do_read(); // 에러가 아니면 read.
                else g_clients.erase(my_id_); }); // 에러시에 세션 날려버림.
    }
};

void do_accept(tcp::acceptor& my_acceptor)
{
    my_acceptor.async_accept([&my_acceptor](boost::system::error_code ec, tcp::socket socket) {
        g_clients.try_emplace(g_client_id, move(socket), g_client_id);
        g_client_id++;

        do_accept(my_acceptor);
        }); // accepter가 socket을 만들어줌
}

int main(int argc, char* argv[])
{
    try {
        boost::asio::io_context io_context;
        tcp::acceptor my_acceptor{ io_context, tcp::endpoint(tcp::v4(), PORT) };
        do_accept(my_acceptor);
        io_context.run(); // 얘를 실행해야 핸들러가 실행됨.
    }
    catch (std::exception& e) {
        std::cerr << "Exception: " << e.what() << "\n";
    }
}
