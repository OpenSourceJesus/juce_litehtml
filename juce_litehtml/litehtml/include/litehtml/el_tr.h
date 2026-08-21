#ifndef LH_EL_TR_H
#define LH_EL_TR_H

#include "html_tag.h"

namespace litehtml
{
	class el_tr : public html_tag
	{
	public:
		/* crust: defined inline. A constructor only declared in a
		   translation is not registered, and the base initializer has to be
		   visible where the class is defined. */
		explicit el_tr(const std::shared_ptr<litehtml::document>& doc) : html_tag(doc)
		{
		}

		void parse_attributes() override;
		void get_inline_boxes(std::vector<position>& boxes) override;
	};
}

#endif  // LH_EL_TR_H
