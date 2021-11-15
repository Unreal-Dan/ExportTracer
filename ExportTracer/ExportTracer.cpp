#include <Windows.h>
#include <Winternl.h>
#include <intrin.h>
#include <stdio.h>

#include <string>

#include "ExportTracer.h"
#include "Hook.h"
#include "Log.h"

using namespace std;

export_tracer::export_tracer() :
    m_module_name(),
    m_base(0),
    m_count(0),
    m_ord_base(0),
    m_func_list(),
    m_func_name_map(),
    m_hook_map()
{
}

export_tracer::~export_tracer()
{
}

bool export_tracer::init(const string &module_name)
{
    if (!module_name.length()) {
        return false;
    }
    // try to find the module
    m_base = (uintptr_t)GetModuleHandle(module_name.c_str());
    if (!m_base) {
        // attempt to load it 
        m_base = (uintptr_t)LoadLibrary(module_name.c_str());
        if (!m_base) {
            error("Could not find or load: %s", module_name);
            return false;
        }
    }
    m_module_name = module_name;

    PIMAGE_DOS_HEADER dos_hdr = (PIMAGE_DOS_HEADER)m_base;
    PIMAGE_NT_HEADERS nt_hdr = (PIMAGE_NT_HEADERS)(m_base + dos_hdr->e_lfanew);
    if (!nt_hdr) {
        error("NT header is NULL");
        return false;
    }

    DWORD exports_rva = nt_hdr->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].VirtualAddress;
    DWORD exports_size = nt_hdr->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].Size;
    if (!exports_rva || !exports_size) {
        error("Exports RVA or size is 0");
        return false;
    }
    PIMAGE_EXPORT_DIRECTORY ptrExportsDirectory = (PIMAGE_EXPORT_DIRECTORY)(m_base + exports_rva);
    PDWORD ptrExportsFunctions = (PDWORD)(m_base + ptrExportsDirectory->AddressOfFunctions);
    PDWORD ptrExportsNames = (PDWORD)(m_base + ptrExportsDirectory->AddressOfNames);
    PWORD ptrExportsOrdinals = (PWORD)(m_base + ptrExportsDirectory->AddressOfNameOrdinals);

    // ordinal base
    m_ord_base = ptrExportsDirectory->Base;

    PIMAGE_SECTION_HEADER sec_hdr = (PIMAGE_SECTION_HEADER)((uintptr_t)nt_hdr + sizeof(IMAGE_NT_HEADERS));
    uintptr_t code_start = 0;
    uintptr_t code_end = 0;
    uintptr_t sec_count = 0;
    do {
        if (strncmp((char *)sec_hdr->Name, ".text", 5) == 0) {
            code_start = m_base + sec_hdr->VirtualAddress;
            code_end = code_start + sec_hdr->SizeOfRawData;
            break;
        }
        sec_hdr = (PIMAGE_SECTION_HEADER)((uintptr_t)sec_hdr + sizeof(IMAGE_SECTION_HEADER));
        sec_count++;
    } while(sec_count < nt_hdr->FileHeader.NumberOfSections);

    uint32_t func_idx = 0;
    uint32_t name_idx = 0;

    // temporary map of index -> bool to indicate whether this 
    // ordinal has a name or not, we only use this temporarily
    // in order to construct the list of functions with meta info
    map<uint32_t, string> ord_name_map;

    // first walk the names table which is 1 to 1 with the ords table
    // the contents of the ords table are indexes of the func table
    // but they aren't necessarily in order so we map them first
    // map all of the names <=> ords
    for (uint32_t i = 0; i < ptrExportsDirectory->NumberOfNames; ++i) {
        string name = (const char *)(m_base + ptrExportsNames[i]);
        uint32_t ord = ptrExportsOrdinals[i];
        if (m_func_name_map.find(name) != m_func_name_map.end()) {
            error("Name %s already exists!", name.c_str());
            continue;
        }
        // hold onto the name -> ord mapping
        m_func_name_map[name] = ord;
        ord_name_map[ord] = name;
    }

    // then walk the pointer table (index = ord - ord_base)
    for (func_idx = 0; func_idx < ptrExportsDirectory->NumberOfFunctions; ++func_idx) {
        if (!ptrExportsFunctions[func_idx]) {
            continue;
        }
        // look up the function rva by raw ordinal
        DWORD func_rva = ptrExportsFunctions[func_idx];
        // the func ptr itself
        uintptr_t ptr = m_base + func_rva;
        // if the rva doesn't point to code then it's a forward
        bool is_forward = (ptr < code_start || ptr >= code_end);
        //bool is_forward = (func_rva >= exports_rva && func_rva < (exports_rva + exports_size));
        // grab the forward name if any
        string forward = is_forward ? (const char*)ptr : "?";
        // if the ord map is missing a name then generate one
        if (ord_name_map.find(func_idx) == ord_name_map.end()) {
            // if the ordinal does not match then this function has no name
            // and we shouldn't walk the name idx forward till we match it
            string new_name = "ordinal_" + to_string(func_idx + m_ord_base);
            m_func_name_map[new_name] = func_idx;
            ord_name_map[func_idx] = new_name;
        } 
        // use name from the ord map
        string name = ord_name_map[func_idx];
        // store list of ptrs, and names
        m_func_list.push_back(export_func(func_idx, ptr, is_forward, name, forward));
        string full_name = name;
        if (is_forward) {
            full_name += " -> " + forward;
        }
        debug("%p %u %s", ptr, func_idx, full_name.c_str());
        // total count of exports
        m_count++;
    }

    return true;
}

// wrapper function to interface with hooking library and call export tracer hooks
uintptr_t __fastcall export_tracer::export_tracer_hookfn(export_tracer::export_tracer_arg *arg, func_args *args)
{
    if (!arg || !arg->tracer || !args) {
        //error("NULL arg or tracer");
        return 0;
    }
    string name = arg->tracer->get_export_name(arg->ordinal);
    uintptr_t func_ptr = arg->tracer->get_export(arg->ordinal);
    if (!arg->func) {
        //error("NULL name or func");
        return 0;
    }
    export_hookinfo hookinfo(arg->hook, func_ptr, name, arg->tracer->m_module_name, arg->ordinal, args);
    // call user defined callback
    uintptr_t rv = arg->func(&hookinfo);
    debug("%s (ord: %d) called (hook returned %u)", name.c_str(), arg->ordinal, rv);
    return rv;
}

bool export_tracer::hook_export(uint32_t ordinal, tracer_callback_fn func, bool do_callback)
{
    if (ordinal < m_ord_base || (ordinal - m_ord_base) > m_count || !m_count) {
        return false;
    }
    uint32_t ord_index = ordinal - m_ord_base;
    // fetch name, should exist
    string name = m_func_list[ord_index].m_name;
    // fetch func ptr
    uintptr_t func_ptr = m_func_list[ord_index].m_func_ptr;
    if (!name.length() || !func_ptr) {
        return false;
    }
    // see if we already hooked this routine
    if (m_hook_ptr_map.find(func_ptr) != m_hook_ptr_map.end()) {
        error("Cannot hook target function, hook already in place for %u");
        return false;
    }
    unique_ptr<export_tracer_arg> arg = make_unique<export_tracer_arg>(this, nullptr, (uintptr_t)func, ordinal);
    if (!arg) {
        return false;
    }
    // create a new hook object for this hook
    unique_ptr<Hook> hook = make_unique<Hook>(func_ptr, (hook_callback_fn)export_tracer_hookfn, (hook_arg_t)arg.get());
    if (!hook) {
        return false;
    }
    // store ptr to the hook obj in the arg
    arg->hook = hook.get();
    // install the hook at the target location
    if (!hook->install_hook(do_callback)) {
        hook->cleanup_hook();
        return false;
    }
    success("Hooked %s (%p)", name.c_str(), func_ptr);
    // store the new hook in the map
    m_hook_map[ordinal] = make_pair(move(hook), move(arg));
    // and store a flag indicating this address is hooked to prevent double hooking
    m_hook_ptr_map[func_ptr] = ord_index;
    return true;
}

bool export_tracer::hook_export(string name, tracer_callback_fn func, bool do_callback)
{
    if (!name.length() || !m_count || !func) {
        return false;
    }
    if (m_func_name_map.find(name) == m_func_name_map.end()) {
        return false;
    }
    uint32_t ord = m_func_name_map[name] + m_ord_base;
    if (ord > (m_count + m_ord_base)) {
        return false;
    }
    return hook_export(ord, func, do_callback);
}

bool export_tracer::hook_all_exports(tracer_callback_fn callback, bool do_callbacks)
{
    for (uint32_t i = 0; i < m_count; ++i) {
        uint32_t ord = i + m_ord_base;
        // don't hook forwards, at least not yet
        if (is_forward(ord)) {
            continue;
        }
        // already hooked? This could happen if multiple export ords point to the same func
        if (m_hook_ptr_map.find(get_export(ord)) != m_hook_ptr_map.end()) {
            continue;
        }
        // hook the export with the given callback
        if (!hook_export(ord, callback, do_callbacks)) {
            error("Could not hook %s (%u)", get_export_name(ord).c_str(), ord);
            //return false;
        }
    }
    return true;
}

void export_tracer::unhook_export(uint32_t ordinal)
{
    if (ordinal < m_ord_base || (ordinal - m_ord_base) > m_count || !m_count) {
        return;
    }
    m_hook_ptr_map.erase(get_export(ordinal));
    m_hook_map.erase(ordinal);
}

void export_tracer::unhook_export(string name)
{
    if (!name.length() || !m_count) {
        return;
    }
    uint32_t ord = m_func_name_map[name] + m_ord_base;
    if (ord > m_count) {
        return;
    }
    return unhook_export(ord);
}

void export_tracer::unhook_all_exports(string name)
{
    for (uint32_t i = 0; i < m_count; ++i) {
        unhook_export(i + m_ord_base);
    }
}

string export_tracer::get_export_name(uint32_t ordinal)
{
    if (ordinal < m_ord_base || (ordinal - m_ord_base) > m_count) {
        return string();
    }
    ordinal -= m_ord_base;
    return m_func_list[ordinal].m_name;
}

uintptr_t export_tracer::get_export(uint32_t ordinal)
{
    if (ordinal < m_ord_base || (ordinal - m_ord_base) > m_count) {
        return 0;
    }
    ordinal -= m_ord_base;
    return m_func_list[ordinal].m_func_ptr;
}

string export_tracer::get_forward_name(uint32_t ordinal)
{
    if (ordinal < m_ord_base || (ordinal - m_ord_base) > m_count) {
        return string();
    }
    ordinal -= m_ord_base;
    return m_func_list[ordinal].m_forward;
}

bool export_tracer::is_forward(uint32_t ordinal)
{
    if (ordinal < m_ord_base || (ordinal - m_ord_base) > m_count) {
        return false;
    }
    ordinal -= m_ord_base;
    return m_func_list[ordinal].m_is_forward;
}
