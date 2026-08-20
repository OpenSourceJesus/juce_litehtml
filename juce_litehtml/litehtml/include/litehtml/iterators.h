#ifndef LH_ITERATORS_H
#define LH_ITERATORS_H

#include "types.h"

namespace litehtml
{
	class element;

	class iterator_selector
	{
	public:
		virtual bool select(const std::shared_ptr<litehtml::element>& el) = 0;

        protected:
		~iterator_selector() = default;
	};

	/* crust: hoisted out of the iterator class. A vector over the nested
	   name instantiates a template whose element type the lowering cannot
	   spell, so the element type lives at namespace scope instead.
	   NB: keep template syntax out of comments here -- comment text is
	   scanned for instantiations, and naming one in prose made the pass try
	   to emit it. */
	struct elements_iterator_stack_item
	{
		int				idx;
		std::shared_ptr<litehtml::element>	el;
		elements_iterator_stack_item() : idx(0)
		{
		}
		elements_iterator_stack_item(const elements_iterator_stack_item& val)
		{
			idx = val.idx;
			el = val.el;
		}
	/* crust: the move constructor is removed -- rvalue references are
	   not in the C++ subset, and the copy constructor above does the
	   same work (one extra shared_ptr refcount bump). */
	};

	class elements_iterator
	{
	private:
		std::vector<elements_iterator_stack_item>		m_stack;
		std::shared_ptr<litehtml::element>				m_el;
		int							m_idx;
		iterator_selector*			m_go_inside;
		iterator_selector*			m_select;
	public:

		elements_iterator(const std::shared_ptr<litehtml::element>& el, iterator_selector* go_inside, iterator_selector* select)
		{ 
			m_el			= el;
			m_idx			= -1; 
			m_go_inside		= go_inside;
			m_select		= select;
		}

		~elements_iterator() = default;

		std::shared_ptr<litehtml::element> next(bool ret_parent = true);
	
	private:
		void next_idx();
	};

	class go_inside_inline final : public iterator_selector
	{
	public:
		bool select(const std::shared_ptr<litehtml::element>& el) override;
	};

	class go_inside_table final : public iterator_selector
	{
	public:
		bool select(const std::shared_ptr<litehtml::element>& el) override;
	};

	class table_rows_selector final : public iterator_selector
	{
	public:
		bool select(const std::shared_ptr<litehtml::element>& el) override;
	};

	class table_cells_selector final : public iterator_selector
	{
	public:
		bool select(const std::shared_ptr<litehtml::element>& el) override;
	};
}

#endif  // LH_ITERATORS_H
