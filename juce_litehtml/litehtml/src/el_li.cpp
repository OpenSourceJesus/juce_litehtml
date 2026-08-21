#include "html.h"
#include "el_li.h"
#include "document.h"

/* crust: the constructor is defined inline in el_li.h. */

int litehtml::el_li::render(int x, int y, int max_width, bool second_pass)
{
	if (m_list_style_type >= list_style_type_armenian && !m_index_initialized)
	{
		std::shared_ptr<litehtml::element> p = parent();
		if (p)
		{
			const tchar_t* hasStart = p->get_attr(_t("start"));
			const int start = hasStart ? t_atoi(hasStart) : 1;
			int val = start;
			for (int i = 0, n = (int)p->get_children_count(); i < n; ++i)
			{
				std::shared_ptr<litehtml::element> child = p->get_child(i);
				if (child.get() == this)
				{
					set_attr(_t("list_index"), t_to_string(val).c_str());
					break;
				}
				else if (!t_strcmp(child->get_tagName(), _t("li")))
					++val;
			}
		}

		m_index_initialized = true;
	}

	return html_tag::render(x, y, max_width, second_pass);
}
