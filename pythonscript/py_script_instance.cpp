#include "py_script_instance.h"
#include "py_script_language.h"
#include "py_script.h"


bool PyScriptInstance::set(const StringName& p_name, const Variant& p_value) {

    //member
    {
        const Map<StringName, PyScript::MemberInfo>::Element *E = script->member_indices.find(p_name);
        if (E) {
            if (E->get().setter) {
                const Variant *val=&p_value;
                Variant::CallError err;
                call(E->get().setter,&val,1,err);
                if (err.error==Variant::CallError::CALL_OK) {
                    return true; //function exists, call was successful
                }
            }
            else
                members[E->get().index] = p_value;
            return true;
        }
    }

    PyScript *sptr=script.ptr();
    while(sptr) {


        Map<StringName,GDFunction*>::Element *E = sptr->member_functions.find(PyScriptLanguage::get_singleton()->strings._set);
        if (E) {

            Variant name=p_name;
            const Variant *args[2]={&name,&p_value};

            Variant::CallError err;
            Variant ret = E->get()->call(this,(const Variant**)args,2,err);
            if (err.error==Variant::CallError::CALL_OK && ret.get_type()==Variant::BOOL && ret.operator bool())
                return true;
        }
        sptr = sptr->_base;
    }

    return false;
}


bool PyScriptInstance::get(const StringName& p_name, Variant &r_ret) const {

    const PyScript *sptr=script.ptr();
    while(sptr) {

        {
            const Map<StringName,PyScript::MemberInfo>::Element *E = script->member_indices.find(p_name);
            if (E) {
                if (E->get().getter) {
                    Variant::CallError err;
                    r_ret=const_cast<PyScriptInstance*>(this)->call(E->get().getter,NULL,0,err);
                    if (err.error==Variant::CallError::CALL_OK) {
                        return true;
                    }
                }
                r_ret=members[E->get().index];
                return true; //index found

            }
        }

        {

            const PyScript *sl = sptr;
            while(sl) {
                const Map<StringName,Variant>::Element *E = sl->constants.find(p_name);
                if (E) {
                    r_ret=E->get();
                    return true; //index found

                }
                sl=sl->_base;
            }
        }

        {
            const Map<StringName,GDFunction*>::Element *E = sptr->member_functions.find(PyScriptLanguage::get_singleton()->strings._get);
            if (E) {

                Variant name=p_name;
                const Variant *args[1]={&name};

                Variant::CallError err;
                Variant ret = const_cast<GDFunction*>(E->get())->call(const_cast<PyScriptInstance*>(this),(const Variant**)args,1,err);
                if (err.error==Variant::CallError::CALL_OK && ret.get_type()!=Variant::NIL) {
                    r_ret=ret;
                    return true;
                }
            }
        }
        sptr = sptr->_base;
    }

    return false;

}


void PyScriptInstance::get_property_list(List<PropertyInfo> *p_properties) const {
    // TODO
}


Variant::Type PyScriptInstance::get_property_type(const StringName& p_name,bool *r_is_valid=NULL) const=0 {
    // TODO
    return Variant::NIL;
}


void PyScriptInstance::get_method_list(List<MethodInfo> *p_list) const {
    // TODO
}


bool PyScriptInstance::has_method(const StringName& p_method) const {
    // TODO
    return false
}


Variant PyScriptInstance::call(const StringName& p_method,VARIANT_ARG_LIST) {
    // TODO
    return Variant();
}


Variant PyScriptInstance::call(const StringName& p_method,const Variant** p_args,int p_argcount,Variant::CallError &r_error) {
    // TODO
}


void PyScriptInstance::call_multilevel(const StringName& p_method,VARIANT_ARG_LIST) {
    // TODO
}


void PyScriptInstance::call_multilevel(const StringName& p_method,const Variant** p_args,int p_argcount) {
    // TODO
}


void PyScriptInstance::call_multilevel_reversed(const StringName& p_method,const Variant** p_args,int p_argcount) {
    // TODO
}


void PyScriptInstance::notification(int p_notification) {
    // TODO
}


//this is used by script languages that keep a reference counter of their own
//you can make make Ref<> not die when it reaches zero, so deleting the reference
//depends entirely from the script

void PyScriptInstance::refcount_incremented() {
    // TODO: use me ?
}


bool PyScriptInstance::refcount_decremented() {
    // TODO: use me ?
    // return true if it can die
    return true;
}


Ref<Script> PyScriptInstance::get_script() const {

    return script;
}


RPCMode PyScriptInstance::get_rpc_mode(const StringName& p_method) const {
    // TODO
    return RPC_MODE_DISABLED;
}


RPCMode PyScriptInstance::get_rset_mode(const StringName& p_variable) const {
    // TODO
    return RPC_MODE_DISABLED;
}


ScriptLanguage *PyScriptInstance::get_language() {

    return PyScriptLanguage::get_singleton();
}


PyScriptInstance::~ScriptInstance() {
    // TODO
}
