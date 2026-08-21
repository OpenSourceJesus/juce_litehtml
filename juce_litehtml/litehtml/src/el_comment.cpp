#include "html.h"
#include "el_comment.h"

/* crust: the constructor is defined inline in el_comment.h. */

bool litehtml::el_comment::is_comment() const
{
	return true;
}

void litehtml::el_comment::get_text( tstring& text )
{
	text += m_text;
}

void litehtml::el_comment::set_data( const tchar_t* data )
{
	if(data)
	{
		m_text += data;
	}
}
