#include <iostream>
#include "include/lua.hpp"

#pragma comment (lib, "lua54.lib")

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

	lua_getglobal(L, "plustwo"); // �Լ��̸��� �����ֵ���
	lua_pushnumber(L, 10); // plustwo �Լ��� �Ķ����
	lua_pcall(L, 1, 1, 0); // �Ķ���� 1��, ���ϰ� 1�� -> ������ �� ���� ����
	int res = lua_tonumber(L, -1);
	lua_pop(L, 1);
	std::cout << "result: " << res << std::endl;

	lua_close(L); // delete ������ ����� ��
}