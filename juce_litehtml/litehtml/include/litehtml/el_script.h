#ifndef LH_EL_SCRIPT_H
#define LH_EL_SCRIPT_H

#include "html_tag.h"

namespace litehtml
{
	class el_script : public element
	{
		tstring m_text;
	public:
		/* crust: defined inline. A constructor only declared in a
		   translation is not registered, and the base initializer has to be
		   visible where the class is defined. */
		explicit el_script(const std::shared_ptr<litehtml::document>& doc) : litehtml::element(doc)
		{
		}

		void parse_attributes() override;
		bool appendChild(const ptr &el) override;
		const tchar_t*	get_tagName() const override;
	};
}

#endif  // LH_EL_SCRIPT_H
