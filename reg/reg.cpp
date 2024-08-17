#include <stdio.h>
#include <string>
#include <windows.h>
#include <stdio.h>
#include <tchar.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <map>

using std::endl;
static auto& out = std::cout;

//-----------------------------------------------------------------------------
//	constants
//-----------------------------------------------------------------------------

#define MAX_KEY_LENGTH 255
#define MAX_VALUE_NAME 16383

enum OP { QUERY, ADD, DEL, EXPORT, IMPORT, COPY, SAVE, RESTORE, LOAD, UNLOAD, COMPARE, FLAGS, BAD };

static const char* ops[] = {
	"QUERY",
	"ADD",
	"DELETE",
	"EXPORT",
	"IMPORT",
//	"COPY",
//	"SAVE",
//	"RESTORE",
//	"LOAD",
//	"UNLOAD",
//	"COMPARE",
//	"FLAGS"
};

static const char* types[] = {
	"REG_NONE",
	"REG_SZ",
	"REG_EXPAND_SZ",
	"REG_BINARY",
	"REG_DWORD",
	//"REG_DWORD_LITTLE_ENDIAN",
	"REG_DWORD_BIG_ENDIAN",
	"REG_LINK",
	"REG_MULTI_SZ",
	"REG_RESOURCE_LIST",
	"REG_FULL_RESOURCE_DESCRIPTOR",
	"REG_RESOURCE_REQUIREMENTS_LIST",
	"REG_QWORD",
	//"REG_QWORD_LITTLE_ENDIAN",
};

const char *hives[][2] = {
	"HKEY_CLASSES_ROOT",	"HKCR",
	"HKEY_CURRENT_USER",	"HKCU",
	"HKEY_LOCAL_MACHINE",	"HKLM",
	"HKEY_USERS",			"HKU",
	"HKEY_PERFORMANCE_DATA","HKPD",
	"HKEY_CURRENT_CONFIG",	"HKCC",
};

//-----------------------------------------------------------------------------
//	helpers
//-----------------------------------------------------------------------------

template<typename T, int N> auto num_elements(T (&)[N]) { return N; }

HKEY get_hive(const std::string &hive) {
	for (auto &i : hives) {
		if (hive == i[0] || hive == i[1])
			return (HKEY)((void*)(0x80000000 + (&i - hives)));
	}
	return 0;
}

auto unescape(const char *s, char *dest, char separator = 0) {
	auto p = dest;
	while (char c = *s++) {
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

auto escape(const char *s, size_t size, char *dest, char separator = 0) {
	auto p = dest;
	for (const char *e = s + size; s < e; s++) {
		char c = *s;
		switch (c) {
			case '\\': *p++ = '\\'; break;
			case '\0': *p++ = '\\'; c = '0'; break;
			case '\n': *p++ = '\\'; c = 'n'; break;
			case '\r': *p++ = '\\'; c = 'r'; break;
			case '\t': *p++ = '\\'; c = 't'; break;
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

auto hexchar(char c) {
	return c >= '0' && c <= '9' ? c - '0'
		: c >= 'A' && c <= 'F' ? c - 'A' + 10
		: c >= 'a' && c <= 'f' ? c - 'a' + 10
		: -1;
}

void write_command_data(BYTE *data, DWORD size, DWORD type, char separator) {
	switch (type) {
		case REG_SZ:
		case REG_EXPAND_SZ:
		case REG_MULTI_SZ: {
			auto p = (char*)malloc(size * 2);
			escape((const char*)data, size - 1, (char*)p, separator);
			out << p << endl; 
			free(p);
			break;
		}
		case REG_DWORD:
			out << "0x" << std::hex << *(DWORD*)data << std::dec << endl;
			break;

		case REG_DWORD_BIG_ENDIAN:
			out << "0x" << std::hex << _byteswap_ulong(*(DWORD*)data) << std::dec << endl;
			break;

		case REG_QWORD:
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

size_t parse_command_data(char *data, DWORD type, char separator) {
	switch (type) {
		case REG_NONE:
		case REG_SZ:
		case REG_EXPAND_SZ:
		case REG_MULTI_SZ:
			return unescape(data, data, separator);

		case REG_DWORD:
			*(DWORD*)data = atol(data);
			return 4;

		case REG_QWORD:
			*(uint64_t*)data = atoll(data);
			return 8;

		case REG_BINARY: {
			BYTE *d = (BYTE*)data;
			for (const char *p = data; ; p++) {
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

void write_reg_data(std::ostream &out, BYTE *data, DWORD size, DWORD type) {
	switch (type) {
		case REG_SZ: {
			auto p = (char*)malloc(size * 2);
			escape((const char*)data, size - 1, (char*)p);
			out << '"' << p << '"' << endl; 
			free(p);
			break;
		}
		case REG_DWORD:
			out << "dword:" << std::hex << *(DWORD*)data << std::dec << endl;
			break;

		case REG_QWORD:
			out << "qword:" << std::hex << *(uint64_t*)data << std::dec << endl;
			break;

		default:
			if (type == REG_BINARY)
				out << "hex:";
			else
				out << "hex(" << std::hex << type << std::dec << "):";

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

size_t parse_reg_data(const char *line, DWORD &type, BYTE *data) {
	if (line[0] == '"') {
		type = REG_SZ;
		return unescape(line + 1, (char*)data) + 1;

	} else if (strncmp(line, "dword:", 5)) {
		type = REG_DWORD;
		*((DWORD*)data) = strtoul(line + 5, nullptr, 16);
		return 4;

	} else if (strncmp(line, "qword:", 5)) {
		type = REG_QWORD;
		*((uint64_t*)data) = strtoull(line + 5, nullptr, 16);
		return 8;

	} else if (strncmp(line, "hex", 3)) {
		line += 3;
	 	type = REG_BINARY;

		if (line[0] == '(') {
			type = strtoul(line + 1, nullptr, 16);
			while (*line && *line != ')')
				++line;
		}

		if (line[0] == ':')
			line++;

		auto d = data;
		for (const char *p = line; ; p++) {
			auto d0 = hexchar(p[0]);
			auto d1 = hexchar(p[1]);
			if (d0 >= 0 && d1 >= 0)
				*d++ = (d0 << 4) | d1;
			p += 2;
			if (*p == ',')
				++p;
		}
		return d - data;
	} else {
		return 0;
	}
}

//-----------------------------------------------------------------------------
//	RegKey
//-----------------------------------------------------------------------------

struct RegKey {
	struct Info {
		char	class_name[MAX_PATH] = "";		// buffer for class name 
		DWORD	num_subkeys = 0;				// number of subkeys 
		DWORD	max_subkey	= 0;				// longest subkey size 
		DWORD	max_class 	= 0;				// longest class string 
		DWORD	num_values 	= 0;				// number of values for key 
		DWORD	max_value 	= 0;				// longest value name 
		DWORD	max_data 	= 0;				// longest value data 
		DWORD	cbSecurityDescriptor = 0; 		// size of security descriptor 
		FILETIME ftLastWriteTime;				// last write time 

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
				&ftLastWriteTime	 			// last write time 
			);
		}
	};
	struct Value {
		std::string	name;
		DWORD	type	= 0;
		DWORD 	size	= 0;

		Value(const char *name = "", DWORD type = 0, DWORD size = 0) : name(name), type(type), size(size) {}
		explicit constexpr operator bool() const {
			return size;
		}
	};


	HKEY h = nullptr;

	RegKey() {}
	RegKey(RegKey &&b) : h(b.h) { b.h = nullptr; }
	RegKey(const char *k) {
		auto	subkey	= strchr(k, '\\');
		auto 	hive	= get_hive(subkey ? std::string(k, subkey - k) : k);
		auto 	ret = ::RegOpenKeyExA(hive, subkey, 0, KEY_READ, &h);
		if (ret != ERROR_SUCCESS)
			h = nullptr;
	}
	RegKey(HKEY hParent, const char *subkey) {
		auto ret = ::RegOpenKeyExA(hParent, subkey, 0, KEY_READ, &h);
		if (ret != ERROR_SUCCESS)
			h = nullptr;
	}
	~RegKey() { if (h) ::RegCloseKey(h); }

	RegKey& operator=(RegKey &&b) { std::swap(h, b.h); return *this; }

	explicit operator bool()	const { return h != nullptr; }
	auto info() 				const { return Info(h); }

	auto value(int i, BYTE *data, DWORD data_size) const {
		char	name[MAX_VALUE_NAME];
		DWORD 	name_size 	= MAX_VALUE_NAME;
		DWORD	type		= 0;
		auto 	ret		= ::RegEnumValue(h, i, name, &name_size, NULL, &type, data, &data_size);
		return ret == ERROR_SUCCESS
			? Value(name, type, data_size)
			: Value();
	}

	auto value(const char *name, BYTE *data, DWORD data_size) const {
		DWORD	type		= 0;
		auto 	ret		= ::RegQueryValueEx(h, name, 0, &type, data, &data_size);
		return ret == ERROR_SUCCESS
			? Value(name, type, data_size)
			: Value();
	}

	auto subkey(int i) const {
		char	name[MAX_KEY_LENGTH];
		DWORD 	name_size	= MAX_KEY_LENGTH;
		auto 	ret			= ::RegEnumKeyEx(h, i,
			name, &name_size, NULL,
			NULL, NULL,	//class
			NULL//&ftLastWriteTime
		);
		return ret == ERROR_SUCCESS ? std::string(name) : std::string();
	}

	auto set_value(const char *name, DWORD type, BYTE *data, DWORD size) {
		auto ret = ::RegSetValueEx(h, name, 0, type, data, size);
		return ret == ERROR_SUCCESS;
	}

	auto remove_value(const char *name) {
		auto ret = ::RegDeleteValue(h, name);
		return ret == ERROR_SUCCESS;
	}
};

//-----------------------------------------------------------------------------
//	Reg
//-----------------------------------------------------------------------------

struct Reg {
	HKEY 		h					= nullptr;
	const char *file				= nullptr;
	const char *subkey				= nullptr;

	const char* value 				= nullptr;
	int			type 				= REG_SZ;
	char* 		data 				= nullptr;
	char 		separator 			= 0;
	bool 		all_subkeys 		= false;
	bool 		all_values 			= false;
	bool 		numeric_type 		= false;
	bool 		search_keys_only	= false;
	bool 		search_data_only	= false;
	bool 		case_sensitive		= false;
	bool 		exact	 			= false;
	bool 		force 	 			= false;
	REGSAM		sam					= 0;

	int get_options(OP op, int argc, char *argv[]);

	void set_key(const char *k) {
		if (k[0] == '\\' && k[1] == '\\') {
			const char *host = k;
			k = strchr(k, '\\') + 1;
		}

		subkey	= strchr(k, '\\');
		h 		= get_hive(subkey ? std::string(k, subkey - k) : k);
	}

	auto get_keyname() {
		const char *hive = hives[(intptr_t)h - 0x80000000][0];
		std::string	key(hive);
		if (subkey)
			key = key + subkey;

		return key;
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


int Reg::get_options(OP op, int argc, char *argv[]) {
	for (int i = 2; i < argc; i++) {
		if (argv[i][0] == '/') {
			switch (argv[i][1]) {
				case 'r':
					if (strcmp(argv[i], "/reg:32") == 0)
						sam |= KEY_WOW64_32KEY;
					else if (strcmp(argv[i], "/reg:64") == 0)
						sam |= KEY_WOW64_32KEY;
					else
						return i;
					break;

				case 'v':
					switch (argv[i][2]) {
						case 'e':
							value = "";
							break;
						case 'a':
							all_values = true;
							break;
						case '\0':
							value = argv[++i];
							break;
						default:
							return i;
					}
					break;

				case 't': {
					auto name = argv[++i];
					for (auto &t : types) {
						if (strcmp(name, t) == 0) {
							type = &t - types;
							break;
						}
					}
					break;
				}
					
				case 'f':
					if (op == QUERY)
						data = argv[++i];
					else
						force = true;
					break;

				case 'd':
					if (op == ADD) {
						data = argv[++i];
					} else if (op == QUERY) {
						search_data_only = true;
					} else {
						return i;
					}
					break;

				case 's': 
					if (op != QUERY || argv[i][2] == 'e') {
						auto p = argv[++i];
						if (unescape(p, p) != 1)
							return i;
						separator = p[0];
					} else {
						all_subkeys = true;
					}
					break;

				case 'z':	numeric_type 		= true;	break;
				case 'k':	search_keys_only	= true; break;
				case 'c':	case_sensitive		= true; break;
				case 'e':	exact 				= true; break;

				default: return i;
			}
		}
	}
	return 0;
}


int Reg::doQUERY() {
	RegKey	r(h, subkey + !!subkey);
	if (!r)
		return 1;//"ERROR: The system was unable to find the specified registry key or value.";

	auto info 		= r.info();
	auto keyname	= get_keyname();
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
			out << tab << types[value.type < 12 ? value.type : 0];

			if (numeric_type)
				out << " (" << value.type << ')';

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
	auto sub = subkey + !!subkey;
	if (!value) {
		//add key
		auto ret = RegCreateKeyEx(h, sub, 0, NULL, REG_OPTION_NON_VOLATILE, KEY_ALL_ACCESS | sam, NULL, &h, NULL);
		return ret == ERROR_SUCCESS ? 0 : 1;
	}

	DWORD	size = parse_command_data(data, type, separator);
	if (size == 0)
		return 1;

	RegKey	r(h, sub);
	return r && r.set_value(value, type, (BYTE*)data, size) ? 0 : 1;
}

int Reg::doDELETE() {
	auto sub = subkey + !!subkey;
	if (!value) {
		//delete key
		auto ret = RegDeleteKeyEx(h, sub, sam, 0);
		return ret == ERROR_SUCCESS ? 0 : 1;
	}

	RegKey	r(h, sub);
	return r && r.remove_value(value) ? 0 : 1;
}

int Reg::doIMPORT() {
	 std::ifstream stream(file);
	if (!stream) {
		std::cerr << "Failed to open file: " << file << std::endl;
		return 1;
	}

	std::string line;
	std::getline(stream, line);
	if (line != "Windows Registry Editor Version 5.00")
		return 1;

	RegKey	key;

	// Parse key values and subkeys
	while (std::getline(stream, line)) {
		if (!line.empty()) {
			if (line[0] == '[') {
				key = RegKey(line.substr(1, line.length() - 2).c_str());
			} else {
				BYTE	data[1024];
				auto 	equals	= line.find_first_of('=');
				auto	name 	= line.substr(0, equals);
				DWORD	type;
				auto	size	= parse_reg_data(line.substr(equals + 1).c_str(), type, data);
				key.set_value(name.c_str(), type, data, size);
			}
		}
	}

	return 0;
}

void export_recurse(std::ostream &out, const RegKey &key, std::string keyname) {
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
			export_recurse(out, RegKey(key.h, name.c_str()), keyname + '\\' + name);
	}

}

int Reg::doEXPORT() {
	 std::ofstream stream(file);
	if (!stream) {
		std::cerr << "Failed to create file: " << file << std::endl;
		return 1;
	}

	stream << "Windows Registry Editor Version 5.00" << endl << endl;

	RegKey	key(h, subkey + !!subkey);
	if (!key)
		return 1;//"ERROR: The system was unable to find the specified registry key or value.";

	export_recurse(stream, key, get_keyname());

	return 0;
}

//-----------------------------------------------------------------------------
//	main
//-----------------------------------------------------------------------------

int main(int argc, char* argv[]) {
#if 0
	bool forever = true;
	while (forever) {
		out << "waiting for attach..." << endl;
		Sleep(1000);
	}
#endif

	if (argc < 2) {
		out << "Usage: " << argv[0] << "<options>" << endl;
		return 0;
	}

	OP op = BAD;

	for (auto& i : ops) {
		if (_stricmp(argv[1], i) == 0) {
			op = (OP)(&i - &ops[0]);
			break;
		}
	}

	if (op == BAD) {
		out << "Unknown operation: " << argv[1] << endl;
		return 0;
	}

	Reg reg;

	if (op == IMPORT) {
		reg.file = argv[2];
	} else {
		reg.set_key(argv[2]);
		if (op == EXPORT)
			reg.file = argv[3];
	}

	auto err = reg.get_options(op, argc, argv);
	if (err) {
		out << "Unknown option: " << argv[err] << endl;
		return 0;
	}

	int r = 0;
	switch (op) {
		case QUERY: 	r = reg.doQUERY(); 	break;
		case ADD: 		r = reg.doADD();	break;
		case DEL: 		r = reg.doDELETE();	break;
		case EXPORT: 	r = reg.doEXPORT(); break;
		case IMPORT: 	r = reg.doIMPORT(); break;
	//	case COPY: 		r = reg.doCOPY();	break;
	//	case SAVE: 		r = reg.doSAVE();	break;
	//	case RESTORE: 	r = reg.doRESTORE();break;
	//	case LOAD: 		r = reg.doLOAD();	break;
	//	case UNLOAD: 	r = reg.doUNLOAD(); break;
	//	case COMPARE: 	r = reg.doCOMPARE();break;
	//	case FLAGS: 	r = reg.doFLAGS();	break;
		default: break;
	}
	return r;
}
