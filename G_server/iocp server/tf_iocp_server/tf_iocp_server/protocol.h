constexpr int PORT_NUM = 4000;
constexpr int BUF_SIZE = 200;
constexpr int NAME_SIZE = 20; // login ID

constexpr int W_WIDTH = 8; // world size
constexpr int W_HEIGHT = 8;

// Packet ID
constexpr char CS_LOGIN = 0;
constexpr char CS_MOVE = 1;

constexpr char SC_LOGIN_INFO = 2;
constexpr char SC_ADD_PLAYER = 3;
constexpr char SC_REMOVE_PLAYER = 4;
constexpr char SC_MOVE_PLAYER = 5;

#pragma pack (push, 1)
struct CS_LOGIN_PACKET {
	unsigned char size;
	char	type;
	char	name[NAME_SIZE];
};

struct CS_MOVE_PACKET {
	unsigned char size;
	char	type;
	char	direction;  // 0 : UP, 1 : DOWN, 2 : LEFT, 3 : RIGHT
};

struct SC_LOGIN_INFO_PACKET { // 누가 로그인 했다.
	unsigned char size;
	char	type;
	short	id;
	short	x, y;
};

struct SC_ADD_PLAYER_PACKET { // 누가 로그인 했다.
	unsigned char size;
	char	type;
	short	id;
	short	x, y;
	char	name[NAME_SIZE]; // namesize는 최소로 만들어야 함
};

struct SC_REMOVE_PLAYER_PACKET { // 누가 로그아웃 했다.
	unsigned char size;
	char	type;
	short	id;
};

struct SC_MOVE_PLAYER_PACKET { // 누가 로그아웃 했다.
	unsigned char size;
	char	type;
	short	id;
	short	x, y;
};

#pragma pack (pop)