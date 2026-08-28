#ifndef LH_CONTEXT_H
#define LH_CONTEXT_H

#include <cassert>

extern "C" {
#	include "quickjs.h"
}

#include "js_object_ref.h"
#include "stylesheet.h"

namespace litehtml
{
	class context
	{
	public:
		context();
		virtual ~context();

		/** Register JavaScript class.
		    crust: the finalizer is passed in rather than written as a lambda
		    in the template body. It used to `delete` a `typename
		    T::js_object_ref`, and a class-scoped name is left alone by the
		    lowering, so the `::` reached the C front end with no destructor
		    to resolve. Spelling `RefT` as a second template parameter did not
		    help either -- the `delete` still sat inside a template, where the
		    operand's type is only known after substitution. A concrete
		    function per class is defined where the type is complete, so
		    nothing here has to be deduced at all. */
		template<class T>
		void js_register_class(const char* className, JSClassFinalizer* finalizer)
		{
			if (T::jsClassID == 0)
				JS_NewClassID (m_jsRuntime, &T::jsClassID);

			if (!JS_IsRegisteredClass(m_jsRuntime, T::jsClassID))
			{
				const JSClassDef def {
					className,
					finalizer,
					nullptr,
					nullptr,
					nullptr
				};

				/* crust: `auto` -> `int`, the declared return type of JS_NewClass. */
				[[maybe_unused]] const int res = JS_NewClass(m_jsRuntime, T::jsClassID, &def);
				assert (res == 0);
			}

			/* crust: `auto` -> `JSValue`, the declared return type of JS_NewObject. */
			JSValue proto { JS_NewObject(m_jsContext) };
			T::register_js_prototype(m_jsContext, proto);
			JS_SetClassProto(m_jsContext, T::jsClassID, proto);
		}

		/** Returns inner reference object.
		    crust: `RefT` spelled at the call site, as above. */
		template<class T, class RefT>
		static RefT* js_get_object_ref(JSValue obj)
		{
			/* quickjs-ng: JS_GetClassID no longer returns the opaque; fetch
			   it separately with JS_GetOpaque. */
			const JSClassID classID { JS_GetClassID(obj) };
			void* opaque { JS_GetOpaque(obj, T::jsClassID) };

			if (classID == T::jsClassID && opaque != nullptr)
				return static_cast<RefT*>(opaque);

			return nullptr;
		}

		/** Evaluate script. */
		JSValue js_eval(const litehtml::tstring& script);

		void			load_master_stylesheet(const tchar_t* str);
		/* crust: reference return -> pointer. */
		litehtml::css*	master_css() { return &m_master_css; }
		JSRuntime*		js_runtime() { return m_jsRuntime; }
		JSContext*		js_context() { return m_jsContext; }

		/** Register JS prototype method. */
    	static void js_register_method(JSContext* ctx, JSValue prototype, const tchar_t* name, JSCFunction func);

    	/** Register JS prototype constructor. */
    	static void js_register_constructor(JSContext* ctx, JSValue prototype, const tchar_t* name, JSCFunction func, int numArgs = 0);

		using JSGetter = JSValue(*)(JSContext*, JSValueConst);
    	using JSSetter = JSValue(*)(JSContext*, JSValueConst, JSValueConst);

    	/** Register JS protptype property. */
    	static void js_register_property(JSContext* ctx, JSValue prototype, const tchar_t* name, JSGetter getter, JSSetter setter = nullptr);

	private:

		litehtml::css	m_master_css;
		JSRuntime*		m_jsRuntime;
		JSContext*		m_jsContext;

		/** Register default classes. */
		void js_register_default_classes();
	};

}

#endif  // LH_CONTEXT_H
