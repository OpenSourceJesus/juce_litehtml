#include "html.h"
#include "el_para.h"
#include "document.h"

/* crust: the constructor is defined inline in el_para.h. */

void litehtml::el_para::parse_attributes()
{
	const tchar_t* str = get_attr(_t("align"));
	if(str)
	{
		m_style.add_property(_t("text-align"), str, nullptr, false, this);
	}

	html_tag::parse_attributes();
}
