#ifndef LH_STYLESHEET_H
#define LH_STYLESHEET_H

#include "style.h"
#include "css_selector.h"

namespace litehtml
{
	class document_container;

	class css
	{
		std::vector<std::shared_ptr<css_selector> >	m_selectors;
	public:
		css() = default;
		~css() = default;

		/* crust: reference return -> pointer. */
		const std::vector<std::shared_ptr<css_selector> >* selectors() const
		{
			return &m_selectors;
		}

		void clear()
		{
			m_selectors.clear();
		}

		void	parse_stylesheet(const tchar_t* str, const tchar_t* baseurl, const std::shared_ptr <document>& doc, const std::shared_ptr<media_query_list>& media);
		void	sort_selectors();
		static void	parse_css_url(const tstring& str, tstring& url);

	private:
		void	parse_atrule(const tstring& text, const tchar_t* baseurl, const std::shared_ptr<document>& doc, const std::shared_ptr<media_query_list>& media);
		void	add_selector(const std::shared_ptr<css_selector>& selector);
		bool	parse_selectors(const tstring& txt, const tstring& styles, const std::shared_ptr<media_query_list>& media, const tstring& baseurl);

	};

	inline void litehtml::css::add_selector( const std::shared_ptr<css_selector>& selector )
	{
		selector->m_order = (int) m_selectors.size();
		m_selectors.push_back(selector);
	}

}

#endif  // LH_STYLESHEET_H
