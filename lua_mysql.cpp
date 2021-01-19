#include <iostream>
#include <string.h>
#include <math.h>
#include <mysql/mysql.h>

extern "C"
{
	#include "lua.h"
	#include "lauxlib.h"
	#include "lualib.h"
}

static int l_mysql_init(lua_State *L) {
	lua_pushlightuserdata(L, (void *)mysql_init(NULL));
	return 1;
}

static int l_mysql_real_connect(lua_State *L) {
	MYSQL *mysql = (MYSQL *)lua_touserdata(L, 1);
	const char *host = lua_tostring(L, 2);
	const char *user = lua_tostring(L, 3);
	const char *pwd = lua_tostring(L, 4);
	const char *db = lua_tostring(L, 5);
	const int port = lua_tonumber(L, 6);
	lua_pushboolean(L, mysql_real_connect(mysql, host, user, pwd, db, port, NULL, 0) ? 1: 0);
	return 1;
}

static int l_mysql_error(lua_State *L) {
	MYSQL *mysql = (MYSQL *)lua_touserdata(L, 1);
	const char *error = mysql_error(mysql);
	lua_pushstring(L, error);
	return 1;
}

static int l_mysql_row_iter(lua_State *L) {
	MYSQL_RES* result = (MYSQL_RES *)lua_touserdata(L, lua_upvalueindex(1));
    int nr_field = lua_tointeger(L, lua_upvalueindex(2));
    MYSQL_FIELD* field_list = (MYSQL_FIELD*)lua_touserdata(L, lua_upvalueindex(3));

    MYSQL_ROW row = mysql_fetch_row(result);
    if (!row) {
    	return 0;
    }
	unsigned long *lengths = mysql_fetch_lengths(result);
    lua_newtable(L);
    for (int i = 0; i < nr_field; ++i) {
    	lua_pushstring(L, field_list[i].name);
        lua_pushlstring(L, row[i], lengths[i]);
        lua_settable(L, -3);
    }

    return 1;
}

static int l_mysql_query(lua_State *L) {
	MYSQL *mysql = (MYSQL *)lua_touserdata(L, 1);
	const char *sql = lua_tostring(L, 2);
	mysql_query(mysql, sql);

	MYSQL_RES *mysql_res = mysql_store_result(mysql);
	lua_pushlightuserdata(L, (void *)mysql_res);
	lua_pushinteger(L, mysql_num_fields(mysql_res));
	lua_pushlightuserdata(L, (void *)mysql_fetch_fields(mysql_res));
	lua_pushcclosure(L, l_mysql_row_iter, 3);
	return 1;
}

static int l_mysql_free_result(lua_State *L) {
	MYSQL_RES *mysql_res = (MYSQL_RES *)lua_touserdata(L, 1);
	mysql_free_result(mysql_res);
	return 0;
}

static int l_mysql_close(lua_State *L) {
	MYSQL *mysql = (MYSQL *)lua_touserdata(L, 1);
	mysql_close(mysql);
}


int main() {
	lua_State *L = luaL_newstate();
	luaL_openlibs(L);
	lua_register(L, "mysql_init", l_mysql_init);
	lua_register(L, "mysql_connect", l_mysql_real_connect);
	lua_register(L, "mysql_error", l_mysql_error);
	lua_register(L, "mysql_query", l_mysql_query);
	lua_register(L, "mysql_free", l_mysql_free_result);
	lua_register(L, "mysql_close", l_mysql_close);

	if(luaL_dofile(L, "./test.lua")) {
		printf("failed to invoke");
	}
	lua_close(L);
    return 0;  
}
