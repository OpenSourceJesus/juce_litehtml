#ifndef LH_EL_CDATA_H
#define LH_EL_CDATA_H

#include "html_tag.h"

namespace litehtml
{
	class el_cdata : public element
	{
		tstring	m_text;
	public:
		/* crust: defined inline. A constructor only declared in a
		   translation is not registered, and the base initializer has to be
		   visible where the class is defined. */
		explicit el_cdata(const std::shared_ptr<litehtml::document>& doc) : litehtml::element(doc)
		{
		m_skip = true;
		}

		void get_text(tstring& text) override;
		void set_data(const tchar_t* data) override;
	};
}

#endif  // LH_EL_CDATA_H
