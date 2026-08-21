#include "html.h"
#include "el_image.h"
#include "document.h"

/* crust: the constructor is defined inline in el_image.h. */

litehtml::el_image::~el_image( void )
{

}

void litehtml::el_image::get_content_size( size& sz, int max_width )
{
	get_document()->container()->get_image_size(m_src.c_str(), 0, sz);
}

int litehtml::el_image::calc_max_height(int image_height)
{
	std::shared_ptr<document> doc = get_document();
	int percentSize = 0;
	if (m_css_max_height.units() == css_units_percentage)
	{
		auto el_parent = parent();
		if (el_parent)
		{
			if (!el_parent->get_predefined_height(percentSize))
			{
				return image_height;
			}
		}
	}
	return doc->cvt_units(m_css_max_height, m_font_size, percentSize);
}

int litehtml::el_image::line_height() const
{
	return height();
}

bool litehtml::el_image::is_replaced() const
{
	return true;
}

int litehtml::el_image::render( int x, int y, int max_width, bool second_pass )
{
	int parent_width = max_width;

	calc_outlines(parent_width);

	m_pos.move_to(x, y);

	std::shared_ptr<document> doc = get_document();

	litehtml::size sz;
	doc->container()->get_image_size(m_src.c_str(), 0, sz);

	m_pos.width		= sz.width;
	m_pos.height	= sz.height;

	if(m_css_height.is_predefined() && m_css_width.is_predefined())
	{
		m_pos.height	= sz.height;
		m_pos.width		= sz.width;

		// check for max-width
		if(!m_css_max_width.is_predefined())
		{
			int max_width = doc->cvt_units(m_css_max_width, m_font_size, parent_width);
			if(m_pos.width > max_width)
			{
				m_pos.width = max_width;
			}
			if(sz.width)
			{
				m_pos.height = (int) ((float) m_pos.width * (float) sz.height / (float)sz.width);
			} else
			{
				m_pos.height = sz.height;
			}
		}

		// check for max-height
		if(!m_css_max_height.is_predefined())
		{
			int max_height = calc_max_height(sz.height);
			if(m_pos.height > max_height)
			{
				m_pos.height = max_height;
			}
			if(sz.height)
			{
				m_pos.width = (int) (m_pos.height * (float)sz.width / (float)sz.height);
			} else
			{
				m_pos.width = sz.width;
			}
		}
	} else if(!m_css_height.is_predefined() && m_css_width.is_predefined())
	{
		if (!get_predefined_height(m_pos.height))
		{
			m_pos.height = (int)m_css_height.val();
		}

		// check for max-height
		if(!m_css_max_height.is_predefined())
		{
			int max_height = calc_max_height(sz.height);
			if(m_pos.height > max_height)
			{
				m_pos.height = max_height;
			}
		}

		if(sz.height)
		{
			m_pos.width = (int) (m_pos.height * (float)sz.width / (float)sz.height);
		} else
		{
			m_pos.width = sz.width;
		}
	} else if(m_css_height.is_predefined() && !m_css_width.is_predefined())
	{
		m_pos.width = (int) m_css_width.calc_percent(parent_width);

		// check for max-width
		if(!m_css_max_width.is_predefined())
		{
			int max_width = doc->cvt_units(m_css_max_width, m_font_size, parent_width);
			if(m_pos.width > max_width)
			{
				m_pos.width = max_width;
			}
		}

		if(sz.width)
		{
			m_pos.height = (int) ((float) m_pos.width * (float) sz.height / (float)sz.width);
		} else
		{
			m_pos.height = sz.height;
		}
	} else
	{
		m_pos.width		= (int) m_css_width.calc_percent(parent_width);
		m_pos.height	= 0;
		if (!get_predefined_height(m_pos.height))
		{
			m_pos.height = (int)m_css_height.val();
		}

		// check for max-height
		if(!m_css_max_height.is_predefined())
		{
			int max_height = calc_max_height(sz.height);
			if(m_pos.height > max_height)
			{
				m_pos.height = max_height;
			}
		}

		// check for max-height
		if(!m_css_max_width.is_predefined())
		{
			int max_width = doc->cvt_units(m_css_max_width, m_font_size, parent_width);
			if(m_pos.width > max_width)
			{
				m_pos.width = max_width;
			}
		}
	}

	calc_auto_margins(parent_width);

	m_pos.x	+= content_margins_left();
	m_pos.y += content_margins_top();

	return m_pos.width + content_margins_left() + content_margins_right();
}

void litehtml::el_image::parse_attributes()
{
	m_src = get_attr(_t("src"), _t(""));

	const tchar_t* attr_height = get_attr(_t("height"));
	if(attr_height)
	{
		m_style.add_property(_t("height"), attr_height, 0, false, this);
	}
	const tchar_t* attr_width = get_attr(_t("width"));
	if(attr_width)
	{
		m_style.add_property(_t("width"), attr_width, 0, false, this);
	}
}

void litehtml::el_image::draw( uint_ptr hdc, int x, int y, const position* clip )
{
	position pos = m_pos;
	pos.x += x;
	pos.y += y;

	position el_pos = pos;
	el_pos += m_padding;
	el_pos += m_borders;

	// draw standard background here
	if (el_pos.does_intersect(clip))
	{
		const background* bg = get_background();
		if (bg)
		{
			background_paint bg_paint;
			init_background_paint(pos, &bg_paint, bg);

			get_document()->container()->draw_background(hdc, &bg_paint);
		}
	}

	// draw image as background
	if(pos.does_intersect(clip))
	{
		if (pos.width > 0 && pos.height > 0) {
			background_paint img_bg;	/* crust: renamed from `bg`. This function
			   already has a `const background* bg` in an outer scope, and the
			   pass does not scope locals by block -- it took this by-value one
			   as the type of the `bg` handed to init_background_paint above. */
			img_bg.image				= m_src;
			img_bg.clip_box				= pos;
			img_bg.origin_box			= pos;
			img_bg.border_box			= pos;
			img_bg.border_box			+= m_padding;
			img_bg.border_box			+= m_borders;
			img_bg.repeat				= background_repeat_no_repeat;
			img_bg.image_size.width		= pos.width;
			img_bg.image_size.height	= pos.height;
			img_bg.border_radius		= m_css_borders.radius.calc_percents(img_bg.border_box.width, img_bg.border_box.height);
			img_bg.position_x			= pos.x;
			img_bg.position_y			= pos.y;
			get_document()->container()->draw_background(hdc, &img_bg);
		}
	}

	// draw borders
	if (el_pos.does_intersect(clip))
	{
		position border_box = pos;
		border_box += m_padding;
		border_box += m_borders;

		/* crust: `borders b = css_borders` is a *converting* constructor, not a
		   copy, and the pass read it as a copy of a type the right-hand side
		   does not have. Default-construct then assign through
		   `operator=(const css_borders&)`, which is the same conversion
		   borders.h already performs, and the same result: the converting
		   constructor does not set `radius` either. */
		borders bdr(m_css_borders);
		bdr.radius = m_css_borders.radius.calc_percents(border_box.width, border_box.height);

		get_document()->container()->draw_borders(hdc, bdr, border_box, !have_parent());
	}
}

void litehtml::el_image::parse_styles( bool is_reparse /*= false*/ )
{
	html_tag::parse_styles(is_reparse);

	if(!m_src.empty())
	{
		if(!m_css_height.is_predefined() && !m_css_width.is_predefined())
		{
			get_document()->container()->load_image(m_src.c_str(), nullptr, true);
		} else
		{
			get_document()->container()->load_image(m_src.c_str(), nullptr, false);
		}
	}
}
