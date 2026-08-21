#ifndef LH_EL_SPACE_H
#define LH_EL_SPACE_H

#include "html_tag.h"
#include "el_text.h"

namespace litehtml
{
	class el_space : public el_text
	{
	public:
		/* crust: defined here rather than out of line in the .cpp. A
		   constructor that is only declared in this translation is not
		   registered, so `make_shared` elsewhere found no constructor to
		   call. A base initializer still resolves against a declared-only
		   base, so only the classes that get constructed by name need this. */
		el_space(const tchar_t* text, const std::shared_ptr<litehtml::document>& doc) : el_text(text, doc)
		{
		}

		bool is_white_space() const override;
		bool is_break() const override;
        bool is_space() const override;
	};
}

#endif  // LH_EL_SPACE_H
