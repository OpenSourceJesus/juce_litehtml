#ifndef LH_EL_LI_H
#define LH_EL_LI_H

#include "html_tag.h"

namespace litehtml
{
	class el_li : public html_tag
	{
	public:
		/* crust: defined inline. A constructor only declared in a
		   translation is not registered, and the base initializer has to be
		   visible where the class is defined. */
		explicit el_li(const std::shared_ptr<litehtml::document>& doc) : litehtml::html_tag(doc)
		{
		}

		int render(int x, int y, int max_width, bool second_pass = false) override;

	private:
		bool m_index_initialized = false;
	};
}

#endif  // LH_EL_LI_H
