#ifndef LH_EL_STYLE_H
#define LH_EL_STYLE_H

#include "html_tag.h"

namespace litehtml
{
	class el_style : public element
	{
		elements_vector		m_children;
	public:
		/* crust: defined inline. A constructor only declared in a
		   translation is not registered, and the base initializer has to be
		   visible where the class is defined. */
		explicit el_style(const std::shared_ptr<litehtml::document>& doc) : litehtml::element(doc)
		{
		}

		void			parse_attributes() override;
		bool			appendChild(const ptr &el) override;
		const tchar_t*	get_tagName() const override;
	};
}

#endif  // LH_EL_STYLE_H
