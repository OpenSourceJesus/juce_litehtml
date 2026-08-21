#ifndef LH_EL_BREAK_H
#define LH_EL_BREAK_H

#include "html_tag.h"

namespace litehtml
{
	class el_break : public html_tag
	{
	public:
		/* crust: defined inline. A constructor only declared in a
		   translation is not registered, and the base initializer has to be
		   visible where the class is defined. */
		explicit el_break(const std::shared_ptr<litehtml::document>& doc) : html_tag(doc)
		{
		}

		bool is_break() const override;
	};
}

#endif  // LH_EL_BREAK_H
