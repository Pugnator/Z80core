#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
#include <assembler.h>
#include <dassembler.h>
#include <luabind.h>

#define LUA_LIB

#define FIRST_ARG (-1)
#define SECOND_ARG (-2)
#define THIRD_ARG (-3)
#define FORTH_ARG (-4)

typedef struct lua_bind_func
{
	int ( *lua_c_api ) ( lua_State* );
	const char* lua_func_name;
} lua_bind_func;

static int lua_asm_load_source ( lua_State* L );

static const lua_bind_func lua_c_api_list[] = 
{
	{.lua_func_name="load_asm", .lua_c_api=&lua_asm_load_source},
  {.lua_func_name="disasm_buffer", .lua_c_api=&lua_disasm_buffer},
  {.lua_func_name="disasm_listing", .lua_c_api=&lua_disasm_listing},
	{ NULL, NULL},
};

void register_functions(lua_State* L)
{
  for(int i = 0; lua_c_api_list[i].lua_func_name; ++i)
  {
    lua_register(L, lua_c_api_list[i].lua_func_name, lua_c_api_list[i].lua_c_api);
  }
}

/* Assembler */

static int lua_asm_load_source ( lua_State* L )
{
	const char* source = lua_tostring ( L, THIRD_ARG );
  const char* target = lua_tostring ( L, SECOND_ARG );
  const char* format = lua_tostring ( L, FIRST_ARG );  
	lua_pushboolean ( L,  assembly_listing (source, target, format));
	return 1;
}

/* DisAssembler */

static int lua_disasm_buffer ( lua_State* L )
{
  const char* buffer = lua_tostring ( L, SECOND_ARG );
  int size = lua_tonumber ( L, FIRST_ARG );  
	disassembly_buffer (buffer, size, NULL);
	return 0;
}

static int lua_disasm_listing ( lua_State* L )
{
  const char* filename = lua_tostring ( L, FIRST_ARG );  
	disassembly_file (filename, NULL);
	return 0;
}

/* Module entry */
int luaopen_z80lib(lua_State* L) 
{
    register_functions(L);    
    return 0;
}