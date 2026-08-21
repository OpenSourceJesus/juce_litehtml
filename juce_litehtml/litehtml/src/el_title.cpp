#include "html.h"
#include "el_title.h"
#include "document.h"

/* crust: the constructor is defined inline in el_title.h. */

void litehtml::el_title::parse_attributes()
{
	tstring text;
	get_text(text);
	get_document()->container()->set_caption(text.c_str());
}
