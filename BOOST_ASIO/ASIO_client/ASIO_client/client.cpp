#include <iostream>
#include <SDKDDKVER.h> // sdk 버전을 기록해놓은 헤더 파일, 최적화 용
// 없으면 요상한 warning 메시지가 뜸.
#include <boost/asio.hpp>
// socket이 없다! boost/asio가 알아서 해줌. 리눅스에서도 똑같이 컴파일 가능

using namespace std;

int main()
{
    try {
        boost::asio::io_context io_context;
        boost::asio::ip::tcp::socket socket(io_context);
        boost::asio::ip::tcp::endpoint server_addr(boost::asio::ip::address::from_string("127.0.0.1"), 3500); // 서버주소, 포트번호
        boost::asio::connect(socket, &server_addr);
        
        for (;;) {
            std::string buf; // 문자열을 버퍼로 쓰면 됨
            boost::system::error_code error; // 에러가 나오면 처리 해야 함

            std::cout << "Enter Message: ";
            std::getline(std::cin, buf); // 한줄 읽어서 버퍼에 넣기
            if (0 == buf.size()) break;

            socket.write_some(boost::asio::buffer(buf), error); // 소켓에 대고 write
            // string을 생성자로 넣어서 버퍼 구조체 만들면 알아서 만들어짐.
            // 버퍼 여러개 있을때는 buffers 사용?? 이건 알아서 찾아보세요
            // 동기식임. 다 전송될때까지 기다림.
            if (error == boost::asio::error::eof) break; // 정상적으로 끊어져도 eof 에러가 나옴.
            else if (error) throw boost::system::system_error(error);

            char reply[1024]; // 읽을땐 string 사용 불가. string은 사이즈가 0이어서 버퍼사이즈가 0이되어버림
            size_t len = socket.read_some(boost::asio::buffer(reply), error); // 그냥 read하면 1024바이트 받을때까지 계속 기다림
            if (error == boost::asio::error::eof) break;
            else if (error) throw boost::system::system_error(error);

            reply[len] = 0; // 맨 뒤에 null 삽입
            std::cout << len << " bytes received: " << reply << endl;
        }
    }
    catch (std::exception& e) {
        std::cerr << e.what() << std::endl; // 에러 확인.
    }
}




