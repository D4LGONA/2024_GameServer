#include <iostream>
#include <SDKDDKVER.h> // sdk ������ ����س��� ��� ����, ����ȭ ��
// ������ ����� warning �޽����� ��.
#include <boost/asio.hpp>
// socket�� ����! boost/asio�� �˾Ƽ� ����. ������������ �Ȱ��� ������ ����

using namespace std;

int main()
{
    try {
        boost::asio::io_context io_context;
        boost::asio::ip::tcp::socket socket(io_context);
        boost::asio::ip::tcp::endpoint server_addr(boost::asio::ip::address::from_string("127.0.0.1"), 3500); // �����ּ�, ��Ʈ��ȣ
        boost::asio::connect(socket, &server_addr);
        
        for (;;) {
            std::string buf; // ���ڿ��� ���۷� ���� ��
            boost::system::error_code error; // ������ ������ ó�� �ؾ� ��

            std::cout << "Enter Message: ";
            std::getline(std::cin, buf); // ���� �о ���ۿ� �ֱ�
            if (0 == buf.size()) break;

            socket.write_some(boost::asio::buffer(buf), error); // ���Ͽ� ��� write
            // string�� �����ڷ� �־ ���� ����ü ����� �˾Ƽ� �������.
            // ���� ������ �������� buffers ���?? �̰� �˾Ƽ� ã�ƺ�����
            // �������. �� ���۵ɶ����� ��ٸ�.
            if (error == boost::asio::error::eof) break; // ���������� �������� eof ������ ����.
            else if (error) throw boost::system::system_error(error);

            char reply[1024]; // ������ string ��� �Ұ�. string�� ����� 0�̾ ���ۻ���� 0�̵Ǿ����
            size_t len = socket.read_some(boost::asio::buffer(reply), error); // �׳� read�ϸ� 1024����Ʈ ���������� ��� ��ٸ�
            if (error == boost::asio::error::eof) break;
            else if (error) throw boost::system::system_error(error);

            reply[len] = 0; // �� �ڿ� null ����
            std::cout << len << " bytes received: " << reply << endl;
        }
    }
    catch (std::exception& e) {
        std::cerr << e.what() << std::endl; // ���� Ȯ��.
    }
}




