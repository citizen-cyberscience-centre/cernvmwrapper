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
// - reporting CPU time through trickle massages
//
// Contributor: Jie Wu (jiewu@cern.ch)

#include <stdio.h>
#include <vector>
#include <string>
#include <time.h>
# include <stdlib.h>

#ifdef _WIN32
#include <windows.h>
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
#define TRICK_PERIOD 45.0*60
#define CHECK_PERIOD 2.0*60
#define POLL_PERIOD 1.0
#define MESSAGE "CPUTIME"
using std::vector;
using std::string;

struct VM {
	string virtual_machine_name;
	string disk_path;
	string name_path;

	double current_period;
	time_t last_poll_point;
		
	bool suspended;
   
	VM();
	void create();
	void start(bool vrdp);
	void kill();
	void stop();
	void resume();
	void check();    
	void remove();
	void release(); //release the virtual disk
	int send_cputime_message();
	void poll();
};
void write_cputime(double);
APP_INIT_DATA aid;

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
	while(1)     
	{
		GetExitCodeProcess(pi.hProcess,&exit); //while the process is running
		if (exit != STILL_ACTIVE)
		  break;
	}
	CloseHandle(pi.hThread);
	CloseHandle(pi.hProcess);
	if(buffer!=NULL){
		memset(buffer,0,nSize);
		DWORD bread;
		ReadFile(read_stdout,buffer,nSize-1,&bread,NULL);
//		buffer[bread]=0;
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
		fprintf(stderr,"vbm_popen popen failed!\n");
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
	
	boinc_resolve_filename_s("cernvm.vmdk",disk_path);
//	fprintf(stderr,"%s\n",disk_path.c_str());
//	relative_to_absolute(buffer,(char*)disk_path.c_str());
	boinc_getcwd(buffer);
	disk_path="/"+disk_path;
	disk_path=buffer+disk_path;
	disk_path="\""+disk_path+"\"";
//	disk_path=buffer;
//	fprintf(stderr, "%s\n",disk_path.c_str());

	name_path="";
	boinc_get_init_data(aid);
	name_path += aid.project_dir;
	name_path += "/";
	name_path+=VM_NAME;
}	

void VM::create() {
	time_t rawtime;
	string arg_list;
	char buffer[256];
	FILE* fp;

	rawtime=time(NULL);
	strftime ( buffer, 256, "%Y%m%d%H%M%S", localtime (&rawtime) );
	virtual_machine_name="";
	virtual_machine_name += "BOINC_VM_";
	virtual_machine_name += buffer;

//createvm
	arg_list="";
	arg_list="createvm --name "+virtual_machine_name+ \
			" --ostype Linux26 --register";
	if(!vbm_popen(arg_list)){
		fprintf(stderr,"Create createvm failed!\n");
		boinc_finish(1);
	}

//modifyvm
	arg_list="";
	arg_list="modifyvm "+virtual_machine_name+ \
			" --memory 256 --acpi on --ioapic on \
			--boot1 disk --boot2 none --boot3 none --boot4 none \
			--nic1 nat ";
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
	arg_list="";
	arg_list="openmedium disk "+disk_path;
	vbm_popen(arg_list);

//storageattach
	arg_list="";
	arg_list="storageattach "+virtual_machine_name+ \
			" --storagectl \"IDE Controller\" \
			--port 0 --device 0 --type hdd --medium " \
			+disk_path;
	if(!vbm_popen(arg_list)){
		fprintf(stderr,"Create storageattach failed!\n");
		boinc_finish(1);
	}


	if((fp=fopen(name_path.c_str(),"w"))==NULL){
		fprintf(stderr,"fopen failed!\n");
		boinc_finish(1);
	}
	fputs(virtual_machine_name.c_str(),fp);
	fclose(fp);

}

void VM::start(bool vrdp=false) {
    // Start the VM in headless mode
	string arg_list="";
    arg_list=" startvm "+ virtual_machine_name + " --type headless";
	vbm_popen(arg_list);
    // Enable or disable VRDP for the VM: (by default is disabled)
    if (vrdp)
    {
        arg_list = "";
        arg_list = " controlvm " + virtual_machine_name + " vrdp on";
    }
    else
    {
        arg_list = "";
        arg_list = " controlvm " + virtual_machine_name + " vrdp off";
    }
    vbm_popen(arg_list);
}

void VM::kill() {
	string arg_list="";
	arg_list="controlvm "+virtual_machine_name+" poweroff";
	vbm_popen(arg_list);
}

void VM::stop() {
	time_t current_time;
	string arg_list="";
	arg_list="controlvm "+virtual_machine_name+" pause";
	if(vbm_popen(arg_list)) {
		suspended = true;
		current_time=time(NULL);
		current_period += difftime (current_time,last_poll_point);
	}
		
}

void VM::resume() {
	string arg_list="";
	arg_list="controlvm "+virtual_machine_name+" resume";
	if(vbm_popen(arg_list)) {
		suspended = false;
		last_poll_point=time(NULL);
	
	}
}

void VM::check(){
	string arg_list="";
	if(suspended){
		arg_list="controlvm "+virtual_machine_name+" resume";
		vbm_popen(arg_list);
	}
	arg_list="controlvm "+virtual_machine_name+" savestate";
	vbm_popen(arg_list);
}

void VM::remove(){
	string arg_list="";
	arg_list="storageattach "+virtual_machine_name+ \
			" --storagectl \"IDE Controller\" \
			--port 0 --device 0 --type hdd --medium  none";
	vbm_popen(arg_list);

	arg_list="";
	arg_list="closemedium disk "+disk_path;
	vbm_popen(arg_list);

	arg_list="";
	arg_list="unregistervm "+virtual_machine_name+" --delete";
	vbm_popen(arg_list);

	boinc_delete_file(name_path.c_str());
}
	
void VM::release(){
    string arg_list="";
	arg_list="closemedium disk "+disk_path;
	vbm_popen(arg_list);
}

int VM::send_cputime_message() {
	char text[256];
	int reval;
	string variety=MESSAGE;
	sprintf(text,"<run_time>%lf</run_time>",current_period);
	reval=boinc_send_trickle_up((char *)variety.c_str(), text);
	current_period=0;
	write_cputime(0);
	return reval;
}

void VM::poll() {
	FILE* fp;
	string arg_list, status;
	char buffer[1024];
	time_t current_time;
	
	arg_list="";
	arg_list="showvminfo "+virtual_machine_name+" --machinereadable" ;
	if (!vbm_popen(arg_list,buffer,sizeof(buffer))){
		fprintf(stderr,"Get_status failed!\n");
		boinc_finish(1);
	}

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
			fprintf(stderr,"poll running\n");
		}
		return;
		fprintf(stderr,"running!\n");  //testing
	}
	if(status.find("VMState=\"paused\"") != string::npos){
		time_t current_time;
		if(!suspended){
			suspended=true;
	                current_time=time(NULL);
	                current_period += difftime (current_time,last_poll_point);
	        }
		fprintf(stderr,"paused!\n");  //testing
		return;
	}
	if(status.find("VMState=\"poweroff\"") != string::npos 
		||status.find("VMState=\"saved\"") != string::npos){
		fprintf(stderr,"poweroff or saved!\n"); //testing
		exit(0);
	}
	fprintf(stderr,"Get cernvm status error!\n");
	remove();
	boinc_finish(1);
}

void poll_boinc_messages(VM& vm) {
    BOINC_STATUS status;
    boinc_get_status(&status);
    if (status.no_heartbeat) {
	fprintf(stderr,"no_heartbeat");
	vm.check();
        exit(0);
    }
    if (status.quit_request) {
     	fprintf(stderr,"quit_request");
        vm.check();
        exit(0);
    }
    if (status.abort_request) {
	fprintf(stderr,"abort_request");   	
        vm.kill();
        vm.send_cputime_message();
		vm.remove();
        boinc_finish(0);
    }
    if (status.suspended) {
    	fprintf(stderr,"suspend");
        if (!vm.suspended) {
            vm.stop();
        }
    } else {
    	fprintf(stderr,"resume");
        if (vm.suspended) {
            vm.resume();
        }
    }
}

void write_cputime(double cpu) {
    FILE* f = fopen(CPU_TIME, "w");
    if (!f) return;
    fprintf(f, "%lf\n", cpu);
    fclose(f);
}

void read_cputime(double& cpu) {
    double c;
    cpu = 0;
    FILE* f = fopen(CPU_TIME, "r");
    if (!f) return;
    int n = fscanf(f, "%lf",&c);
    fclose(f);
    if (n != 1) return;
    cpu = c;
}

int main(int argc, char** argv) {
	BOINC_OPTIONS options;
	double cpu_time=0;
	FILE*fp;
	char buffer[256];
	unsigned int i;
	bool graphics = false;
    bool vrdp = false;
	for (i=1; i<(unsigned int)argc; i++) {
	if (!strcmp(argv[i], "--graphics")) {
	    graphics = true;
	}
	}

	memset(&options, 0, sizeof(options));
	options.main_program = true;
	options.check_heartbeat = true;
	options.handle_process_control = true;
	options.handle_trickle_ups = true;
	if (graphics) {
	options.backwards_compatible_graphics = true;
	}
	 boinc_init_options(&options);

	VM vm;
	if(boinc_file_exists(vm.name_path.c_str())){
		bool VMexist=false;
		string arg_list;
		if((fp=fopen(vm.name_path.c_str(),"r"))==NULL){
			fprintf(stderr,"Main fopen failed\n");
			boinc_finish(1);
		}
		if(fgets(buffer,256,fp)) vm.virtual_machine_name=buffer;
		fclose(fp);

		arg_list="";
		arg_list=" list vms";
		if (!vbm_popen(arg_list,buffer,sizeof(buffer))){
			fprintf(stderr, "CernVMManager list failed!\n");
			boinc_finish(1);
		}

		string VMlist=buffer;
		if(VMlist.find(vm.virtual_machine_name.c_str()) != string::npos){
			VMexist=true;
		}

		//Maybe voluteers delete CernVM using VB GUI

		if(!VMexist){
			vm.release();
			vm.create();
		}

	}
	else{		
		fprintf(stderr,"File don't exist!\n");
		vm.create();
	}

	read_cputime(cpu_time);
	vm.current_period=cpu_time;
	vm.start(vrdp);
	vm.last_poll_point = time(NULL);
	while (1) {
		poll_boinc_messages(vm);
		vm.poll();
		if(vm.current_period >= CHECK_PERIOD)
			write_cputime(vm.current_period);
		if(vm.current_period >= TRICK_PERIOD)
		vm.send_cputime_message();
		boinc_sleep(POLL_PERIOD);
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
