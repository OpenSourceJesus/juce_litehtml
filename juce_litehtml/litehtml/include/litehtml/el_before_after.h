#ifndef LH_EL_BEFORE_AFTER_H
#define LH_EL_BEFORE_AFTER_H

#include "html_tag.h"

namespace litehtml
{
	/* crust: the base took `(doc, before)`, and a two-argument base could not
	   be reached from a derived initializer list -- an only-constructor keeps
	   the plain `T_new` symbol whatever its arity, while the base initializer
	   resolves by arity and looked for the two-argument spelling.

	   Passing only `doc` puts this back on the same shape as every other
	   `html_tag` subclass in the tree (`el_div`, `el_para`, ...), which
	   already lower. The bool became `init_pseudo`, called from the derived
	   constructor body, which does exactly what the base body did. */
	class el_before_after_base : public html_tag
	{
	public:
		el_before_after_base(const std::shared_ptr<litehtml::document>& doc);

		void add_style(const tstring& style, const tstring& baseurl) override;
		void apply_stylesheet(const litehtml::css& stylesheet) override;
	protected:
		void	init_pseudo(bool before);
	private:
		void	add_text(const tstring& txt);
		void	add_function(const tstring& fnc, const tstring& params);
		static tstring convert_escape(const tchar_t* txt);
	};

	/* crust: these two constructors are defined out of line in
	   el_before_after.cpp, which is what `el_div` and every other html_tag
	   subclass already does. Defined inline in the class body they were
	   lowered while the base's own constructor was still only a declaration
	   -- out-of-line definitions are lifted and emitted last -- so the base
	   had nothing to pass arguments to. */
	class el_before : public el_before_after_base
	{
	public:
		explicit el_before(const std::shared_ptr<litehtml::document>& doc);
	};

	class el_after : public el_before_after_base
	{
	public:
		explicit el_after(const std::shared_ptr<litehtml::document>& doc);
	};
}

#endif  // LH_EL_BEFORE_AFTER_H
