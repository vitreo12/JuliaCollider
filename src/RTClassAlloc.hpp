#pragma once
#include "SC_PlugIn.hpp"

//Overload new and delete operators with RTAlloc and RTFree calls
class RTClassAlloc
{
    public:
        void* operator new(size_t size, World* in_world, InterfaceTable* interface_table)
        {
            return (void*)interface_table->fRTAlloc(in_world, size);
        }

        void operator delete(void* p, World* in_world, InterfaceTable* interface_table) 
        {
            interface_table->fRTFree(in_world, p);
        }
    private:
};
