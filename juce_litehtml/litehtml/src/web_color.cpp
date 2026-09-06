#include "html.h"
#include "web_color.h"
#include <cmath>
#include <cstring>

litehtml::def_color litehtml::g_def_colors[] = 
{
	{_t("transparent"),_t("rgba(0, 0, 0, 0)")},
	{_t("AliceBlue"),_t("#F0F8FF")},
	{_t("AntiqueWhite"),_t("#FAEBD7")},
	{_t("Aqua"),_t("#00FFFF")},
	{_t("Aquamarine"),_t("#7FFFD4")},
	{_t("Azure"),_t("#F0FFFF")},
	{_t("Beige"),_t("#F5F5DC")},
	{_t("Bisque"),_t("#FFE4C4")},
	{_t("Black"),_t("#000000")},
	{_t("BlanchedAlmond"),_t("#FFEBCD")},
	{_t("Blue"),_t("#0000FF")},
	{_t("BlueViolet"),_t("#8A2BE2")},
	{_t("Brown"),_t("#A52A2A")},
	{_t("BurlyWood"),_t("#DEB887")},
	{_t("CadetBlue"),_t("#5F9EA0")},
	{_t("Chartreuse"),_t("#7FFF00")},
	{_t("Chocolate"),_t("#D2691E")},
	{_t("Coral"),_t("#FF7F50")},
	{_t("CornflowerBlue"),_t("#6495ED")},
	{_t("Cornsilk"),_t("#FFF8DC")},
	{_t("Crimson"),_t("#DC143C")},
	{_t("Cyan"),_t("#00FFFF")},
	{_t("DarkBlue"),_t("#00008B")},
	{_t("DarkCyan"),_t("#008B8B")},
	{_t("DarkGoldenRod"),_t("#B8860B")},
	{_t("DarkGray"),_t("#A9A9A9")},
	{_t("DarkGrey"),_t("#A9A9A9")},
	{_t("DarkGreen"),_t("#006400")},
	{_t("DarkKhaki"),_t("#BDB76B")},
	{_t("DarkMagenta"),_t("#8B008B")},
	{_t("DarkOliveGreen"),_t("#556B2F")},
	{_t("Darkorange"),_t("#FF8C00")},
	{_t("DarkOrchid"),_t("#9932CC")},
	{_t("DarkRed"),_t("#8B0000")},
	{_t("DarkSalmon"),_t("#E9967A")},
	{_t("DarkSeaGreen"),_t("#8FBC8F")},
	{_t("DarkSlateBlue"),_t("#483D8B")},
	{_t("DarkSlateGray"),_t("#2F4F4F")},
	{_t("DarkSlateGrey"),_t("#2F4F4F")},
	{_t("DarkTurquoise"),_t("#00CED1")},
	{_t("DarkViolet"),_t("#9400D3")},
	{_t("DeepPink"),_t("#FF1493")},
	{_t("DeepSkyBlue"),_t("#00BFFF")},
	{_t("DimGray"),_t("#696969")},
	{_t("DimGrey"),_t("#696969")},
	{_t("DodgerBlue"),_t("#1E90FF")},
	{_t("FireBrick"),_t("#B22222")},
	{_t("FloralWhite"),_t("#FFFAF0")},
	{_t("ForestGreen"),_t("#228B22")},
	{_t("Fuchsia"),_t("#FF00FF")},
	{_t("Gainsboro"),_t("#DCDCDC")},
	{_t("GhostWhite"),_t("#F8F8FF")},
	{_t("Gold"),_t("#FFD700")},
	{_t("GoldenRod"),_t("#DAA520")},
	{_t("Gray"),_t("#808080")},
	{_t("Grey"),_t("#808080")},
	{_t("Green"),_t("#008000")},
	{_t("GreenYellow"),_t("#ADFF2F")},
	{_t("HoneyDew"),_t("#F0FFF0")},
	{_t("HotPink"),_t("#FF69B4")},
	{_t("Ivory"),_t("#FFFFF0")},
	{_t("Khaki"),_t("#F0E68C")},
	{_t("Lavender"),_t("#E6E6FA")},
	{_t("LavenderBlush"),_t("#FFF0F5")},
	{_t("LawnGreen"),_t("#7CFC00")},
	{_t("LemonChiffon"),_t("#FFFACD")},
	{_t("LightBlue"),_t("#ADD8E6")},
	{_t("LightCoral"),_t("#F08080")},
	{_t("LightCyan"),_t("#E0FFFF")},
	{_t("LightGoldenRodYellow"),_t("#FAFAD2")},
	{_t("LightGray"),_t("#D3D3D3")},
	{_t("LightGrey"),_t("#D3D3D3")},
	{_t("LightGreen"),_t("#90EE90")},
	{_t("LightPink"),_t("#FFB6C1")},
	{_t("LightSalmon"),_t("#FFA07A")},
	{_t("LightSeaGreen"),_t("#20B2AA")},
	{_t("LightSkyBlue"),_t("#87CEFA")},
	{_t("LightSlateGray"),_t("#778899")},
	{_t("LightSlateGrey"),_t("#778899")},
	{_t("LightSteelBlue"),_t("#B0C4DE")},
	{_t("LightYellow"),_t("#FFFFE0")},
	{_t("Lime"),_t("#00FF00")},
	{_t("LimeGreen"),_t("#32CD32")},
	{_t("Linen"),_t("#FAF0E6")},
	{_t("Magenta"),_t("#FF00FF")},
	{_t("Maroon"),_t("#800000")},
	{_t("MediumAquaMarine"),_t("#66CDAA")},
	{_t("MediumBlue"),_t("#0000CD")},
	{_t("MediumOrchid"),_t("#BA55D3")},
	{_t("MediumPurple"),_t("#9370D8")},
	{_t("MediumSeaGreen"),_t("#3CB371")},
	{_t("MediumSlateBlue"),_t("#7B68EE")},
	{_t("MediumSpringGreen"),_t("#00FA9A")},
	{_t("MediumTurquoise"),_t("#48D1CC")},
	{_t("MediumVioletRed"),_t("#C71585")},
	{_t("MidnightBlue"),_t("#191970")},
	{_t("MintCream"),_t("#F5FFFA")},
	{_t("MistyRose"),_t("#FFE4E1")},
	{_t("Moccasin"),_t("#FFE4B5")},
	{_t("NavajoWhite"),_t("#FFDEAD")},
	{_t("Navy"),_t("#000080")},
	{_t("OldLace"),_t("#FDF5E6")},
	{_t("Olive"),_t("#808000")},
	{_t("OliveDrab"),_t("#6B8E23")},
	{_t("Orange"),_t("#FFA500")},
	{_t("OrangeRed"),_t("#FF4500")},
	{_t("Orchid"),_t("#DA70D6")},
	{_t("PaleGoldenRod"),_t("#EEE8AA")},
	{_t("PaleGreen"),_t("#98FB98")},
	{_t("PaleTurquoise"),_t("#AFEEEE")},
	{_t("PaleVioletRed"),_t("#D87093")},
	{_t("PapayaWhip"),_t("#FFEFD5")},
	{_t("PeachPuff"),_t("#FFDAB9")},
	{_t("Peru"),_t("#CD853F")},
	{_t("Pink"),_t("#FFC0CB")},
	{_t("Plum"),_t("#DDA0DD")},
	{_t("PowderBlue"),_t("#B0E0E6")},
	{_t("Purple"),_t("#800080")},
	{_t("Red"),_t("#FF0000")},
	{_t("RosyBrown"),_t("#BC8F8F")},
	{_t("RoyalBlue"),_t("#4169E1")},
	{_t("SaddleBrown"),_t("#8B4513")},
	{_t("Salmon"),_t("#FA8072")},
	{_t("SandyBrown"),_t("#F4A460")},
	{_t("SeaGreen"),_t("#2E8B57")},
	{_t("SeaShell"),_t("#FFF5EE")},
	{_t("Sienna"),_t("#A0522D")},
	{_t("Silver"),_t("#C0C0C0")},
	{_t("SkyBlue"),_t("#87CEEB")},
	{_t("SlateBlue"),_t("#6A5ACD")},
	{_t("SlateGray"),_t("#708090")},
	{_t("SlateGrey"),_t("#708090")},
	{_t("Snow"),_t("#FFFAFA")},
	{_t("SpringGreen"),_t("#00FF7F")},
	{_t("SteelBlue"),_t("#4682B4")},
	{_t("Tan"),_t("#D2B48C")},
	{_t("Teal"),_t("#008080")},
	{_t("Thistle"),_t("#D8BFD8")},
	{_t("Tomato"),_t("#FF6347")},
	{_t("Turquoise"),_t("#40E0D0")},
	{_t("Violet"),_t("#EE82EE")},
	{_t("Wheat"),_t("#F5DEB3")},
	{_t("White"),_t("#FFFFFF")},
	{_t("WhiteSmoke"),_t("#F5F5F5")},
	{_t("Yellow"),_t("#FFFF00")},
	{_t("YellowGreen"),_t("#9ACD32")},
	{nullptr,nullptr}
};


/* Expand one hex digit to a byte: 'a' → 0xaa. */
static litehtml::byte hex_digit_byte(litehtml::tchar_t c)
{
	litehtml::tchar_t pair[3] = { c, c, 0 };
	litehtml::tchar_t* end = nullptr;
	return (litehtml::byte) t_strtol(pair, &end, 16);
}

static litehtml::byte hex_pair_byte(litehtml::tchar_t hi, litehtml::tchar_t lo)
{
	litehtml::tchar_t pair[3] = { hi, lo, 0 };
	litehtml::tchar_t* end = nullptr;
	return (litehtml::byte) t_strtol(pair, &end, 16);
}

/* CSS hsl(h, s%, l%[, a]) → sRGB. h may carry a unit suffix (deg); s/l %. */
static litehtml::web_color hsl_to_rgb(double h, double s, double l, double a)
{
	h = std::fmod(h, 360.0);
	if(h < 0) h += 360.0;
	s = s < 0 ? 0 : (s > 1 ? 1 : s);
	l = l < 0 ? 0 : (l > 1 ? 1 : l);
	a = a < 0 ? 0 : (a > 1 ? 1 : a);

	double c = (1.0 - std::fabs(2.0 * l - 1.0)) * s;
	double x = c * (1.0 - std::fabs(std::fmod(h / 60.0, 2.0) - 1.0));
	double m = l - c / 2.0;
	double r1 = 0, g1 = 0, b1 = 0;
	if(h < 60)       { r1 = c; g1 = x; }
	else if(h < 120) { r1 = x; g1 = c; }
	else if(h < 180) { g1 = c; b1 = x; }
	else if(h < 240) { g1 = x; b1 = c; }
	else if(h < 300) { r1 = x; b1 = c; }
	else             { r1 = c; b1 = x; }

	return litehtml::web_color(
		(litehtml::byte)((r1 + m) * 255.0 + 0.5),
		(litehtml::byte)((g1 + m) * 255.0 + 0.5),
		(litehtml::byte)((b1 + m) * 255.0 + 0.5),
		(litehtml::byte)(a * 255.0 + 0.5));
}

litehtml::web_color litehtml::web_color::from_string(const tchar_t* str, litehtml::document_container* callback)
{
	if(!str || !str[0])
	{
		return web_color(0, 0, 0);
	}
	if(str[0] == _t('#'))
	{
		/* #rgb, #rgba, #rrggbb, #rrggbbaa. Other lengths used to fall
		   through with empty channels → opaque black. */
		size_t n = t_strlen(str + 1);
		web_color clr;
		if(n == 3 || n == 4)
		{
			clr.red   = hex_digit_byte(str[1]);
			clr.green = hex_digit_byte(str[2]);
			clr.blue  = hex_digit_byte(str[3]);
			if(n == 4) clr.alpha = hex_digit_byte(str[4]);
			return clr;
		}
		if(n == 6 || n == 8)
		{
			clr.red   = hex_pair_byte(str[1], str[2]);
			clr.green = hex_pair_byte(str[3], str[4]);
			clr.blue  = hex_pair_byte(str[5], str[6]);
			if(n == 8) clr.alpha = hex_pair_byte(str[7], str[8]);
			return clr;
		}
		return web_color(0, 0, 0, 0);
	} else if(!t_strncasecmp(str, _t("rgb"), 3))
	{
		tstring s = str;

		tstring::size_type pos = s.find_first_of(_t('('));
		if(pos != tstring::npos)
		{
			s.erase(s.begin(), s.begin() + pos + 1);
		}
		pos = s.find_last_of(_t(')'));
		if(pos != tstring::npos)
		{
			s.erase(s.begin() + pos, s.end());
		}

		std::vector<tstring> tokens;
		split_string(s.c_str(), tokens, _t(", \t"));

		web_color clr;

		if(tokens.size() >= 1)	clr.red		= (byte) t_atoi(tokens[0].c_str());
		if(tokens.size() >= 2)	clr.green	= (byte) t_atoi(tokens[1].c_str());
		if(tokens.size() >= 3)	clr.blue	= (byte) t_atoi(tokens[2].c_str());
		if(tokens.size() >= 4)	clr.alpha	= (byte) (t_strtod(tokens[3].c_str(), nullptr) * 255.0);

		return clr;
	} else if(!t_strncasecmp(str, _t("hsl"), 3))
	{
		tstring s = str;
		tstring::size_type pos = s.find_first_of(_t('('));
		if(pos != tstring::npos)
		{
			s.erase(s.begin(), s.begin() + pos + 1);
		}
		pos = s.find_last_of(_t(')'));
		if(pos != tstring::npos)
		{
			s.erase(s.begin() + pos, s.end());
		}

		std::vector<tstring> tokens;
		split_string(s.c_str(), tokens, _t(", \t"));
		if(tokens.size() < 3)
		{
			return web_color(0, 0, 0, 0);
		}

		double h = t_strtod(tokens[0].c_str(), nullptr);
		double s_val = t_strtod(tokens[1].c_str(), nullptr);
		double l_val = t_strtod(tokens[2].c_str(), nullptr);
		/* Percentages are the common form; bare 0..1 also accepted. */
		if(tokens[1].find(_t('%')) != tstring::npos || s_val > 1.0) s_val /= 100.0;
		if(tokens[2].find(_t('%')) != tstring::npos || l_val > 1.0) l_val /= 100.0;
		double a = 1.0;
		if(tokens.size() >= 4)
		{
			a = t_strtod(tokens[3].c_str(), nullptr);
			if(tokens[3].find(_t('%')) != tstring::npos) a /= 100.0;
		}
		return hsl_to_rgb(h, s_val, l_val, a);
	} else
	{
		tstring rgb = resolve_name(str, callback);
		if(!rgb.empty())
		{
			return from_string(rgb.c_str(), callback);
		}
	}
	/* Unrecognised value (e.g. linear-gradient stored as a colour by a
	   too-loose is_color). Transparent, not opaque black — otherwise every
	   unsupported background function paints a solid black box. */
	return web_color(0, 0, 0, 0);
}

litehtml::tstring litehtml::web_color::resolve_name(const tchar_t* name, litehtml::document_container* callback)
{
	for(int i=0; g_def_colors[i].name; i++)
	{
		if(!t_strcasecmp(name, g_def_colors[i].name))
		{
            return litehtml::tstring(g_def_colors[i].rgb);
		}
	}
    if (callback)
    {
        litehtml::tstring clr = callback->resolve_color(name);
        return clr;
    }
    return litehtml::tstring();
}

bool litehtml::web_color::is_color(const tchar_t* str)
{
	if(!str || !str[0])
	{
		return false;
	}
	if(str[0] == _t('#'))
	{
		return true;
	}
	/* rgb/rgba/hsl/hsla */
	if(!t_strncasecmp(str, _t("rgb"), 3) || !t_strncasecmp(str, _t("hsl"), 3))
	{
		return true;
	}
	/* CSS functions that are not colours (linear-gradient, url, var, …)
	   used to match the "starts with a letter" branch below, get stored as
	   background-color, then from_string failed to opaque black — full-width
	   black bars on Wikipedia sticky fades, etc. */
	for(const tchar_t* p = str; *p; p++)
	{
		if(*p == _t('('))
		{
			return false;
		}
	}
	if(!t_isdigit(str[0]) && str[0] != _t('.'))
	{
		return true;
	}
	return false;
}
