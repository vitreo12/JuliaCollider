/*
    JuliaCollider: Julia's JIT compilation for low-level audio synthesis and prototyping in SuperCollider.
    Copyright (C) 2019 Francesco Cameli. All rights reserved.

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

#pragma once
#include "SC_PlugIn.hpp"

/* Class which overloads C++ new/delete operators with InterfacteTable->fRTAlloc/fRTFree.
Used for all user data in the async commands. */

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
