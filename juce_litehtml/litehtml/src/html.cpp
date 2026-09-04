#include "html.h"
#include "types.h"
#include "utf8_strings.h"

void litehtml::trim(tstring &s) 
{
	tstring::size_type pos = s.find_first_not_of(_t(" \n\r\t"));
	if(pos != tstring::npos)
	{
		s.erase(s.begin(), s.begin() + pos);
	}
	pos = s.find_last_not_of(_t(" \n\r\t"));
	if(pos != tstring::npos)
	{
		s.erase(s.begin() + pos + 1, s.end());
	}
}

void litehtml::lcase(tstring &s) 
{
	/* crust: indexed loop rather than a range-`for`. The range has to be a
	   named array with a written size or a container with `size()` and
	   `operator[]`; `s` is a reference parameter, which is neither. */
	for(tstring::size_type i = 0; i < s.length(); i++)
	{
		s[i] = t_tolower(s[i]);
	}
}

litehtml::tstring::size_type litehtml::find_close_bracket(const tstring &s, tstring::size_type off, tchar_t open_b, tchar_t close_b)
{
	int cnt = 0;
	for(tstring::size_type i = off; i < s.length(); i++)
	{
		if(s[i] == open_b)
		{
			cnt++;
		} else if(s[i] == close_b)
		{
			cnt--;
			if(!cnt)
			{
				return i;
			}
		}
	}
	return tstring::npos;
}

int litehtml::value_index( const tchar_t* val, const tchar_t* strings, int defValue, tchar_t delim )
{
	/* crust: bind pointer args to named strings once -- see split_string. */
	tstring val_s;
	if(val)
	{
		val_s = val;
	}
	tstring strings_s;
	if(strings)
	{
		strings_s = strings;
	}

	if(val_s.empty() || strings_s.empty() || !delim)
	{
		return defValue;
	}

	int idx = 0;
	tstring::size_type delim_start	= 0;
	tstring::size_type delim_end	= strings_s.find(delim, delim_start);
	tstring::size_type item_len;
	while(true)
	{
		if(delim_end == tstring::npos)
		{
			item_len = strings_s.length() - delim_start;
		} else
		{
			item_len = delim_end - delim_start;
		}
		if(item_len == val_s.length())
		{
			if(val_s == strings_s.substr(delim_start, item_len))
			{
				return idx;
			}
		}
		idx++;
		delim_start = delim_end;
		if(delim_start == tstring::npos) break;
		delim_start++;
		if(delim_start == strings_s.length()) break;
		delim_end = strings_s.find(delim, delim_start);
	}
	return defValue;
}

bool litehtml::value_in_list( const tchar_t* val, const tchar_t* strings, tchar_t delim )
{
	int idx = value_index(val, strings, -1, delim);
	if(idx >= 0)
	{
		return true;
	}
	return false;
}

void litehtml::split_string(const tchar_t* str, string_vector& tokens, const tchar_t* delims, const tchar_t* delims_preserve, const tchar_t* quote)
{
	/* crust: all string args are C pointers -- bind once. */
	tstring str_s;
	if(str)
	{
		str_s = str;
	}
	tstring delims_s = delims;
	tstring delims_preserve_s = delims_preserve;
	tstring quote_s = quote;

	if(str_s.empty() || (delims_s.empty() && delims_preserve_s.empty()))
	{
		return;
	}

	/* crust: string `operator+` is not in the subset; append instead. */
	tstring all_delims;
	all_delims.append(delims_s.c_str());
	all_delims.append(delims_preserve_s.c_str());
	all_delims.append(quote_s.c_str());

	tstring::size_type token_start	= 0;
	tstring::size_type token_end	= str_s.find_first_of(all_delims, token_start);
	tstring::size_type token_len;
	tstring token;
	while(true)
	{
		while( token_end != tstring::npos && quote_s.find_first_of(str_s[token_end]) != tstring::npos )
		{
			if(str_s[token_end] == _t('('))
			{
				token_end = find_close_bracket(str, token_end, _t('('), _t(')'));
			} else if(str_s[token_end] == _t('['))
			{
				token_end = find_close_bracket(str, token_end, _t('['), _t(']'));
			} else if(str_s[token_end] == _t('{'))
			{
				token_end = find_close_bracket(str, token_end, _t('{'), _t('}'));
			} else
			{
				token_end = str_s.find_first_of(str_s[token_end], token_end + 1);
			}
			if(token_end != tstring::npos)
			{
				token_end = str_s.find_first_of(all_delims, token_end + 1);
			}
		}

		if(token_end == tstring::npos)
		{
			token_len = tstring::npos;
		} else
		{
			token_len = token_end - token_start;
		}

		token = str_s.substr(token_start, token_len);
		if(!token.empty())
		{
			tokens.push_back( token );
		}
		if(token_end != tstring::npos && !delims_preserve_s.empty() && delims_preserve_s.find_first_of(str_s[token_end]) != tstring::npos)
		{
			/* crust: bind the call result before the reference parameter.
			   `push_back` takes `const T &`, and a call result has no
			   address. */
			tstring delim_tok = str_s.substr(token_end, 1);
			tokens.push_back( delim_tok );
		}

		token_start = token_end;
		if(token_start == tstring::npos) break;
		token_start++;
		if(token_start == str_s.length()) break;
		token_end = str_s.find_first_of(all_delims, token_start);
	}
}

void litehtml::join_string(tstring& str, const string_vector& tokens, const tchar_t* delims)
{
	/* crust: delims is a C string -- see split_string. */
	tstring delims_s = delims;
	tstringstream ss;
	for(size_t i=0; i<tokens.size(); ++i)
	{
		if(i != 0)
		{
			ss << delims_s;
		}
		ss << tokens[i];
	}

	str = ss.str();
}

int litehtml::t_strcasecmp(const litehtml::tchar_t *s1, const litehtml::tchar_t *s2)
{
	int i, d, c;

	for (i = 0;; i++)
	{
		c = t_tolower((unsigned char)s1[i]);
		d = c - t_tolower((unsigned char)s2[i]);
		if (d < 0)
			return -1;
		else if (d > 0)
			return 1;
		else if (c == 0)
			return 0;
	}
}

int litehtml::t_strncasecmp(const litehtml::tchar_t *s1, const litehtml::tchar_t *s2, size_t n)
{
	int i, d, c;

	for (i = 0; i < n; i++)
	{
		c = t_tolower((unsigned char)s1[i]);
		d = c - t_tolower((unsigned char)s2[i]);
		if (d < 0)
			return -1;
		else if (d > 0)
			return 1;
	}

	return 0;
}

void litehtml::document_container::split_text_parts(const char* text, string_vector& parts, std::vector<int>& kinds)
{
	std::wstring str;
	std::wstring str_in = (const wchar_t*)(utf8_to_wchar(text));
	ucode_t c;
	for (size_t i = 0; i < str_in.length(); i++)
	{
		c = (ucode_t)str_in[i];
		if (c <= ' ' && (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f'))
		{
			if (!str.empty())
			{
				/* crust: a named local -- litehtml_from_wchar builds a
				   wchar_to_utf8 by value, and a method call on a by-value
				   result has no address to take. */
				litehtml::wchar_to_utf8 piece(str.c_str());
				/* crust: bind c_str into a named string before push_back --
				   `const char*` has no address as a string reference. */
				tstring part = piece.c_str();
				parts.push_back(part);
				kinds.push_back(0);
				str.clear();
			}
			str += c;
			/* crust: a named local -- litehtml_from_wchar builds a
			   wchar_to_utf8 by value, and a method call on a by-value
			   result has no address to take. */
			litehtml::wchar_to_utf8 piece(str.c_str());
			/* crust: bind c_str into a named string before push_back --
			   `const char*` has no address as a string reference. */
			tstring part = piece.c_str();
			parts.push_back(part);
			kinds.push_back(1);
			str.clear();
		}
		// CJK character range
		else if (c >= 0x4E00 && c <= 0x9FCC)
		{
			if (!str.empty())
			{
				/* crust: a named local -- litehtml_from_wchar builds a
				   wchar_to_utf8 by value, and a method call on a by-value
				   result has no address to take. */
				litehtml::wchar_to_utf8 piece(str.c_str());
				/* crust: bind c_str into a named string before push_back --
				   `const char*` has no address as a string reference. */
				tstring part = piece.c_str();
				parts.push_back(part);
				kinds.push_back(0);
				str.clear();
			}
			str += c;
			/* crust: a named local -- litehtml_from_wchar builds a
			   wchar_to_utf8 by value, and a method call on a by-value
			   result has no address to take. */
			litehtml::wchar_to_utf8 piece(str.c_str());
			/* crust: bind c_str into a named string before push_back --
			   `const char*` has no address as a string reference. */
			tstring part = piece.c_str();
			parts.push_back(part);
			kinds.push_back(0);
			str.clear();
		}
		else
		{
			str += c;
		}
	}
	if (!str.empty())
	{
		/* crust: a named local -- litehtml_from_wchar builds a
		   wchar_to_utf8 by value, and a method call on a by-value
		   result has no address to take. */
		litehtml::wchar_to_utf8 piece(str.c_str());
		/* crust: bind c_str into a named string before push_back --
		   `const char*` has no address as a string reference. */
		tstring part = piece.c_str();
		parts.push_back(part);
		kinds.push_back(0);
	}
}

/* crust: kept so existing host code that calls the callback form still
   works; it is not called from inside litehtml any more. */
void litehtml::document_container::split_text(const char* text, const std::function<void(const tchar_t*)>& on_word, const std::function<void(const tchar_t*)>& on_space)
{
	string_vector parts;
	std::vector<int> kinds;
	split_text_parts(text, parts, kinds);
	for (int i = 0; i < (int)parts.size(); i++)
	{
		if (kinds[i] == 0)
			on_word(parts[i].c_str());
		else
			on_space(parts[i].c_str());
	}
}
