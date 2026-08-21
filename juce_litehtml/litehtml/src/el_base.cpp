#include "html.h"
#include "el_base.h"
#include "document.h"

/* crust: the constructor is defined inline in el_base.h. */

void litehtml::el_base::parse_attributes()
{
	get_document()->container()->set_base_url(get_attr(_t("href")));
}
