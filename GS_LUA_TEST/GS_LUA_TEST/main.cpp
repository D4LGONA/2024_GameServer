#include <iostream>
#include "include/lua.hpp"

#pragma comment (lib, "lua54.lib")

int LUAapi_add(lua_State* L)
{
	int a = lua_tonumber(L, -1);
	int b = lua_tonumber(L, -2);
	lua_pop(L, 3); // 3�� ������ ��
	int c = a + b;
	lua_pushnumber(L, c);
	return 1; // Ǫ���Ѱ��� ����
}

int main()
{
	//const char* program = "print \"Hello from Lua\""; // �ڵ��� ���Ϸ� �ص� �ǰ� �̷��� �ص� ��

	lua_State* L = luaL_newstate(); // ����ӽ� �����ϴ� �Լ�
	luaL_openlibs(L); // �⺻ ���̺귯���� �ε�
	//luaL_loadbuffer(L, program, strlen(program), "line"); // �ڵ� �������� ��
	luaL_loadfile(L, "dragon.lua");
	// sql�Ҷ��� ���ڿ� ���̱��� �־���� ��
	int ret = lua_pcall(L, 0, 0, 0); // ����ӽ�, ���ڷ� �Ѱ��ٰ�, ���ϰ� ����(������ ����), ����ó���� �Լ�
	// ����!
	if (0 != ret) {
		std::cout << "error: " << lua_tostring(L, -1); // ������ L �ӽſ��� 1�� �̾ƶ�
		lua_pop(L, 1);
	}

	//lua_getglobal(L, "pos_x");
	//lua_getglobal(L, "pos_y"); // ���� �������� ��
	//int pos_x = lua_tonumber(L, -2);
	//int pos_y = lua_tonumber(L, -1);
	//lua_pop(L, 2);
	//std::cout << "pos_x: " << pos_x << ", pos_y: " << pos_y << std::endl;
	//// ���Ӽ������� �˾Ƽ� Ȱ���ϵ��� ...

	lua_register(L, "callC_add", LUAapi_add);

	lua_getglobal(L, "event_add"); // �Լ��̸��� �����ֵ���
	lua_pushnumber(L, 10);
	lua_pushnumber(L, 20);
	int ret2 = lua_pcall(L, 2, 1, 0); // �Ķ���� 1��, ���ϰ� 1�� -> ������ �� ���� ����
	int res = lua_tonumber(L, -1);
	if (0 != ret2) {
		std::cout << "error: " << lua_tostring(L, -1); // ������ L �ӽſ��� 1�� �̾ƶ�
		lua_pop(L, 1);
	}
	else {
		lua_pop(L, 1);
		std::cout << "result: " << res << std::endl;
	}

	lua_close(L); // delete ������ ����� ��
}