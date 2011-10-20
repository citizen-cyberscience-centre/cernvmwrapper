// This file is part of BOINC.
// http://boinc.berkeley.edu
// Copyright (C) 2008 University of California
//
// BOINC is free software; you can redistribute it and/or modify it
// under the terms of the GNU Lesser General Public License
// as published by the Free Software Foundation,
// either version 3 of the License, or (at your option) any later version.
//
// BOINC is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
// See the GNU Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public License
// along with BOINC.  If not, see <http://www.gnu.org/licenses/>.

// wrapper.C
// wrapper program for CernVM - lets you use CernVM on BOINC
//
// Handles:
// - suspend/resume/quit/abort virtual machine 
//
// Contributor: Jie Wu <jiewu AT cern DOT ch>
//
// Contributor: Daniel Lombraña González <teleyinex AT gmail DOT com>

#include <stdio.h>
#include <string>
#include <sstream>
#include <iostream>
#include <fstream>
#include <time.h>
#include <stdlib.h>
#include "zlib.h"

#ifdef _WIN32
#include <windows.h>
#include <tchar.h>
#include <shlwapi.h>
#pragma comment(lib, "shlwapi.lib")
#include <stdio.h>
#include <conio.h>
#include <string.h>
#pragma hdrstop
#include "boinc_win.h"
#include "win_util.h"
#else
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include "procinfo.h"
#endif

#include "boinc_api.h"
#include "diagnostics.h"
#include "filesys.h"
#include "parse.h"
#include "str_util.h"
#include "str_replace.h"
#include "util.h"
#include "error_numbers.h"
#include "graphics2.h"
#include "vbox.h"

int main(int argc, char** argv) 
{
        BOINC_OPTIONS options;
        BOINC_STATUS status;
        char buffer[2048]; // Enough size for the VBoxManage list vms output
        unsigned int i;
        //bool graphics = false;
        bool headless = false;
        bool vrde = false;
        bool vm_name = false;
        bool retval = false;
        // Name for the VM vmdk filename
        string cernvm = "cernvm.vmdk";
        string resolved_name;
    
        // Get BOINC APP INIT DATA
        boinc_get_init_data(aid);
    
        VM vm;
    
        vm.poll_err_number = 0;
    
        // Registering time for progress accounting
        time_t init_secs = time (NULL); 
    
        for (i = 1; i < (unsigned int)argc; i++) {
                // TODO: if (!strcmp(argv[i], "--graphics")) bool graphics = true;
                if (!strcmp(argv[i], "--headless")) headless = true;
                if (!strcmp(argv[i], "--debug")) {
                        std::istringstream ArgStream(argv[i+1]);
                        if (ArgStream >> debug)
                                if (debug >= 4) fprintf(stderr, "INFO: Setting DEBUG level to: %i\n", debug);
                }

                if (!strcmp(argv[i], "--vmname")) {
                        vm.virtual_machine_name = argv[i+1];
                        if (debug >= 3) fprintf(stderr, "NOTICE: The name of the VM is: %s\n", vm.virtual_machine_name.c_str());
                }
        }
    
        // If the wrapper has not be called with the command line argument --vmname NAME, give a default name to the VM
        if (vm.virtual_machine_name.empty()) {
                vm.virtual_machine_name = "BOINC_VM";
        }
    
        memset(&options, 0, sizeof(options));
        options.main_program = true;
        options.check_heartbeat = true;
        options.handle_process_control = true;
        options.send_status_msgs = true;
        
        boinc_init_options(&options);
    
        // Setting up the PATH for Windows machines:
        #ifdef _WIN32
        // DEBUG information:
        if ( debug >= 3 ) fprintf(stderr,"\nNOTICE: Setting VirtualBox PATH in Windows...\n");
    
        // First get the HKEY_LOCAL_MACHINE\SOFTWARE\Oracle\VirtualBox
        if ( debug >= 4 ) fprintf(stderr,"INFO: Trying to grab installation path of VirtualBox from Windows Registry...\n");
        HKEY keyHandle;
        DWORD dwBufLen;
        LPTSTR  szPath = NULL;
    
        if (RegOpenKeyEx(HKEY_LOCAL_MACHINE, _T("SOFTWARE\\Oracle\\VirtualBox"), 0, KEY_READ, &keyHandle) == ERROR_SUCCESS) {
	        if (RegQueryValueEx(keyHandle, _T("InstallDir"), NULL, NULL, NULL, &dwBufLen) == ERROR_SUCCESS) {
                        // Allocate the buffer space
                        szPath = (LPTSTR) malloc(dwBufLen);
                        (*szPath) = NULL;
                        
                        // Now get the data
                        if (RegQueryValueEx (keyHandle, _T("InstallDir"), NULL, NULL, (LPBYTE)szPath, &dwBufLen) == ERROR_SUCCESS) {
                        	if (debug >= 3 ) fprintf(stderr, "NOTICE: Success!!! Installation PATH of VirtualBox is: %s;\n", szPath);
                        }
    				
                }
                else {
               	        if (debug >= 1) 
                                fprintf(stderr, "ERROR: Retrieving the HKEY_LOCAL_MACHINE\\SOFTWARE\\Oracle\\VirtualBox\\InstallDir value was impossible\n\n");
                }
                if (keyHandle) RegCloseKey(keyHandle);	
        }
        else {
                if (debug >= 1)
                        fprintf(stderr, "ERROR: Opening Windows registry!\n");
        }
    		
        string old_path = getenv("path");
        if (debug >= 3) fprintf(stderr, "NOTICE: Old path %s\n", old_path.c_str());
        
        string new_path = "path=";
        new_path += szPath;
        new_path += ";";
        new_path += old_path;
        putenv(const_cast<char*>(new_path.c_str()));
        if (debug >= 3) fprintf(stderr, "INFO: New path %s\n", getenv("path"));
        if (szPath) free(szPath);
        #endif
    
        // First print the version of VirtualBox
        string arg_list = " --version";
        char version[BUFSIZE];
    
        if (vbm_popen(arg_list, version, sizeof(version))) {
                cerr << "VirtualBox version: " << version << endl;
        }
    
        // We check if the VM has already been created and launched
        std::ifstream f("VMName");
        if (f.is_open()) {
                f.close();
                vm_name = true;
        }
        else {
                // First remove old versions
                if (debug >= 3) fprintf(stderr, "NOTICE: Cleaning old VMs of the project...\n");
                vm.remove();
                if (debug >= 3) fprintf(stderr, "NOTICE: Cleaning completed\n");
                // Then, Decompress the new VM.gz file
                std::ifstream f(PROGRESS_FN);
                if (f.is_open()) {
                    if (debug >= 3) fprintf(stderr, "NOTICE: ProgressFile should not be present. Deleting it\n");
                    f.close();
                    remove(PROGRESS_FN);
                }

    	    	fprintf(stderr, "\nInitializing VM...\n");
                fprintf(stderr, "Decompressing the VM\n");
                retval = boinc_resolve_filename_s("cernvm.vmdk.gz", resolved_name);
                if (retval) fprintf(stderr, "can't resolve cernvm.vmdk.gz filename");
                Helper::unzip(resolved_name.c_str(), cernvm.c_str());
                fprintf(stderr, "Uncompressed finished\n");
                vm_name = false;
        }
    
        if (vm_name) {
            fprintf(stderr, "VMName exists\n");
    
            bool VMexist = false;
            string arg_list;
            if (debug >= 3) fprintf(stderr, "NOTICE: Virtual machine name %s\n", vm.virtual_machine_name.c_str());
    
            arg_list = "";
            arg_list = " list vms";
            if (!vbm_popen(arg_list, buffer, sizeof(buffer))) {
                    if (debug >= 1) fprintf(stderr, "ERROR: CernVMManager list failed!\n");
                    boinc_finish(1);
            }
    
            string VMlist = buffer;
            // DEBUG for the list of running VMs
            // fprintf(stderr,"List of running VMs:\n");
            // fprintf(stderr,VMlist.c_str());
            // fprintf(stderr,"\n");
            if (VMlist.find(vm.virtual_machine_name.c_str()) != string::npos) {
                    VMexist=true;
            }
    
            // Maybe voluteers deleted the Virtual Machine using the VirtualBox
            // Manager
    
            if (!VMexist) {
                    if (debug >= 3) {
                            fprintf(stderr, "NOTICE: VM does not exists.\n");
                            fprintf(stderr, "NOTICE: Cleaning old instances...\n");
                    }
                    vm.remove();
                    if (debug >= 3) {
                            fprintf(stderr, "NOTICE: Done!\n");
                            fprintf(stderr, "NOTICE: Unzipping image...\n");
                    }

                    retval = boinc_resolve_filename_s("cernvm.vmdk.gz", resolved_name);
                    if (retval) if (debug >= 1) fprintf(stderr, "ERROR: can't resolve cernvm.vmdk.gz filename");
                    Helper::unzip(resolved_name.c_str(),cernvm.c_str());
                    if (debug >= 3) fprintf(stderr, "NOTICE: Uncompressed finished\n");
    	            fprintf(stderr,"Registering a new VM from an unzipped image...\n");
                    vm.create();
                    fprintf(stderr,"Done!\n");
            }
    
        }
        else {       
                if (debug >= 3) fprintf(stderr, "NOTICE: Cleaning old instances...\n");
                vm.remove();
    	    	fprintf(stderr, "Registering a new VM from unzipped image...\n");
                vm.create();
                fprintf(stderr, "Done!\n");
        }
    
        time_t elapsed_secs = 0; 
        long int t = 0;
        double frac_done = 0, dif_secs = 0; 
    
        vm.start(vrde, headless);
        vm.last_poll_point = time(NULL);
    
        #ifdef APP_GRAPHICS
        // create shared mem segment for graphics, and arrange to update it
        Share::data = (Share::SharedData*)boinc_graphics_make_shmem("cernvm", sizeof(Share::SharedData));
        if (!Share::data) {
                if (debug >= 1) fprintf(stderr, "ERROR: failed to create shared mem segment\n");
        }
        Helper::update_shmem();
        boinc_register_timer_callback(Helper::update_shmem);
        #endif
        
        fprintf(stderr, "DEBUG level %i\n", debug);
        while (1) {
                boinc_get_status(&status);
                poll_boinc_messages(vm, status);
                
                // Report progress to BOINC client
                if (!status.suspended) {
                        vm.poll();
                        if (vm.suspended) {
                                if (debug >= 2) fprintf(stderr, "WARNING: VM should be running as the WU is not suspended.\n");
                                vm.resume();
                        }
    
                        elapsed_secs = time(NULL);
                        dif_secs = Helper::update_progress(difftime(elapsed_secs,init_secs));
                        // Convert it for Windows machines:
                        t = static_cast<int>(dif_secs);
                        if (debug >= 4) fprintf(stderr, "INFO: Running seconds %f\n", dif_secs);
                        // For 24 hours:
                        frac_done = floor((t / 86400.0) * 100.0) / 100.0;
                        
                        if (debug >= 4) fprintf(stderr, "INFO: Fraction done %f\n", frac_done);
                        // Checkpoint for reporting correctly the time
                        boinc_time_to_checkpoint();
                        boinc_checkpoint_completed();
                        boinc_fraction_done(frac_done);
                        if (frac_done >= 1.0) {
                                if (debug >= 3) fprintf(stderr, "NOTICE: Stopping the VM...\n");
                                vm.savestate();
                                if (debug >= 3) fprintf(stderr, "NOTICE: VM stopped!\n");
                                vm.remove();
                                // Update the ProgressFile for starting from zero next WU
                                Helper::write_progress(0);
                                if (debug >= 3) {
                                        fprintf(stderr,"NOTICE: Done!! Cleanly exiting.\n");
                                        fprintf(stderr,"NOTICE: Work Unit completed.\n");
                                        fprintf(stderr,"NOTICE: Creating output file...\n");
                                }
                                std::ofstream f("output");
                                if (f.is_open()) {
                                        if (f.good()) {
                                                f << "Work Unit completed!\n";
                                                f.close();
                                        }
                                }
                                if (debug >= 3) fprintf(stderr, "NOTICE: Done!\n");
                                #ifdef APP_GRAPHICS
                                Helper::update_shmem();
                                #endif
                                boinc_finish(0);
                        }
                        else {
                                init_secs = elapsed_secs;
                                boinc_sleep(POLL_PERIOD);
                        }
                }
                else {
                        init_secs = time(NULL);
                        boinc_sleep(POLL_PERIOD);
                }
        }
}

#ifdef _WIN32

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrevInst, LPSTR Args, int WinMode) {
        LPSTR command_line;
        char* argv[100];
        int argc;
        
        command_line = GetCommandLine();
        argc = parse_command_line(command_line, argv);
        return main(argc, argv);
}
#endif
