#ifndef LH_JS_OBJECT_REF_H
#define LH_JS_OBJECT_REF_H

#include <memory>

/* crust: these two were nested inside `element` and `document`.

   A nested class has to be reached as `element::js_object_ref`, and a
   class-scoped name is deliberately left alone by the lowering, so the `::`
   survived into the emitted C and `delete ref` had no type to resolve a
   destructor against.

   Hoisting them to namespace scope is only half the fix. `context.h` is where
   they are used, and it is spliced from inside `element.h` -- element.h
   includes context.h at the top, so declaring them in element.h put them
   *after* the template that needs them. Each header is spliced once, so the
   order is fixed by whoever gets there first. Declaring them here, in a
   header context.h includes itself, is what makes them complete at the point
   the instantiation is emitted.

   Both only need a forward declaration of the class they refer to, so this
   header pulls in nothing from litehtml. */

namespace litehtml
{
	class element;
	class document;

	struct element_js_object_ref final
	{
		std::weak_ptr<litehtml::element> element {};
		/* crust: by const reference. A by-value owning parameter is
		   destroyed by the callee, and the caller still owns it too. */
		/* crust: defined inline. A constructor only declared in a
		   translation is not registered, so `new element_js_object_ref(..)`
		   found no constructor to call. The destructor stays out of line --
		   its body needs a complete `element`, which is not available here. */
		element_js_object_ref(const std::shared_ptr<litehtml::element>& el)
			: element { el }
		{}
		~element_js_object_ref();
	};

	struct document_js_object_ref
	{
		litehtml::document* document { nullptr };

		document_js_object_ref(litehtml::document* d)
			: document { d }
		{}
	};
}

#endif
