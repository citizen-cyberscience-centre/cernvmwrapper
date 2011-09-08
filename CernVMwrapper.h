// This file is part of CernVMwrapper
// Copyright (C) 2011 Daniel Lombraña González
//
// CernVMwrapper is free software; you can redistribute it and/or modify it
// under the terms of the GNU Lesser General Public License
// as published by the Free Software Foundation,
// either version 3 of the License, or (at your option) any later version.
//
// CernVMwrapper is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
// See the GNU Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public License
// along with CernVMwrapper.  If not, see <http://www.gnu.org/licenses/>.

#include "boinc_api.h"

struct UC_SHMEM {
    double update_time;
    double fraction_done;
    double cpu_time;
    BOINC_STATUS status;
    APP_INIT_DATA init_data;
    int countdown;
        // graphics app sets this to 5 repeatedly,
        // main program decrements it once/sec.
        // If it's zero, don't bother updating shmem
};

