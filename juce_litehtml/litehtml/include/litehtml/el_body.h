#ifndef LH_EL_BODY_H
#define LH_EL_BODY_H

#include "html_tag.h"

namespace litehtml
{
	class el_body : public html_tag
	{
	public:
		/* crust: defined inline. A constructor only declared in a
		   translation is not registered, and the base initializer has to be
		   visible where the class is defined. */
		explicit el_body(const std::shared_ptr<litehtml::document>& doc) : litehtml::html_tag(doc)
		{
		}

		bool is_body() const override;
	};
}

#endif  // LH_EL_BODY_H
