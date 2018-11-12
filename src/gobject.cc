
#include <string.h>

#include "boxed.h"
#include "callback.h"
#include "closure.h"
#include "debug.h"
#include "function.h"
#include "gi.h"
#include "gobject.h"
#include "macros.h"
#include "type.h"
#include "util.h"
#include "value.h"

using v8::Array;
using v8::External;
using v8::Function;
using v8::FunctionTemplate;
using v8::Local;
using v8::Number;
using v8::Object;
using v8::String;
using v8::Persistent;
using Nan::New;
using Nan::FunctionCallbackInfo;
using Nan::WeakCallbackType;

namespace GNodeJS {

// Our base template for all GObjects
static Nan::Persistent<FunctionTemplate> baseTemplate;


static void GObjectDestroyed(const v8::WeakCallbackInfo<GObject> &data);

static Local<v8::FunctionTemplate> GetObjectFunctionTemplate(GIBaseInfo *info);

static bool InitGParameterFromProperty(GParameter    *parameter,
                                       void          *klass,
                                       Local<String>  name,
                                       Local<Value>   value) {
    Nan::Utf8String name_utf8 (name);
    GParamSpec *pspec = g_object_class_find_property (G_OBJECT_CLASS (klass), *name_utf8);

    // Ignore additionnal keys in options, thus return true
    if (pspec == NULL)
        return true;

    GType value_type = G_PARAM_SPEC_VALUE_TYPE (pspec);
    parameter->name = pspec->name;
    g_value_init (&parameter->value, value_type);

    if (!CanConvertV8ToGValue(&parameter->value, value)) {
        char* message = g_strdup_printf("Cannot convert value for property \"%s\", expected type %s",
                *name_utf8, g_type_name(value_type));
        Nan::ThrowTypeError(message);
        free(message);
        return false;
    }

    if (!V8ToGValue (&parameter->value, value)) {
        char* message = g_strdup_printf("Couldn't convert value for property \"%s\", expected type %s",
                *name_utf8, g_type_name(value_type));
        Nan::ThrowTypeError(message);
        free(message);
        return false;
    }

    return true;
}

static bool InitGParametersFromProperty(GParameter    **parameters_p,
                                        int            *n_parameters_p,
                                        void           *klass,
                                        Local<Object>  property_hash) {
    Local<Array> properties = property_hash->GetOwnPropertyNames ();
    int n_parameters = properties->Length ();
    GParameter *parameters = g_new0 (GParameter, n_parameters);

    for (int i = 0; i < n_parameters; i++) {
        Local<String> name = properties->Get(i)->ToString();
        Local<Value> value = property_hash->Get (name);

        if (!InitGParameterFromProperty (&parameters[i], klass, name->ToString (), value))
            return false;
    }

    *parameters_p = parameters;
    *n_parameters_p = n_parameters;
    return true;
}

static void ToggleNotify(gpointer user_data, GObject *gobject, gboolean toggle_down) {
    void *data = g_object_get_qdata (gobject, GNodeJS::object_quark());

    g_assert (data != NULL);

    auto *persistent = (Persistent<Object> *) data;

    if (toggle_down) {
        /* We're dropping from 2 refs to 1 ref. We are the last holder. Make
         * sure that that our weak ref is installed. */
        persistent->SetWeak (gobject, GObjectDestroyed, v8::WeakCallbackType::kParameter);
    } else {
        /* We're going from 1 ref to 2 refs. We can't let our wrapper be
         * collected, so make sure that our reference is persistent */
        persistent->ClearWeak ();
    }
}

static void AssociateGObject(Isolate *isolate, Local<Object> object, GObject *gobject) {
    object->SetAlignedPointerInInternalField (0, gobject);

    g_object_ref_sink (gobject);
    g_object_add_toggle_ref (gobject, ToggleNotify, NULL);

    Persistent<Object> *persistent = new Persistent<Object>(isolate, object);
    g_object_set_qdata (gobject, GNodeJS::object_quark(), persistent);
}

static void GObjectConstructor(const FunctionCallbackInfo<Value> &info) {
    Isolate *isolate = info.GetIsolate ();

    /* The flow of this function is a bit twisty.

     * There's two cases for when this code is called:
     * user code doing `new Gtk.Widget({ ... })`, and
     * internal code as part of WrapperFromGObject, where
     * the constructor is called with one external. */
    if (!info.IsConstructCall ()) {
        log("Not a construct call.");
        Nan::ThrowTypeError("Not a construct call.");
        return;
    }

    Local<Object> self = info.This ();

    if (info[0]->IsExternal ()) {
        /* The External case. This is how WrapperFromGObject is called. */
        void *data = External::Cast (*info[0])->Value ();
        GObject *gobject = G_OBJECT (data);
        AssociateGObject (isolate, self, gobject);

        Nan::DefineOwnProperty(self,
                Nan::New<String>("__gtype__").ToLocalChecked(),
                Nan::New<Number>(G_OBJECT_TYPE(gobject)),
                (v8::PropertyAttribute)(v8::PropertyAttribute::ReadOnly | v8::PropertyAttribute::DontEnum)
        );
    } else {
        /* User code calling `new Gtk.Widget({ ... })` */

        GObject *gobject;
        GIBaseInfo *gi_info = (GIBaseInfo *) External::Cast (*info.Data ())->Value ();
        GType gtype = g_registered_type_info_get_g_type ((GIRegisteredTypeInfo *) gi_info);
        void *klass = g_type_class_ref (gtype);

        GParameter *parameters = NULL;
        int n_parameters = 0;

        if (info[0]->IsObject ()) {
            Local<Object> property_hash = info[0]->ToObject ();

            if (!InitGParametersFromProperty (&parameters, &n_parameters, klass, property_hash)) {
                // Error will already be thrown from InitGParametersFromProperty
                goto out;
            }
        }

        gobject = (GObject *) g_object_newv (gtype, n_parameters, parameters);
        AssociateGObject (isolate, self, gobject);

        Nan::DefineOwnProperty(self,
                UTF8("__gtype__"),
                Nan::New<Number>(gtype),
                (v8::PropertyAttribute)(v8::PropertyAttribute::ReadOnly | v8::PropertyAttribute::DontEnum)
        );

    out:
        g_free (parameters);
        g_type_class_unref (klass);
    }
}

static void GObjectDestroyed(const v8::WeakCallbackInfo<GObject> &data) {
    GObject *gobject = data.GetParameter ();

    void *type_data = g_object_get_qdata (gobject, GNodeJS::object_quark());
    Persistent<Object> *persistent = (Persistent<Object> *) type_data;
    delete persistent;

    /* We're destroying the wrapper object, so make sure to clear out
     * the qdata that points back to us. */
    g_object_set_qdata (gobject, GNodeJS::object_quark(), NULL);

    g_object_unref (gobject);
}

static GISignalInfo* FindSignalInfo(GIObjectInfo *info, const char *signal_detail) {
    char* signal_name = Util::GetSignalName(signal_detail);

    GISignalInfo *signal_info = NULL;

    GIBaseInfo *parent = g_base_info_ref(info);

    while (parent) {
        // Find on GObject
        signal_info = g_object_info_find_signal (parent, signal_name);
        if (signal_info)
            break;

        // Find on Interfaces
        int n_interfaces = g_object_info_get_n_interfaces (info);
        for (int i = 0; i < n_interfaces; i++) {
            GIBaseInfo* interface_info = g_object_info_get_interface (info, i);
            signal_info = g_interface_info_find_signal (interface_info, signal_name);
            g_base_info_unref (interface_info);
            if (signal_info)
                goto out;
        }

        GIBaseInfo* next_parent = g_object_info_get_parent(parent);
        g_base_info_unref(parent);
        parent = next_parent;
    }

out:

    if (parent)
        g_base_info_unref(parent);

    g_free(signal_name);

    return signal_info;
}

static void ThrowSignalNotFound(GIBaseInfo *object_info, const char* signal_name) {
    char *message = g_strdup_printf("Signal \"%s\" not found for instance of %s",
            signal_name, GetInfoName(object_info));
    Nan::ThrowError(message);
    g_free(message);
}

static void SignalConnectInternal(const Nan::FunctionCallbackInfo<v8::Value> &info, bool after) {
    GObject *gobject = GObjectFromWrapper (info.This ());

    if (!gobject) {
        Nan::ThrowTypeError("Object is not a GObject");
        return;
    }

    if (!info[0]->IsString()) {
        Nan::ThrowTypeError("Signal ID invalid");
        return;
    }

    if (!info[1]->IsFunction()) {
        Nan::ThrowTypeError("Signal callback is not a function");
        return;
    }

    const char *signal_name = *Nan::Utf8String (info[0]->ToString());
    Local<Function> callback = info[1].As<Function>();
    GType gtype = (GType) Nan::Get(info.This(), UTF8("__gtype__")).ToLocalChecked()->NumberValue();

    GIBaseInfo *object_info = g_irepository_find_by_gtype (NULL, gtype);
    GISignalInfo *signal_info = FindSignalInfo (object_info, signal_name);

    if (signal_info == NULL) {
        ThrowSignalNotFound(object_info, signal_name);
    }
    else {
        GClosure *gclosure = MakeClosure (callback, signal_info);
        ulong handler_id = g_signal_connect_closure (gobject, signal_name, gclosure, after);

        info.GetReturnValue().Set((double)handler_id);
    }

    g_base_info_unref(object_info);
}

static void SignalDisconnectInternal(const Nan::FunctionCallbackInfo<v8::Value> &info) {
    GObject *gobject = GObjectFromWrapper (info.This ());

    if (!gobject) {
        Nan::ThrowTypeError("Object is not a GObject");
        return;
    }

    if (!info[0]->IsNumber()) {
        Nan::ThrowTypeError("Signal ID should be a number");
        return;
    }

    gpointer instance = static_cast<gpointer>(gobject);
    ulong handler_id = info[0]->NumberValue();
    g_signal_handler_disconnect (instance, handler_id);

    info.GetReturnValue().Set((double)handler_id);
}

std::vector<std::string> getPropertyListForObject(const GIObjectInfo& arg_object_info) {
    std::vector<std::string> property_list = {};
    GIObjectInfo object_info(arg_object_info);
    int num_properties = g_object_info_get_n_properties(&object_info);
    for (int i = 0; i < num_properties; i++) {
        GIPropertyInfo *prop = g_object_info_get_property(&object_info, i);
        property_list.push_back(Util::snakeCaseToCamelCase(Util::hyphenCaseToSnakeCase(g_base_info_get_name(prop))));
        g_base_info_unref(prop);
    }
    int num_methods = g_object_info_get_n_methods(&object_info);
    for (int i = 0; i < num_methods; i++) {
        GIFunctionInfo *prop = g_object_info_get_method(&object_info, i);
        property_list.push_back(Util::snakeCaseToCamelCase(Util::hyphenCaseToSnakeCase(g_base_info_get_name(prop))));
        g_base_info_unref(prop);
    }
    int num_constants = g_object_info_get_n_constants(&object_info);
    for (int i = 0; i < num_constants; i++) {
        GIConstantInfo *prop = g_object_info_get_constant(&object_info, i);
        property_list.push_back(Util::snakeCaseToCamelCase(Util::hyphenCaseToSnakeCase(g_base_info_get_name(prop))));
        g_base_info_unref(prop);
    }

    int num_interfaces = g_object_info_get_n_interfaces(&object_info);
    for (int i = 0; i < num_interfaces; i++) {
        GIInterfaceInfo *interface_info = g_object_info_get_interface(&object_info, i);

        int num_properties = g_interface_info_get_n_properties(interface_info);
        for (int i = 0; i < num_properties; i++) {
            GIPropertyInfo *prop = g_interface_info_get_property(interface_info, i);
            property_list.push_back(Util::snakeCaseToCamelCase(Util::hyphenCaseToSnakeCase(g_base_info_get_name(prop))));
            g_base_info_unref(prop);
        }
        int num_methods = g_interface_info_get_n_methods(interface_info);
        for (int i = 0; i < num_methods; i++) {
            GIFunctionInfo *prop = g_interface_info_get_method(interface_info, i);
            property_list.push_back(Util::snakeCaseToCamelCase(Util::hyphenCaseToSnakeCase(g_base_info_get_name(prop))));
            g_base_info_unref(prop);
        }
        int num_constants = g_interface_info_get_n_constants(interface_info);
        for (int i = 0; i < num_constants; i++) {
            GIConstantInfo *prop = g_interface_info_get_constant(interface_info, i);
            property_list.push_back(Util::snakeCaseToCamelCase(Util::hyphenCaseToSnakeCase(g_base_info_get_name(prop))));
            g_base_info_unref(prop);
        }
        g_base_info_unref(interface_info);
    }

    GIObjectInfo* parent_info = g_object_info_get_parent(&object_info);
    if (parent_info != NULL) {
        auto parent_property_list = getPropertyListForObject(*parent_info);
        property_list.insert(property_list.end(), std::make_move_iterator(parent_property_list.begin()), std::make_move_iterator(parent_property_list.end()));
    }

    return property_list;
}

GIFunctionInfo* g_object_info_find_method_recursive(GIObjectInfo* object_info, const char* property_name) {
    GIFunctionInfo* function_info = g_object_info_find_method(object_info, property_name);
    if (function_info == nullptr) {
        GIObjectInfo* parent_info = g_object_info_get_parent(object_info);
        if (parent_info != nullptr) {
            function_info = g_object_info_find_method_recursive(parent_info, property_name);
            g_base_info_unref(parent_info);
        }
    }
    return function_info;
}

GIPropertyInfo* g_object_info_find_property(GIObjectInfo* object_info, const char *property_name) {
    int num_properties = g_object_info_get_n_properties(object_info);
    for (int i = 0; i < num_properties; i++) {
        GIPropertyInfo *prop = g_object_info_get_property(object_info, i);
        if (strcmp(g_base_info_get_name(prop), property_name) == 0) {
            return prop;
        }
        g_base_info_unref(prop);
    }
    return nullptr;
}

GIPropertyInfo* g_object_info_find_property_recursive(GIObjectInfo* object_info, const char* property_name) {
    GIPropertyInfo* property_info = g_object_info_find_property(object_info, property_name);
    if (property_info == nullptr) {
        GIObjectInfo* parent_info = g_object_info_get_parent(object_info);
        if (parent_info != nullptr) {
            property_info = g_object_info_find_property_recursive(parent_info, property_name);
            g_base_info_unref(parent_info);
        }
    }
    return property_info;
}

NAN_METHOD(SignalConnect) {
    SignalConnectInternal(info, false);
}

NAN_METHOD(SignalDisconnect) {
    SignalDisconnectInternal(info);
}

NAN_METHOD(GObjectToString) {
    Local<Object> self = info.This();

    if (!ValueHasInternalField(self)) {
        Nan::ThrowTypeError("Object is not a GObject");
        return;
    }

    GObject* g_object = GObjectFromWrapper(self);
    GType type = G_OBJECT_TYPE (g_object);

    const char* typeName = g_type_name(type);
    char *className = *Nan::Utf8String(self->GetConstructorName());
    void *address = self->GetAlignedPointerFromInternalField(0);

    char *str = g_strdup_printf("[%s:%s %#zx]", typeName, className, (unsigned long)address);

    info.GetReturnValue().Set(UTF8(str));
    g_free(str);
}
NAN_PROPERTY_ENUMERATOR(property_enumerator_handler) {
    v8::Handle<v8::External> info_ptr = v8::Handle<v8::External>::Cast(info.Data());
    GIBaseInfo *base_info = (GIBaseInfo *)info_ptr->Value();
    if (base_info == NULL) {
        info.GetReturnValue().Set(Nan::New<v8::Array>());
        return;
    }
    // TODO: Cache this!
    auto property_list = getPropertyListForObject(*base_info);
    std::sort(property_list.begin(), property_list.end());
    auto v8_property_list = Nan::New<v8::Array>(property_list.size());
    int i = 0;
    for(std::string property_name : property_list) {
        Nan::Set(v8_property_list, i, UTF8(property_name));
        i++;
    }
    info.GetReturnValue().Set(v8_property_list);
}

NAN_PROPERTY_QUERY(property_query_handler) {
    // FIXME: implement this
    String::Utf8Value _name(info.GetIsolate(), property);
    info.GetReturnValue().Set(Nan::New(0));
}

NAN_PROPERTY_GETTER(property_get_handler) {
    String::Utf8Value property_name_v8(info.GetIsolate(), property);
    if (*property_name_v8) {
        std::string property_name = Util::camelCaseToSnakeCase(*property_name_v8);
        v8::Handle<v8::External> info_ptr = v8::Handle<v8::External>::Cast(info.Data());
        GIObjectInfo* object_info = (GIObjectInfo*)info_ptr->Value();
        if (object_info != NULL) {
            /*if (strcmp(*property_name_v8, "constructor") == 0) {
                v8::Handle<v8::External> info_ptr = v8::Handle<v8::External>::Cast(info.Data());
                GIObjectInfo* object_info = (GIObjectInfo*)info_ptr->Value();
                log("constructor: %s", g_base_info_get_name(object_info));
                auto tpl = GetObjectFunctionTemplate(object_info);
                //Local<Function> constructor = tpl->GetFunction();
                //Local<Value> args[] = { info_ptr };
                //Local<Object> obj = Nan::NewInstance(constructor, 1, args).ToLocalChecked();
                info.GetReturnValue().Set(tpl->GetFunction());
                return;
            }*/
            if (strcmp(*property_name_v8, "__gtype__") == 0) {
                GObject *gobject = GNodeJS::GObjectFromWrapper(info.This()->ToObject());
                info.GetReturnValue().Set(Nan::New<Number>(G_OBJECT_TYPE(gobject)));
                return;
            }
            // TODO: Refactor
            GIPropertyInfo* property_info = g_object_info_find_property_recursive(object_info, property_name.c_str());
            if (property_info != NULL) {
                GObject *gobject = GNodeJS::GObjectFromWrapper(info.This()->ToObject());
                if (gobject != NULL) {
                    GParamSpec *param_spec = g_object_class_find_property(G_OBJECT_GET_CLASS(gobject), property_name.c_str());
                    if (param_spec) {
                        if (!(param_spec->flags & G_PARAM_READABLE)) {
                            info.GetReturnValue().Set(Nan::Undefined());
                            return;
                        }
                        GType value_type = G_TYPE_FUNDAMENTAL(param_spec->value_type);
                        GValue gvalue = {0, {{0}}};
                        g_value_init(&gvalue, param_spec->value_type);
                        g_object_get_property(gobject, property_name.c_str(), &gvalue);
                        Local<Value> v8_value = GNodeJS::GValueToV8(&gvalue);
                        if (value_type != G_TYPE_OBJECT && value_type != G_TYPE_BOXED) {
                            g_value_unset(&gvalue);
                        }
                        info.GetReturnValue().Set(v8_value);
                        return;
                    }
                }
            }

            GIFunctionInfo* function_info = g_object_info_find_method_recursive(object_info, property_name.c_str());
            if (function_info != NULL) {
                info.GetReturnValue().Set(GNodeJS::MakeFunction(function_info));
                return;
            }

        }
    }
    //log("default %s", *property_name_v8);
    info.GetReturnValue().Set(info.This()->GetPrototype()->ToObject()->Get(property));
}

NAN_PROPERTY_SETTER(property_set_handler) {
    String::Utf8Value property_name(info.GetIsolate(), property);
    if (strcmp(*property_name, "constructor") != 0) {
        GObject *gobject = GNodeJS::GObjectFromWrapper (info.This()->ToObject());
        if (gobject != NULL) {
            GParamSpec *pspec = g_object_class_find_property(G_OBJECT_GET_CLASS(gobject), *property_name);
            if (pspec) {
                // Property is not readable
                if (!(pspec->flags & G_PARAM_WRITABLE)) {
                    Nan::ThrowTypeError("property is not writable");
                }
                GValue gvalue = {};
                g_value_init(&gvalue, G_PARAM_SPEC_VALUE_TYPE (pspec));
                if (GNodeJS::V8ToGValue (&gvalue, value)) {
                    g_object_set_property (gobject, *property_name, &gvalue);
                    RETURN(Nan::True());
                } else {
                    Nan::ThrowError("ObjectPropertySetter: could not convert value");
                    RETURN(Nan::False());
                }
            }
        }
    }
    // Fallback to defaults
    info.This()->GetPrototype()->ToObject()->Set(property, value);
}

Local<FunctionTemplate> GetObjectFunctionRootTemplate() {
    static bool isBaseClassCreated = false;
    if (!isBaseClassCreated) {
        isBaseClassCreated = true;
        Local<FunctionTemplate> tpl = Nan::New<FunctionTemplate>();
        Nan::SetPrototypeMethod(tpl, "connect", SignalConnect);
        Nan::SetPrototypeMethod(tpl, "disconnect", SignalDisconnect);
        Nan::SetPrototypeMethod(tpl, "toString", GObjectToString);
        baseTemplate.Reset(tpl);
    }
    Local<FunctionTemplate> tpl = Nan::New(baseTemplate);
    return tpl;
}

static Local<FunctionTemplate> NewFunctionTemplate (GIBaseInfo *info, GType gtype) {
    g_assert(gtype != G_TYPE_NONE);

    const char *class_name = g_type_name (gtype);

    auto tpl = New<FunctionTemplate> (GObjectConstructor, New<External> (info));
    tpl->SetClassName (UTF8(class_name));
    tpl->InstanceTemplate()->SetInternalFieldCount(1);

    v8::Handle<v8::External> info_handle = Nan::New<v8::External>((void *)g_base_info_ref(info));
    Nan::SetNamedPropertyHandler(tpl->InstanceTemplate(),
                            property_get_handler,
                            property_set_handler,
                            property_query_handler,
                            nullptr,
                            property_enumerator_handler,
                            info_handle);

    // This is so we can access static methods, not sure if there are static properties to add...
    int num_methods = g_object_info_get_n_methods(info);
    for (int i = 0; i < num_methods; i++) {
        GIFunctionInfo *method_info = g_object_info_get_method(info, i);
        auto method_name = Util::snakeCaseToCamelCase(Util::hyphenCaseToSnakeCase(g_base_info_get_name(method_info)));
        Nan::SetTemplate(tpl, method_name.c_str(), GNodeJS::MakeFunctionTemplate(method_info));
        g_base_info_unref(method_info);
    }

    GIObjectInfo *parent_info = g_object_info_get_parent(info);
    if (parent_info) {
        auto parent_tpl = GetObjectFunctionTemplate((GIBaseInfo *) parent_info);
        tpl->Inherit(parent_tpl);
    } else {
        tpl->Inherit(GetObjectFunctionRootTemplate());
    }

    return tpl;
}

static Local<v8::FunctionTemplate> GetObjectFunctionTemplate(GIBaseInfo *gi_info) {
    GType gtype = g_registered_type_info_get_g_type ((GIRegisteredTypeInfo *) gi_info);
/*    void *data = g_type_get_qdata (gtype, GNodeJS::template_quark());
    if (data) {
        auto *persistent = (Persistent<FunctionTemplate> *) data;
        auto tpl = New<FunctionTemplate> (*persistent);
        return tpl;
    }*/
    if (gi_info == NULL) {
        gi_info = g_irepository_find_by_gtype(NULL, gtype);
    }
    assert_printf (gi_info != NULL, "Missing GIR info for: %s\n", g_type_name (gtype));
    auto tpl = NewFunctionTemplate(gi_info, gtype);

    auto *persistent = new Persistent<FunctionTemplate>(Isolate::GetCurrent(), tpl);
    persistent->SetWeak(
            g_base_info_ref(gi_info),
            GNodeJS::ClassDestroyed,
            WeakCallbackType::kParameter);
    //g_type_set_qdata(gtype, GNodeJS::template_quark(), persistent);
    return tpl;
}

Local<Object> MakeClass(GIBaseInfo *info) {
    //log("MakeClass %s", g_base_info_get_name(info));
    auto tpl = GetObjectFunctionTemplate(info);
    return Nan::GetFunction(tpl).ToLocalChecked();
}

Local<Value> WrapperFromGObject(GObject *gobject, GIBaseInfo *object_info) {
    //log("WrapperFromGObject %s", g_base_info_get_name(object_info));
    if (gobject == NULL)
        return Nan::Null();

    void *data = g_object_get_qdata (gobject, GNodeJS::object_quark());

    if (data) {
        /* Easy case: we already have an object. */
        auto *persistent = (Persistent<Object> *) data;
        auto obj = New<Object> (*persistent);
        return obj;

    } else {
        auto tpl = GetObjectFunctionTemplate(object_info);
        Local<Function> constructor = tpl->GetFunction();
        Local<Value> gobject_external = New<External> (gobject);
        Local<Value> args[] = { gobject_external };
        Local<Object> obj = Nan::NewInstance(constructor, 1, args).ToLocalChecked();

        return obj;
    }
}

GObject * GObjectFromWrapper(Local<Value> value) {
    if (!ValueHasInternalField(value))
        return nullptr;

    Local<Object> object = value->ToObject ();

    void    *ptr     = object->GetAlignedPointerFromInternalField (0);
    GObject *gobject = G_OBJECT (ptr);
    return gobject;
}

};
