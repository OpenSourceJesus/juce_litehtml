#ifndef LH_EL_LINK_H
#define LH_EL_LINK_H

#include "html_tag.h"

namespace litehtml
{
	class el_link : public html_tag
	{
	public:
		/* crust: defined inline. A constructor only declared in a
		   translation is not registered, and the base initializer has to be
		   visible where the class is defined. */
		explicit el_link(const std::shared_ptr<litehtml::document>& doc) : litehtml::html_tag(doc)
		{
		}

	protected:
		void parse_attributes() override;
	};
}

#endif  // LH_EL_LINK_H
