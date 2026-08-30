#pragma once
#include "game.h"
#include "wrappers.h"

#include <type_traits>
#include <tuple>
#include <array>
#include <functional>
#include <string>
#include <vector>
#include <cstring>
#include <new>

extern "C"
{
	#include "src/lua.h"
	#include "src/lauxlib.h"
	#include "src/lualib.h"
}

struct lua_State;


//========================================================
//Lua operators
//========================================================

enum class LuaOperator
{
	Add,
	Sub,
	Mul,
	Div,
	Mod,
	Pow,
	Unm,
	Eq,
	Lt,
	Le,
	Concat,
	Len,
	ToString,
	MAX_COUNT
};

//========================================================
//Registered variable access mode
//========================================================

enum class LuaVariableMode
{
	ReadOnly,
	ReadWrite
};

//========================================================
//Struct member
//========================================================

struct LuaStructMember
{
	const char* name;

	std::function<void(lua_State*, void*)> getter;
	std::function<void(lua_State*, void*)> setter;
};

//========================================================
//Struct constructor
//========================================================

struct LuaStructConstructor
{
	std::function<bool(lua_State*)> matches;
	std::function<void(lua_State*, void*)> construct;
	std::string signature;
};

//========================================================
//Lua userdata wrapper
//
//Every Lua representation of a registered struct uses this.
//
//object:
//    Pointer to the actual C++ object.
//
//owns:
//    True  = Lua owns the object and must destroy it.
//    False = object belongs to somebody else.
//
//readOnly:
//    Prevents Lua from writing through the userdata.
//========================================================

struct LuaStructUserdata
{
	void* object = nullptr;

	bool owns = false;
	bool readOnly = false;
};

//========================================================
//Registered struct type
//========================================================

struct LuaStructType
{
	const char* name = nullptr;
	size_t size = 0;
	bool allowConstruction = true;

	std::vector<LuaStructMember> members;

	std::vector<LuaStructConstructor> constructors;
	std::vector<std::string> constructorTypes;

	std::function<void(void*)> destructor;

	// Push an owned/copy struct.
	std::function<void(lua_State*, const void*)> push;

	// Get the underlying C++ object from Lua userdata.
	std::function<void* (lua_State*, int)> get;

	// Check whether Lua value represents this struct.
	std::function<bool(lua_State*, int)> is;

	std::array<std::function<int(lua_State*)>, static_cast<size_t>(LuaOperator::MAX_COUNT)> operators{};
};


//========================================================
//Function traits
//========================================================

template<typename T>
struct LuaFunctionTraits;

//Const lambda
template<typename Return, typename Class, typename... Args>
struct LuaFunctionTraits<Return(Class::*)(Args...) const>
{
	using ReturnType = Return;

	template<size_t I>
	using Arg = std::tuple_element_t<I, std::tuple<Args...>>;

	static constexpr size_t ArgumentCount = sizeof...(Args);
};

//Non-const lambda
template<typename Return, typename Class, typename... Args>
struct LuaFunctionTraits<Return(Class::*)(Args...)>
{
	using ReturnType = Return;

	template<size_t I>
	using Arg = std::tuple_element_t<I, std::tuple<Args...>>;

	static constexpr size_t ArgumentCount = sizeof...(Args);
};


//Remove const/reference from an argument.
//
//const Vector& -> Vector
//Vector&       -> Vector
//const int&    -> int
//float         -> float
template<typename T>
using LuaArgumentType =
std::remove_cv_t<std::remove_reference_t<T>>;


class LuaManager
{
	Game* m_Game = nullptr;
	lua_State* m_State = nullptr;
	bool m_Initialized = false;

	std::string m_WorkingDir;

private:
#pragma region Struct Helpers
	//========================================================
	//Struct type storage
	//========================================================

	template<typename T>
	static LuaStructType*& GetStructTypeStorage()
	{
		static LuaStructType* type = nullptr;
		return type;
	}

	template<typename T>
	static LuaStructType* GetRegisteredStructType()
	{
		return GetStructTypeStorage<T>();
	}

	//========================================================
	//Resolve Lua userdata to its actual C++ object
	//========================================================

	static LuaStructUserdata* GetStructUserdata(lua_State* state, int index)
	{
		return static_cast<LuaStructUserdata*>(luaL_checkudata(state, index, "LuaStructUserdata"));
	}

	//========================================================
	//Basic Lua conversions
	//========================================================

	template<typename T>
	static void LuaPush(lua_State* state, const T& value)
	{
		if constexpr (std::is_same_v<T, std::string>)
			lua_pushlstring(state, value.data(), value.size());

		else if constexpr (std::is_same_v<T, const char*>)
			lua_pushstring(state, value);

		else if constexpr (std::is_same_v<T, bool>)
			lua_pushboolean(state, value);

		else if constexpr (std::is_integral_v<T>)
			lua_pushinteger(state, static_cast<lua_Integer>(value));

		else if constexpr (std::is_floating_point_v<T>)
			lua_pushnumber(state, static_cast<lua_Number>(value));

		else
		{
			LuaStructType* type = GetRegisteredStructType<T>();

			if (type && type->push)
			{
				type->push(state, &value);
				return;
			}

			luaL_error(state, "LuaPush does not support this C++ type");
		}
	}

	//========================================================
	//Get basic Lua value
	//========================================================

	template<typename T>
	static T LuaGet(lua_State* state, int index)
	{
		if constexpr (std::is_same_v<T, std::string>)
			return luaL_checkstring(state, index);

		else if constexpr (std::is_same_v<T, bool>)
		{
			luaL_checktype(state, index, LUA_TBOOLEAN);
			return lua_toboolean(state, index) != 0;
		}

		else if constexpr (std::is_integral_v<T>)
			return static_cast<T>(luaL_checkinteger(state, index));

		else if constexpr (std::is_floating_point_v<T>)
			return static_cast<T>(luaL_checknumber(state, index));

		else
		{
			LuaStructType* type = GetRegisteredStructType<T>();

			if (type && type->get)
			{
				void* object = type->get(state, index);
				return *static_cast<T*>(object);
			}

			luaL_error(state, "LuaGet does not support this C++ type");
			return T{};
		}
	}

	//========================================================
	//Get struct pointer
	//========================================================

	template<typename T>
	static T* LuaGetStruct(lua_State* state, int index)
	{
		LuaStructType* type = GetRegisteredStructType<T>();

		if (!type || !type->get)
		{
			luaL_error(state, "LuaGetStruct does not support this type");
			return nullptr;
		}

		return static_cast<T*>(type->get(state, index));
	}

	//========================================================
	//Type names
	//========================================================

	template<typename T>
	static const char* LuaTypeName()
	{
		return "unknown";
	}

	template<>
	static const char* LuaTypeName<std::string>()
	{
		return "string";
	}

	template<>
	static const char* LuaTypeName<int>()
	{
		return "integer";
	}

	template<>
	static const char* LuaTypeName<float>()
	{
		return "float";
	}

	template<>
	static const char* LuaTypeName<double>()
	{
		return "double";
	}

	template<>
	static const char* LuaTypeName<bool>()
	{
		return "boolean";
	}

	//========================================================
	//Lua type checking
	//========================================================

	template<typename T>
	static bool LuaIs(lua_State* state, int index)
	{
		if constexpr (std::is_same_v<T, std::string>)
			return lua_isstring(state, index);

		else if constexpr (std::is_same_v<T, bool>)
			return lua_isboolean(state, index);

		else if constexpr (std::is_integral_v<T>)
			return lua_isinteger(state, index);

		else if constexpr (std::is_floating_point_v<T>)
			return lua_isnumber(state, index);

		else
		{
			LuaStructType* type = GetRegisteredStructType<T>();

			if (type && type->is)
				return type->is(state, index);

			return false;
		}
	}

	//========================================================
	//Operator -> Lua metamethod
	//========================================================

	static const char* LuaOperatorName(LuaOperator op)
	{
		switch (op)
		{
			case LuaOperator::Add: return "__add";
			case LuaOperator::Sub: return "__sub";
			case LuaOperator::Mul: return "__mul";
			case LuaOperator::Div: return "__div";
			case LuaOperator::Mod: return "__mod";
			case LuaOperator::Pow: return "__pow";
			case LuaOperator::Unm: return "__unm";
			case LuaOperator::Eq: return "__eq";
			case LuaOperator::Lt: return "__lt";
			case LuaOperator::Le: return "__le";
			case LuaOperator::Concat: return "__concat";
			case LuaOperator::Len: return "__len";
			case LuaOperator::ToString: return "__tostring";
			default: return nullptr;
		}
	}

	//========================================================
	//Get Lua argument
	//========================================================

	template<typename T>
	static auto LuaGetArgument(lua_State* state, int index)
	{
		using Type = LuaArgumentType<T>;
		return LuaGet<Type>(state, index);
	}

	//========================================================
	//Call C++ function with Lua arguments
	//========================================================

	template<typename Func, typename Traits, size_t... I>
	static int LuaCallFunction(lua_State* state, Func& function, std::index_sequence<I...>)
	{
		if constexpr (std::is_void_v<typename Traits::ReturnType>)
		{
			function(LuaGetArgument<typename Traits::template Arg<I>>(state, I + 1)...);
			return 0;
		}
		else
		{
			auto result = function(LuaGetArgument<typename Traits::template Arg<I>>(state, I + 1)...);
			LuaPush(state, result);
			return 1;
		}
	}

	//========================================================
	//Operator dispatcher
	//========================================================

	static int LuaStructOperator(lua_State* state)
	{
		LuaStructType* type = static_cast<LuaStructType*>(lua_touserdata(state, lua_upvalueindex(1)));
		if (!type)
			return luaL_error(state, "Invalid struct type");

		size_t operatorIndex = static_cast<size_t>(lua_tointeger(state, lua_upvalueindex(2)));
		if (operatorIndex >= static_cast<size_t>(LuaOperator::MAX_COUNT))
			return luaL_error(state, "Invalid struct operator");

		auto& function = type->operators[operatorIndex];
		if (!function)
			return luaL_error(state, "Struct '%s' does not support this operator", type->name);

		return function(state);
	}

	//========================================================
	//Struct registration helper
	//========================================================

	template<typename T>
	class LuaStructRegistrar
	{
		LuaStructType& m_Type;

	public:
		LuaStructRegistrar(LuaStructType& type) : m_Type(type) {}

		//====================================================
		//Member
		//====================================================

		template<typename M>
		void Member(const char* name, M T::* member)
		{
			LuaStructMember field;

			field.name = name;
			field.getter = [member](lua_State* state, void* object)
			{
				T* value = static_cast<T*>(object);
				LuaPush(state, value->*member);
			};

			field.setter = [member](lua_State* state, void* object)
			{
				T* value = static_cast<T*>(object);
				value->*member = LuaGet<M>(state, -1);
			};

			m_Type.members.push_back(std::move(field));
		}

		//====================================================
		//Constructor
		//====================================================

		template<typename... Args>
		void Constructor()
		{
			LuaStructConstructor ctor;
			ctor.matches = [](lua_State* state) -> bool
			{
				if (lua_gettop(state) != sizeof...(Args))
					return false;

				int index = 1;

				return (LuaIs<Args>(state, index++) && ...);
			};

			ctor.construct = [](lua_State* state, void* memory)
			{
				new (memory) T(LuaGet<Args>(state, 1)...);
			};

			std::string signature = "(";
			bool first = true;

			((signature += (first ? "" : ", "), signature += LuaTypeName<Args>(), first = false), ...);

			signature += ")";
			ctor.signature = signature;
			m_Type.constructorTypes.push_back(signature);
			m_Type.constructors.push_back(std::move(ctor));
		}

		//====================================================
		//Disable constructors
		//====================================================

		void NoConstructor()
		{
			m_Type.allowConstruction = false;
			m_Type.constructors.clear();
		}

		//====================================================
		//Destructor
		//====================================================

		void Destructor(std::function<void(void*)> destructor)
		{
			m_Type.destructor = std::move(destructor);
		}

		//====================================================
		//Operator
		//====================================================

		template<typename Func>
		void Operator(LuaOperator op, Func&& function)
		{
			using Function = std::decay_t<Func>;
			using Traits = LuaFunctionTraits<decltype(&Function::operator())>;

			m_Type.operators[static_cast<size_t>(op)] = [function](lua_State* state) mutable -> int
			{
				return LuaCallFunction<Function, Traits>(state, function, std::make_index_sequence<Traits::ArgumentCount>{});
			};
		}
	};

	//========================================================
	//Get struct type from userdata
	//========================================================

	static LuaStructType* GetStructType(lua_State* state)
	{
		LuaStructUserdata* userdata = GetStructUserdata(state, 1);

		if (!userdata)
			return nullptr;

		lua_getmetatable(state, 1);
		lua_getfield(state, -1, "__struct_type");

		LuaStructType* type = static_cast<LuaStructType*>(lua_touserdata(state, -1));

		lua_pop(state, 2);
		return type;
	}

	//========================================================
	//Constructor dispatcher
	//========================================================

	static int LuaStructConstructorCall(lua_State* state)
	{
		LuaStructType* type = static_cast<LuaStructType*>(lua_touserdata(state, lua_upvalueindex(1)));

		if (!type)
			return luaL_error(state, "Invalid struct type");

		if (!type->allowConstruction)
			return luaL_error(state, "Struct '%s' cannot be constructed from Lua", type->name);

		for (const auto& constructor : type->constructors)
		{
			if (constructor.matches(state))
			{
				//Wrapper userdata.
				LuaStructUserdata* userdata = static_cast<LuaStructUserdata*>(lua_newuserdata(state, sizeof(LuaStructUserdata)));

				//Constructor-created objects are owned by Lua.
				userdata->object = ::operator new(type->size);
				userdata->owns = true;
				userdata->readOnly = false;

				constructor.construct(state, userdata->object);

				luaL_getmetatable(state, type->name);

				lua_setmetatable(state, -2);
				return 1;
			}
		}

		return luaL_error(state, "No matching constructor for '%s'", type->name);
	}

	//========================================================
	//__index
	//========================================================

	static int LuaStructIndex(lua_State* state)
	{
		LuaStructType* type = GetStructType(state);

		if (!type)
			return luaL_error(state, "Invalid struct");

		LuaStructUserdata* userdata = GetStructUserdata(state, 1);

		if (!userdata || !userdata->object)
			return luaL_error(state, "Invalid struct object");

		const char* name = luaL_checkstring(state, 2);
		for (const auto& member : type->members)
		{
			if (std::strcmp(member.name, name) == 0)
			{
				member.getter(state, userdata->object);
				return 1;
			}
		}

		return luaL_error(state, "Struct '%s' has no member '%s'", type->name, name);
	}

	//========================================================
	//__newindex
	//========================================================

	static int LuaStructNewIndex(lua_State* state)
	{
		LuaStructType* type = GetStructType(state);
		if (!type)
			return luaL_error(state, "Invalid struct");

		LuaStructUserdata* userdata = GetStructUserdata(state, 1);

		if (!userdata || !userdata->object)
			return luaL_error(state, "Invalid struct object");

		//Read-only variable protection
		if (userdata->readOnly)
		{
			const char* name = luaL_checkstring(state, 2);

			return luaL_error(state, "Struct '%s' is read-only", type->name);
		}

		const char* name = luaL_checkstring(state, 2);
		for (const auto& member : type->members)
		{
			if (std::strcmp(member.name, name) == 0)
			{
				if (!member.setter)
					return luaL_error(state, "Member '%s.%s' is read-only", type->name, name);

				member.setter(state, userdata->object);
				return 0;
			}
		}

		return luaL_error(state, "Struct '%s' has no member '%s'", type->name, name);
	}

	//========================================================
	//__gc
	//========================================================

	static int LuaStructGC(lua_State* state)
	{
		LuaStructType* type = GetStructType(state);

		if (!type)
			return 0;

		LuaStructUserdata* userdata = GetStructUserdata(state, 1);
		if (!userdata || !userdata->object)
			return 0;

		//================================================
		//IMPORTANT:
		//
		//Registered variables do NOT own their object.
		//Therefore Lua must never destroy them.
		//================================================
		if (userdata->owns)
		{
			if (type->destructor)
				type->destructor(userdata->object);

			::operator delete(userdata->object);
		}

		userdata->object = nullptr;
		return 0;
	}

	//========================================================
	//Push a reference to an existing variable
	//========================================================

	template<typename T>
	static void LuaPushVariable(lua_State* state, const char* typeName, T* variable, LuaVariableMode mode)
	{
		if (!variable)
		{
			lua_pushnil(state);
			return;
		}

		LuaStructUserdata* userdata = static_cast<LuaStructUserdata*>(lua_newuserdata(state, sizeof(LuaStructUserdata)));

		//================================================
		//This is the critical difference from a normal
		//Lua struct.
		//
		//We store the ORIGINAL C++ address.
		//================================================

		userdata->object = variable;

		//Lua does NOT own the variable.
		userdata->owns = false;

		//Access mode controls __newindex.
		userdata->readOnly = mode == LuaVariableMode::ReadOnly;

		luaL_getmetatable(state, typeName);
		lua_setmetatable(state, -2);
	}

	//========================================================
	//Variable registration helper
	//========================================================

	template<typename T>
	void RegisterVariableInternal(T& variable, const char* name,LuaVariableMode mode)
	{
		LuaStructType* type = GetRegisteredStructType<T>();

		if (!type)
		{
			luaL_error(m_State, "Cannot register variable '%s': " "type has not been registered with RegisterStruct", name);
			return;
		}

		//Push the variable as a reference.
		LuaPushVariable(m_State, type->name, &variable, mode);
		lua_setglobal(m_State, name);

		if (m_Game->m_VRDebuglvl > 1)
		{
			const char* modeName = mode == LuaVariableMode::ReadOnly ? "ReadOnly" : "ReadWrite";

			m_Game->logMsg(LOGTYPE_DEBUG, "LuaManager: Registered variable %s (%s)", name, modeName);
		}
	}
#pragma endregion

public:
#pragma region Helpers
	//========================================================
	//Register enum
	//========================================================

	template<typename T>
	void RegisterEnum(T value, const char* str)
	{
		lua_pushinteger(m_State, value);
		lua_setfield(m_State, -2, str);

		if (m_Game->m_VRDebuglvl > 1)
			m_Game->logMsg(LOGTYPE_DEBUG, "LuaManager: Registered enum %s", str);
	}

	//========================================================
	//Register function
	//========================================================

	void RegisterFunction(lua_CFunction function, const char* str)
	{
		lua_pushcfunction(m_State, function);
		lua_setfield(m_State, -2, str);

		if (m_Game->m_VRDebuglvl > 1)
			m_Game->logMsg(LOGTYPE_DEBUG, "LuaManager: Registered function %s", str);
	}

	//========================================================
	//Register member function
	//========================================================

	void RegisterMemberFunction(lua_CFunction function, const char* str)
	{
		lua_pushlightuserdata(m_State, this);
		lua_pushcclosure(m_State, function, 1);
		lua_setfield(m_State, -2, str);

		if (m_Game->m_VRDebuglvl > 1)
			m_Game->logMsg(LOGTYPE_DEBUG, "LuaManager: Registered function %s", str);
	}

	//========================================================
	//RegisterStruct
	//This defines the TYPE.
	//
	//RegisterVariable() can then expose existing instances of this type without copying them.
	//
	//Example:
	//RegisterStruct<Vector>("Vector", [](auto& type)
	//{
	//    type.Constructor<float, float>();
	//
	//    type.Member("x", &Vector::x);
	//    type.Member("y", &Vector::y);
	//});
	//========================================================

	template<typename T, typename Registration>
	void RegisterStruct(const char* name, Registration registration)
	{
		LuaStructType* type = new LuaStructType();

		type->name = name;
		type->size = sizeof(T);
		GetStructTypeStorage<T>() = type;

		//Generic Lua -> C++ conversion
		type->push = [name](lua_State* state, const void* value)
		{
			const T* object = static_cast<const T*>(value);

			//Allocate the actual object separately.
			//
			//This lets userdata simply hold a pointer,
			//which is also what registered variables use.
			T* copy = new T(*object);

			LuaStructUserdata* userdata = static_cast<LuaStructUserdata*>(lua_newuserdata(state, sizeof(LuaStructUserdata)));

			userdata->object = copy;
			userdata->owns = true;
			userdata->readOnly = false;

			luaL_getmetatable(state, name);
			lua_setmetatable(state, -2);
		};

		//Generic Lua -> C++ get
		type->get = [name](lua_State* state, int index) -> void*
		{
			LuaStructUserdata* userdata = static_cast<LuaStructUserdata*>(luaL_checkudata(state, index, name));

			if (!userdata || !userdata->object)
			{
				luaL_error(state, "Invalid '%s' userdata", name);
				return nullptr;
			}

			return userdata->object;
		};

		//Generic type check
		type->is = [name](lua_State* state, int index)
		{
			return luaL_testudata(state, index, name) != nullptr;
		};

		//Generic default constructor
		{
			LuaStructConstructor constructor;

			constructor.matches = [](lua_State* state)
			{
				return lua_gettop(state) == 0;
			};

			constructor.construct = [](lua_State*, void* memory)
			{
				new (memory) T();
			};

			constructor.signature = "()";
			type->constructorTypes.push_back("()");
			type->constructors.push_back(std::move(constructor));
		}

		//Generic destructor
		type->destructor = [](void* memory)
		{
			static_cast<T*>(memory)->~T();
		};

		//Registration
		LuaStructRegistrar<T> registrar(*type);
		registration(registrar);

		//Debug logging
		if (m_Game->m_VRDebuglvl > 1)
		{
			m_Game->logMsg(LOGTYPE_DEBUG, "LuaManager: Registered struct %s", name);

			for (const auto& member : type->members)
			{
				m_Game->logMsg(LOGTYPE_DEBUG, "LuaManager: Member %s", member.name);
			}

			for (const auto& constructor : type->constructorTypes)
			{
				m_Game->logMsg(LOGTYPE_DEBUG, "LuaManager: Constructor %s", constructor.c_str());
			}

			for (size_t i = 0; i < static_cast<size_t>(LuaOperator::MAX_COUNT); ++i)
			{
				if (!type->operators[i])
					continue;

				LuaOperator op = static_cast<LuaOperator>(i);
				const char* operatorName = LuaOperatorName(op);

				if (!operatorName)
					continue;

				m_Game->logMsg(LOGTYPE_DEBUG, "LuaManager: Operator %s", operatorName);
			}
		}

		//Create metatable
		luaL_newmetatable(m_State, name);

		//Store type information
		lua_pushlightuserdata(m_State, type);
		lua_setfield(m_State, -2, "__struct_type");

		//Member access
		lua_pushcfunction(m_State, LuaStructIndex);
		lua_setfield(m_State, -2, "__index");

		//Member assignment
		lua_pushcfunction(m_State, LuaStructNewIndex);
		lua_setfield(m_State, -2, "__newindex");

		//Destructor
		lua_pushcfunction(m_State, LuaStructGC);
		lua_setfield(m_State, -2, "__gc");

		//Operators

		for (size_t i = 0; i < static_cast<size_t>(LuaOperator::MAX_COUNT); i++)
		{
			if (!type->operators[i])
				continue;

			const char* operatorName = LuaOperatorName(static_cast<LuaOperator>(i));

			if (!operatorName)
				continue;

			//Upvalue #1 = LuaStructType*
			lua_pushlightuserdata(m_State, type);

			//Upvalue #2 = operator index
			lua_pushinteger(m_State, static_cast<lua_Integer>(i));
			lua_pushcclosure(m_State, LuaStructOperator, 2);
			lua_setfield(m_State, -2, operatorName);
		}

		lua_pop(m_State,1);

		//Register constructor globally
		lua_pushlightuserdata(m_State, type);
		lua_pushcclosure(m_State, LuaStructConstructorCall, 1);
		lua_setglobal(m_State, name);
	}

	//========================================================
	//RegisterVariable
	//Exposes an EXISTING C++ variable to Lua.
	//
	//IMPORTANT: No copy is made.
	//
	//LuaStructUserdata::object points directly at the supplied variable.
	//Therefore: C++:
	//    m_Config.foo = 10;
	//
	//Lua:
	//    Config.foo
	// 
	//will see 10 immediately.
	//And with ReadWrite:
	//Lua:
	//
	//    Config.foo = 20
	//
	//modifies the actual C++ variable.
	//========================================================

	template<typename T>
	void RegisterVariable(T& variable, const char* name, LuaVariableMode mode)
	{
		RegisterVariableInternal(variable, name, mode);
	}

	//========================================================
	//Scope
	//========================================================

	template<typename Func>
	void Scope(const char* name, Func&& func)
	{
		lua_newtable(m_State);

		func();

		lua_setglobal(m_State, name);

		if (m_Game->m_VRDebuglvl > 1)
			m_Game->logMsg(LOGTYPE_DEBUG, "LuaManager: Scope %s", name);
	}

	//========================================================
	//Check Lua struct
	//========================================================

	template<typename T>
	static T* LuaCheckStruct(lua_State* state, int index)
	{
		LuaStructType* type = GetRegisteredStructType<T>();

		if (!type || !type->get)
		{
			luaL_error(state, "C++ type is not registered as a Lua struct");
			return nullptr;
		}

		return static_cast<T*>(type->get(state, index));
	}

	//========================================================
	//Call Lua function
	//========================================================

	bool CallLuaFunction(const char* name)
	{
		if (!m_State || !m_Initialized)
			return false;

		lua_getglobal(m_State, name);
		if (!lua_isfunction( m_State, -1))
		{
			lua_pop(m_State, 1);
			return false;
		}

		if (lua_pcall(m_State, 0, 0, 0) != LUA_OK)
		{
			const char* error = lua_tostring(m_State, -1);

			Game::logMsg(LOGTYPE_ERROR, "LuaManager: %s error: %s", name, error ? error : "Unknown error");

			lua_pop(m_State, 1);
			return false;
		}

		return true;
	}
#pragma endregion

	LuaManager(Game* game);
	~LuaManager();

	void Initialize();
	void Shutdown();

	bool ExecuteFile(const char* filename);
	void ReloadFiles();

	//Mapped functions

	void Lua_PreUpdate();
	void Lua_TextureMapping();
	void Lua_CreateControllerBindings();

private:
	static int Lua_Log(lua_State* state);
	static int Lua_GetPanel(lua_State* state);
	static int Lua_FindParentOf(lua_State* state);
	static int Lua_SetBinding(lua_State* state);
	static int Lua_SetTurnBinding(lua_State* state);
	static int Lua_SetWalkBinding(lua_State* state);
	static int Lua_SendButton(lua_State* state);
	static int Lua_IsInGame(lua_State* state);
};