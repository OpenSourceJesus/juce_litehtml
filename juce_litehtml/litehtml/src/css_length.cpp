#include "html.h"
#include "css_length.h"

void litehtml::css_length::fromString( const tstring& str, const tstring& predefs, int defValue )
{
	// TODO: Make support for calc
	if(str.substr(0, 4) == _t("calc"))
	{
		m_is_predefined = true;
		m_predef		= defValue;
		return;
	}

	int predef = value_index(str.c_str(), predefs.c_str(), -1);
	if(predef >= 0)
	{
		m_is_predefined = true;
		m_predef		= predef;
	} else
	{
		m_is_predefined = false;

		tstring num;
		tstring un;
		bool is_unit = false;
		for(tchar_t chr : str)
		{
			if(!is_unit)
			{
				if(t_isdigit(chr) || chr == _t('.') || chr == _t('+') || chr == _t('-'))
				{
					/* crust: `+=` needs a named string; a char has no address
					   as one. `push_back` takes the char directly. */
					num.push_back(chr);
				} else
				{
					is_unit = true;
				}
			}
			if(is_unit)
			{
				/* crust: see above -- `push_back` rather than `+=`. */
				un.push_back(chr);
			}
		}
		if(!num.empty())
		{
			m_value = (float) t_strtod(num.c_str(), nullptr);
			m_units	= (css_units) value_index(un.c_str(), css_units_strings, css_units_none);
		} else
		{
			// not a number so it is predefined
			m_is_predefined = true;
			m_predef = defValue;
		}
	}
}
