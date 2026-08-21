#include "html.h"
#include "el_body.h"
#include "document.h"

/* crust: the constructor is defined inline in el_body.h. */

bool litehtml::el_body::is_body()  const
{
	return true;
}
