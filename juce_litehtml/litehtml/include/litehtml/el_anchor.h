#ifndef LH_EL_ANCHOR_H
#define LH_EL_ANCHOR_H

#include "html_tag.h"

namespace litehtml
{
	class el_anchor : public html_tag
	{
	public:
		/* crust: defined inline. A constructor only declared in a
		   translation is not registered, and the base initializer has to be
		   visible where the class is defined. */
		explicit el_anchor(const std::shared_ptr<litehtml::document>& doc) : html_tag(doc)
		{
		}

		void	on_click() override;
		void	apply_stylesheet(const litehtml::css& stylesheet) override;
	};
}

#endif  // LH_EL_ANCHOR_H
