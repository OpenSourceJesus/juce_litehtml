#include "html.h"
#include "el_div.h"


/* crust: the constructor is defined inline in el_div.h. */

void litehtml::el_div::parse_attributes()
{
	const tchar_t* str = get_attr(_t("align"));
	if(str)
	{
		m_style.add_property(_t("text-align"), str, 0, false, this);
	}
	html_tag::parse_attributes();
}
