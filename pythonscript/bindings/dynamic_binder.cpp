#include <cstring>

// Pythonscript imports
#include "bindings/dynamic_binder.h"
#include "bindings/builtins_binder/atomic.h"
#include "bindings/tools.h"


// Generate a python function calling `callback` with data as first
// parameter, usefull for dynamically create the binding functions
static mp_obj_t _generate_custom_trampoline(mp_obj_t callback, mp_obj_t data) {
    // Hold my beer...

    auto compile = [](const char *src) -> mp_obj_t {
        mp_lexer_t *lex = mp_lexer_new_from_str_len(MP_QSTR__lt_stdin_gt_, src, strlen(src), false);
        mp_parse_tree_t pt = mp_parse(lex, MP_PARSE_SINGLE_INPUT);
        mp_obj_t fun = mp_compile(&pt, lex->source_name, MP_EMIT_OPT_NONE, true);
        return fun;
    };

    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        // First thing to do: compile this code...
        const char src[] = "" \
            "def builder():\n" \
            "    cb = __global_cb\n" \
            "    data = __global_data\n" \
            "    def trampoline(*args):\n" \
            "        return cb(data, *args)\n" \
            "    return trampoline\n";
        mp_obj_t gen_fun = compile(src);

        const qstr cb_name = qstr_from_str("__global_cb");
        const qstr data_name = qstr_from_str("__global_data");
        const qstr builder_name = qstr_from_str("builder");

        // Now execute the code to create a builder function in our scope
        // Note that given we are extra-confident, we don't even bother to
        // set up a nlr context to catch exceptions...
        mp_call_function_n_kw(gen_fun, 0, 0, NULL);

        // Now set the configuration through global variables,
        // then call the builder to retrieve our custom trampoline !
        mp_store_name(cb_name, callback);
        mp_store_name(data_name, data);
        auto builder = mp_load_name(builder_name);
        auto trampoline = mp_call_function_n_kw(builder, 0, 0, NULL);

        // Don't forget to clean the scope before leaving
        mp_delete_name(cb_name);
        mp_delete_name(data_name);
        mp_delete_name(builder_name);

        return trampoline;
    } else {
        mp_obj_print_exception(&mp_plat_print, (mp_obj_t)nlr.ret_val);
        // uncaught exception
        return mp_const_none;
    }
}


static mp_obj_t _wrap_godot_method(StringName type_name, StringName method_name) {
    // TODO: micropython doesn't allow to store a name for native functions
    // TODO: don't use `m_new_obj` but good old' `malloc` to avoid useless
    // python gc work on those stay-forever functions
    auto p_method_bind = ClassDB::get_method(type_name, method_name);
    // It seems methods starting with "_" are considered private so ignore them
    if (!p_method_bind) {
        WARN_PRINTS("--- Bad Binding " + String(type_name) + ":" + String(method_name));
        return mp_const_none;
    // } else {
    //     WARN_PRINTS("+++ Good Binding " + String(type_name) + ":" + String(method_name));
    }

    // Define the wrapper function that is responsible to:
    // - Convert arguments to python obj
    // - Call the godot method from p_method_bind pointer passed as first argument
    // - Convert back result to Variant
    // - Handle call errors as python exceptions
    auto caller_fun = m_new_obj(mp_obj_fun_builtin_var_t);
    caller_fun->base.type = &mp_type_fun_builtin_var;
    caller_fun->is_kw = false;
    // Godot doesn't count self as an argument but python does
    // we will also be passed `p_method_bind` by the trampoline caller
    caller_fun->n_args_min = p_method_bind->get_argument_count() + 2;
    caller_fun->n_args_max = p_method_bind->get_argument_count() + 2;
    caller_fun->fun.var = [](size_t n, const mp_obj_t *args) -> mp_obj_t {
        // First arg is the p_method_bind
        auto p_method_bind = static_cast<MethodBind *>(args[0]);
        auto self = static_cast<DynamicBinder::mp_godot_bind_t *>(MP_OBJ_TO_PTR(args[1]));
        // Remove self and also don't pass p_method_bind as argument
        const int godot_n = n - 2;
        const Variant def_arg;
        const Variant *godot_args[godot_n];
        auto bindings = GodotBindingsModule::get_singleton();
        for (int i = 0; i < godot_n; ++i) {
            godot_args[i] = new Variant(bindings->pyobj_to_variant(args[i+2]));
        }
        Variant::CallError err;
        Variant ret = p_method_bind->call(self->godot_obj, godot_args, godot_n, err);
        for (int i = 0; i < godot_n; ++i) {
            delete godot_args[i];
        }
        if (err.error != Variant::CallError::CALL_OK) {
            // Throw exception
            // TODO: improve error message...
            nlr_raise(mp_obj_new_exception_msg(&mp_type_RuntimeError, "Tough shit dude..."));
        }
        return bindings->variant_to_pyobj(ret);
    };

    // Yes, p_method_bind is not an mp_obj_t... but it's only to pass to caller_fun
    auto trampoline = _generate_custom_trampoline(caller_fun, static_cast<mp_obj_t>(p_method_bind));

    return trampoline;
}


static mp_obj_t _wrap_godot_property_getter(StringName *property_name) {
    auto caller_fun = m_new_obj(_mp_obj_fun_builtin_fixed_t);
    caller_fun->base.type = &mp_type_fun_builtin_2;
    caller_fun->fun._2 = [](mp_obj_t mp_name, mp_obj_t mp_self) -> mp_obj_t {
        auto p_name = static_cast<StringName*>(mp_name);
        auto self = static_cast<DynamicBinder::mp_godot_bind_t *>(MP_OBJ_TO_PTR(mp_self));
        Variant ret;
        if (!ClassDB::get_property(self->godot_obj, *p_name, ret)) {
            nlr_raise(mp_obj_new_exception_msg(&mp_type_RuntimeError, "Tough shit dude..."));
        }
        return GodotBindingsModule::get_singleton()->variant_to_pyobj(ret);
    };
    auto trampoline = _generate_custom_trampoline(caller_fun, static_cast<mp_obj_t>(property_name));
    return trampoline;
}


static mp_obj_t _wrap_godot_property_setter(StringName *property_name) {
    auto caller_fun = m_new_obj(_mp_obj_fun_builtin_fixed_t);
    caller_fun->base.type = &mp_type_fun_builtin_3;
    caller_fun->fun._3 = [](mp_obj_t mp_name, mp_obj_t mp_self, mp_obj_t mp_value) -> mp_obj_t {
        auto p_name = static_cast<StringName*>(mp_name);
        auto self = static_cast<DynamicBinder::mp_godot_bind_t *>(MP_OBJ_TO_PTR(mp_self));
        auto value = GodotBindingsModule::get_singleton()->pyobj_to_variant(mp_value);
        bool valid;
        if (!ClassDB::set_property(self->godot_obj, *p_name, value, &valid)) {
            char buff[64];
            snprintf(buff, sizeof(buff), "'%s' has no attribute '%s'",
                     self->godot_obj->get_class().utf8().get_data(), String(*p_name).utf8().get_data());
            nlr_raise(mp_obj_new_exception_msg(&mp_type_AttributeError, buff));
        } else if (!valid) {
            nlr_raise(mp_obj_new_exception_msg(&mp_type_RuntimeError, "Tough shit dude..."));
        }
        return mp_const_none;
    };
    auto trampoline = _generate_custom_trampoline(caller_fun, static_cast<mp_obj_t>(property_name));
    return trampoline;
}


static mp_obj_t _type_make_new(const mp_obj_type_t *type, mp_uint_t n_args, mp_uint_t n_kw, const mp_obj_t *args) {
    auto p_type_binder = static_cast<const DynamicBinder *>(type->protocol);
    // TODO: Optimize this by using TypeInfo::creation_func ?
    // TODO: Handle constructor's parameters
    Object *godot_obj = ClassDB::instance(p_type_binder->get_type_name());
    DynamicBinder::mp_godot_bind_t *obj = m_new_obj_with_finaliser(DynamicBinder::mp_godot_bind_t);
    obj->base.type = type;
    obj->godot_obj = godot_obj;
    obj->godot_variant = Variant(godot_obj);
    return MP_OBJ_FROM_PTR(obj);
}


static mp_obj_t _binary_op(mp_uint_t op, mp_obj_t lhs_in, mp_obj_t rhs_in) {
    const auto self = static_cast<DynamicBinder::mp_godot_bind_t *>(MP_OBJ_TO_PTR(lhs_in));
    if (op == MP_BINARY_OP_EQUAL && mp_obj_get_type(rhs_in) == self->base.type) {
        const auto other = static_cast<DynamicBinder::mp_godot_bind_t *>(MP_OBJ_TO_PTR(rhs_in));
        return mp_obj_new_bool(self->godot_obj == other->godot_obj);
    }
    // op not supported
    return MP_OBJ_NULL;
}


#if 0
static void _mp_type_attr(mp_obj_t self_in, qstr attr, mp_obj_t *dest) {
    auto obj = static_cast<mp_godot_bind_t *>(MP_OBJ_TO_PTR(self_in));
    auto p_type_binder = static_cast<const DynamicBinder *>(obj->base.type->protocol);
    p_type_binder->get_attr(self_in, attr, dest);
}


void DynamicBinder::get_attr(mp_obj_t self_in, qstr attr, mp_obj_t *dest) const {
    // Try to retrieve the attr as a method
    auto E = this->method_lookup.find(attr);
    if (E != NULL) {
        // attr is a method
        if (dest[0] == MP_OBJ_NULL) {
            // do a load
            dest[0] = E->get();
            dest[1] = self_in;
        }
#if 0
    } else {
        // TODO
        // We consider attr is a property
        if (dest[0] == MP_OBJ_NULL) {
            // do a load
            Variant r_value;
            if (ClassDB::get_property(o->godot_obj->ptr(), attr_name, r_value)) {
                // attr was actually a property
                // TODO:convert r_value to py object
                dest[0] = mp_const_none;
            }
        } else if (dest[1] != MP_OBJ_NULL) {
            // do a store
            bool r_valid = false;
            // TODO: convert dest[1] to variant
            Variant value = Variant(true);
            if (ClassDB::set_property(o->godot_obj->ptr(), attr_name, value, r_valid) && r_valid) {
                // attr was actually a property
                dest[0] = MP_OBJ_NULL;
            }
        }
#endif
    }
    // All other cases fail (i.e. don't modify `dest`)
}
#endif


mp_obj_t DynamicBinder::build_pyobj() const {
    return this->build_pyobj(NULL);
}


mp_obj_t DynamicBinder::build_pyobj(Object *obj) const {
    mp_godot_bind_t *py_obj = m_new_obj_with_finaliser(mp_godot_bind_t);
    py_obj->base.type = this->get_mp_type();
    py_obj->godot_obj = obj;
    py_obj->godot_variant = Variant(obj);
    return MP_OBJ_FROM_PTR(py_obj);
}


Variant DynamicBinder::pyobj_to_variant(mp_obj_t pyobj) const {
    mp_obj_type_t *pyobj_type = mp_obj_get_type(pyobj);
    if (pyobj_type->name == this->get_type_qstr()) {
        auto p_obj = static_cast<mp_godot_bind_t *>(MP_OBJ_TO_PTR(pyobj));
        return p_obj->godot_variant;
    } else {
        return Variant();
    }
}


mp_obj_t DynamicBinder::variant_to_pyobj(const Variant &p_variant) const {
    Object *obj = p_variant;
    if (obj != NULL) {
        return this->build_pyobj(obj);
    } else {
        return mp_const_none;
    }
}

DynamicBinder::~DynamicBinder() {
    for(auto *E=this->property_lookup.front();E;E=E->next()) {
        memdelete(E->value());
    }

}


DynamicBinder::DynamicBinder(StringName type_name) {
    this->_type_name = type_name;
    // Retrieve method, property & constants from ClassDB and cook what
    // can be for faster runtime lookup
    List<PropertyInfo> properties;
    List<MethodInfo> methods;
    List<String> constants;

    // TODO: bypass micropython memory allocation
    ClassDB::get_property_list(type_name, &properties, true);
    ClassDB::get_method_list(type_name, &methods, true);
    ClassDB::get_integer_constant_list(type_name, &constants, true);
    mp_obj_t locals_dict = mp_obj_new_dict(methods.size() + properties.size() + constants.size());

    // TODO: get inherited properties/methods as well ?
    for(List<PropertyInfo>::Element *E=properties.front();E;E=E->next()) {
        const PropertyInfo info = E->get();
        const auto qstr_name = qstr_from_str(info.name.utf8().get_data());
        auto name = memnew(StringName(info.name));
        this->property_lookup.insert(qstr_name, name);
        auto g = _wrap_godot_property_getter(name);
        auto s = _wrap_godot_property_setter(name);
        mp_obj_t property = mp_call_function_2(MP_OBJ_FROM_PTR(&mp_type_property), g, s);
        mp_obj_dict_store(locals_dict, MP_OBJ_NEW_QSTR(qstr_name), property);
    }
    for(List<MethodInfo>::Element *E=methods.front();E;E=E->next()) {
        const MethodInfo info = E->get();
        const auto qstr_name = qstr_from_str(info.name.utf8().get_data());
        const auto mpo_method = _wrap_godot_method(type_name, info.name);
        if (mpo_method != mp_const_none) {
            this->method_lookup.insert(qstr_name, mpo_method);
            mp_obj_dict_store(locals_dict, MP_OBJ_NEW_QSTR(qstr_name), mpo_method);
        }
    }
    const auto int_binder = IntBinder::get_singleton();
    for(List<String>::Element *E=constants.front();E;E=E->next()) {
        const String name = E->get();
        const auto qstr_name = qstr_from_str(name.utf8().get_data());
        mp_obj_t val = int_binder->build_pyobj(ClassDB::get_integer_constant(type_name, name));
        mp_obj_dict_store(locals_dict, MP_OBJ_NEW_QSTR(qstr_name), val);
    }

    // Retrieve parent binding to create inheritance between bindings
    mp_obj_t bases_tuple = 0;
    const StringName parent_name = ClassDB::get_parent_class(type_name);
    if (parent_name != StringName()) {
        const BaseBinder *parent_binder = GodotBindingsModule::get_singleton()->get_binder(parent_name);
        if (parent_binder != NULL) {
            const mp_obj_t args[1] = {
                MP_OBJ_FROM_PTR(parent_binder->get_mp_type()),
            };
            bases_tuple = mp_obj_new_tuple(1, args);
        } else {
            WARN_PRINTS("Cannot retrieve `" + String(type_name) + "`'s parent `" + String(parent_name) + "`");
        }
    }
    const String s_name = String(type_name);
    qstr name = qstr_from_str(s_name.utf8().get_data());
    // TODO: handle inheritance with bases_tuple
    this->_mp_type = {
        { &mp_type_type },                        // base
        name,                                     // name
        0,                                        // print
        _type_make_new,                           // make_new
        0,                                        // call
        0,                                        // unary_op
        _binary_op,                               // binary_op
        attr_with_locals_and_properties,          // attr
        0,                                        // subscr
        0,                                        // getiter
        0,                                        // iternext
        {0},                                      // buffer_p
        static_cast<void *>(this),                // protocol
        static_cast<mp_obj_tuple_t *>(MP_OBJ_TO_PTR(bases_tuple)),  // bases_tuple
        static_cast<mp_obj_dict_t *>(MP_OBJ_TO_PTR(locals_dict))    // locals_dict
    };
    this->_p_mp_type = &this->_mp_type;
}
