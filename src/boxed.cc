
#include <girepository.h>
#include <glib.h>

#include "boxed.h"
#include "debug.h"
#include "error.h"
#include "function.h"
#include "gi.h"
#include "gobject.h"
#include "type.h"
#include "util.h"
#include "value.h"

using v8::Array;
using v8::External;
using v8::Function;
using v8::FunctionTemplate;
using v8::Isolate;
using v8::Local;
using v8::Number;
using v8::Object;
using v8::String;
using v8::Persistent;
using Nan::New;
using Nan::WeakCallbackType;

namespace GNodeJS {


NAN_PROPERTY_QUERY(boxed_property_query_handler) {
    // FIXME: implement this
    String::Utf8Value _name(property);
    info.GetReturnValue().Set(Nan::New(0));
}

NAN_PROPERTY_GETTER(boxed_property_get_handler) {
    String::Utf8Value property_name(property);
    Isolate *isolate = info.GetIsolate();

    Local<Object> boxedWrapper = info.This();
    if (boxedWrapper->InternalFieldCount() == 0) {
        Nan::ThrowError("StructFieldGetter: instance is not a boxed");
        return;
    }
    void *boxed = GNodeJS::BoxedFromWrapper(boxedWrapper);
    v8::Handle<v8::External> info_ptr = v8::Handle<v8::External>::Cast(info.Data());
    GIBaseInfo *base_info = (GIBaseInfo *)info_ptr->Value();
    if (base_info != nullptr) {
        auto fieldspec = g_struct_info_find_field(base_info, *property_name);
        if (fieldspec) {
            GIArgument value;
            if (!g_field_info_get_field(fieldspec, boxed, &value)) {
                Nan::ThrowError("Unable to get field (complex types not allowed)");
                return;
            }
            GITypeInfo *field_type = g_field_info_get_type(fieldspec);
            auto ret = GNodeJS::GIArgumentToV8(field_type, &value);
            info.GetReturnValue().Set(ret);
            g_base_info_unref(field_type);
            return;
        } else {
            if (strcmp(g_base_info_get_namespace(base_info), "GLib") == 0 && strcmp(g_base_info_get_name(base_info), "MainLoop") == 0) {
                info.GetReturnValue().Set(info.This()->GetPrototype()->ToObject()->Get(property));
                return;
            }
            auto methodspec = g_struct_info_find_method(base_info, *property_name);
            if (methodspec) {
                info.GetReturnValue().Set(GNodeJS::MakeFunction(methodspec));
                return;
            }
        }
    }
    // Fallback to defaults
    info.GetReturnValue().Set(info.This()->GetPrototype()->ToObject()->Get(property));
}

// NXT-TODO - implement this
NAN_PROPERTY_SETTER(boxed_property_set_handler) {
//    String::Utf8Value property_name(property);
    // Fallback to defaults
    info.This()->GetPrototype()->ToObject()->Set(property, value);
}

size_t Boxed::GetSize (GIBaseInfo *boxed_info) {
    GIInfoType i_type = g_base_info_get_type(boxed_info);
    if (i_type == GI_INFO_TYPE_STRUCT) {
        return g_struct_info_get_size((GIStructInfo*)boxed_info);
    } else if (i_type == GI_INFO_TYPE_UNION) {
        return g_union_info_get_size((GIUnionInfo*)boxed_info);
    } else {
        g_assert_not_reached();
    }
}

static bool IsNoArgsConstructor (GIFunctionInfo *info) {
    auto flags = g_function_info_get_flags (info);
    return ((flags & GI_FUNCTION_IS_CONSTRUCTOR) != 0
        && g_callable_info_get_n_args (info) == 0);
}

static bool IsConstructor (GIFunctionInfo *info) {
    auto flags = g_function_info_get_flags (info);
    return (flags & GI_FUNCTION_IS_CONSTRUCTOR) != 0;
}

static GIFunctionInfo* FindBoxedConstructorCached (GType gtype) {
    if (gtype == G_TYPE_NONE)
        return NULL;

    GIFunctionInfo* fn_info = (GIFunctionInfo*) g_type_get_qdata(gtype, GNodeJS::constructor_quark());

    if (fn_info != NULL)
        return fn_info;

    return NULL;
}

static GIFunctionInfo* FindBoxedConstructor (GIBaseInfo* info, GType gtype) {
    GIFunctionInfo* fn_info = NULL;

    if ((fn_info = FindBoxedConstructorCached(gtype)) != NULL)
        return g_base_info_ref (fn_info);

    if (GI_IS_STRUCT_INFO (info)) {
        int n_methods = g_struct_info_get_n_methods (info);
        for (int i = 0; i < n_methods; i++) {
            fn_info = g_struct_info_get_method (info, i);

            if (IsNoArgsConstructor (fn_info))
                break;

            g_base_info_unref(fn_info);
            fn_info = NULL;
        }

        if (fn_info == NULL)
            fn_info = g_struct_info_find_method(info, "new");

        if (fn_info == NULL) {
            for (int i = 0; i < n_methods; i++) {
                fn_info = g_struct_info_get_method (info, i);

                if (IsConstructor (fn_info))
                    break;

                g_base_info_unref(fn_info);
                fn_info = NULL;
            }
        }
    }
    else {
        int n_methods = g_union_info_get_n_methods (info);
        for (int i = 0; i < n_methods; i++) {
            fn_info = g_union_info_get_method (info, i);

            if (IsNoArgsConstructor (fn_info))
                break;

            g_base_info_unref(fn_info);
            fn_info = NULL;
        }

        if (fn_info == NULL)
            fn_info = g_union_info_find_method(info, "new");

        if (fn_info == NULL) {
            for (int i = 0; i < n_methods; i++) {
                fn_info = g_union_info_get_method (info, i);

                if (IsConstructor (fn_info))
                    break;

                g_base_info_unref(fn_info);
                fn_info = NULL;
            }
        }
    }

    if (fn_info != NULL && gtype != G_TYPE_NONE) {
        g_type_set_qdata(gtype, GNodeJS::constructor_quark(),
                g_base_info_ref (fn_info));
    }

    return fn_info;
}

static void BoxedDestroyed(const Nan::WeakCallbackInfo<Boxed> &info);

static void BoxedConstructor(const Nan::FunctionCallbackInfo<Value> &info) {
    /* See gobject.cc for how this works */
    if (!info.IsConstructCall ()) {
        Nan::ThrowTypeError("Not a construct call");
        return;
    }

    void *boxed = NULL;
    unsigned long size = 0;

    Local<Object> self = info.This ();
    GIBaseInfo *gi_info = (GIBaseInfo *) External::Cast (*info.Data ())->Value ();
    GType gtype = g_registered_type_info_get_g_type (gi_info);

    if (info[0]->IsExternal ()) {
        /* The External case. This is how WrapperFromBoxed is called. */

        boxed = External::Cast(*info[0])->Value();

    } else {
        /* User code calling `new Pango.AttrList()` */

        GIFunctionInfo* fn_info = FindBoxedConstructor(gi_info, gtype);

        if (fn_info != NULL) {

            FunctionInfo func(fn_info);
            GIArgument return_value;
            GError *error = NULL;

            auto jsResult = FunctionCall (&func, info, &return_value, &error);

            g_base_info_unref (fn_info);

            if (jsResult.IsEmpty()) {
                // func->Init() or func->TypeCheck() have thrown
                return;
            }

            if (error) {
                Throw::GError ("Boxed constructor failed", error);
                g_error_free (error);
                return;
            }

            boxed = return_value.v_pointer;

        } else if ((size = Boxed::GetSize(gi_info)) != 0) {
            boxed = g_slice_alloc0(size);

        } else {
            Nan::ThrowError("Boxed allocation failed: no constructor found");
            return;
        }

        if (!boxed) {
            Nan::ThrowError("Boxed allocation failed");
            return;
        }
    }

    self->SetAlignedPointerInInternalField (0, boxed);

    Nan::DefineOwnProperty(self,
            UTF8("__gtype__"),
            Nan::New<Number>(gtype),
            (v8::PropertyAttribute)(v8::PropertyAttribute::ReadOnly | v8::PropertyAttribute::DontEnum)
    );

    auto* box = new Boxed();
    box->data = boxed;
    box->size = size;
    box->g_type = gtype;
    box->persistent = new Nan::Persistent<Object>(self);
    box->persistent->SetWeak(box, BoxedDestroyed, Nan::WeakCallbackType::kParameter);
}

static void BoxedDestroyed(const Nan::WeakCallbackInfo<Boxed> &info) {
    Boxed *box = info.GetParameter();

    if (G_TYPE_IS_BOXED(box->g_type)) {
        g_boxed_free(box->g_type, box->data);
    }
    else if (box->size != 0) {
        // Allocated in ./function.cc @ AllocateArgument
        g_slice_free1(box->size, box->data);
    }
    else if (box->data != NULL) {
        /*
         * TODO(find informations on what to do here. Only seems to be reached for GI.Typelib)
         */
        warn("boxed possibly not freed");
    }

    delete box->persistent;
    delete box;
}


Local<FunctionTemplate> GetBoxedTemplate(GIBaseInfo *info, GType gtype) {
    void *data = NULL;

    if (gtype != G_TYPE_NONE) {
        data = g_type_get_qdata(gtype, GNodeJS::template_quark());
    }

    /*
     * Template already created
     */

    if (data) {
        Persistent<FunctionTemplate> *persistent = (Persistent<FunctionTemplate> *) data;
        Local<FunctionTemplate> tpl = Nan::New<FunctionTemplate> (*persistent);
        return tpl;
    }

    /*
     * Template not created yet
     */

    auto tpl = New<FunctionTemplate>(BoxedConstructor, New<External>(info));
    tpl->InstanceTemplate()->SetInternalFieldCount(1);

    if (gtype != G_TYPE_NONE) {
        const char *class_name = g_type_name(gtype);
        tpl->SetClassName (UTF8(class_name));
    } else {
        const char *class_name = g_base_info_get_name (info);
        tpl->SetClassName (UTF8(class_name));
    }

    v8::Handle<v8::External> info_handle = Nan::New<v8::External>((void *)g_base_info_ref(info));
    SetNamedPropertyHandler(tpl->InstanceTemplate(),
                            boxed_property_get_handler,
                            boxed_property_set_handler,
                            boxed_property_query_handler,
                            nullptr,
                            nullptr,
                            info_handle);

    if (gtype == G_TYPE_NONE)
        return tpl;

    Isolate *isolate = Isolate::GetCurrent();
    auto *persistent = new v8::Persistent<FunctionTemplate>(isolate, tpl);
    persistent->SetWeak(
            g_base_info_ref(info),
            GNodeJS::ClassDestroyed,
            WeakCallbackType::kParameter);

    g_type_set_qdata(gtype, GNodeJS::template_quark(), persistent);

    return tpl;
}

Local<Function> MakeBoxedClass(GIBaseInfo *info) {
    GType gtype = g_registered_type_info_get_g_type ((GIRegisteredTypeInfo *) info);

    if (gtype == G_TYPE_NONE) {
        auto moduleCache = GNodeJS::GetModuleCache();
        auto ns   = UTF8 (g_base_info_get_namespace (info));
        auto name = UTF8 (g_base_info_get_name (info));

        if (Nan::HasOwnProperty(moduleCache, ns).FromMaybe(false)) {
            auto module = Nan::Get(moduleCache, ns).ToLocalChecked()->ToObject();

            if (Nan::HasOwnProperty(module, name).FromMaybe(false)) {
                auto constructor = Nan::Get(module, name).ToLocalChecked()->ToObject();
                return Local<Function>::Cast (constructor);
            }
        }
    }

    Local<FunctionTemplate> tpl = GetBoxedTemplate (info, gtype);
    return tpl->GetFunction ();
}

Local<Value> WrapperFromBoxed(GIBaseInfo *info, void *data) {
    if (data == NULL)
        return Nan::Null();

    Local<Function> constructor = MakeBoxedClass (info);

    Local<Value> boxed_external = Nan::New<External> (data);
    Local<Value> args[] = { boxed_external };

    MaybeLocal<Object> instance = Nan::NewInstance(constructor, 1, args);

    // FIXME(we should propage failure here)
    if (instance.IsEmpty())
        return Nan::Null();

    return instance.ToLocalChecked();
}

void* BoxedFromWrapper(Local<Value> value) {
    Local<Object> object = value->ToObject ();
    g_assert(object->InternalFieldCount() > 0);
    void *boxed = object->GetAlignedPointerFromInternalField(0);
    return boxed;
}

};
