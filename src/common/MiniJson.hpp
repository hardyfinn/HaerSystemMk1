// A very small JSON writer and reader in plain C++, for the cores that must
// save and load without Rack's JSON library (so the unit tests can check it).
// It reads any valid JSON; callers pick out the keys they know and skip the rest.
#pragma once
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace codex {
namespace minijson {

inline void putString(std::string& out, const std::string& s) {
	out += '"';
	for (unsigned char c : s) {
		if (c == '"' || c == '\\') {
			out += '\\';
			out += (char)c;
		}
		else if (c < 0x20) {
			char buf[8];
			std::snprintf(buf, sizeof(buf), "\\u%04x", c);
			out += buf;
		}
		else
			out += (char)c;
	}
	out += '"';
}

inline void putNumber(std::string& out, double v, int digits) {
	char buf[40];
	std::snprintf(buf, sizeof(buf), "%.*g", digits, v);
	out += buf;
}

// A reader for the JSON this file writes (and for the same data re-written by another JSON library).
struct Reader {
	const char* p;
	const char* end;
	bool ok = true;

	explicit Reader(const std::string& text) : p(text.data()), end(text.data() + text.size()) {}

	void space() {
		while (p < end && (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t'))
			p++;
	}
	bool eat(char c) {
		space();
		if (p < end && *p == c) {
			p++;
			return true;
		}
		return false;
	}
	bool need(char c) {
		if (!eat(c))
			ok = false;
		return ok;
	}
	double number() {
		space();
		char* stop = nullptr;
		double v = std::strtod(p, &stop);
		if (stop == p || stop > end)
			ok = false;
		else
			p = stop;
		return v;
	}
	std::string string() {
		std::string s;
		if (!need('"'))
			return s;
		while (p < end && *p != '"') {
			char c = *p++;
			if (c != '\\') {
				s += c;
				continue;
			}
			if (p >= end)
				break;
			char e = *p++;
			switch (e) {
				case 'n': s += '\n'; break;
				case 't': s += '\t'; break;
				case 'r': s += '\r'; break;
				case 'b': s += '\b'; break;
				case 'f': s += '\f'; break;
				case 'u': {
					if (end - p < 4) {
						ok = false;
						return s;
					}
					char hex[5] = {p[0], p[1], p[2], p[3], 0};
					unsigned long cp = std::strtoul(hex, nullptr, 16);
					p += 4;
					if (cp < 0x80)
						s += (char)cp;
					else if (cp < 0x800) {
						s += (char)(0xC0 | (cp >> 6));
						s += (char)(0x80 | (cp & 0x3F));
					}
					else {
						s += (char)(0xE0 | (cp >> 12));
						s += (char)(0x80 | ((cp >> 6) & 0x3F));
						s += (char)(0x80 | (cp & 0x3F));
					}
					break;
				}
				default: s += e; break; // \" \\ \/
			}
		}
		if (p >= end)
			ok = false;
		else
			p++; // closing quote
		return s;
	}
	// the text of any value, exactly as written (for JSON that belongs to someone else)
	std::string raw() {
		space();
		const char* from = p;
		skip();
		return ok ? std::string(from, (size_t)(p - from)) : std::string();
	}
	bool boolean() {
		space();
		if (end - p >= 4 && !std::strncmp(p, "true", 4)) {
			p += 4;
			return true;
		}
		if (end - p >= 5 && !std::strncmp(p, "false", 5)) {
			p += 5;
			return false;
		}
		return number() != 0.0;
	}
	// step over any value
	void skip() {
		space();
		if (p >= end) {
			ok = false;
			return;
		}
		if (*p == '"')
			string();
		else if (*p == '{' || *p == '[') {
			char open = *p, close = open == '{' ? '}' : ']';
			p++;
			if (eat(close))
				return;
			do {
				if (open == '{') {
					string();
					need(':');
				}
				skip();
			} while (ok && eat(','));
			need(close);
		}
		else if (!std::strncmp(p, "true", 4) && end - p >= 4)
			p += 4;
		else if (!std::strncmp(p, "false", 5) && end - p >= 5)
			p += 5;
		else if (!std::strncmp(p, "null", 4) && end - p >= 4)
			p += 4;
		else
			number();
	}
};


} // namespace minijson
} // namespace codex
