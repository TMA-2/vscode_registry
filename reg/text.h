#pragma once
#include "base.h"

//-----------------------------------------------------------------------------
//	text
//-----------------------------------------------------------------------------

constexpr bool	is_whitespace(char c) 			{ return c <= ' '; }
constexpr bool	is_digit(char c)				{ return between(c, '0', '9'); }
constexpr bool	is_alpha(char c)				{ return between(c, 'A', 'Z') || between(c, 'a', 'z'); }
constexpr bool	is_alphanum(char c) 			{ return is_digit(c) || is_alpha(c); }
constexpr int	from_digit(char c)				{ return c <= '9' ? c - '0' : (c & 31) + 9; }
constexpr char	to_digit(int i, char ten = 'A') { return (i < 10 ? '0' : ten - 10) + i; }
constexpr char	to_lower(char c)				{ return between(c, 'A', 'Z') ? c + ('a' - 'A') : c; }
constexpr char	to_upper(char c)				{ return between(c, 'a', 'z') ? c - ('a' - 'A') : c; }

template<typename C> size_t string_length(const C* s) {
	auto i = s;
	if (i) {
		while (*i)
			++i;
	}
	return i - s;
}

template<typename C> size_t string_compare(const C* a, const C *b) {
	if (a && b) {
		while (*a && *a == *b)
			++a, ++b;
		return *a - *b;
	}
	return (a ? *a : 0) - (b ? *b : 0);
}

template<typename C> size_t string_compare(const C* a, const C *b, size_t blen) {
	while (blen && *a && *a == *b)
		++a, ++b, --blen;
	return blen ? *a - *b : *a;
}

template<typename C> size_t string_compare(const C* a, const C *b, size_t alen, size_t blen) {
	auto r = memcmp(a, b, min(alen, blen));
	return r ? r : alen - blen;
}

template<int B, typename C, typename T> inline C *put_digits(T t, C *d, identity_t<C> ten = 'A', int num_digits = -1) {
	while (num_digits--) {
		*--d = to_digit(t % B, ten);
		t /= B;
		if (!t && num_digits < 0)
			break;
	}
	return d;
}

template<typename T, typename R> T read_digits(R& r, int base = 10, int max_digits = -1) {
	T	val = 0;
	int c;
	while (max_digits-- && is_alphanum(c = r.peek())) {
		int d = from_digit(c);
		if (d >= base)
			break;
		val = val * base + d;
		r.read();
	}
	return val;
}

template<typename T, typename R> T read_prefixed_digits(R& r) {
	return !r.skip('0') ? read_digits<T>(r, 10)
				: r.skip('b') ? read_digits<T>(r, 2)
				: r.skip('x') ? read_digits<T>(r, 16)
				: read_digits<T>(r, 8);
}

//-----------------------------------------------------------------------------
// TextReader
//-----------------------------------------------------------------------------

template<typename T> constexpr bool equal(const T &a, const T &b)	{ return a == b; }
template<int N> bool equal(const char (&a)[N], const char (&b)[N])	{ return memcmp(a, b, N) == 0; }
#if 1
template<typename C> struct TextReader {
	struct Parser {
		TextReader* r;

		Parser(TextReader* r) : r(r) {}
		operator bool() { return !!r; }

		template<typename T> Parser operator>>(T& t)		{ return r && get(r->skip_whitespace(), t) ? r : nullptr; }
		template<typename T> Parser operator>=(T& t)		{ return r && get(*r, t) ? r : nullptr; }
		template<typename T> Parser operator>>(const T& t)	{ return r && r->skip_whitespace().skip(t) ? r : nullptr; }
		template<typename T> Parser operator>=(const T& t)	{ return r && r->skip(t) ? r : nullptr; }
		Parser	operator>>(const char* t) { return r && r->skip_whitespace().skip(t) ? r : nullptr; }
		Parser	operator>=(const char* t) { return r && r->skip(t) ? r : nullptr; }
	};

	const C *p, *end;

	TextReader(const C* p, size_t len)	: p(p), end(p + len) {}
	TextReader(const C* p)				: TextReader(p, string_length(p)) {}
	
	C 		peek()		const	{ return p < end ? *p : 0; }
	auto	available()	const	{ return size_t(end - p); }
	C 		read()				{ return p < end ? *p++ : 0; }

	range<const C*> to(const C *end)	{ return {exchange(p, end), end}; }
	range<const C*> remainder()	const	{ return {p, end}; }

	TextReader& move(int n) {
		p = min(p + n, end);
		return *this;
	}

	TextReader& skip_whitespace() {
		while (p < end && is_whitespace(*p))
			++p;
		return *this;
	}

	bool skip(C c) {
		bool ret = p < end && *p == c;
		p += ret;
		return ret;
	}
	bool skip(const C *t) {
		auto len = string_length(t);
		bool ok = available() >= len && memcmp(p, t, len) == 0;
		if (ok)
			move(len);
		return ok;
	}
	template<typename T> bool skip(const T &t) {
		T t2;
		return get(p, t2) && equal(t, t2);
	}
	template<typename T> Parser operator>>(T& t)	   { return get(skip_whitespace(), t) ? this : nullptr; }
	template<typename T> Parser operator>>(const T& t) { return skip(skip_whitespace(), t) this : nullptr; }
	template<typename T> Parser operator>=(T& t)	   { return get(r, t) ? this : nullptr; }
	template<typename T> Parser operator>=(const T& t) { return skip(r, t) ? this : nullptr; }
};
#else

template<typename C> struct CharReader {
	virtual ~CharReader() {}
	virtual int read()		= 0;
};

template<typename C> struct TextReader : CharReader<C> {
	struct Parser {
		TextReader* r;

		Parser(TextReader* r) : r(r) {}
		operator bool() { return !!r; }

		template<typename T> Parser operator>>(T& t)		{ return r && get(r->skip_whitespace(), t) ? r : nullptr; }
		template<typename T> Parser operator>=(T& t)		{ return r && get(*r, t) ? r : nullptr; }
		template<typename T> Parser operator>>(const T& t)	{ return r && r->skip_whitespace().skip(t) ? r : nullptr; }
		template<typename T> Parser operator>=(const T& t)	{ return r && r->skip(t) ? r : nullptr; }
		Parser	operator>>(const char* t) { return r && r->skip_whitespace().skip(t) ? r : nullptr; }
		Parser	operator>=(const char* t) { return r && r->skip(t) ? r : nullptr; }
	};

public:
	virtual size_t 	available() = 0;
	virtual int 	peek()		= 0;

	TextReader& skip_whitespace() {
		while (is_whitespace(peek()))
			read();
		return *this;
	}

	bool skip(C c) {
		bool ret = peek() == c;
		if (ret)
			read();
		return ret;
	}
	bool skip(const C* t) {
		while (char c = *t) {
			if (!skip(c))
				return false;
		}
		return true;
	}
	template<typename T> bool skip(const T& t) {
		T t2;
		return get(*this, t2) && equal(t, t2);
	}

	template<typename T> Parser operator>>(T& t)	   { return get(skip_whitespace(), t) ? this : nullptr; }
	template<typename T> Parser operator>>(const T& t) { return skip(skip_whitespace(), t) this : nullptr; }
	template<typename T> Parser operator>=(T& t)	   { return get(r, t) ? this : nullptr; }
	template<typename T> Parser operator>=(const T& t) { return skip(r, t) ? this : nullptr; }
};

#endif
//-----------------------------------------------------------------------------
// specific type getters
//-----------------------------------------------------------------------------

template<typename C, int N> bool get(TextReader<C>& p, C (&t)[N]) {
	if (p.available() >= N - 1) {
		for (int i = 0; i < N - 1; i++)
			t[i] = p.read();
		t[N - 1] = 0;
		return true;
	}
	return false;
}

template<typename C> inline bool get(TextReader<C>& p, C& t) {
	if (p.available()) {
		t = p.read();
		return true;
	}
	return false;
}

template<typename C> inline bool get(TextReader<C>& p, uint32_t& t) {
	if (is_digit(p.peek())) {
		t = read_prefixed_digits<uint32_t>(p);
		return true;
	}
	return false;
}

template<typename C> inline bool get(TextReader<C>& p, int& t) {
	bool neg = p.skip('-');
	if (is_digit(p.peek())) {
		auto u 	= read_prefixed_digits<uint32_t>(p);
		t		= neg ? -(int)u : (int)u;
		return true;
	}
	return false;
}

template<typename C, typename R, typename T> bool get_limited(TextReader<C>& p, T& t) {
	R	t2;
	if (get(p, t2) && (T)t2 == t2) {
		t = t2;
		return true;
	}
	return false;
}

template<typename C> inline bool get(TextReader<C>& p, uint8_t& t)	{ return get_limited<uint32_t>(p, t); }
template<typename C> inline bool get(TextReader<C>& p, uint16_t& t) { return get_limited<uint32_t>(p, t); }
template<typename C> inline bool get(TextReader<C>& p, int8_t& t)	{ return get_limited<int>(p, t); }
template<typename C> inline bool get(TextReader<C>& p, int16_t& t)	{ return get_limited<int>(p, t); }

//-----------------------------------------------------------------------------
// TextWriter
//-----------------------------------------------------------------------------

template<typename C> struct TextWriter {
	virtual size_t write(const C* buffer, size_t size) = 0;
	virtual void flush() {}

	template<typename T> TextWriter& operator<<(const T& t)	   { put(*this, t); return *this; }
};

//-----------------------------------------------------------------------------
// specific type putters
//-----------------------------------------------------------------------------

template<typename T> auto onlyif(bool b, const T& t) {
	return [b, &t](auto& p) {
		if (b)
			p << t;
	};
}
template<typename T, typename F> auto ifelse(bool b, const T& t, const F& f) {
	return [b, &t, &f](auto& p) {
		if (b)
			p << t;
		else
			p << f;
	};
}

void endl(TextWriter<wchar_t>& p) { p.write(L"\n", 1); p.flush(); };

template<typename C> inline		 	void put(TextWriter<C>& p, const _none&)		{}
template<typename C> inline		 	void put(TextWriter<C>& p, C t)					{ p.write(&t, 1); }
template<typename C> inline		 	void put(TextWriter<C>& p, const C *t)			{ p.write(t, string_length(t)); }
//template<typename C, int N> inline  void put(TextWriter<C>& p, const C (&t)[N])	{ p.write(t, N - 1); return p; }

template<typename C, typename T> inline enable_if_t<is_integral_v<T> && !is_signed_v<T>> put(TextWriter<C>& p, const T &t) {
	C	temp[(sizeof(T) * 5 + 1) / 2];
	C*	d = end(temp);
	T	i = t;
	do {
		*--d = to_digit(i % 10);
		i /= 10;
	} while (i);

	p.write(d, end(temp) - d);
}

template<typename C, typename T> inline enable_if_t<is_signed_v<T>> put(TextWriter<C>& p, const T &t) {
	p << onlyif(t < 0, '-') << int_t<sizeof(T) * 8, false>(abs(t));
}

template<typename C, typename F> exists_t<decltype(declval<F>()(declval<TextWriter<C>&>()))> put(TextWriter<C>& p, const F& f) {
	f(p);
}

template<typename C> inline void put(TextWriter<C> &p, const range<C*> &t)			{ p.write(t.begin(), t.size());	}
template<typename C> inline void put(TextWriter<C> &p, const range<const C*> &t)	{ p.write(t.begin(), t.size());	}

template<typename C> inline void put(TextWriter<C> &p, void *v)	{ p << L"0x" << base<16>(intptr_t(v));	}

