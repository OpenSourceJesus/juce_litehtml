#include "html.h"
#include "style.h"
#ifndef WINCE
#include <locale>
#endif

litehtml::string_map litehtml::style::m_valid_values =
{
	{ _t("white-space"), white_space_strings }
};

litehtml::style::style( const style& val )
{
	m_properties = val.m_properties;
}

void litehtml::style::parse( const tchar_t* txt, const tchar_t* baseurl, const element* el )
{
	std::vector<tstring> properties;
	split_string(txt, properties, _t(";"), _t(""), _t("\"'"));

	for(const tstring & property : properties)
	{
		parse_property(property, baseurl, el);
	}
}

void litehtml::style::parse_property( const tstring& txt, const tchar_t* baseurl, const element* el )
{
	tstring::size_type pos = txt.find_first_of(_t(':'));
	if(pos != tstring::npos)
	{
		tstring name = txt.substr(0, pos);
		tstring val	= txt.substr(pos + 1);

		trim(name); lcase(name);
		trim(val);

		if(!name.empty() && !val.empty())
		{
			string_vector vals;
			split_string(val.c_str(), vals, _t("!"));
			if(vals.size() == 1)
			{
				add_property(name.c_str(), val.c_str(), baseurl, false, el);
			} else if(vals.size() > 1)
			{
				trim(vals[0]);
				lcase(vals[1]);
				add_property(name.c_str(), vals[0].c_str(), baseurl, vals[1] == _t("important"), el);
			}
		}
	}
}

void litehtml::style::combine( const litehtml::style& src )
{
	/* crust: written type; pass C strings into add_parsed_property. */
	for(const props_map::value_type& property : src.m_properties)
	{
		add_parsed_property(property.first.c_str(), property.second.m_value.c_str(), property.second.m_important);
	}
}

bool litehtml::style::subst_vars( tstring& str, const element* el )
{
	if (!el) return true;

	// Guards against a variable that refers to itself.
	int guard = 0;

	while (1)
	{
		if (++guard > 32) return false;

		/* crust: written types rather than `auto` -- the deduction pass
		   reads types as they are spelled. */
		tstring::size_type start = str.find(_t("var("));
		if (start == tstring::npos) break;
		if (start > 0 && isalnum(str[start - 1])) break;

		// Find the ')' that closes this var(, not the first one in the
		// string: a fallback may itself contain parentheses, as in
		// var(--x, rgb(1, 2, 3)).
		tstring::size_type scan = start + 4;
		int depth = 1;
		tstring::size_type end = tstring::npos;

		while (scan < str.length())
		{
			if (str[scan] == _t('(')) depth++;
			else if (str[scan] == _t(')'))
			{
				depth--;
				if (!depth) { end = scan; break; }
			}
			scan++;
		}

		if (end == tstring::npos) return false;

		tstring inner = str.substr(start + 4, end - start - 4);

		// Split the name from its optional fallback at the first comma
		// that is not inside parentheses.
		tstring name = inner;
		tstring fallback;
		bool have_fallback = false;
		depth = 0;

		for (size_t i = 0; i < inner.length(); i++)
		{
			if (inner[i] == _t('(')) depth++;
			else if (inner[i] == _t(')')) depth--;
			else if (inner[i] == _t(',') && !depth)
			{
				name = inner.substr(0, i);
				fallback = inner.substr(i + 1);
				have_fallback = true;
				break;
			}
		}

		trim(name);
		trim(fallback);

		const tchar_t* val = el->get_style_property(name.c_str(), true);

		if (val)
		{
			str.replace(start, end - start + 1, val);
		}
		else if (have_fallback)
		{
			str.replace(start, end - start + 1, fallback);
		}
		else
		{
			// Undefined and no fallback. In CSS this makes the declaration
			// invalid at computed-value time, so the caller drops it and
			// the inherited or initial value applies.
			return false;
		}
	}

	return true;
}

void litehtml::style::add_property( const tchar_t* name, const tchar_t* _val, const tchar_t* baseurl, bool important, const element* el )
{
	if(!name || !_val)
	{
		return;
	}

	tstring val = _val;

	// A declaration whose var() cannot be resolved is dropped rather than
	// kept as literal text. Keeping it made every such colour parse as
	// opaque black, which on a page that sets both background and text
	// through variables produced black on black.
	if (!subst_vars(val, el))
	{
		return;
	}

	// Add baseurl for background image 
	if(	!t_strcmp(name, _t("background-image")))
	{
		add_parsed_property(name, val.c_str(), important);
		if(baseurl)
		{
			add_parsed_property(_t("background-image-baseurl"), baseurl, important);
		}
	} else

	// Parse border spacing properties 
	if(	!t_strcmp(name, _t("border-spacing")))
	{
		string_vector tokens;
		split_string(val.c_str(), tokens, _t(" "));
		if(tokens.size() == 1)
		{
			add_parsed_property(_t("-litehtml-border-spacing-x"), tokens[0].c_str(), important);
			add_parsed_property(_t("-litehtml-border-spacing-y"), tokens[0].c_str(), important);
		} else if(tokens.size() == 2)
		{
			add_parsed_property(_t("-litehtml-border-spacing-x"), tokens[0].c_str(), important);
			add_parsed_property(_t("-litehtml-border-spacing-y"), tokens[1].c_str(), important);
		}
	} else

	// Parse borders shorthand properties 

	if(	!t_strcmp(name, _t("border")))
	{
		string_vector tokens;
		split_string(val.c_str(), tokens, _t(" "), _t(""), _t("("));
		int idx;
		tstring str;
		for(const auto& token : tokens)
		{
			idx = value_index(token.c_str(), border_style_strings, -1);
			if(idx >= 0)
			{
				add_property(_t("border-left-style"), token.c_str(), baseurl, important, el);
				add_property(_t("border-right-style"), token.c_str(), baseurl, important, el);
				add_property(_t("border-top-style"), token.c_str(), baseurl, important, el);
				add_property(_t("border-bottom-style"), token.c_str(), baseurl, important, el);
			} else
			{
				if (t_isdigit(token[0]) || token[0] == _t('.') ||
					value_in_list(token.c_str(), _t("thin;medium;thick")))
				{
					add_property(_t("border-left-width"), token.c_str(), baseurl, important, el);
					add_property(_t("border-right-width"), token.c_str(), baseurl, important, el);
					add_property(_t("border-top-width"), token.c_str(), baseurl, important, el);
					add_property(_t("border-bottom-width"), token.c_str(), baseurl, important, el);
				} 
				else
				{
					add_property(_t("border-left-color"), token.c_str(), baseurl, important, el);
					add_property(_t("border-right-color"), token.c_str(), baseurl, important, el);
					add_property(_t("border-top-color"), token.c_str(), baseurl, important, el);
					add_property(_t("border-bottom-color"), token.c_str(), baseurl, important, el);
				}
			}
		}
	} else if(	!t_strcmp(name, _t("border-left"))	||
		!t_strcmp(name, _t("border-right"))	||
		!t_strcmp(name, _t("border-top"))	||
		!t_strcmp(name, _t("border-bottom")) )
	{
		string_vector tokens;
		split_string(val.c_str(), tokens, _t(" "), _t(""), _t("("));
		int idx;
		tstring str;
		for(const auto& token : tokens)
		{
			idx = value_index(token.c_str(), border_style_strings, -1);
			if(idx >= 0)
			{
				/* crust: `+=` needs a named string; append a C string. */
				str = name;
				str.append(_t("-style"));
				add_property(str.c_str(), token.c_str(), baseurl, important, el);
			} else
			{
				if(web_color::is_color(token.c_str()))
				{
					str = name;
					str.append(_t("-color"));
					add_property(str.c_str(), token.c_str(), baseurl, important, el);
				} else
				{
					str = name;
					str.append(_t("-width"));
					add_property(str.c_str(), token.c_str(), baseurl, important, el);
				}
			}
		}
	} else 

	// Parse border radius shorthand properties 
	if(!t_strcmp(name, _t("border-bottom-left-radius")))
	{
		string_vector tokens;
		split_string(val.c_str(), tokens, _t(" "));
		if(tokens.size() >= 2)
		{
			add_property(_t("border-bottom-left-radius-x"), tokens[0].c_str(), baseurl, important, el);
			add_property(_t("border-bottom-left-radius-y"), tokens[1].c_str(), baseurl, important, el);
		} else if(tokens.size() == 1)
		{
			add_property(_t("border-bottom-left-radius-x"), tokens[0].c_str(), baseurl, important, el);
			add_property(_t("border-bottom-left-radius-y"), tokens[0].c_str(), baseurl, important, el);
		}

	} else if(!t_strcmp(name, _t("border-bottom-right-radius")))
	{
		string_vector tokens;
		split_string(val.c_str(), tokens, _t(" "));
		if(tokens.size() >= 2)
		{
			add_property(_t("border-bottom-right-radius-x"), tokens[0].c_str(), baseurl, important, el);
			add_property(_t("border-bottom-right-radius-y"), tokens[1].c_str(), baseurl, important, el);
		} else if(tokens.size() == 1)
		{
			add_property(_t("border-bottom-right-radius-x"), tokens[0].c_str(), baseurl, important, el);
			add_property(_t("border-bottom-right-radius-y"), tokens[0].c_str(), baseurl, important, el);
		}

	} else if(!t_strcmp(name, _t("border-top-right-radius")))
	{
		string_vector tokens;
		split_string(val.c_str(), tokens, _t(" "));
		if(tokens.size() >= 2)
		{
			add_property(_t("border-top-right-radius-x"), tokens[0].c_str(), baseurl, important, el);
			add_property(_t("border-top-right-radius-y"), tokens[1].c_str(), baseurl, important, el);
		} else if(tokens.size() == 1)
		{
			add_property(_t("border-top-right-radius-x"), tokens[0].c_str(), baseurl, important, el);
			add_property(_t("border-top-right-radius-y"), tokens[0].c_str(), baseurl, important, el);
		}

	} else if(!t_strcmp(name, _t("border-top-left-radius")))
	{
		string_vector tokens;
		split_string(val.c_str(), tokens, _t(" "));
		if(tokens.size() >= 2)
		{
			add_property(_t("border-top-left-radius-x"), tokens[0].c_str(), baseurl, important, el);
			add_property(_t("border-top-left-radius-y"), tokens[1].c_str(), baseurl, important, el);
		} else if(tokens.size() == 1)
		{
			add_property(_t("border-top-left-radius-x"), tokens[0].c_str(), baseurl, important, el);
			add_property(_t("border-top-left-radius-y"), tokens[0].c_str(), baseurl, important, el);
		}

	} else 

	// Parse border-radius shorthand properties 
	if(!t_strcmp(name, _t("border-radius")))
	{
		string_vector tokens;
		split_string(val.c_str(), tokens, _t("/"));
		if(tokens.size() == 1)
		{
			add_property(_t("border-radius-x"), tokens[0].c_str(), baseurl, important, el);
			add_property(_t("border-radius-y"), tokens[0].c_str(), baseurl, important, el);
		} else if(tokens.size() >= 2)
		{
			add_property(_t("border-radius-x"), tokens[0].c_str(), baseurl, important, el);
			add_property(_t("border-radius-y"), tokens[1].c_str(), baseurl, important, el);
		}
	} else if(!t_strcmp(name, _t("border-radius-x")))
	{
		string_vector tokens;
		split_string(val.c_str(), tokens, _t(" "));
		if(tokens.size() == 1)
		{
			add_property(_t("border-top-left-radius-x"),		tokens[0].c_str(), baseurl, important, el);
			add_property(_t("border-top-right-radius-x"),		tokens[0].c_str(), baseurl, important, el);
			add_property(_t("border-bottom-right-radius-x"),	tokens[0].c_str(), baseurl, important, el);
			add_property(_t("border-bottom-left-radius-x"),	tokens[0].c_str(), baseurl, important, el);
		} else if(tokens.size() == 2)
		{
			add_property(_t("border-top-left-radius-x"),		tokens[0].c_str(), baseurl, important, el);
			add_property(_t("border-top-right-radius-x"),		tokens[1].c_str(), baseurl, important, el);
			add_property(_t("border-bottom-right-radius-x"),	tokens[0].c_str(), baseurl, important, el);
			add_property(_t("border-bottom-left-radius-x"),	tokens[1].c_str(), baseurl, important, el);
		} else if(tokens.size() == 3)
		{
			add_property(_t("border-top-left-radius-x"),		tokens[0].c_str(), baseurl, important, el);
			add_property(_t("border-top-right-radius-x"),		tokens[1].c_str(), baseurl, important, el);
			add_property(_t("border-bottom-right-radius-x"),	tokens[2].c_str(), baseurl, important, el);
			add_property(_t("border-bottom-left-radius-x"),	tokens[1].c_str(), baseurl, important, el);
		} else if(tokens.size() == 4)
		{
			add_property(_t("border-top-left-radius-x"),		tokens[0].c_str(), baseurl, important, el);
			add_property(_t("border-top-right-radius-x"),		tokens[1].c_str(), baseurl, important, el);
			add_property(_t("border-bottom-right-radius-x"),	tokens[2].c_str(), baseurl, important, el);
			add_property(_t("border-bottom-left-radius-x"),	tokens[3].c_str(), baseurl, important, el);
		}
	} else if(!t_strcmp(name, _t("border-radius-y")))
	{
		string_vector tokens;
		split_string(val.c_str(), tokens, _t(" "));
		if(tokens.size() == 1)
		{
			add_property(_t("border-top-left-radius-y"),		tokens[0].c_str(), baseurl, important, el);
			add_property(_t("border-top-right-radius-y"),		tokens[0].c_str(), baseurl, important, el);
			add_property(_t("border-bottom-right-radius-y"),	tokens[0].c_str(), baseurl, important, el);
			add_property(_t("border-bottom-left-radius-y"),	tokens[0].c_str(), baseurl, important, el);
		} else if(tokens.size() == 2)
		{
			add_property(_t("border-top-left-radius-y"),		tokens[0].c_str(), baseurl, important, el);
			add_property(_t("border-top-right-radius-y"),		tokens[1].c_str(), baseurl, important, el);
			add_property(_t("border-bottom-right-radius-y"),	tokens[0].c_str(), baseurl, important, el);
			add_property(_t("border-bottom-left-radius-y"),	tokens[1].c_str(), baseurl, important, el);
		} else if(tokens.size() == 3)
		{
			add_property(_t("border-top-left-radius-y"),		tokens[0].c_str(), baseurl, important, el);
			add_property(_t("border-top-right-radius-y"),		tokens[1].c_str(), baseurl, important, el);
			add_property(_t("border-bottom-right-radius-y"),	tokens[2].c_str(), baseurl, important, el);
			add_property(_t("border-bottom-left-radius-y"),	tokens[1].c_str(), baseurl, important, el);
		} else if(tokens.size() == 4)
		{
			add_property(_t("border-top-left-radius-y"),		tokens[0].c_str(), baseurl, important, el);
			add_property(_t("border-top-right-radius-y"),		tokens[1].c_str(), baseurl, important, el);
			add_property(_t("border-bottom-right-radius-y"),	tokens[2].c_str(), baseurl, important, el);
			add_property(_t("border-bottom-left-radius-y"),	tokens[3].c_str(), baseurl, important, el);
		}
	}
	else

	// Parse list-style shorthand properties 
	if(!t_strcmp(name, _t("list-style")))
	{
		add_parsed_property(_t("list-style-type"),			_t("disc"),		important);
		add_parsed_property(_t("list-style-position"),		_t("outside"),	important);
		add_parsed_property(_t("list-style-image"),			_t(""),			important);
		add_parsed_property(_t("list-style-image-baseurl"),	_t(""),			important);

		string_vector tokens;
		split_string(val.c_str(), tokens, _t(" "), _t(""), _t("("));
		for(const auto& token : tokens)
		{
			int idx = value_index(token.c_str(), list_style_type_strings, -1);
			if(idx >= 0)
			{
				add_parsed_property(_t("list-style-type"), token.c_str(), important);
			} else
			{
				idx = value_index(token.c_str(), list_style_position_strings, -1);
				if(idx >= 0)
				{
					add_parsed_property(_t("list-style-position"), token.c_str(), important);
				} else if(!t_strncmp(val.c_str(), _t("url"), 3))
				{
					add_parsed_property(_t("list-style-image"), token.c_str(), important);
					if(baseurl)
					{
						add_parsed_property(_t("list-style-image-baseurl"), baseurl, important);
					}
				}
			}
		}
	} else 

	// Add baseurl for background image 
	if(	!t_strcmp(name, _t("list-style-image")))
	{
		add_parsed_property(name, val.c_str(), important);
		if(baseurl)
		{
			add_parsed_property(_t("list-style-image-baseurl"), baseurl, important);
		}
	} else
		
	// Parse background shorthand properties 
	if(!t_strcmp(name, _t("background")))
	{
		parse_short_background(val.c_str(), baseurl, important);

	} else 
		
	// Parse margin and padding shorthand properties 
	if(!t_strcmp(name, _t("margin")) || !t_strcmp(name, _t("padding")))
	{
		/* crust: build side names with append; pass C strings. */
		string_vector tokens;
		split_string(val.c_str(), tokens, _t(" "));
		tstring p_top = name; p_top.append(_t("-top"));
		tstring p_right = name; p_right.append(_t("-right"));
		tstring p_bottom = name; p_bottom.append(_t("-bottom"));
		tstring p_left = name; p_left.append(_t("-left"));
		if(tokens.size() >= 4)
		{
			add_parsed_property(p_top.c_str(),		tokens[0].c_str(), important);
			add_parsed_property(p_right.c_str(),	tokens[1].c_str(), important);
			add_parsed_property(p_bottom.c_str(),	tokens[2].c_str(), important);
			add_parsed_property(p_left.c_str(),		tokens[3].c_str(), important);
		} else if(tokens.size() == 3)
		{
			add_parsed_property(p_top.c_str(),		tokens[0].c_str(), important);
			add_parsed_property(p_right.c_str(),	tokens[1].c_str(), important);
			add_parsed_property(p_left.c_str(),		tokens[1].c_str(), important);
			add_parsed_property(p_bottom.c_str(),	tokens[2].c_str(), important);
		} else if(tokens.size() == 2)
		{
			add_parsed_property(p_top.c_str(),		tokens[0].c_str(), important);
			add_parsed_property(p_bottom.c_str(),	tokens[0].c_str(), important);
			add_parsed_property(p_right.c_str(),	tokens[1].c_str(), important);
			add_parsed_property(p_left.c_str(),		tokens[1].c_str(), important);
		} else if(tokens.size() == 1)
		{
			add_parsed_property(p_top.c_str(),		tokens[0].c_str(), important);
			add_parsed_property(p_bottom.c_str(),	tokens[0].c_str(), important);
			add_parsed_property(p_right.c_str(),	tokens[0].c_str(), important);
			add_parsed_property(p_left.c_str(),		tokens[0].c_str(), important);
		}
	} else 
		
		
	// Parse border-* shorthand properties 
	if(	!t_strcmp(name, _t("border-left")) || 
		!t_strcmp(name, _t("border-right")) ||
		!t_strcmp(name, _t("border-top"))  || 
		!t_strcmp(name, _t("border-bottom")))
	{
		parse_short_border(name, val.c_str(), important);
	} else 
		
	// Parse border-width/style/color shorthand properties 
	if(	!t_strcmp(name, _t("border-width")) ||
		!t_strcmp(name, _t("border-style"))  ||
		!t_strcmp(name, _t("border-color")) )
	{
		string_vector nametokens;
		split_string(name, nametokens, _t("-"));

		string_vector tokens;
		split_string(val.c_str(), tokens, _t(" "));
		/* crust: build corner names with append; pass C strings. */
		if(tokens.size() >= 4)
		{
			tstring p0 = nametokens[0]; p0.append(_t("-top-")); p0.append(nametokens[1].c_str());
			tstring p1 = nametokens[0]; p1.append(_t("-right-")); p1.append(nametokens[1].c_str());
			tstring p2 = nametokens[0]; p2.append(_t("-bottom-")); p2.append(nametokens[1].c_str());
			tstring p3 = nametokens[0]; p3.append(_t("-left-")); p3.append(nametokens[1].c_str());
			add_parsed_property(p0.c_str(), tokens[0].c_str(), important);
			add_parsed_property(p1.c_str(), tokens[1].c_str(), important);
			add_parsed_property(p2.c_str(), tokens[2].c_str(), important);
			add_parsed_property(p3.c_str(), tokens[3].c_str(), important);
		} else if(tokens.size() == 3)
		{
			tstring p0 = nametokens[0]; p0.append(_t("-top-")); p0.append(nametokens[1].c_str());
			tstring p1 = nametokens[0]; p1.append(_t("-right-")); p1.append(nametokens[1].c_str());
			tstring p2 = nametokens[0]; p2.append(_t("-left-")); p2.append(nametokens[1].c_str());
			tstring p3 = nametokens[0]; p3.append(_t("-bottom-")); p3.append(nametokens[1].c_str());
			add_parsed_property(p0.c_str(), tokens[0].c_str(), important);
			add_parsed_property(p1.c_str(), tokens[1].c_str(), important);
			add_parsed_property(p2.c_str(), tokens[1].c_str(), important);
			add_parsed_property(p3.c_str(), tokens[2].c_str(), important);
		} else if(tokens.size() == 2)
		{
			tstring p0 = nametokens[0]; p0.append(_t("-top-")); p0.append(nametokens[1].c_str());
			tstring p1 = nametokens[0]; p1.append(_t("-bottom-")); p1.append(nametokens[1].c_str());
			tstring p2 = nametokens[0]; p2.append(_t("-right-")); p2.append(nametokens[1].c_str());
			tstring p3 = nametokens[0]; p3.append(_t("-left-")); p3.append(nametokens[1].c_str());
			add_parsed_property(p0.c_str(), tokens[0].c_str(), important);
			add_parsed_property(p1.c_str(), tokens[0].c_str(), important);
			add_parsed_property(p2.c_str(), tokens[1].c_str(), important);
			add_parsed_property(p3.c_str(), tokens[1].c_str(), important);
		} else if(tokens.size() == 1)
		{
			tstring p0 = nametokens[0]; p0.append(_t("-top-")); p0.append(nametokens[1].c_str());
			tstring p1 = nametokens[0]; p1.append(_t("-bottom-")); p1.append(nametokens[1].c_str());
			tstring p2 = nametokens[0]; p2.append(_t("-right-")); p2.append(nametokens[1].c_str());
			tstring p3 = nametokens[0]; p3.append(_t("-left-")); p3.append(nametokens[1].c_str());
			add_parsed_property(p0.c_str(), tokens[0].c_str(), important);
			add_parsed_property(p1.c_str(), tokens[0].c_str(), important);
			add_parsed_property(p2.c_str(), tokens[0].c_str(), important);
			add_parsed_property(p3.c_str(), tokens[0].c_str(), important);
		}
	} else
	{
		add_parsed_property(name, val.c_str(), important);
	}
}

void litehtml::style::parse_short_border( const tchar_t* prefix, const tchar_t* val, bool important )
{
	string_vector tokens;
	split_string(val, tokens, _t(" "), _t(""), _t("("));
	tstring prefix_s;
	if(prefix)
	{
		prefix_s = prefix;
	}
	if(tokens.size() >= 3)
	{
		/* crust: build property names with append -- operator+ is out. */
		tstring p_width = prefix_s; p_width.append(_t("-width"));
		tstring p_style = prefix_s; p_style.append(_t("-style"));
		tstring p_color = prefix_s; p_color.append(_t("-color"));
		add_parsed_property(p_width.c_str(),	tokens[0].c_str(), important);
		add_parsed_property(p_style.c_str(),	tokens[1].c_str(), important);
		add_parsed_property(p_color.c_str(),	tokens[2].c_str(), important);
	} else if(tokens.size() == 2)
	{
		if(iswdigit(tokens[0][0]) || value_index(val, border_width_strings) >= 0)
		{
			tstring p_width = prefix_s; p_width.append(_t("-width"));
			tstring p_style = prefix_s; p_style.append(_t("-style"));
			add_parsed_property(p_width.c_str(),	tokens[0].c_str(), important);
			add_parsed_property(p_style.c_str(),	tokens[1].c_str(), important);
		} else
		{
			tstring p_style = prefix_s; p_style.append(_t("-style"));
			tstring p_color = prefix_s; p_color.append(_t("-color"));
			add_parsed_property(p_style.c_str(),	tokens[0].c_str(), important);
			add_parsed_property(p_color.c_str(),	tokens[1].c_str(), important);
		}
	}
}

void litehtml::style::parse_short_background( const tchar_t* val, const tchar_t* baseurl, bool important )
{
	/* crust: bind for split_string's string-ref parameter. */
	tstring val_s;
	if(val)
	{
		val_s = val;
	}
	add_parsed_property(_t("background-color"),			_t("transparent"),	important);
	add_parsed_property(_t("background-image"),			_t(""),				important);
	add_parsed_property(_t("background-image-baseurl"), _t(""),				important);
	add_parsed_property(_t("background-repeat"),		_t("repeat"),		important);
	add_parsed_property(_t("background-origin"),		_t("padding-box"),	important);
	add_parsed_property(_t("background-clip"),			_t("border-box"),	important);
	add_parsed_property(_t("background-attachment"),	_t("scroll"),		important);

	if(val_s == _t("none"))
	{
		return;
	}

	string_vector tokens;
	split_string(val_s.c_str(), tokens, _t(" "), _t(""), _t("("));
	bool origin_found = false;
	for(const auto& token : tokens)
	{
		if(token.substr(0, 3) == _t("url"))
		{
			add_parsed_property(_t("background-image"), token.c_str(), important);
			if(baseurl)
			{
				add_parsed_property(_t("background-image-baseurl"), baseurl, important);
			}

		} else if( value_in_list(token.c_str(), background_repeat_strings) )
		{
			add_parsed_property(_t("background-repeat"), token.c_str(), important);
		} else if( value_in_list(token.c_str(), background_attachment_strings) )
		{
			add_parsed_property(_t("background-attachment"), token.c_str(), important);
		} else if( value_in_list(token.c_str(), background_box_strings) )
		{
			if(!origin_found)
			{
				add_parsed_property(_t("background-origin"), token.c_str(), important);
				origin_found = true;
			} else
			{
				add_parsed_property(_t("background-clip"), token.c_str(), important);
			}
		} else if(	value_in_list(token.c_str(), _t("left;right;top;bottom;center")) ||
					iswdigit(token[0]) ||
					token[0] == _t('-')	||
					token[0] == _t('.')	||
					token[0] == _t('+'))
		{
			/* crust: named key -- map find/[] take string refs; append in
			   place rather than copying an owning string. */
			tstring bgpos_key = _t("background-position");
			if(m_properties.find(bgpos_key) != m_properties.end())
			{
				m_properties[bgpos_key].m_value.append(_t(" "));
				m_properties[bgpos_key].m_value.append(token.c_str());
			} else
			{
				add_parsed_property(_t("background-position"), token.c_str(), important);
			}
		} else if (web_color::is_color(token.c_str()))
		{
			add_parsed_property(_t("background-color"), token.c_str(), important);
		}
	}
}

void litehtml::style::parse_short_font( const tchar_t* val, bool important )
{
	tstring val_s;
	if(val)
	{
		val_s = val;
	}
	add_parsed_property(_t("font-style"),	_t("normal"),	important);
	add_parsed_property(_t("font-variant"),	_t("normal"),	important);
	add_parsed_property(_t("font-weight"),	_t("normal"),	important);
	add_parsed_property(_t("font-size"),		_t("medium"),	important);
	add_parsed_property(_t("line-height"),	_t("normal"),	important);

	string_vector tokens;
	split_string(val_s.c_str(), tokens, _t(" "), _t(""), _t("\""));

	int idx;
	bool is_family = false;
	tstring font_family;
	for(const auto& token : tokens)
	{
		idx = value_index(token.c_str(), font_style_strings);
		if(!is_family)
		{
			if(idx >= 0)
			{
				if(idx == 0)
				{
					add_parsed_property(_t("font-weight"), token.c_str(), important);
					add_parsed_property(_t("font-variant"), token.c_str(), important);
					add_parsed_property(_t("font-style"), token.c_str(), important);
				} else
				{
					add_parsed_property(_t("font-style"), token.c_str(), important);
				}
			} else
			{
				if(value_in_list(token.c_str(), font_weight_strings))
				{
					add_parsed_property(_t("font-weight"), token.c_str(), important);
				} else
				{
					if(value_in_list(token.c_str(), font_variant_strings))
					{
						add_parsed_property(_t("font-variant"), token.c_str(), important);
					} else if( iswdigit(token[0]) )
					{
						string_vector szlh;
						split_string(token.c_str(), szlh, _t("/"));

						if(szlh.size() == 1)
						{
							add_parsed_property(_t("font-size"),	szlh[0].c_str(), important);
						} else	if(szlh.size() >= 2)
						{
							add_parsed_property(_t("font-size"),	szlh[0].c_str(), important);
							add_parsed_property(_t("line-height"),	szlh[1].c_str(), important);
						}
					} else
					{
						is_family = true;
						/* crust: append named string rather than += of unknown RHS shape. */
			font_family.append(token.c_str());
					}
				}
			}
		} else
		{
			/* crust: append named string rather than += of unknown RHS shape. */
			font_family.append(token.c_str());
		}
	}
	add_parsed_property(_t("font-family"), font_family.c_str(), important);
}

void litehtml::style::add_parsed_property( const tchar_t* name, const tchar_t* val, bool important )
{
	bool is_valid = true;
	/* crust: map keys are strings; bind C-string args once. */
	tstring key;
	if(name)
	{
		key = name;
	}
	tstring val_s;
	if(val)
	{
		val_s = val;
	}
	string_map::iterator vals = m_valid_values.find(key);
	if (vals != m_valid_values.end())
	{
		if (!value_in_list(val_s.c_str(), vals->second.c_str()))
		{
			is_valid = false;
		}
	}

	if (is_valid)
	{
		props_map::iterator prop = m_properties.find(key);
		if (prop != m_properties.end())
		{
			if (!prop->second.m_important || (important && prop->second.m_important))
			{
				prop->second.m_value = val_s.c_str();
				prop->second.m_important = important;
			}
		}
		else
		{
			m_properties[key] = property_value(val_s.c_str(), important);
		}
	}
}

void litehtml::style::remove_property( const tchar_t* name, bool important )
{
	tstring key;
	if(name)
	{
		key = name;
	}
	props_map::iterator prop = m_properties.find(key);
	if(prop != m_properties.end())
	{
		if( !prop->second.m_important || (important && prop->second.m_important) )
		{
			m_properties.erase(prop);
		}
	}
}
