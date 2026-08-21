#ifndef LH_EL_TD_H
#define LH_EL_TD_H

#include "html_tag.h"

namespace litehtml
{
	class el_td : public html_tag
	{
	public:
		/* crust: defined inline. A constructor only declared in a
		   translation is not registered, and the base initializer has to be
		   visible where the class is defined. */
		explicit el_td(const std::shared_ptr<litehtml::document>& doc) : html_tag(doc)
		{
		}

		void parse_attributes() override;
	};
}

#endif  // LH_EL_TD_H
