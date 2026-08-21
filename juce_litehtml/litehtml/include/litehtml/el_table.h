#ifndef LH_EL_TABLE_H
#define LH_EL_TABLE_H

#include "html_tag.h"

namespace litehtml
{
	struct col_info
	{
		int		width;
		bool	is_auto;
	};


	class el_table : public html_tag
	{
	public:
		/* crust: defined inline. A constructor only declared in a
		   translation is not registered, and the base initializer has to be
		   visible where the class is defined. */
		explicit el_table(const std::shared_ptr<litehtml::document>& doc) : html_tag(doc)
		{
		m_border_spacing_x	= 0;
		m_border_spacing_y	= 0;
		m_border_collapse	= border_collapse_separate;
		}

		bool appendChild(const std::shared_ptr<litehtml::element>& el) override;
		void parse_styles(bool is_reparse = false) override;
		void parse_attributes() override;
	};
}

#endif  // LH_EL_TABLE_H
