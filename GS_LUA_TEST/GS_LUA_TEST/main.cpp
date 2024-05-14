#include <iostream>
#include "include/lua.hpp"

#pragma comment (lib, "lua54.lib")

int LUAapi_add(lua_State* L)
{
	int a = lua_tonumber(L, -1);
	int b = lua_tonumber(L, -2);
	lua_pop(L, 3); // 3개 꺼내는 것
	int c = a + b;
	lua_pushnumber(L, c);
	return 1; // 푸시한것의 개수
}

int main()
{
	//const char* program = "print \"Hello from Lua\""; // 코딩을 파일로 해도 되고 이렇게 해도 됨

	lua_State* L = luaL_newstate(); // 가상머신 생성하는 함수
	luaL_openlibs(L); // 기본 라이브러리를 로딩
	//luaL_loadbuffer(L, program, strlen(program), "line"); // 코드 가져오는 것
	luaL_loadfile(L, "dragon.lua");
	// sql할때도 문자열 길이까지 넣어줘야 함
	int ret = lua_pcall(L, 0, 0, 0); // 가상머신, 인자로 넘겨줄것, 리턴값 개수(여러개 가능), 에러처리할 함수
	// 실행!
	if (0 != ret) {
		std::cout << "error: " << lua_tostring(L, -1); // 에러를 L 머신에서 1개 뽑아라
		lua_pop(L, 1);
	}

	//lua_getglobal(L, "pos_x");
	//lua_getglobal(L, "pos_y"); // 변수 가져오는 것
	//int pos_x = lua_tonumber(L, -2);
	//int pos_y = lua_tonumber(L, -1);
	//lua_pop(L, 2);
	//std::cout << "pos_x: " << pos_x << ", pos_y: " << pos_y << std::endl;
	//// 게임서버에서 알아서 활용하도록 ...

	lua_register(L, "callC_add", LUAapi_add);

	lua_getglobal(L, "event_add"); // 함수이름을 적어주도록
	lua_pushnumber(L, 10);
	lua_pushnumber(L, 20);
	int ret2 = lua_pcall(L, 2, 1, 0); // 파라미터 1개, 리턴값 1개 -> 스택의 맨 위에 저장
	int res = lua_tonumber(L, -1);
	if (0 != ret2) {
		std::cout << "error: " << lua_tostring(L, -1); // 에러를 L 머신에서 1개 뽑아라
		lua_pop(L, 1);
	}
	else {
		lua_pop(L, 1);
		std::cout << "result: " << res << std::endl;
	}

	lua_close(L); // delete 무조건 해줘야 함
}