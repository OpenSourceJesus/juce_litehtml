#include "html.h"
#include "stylesheet.h"
#include <algorithm>
#include "document.h"


void litehtml::css::parse_stylesheet(const tchar_t* str, const tchar_t* baseurl, const std::shared_ptr<document>& doc, const std::shared_ptr<media_query_list>& media)
{
	tstring text = str;

	// remove comments
	tstring::size_type c_start = text.find(_t("/*"));
	while(c_start != tstring::npos)
	{
		tstring::size_type c_end = text.find(_t("*/"), c_start + 2);
		text.erase(c_start, c_end - c_start + 2);
		c_start = text.find(_t("/*"));
	}

	tstring::size_type pos = text.find_first_not_of(_t(" \n\r\t"));
	while(pos != tstring::npos)
	{
		while(pos != tstring::npos && text[pos] == _t('@'))
		{
			tstring::size_type sPos = pos;
			pos = text.find_first_of(_t("{;"), pos);
			if(pos != tstring::npos && text[pos] == _t('{'))
			{
				pos = find_close_bracket(text, pos, _t('{'), _t('}'));
			}
			if(pos != tstring::npos)
			{
				/* crust: bind substr before the reference parameter. */
				tstring atrule = text.substr(sPos, pos - sPos + 1);
				parse_atrule(atrule, baseurl, doc, media);
			} else
			{
				/* crust: bind substr before the reference parameter. */
				tstring atrule = text.substr(sPos);
				parse_atrule(atrule, baseurl, doc, media);
			}

			if(pos != tstring::npos)
			{
				pos = text.find_first_not_of(_t(" \n\r\t"), pos + 1);
			}
		}

		if(pos == tstring::npos)
		{
			break;
		}

		tstring::size_type style_start	= text.find(_t('{'), pos);
		tstring::size_type style_end	= text.find(_t('}'), pos);
		if(style_start != tstring::npos && style_end != tstring::npos)
		{
			/* crust: written type -- `auto` cannot deduce substr here
			   without a clang subprocess, and the harness defaults to
			   --no-clang. */
			tstring style = text.substr(style_start + 1, style_end - style_start - 1);

			/* crust: bind substr before the reference parameter. */
			tstring selector_txt = text.substr(pos, style_start - pos);
			const tchar_t* baseurl_arg = baseurl;
			if(!baseurl_arg)
			{
				baseurl_arg = _t("");
			}
			parse_selectors(selector_txt, style, media, baseurl_arg);

			if(media && doc)
			{
				doc->add_media_list(media);
			}

			pos = style_end + 1;
		} else
		{
			pos = tstring::npos;
		}

		if(pos != tstring::npos)
		{
			pos = text.find_first_not_of(_t(" \n\r\t"), pos);
		}
	}
}

void litehtml::css::parse_css_url( const tstring& str, tstring& url )
{
	url = _t("");
	size_t pos1 = str.find(_t('('));
	size_t pos2 = str.find(_t(')'));
	if(pos1 != tstring::npos && pos2 != tstring::npos)
	{
		url = str.substr(pos1 + 1, pos2 - pos1 - 1);
		if(url.length())
		{
			if(url[0] == _t('\'') || url[0] == _t('"'))
			{
				url.erase(0, 1);
			}
		}
		if(url.length())
		{
			if(url[url.length() - 1] == _t('\'') || url[url.length() - 1] == _t('"'))
			{
				url.erase(url.length() - 1, 1);
			}
		}
	}
}

bool litehtml::css::parse_selectors( const tstring& txt, const tstring& styles, const std::shared_ptr<media_query_list>& media, const tchar_t* baseurl )
{
	tstring selector = txt;
	trim(selector);
	string_vector tokens;
	split_string(selector.c_str(), tokens, _t(","));

	bool added_something = false;

	/* crust: written type -- `auto` cannot stand alone under --no-clang. */
	for(tstring & token : tokens)
	{
		std::shared_ptr<css_selector> new_selector = std::make_shared<css_selector>(media, baseurl);
        new_selector->m_style = styles;
		trim(token);
		if(new_selector->parse(token))
		{
			new_selector->calc_specificity();
			add_selector(new_selector);
			added_something = true;
		}
	}

	return added_something;
}

void litehtml::css::sort_selectors()
{
	/* crust: insertion sort. `std::sort` with a lambda is outside the
	   subset, and the free `sort` template needs an explicit `<T>` plus
	   `__cpp_cmp` rather than the free `operator<` on css_selector. Same
	   ascending order as before. */
	for (int i = 1; i < (int)m_selectors.size(); i++)
	{
		std::shared_ptr<css_selector> key = m_selectors[i];
		int j = i - 1;
		while (j >= 0 && key < m_selectors[j])
		{
			m_selectors[j + 1] = m_selectors[j];
			j = j - 1;
		}
		m_selectors[j + 1] = key;
	}
}

void litehtml::css::parse_atrule(const tstring& text, const tchar_t* baseurl, const std::shared_ptr<document>& doc, const std::shared_ptr<media_query_list>& media)
{
	if(text.substr(0, 7) == _t("@import"))
	{
		int sPos = 7;
		tstring iStr;
		iStr = text.substr(sPos);
		if(iStr[iStr.length() - 1] == _t(';'))
		{
			iStr.erase(iStr.length() - 1);
		}
		trim(iStr);
		string_vector tokens;
		split_string(iStr.c_str(), tokens, _t(" "), _t(""), _t("(\""));
		if(!tokens.empty())
		{
			tstring url;
			/* crust: bind tokens[0] before the reference parameter --
			   `front()` is a call result with no address. */
			tstring front = tokens[0];
			parse_css_url(front, url);
			if(url.empty())
			{
				url = front;
			}
			tokens.erase(tokens.begin());
			if(doc)
			{
				document_container* doc_cont = doc->container();
				if(doc_cont)
				{
					tstring css_text;
					tstring css_baseurl;
					if(baseurl)
					{
						css_baseurl = baseurl;
					}
					doc_cont->import_css(css_text, url, css_baseurl);
					if(!css_text.empty())
					{
						std::shared_ptr<media_query_list> new_media = media;
						if(!tokens.empty())
						{
							/* crust: indexed loop and `append` -- iterators
							   and `+=` of a `const tchar_t*` are outside
							   what this pass can name. */
							tstring media_str;
							for(size_t ti = 0; ti < tokens.size(); ti++)
							{
								if(ti != 0)
								{
									media_str.append(_t(" "));
								}
								media_str.append(tokens[ti].c_str());
							}
							new_media = media_query_list::create_from_string(media_str, doc);
							if(!new_media)
							{
								new_media = media;
							}
						}
						parse_stylesheet(css_text.c_str(), css_baseurl.c_str(), doc, new_media);
					}
				}
			}
		}
	} else if(text.substr(0, 6) == _t("@media"))
	{
		tstring::size_type b1 = text.find_first_of(_t('{'));
		tstring::size_type b2 = text.find_last_of(_t('}'));
		if(b1 != tstring::npos)
		{
			tstring media_type = text.substr(6, b1 - 6);
			trim(media_type);
			std::shared_ptr<media_query_list> new_media = media_query_list::create_from_string(media_type, doc);

			tstring media_style;
			if(b2 != tstring::npos)
			{
				media_style = text.substr(b1 + 1, b2 - b1 - 1);
			} else
			{
				media_style = text.substr(b1 + 1);
			}

			parse_stylesheet(media_style.c_str(), baseurl, doc, new_media);
		}
	}
}
