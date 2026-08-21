#include "html.h"
#include "el_script.h"
#include "document.h"


/* crust: the constructor is defined inline in el_script.h. */

void litehtml::el_script::parse_attributes()
{
	std::shared_ptr<document> doc = get_document();

	if (const tchar_t* src = get_attr(_t("src")))
	{
		tstring script;

		doc->container()->import_script(script, src);
	}
}

bool litehtml::el_script::appendChild(const ptr &el)
{
	el->get_text(m_text);
	return true;
}

const litehtml::tchar_t* litehtml::el_script::get_tagName() const
{
	return _t("script");
}
