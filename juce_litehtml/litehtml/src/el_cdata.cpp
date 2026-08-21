#include "html.h"
#include "el_cdata.h"

/* crust: the constructor is defined inline in el_cdata.h. */

void litehtml::el_cdata::get_text( tstring& text )
{
	text += m_text;
}

void litehtml::el_cdata::set_data( const tchar_t* data )
{
	if(data)
	{
		m_text += data;
	}
}
