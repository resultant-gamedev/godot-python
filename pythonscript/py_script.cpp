// Godot imports
#include "core/os/file_access.h"
// Pythonscript imports
#include "py_script.h"
#include "py_instance.h"


void PyScript::_bind_methods() {
    DEBUG_TRACE();
    // TODO: bind class methods here
    // ClassDB::bind_native_method(METHOD_FLAGS_DEFAULT, "new", &PyScript::_new, MethodInfo(Variant::OBJECT, "new"));
    // ClassDB::bind_method(_MD("get_as_byte_code"), &PyScript::get_as_byte_code);
}


#ifdef TOOLS_ENABLED


void PyScript::_placeholder_erased(PlaceHolderScriptInstance *p_placeholder) {
    DEBUG_TRACE_METHOD();
    placeholders.erase(p_placeholder);
}

#endif


bool PyScript::can_instance() const {
    DEBUG_TRACE_METHOD_ARGS((this->valid && this->_mpo_exposed_class != mp_const_none ? " true" : " false"));
    // TODO: think about it...
    // Only script file defining an exposed class can be instanciated
    return this->valid && this->_mpo_exposed_class != mp_const_none;
    // return valid; //script can instance
    // return this->valid || (!this->tool && !ScriptServer::is_scripting_enabled());
}


Ref<Script> PyScript::get_base_script() const {
    DEBUG_TRACE_METHOD();
    if (this->base.ptr()) {
        return Ref<PyScript>(this->base);
    } else {
        return Ref<Script>();
    }
}


StringName PyScript::get_instance_base_type() const {
    DEBUG_TRACE_METHOD();
    // if (this->native.is_valid())
    //     return this->native->get_name();
    if (this->base.is_valid())
        return this->base->get_instance_base_type();
    return StringName();
}


// TODO: rename p_this "p_owner" ?
ScriptInstance* PyScript::instance_create(Object *p_this) {
    DEBUG_TRACE_METHOD();
    if (!this->tool && !ScriptServer::is_scripting_enabled()) {
#ifdef TOOLS_ENABLED
        //instance a fake script for editing the values
        //plist.invert();

        /*print_line("CREATING PLACEHOLDER");
        for(List<PropertyInfo>::Element *E=plist.front();E;E=E->next()) {
            print_line(E->get().name);
        }*/
        PlaceHolderScriptInstance *si = memnew(PlaceHolderScriptInstance(PyLanguage::get_singleton(), Ref<Script>(this), p_this));
        placeholders.insert(si);
        //_update_placeholder(si);
        // _update_exports();
        return si;
#else
        return NULL;
#endif
    }

    PyScript *top = this;
    while(top->base.ptr())
        top = top->base.ptr();

    // if (top->native.is_valid()) {
    //     if (!ClassDB::is_type(p_this->get_type_name(),top->native->get_name())) {
    //         if (ScriptDebugger::get_singleton()) {
    //             PyLanguage::get_singleton()->debug_break_parse(get_path(),0,"Script inherits from native type '"+String(top->native->get_name())+"', so it can't be instanced in object of type: '"+p_this->get_type()+"'");
    //         }
    //         ERR_EXPLAIN("Script inherits from native type '"+String(top->native->get_name())+"', so it can't be instanced in object of type: '"+p_this->get_type()+"'");
    //         ERR_FAIL_V(NULL);
    //     }
    // }
    // Variant::CallError unchecked_error;
    // return _create_instance(NULL,0,p_this,p_this->cast_to<Reference>(),unchecked_error);
    // TODO !!!!

    PyInstance* instance = memnew(PyInstance);
    const bool success = instance->init(this, p_this);
    if (success) {
        this->_instances.insert(instance->get_owner());
        return instance;
    } else {
        memdelete(instance);
        ERR_FAIL_V(NULL);
    }
}


bool PyScript::instance_has(const Object *p_this) const {
    DEBUG_TRACE_METHOD();
    return this->_instances.has((Object*)p_this);
}


bool PyScript::has_source_code() const {
    DEBUG_TRACE_METHOD();
    return this->source != "";
}


String PyScript::get_source_code() const {
    DEBUG_TRACE_METHOD();
    return this->source;
}


void PyScript::set_source_code(const String& p_code) {
    DEBUG_TRACE_METHOD();
    if (this->source == p_code)
        return;
    this->source = p_code;
// #ifdef TOOLS_ENABLED
//     source_changed_cache = true;
//     //print_line("SC CHANGED "+get_path());
// #endif
}


static const String _to_mp_module_path(const String &p_path) {
    ERR_EXPLAIN("Bad python script path, must starts by `res://` and ends with `.py`");
    ERR_FAIL_COND_V(!p_path.begins_with("res://") || !p_path.ends_with(".py"), String());
    return p_path.substr(6, p_path.length() - 6 - 3).replace("/", ".");
}


Error PyScript::reload(bool p_keep_state) {
    DEBUG_TRACE_METHOD();
    ERR_FAIL_COND_V(!p_keep_state && this->_instances.size(), ERR_ALREADY_IN_USE);

    this->valid = false;
    String basedir = this->path;

    if (basedir=="")
        basedir=this->get_path();

    if (basedir!="")
        basedir=basedir.get_base_dir();

    // Retrieve the module path in python format from the ressource path
    const String mp_module_path = _to_mp_module_path(this->path);
    ERR_FAIL_COND_V(!mp_module_path.length(), ERR_FILE_BAD_PATH);

    // Load the module into micropython
    // TODO: mp_execute_expr should return error with traceback ?
    // const mp_obj_t ret = mp_execute_expr((String("import ") + mp_module_path).ascii().get_data());
    // ERR_FAIL_COND_V(ret != mp_const_none, ERR_FILE_UNRECOGNIZED);

    mp_obj_t error = 0;
    qstr qstr_module_path = qstr_from_str(mp_module_path.ascii().get_data());
    auto import_module = [this, &qstr_module_path]() {
        // TODO handle deep path for module (e.g. `import foo.bar`)
        this->_mpo_module = mp_import_name(qstr_module_path, mp_const_none, MP_OBJ_NEW_SMALL_INT(0));
        mp_store_global(qstr_module_path, this->_mpo_module);
        this->valid = true;
    };
    auto handle_ex = [&error](mp_obj_t ex) {
        mp_obj_print_exception(&mp_plat_print, ex);
        error = ex;
    };
    MP_WRAP_CALL_EX(import_module, handle_ex);
    ERR_FAIL_COND_V(error, ERR_COMPILATION_FAILED);

    // Retrieve module's exposed class or set it to `mp_const_none` if not available
    this->_mpo_exposed_class = PyLanguage::get_singleton()->get_mp_exposed_class_from_module(qstr_module_path);

    // mp_execute_as_module(this->sources)
    // TODO: load the module and retrieve exposed class here

    // valid=false;
    // GDParser parser;
    // Error err = parser.parse(source,basedir,false,path);
    // if (err) {
    //     if (ScriptDebugger::get_singleton()) {
    //         PyLanguage::get_singleton()->debug_break_parse(get_path(),parser.get_error_line(),"Parser Error: "+parser.get_error());
    //     }
    //     _err_print_error("PyScript::reload",path.empty()?"built-in":(const char*)path.utf8().get_data(),parser.get_error_line(),("Parse Error: "+parser.get_error()).utf8().get_data(),ERR_HANDLER_SCRIPT);
    //     ERR_FAIL_V(ERR_PARSE_ERROR);
    // }


    // bool can_run = ScriptServer::is_scripting_enabled() || parser.is_tool_script();

    // GDCompiler compiler;
    // err = compiler.compile(&parser,this,p_keep_state);

    // if (err) {

    //     if (can_run) {
    //         if (ScriptDebugger::get_singleton()) {
    //             PyLanguage::get_singleton()->debug_break_parse(get_path(),compiler.get_error_line(),"Parser Error: "+compiler.get_error());
    //         }
    //         _err_print_error("PyScript::reload",path.empty()?"built-in":(const char*)path.utf8().get_data(),compiler.get_error_line(),("Compile Error: "+compiler.get_error()).utf8().get_data(),ERR_HANDLER_SCRIPT);
    //         ERR_FAIL_V(ERR_COMPILATION_FAILED);
    //     } else {
    //         return err;
    //     }
    // }

    // for(Map<StringName,Ref<PyScript> >::Element *E=subclasses.front();E;E=E->next()) {

    //     _set_subclass_path(E->get(),path);
    // }

#ifdef TOOLS_ENABLED
    /*for (Set<PlaceHolderScriptInstance*>::Element *E=placeholders.front();E;E=E->next()) {

        _update_placeholder(E->get());
    }*/
#endif
    return OK;
}


#if 0

struct _PyScriptMemberSort {

    int index;
    StringName name;
    _FORCE_INLINE_ bool operator<(const _PyScriptMemberSort& p_member) const { return index < p_member.index; }

};

#endif


void PyScript::get_script_method_list(List<MethodInfo> *p_list) const {
    DEBUG_TRACE_METHOD();
    // TODO
    return;

    // for (const Map<StringName,GDFunction*>::Element *E=member_functions.front();E;E=E->next()) {
    //     MethodInfo mi;
    //     mi.name=E->key();
    //     for(int i=0;i<E->get()->get_argument_count();i++) {
    //         PropertyInfo arg;
    //         arg.type=Variant::NIL; //variant
    //         arg.name=E->get()->get_argument_name(i);
    //         mi.arguments.push_back(arg);
    //     }

    //     mi.return_val.name="Variant";
    //     p_list->push_back(mi);
    // }
}


void PyScript::get_script_property_list(List<PropertyInfo> *p_list) const {
    DEBUG_TRACE_METHOD();
    // TODO
    return;

    // const PyScript *sptr=this;
    // List<PropertyInfo> props;

    // while(sptr) {

    //     Vector<_PyScriptMemberSort> msort;
    //     for(Map<StringName,PropertyInfo>::Element *E=sptr->member_info.front();E;E=E->next()) {

    //         _PyScriptMemberSort ms;
    //         ERR_CONTINUE(!sptr->member_indices.has(E->key()));
    //         ms.index=sptr->member_indices[E->key()].index;
    //         ms.name=E->key();
    //         msort.push_back(ms);

    //     }

    //     msort.sort();
    //     msort.invert();
    //     for(int i=0;i<msort.size();i++) {

    //         props.push_front(sptr->member_info[msort[i].name]);

    //     }

    //     sptr = sptr->_base;
    // }

    // for (List<PropertyInfo>::Element *E=props.front();E;E=E->next()) {
    //     p_list->push_back(E->get());
    // }

}


bool PyScript::has_method(const StringName& p_method) const {
    DEBUG_TRACE_METHOD();
    // TODO !
    return false;
    // return member_functions.has(p_method);
}


MethodInfo PyScript::get_method_info(const StringName& p_method) const {
    DEBUG_TRACE_METHOD();
    // TODO !
    return MethodInfo();
    // const Map<StringName,GDFunction*>::Element *E=member_functions.find(p_method);
    // if (!E)
    //     return MethodInfo();

    // MethodInfo mi;
    // mi.name=E->key();
    // for(int i=0;i<E->get()->get_argument_count();i++) {
    //     PropertyInfo arg;
    //     arg.type=Variant::NIL; //variant
    //     arg.name=E->get()->get_argument_name(i);
    //     mi.arguments.push_back(arg);
    // }

    // mi.return_val.name="Variant";
    // return mi;

}


bool PyScript::get_property_default_value(const StringName& p_property, Variant &r_value) const {
    DEBUG_TRACE_METHOD();
    // TODO
// #ifdef TOOLS_ENABLED

//     //for (const Map<StringName,Variant>::Element *I=member_default_values.front();I;I=I->next()) {
//     //  print_line("\t"+String(String(I->key())+":"+String(I->get())));
//     //}
//     const Map<StringName,Variant>::Element *E=member_default_values_cache.find(p_property);
//     if (E) {
//         r_value=E->get();
//         return true;
//     }

//     if (base_cache.is_valid()) {
//         return base_cache->get_property_default_value(p_property,r_value);
//     }
// #endif
    return false;

}


#if 0

#ifdef TOOLS_ENABLED
void PyScript::_update_exports_values(Map<StringName,Variant>& values, List<PropertyInfo> &propnames) {

    if (base_cache.is_valid()) {
        base_cache->_update_exports_values(values,propnames);
    }

    for(Map<StringName,Variant>::Element *E=member_default_values_cache.front();E;E=E->next()) {
        values[E->key()]=E->get();
    }

    for (List<PropertyInfo>::Element *E=members_cache.front();E;E=E->next()) {
        propnames.push_back(E->get());
    }

}
#endif

bool PyScript::_update_exports() {

#ifdef TOOLS_ENABLED

    bool changed=false;

    if (source_changed_cache) {
        //print_line("updating source for "+get_path());
        source_changed_cache=false;
        changed=true;

        String basedir=path;

        if (basedir=="")
            basedir=get_path();

        if (basedir!="")
            basedir=basedir.get_base_dir();

        GDParser parser;
        Error err = parser.parse(source,basedir,true,path);

        if (err==OK) {

            const GDParser::Node* root = parser.get_parse_tree();
            ERR_FAIL_COND_V(root->type!=GDParser::Node::TYPE_CLASS,false);

            const GDParser::ClassNode *c = static_cast<const GDParser::ClassNode*>(root);

            if (base_cache.is_valid()) {
                base_cache->inheriters_cache.erase(get_instance_ID());
                base_cache=Ref<PyScript>();
            }


            if (c->extends_used && String(c->extends_file)!="" && String(c->extends_file) != get_path()) {

                String path = c->extends_file;
                if (path.is_rel_path()) {

                    String base = get_path();
                    if (base=="" || base.is_rel_path()) {

                        ERR_PRINT(("Could not resolve relative path for parent class: "+path).utf8().get_data());
                    } else {
                        path=base.get_base_dir().plus_file(path);
                    }
                }

                if (path!=get_path()) {

                    Ref<PyScript> bf = ResourceLoader::load(path);

                    if (bf.is_valid()) {

                        //print_line("parent is: "+bf->get_path());
                        base_cache=bf;
                        bf->inheriters_cache.insert(get_instance_ID());

                        //bf->_update_exports(p_instances,true,false);

                    }
                } else {
                    ERR_PRINT(("Path extending itself in  "+path).utf8().get_data());
                }
            }

            members_cache.clear();;
            member_default_values_cache.clear();

            for(int i=0;i<c->variables.size();i++) {
                if (c->variables[i]._export.type==Variant::NIL)
                    continue;

                members_cache.push_back(c->variables[i]._export);
                //print_line("found "+c->variables[i]._export.name);
                member_default_values_cache[c->variables[i].identifier]=c->variables[i].default_value;
            }

            _signals.clear();

            for(int i=0;i<c->_signals.size();i++) {
                _signals[c->_signals[i].name]=c->_signals[i].arguments;
            }
        }
    } else {
        //print_line("unchaged is "+get_path());

    }

    if (base_cache.is_valid()) {
        if (base_cache->_update_exports()) {
            changed = true;
        }
    }

    if (/*changed &&*/ placeholders.size()) { //hm :(

        //print_line("updating placeholders for "+get_path());

        //update placeholders if any
        Map<StringName,Variant> values;
        List<PropertyInfo> propnames;
        _update_exports_values(values,propnames);

        for (Set<PlaceHolderScriptInstance*>::Element *E=placeholders.front();E;E=E->next()) {

            E->get()->update(propnames,values);
        }
    }

    return changed;

#endif
    return false;
}

void PyScript::update_exports() {

#ifdef TOOLS_ENABLED

    _update_exports();

    Set<ObjectID> copy=inheriters_cache; //might get modified

    //print_line("update exports for "+get_path()+" ic: "+itos(copy.size()));
    for(Set<ObjectID>::Element *E=copy.front();E;E=E->next()) {
        Object *id=ClassDB::get_instance(E->get());
        if (!id)
            continue;
        PyScript *s=id->cast_to<PyScript>();
        if (!s)
            continue;
        s->update_exports();
    }

#endif
}

void PyScript::_set_subclass_path(Ref<PyScript>& p_sc,const String& p_path) {

    p_sc->path=p_path;
    for(Map<StringName,Ref<PyScript> >::Element *E=p_sc->subclasses.front();E;E=E->next()) {

        _set_subclass_path(E->get(),p_path);
    }
}
#endif


String PyScript::get_node_type() const {
    DEBUG_TRACE_METHOD();
    // Even GDscript doesn't know what to put here !
    return ""; // ?
}


ScriptLanguage *PyScript::get_language() const {
    DEBUG_TRACE_METHOD();

    return PyLanguage::get_singleton();
}


#if 0

Variant PyScript::call(const StringName& p_method,const Variant** p_args,int p_argcount,Variant::CallError &r_error) {


    PyScript *top=this;
    while(top) {

        Map<StringName,GDFunction*>::Element *E=top->member_functions.find(p_method);
        if (E) {

            if (!E->get()->is_static()) {
                WARN_PRINT(String("Can't call non-static function: '"+String(p_method)+"' in script.").utf8().get_data());
            }

            return E->get()->call(NULL,p_args,p_argcount,r_error);
        }
        top=top->_base;
    }

    //none found, regular

    return Script::call(p_method,p_args,p_argcount,r_error);

}

bool PyScript::_get(const StringName& p_name,Variant &r_ret) const {

    {


        const PyScript *top=this;
        while(top) {

            {
                const Map<StringName,Variant>::Element *E=top->constants.find(p_name);
                if (E) {

                    r_ret= E->get();
                    return true;
                }
            }

            {
                const Map<StringName,Ref<PyScript> >::Element *E=subclasses.find(p_name);
                if (E) {

                    r_ret=E->get();
                    return true;
                }
            }
            top=top->_base;
        }

        if (p_name==PyLanguage::get_singleton()->strings._script_source) {

            r_ret=get_source_code();
            return true;
        }
    }



    return false;

}
bool PyScript::_set(const StringName& p_name, const Variant& p_value) {

    if (p_name==PyLanguage::get_singleton()->strings._script_source) {

        set_source_code(p_value);
        reload();
    } else
        return false;

    return true;
}

void PyScript::_get_property_list(List<PropertyInfo> *p_properties) const {

    p_properties->push_back( PropertyInfo(Variant::STRING,"script/source",PROPERTY_HINT_NONE,"",PROPERTY_USAGE_NOEDITOR) );
}



Vector<uint8_t> PyScript::get_as_byte_code() const {

    GDTokenizerBuffer tokenizer;
    return tokenizer.parse_code_string(source);
};


Error PyScript::load_byte_code(const String& p_path) {

    Vector<uint8_t> bytecode;

    if (p_path.ends_with("gde")) {

        FileAccess *fa = FileAccess::open(p_path,FileAccess::READ);
        ERR_FAIL_COND_V(!fa,ERR_CANT_OPEN);
        FileAccessEncrypted *fae = memnew( FileAccessEncrypted );
        ERR_FAIL_COND_V(!fae,ERR_CANT_OPEN);
        Vector<uint8_t> key;
        key.resize(32);
        for(int i=0;i<key.size();i++) {
            key[i]=script_encryption_key[i];
        }
        Error err = fae->open_and_parse(fa,key,FileAccessEncrypted::MODE_READ);
        ERR_FAIL_COND_V(err,err);
        bytecode.resize(fae->get_len());
        fae->get_buffer(bytecode.ptr(),bytecode.size());
        memdelete(fae);
    } else {

        bytecode = FileAccess::get_file_as_array(p_path);
    }
    ERR_FAIL_COND_V(bytecode.size()==0,ERR_PARSE_ERROR);
    path=p_path;

    String basedir=path;

    if (basedir=="")
        basedir=get_path();

    if (basedir!="")
        basedir=basedir.get_base_dir();

    valid=false;
    GDParser parser;
    Error err = parser.parse_bytecode(bytecode,basedir,get_path());
    if (err) {
        _err_print_error("PyScript::load_byte_code",path.empty()?"built-in":(const char*)path.utf8().get_data(),parser.get_error_line(),("Parse Error: "+parser.get_error()).utf8().get_data(),ERR_HANDLER_SCRIPT);
        ERR_FAIL_V(ERR_PARSE_ERROR);
    }

    GDCompiler compiler;
    err = compiler.compile(&parser,this);

    if (err) {
        _err_print_error("PyScript::load_byte_code",path.empty()?"built-in":(const char*)path.utf8().get_data(),compiler.get_error_line(),("Compile Error: "+compiler.get_error()).utf8().get_data(),ERR_HANDLER_SCRIPT);
        ERR_FAIL_V(ERR_COMPILATION_FAILED);
    }

    valid=true;

    for(Map<StringName,Ref<PyScript> >::Element *E=subclasses.front();E;E=E->next()) {

        _set_subclass_path(E->get(),path);
    }

    return OK;
}

#endif // if 0


Error PyScript::load_source_code(const String& p_path) {

    PoolVector<uint8_t> sourcef;
    Error err;
    FileAccess *f=FileAccess::open(p_path,FileAccess::READ,&err);
    if (err) {

        ERR_FAIL_COND_V(err,err);
    }

    int len = f->get_len();
    sourcef.resize(len+1);
    PoolVector<uint8_t>::Write w = sourcef.write();
    int r = f->get_buffer(w.ptr(),len);
    f->close();
    memdelete(f);
    ERR_FAIL_COND_V(r!=len,ERR_CANT_OPEN);
    w[len]=0;

    String s;
    if (s.parse_utf8((const char*)w.ptr())) {

        ERR_EXPLAIN("Script '"+p_path+"' contains invalid unicode (utf-8), so it was not loaded. Please ensure that scripts are saved in valid utf-8 unicode.");
        ERR_FAIL_V(ERR_INVALID_DATA);
    }

    this->source=s;
#ifdef TOOLS_ENABLED
    // source_changed_cache=true;
#endif
    //print_line("LSC :"+get_path());
    this->path=p_path;
    return OK;

}


#if 0
const Map<StringName,GDFunction*>& PyScript::debug_get_member_functions() const {

    return member_functions;
}



StringName PyScript::debug_get_member_by_index(int p_idx) const {


    for(const Map<StringName,MemberInfo>::Element *E=member_indices.front();E;E=E->next()) {

        if (E->get().index==p_idx)
            return E->key();
    }

    return "<error>";
}


Ref<PyScript> PyScript::get_base() const {

    return base;
}
#endif


bool PyScript::has_script_signal(const StringName& p_signal) const {
    DEBUG_TRACE_METHOD();
    // TODO
//     if (_signals.has(p_signal))
//         return true;
//     if (base.is_valid()) {
//         return base->has_script_signal(p_signal);
//     }
// #ifdef TOOLS_ENABLED
//     else if (base_cache.is_valid()){
//         return base_cache->has_script_signal(p_signal);
//     }

// #endif
    return false;
}


void PyScript::get_script_signal_list(List<MethodInfo> *r_signals) const {
    DEBUG_TRACE_METHOD();
    // TODO
    return;

//     for(const Map<StringName,Vector<StringName> >::Element *E=_signals.front();E;E=E->next()) {

//         MethodInfo mi;
//         mi.name=E->key();
//         for(int i=0;i<E->get().size();i++) {
//             PropertyInfo arg;
//             arg.name=E->get()[i];
//             mi.arguments.push_back(arg);
//         }
//         r_signals->push_back(mi);
//     }

//     if (base.is_valid()) {
//         base->get_script_signal_list(r_signals);
//     }
// #ifdef TOOLS_ENABLED
//     else if (base_cache.is_valid()){
//         base_cache->get_script_signal_list(r_signals);
//     }

// #endif

}


PyScript::PyScript() : tool(false), valid(false), _mpo_exposed_class(mp_const_none), _mpo_module(mp_const_none) {
    DEBUG_TRACE_METHOD();

    // _mp_exposed_mp_class = NULL;
    // _mp_module = NULL;
    // base = NULL;

#ifdef DEBUG_ENABLED
    // if (PyLanguage::get_singleton()->lock) {
    //     PyLanguage::get_singleton()->lock->lock();
    // }
    // PyLanguage::get_singleton()->script_list.add(&script_list);

    // if (PyLanguage::get_singleton()->lock) {
    //     PyLanguage::get_singleton()->lock->unlock();
    // }
#endif
}


PyScript::~PyScript() {
    DEBUG_TRACE_METHOD();
    // for (Map<StringName,GDFunction*>::Element *E=member_functions.front();E;E=E->next()) {
    //     memdelete( E->get() );
    // }

    // for (Map<StringName,Ref<PyScript> >::Element *E=subclasses.front();E;E=E->next()) {
    //     E->get()->_owner=NULL; //bye, you are no longer owned cause I died
    // }

#ifdef DEBUG_ENABLED
    // if (PyLanguage::get_singleton()->lock) {
    //     PyLanguage::get_singleton()->lock->lock();
    // }
    // PyLanguage::get_singleton()->script_list.remove(&script_list);

    // if (PyLanguage::get_singleton()->lock) {
    //     PyLanguage::get_singleton()->lock->unlock();
    // }
#endif
}
