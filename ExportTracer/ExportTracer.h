#pragma once
#include <Windows.h>

#include <memory>
#include <vector>
#include <string>
#include <map>

#include "Hook.h"

class export_hookinfo
{
public:
    export_hookinfo(Hook *hook, uintptr_t func, std::string name, std::string module, uint32_t ord, func_args *args) : 
        hook(hook), func(func), name(name), module(module), ord(ord), args(args) {}
    Hook *hook;
    uintptr_t func;
    std::string name;
    std::string module;
    uint32_t ord;
    func_args *args;
};

typedef uintptr_t (*tracer_callback_fn)(export_hookinfo *info);

class export_tracer
{
public:
    export_tracer();
    ~export_tracer();

    bool init(const std::string &module_name);

    bool hook_export(uint32_t ordinal, tracer_callback_fn func, bool do_callback = true);
    bool hook_export(std::string name, tracer_callback_fn func, bool do_callback = true);
    bool hook_all_exports(tracer_callback_fn func, bool do_callback = true);

    void unhook_export(uint32_t ordinal);
    void unhook_export(std::string name);
    void unhook_all_exports(std::string name);

    std::string get_export_name(uint32_t ordinal);
    std::string get_forward_name(uint32_t ordinal);
    uintptr_t get_export(uint32_t ordinal);
    bool is_forward(uint32_t ordinal);

private:

    class export_tracer_arg 
    {
    public:
        export_tracer_arg(export_tracer *tracer, Hook *hook, uintptr_t func, uint32_t ordinal) :
            tracer(tracer), hook(hook), func((tracer_callback_fn)func), ordinal(ordinal) {}
        // public members
        export_tracer *tracer;
        Hook *hook;
        tracer_callback_fn func;
        uint32_t ordinal;
    };

    class export_func {
    public:
        export_func(uint32_t ord, uintptr_t func_ptr, bool is_forward, std::string name, std::string forward) :
            m_ord(ord), m_func_ptr(func_ptr), m_is_forward(is_forward), m_name(name), m_forward(forward) { }
        uint32_t m_ord;
        uintptr_t m_func_ptr;
        bool m_is_forward;
        std::string m_name;
        std::string m_forward;
    };

    static uintptr_t __fastcall export_tracer_hookfn(export_tracer::export_tracer_arg *arg, func_args *args);

    // name of module being processed
    std::string m_module_name;
    // base of module being processed
    uintptr_t m_base;
    // number of exports
    uint32_t m_count;

    // ordinal base
    uint32_t m_ord_base;

    // list of export functions ptrs with meta info like ordinal and name
    std::vector<export_func> m_func_list;

    // map of name -> index in above list
    std::map<std::string, uint32_t> m_func_name_map;
    // map of hook address -> index in above list
    std::map<uintptr_t, uint32_t> m_hook_ptr_map;

    // map of ordinals -> pair<hook, hook arg>
    std::map<uint32_t, std::pair<std::unique_ptr<Hook>, std::unique_ptr<export_tracer_arg>>> m_hook_map;
};
