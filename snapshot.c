
#define LUA_LIB

#include <lua.h>
#include <lauxlib.h>
#include <string.h>

static void mark_object(lua_State *L, lua_State *dL, const void * parent, const char * desc);

#if LUA_VERSION_NUM == 501

static void
luaL_checkversion(lua_State *L) {
	if (lua_pushthread(L) == 0) {
		luaL_error(L, "Must require in main thread");
	}
	lua_setfield(L, LUA_REGISTRYINDEX, "mainthread");
}

static void
lua_rawsetp(lua_State *L, int idx, const void *p) {
	if (idx < 0) {
		idx += lua_gettop(L) + 1;
	}
	lua_pushlightuserdata(L, (void *)p);
	lua_insert(L, -2);
	lua_rawset(L, idx);
}

static int
lua_rawgetp(lua_State *L, int idx, const void *p) {
	if (idx < 0) {
		idx += lua_gettop(L) + 1;
	}
	lua_pushlightuserdata(L, (void *)p);
	lua_rawget(L, idx);
	return lua_type(L, -1);
}

static void
lua_getuservalue(lua_State *L, int idx) {
	lua_getfenv(L, idx);
}

static void
mark_function_env(lua_State *L, lua_State *dL, const void * t) {
	lua_getfenv(L,-1);
	if (lua_istable(L,-1)) {
		mark_object(L, dL, t, "[environment]");
	} else {
		lua_pop(L,1);
	}
}

static int 
lua_absindex (lua_State *L, int i) {
	if (i < 0 && i > LUA_REGISTRYINDEX)
	  i += lua_gettop(L) + 1;
	return i;
}

static int 
luaL_getsubtable (lua_State *L, int i, const char *name) {
	int abs_i = lua_absindex(L, i);
	luaL_checkstack(L, 3, "not enough stack slots");
	lua_pushstring(L, name);
	lua_gettable(L, abs_i);
	if (lua_istable(L, -1))
	  return 1;
	lua_pop(L, 1);
	lua_newtable(L);
	lua_pushstring(L, name);
	lua_pushvalue(L, -2);
	lua_settable(L, abs_i);
	return 0;
}

#else

#define mark_function_env(L,dL,t)

#endif

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#define TABLE 1
#define FUNCTION 2
#define SOURCE 3
#define THREAD 4
#define USERDATA 5
#define MARK 6

static bool
ismarked(lua_State *dL, const void *p) {
	lua_rawgetp(dL, MARK, p);
	if (lua_isnil(dL,-1)) {
		lua_pop(dL,1);
		lua_pushboolean(dL,1);
		lua_rawsetp(dL, MARK, p);
		return false;
	}
	lua_pop(dL,1);
	return true;
}

static const void *
readobject(lua_State *L, lua_State *dL, const void *parent, const char *desc) {
	int t = lua_type(L, -1);
	int tidx = 0;
	switch (t) {
	case LUA_TTABLE:
		tidx = TABLE;
		break;
	case LUA_TFUNCTION:
		tidx = FUNCTION;
		break;
	case LUA_TTHREAD:
		tidx = THREAD;
		break;
	case LUA_TUSERDATA:
		tidx = USERDATA;
		break;
	default:
		return NULL;
	}

	const void * p = lua_topointer(L, -1);
	if (ismarked(dL, p)) {
		lua_rawgetp(dL, tidx, p);
		if (!lua_isnil(dL,-1)) {
			lua_pushstring(dL,desc);
			lua_rawsetp(dL, -2, parent);
		}
		lua_pop(dL,1);
		lua_pop(L,1);
		return NULL;
	}

	lua_newtable(dL);
	lua_pushstring(dL,desc);
	lua_rawsetp(dL, -2, parent);
	lua_rawsetp(dL, tidx, p);

	return p;
}

static const char *
keystring(lua_State *L, int index, char * buffer) {
	int t = lua_type(L,index);
	switch (t) {
	case LUA_TSTRING:
		return lua_tostring(L,index);
	case LUA_TNUMBER:
		sprintf(buffer,"[%lg]",lua_tonumber(L,index));
		break;
	case LUA_TBOOLEAN:
		sprintf(buffer,"[%s]",lua_toboolean(L,index) ? "true" : "false");
		break;
	case LUA_TNIL:
		sprintf(buffer,"[nil]");
		break;
	default:
		sprintf(buffer,"[%s:%p]",lua_typename(L,t),lua_topointer(L,index));
		break;
	}
	return buffer;
}

static void
mark_table(lua_State *L, lua_State *dL, const void * parent, const char * desc) {
	const void * t = readobject(L, dL, parent, desc);
	if (t == NULL)
		return;

	bool weakk = false;
	bool weakv = false;
	if (lua_getmetatable(L, -1)) {
		lua_pushliteral(L, "__mode");
		lua_rawget(L, -2);
		if (lua_isstring(L,-1)) {
			const char *mode = lua_tostring(L, -1);
			if (strchr(mode, 'k')) {
				weakk = true;
			}
			if (strchr(mode, 'v')) {
				weakv = true;
			}
		}
		lua_pop(L,1);

		luaL_checkstack(L, LUA_MINSTACK, NULL);
		mark_table(L, dL, t, "[metatable]");
	}

	lua_pushnil(L);
	while (lua_next(L, -2) != 0) {
		if (weakv) {
			lua_pop(L,1);
		} else {
			char temp[32];
			const char * desc = keystring(L, -2, temp);
			mark_object(L, dL, t , desc);
		}
		if (!weakk) {
			lua_pushvalue(L,-1);
			mark_object(L, dL, t , "[key]");
		}
	}

	lua_pop(L,1);
}

static void
mark_userdata(lua_State *L, lua_State *dL, const void * parent, const char *desc) {
	const void * t = readobject(L, dL, parent, desc);
	if (t == NULL)
		return;
	if (lua_getmetatable(L, -1)) {
		mark_table(L, dL, t, "[metatable]");
	}

	lua_getuservalue(L,-1);
	if (lua_isnil(L,-1)) {
		lua_pop(L,2);
	} else {
		mark_table(L, dL, t, "[uservalue]");
		lua_pop(L,1);
	}
}

static void
mark_function(lua_State *L, lua_State *dL, const void * parent, const char *desc) {
	const void * t = readobject(L, dL, parent, desc);
	if (t == NULL)
		return;

	mark_function_env(L,dL,t);
	int i;
	for (i=1;;i++) {
		const char *name = lua_getupvalue(L,-1,i);
		if (name == NULL)
			break;
		mark_object(L, dL, t, name[0] ? name : "[upvalue]");
	}
	if (lua_iscfunction(L,-1)) {
		if (i==1) {
			// light c function
			lua_pushnil(dL);
			lua_rawsetp(dL, FUNCTION, t);
		}
		lua_pop(L,1);
	} else {
		lua_Debug ar;
		lua_getinfo(L, ">S", &ar);
		luaL_Buffer b;
		luaL_buffinit(dL, &b);
		luaL_addstring(&b, ar.short_src);
		char tmp[16];
		sprintf(tmp,":%d",ar.linedefined);
		luaL_addstring(&b, tmp);
		luaL_pushresult(&b);
		lua_rawsetp(dL, SOURCE, t);
	}
}

static void
mark_thread(lua_State *L, lua_State *dL, const void * parent, const char *desc) {
	const void * t = readobject(L, dL, parent, desc);
	if (t == NULL)
		return;
	int level = 0;
	lua_State *cL = lua_tothread(L,-1);
	if (cL == L) {
		level = 1;
	} else {
		// mark stack
		int top = lua_gettop(cL);
		luaL_checkstack(cL, 1, NULL);
		int i;
		char tmp[16];
		for (i=0;i<top;i++) {
			lua_pushvalue(cL, i+1);
			sprintf(tmp, "[%d]", i+1);
			mark_object(cL, dL, cL, tmp);
		}
	}
	lua_Debug ar;
	luaL_Buffer b;
	luaL_buffinit(dL, &b);
	while (lua_getstack(cL, level, &ar)) {
		char tmp[128];
		lua_getinfo(cL, "Sl", &ar);
		luaL_addstring(&b, ar.short_src);
		if (ar.currentline >=0) {
			char tmp[16];
			sprintf(tmp,":%d ",ar.currentline);
			luaL_addstring(&b, tmp);
		}

		int i,j;
		for (j=1;j>-1;j-=2) {
			for (i=j;;i+=j) {
				const char * name = lua_getlocal(cL, &ar, i);
				if (name == NULL)
					break;
				snprintf(tmp, sizeof(tmp), "%s : %s:%d",name,ar.short_src,ar.currentline);
				mark_object(cL, dL, t, tmp);
			}
		}

		++level;
	}
	luaL_pushresult(&b);
	lua_rawsetp(dL, SOURCE, t);
	lua_pop(L,1);
}

static void 
mark_object(lua_State *L, lua_State *dL, const void * parent, const char *desc) {
	luaL_checkstack(L, LUA_MINSTACK, NULL);
	int t = lua_type(L, -1);
	switch (t) {
	case LUA_TTABLE:
		mark_table(L, dL, parent, desc);
		break;
	case LUA_TUSERDATA:
		mark_userdata(L, dL, parent, desc);
		break;
	case LUA_TFUNCTION:
		mark_function(L, dL, parent, desc);
		break;
	case LUA_TTHREAD:
		mark_thread(L, dL, parent, desc);
		break;
	default:
		lua_pop(L,1);
		break;
	}
}

static int
count_table(lua_State *L, int idx) {
	int n = 0;
	lua_pushnil(L);
	while (lua_next(L, idx) != 0) {
		++n;
		lua_pop(L,1);
	}
	return n;
}

static void
gen_table_desc(lua_State *dL, luaL_Buffer *b, const void * parent, const char *desc) {
	char tmp[32];
	size_t l = sprintf(tmp,"%p : ",parent);
	luaL_addlstring(b, tmp, l);
	luaL_addstring(b, desc);
	luaL_addchar(b, '\n');
}

static void
pdesc(lua_State *L, lua_State *dL, int idx, const char * typename) {
	lua_pushnil(dL);
	while (lua_next(dL, idx) != 0) {
		luaL_Buffer b;
		luaL_buffinit(L, &b);
		const void * key = lua_touserdata(dL, -2);
		if (idx == FUNCTION) {
			lua_rawgetp(dL, SOURCE, key);
			if (lua_isnil(dL, -1)) {
				luaL_addstring(&b,"cfunction\n");
			} else {
				size_t l = 0;
				const char * s = lua_tolstring(dL, -1, &l);
				luaL_addlstring(&b,s,l);
				luaL_addchar(&b,'\n');
			}
			lua_pop(dL, 1);
		} else if (idx == THREAD) {
			lua_rawgetp(dL, SOURCE, key);
			size_t l = 0;
			const char * s = lua_tolstring(dL, -1, &l);
			luaL_addlstring(&b,s,l);
			luaL_addchar(&b,'\n');
			lua_pop(dL, 1);
		} else {
			luaL_addstring(&b, typename);
			luaL_addchar(&b,'\n');
		}
		lua_pushnil(dL);
		while (lua_next(dL, -2) != 0) {
			const void * parent = lua_touserdata(dL,-2);
			const char * desc = luaL_checkstring(dL,-1);
			gen_table_desc(dL, &b, parent, desc);
			lua_pop(dL,1);
		}
		luaL_pushresult(&b);
		lua_rawsetp(L, -2, key);
		lua_pop(dL,1);
	}
}

static void
gen_result(lua_State *L, lua_State *dL) {
	int count = 0;
	count += count_table(dL, TABLE);
	count += count_table(dL, FUNCTION);
	count += count_table(dL, USERDATA);
	count += count_table(dL, THREAD);
	lua_createtable(L, 0, count);
	pdesc(L, dL, TABLE, "table");
	pdesc(L, dL, USERDATA, "userdata");
	pdesc(L, dL, FUNCTION, "function");
	pdesc(L, dL, THREAD, "thread");
}

static int
snapshot(lua_State *L) {
	int i;
	lua_State *dL = luaL_newstate();
	for (i=0;i<MARK;i++) {
		lua_newtable(dL);
	}
	lua_pushvalue(L, LUA_REGISTRYINDEX);
	mark_table(L, dL, NULL, "[registry]");
	gen_result(L, dL);
	lua_close(dL);
	return 1;
}


// struct profiler_log {
// 	int linedefined;
// 	char source[LUA_IDSIZE];
// };

// struct profiler_count {
// 	int total;
// 	int index;
// };

// static void
// profiler_hook(lua_State *L, lua_Debug *ar) {
// 	if (lua_rawgetp(L, LUA_REGISTRYINDEX, L) != LUA_TUSERDATA) {
// 		lua_pop(L, 1);
// 		return;
// 	}
// 	struct profiler_count * p = lua_touserdata(L, -1);
// 	lua_pop(L, 1);
// 	struct profiler_log * log = (struct profiler_log *)(p+1);
// 	int index = p->index++;
// 	while(index >= p->total) {
// 		index -= p->total;
// 	}
// 	if (lua_getinfo(L, "S", ar) != 0) {
// 		log[index].linedefined = ar->linedefined;
// 		strcpy(log[index].source, ar->short_src);
// 	} else {
// 		log[index].linedefined = 1;
// 		strcpy(log[index].source, "[unknown]");
// 	}
// }

// static int
// lstart(lua_State *L) {
// 	lua_State *cL = L;
// 	int args = 0;
// 	if (lua_isthread(L, 1)) {
// 		cL = lua_tothread(L, 1);
// 		args = 1;
// 	}
// 	int count = luaL_optinteger(L, args+1, 1000);
// 	int interval = luaL_optinteger(L, args+2, 100);
// 	struct profiler_count * p = lua_newuserdata(L, sizeof(struct profiler_count) + count * sizeof(struct profiler_log));
// 	p->total = count;
// 	p->index = 0;
// 	lua_pushvalue(L, -1);
// 	lua_rawsetp(L, LUA_REGISTRYINDEX, cL);
// 	lua_sethook(cL, profiler_hook, LUA_MASKCOUNT, interval);
	
// 	return 0;
// }

// static int
// lstop(lua_State *L) {
// 	lua_State *cL = L;
// 	if (lua_isthread(L, 1)) {
// 		cL = lua_tothread(L, 1);
// 	}
// 	if (lua_rawgetp(L, LUA_REGISTRYINDEX, cL) != LUA_TNIL) {
// 		lua_pushnil(L);
// 		lua_rawsetp(L, LUA_REGISTRYINDEX, cL);
// 		lua_sethook(cL, NULL, 0, 0);
// 	} else {
// 		return luaL_error(L, "thread profiler not begin");
// 	}

// 	return 0;
// }

// static int
// linfo(lua_State *L) {
// 	lua_State *cL = L;
// 	if (lua_isthread(L, 1)) {
// 		cL = lua_tothread(L, 1);
// 	}
// 	if (lua_rawgetp(L, LUA_REGISTRYINDEX, cL) != LUA_TUSERDATA) {
// 		return luaL_error(L, "thread profiler not begin");
// 	}
// 	struct profiler_count * p = lua_touserdata(L, -1);
// 	struct profiler_log * log = (struct profiler_log *)(p+1);
// 	lua_newtable(L);
// 	int n = (p->index > p->total) ? p->total : p->index;
// 	int i;
// 	for (i=0;i<n;i++) {
// 		luaL_getsubtable(L, -1, log[i].source);
// 		lua_rawgeti(L, -1, log[i].linedefined);
// 		int c = lua_tointeger(L, -1);
// 		lua_pushinteger(L, c + 1);
// 		// subtbl, c, c + 1
// 		lua_rawseti(L, -3, log[i].linedefined);
// 		lua_pop(L, 2);
// 	}
// 	lua_pushinteger(L, p->index);
// 	return 2;
// }



LUALIB_API int 
luaopen_snapshot(lua_State *L) {
	luaL_checkversion(L);
	
	static const luaL_Reg snapshot_funcs[] = {
		{ "snapshot",    snapshot },
		{ NULL, NULL }
	};

	luaL_register(L, "snapshot", snapshot_funcs);
    return 1;
}

// LUALIB_API int
// luaopen_profiler(lua_State *L) {
// 	luaL_checkversion(L);
	
// 	static const luaL_Reg profiler_funcs[] = {
// 		{ "start", lstart },
// 		{ "stop", lstop },
// 		{ "info", linfo },
// 		{ NULL, NULL },
// 	};

// 	luaL_register(L, "profiler", profiler_funcs);
//     return 1;
// }