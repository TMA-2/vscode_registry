#include <windows.h>
#include <string>
#include <iostream>
#include <iomanip>
#include <fstream>
#include <codecvt>
#include <io.h>
#include <fcntl.h>
#include <cctype>

using std::endl;
static auto& out = std::wcout;


//-----------------------------------------------------------------------------
//	helpers
//-----------------------------------------------------------------------------

template<typename T, int N> auto num_elements(T (&)[N]) { return N; }

auto trim(const std::wstring &s) {
	int	a = 0, b = s.length();
	while (isspace(s[a]))
		a++;
	while (b > a && isspace(s[b - 1]))
		--b;
	return s.substr(a, b);
}

auto startsWith(const std::wstring &a, const wchar_t *b) {
	auto n = wcslen(b);
	return wcsncmp(&a[0], b, n);
}

auto endWith(const std::wstring &a, const wchar_t *b) {
	auto n = wcslen(b);
	return wcsncmp(&*a.end() - n, b, n);
}

auto unescape(const wchar_t *s, wchar_t *dest, char separator = 0) {
	auto p = dest;
	while (auto c = *s++) {
		if (c == '\\' && s[0]) {
			switch (c = *s++) {
				case '\\': break;
				case '0': c = '\0'; break;
				case 'n': c = '\n'; break;
				case 'r': c = '\r'; break;
				case 't': c = '\t'; break;
				default: *p++ = '\\'; break;
			}
		}
		if (c == separator)
			c = 0;
		*p++ = c;
	}
	*p = 0;
	return p - dest;
}

auto escape(const wchar_t *s, size_t size, wchar_t *dest, char separator = 0) {
	auto p = dest;
	for (auto e = s + size; s < e; s++) {
		auto c = *s;
		switch (c) {
			case '\\': *p++ = '\\'; break;
			case '\0': *p++ = '\\'; c = '0'; break;
			case '\n': *p++ = '\\'; c = 'n'; break;
			case '\r': *p++ = '\\'; c = 'r'; break;
			case '\t': *p++ = '\\'; c = 't'; break;
			case '"': *p++ = '\\'; break;
			default:
				if (c == separator) {
					*p++ = '\\';
					c = '0';
				}
				break;
		}
		*p++ = c;
	}
	*p = 0;
	return p - dest;
}

const char *hex = "0123456789abcdef";

auto hexchar(wchar_t c) {
	return c >= '0' && c <= '9' ? c - '0'
		: c >= 'A' && c <= 'F' ? c - 'A' + 10
		: c >= 'a' && c <= 'f' ? c - 'a' + 10
		: -1;
}

//-----------------------------------------------------------------------------
//	registry stuff
//-----------------------------------------------------------------------------

#define MAX_KEY_LENGTH 255
#define MAX_VALUE_NAME 16383

enum class OP : uint8_t {
	QUERY,
	ADD,
	DEL,
	EXPORT,
	IMPORT
	/*, COPY, SAVE, RESTORE, LOAD, UNLOAD, COMPARE, FLAGS*/,
	NUM
};
static const wchar_t* ops[] = {
	L"QUERY",
	L"ADD",
	L"DELETE",
	L"EXPORT",
	L"IMPORT",
//	L"COPY",
//	L"SAVE",
//	L"RESTORE",
//	L"LOAD",
//	L"UNLOAD",
//	L"COMPARE",
//	L"FLAGS"
};
OP get_op(const wchar_t *op) {
	for (auto& i : ops) {
		if (_wcsicmp(op, i) == 0) {
			return OP(&i - &ops[0]);
		}
	}
	return OP::NUM;
}

enum class TYPE : uint8_t {
	NONE,
	SZ,
	EXPAND_SZ,
	BINARY,
	DWORD,
	DWORD_BIG_ENDIAN,
	LINK,
	MULTI_SZ,
	RESOURCE_LIST,
	FULL_RESOURCE_DESCRIPTOR,
	RESOURCE_REQUIREMENTS_LIST,
	QWORD,
	NUM,
};
static const wchar_t* types[] = {
	L"REG_NONE",
	L"REG_SZ",
	L"REG_EXPAND_SZ",
	L"REG_BINARY",
	L"REG_DWORD",	// aka REG_DWORD_LITTLE_ENDIAN
	L"REG_DWORD_BIG_ENDIAN",
	L"REG_LINK",
	L"REG_MULTI_SZ",
	L"REG_RESOURCE_LIST",
	L"REG_FULL_RESOURCE_DESCRIPTOR",
	L"REG_RESOURCE_REQUIREMENTS_LIST",
	L"REG_QWORD",	// aka REG_QWORD_LITTLE_ENDIAN
};
TYPE get_type(const wchar_t *type) {
	if (!type)
		return TYPE::SZ;
	for (auto &t : types) {
		if (wcscmp(type, t) == 0)
			return TYPE(&t - types);
	}
	return TYPE::NUM;
}

enum class HIVE : uint8_t {
	HKCR,
	HKCU,
	HKLM,
	HKU,
	HKPD,
	HKCC,
	NUM,
};
const wchar_t *hives[][2] = {
	L"HKEY_CLASSES_ROOT",		L"HKCR",
	L"HKEY_CURRENT_USER",		L"HKCU",
	L"HKEY_LOCAL_MACHINE",		L"HKLM",
	L"HKEY_USERS",				L"HKU",
	L"HKEY_PERFORMANCE_DATA",	L"HKPD",
	L"HKEY_CURRENT_CONFIG",		L"HKCC",
};

auto toupper(std::wstring &&str) {
    for (auto &i : str)
        i = toupper(i);
	return str;
}

HIVE get_hive(const std::wstring &hive) {
	for (auto &i : hives) {
		if (hive == i[0] || hive == i[1])
			return (HIVE)(&i - hives);
	}
	return HIVE::NUM;
}

HKEY hive_to_hkey(HIVE hive) {
	return (HKEY)intptr_t(0x80000000 + (int)hive);
}

enum class OPT : uint8_t {
//string options
	key			= 0,
	value,
	file,
	type,
	data,
	separator,

//bool options
	all_subkeys	= 0,
	all_values,
	def_value,
	numeric_type,
	keys_only,
	data_only,
	case_sensitive,
	exact,
	force,
	view32,
	view64,

//flags
	alternative	= 1 << 6,

	end			= 0xff,
};
auto constexpr operator|(OPT a, OPT b) 	{ return OPT(uint8_t(a) | uint8_t(b)); }
auto constexpr operator&(OPT a, OPT b)	{ return bool(uint8_t(a) & uint8_t(b)); }

struct Option {
	OPT		opt;
	const wchar_t	*sw, *arg, *desc;
};

struct OPOptions {
	Option	*opts;
};

#define opt_end		{OPT::end,		nullptr, nullptr, nullptr}
#define opt_key		{OPT::key,		nullptr,	L"KeyName",	L"[\\\\Machine\\]FullKey\nMachine - Name of remote machine, omitting defaults to the current machine. Only HKLM and HKU are available on remote machines\nFullKey - in the form of ROOTKEY\\SubKey name\nROOTKEY - [ HKLM | HKCU | HKCR | HKU | HKCC ]\nSubKey  - The full name of a registry key under the selected ROOTKEY\n"}
#define opt_reg32	{OPT::view32,	L"reg:32",	nullptr,	L"Specifies the key should be accessed using the 32-bit registry view."}
#define opt_reg64	{OPT::view64|OPT::alternative,	L"reg:64",	nullptr,	L"Specifies the key should be accessed using the 64-bit registry view."}

static const OPOptions op_options[] = {
//QUERY,
{(Option[]){
	opt_key,
	{OPT::value,		L"v",     	L"ValueName",	L"Queries for a specific registry key values.\nIf omitted, all values for the key are queried.\nArgument to this switch can be optional only when specified along with /f switch. This specifies to search in valuenames only."},
	{OPT::def_value|OPT::alternative,	L"ve",    	nullptr,		L"Queries for the default value or empty value name (Default)."},
	{OPT::all_subkeys,	L"s",     	nullptr,		L"Queries all subkeys and values recursively (like dir /s)."},
	{OPT::data,			L"f",     	L"Data",		L"Specifies the data or pattern to search for.\nUse double quotes if a string contains spaces. Default is \"*\"."},
	{OPT::keys_only,	L"k",     	nullptr,		L"Specifies to search in key names only."},
	{OPT::data_only,	L"d",     	nullptr,		L"Specifies the search in data only."},
	{OPT::case_sensitive,L"c",     	nullptr,		L"Specifies that the search is case sensitive.\nThe default search is case insensitive."},
	{OPT::exact,		L"e",     	nullptr,		L"Specifies to return only exact matches.\nBy default all the matches are returned."},
	{OPT::type,			L"t",     	L"Type",		L"Specifies registry value data type.\nValid types are:\nREG_SZ, REG_MULTI_SZ, REG_EXPAND_SZ, REG_DWORD, REG_QWORD, REG_BINARY, REG_NONE\nDefaults to all types."},
	{OPT::numeric_type,	L"z",     	nullptr,		L"Verbose: Shows the numeric equivalent for the type of the valuename."},
	{OPT::separator,	L"se",    	L"Separator",	L"Specifies the separator (length of 1 character only) in data string for REG_MULTI_SZ. Defaults to \"\\0\" as the separator."},
	opt_reg32,
	opt_reg64,
	opt_end
}},
//ADD,
{(Option[]){
	opt_key,
	{OPT::value,		L"v",		L"ValueName",	L"The value name, under the selected Key, to add."},
	{OPT::def_value|OPT::alternative,	L"ve",    	nullptr,		L"adds an empty value name (Default) for the key."},
	{OPT::type,			L"t",     	L"Type",		L"RegKey data types\n[ REG_SZ | REG_MULTI_SZ | REG_EXPAND_SZ | REG_DWORD | REG_QWORD | REG_BINARY | REG_NONE ]\nIf omitted, REG_SZ is assumed."},
	{OPT::separator,	L"s",     	L"Separator",	L"Specify one character that you use as the separator in your data string for REG_MULTI_SZ. If omitted, use \"\\0\" as the separator."},
	{OPT::data,			L"d",     	L"Data",		L"The data to assign to the registry ValueName being added."},
	{OPT::force,		L"f",     	nullptr,		L"Force overwriting the existing registry entry without prompt."},
	opt_reg32,
	opt_reg64,
	opt_end
}},
//DEL,
{(Option[]){
	opt_key,
	{OPT::value,		L"v",		L"ValueName",	L"The value name, under the selected Key, to delete."},
	{OPT::def_value|OPT::alternative,	L"ve",    	nullptr,		L"delete the value of empty value name (Default)."},
	{OPT::all_values|OPT::alternative,	L"va",    	nullptr,		L"delete all values under this key."},
	{OPT::force,		L"f",     	nullptr,		L"Forces the deletion without prompt."},
	opt_reg32,
	opt_reg64,
	opt_end
}},
//EXPORT,
{(Option[]){
	opt_key,
	{OPT::file,			nullptr,	L"FileName",	L"The name of the disk file to export."},
	{OPT::force,		L"y",     	nullptr,		L"Force overwriting the existing file without prompt."},
	opt_reg32,
	opt_reg64,
	opt_end
}},
//IMPORT
{(Option[]){
	{OPT::file,			nullptr, 	L"FileName",	L"The name of the disk file to import (local machine only)."},
	opt_reg32,
	opt_reg64,
	opt_end
}},
};

wchar_t *get_options(Option *opts, int argc, wchar_t *argv[], wchar_t **string_args, uint32_t &bool_args) {
	auto arge = argv + argc;
	while (argv < arge) {
		auto a = *argv++;
		if (!opts->sw) {
			if (a[0] == '/')
				return a;
			string_args[(int)opts++->opt] = a;
			continue;
		}

		if (a[0] == '/') {
			bool found = false;
			for (auto o = opts; o->desc; ++o) {
				if (wcscmp(a + 1, o->sw) == 0) {
					if (o->arg)
						string_args[(int)o->opt] = *argv++;
					else
						bool_args |= 1 << (int)o->opt;
					found = true;
					break;
				}
			}
			if (!found)
				return a;
		}
	}
	return nullptr;
}

void write_command_data(BYTE *data, DWORD size, TYPE type, char separator) {
	switch (type) {
		case TYPE::SZ:
		case TYPE::EXPAND_SZ:
		case TYPE::MULTI_SZ: {
			auto p = (wchar_t*)malloc(size * 2);
			escape((const wchar_t*)data, size / 2 - 1, (wchar_t*)p, separator);
			out << p << endl; 
			free(p);
			break;
		}
		case TYPE::DWORD:
			out << "0x" << std::hex << *(DWORD*)data << std::dec << endl;
			break;

		case TYPE::DWORD_BIG_ENDIAN:
			out << "0x" << std::hex << _byteswap_ulong(*(DWORD*)data) << std::dec << endl;
			break;

		case TYPE::QWORD:
			out << "0x" << std::hex << *(uint64_t*)data << std::dec << endl;
			break;

		default:
			for (int i = 0; i < size; i++) {
				auto b = data[i];
				out << hex[b >> 4] << hex[b & 15];
			}
			out << endl;
			break;
	}
}

size_t parse_command_data(wchar_t *data, TYPE type, char separator) {
	switch (type) {
		case TYPE::NONE:
		case TYPE::SZ:
		case TYPE::EXPAND_SZ:
		case TYPE::MULTI_SZ:
			return unescape(data, data, separator);

		case TYPE::DWORD:
			*(DWORD*)data = wcstol(data, nullptr, 10);
			return 4;

		case TYPE::QWORD:
			*(uint64_t*)data = wcstoll(data, nullptr, 10);
			return 8;

		case TYPE::BINARY: {
			BYTE *d = (BYTE*)data;
			for (const wchar_t *p = data; ; p++) {
				auto d0 = hexchar(p[0]);
				auto d1 = hexchar(p[1]);
				if (d0 >= 0 && d1 >= 0)
					*d++ = (d0 << 4) | d1;
				p += 2;
			}
			return d - (BYTE*)data;
		}
		default:
			return 0;
	}
}

void write_reg_data(std::wostream &out, BYTE *data, DWORD size, TYPE type) {
	switch (type) {
		case TYPE::SZ: {
			auto p = (wchar_t*)malloc(size * 2);
			escape((const wchar_t*)data, size / 2 - 1, p);
			out << '"' << p << '"' << endl; 
			free(p);
			break;
		}
		case TYPE::DWORD:
			out << "dword:" << std::setfill(L'0') << std::setw(8) << std::hex << *(DWORD*)data << std::dec << endl;
			break;

		//case TYPE::QWORD:
		//	out << "qword:" << std::hex << *(uint64_t*)data << std::dec << endl;
		//	break;

		default:
			if (type == TYPE::BINARY)
				out << "hex:";
			else
				out << "hex(" << std::hex << (int)type << std::dec << "):";

			for (int i = 0; i < size; i++) {
				auto b = data[i];
				out << hex[b >> 4] << hex[b & 15];
				if (i != size - 1)
					out << ',';
			}
			out << endl;
			break;
	}
}

size_t parse_reg_data(const std::wstring &line, TYPE &type, BYTE *data) {
	if (line[0] == '"') {
		auto end = line.find_last_of('"');
		if (end >= 0) {
			type = TYPE::SZ;
			return unescape(line.substr(1, end - 1).c_str(), (wchar_t*)data) * 2 + 2;
		}

	} else if (startsWith(line, L"dword:")) {
		type = TYPE::DWORD;
		*((DWORD*)data) = wcstoul(&line[5], nullptr, 16);
		return 4;

	} else if (startsWith(line, L"qword:")) {
		type = TYPE::QWORD;
		*((uint64_t*)data) = wcstoull(&line[5], nullptr, 16);
		return 8;

	} else if (startsWith(line, L"hex")) {
		auto p = &line[3];
	 	type = TYPE::BINARY;

		if (p[0] == '(') {
			type = (TYPE)wcstoul(p + 1, nullptr, 16);
			while (*p && *p != ')')
				++p;
		}

		if (p[0] == ':')
			p++;

		auto d = data;
		for (;;) {
			auto d0 = hexchar(p[0]);
			auto d1 = hexchar(p[1]);
			if (d0 < 0 || d1 < 0)
				break;
			*d++ = (d0 << 4) | d1;
			p += 2;
			if (*p == ',')
				++p;
		}
		return d - data;
	}
	return 0;
}

//-----------------------------------------------------------------------------
//	RegKey
//-----------------------------------------------------------------------------

struct RegKey {
	struct Info {
		wchar_t	class_name[MAX_PATH] = L"";		// buffer for class name 
		DWORD	num_subkeys = 0;				// number of subkeys 
		DWORD	max_subkey	= 0;				// longest subkey size 
		DWORD	max_class 	= 0;				// longest class string 
		DWORD	num_values 	= 0;				// number of values for key 
		DWORD	max_value 	= 0;				// longest value name 
		DWORD	max_data 	= 0;				// longest value data 
		DWORD	cbSecurityDescriptor = 0; 		// size of security descriptor 
		FILETIME last_write;					// last write time 

		Info(HKEY h) {
			DWORD	class_size = MAX_PATH;		// size of class string 
			::RegQueryInfoKey(
				h,								// key handle 
				class_name,						// buffer for class name 
				&class_size,					// size of class string 
				NULL,							// reserved 
				&num_subkeys,					// number of subkeys 
				&max_subkey,					// longest subkey size 
				&max_class,						// longest class string 
				&num_values,					// number of values for this key 
				&max_value,						// longest value name 
				&max_data,						// longest value data 
				&cbSecurityDescriptor,			// security descriptor 
				&last_write			 			// last write time 
			);
		}
	};
	struct Value {
		std::wstring	name;
		TYPE	type	= TYPE::NONE;
		DWORD 	size	= 0;

		Value(const wchar_t *name = L"", TYPE type = TYPE::NONE, DWORD size = 0) : name(name), type(type), size(size) {}
		explicit constexpr operator bool() const { return size; }
	};


	HKEY h = nullptr;

	RegKey(HKEY h = nullptr)	: h(h) {}
	RegKey(RegKey &&b) 			: h(b.h) { b.h = nullptr; }
	RegKey(const wchar_t *k, REGSAM sam = KEY_READ) {
		auto	subkey	= wcschr(k, '\\');
		auto 	hive	= get_hive(subkey ? std::wstring(k, subkey - k) : k);
		auto 	ret = ::RegOpenKeyEx(hive_to_hkey(hive), subkey + !!subkey, 0, sam, &h);
		if (ret != ERROR_SUCCESS)
			h = nullptr;
	}
	RegKey(HKEY hParent, const wchar_t *subkey, REGSAM sam = KEY_READ) {
		auto ret = ::RegOpenKeyEx(hParent, subkey, 0, sam, &h);
		if (ret != ERROR_SUCCESS)
			h = nullptr;
	}
	~RegKey() { if (h) ::RegCloseKey(h); }

	RegKey& operator=(RegKey &&b) { std::swap(h, b.h); return *this; }

	explicit operator bool()	const { return h != nullptr; }
	auto info() 				const { return Info(h); }

	auto value(int i, BYTE *data, DWORD data_size) const {
		wchar_t	name[MAX_VALUE_NAME];
		DWORD 	name_size 	= MAX_VALUE_NAME;
		DWORD	type		= 0;
		auto 	ret		= ::RegEnumValue(h, i, name, &name_size, NULL, &type, data, &data_size);
		return ret == ERROR_SUCCESS
			? Value(name, (TYPE)type, data_size)
			: Value();
	}

	auto value(const wchar_t *name, BYTE *data, DWORD data_size) const {
		DWORD	type		= 0;
		auto 	ret		= ::RegQueryValueEx(h, name, 0, &type, data, &data_size);
		return ret == ERROR_SUCCESS
			? Value(name, (TYPE)type, data_size)
			: Value();
	}

	auto subkey(int i) const {
		wchar_t	name[MAX_KEY_LENGTH];
		DWORD 	name_size	= MAX_KEY_LENGTH;
		auto 	ret			= ::RegEnumKeyEx(h, i,
			name, &name_size, NULL,
			NULL, NULL,	//class
			NULL//&ftLastWriteTime
		);
		return ret == ERROR_SUCCESS ? std::wstring(name) : std::wstring();
	}

	auto set_value(const wchar_t *name, TYPE type, BYTE *data, DWORD size) {
		return ::RegSetValueEx(h, name, 0, (int)type, data, size);
	}

	auto remove_value(const wchar_t *name) {
		return ::RegDeleteValue(h, name);
	}
};

//-----------------------------------------------------------------------------
//	Reg
//-----------------------------------------------------------------------------

struct ParsedKey {
	std::wstring	host;
	HIVE			hive;
	const wchar_t	*subkey;

	ParsedKey(const wchar_t *k) {
		if (k[0] == '\\' && k[1] == '\\') {
			auto k0 = k + 2;
			k = wcschr(k + 2, '\\') + 1;
			host = std::wstring(k0, k - 1);
		}

		subkey	= wcschr(k, '\\');
		hive	= get_hive(toupper(subkey ? std::wstring(k, subkey - k) : k));
	}

	HKEY get_rootkey() {
		auto h = hive_to_hkey(hive);
		if (!host.empty()) {
			auto ret = RegConnectRegistry(host.c_str(), h, &h);
			if (ret != ERROR_SUCCESS)
				return nullptr;
		}
		return h;
	}
	RegKey get_key(REGSAM sam = KEY_READ) {
		auto h = get_rootkey();
		return h ? RegKey(h, subkey + !!subkey, sam) : RegKey();
	}

	auto get_keyname() {
		std::wstring	key = hives[(int)hive][0];
		return subkey ? key + subkey : key;
	}
};

struct Reg {
	union {
		wchar_t *string_args[6] = {nullptr};
		struct {
			wchar_t *key, *value, *file, *type, *data, *sep;
		};
	};

	union {
		uint32_t	bool_args;
		struct {
			bool all_subkeys 		: 1;
			bool all_values 		: 1;
			bool def_value 			: 1;
			bool numeric_type 		: 1;
			bool keys_only			: 1;
			bool data_only			: 1;
			bool case_sensitive		: 1;
			bool exact	 			: 1;
			bool force 	 			: 1;
			bool view32 			: 1;
			bool view64 			: 1;
		};
	};

	REGSAM	get_sam() const {
		REGSAM	sam = 0;
		if (view32)
			sam |= KEY_WOW64_32KEY;
		else if (view64)
			sam |= KEY_WOW64_32KEY;
		return sam;
	}

	int doQUERY();
	int doADD();
	int doDELETE();
	int doEXPORT();
	int doIMPORT();
//	int doCOPY()	{ return 0; }
//	int doSAVE()	{ return 0; }
//	int doRESTORE() { return 0; }
//	int doLOAD()	{ return 0; }
//	int doUNLOAD()	{ return 0; }
//	int doCOMPARE() { return 0; }
//	int doFLAGS()	{ return 0; }
};

int Reg::doQUERY() {
	ParsedKey	parsed(key);
	RegKey	r = parsed.get_key(KEY_READ | get_sam());
	if (!r)
		return ERROR_INVALID_FUNCTION;//"ERROR: The system was unable to find the specified registry key or value.";

	wchar_t separator = L'\0';
	if (sep) {
		if (unescape(sep, sep) != 1)
			return ERROR_INVALID_FUNCTION;//bad sep
		separator = sep[0];
	}

	auto info 		= r.info();
	auto keyname	= parsed.get_keyname();
	auto tab		= "	";
	auto data		= (BYTE*)malloc(info.max_data + 1);

	out << keyname << endl;

	// Enumerate the values
	for (int i = 0; i < info.num_values; i++) {
		if (auto value = r.value(i, data, info.max_data)) {
			out << tab;
			if (value.name.length())
				out << value.name;
			else
				out << "(Default)";
			out << tab << types[value.type < TYPE::NUM ? (int)value.type : 0];

			if (numeric_type)
				out << " (" << (int)value.type << ')';

			out << tab;
			write_command_data(data, value.size, value.type, separator);
		}
	}

	out << endl;

	// Enumerate the subkeys
	for (int i = 0; i < info.num_subkeys; i++) {
		auto name = r.subkey(i);
		if (name.length())
			out << keyname << '\\' << name << endl;
	}

	free(data);
	return 0;
}

int Reg::doADD() {
	ParsedKey	parsed(key);
	auto 		access = KEY_ALL_ACCESS | get_sam();
	HKEY		h;

	auto ret = RegCreateKeyEx(parsed.get_rootkey(), parsed.subkey + !!parsed.subkey, 0, NULL, REG_OPTION_NON_VOLATILE, access, NULL, &h, NULL);
	if (ret != ERROR_SUCCESS || (!value && !def_value))
		return ret;

	wchar_t separator = L'\0';
	if (sep) {
		if (unescape(sep, sep) != 1)
			return ERROR_INVALID_FUNCTION;//bad sep
		separator = sep[0];
	}

	TYPE	itype = get_type(type);
	if (itype == TYPE::NUM)
		return 1;

	DWORD	size = parse_command_data(data, itype, separator);
	RegKey	r(h);
	return r.set_value(value, itype, (BYTE*)data, size);
}

int Reg::doDELETE() {
	ParsedKey	parsed(key);
	auto 		access = KEY_ALL_ACCESS | get_sam();

	if (all_values) {
		RegKey	r		= parsed.get_key(access);
		auto 	info	= r.info();
		for (int i = 0; i < info.num_values; i++) {
			if (auto value = r.value(i, nullptr, 0)) {
				if (auto ret = r.remove_value(value.name.c_str()))
					return ret;
			}
		}
		return 0;

	} else if (def_value) {
		RegKey	r		= parsed.get_key(access);
		return r.remove_value(nullptr);

	} else if (value) {
		RegKey	r		= parsed.get_key(KEY_ALL_ACCESS);
		return r.remove_value(value);
		
	} else {
		return RegDeleteKeyEx(parsed.get_rootkey(), parsed.subkey + !!parsed.subkey, get_sam(), 0);
	}
}

auto& win_getline(std::wifstream &stream, std::wstring &line) {
	auto &result = getline(stream, line);
	if (line.back() == '\r')
		line.pop_back();
	return result;
}

int Reg::doIMPORT() {
	 std::wifstream stream(file);
	if (!stream) {
		std::cerr << "Failed to open file: " << file << std::endl;
		return ERROR_FILE_NOT_FOUND;
	}

	stream.imbue(std::locale(std::locale(), new std::codecvt_utf16<wchar_t, 0x10FFFF, std::consume_header>));

	std::wstring line, line2;
	if (!win_getline(stream, line) || line != L"Windows Registry Editor Version 5.00")
		return 1;

	RegKey	key;
	auto 	access = KEY_ALL_ACCESS | get_sam();

	// Parse key values and subkeys
	while (win_getline(stream, line)) {
		line = trim(line);
		if (!line.empty()) {
			while (line.back() == '\\' && win_getline(stream, line2))
				line += line2;

			if (line[0] == '[') {
				key = RegKey(line.substr(1, line.length() - 2).c_str(), access);
			} else {
				BYTE	data[1024];
				auto 	equals	= line.find_first_of('=');
				if (equals >= 0) {
					auto	name 	= trim(line.substr(0, equals));
					if (name[0] == '"' && name.back() == '"')
						name = name.substr(1, name.length() - 2);
					TYPE	type;
					auto	size	= parse_reg_data(trim(line.substr(equals + 1)), type, data);
					if (auto r = key.set_value(name.c_str(), type, data, size))
						return r;
				}
			}
		}
	}

	return 0;
}

void export_recurse(std::wostream &out, const RegKey &key, std::wstring keyname) {
	out << '[' << keyname << ']' << endl;

	auto info 		= key.info();

	// Enumerate the values
	auto data		= (BYTE*)malloc(info.max_data + 1);
	for (int i = 0; i < info.num_values; i++) {
		if (auto value = key.value(i, data, info.max_data)) {
			if (value.name.length())
				out << '"' << value.name << '"';
			else
				out << '@';
			out << '=';

			write_reg_data(out, data, value.size, value.type);
		}
	}
	free(data);

	out << endl;

	// Enumerate the subkeys
	for (int i = 0; i < info.num_subkeys; i++) {
		auto name = key.subkey(i);
		if (name.length())
			export_recurse(out, RegKey(key.h, name.c_str()), keyname + L'\\' + name);
	}

}

int Reg::doEXPORT() {
	 std::wofstream stream(file);
	if (!stream) {
		std::cerr << "Failed to create file: " << file << std::endl;
		return 1;
	}

    stream.imbue(std::locale(std::locale(), new std::codecvt_utf8<wchar_t>));

	stream << "Windows Registry Editor Version 5.00" << endl << endl;

	ParsedKey	parsed(key);
	RegKey		key	= parsed.get_key(KEY_READ | get_sam());
	if (!key)
		return 1;//"ERROR: The system was unable to find the specified registry key or value.";

	export_recurse(stream, key, parsed.get_keyname());

	return 0;
}

//-----------------------------------------------------------------------------
//	main
//-----------------------------------------------------------------------------


void print_options(OP op) {
	out << "REG " << ops[(uint8_t)op];
	bool	optional = false;
	for (auto opt = op_options[(uint8_t)op].opts; opt->desc; ++opt) {
		if ((opt->opt & OPT::alternative)) {
			out << " | ";
		} else {
			if (optional)
				out << ']';
			out << ' ';
			optional = !!opt->sw;
			if (optional)
				out << '[';
		}

		if (opt->sw) {
			out << '/' << opt->sw;
			if (opt->arg)
				out << ' ';
		}
		if (opt->arg)
			out << opt->arg;
	}
	if (optional)
		out << ']';
	out << endl;

	for (auto opt = op_options[(uint8_t)op].opts; opt->desc; ++opt) {
		out << "  ";
		if (opt->sw) {
			out << '/' << opt->sw;
		} else if (opt->arg) {
			out << opt->arg;
		}
		auto desc = opt->desc;
		while (auto p = wcschr(desc, '\n')) {
			out << '\t' << std::wstring(desc, p + 1);
			desc = p + 1;
		}
		out << '\t' << desc << endl;
	}
}

int wmain(int argc, wchar_t* argv[]) {
	_setmode(_fileno(stdout), _O_U8TEXT);

#if 0
	bool forever = true;
	while (forever) {
		out << "waiting for attach..." << endl;
		Sleep(1000);
	}
#endif

	if (argc < 2) {
		out << "** NOTE: this is an unofficial replacement for REG **" << endl << endl
			<< "REG Operation [Parameter List]" << endl << endl
			<< "Operation  [ QUERY | ADD | DELETE | EXPORT | IMPORT]" << endl << endl
			<< "Returns WINERROR code (e.g ERROR_SUCCESS = 0 on sucess)" << endl << endl
			<< "For help on a specific operation type:" << endl << endl
			<< "REG Operation /?" << endl << endl;
		return 0;
	}

	OP op = get_op(argv[1]);
	if (op == OP::NUM) {
		out << "Unknown operation: " << argv[1] << endl;
		return ERROR_INVALID_FUNCTION;
	}

	if (wcscmp(argv[2], L"/?") == 0) {
		print_options(op);
		return 0;
	}

	Reg reg;
	auto err = get_options(op_options[(uint8_t)op].opts, argc - 2, argv + 2, reg.string_args, reg.bool_args);
	if (err) {
		out << "Unknown option: " << err << endl;
		return ERROR_INVALID_FUNCTION;
	}

	int r = 0;
	switch (op) {
		case OP::QUERY: 	r = reg.doQUERY(); 	break;
		case OP::ADD: 		r = reg.doADD();	break;
		case OP::DEL: 		r = reg.doDELETE();	break;
		case OP::EXPORT: 	r = reg.doEXPORT(); break;
		case OP::IMPORT: 	r = reg.doIMPORT(); break;
	//	case OP::COPY: 		r = reg.doCOPY();	break;
	//	case OP::SAVE: 		r = reg.doSAVE();	break;
	//	case OP::RESTORE: 	r = reg.doRESTORE();break;
	//	case OP::LOAD: 		r = reg.doLOAD();	break;
	//	case OP::UNLOAD: 	r = reg.doUNLOAD(); break;
	//	case OP::COMPARE: 	r = reg.doCOMPARE();break;
	//	case OP::FLAGS: 	r = reg.doFLAGS();	break;
		default: break;
	}
	switch (r) {
		case ERROR_SUCCESS:
			break;
		case ERROR_FILE_NOT_FOUND:
			out << "ERROR: File not found" << endl;
			break;
		case ERROR_ACCESS_DENIED:
			out << "ERROR: Access denied" << endl;
			break;
		default:
			out << "ERROR: " << r << endl;
			break;
	}
	return r;
}
