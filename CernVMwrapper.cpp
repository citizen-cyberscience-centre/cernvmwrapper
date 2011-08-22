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

#define VM_NAME "VMName"
#define CPU_TIME      "CpuTime"
#define PROGRESS_FN "ProgressFile"
#define TRICK_PERIOD 45.0*60
#define CHECK_PERIOD 2.0*60
#define POLL_PERIOD 1.0
#define MESSAGE "CPUTIME"
#define YEAR_SECS 365*24*60*60
#define BUFSIZE 4096

using std::string;

// DEBUG variable to control verbose
// debug = 0 -> NOTHING
// debug = 1 -> ERROR
// debug = 2 -> ERROR and WARNING
// debug = 3 -> ERROR, WARNING and NOTICE 
// debug = 4 -> ERROR, WARNING, NOTICE, and INFO

int debug = 3;

struct VM {
    string virtual_machine_name;
    string disk_name;
    string disk_path;
    string name_path;

    double current_period;
    time_t last_poll_point;
        
    bool suspended;
    int  poll_err_number;
   
    VM();
    void create();
    void throttle();
    void start(bool vrde, bool headless);
    void kill();
    void pause();
    void savestate();
    void resume();
    void Check();    
    void remove();
    void release(); //release the virtual disk
    void poll();
};

void write_cputime(double);

APP_INIT_DATA aid;


int unzip (const char *infilename, const char *outfilename)
{
    gzFile infile = gzopen(infilename, "rb");
    FILE *outfile = fopen(outfilename, "wb");
    if (!infile || !outfile) return -1;

    char buffer[128];
    int num_read = 0;

    while ((num_read = gzread(infile, buffer, sizeof(buffer))) > 0)
    {
        fwrite(buffer, 1, num_read, outfile);
    }

    gzclose(infile);
    fclose(outfile);
}

#ifdef _WIN32
bool IsWinNT()  //check if we're running NT
{
  OSVERSIONINFO osv;
  osv.dwOSVersionInfoSize = sizeof(osv);
  GetVersionEx(&osv);
  return (osv.dwPlatformId == VER_PLATFORM_WIN32_NT);
}
#endif

bool vbm_popen(string arg_list,
               char * buffer=NULL, 
               int nSize=1024,
               string command="VBoxManage -q "){
//when buffer is NULL, this function will not return the input of new process.
//Otherwise, it will not redirect the input of new process to buffer
#ifdef _WIN32
    STARTUPINFO si;
    SECURITY_ATTRIBUTES sa;
    SECURITY_DESCRIPTOR sd;               //security information for pipes
    PROCESS_INFORMATION pi;
    HANDLE newstdout,read_stdout;  //pipe handles
    unsigned long exit=0;
    if(buffer!=0){
        if (IsWinNT())        //initialize security descriptor (Windows NT)
        {
        InitializeSecurityDescriptor(&sd,SECURITY_DESCRIPTOR_REVISION);
        SetSecurityDescriptorDacl(&sd, true, NULL, false);
        sa.lpSecurityDescriptor = &sd;
        }
        else sa.lpSecurityDescriptor = NULL;
        sa.nLength = sizeof(SECURITY_ATTRIBUTES);
        sa.bInheritHandle = true;         //allow inheritable handles

        if (!CreatePipe(&read_stdout,&newstdout,&sa,0))  //create stdout pipe
        {
            fprintf(stderr,"CreatePipe Failed\n");
            CloseHandle(newstdout);
            CloseHandle(read_stdout);
            return false;
        }
    }
    GetStartupInfo(&si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    if(buffer!=NULL){
        si.dwFlags = STARTF_USESTDHANDLES|si.dwFlags;
        si.hStdOutput = newstdout;
        si.hStdError = newstdout;   //set the new handles for the child process
        si.hStdInput = NULL;
    }

    command+=arg_list;
    if (!CreateProcess( NULL,
                        (LPTSTR)command.c_str(),
                        NULL,
                        NULL,
                        TRUE,
                        CREATE_NO_WINDOW,
                        NULL,
                        NULL,
                        &si,
                        &pi))
    {
        fprintf(stderr,"CreateProcess Failed!");
        if(buffer!=NULL){
            CloseHandle(newstdout);
            CloseHandle(read_stdout);
        }
        return false;
    }
    //while(1)     
    //{
    //    GetExitCodeProcess(pi.hProcess,&exit); //while the process is running
    //    if (exit != STILL_ACTIVE)
    //      break;
    //}

    // Wait until process exists.
    WaitForSingleObject( pi.hProcess, INFINITE );

    // Close process and thread handles.
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    if(buffer!=NULL){
        memset(buffer,0,nSize);
        DWORD bread;
        BOOL bSuccess = false;

        for (;;)
        {
            ReadFile(read_stdout,buffer,nSize-1,&bread,NULL);
            if ( ! bSuccess || bread == 0 ) break;
        }
//      buffer[bread]=0;
        CloseHandle(newstdout);
        CloseHandle(read_stdout);
    }

    if(exit==0) 
        return true;
    else
        return false;
#else     //linux
    FILE* fp;
    char temp[256];
    string strTemp="";
    command+=arg_list;
    if(buffer==NULL){
        if(!system(command.c_str()))
            return true;
        else return false;
    }

    fp = popen(command.c_str(),"r");
    if (fp == NULL){
        if (debug >= 1) fprintf(stderr,"ERROR: vbm_popen popen failed!\n");
        return false;
    }
    memset(buffer,0,nSize);
    while (fgets(temp,256,fp) != NULL){
        strTemp+=temp;
    }
    pclose(fp);
    strncpy(buffer,strTemp.c_str(),nSize-1);
    return true;
#endif
}

VM::VM(){
    char buffer[256];

    virtual_machine_name="";
    current_period=0;
    suspended=false;
    last_poll_point=0;
    
    //boinc_resolve_filename_s("cernvm.vmdk.gz",disk_path);
//  fprintf(stderr,"%s\n",disk_path.c_str());
//  relative_to_absolute(buffer,(char*)disk_path.c_str());
    boinc_getcwd(buffer);
    disk_name = "cernvm.vmdk";
    disk_path = "cernvm.vmdk";
    disk_path="/"+disk_path;
    disk_path=buffer+disk_path;
    disk_path="\""+disk_path+"\"";
//  disk_path=buffer;
//  fprintf(stderr, "%s\n",disk_path.c_str());

    name_path="";
    //boinc_get_init_data(aid);
    //name_path += aid.project_dir;
    //name_path += "/";
    name_path += VM_NAME;
}   

void VM::create() {
    time_t rawtime;
    string arg_list;

    //createvm
    arg_list="";
    arg_list="createvm --name "+virtual_machine_name+ \
            " --ostype Linux26 --register";
    if(!vbm_popen(arg_list)){
        if (debug >= 1)
        {
            fprintf(stderr,"ERROR: Create VM method: createvm failed!\n");
            fprintf(stderr,"ERROR: %s\n",arg_list.c_str());
        }
        if (debug >= 3) fprintf(stderr,"NOTICE: Cleaning registered VM from a failure...\n");
        remove();
        if (debug >= 1) fprintf(stderr,"ERROR: createvm() failed. Aborting\n");
        boinc_finish(1);
    }

    //modifyvm
    arg_list="";
    arg_list="modifyvm "+virtual_machine_name+ \
            " --memory 256 --acpi on --ioapic on \
            --boot1 disk --boot2 none --boot3 none --boot4 none \
            --nic1 nat \
            --natdnsproxy1 on";

    //CernVM BOINC version doesn't need hostonly network interface
    /*
    #ifdef _WIN32
        arg_list+="--nic2 hostonly --hostonlyadapter2 \"VirtualBox Host-Only Ethernet Adapter\"";
    #else
        arg_list+="--nic2 hostonly --hostonlyadapter2 \"vboxnet0\"";
    #endif
    */
    vbm_popen(arg_list);


    //storagectl
    arg_list="";
    arg_list="storagectl "+virtual_machine_name+ \
            " --name \"IDE Controller\" --add ide --controller PIIX4";
    vbm_popen(arg_list);


    //openmedium
    //    arg_list="";
    //    arg_list="openmedium disk "+disk_path;
    //    vbm_popen(arg_list);
    
    //storageattach
    arg_list="";
    arg_list="storageattach "+virtual_machine_name+ \
            " --storagectl \"IDE Controller\" \
            --port 0 --device 0 --type hdd --medium " \
            + disk_path;
    if(!vbm_popen(arg_list)){
        if (debug >= 1)
        {
            fprintf(stderr,"ERROR: Create storageattach failed!\n");
            fprintf(stderr,"ERROR: %s\n",arg_list.c_str());
            fprintf(stderr,"ERROR: storageattach() Aborting\n");
        }
        //DEBUG for knowing which filename is being used
        //fprintf(stderr,disk_path.c_str());
        //fprintf(stderr,"\n");
        remove();
        boinc_finish(1);
        exit(0);
    }

    std::ofstream f(name_path.c_str());
    if (f.is_open())
    {
        if (f.good())
        {
            f << virtual_machine_name;
        }

        f.close();

    }
    else
    {
        if (debug >=1) 
        {
            fprintf(stderr,"ERROR: Saving VM name failed. Details: ofstream failed!\n");
            fprintf(stderr,"ERROR: VM_NAME Aborting\n");
        }
        boinc_finish(1);
    }

}

void VM::throttle()
{
    // Check the BOINC CPU preferences for running the VM accordingly
    string arg_list = "";
    boinc_get_init_data(aid);
    
    if (aid.project_preferences)
    {
        if (!aid.project_preferences) return;
        double max_vm_cpu_pct = 100.0;
        if (parse_double(aid.project_preferences, "<max_vm_cpu_pct>", max_vm_cpu_pct)) 
        {
            if (debug >= 3)
            {
                fprintf(stderr,"NOTICE: Maximum usage of CPU: %f\n", max_vm_cpu_pct);
                fprintf(stderr,"NOTICE: Setting how much CPU time the virtual CPU can use: %i\n", int(max_vm_cpu_pct));
            }
            std::stringstream out;
            out << int(max_vm_cpu_pct);

            arg_list = " controlvm " + virtual_machine_name + " cpuexecutioncap " + out.str();
            if (!vbm_popen(arg_list))
            {
                if (debug >= 1) fprintf(stderr,"ERROR: Impossible to set up CPU percentage usage limit\n");
            }
            else
            {
                if (debug >= 3) fprintf(stderr,"NOTICE: Success!\n");
            
            }
        
        }
    
    }
}

void VM::start(bool vrde=false, bool headless=false) {
    // Start the VM in headless mode
    
    boinc_begin_critical_section();
    string arg_list="";

    if (headless) arg_list=" startvm "+ virtual_machine_name + " --type headless";
    else arg_list = " startvm "+ virtual_machine_name;
    if (!vbm_popen(arg_list))
    {
        if (debug >= 1)
        {
            fprintf(stderr,"ERROR: Impossible to start the VM\n");
            fprintf(stderr,"ERROR: %s\n",arg_list.c_str());
        }
        if (debug >= 3) fprintf(stderr,"NOTICE: Removing VM...\n");
        remove();
        boinc_end_critical_section();
        boinc_finish(1);
    }
    // Enable or disable VRDP for the VM: (by default is disabled)
    if (vrde)
    {
        arg_list = "";
        arg_list = " controlvm " + virtual_machine_name + " vrde on";
    }
    else
    {
        arg_list = "";
        arg_list = " controlvm " + virtual_machine_name + " vrde off";
    }
    vbm_popen(arg_list);

    // If not running in Headless mode, don't allow the user to save, shutdown, power off or restore the VM
    if (!headless)
    {
        arg_list = "";
        // Don't allow the user to save, shutdown, power off or restore the VM
        arg_list = " setextradata " + virtual_machine_name + " GUI/RestrictedCloseActions SaveState,Shutdown,PowerOff,Restore";
        vbm_popen(arg_list);
    }

    throttle();

    boinc_end_critical_section();
}

void VM::kill() {
    boinc_begin_critical_section();
    string arg_list="";
    arg_list="controlvm "+virtual_machine_name+" poweroff";
    vbm_popen(arg_list);
    boinc_end_critical_section();
}

void VM::pause() {
    boinc_begin_critical_section();
    string arg_list="";
    arg_list="controlvm "+virtual_machine_name+" pause";
    if(vbm_popen(arg_list)) {
        suspended = true;
        time_t current_time = time(NULL);
        current_period += difftime (current_time,last_poll_point);
    }
    boinc_end_critical_section();
        
}

void VM::resume() {
    boinc_begin_critical_section();
    string arg_list="";
    arg_list="controlvm "+virtual_machine_name+" resume";
    if(vbm_popen(arg_list)) {
        suspended = false;
        last_poll_point=time(NULL);
    
    }
    boinc_end_critical_section();
}

void VM::Check(){
    boinc_begin_critical_section();
    string arg_list="";
    if(suspended){
        arg_list="controlvm "+virtual_machine_name+" resume";
        vbm_popen(arg_list);
    }
    arg_list="controlvm "+virtual_machine_name+" savestate";
    vbm_popen(arg_list);
    boinc_end_critical_section();
}

void VM::savestate()
{
    boinc_begin_critical_section();
    string arg_list = "";
    arg_list = "controlvm " + virtual_machine_name + " savestate";
    if (!vbm_popen(arg_list))
    {
        if (debug >= 1) fprintf(stderr,"ERROR: The VM could not be saved.\n");
    }
    boinc_end_critical_section();

}

void VM::remove(){
    boinc_begin_critical_section();
    string arg_list="",vminfo, vboxfolder, vboxXML, vboxXMLNew, vmfolder, vmdisk;
    char * env;
    bool vmRegistered = false;


    arg_list = "";
    arg_list = " discardstate " + virtual_machine_name;
    if (vbm_popen(arg_list)) if (debug >= 3) fprintf(stderr,"NOTICE: VM state discarded!\n");
    else if (debug >= 2) fprintf(stderr,"WARNING: it was not possible to discard the state of the VM.\n");

    // Unregistervm command with --delete option. VBox 4.1 should work well
    arg_list = "";
    arg_list = " unregistervm " + virtual_machine_name + " --delete";
    if (vbm_popen(arg_list)) if (debug >= 3) fprintf(stderr, "NOTICE: VM unregistered and deleted via VBoxManage.\n");
    else if (debug >= 2) fprintf(stderr, "WARNING: The VM could not be removed via VBoxManage.\n");
    
    // We test if we can remove the hard disk controller. If the command works, the cernvm.vmdk virtual disk will be also
    // removed automatically

    arg_list = "";
    arg_list = " storagectl  " + virtual_machine_name + " --name \"IDE Controller\" --remove";
    if (vbm_popen(arg_list)) if (debug >= 3) fprintf(stderr, "NOTICE: Hard disk removed!\n");
    else  if (debug >= 2) fprintf(stderr,"WARNING: it was not possible to remove the IDE controller.\n");

#ifdef _WIN32
	env = getenv("HOMEDRIVE");
	if (debug >= 3) fprintf(stderr,"NOTICE: I'm running in a Windows system...\n");
	vboxXML = string(env);
	env = getenv("HOMEPATH");
	vboxXML = vboxXML + string(env);
	vboxfolder = vboxXML + "\\VirtualBox VMs\\";
	vboxXML = vboxXML + "\\.VirtualBox\\VirtualBox.xml";
	//fprintf(stderr,"INFO: VirtualBox XML file: %s\n",vboxXML.c_str());


#else 
    env = getenv("HOME");
    vboxXML = string(env);

    if (vboxXML.find("Users") == string::npos)
    {
        // GNU/Linux
        vboxXML = vboxXML + "/.VirtualBox/VirtualBox.xml";
        vboxfolder = string(env) + "/.VirtualBox/";
        if (debug >= 3) fprintf(stderr,"NOTICE: I'm running in a GNU/Linux system...\n");
    }
    else
    {
        // Mac OS X
        vboxXML = vboxXML + "/Library/VirtualBox/VirtualBox.xml";
        vboxfolder = string(env) + "/Library/VirtualBox/";
        if (debug >= 3) fprintf(stderr,"NOTICE: I'm running in a Mac OS X system...\n");
    }
#endif

    std::ifstream in(vboxXML.c_str());

    if (in.is_open())
    {
        vboxXMLNew = vboxfolder + "VirtualBox.xmlNew";
        std::ofstream out(vboxXMLNew.c_str());

        int line_n = 0;
        size_t found_init, found_end;
        string line;
        while (std::getline(in,line))
        {
            found_init = line.find("BOINC_VM");
            if (found_init == string::npos)
                out << line + "\n";
            else
            {
                vmRegistered = true; 
                if (debug >= 3) fprintf(stderr,"NOTICE: Obtaining the VM folder...\n");
                found_init = line.find("src=");
                found_end = line.find(virtual_machine_name + ".vbox");
                if (found_end != string::npos)
                    if (debug >= 3) fprintf(stderr,"NOTICE: .vbox found at line %i in VirtualBox.xml file\n", line_n);
                vmfolder = line.substr(found_init+5,found_end-(found_init+5));
                // For debugging, uncomment following line:
                //fprintf(stderr,"INFO: %s VM folder: %s\n", virtual_machine_name.c_str(),vmfolder.c_str());
                if (debug >= 3) fprintf(stderr,"NOTICE: Done!\n");
            }
            
            line_n +=1;
        }
        in.close();
        out.close();
    }

    // When the project is reset, we have to first unregister the VM, else we will have an error.
    arg_list="unregistervm "+virtual_machine_name;
    if(!vbm_popen(arg_list))
    {
        if (debug >= 3) fprintf(stderr,"NOTICE: CernVM does not exist, so it is not necessary to unregister.\n");
    }
    else
    {
        if (debug >= 3) fprintf(stderr,"NOTICE: Successfully unregistered the CernVM\n");
    
    }


    // Delete old VirtualBox.xml and replace with new one
    std::remove(vboxXML.c_str());
    std::rename(vboxXMLNew.c_str(),vboxXML.c_str());
    

    // Remove remaining BOINC_VM folder
#ifdef _WIN32
	if (vmRegistered)
	{
		vmfolder = "RMDIR \"" + vmfolder + "\" /s /q";
		if (system(vmfolder.c_str()) == 0)
			if (debug >= 3) fprintf(stderr,"NOTICE: VM folder deleted!\n");
		else
			if (debug >= 3) fprintf(stderr,"NOTICE: System was clean, nothing to delete.\n");
	}
    else
    {
            if (debug >= 3) fprintf(stderr,"NOTICE: VM was not registered, deleting old VM folders...\n");
			vmfolder = "RMDIR \"" + vboxfolder + virtual_machine_name + "\" /s /q";
            if ( system(vmfolder.c_str()) == 0 )
                if (debug >= 3) fprintf(stderr,"NOTICE: VM folder deleted!\n");
            else
                if (debug >= 3) fprintf(stderr,"NOTICE: System was clean, nothing to delete.\n");
    }

#else // GNU/Linux and Mac OS X 
    // First delete the VM folder obtained in VirtualBox.xml
    if (vmRegistered)
    {
        vmfolder = "rm -rf \"" + vmfolder + "\"";
        if ( system(vmfolder.c_str()) == 0 )
            if (debug >= 3) fprintf(stderr,"NOTICE: VM folder deleted!\n");
        else
        {
            if (debug >= 3) fprintf(stderr,"NOTICE: System was clean, nothing to delete.\n");
        }
    }
    else
    {
            if (debug >= 3) fprintf(stderr,"NOTICE: VM was not registered, deleting old VM folders...\n");
            vmfolder = "rm -rf \"" + string(env) + "/VirtualBox VMs/" + virtual_machine_name + "\" ";
            if ( system(vmfolder.c_str()) == 0 )
                if (debug >= 3) fprintf(stderr,"NOTICE: VM folder deleted!\n");
            else
                if (debug >= 3) fprintf(stderr,"NOTICE: System was clean, nothing to delete.\n");
    }
#endif
    boinc_end_critical_section();
}
    
void VM::release(){
    boinc_begin_critical_section();
    string arg_list="";
    arg_list="closemedium disk "+disk_path;
    if(!vbm_popen(arg_list))
    {
        if (debug >= 1) fprintf(stderr,"ERROR: It was impossible to release the virtual hard disk\n");
    }
    else
        if (debug >= 3) fprintf(stderr,"NOTICE: Virtual Hard disk unregistered\n");
    boinc_end_critical_section();
}

void VM::poll() {
    boinc_begin_critical_section();
    string arg_list, status;
    char buffer[1024];
    time_t current_time;

    
    arg_list="";
    arg_list="showvminfo "+virtual_machine_name+" --machinereadable" ;
    if (!vbm_popen(arg_list,buffer,sizeof(buffer))){
        // Increase the number of errors
        double wait_time = 5.0;
        poll_err_number += 1;
        if (debug >= 1) fprintf(stderr,"ERROR: Get status from VM failed %i time!\n", poll_err_number);
        if (debug >= 3) fprintf(stderr,"WARNING: Sleeping poll for %f seconds!\n", wait_time);
        boinc_sleep(wait_time);
        if (debug >= 3) fprintf(stderr,"WARNING: Resuming poll!\n");
        if (poll_err_number > 4)
        {
            if (debug >= 1) fprintf(stderr,"ERROR: Get status from VM has failed %i times!\n", poll_err_number);
            if (debug >= 1) fprintf(stderr,"ERROR: Aborting the execution\n");
            if (debug >= 3) fprintf(stderr,"NOTICE: poll() Aborting\n");
            remove();
            boinc_end_critical_section();
            boinc_finish(1);
        }
    }

    else 
    {
        // Each time we read the status we reset the counter of errors
        poll_err_number = 0;

        status=buffer;
        if(status.find("VMState=\"running\"") !=string::npos){
            if(suspended){
                suspended=false;
                last_poll_point=time(NULL);
            }
            else{
                current_time=time(NULL);
                current_period += difftime (current_time,last_poll_point);
                last_poll_point=current_time;
                if (debug >= 4) fprintf(stderr,"INFO: VM poll is running\n");
            }
            boinc_end_critical_section();
            return;
            if (debug >= 3) fprintf(stderr,"NOTICE: VM is running!\n");  //testing
        }

        if(status.find("VMState=\"paused\"") != string::npos){
            if(!suspended){
                suspended=true;
                        time_t current_time=time(NULL);
                        current_period += difftime (current_time,last_poll_point);
                }
            if (debug >= 3) fprintf(stderr,"NOTICE: VM is paused!\n");  //testing
            boinc_end_critical_section();
            return;
        }

        if (status.find("VMState=\"poweroff\"") != string::npos)
        {
            if (debug >= 3)
            {
                fprintf(stderr, "NOTICE: VM is powered off and it shouldn't\n");
                fprintf(stderr, "NOTICE: Cancelling WU...\n");
            }
            boinc_end_critical_section();
            boinc_finish(1);
            exit(1);
        }
    }

}

void poll_boinc_messages(VM& vm, BOINC_STATUS &status) {

    if (status.reread_init_data_file)
    {
        if (debug >= 3) fprintf(stderr,"NOTICE: Project preferences changed\n");
        vm.throttle();
    }

    if (status.no_heartbeat) {
    if (debug >= 3) fprintf(stderr,"NOTICE: BOINC no_heartbeat\n");
    //vm.Check();
    vm.savestate();
        exit(0);
    }
    if (status.quit_request) {
        if (debug >= 3) fprintf(stderr,"NOTICE: BOINC status quit_request = True\n");
        //vm.Check();
        vm.savestate();
        exit(0);
    }
    if (status.abort_request) {
        if (debug >= 3)
        {
            fprintf(stderr,"NOTICE: BOINC status abort_request = True\n");    
            fprintf(stderr,"NOTICE: saving state of the vm and removing it...\n");
        }
        vm.savestate();
        vm.remove();
        if (debug >= 3) fprintf(stderr,"NOTICE: VM removed and task aborted\n");
        boinc_finish(0);
    }
    if (status.suspended) {
        if (debug >= 4) fprintf(stderr,"INFO: BOINC status suspend = True. Stopping VM\n");
        if (!vm.suspended) {
            vm.pause();
        }
    } else {
        if (debug >= 4) fprintf(stderr,"INFO: BOINC status suspend = False. Resuming VM\n");
        if (vm.suspended) {
            vm.resume();
        }
    }
}

void write_progress(time_t secs)
{
    std::ofstream f(PROGRESS_FN);
    if (f.is_open())
    {
        f <<  secs;
    }
    f.close();
}   

time_t read_progress() {
    std::ifstream f(PROGRESS_FN);
    if (f.is_open())
    {
        if (f.good()) 
        {
            time_t stored_secs;
            f >> stored_secs;
            return stored_secs;
        }
        else return 0;
    }
}

time_t update_progress(time_t secs) {
    time_t old_secs;

    old_secs = read_progress();
    write_progress(old_secs +  secs);
    return(old_secs + secs);
    
}



int main(int argc, char** argv) {
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
    //
    boinc_get_init_data(aid);


    // The VM
    VM vm;

    // Init the errors number
    vm.poll_err_number = 0;

    // Registering time for progress accounting
    time_t init_secs = time (NULL); 
    //fprintf(stderr,"INFO: %ld seconds since January 1, 1970\n", init_secs);



    // Checking command line options
    for (i=1; i<(unsigned int)argc; i++) 
    {
        //if (!strcmp(argv[i], "--graphics")) bool graphics = true;
        if (!strcmp(argv[i], "--headless")) headless = true;
        if (!strcmp(argv[i], "--debug"))
        {
            std::istringstream ArgStream(argv[i+1]);
            if (ArgStream >> debug)
                if (debug >= 4) fprintf(stderr,"INFO: Setting DEBUG level to: %i\n", debug);
        }
        if (!strcmp(argv[i], "--vmname"))
        {
            vm.virtual_machine_name = argv[i+1];
            if (debug >= 3) fprintf(stderr,"NOTICE: The name of the VM is: %s\n",vm.virtual_machine_name.c_str());
        
        }
    }



    // If the wrapper has not be called with the command line argument --vmname NAME, give a default name to the VM
    if (vm.virtual_machine_name.empty())
    {
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
        fprintf(stderr,"\nSetting VirtualBox PATH in Windows...\n");

		// First get the HKEY_LOCAL_MACHINE\SOFTWARE\Oracle\VirtualBox
        fprintf(stderr,"Trying to grab installation path of VirtualBox from Windows Registry...\n");
		TCHAR szPath[4096];
		DWORD dwType;
		DWORD cbSize = sizeof(szPath) - sizeof(TCHAR); // Leave room for nul terminator
		if (SHGetValue(HKEY_LOCAL_MACHINE,TEXT("SOFTWARE\\Oracle\\VirtualBox"),TEXT("InstallDir"),&dwType,szPath,&cbSize) == ERROR_SUCCESS)
		{
			//szPath[cbSize / sizeof(TCHAR)] = TEXT(´\0´);
			fprintf(stderr,"Success!!! Installation PATH of VirtualBox is: %s.\n",szPath);
		}
		else
		{
			if (debug >= 1) fprintf(stderr,"ERROR: Retrieving the HKEY_LOCAL_MACHINE\\SOFTWARE\\Oracle\\VirtualBox\\InstallDir value was impossible\n\n");

            fprintf(stderr,"Trying with VBOX_INSTALL_PATH environment variable...\n");
            LPTSTR VBoxInsPath;
            DWORD dwRet;
            BOOL fSuccess;
            // Retrieve old PATH variable
            VBoxInsPath = (LPTSTR) malloc(4096*sizeof(TCHAR));
                if(NULL == VBoxInsPath)
                {
					if (debug >= 1) fprintf(stderr,"ERROR: malloc for VBoxInsPAth variable. Reason: Out of memory\n");
                    return FALSE;
                }
            
                dwRet = GetEnvironmentVariable("VBOX_INSTALL_PATH", VBoxInsPath, 4096);
            if(0 == dwRet)
                {
                    DWORD dwErr = GetLastError();
                    if( ERROR_ENVVAR_NOT_FOUND == dwErr )
                    {
                        if (debug >= 1)
                        {
                            fprintf(stderr,"ERROR: VBOX_INSTALL_PATH environment variable does not exist.\n");
    						fprintf(stderr,"ERROR: Impossible to set up the VirtualBox PATH. Aborting execution.\n\n");
                        }
                        BOOL fExist=FALSE;
                        boinc_finish(1);
                    }
                    else
                    {
                        if (debug >= 1)
                        {
                            fprintf(stderr,"ERROR: GetLastError ouput for VBOX_INSTALL_PATH environment variable: %u\n", dwErr);
                            if (debug >= 3) fprintf(stderr,"NOTICE: GetLastError Aborting\n");
                        }
                        BOOL fExist=FALSE;
                        boinc_finish(1);
                    
                    }
                }
            free(VBoxInsPath);
		}

        // New variables for setting the environment variable PATH
        LPTSTR pszOldVal;
        LPTSTR newVirtualBoxPath;
        LPTSTR virtualbox;
        DWORD dwRet;
        BOOL fSuccess;


        // Create the new PATH variable
        newVirtualBoxPath = (LPTSTR) malloc(4096*sizeof(TCHAR));
        if(NULL == newVirtualBoxPath)
            {
				if (debug >= 1) fprintf(stderr, "ERROR: malloc for newVirtualBoxPath variable. Reason: Out of memory\n");
                return FALSE;
            }
        virtualbox = szPath;
                
        // Retrieve old PATH variable
        pszOldVal = (LPTSTR) malloc(4096*sizeof(TCHAR));
            if(NULL == pszOldVal)
            {
				if (debug >= 1) fprintf(stderr,"ERROR: malloc of pszOldVal variable. Reason: Out of memory\n");
                return FALSE;
            }
        
            dwRet = GetEnvironmentVariable("PATH", pszOldVal, 4096);
        if(0 == dwRet)
            {
                DWORD dwErr = GetLastError();
                if( ERROR_ENVVAR_NOT_FOUND == dwErr )
                {
                    if (debug >= 1) fprintf(stderr,"ERROR: PATH environment variable does not exist.\n");
                    BOOL fExist=FALSE;
                    exit(1);
                }
            }
        else
        {
            // DEBUG: print old PATH enviroment variable
            fprintf(stderr,"Old PATH environment variable:\n");
            fprintf(stderr,pszOldVal);
            fprintf(stderr,"\n");
            // Set new PATH environment variable
			lstrcat(pszOldVal,";"); // Concat ; to old PATH
			// Add VirtualBox path
			SetEnvironmentVariable("PATH",lstrcat(pszOldVal,virtualbox));
		    dwRet = GetEnvironmentVariable("PATH", pszOldVal, 4096);
            fprintf(stderr,"\nAdding VirtualBox to PATH:\n");
            fprintf(stderr,pszOldVal);
            fprintf(stderr,"\n");
        }
        // Free memory
        free(pszOldVal);
        free(newVirtualBoxPath);
    #endif

    // First print the version of VirtualBox
    string arg_list = " --version";
    char version[BUFSIZE];

    if (vbm_popen(arg_list, version, sizeof(version)))
    {
        fprintf(stderr,"\n");
        fprintf(stderr,"VirtualBox version: %s\n", version);
        fprintf(stderr,"\n");
    }


    // We check if the VM has already been created and launched
    std::ifstream f("VMName");
    if (f.is_open())
    {
        f.close();
        vm_name = true;
    
    }
    else
    {
        // First remove old versions
        if (debug >= 3) fprintf(stderr,"NOTICE: Cleaning old VMs of the project...\n");
        vm.remove();
        if (debug >= 3) fprintf(stderr,"NOTICE: Cleaning completed\n");
        // Then, Decompress the new VM.gz file
		fprintf(stderr,"\nInitializing VM...\n");
        fprintf(stderr,"Decompressing the VM\n");
        retval = boinc_resolve_filename_s("cernvm.vmdk.gz",resolved_name);
        if (retval) fprintf(stderr,"can't resolve cernvm.vmdk.gz filename");
        unzip(resolved_name.c_str(),cernvm.c_str());
        fprintf(stderr,"Uncompressed finished\n");
        vm_name= false;
    }

    if (vm_name)
    {
        fprintf(stderr,"VMName exists\n");

        bool VMexist=false;
        string arg_list;
        if (debug >= 3) fprintf(stderr,"NOTICE: Virtual machine name %s\n",vm.virtual_machine_name.c_str());

        arg_list="";
        arg_list=" list vms";
        if (!vbm_popen(arg_list,buffer,sizeof(buffer))){
            if (debug >= 1) fprintf(stderr, "ERROR: CernVMManager list failed!\n");
            boinc_finish(1);
        }

        string VMlist=buffer;
        // DEBUG for the list of running VMs
        // fprintf(stderr,"List of running VMs:\n");
        // fprintf(stderr,VMlist.c_str());
        // fprintf(stderr,"\n");
        if(VMlist.find(vm.virtual_machine_name.c_str()) != string::npos){
            VMexist=true;
        }

        //Maybe voluteers delete CernVM using VB GUI

        if(!VMexist){

            if (debug >= 3)
            {
                fprintf(stderr,"NOTICE: VM does not exists.\n");
                fprintf(stderr,"NOTICE: Cleaning old instances...\n");
            }
            vm.remove();
            if (debug >= 3)
            {
                fprintf(stderr,"NOTICE: Done!\n");
                fprintf(stderr,"NOTICE: Unzipping image...\n");
            }
            retval = boinc_resolve_filename_s("cernvm.vmdk.gz",resolved_name);
            if (retval) if (debug >= 1) fprintf(stderr,"ERROR: can't resolve cernvm.vmdk.gz filename");
            unzip(resolved_name.c_str(),cernvm.c_str());
            if (debug >= 3) fprintf(stderr,"NOTICE: Uncompressed finished\n");
		    fprintf(stderr,"Registering a new VM from an unzipped image...\n");
            vm.create();
            fprintf(stderr,"Done!\n");
        }

    }
    else{       
        if (debug >= 3) fprintf(stderr,"NOTICE: Cleaning old instances...\n");
        vm.remove();
		fprintf(stderr,"Registering a new VM from unzipped image...\n");
        vm.create();
        fprintf(stderr,"Done!\n");
    }

    time_t elapsed_secs = 0, dif_secs = 0;
    long int t = 0;
    double frac_done = 0; 

    vm.start(vrde,headless);
    vm.last_poll_point = time(NULL);
    
    fprintf(stderr,"DEBUG level %i\n", debug);
    while (1) {
        boinc_get_status(&status);
        poll_boinc_messages(vm, status);
        
        // Report progress to BOINC client
        if (!status.suspended)
        {
            vm.poll();
            if (vm.suspended) 
            {
                if (debug >= 2) fprintf(stderr,"WARNING: VM should be running as the WU is not suspended.\n");
                vm.resume();
            }

            elapsed_secs = time(NULL);
            dif_secs = update_progress(elapsed_secs - init_secs);
            // Convert it for Windows machines:
            t = static_cast<int>(dif_secs);
            if (debug >= 4) fprintf(stderr,"INFO: Running seconds %ld\n",dif_secs);
            // For 24 hours:
            frac_done = floor((t/86400.0)*100.0)/100.0;
            
            if (debug >= 4) fprintf(stderr,"INFO: Fraction done %f\n",frac_done);
            // Checkpoint for reporting correctly the time
            boinc_time_to_checkpoint();
            boinc_checkpoint_completed();
            boinc_fraction_done(frac_done);
            if (frac_done >= 1.0)
            {
                if (debug >= 3) fprintf(stderr,"NOTICE: Stopping the VM...\n");
                vm.savestate();
                if (debug >= 3) fprintf(stderr,"NOTICE: VM stopped!\n");
                vm.remove();
                // Update the ProgressFile for starting from zero next WU
                write_progress(0);
                if (debug >= 3) 
                {
                    fprintf(stderr,"NOTICE: Done!! Cleanly exiting.\n");
                    fprintf(stderr,"NOTICE: Work Unit completed.\n");
                    // Output file:
                    fprintf(stderr,"NOTICE: Creating output file...\n");
                }
                std::ofstream f("output");
                if (f.is_open())
                {
                    if (f.good())
                    {
                        f << "Work Unit completed!\n";
                        f.close();
                    }
                
                }
                if (debug >= 3) fprintf(stderr,"NOTICE: Done!\n");
                boinc_finish(0);
            }
            else
            {
                init_secs = elapsed_secs;
                boinc_sleep(POLL_PERIOD);
            }
        }
        else
        {
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
