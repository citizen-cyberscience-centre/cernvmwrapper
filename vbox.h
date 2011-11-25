#include <iostream>
#include <string>

#ifdef APP_GRAPHICS
#include "boincShare.h" // provided by CernVM-Graphics
Share::SharedData* Share::data;
#endif

#include "helper.h"

#define VM_NAME "VMName"
#define CPU_TIME "CpuTime"
#define TRICK_PERIOD 45.0*60
#define CHECK_PERIOD 2.0*60
#define POLL_PERIOD 1.0
#define MESSAGE "CPUTIME"
#define YEAR_SECS 365*24*60*60
#define BUFSIZE 4096

using std::string;

struct VM {
        string virtual_machine_name;
        string disk_name;
        string disk_path;
        string name_path;
        
        double current_period;
        time_t last_poll_point;
            
        bool suspended;
        int  poll_err_number;
        int  poweroff_err_number;
        int  start_err_number;
        int  debug_level;
        int  n_cpus;
        
        VM();
        void create();
        bool exists();
        void throttle();
        void start(bool vrde, bool headless);
        void kill();
        void pause();
        void savestate();
        void resume();
        void Check();    
        void remove();
        void release(); 
        void poll();
};

//void write_cputime(double);

APP_INIT_DATA aid;

// Run VBoxManage commands to interact with the virtual machine.
// When buffer is NULL, this function will not return the input of new process.
// Otherwise, it will not redirect the input of new process to buffer
bool vbm_popen(string arg_list, char * buffer=NULL, int nSize=1024, 
                                            string command="VBoxManage -q ") {
#ifdef _WIN32
        STARTUPINFO si;
        SECURITY_ATTRIBUTES sa;
        SECURITY_DESCRIPTOR sd; //security information for pipes
        PROCESS_INFORMATION pi;
        HANDLE newstdout,read_stdout; //pipe handles
        unsigned long exit=0;
        if (buffer!=0) {
            //initialize security descriptor (Windows NT)
            if (Helper::IsWinNT()) {
                    InitializeSecurityDescriptor(&sd,SECURITY_DESCRIPTOR_REVISION);
                    SetSecurityDescriptorDacl(&sd, true, NULL, false);
                    sa.lpSecurityDescriptor = &sd;
            }
            else sa.lpSecurityDescriptor = NULL;
            sa.nLength = sizeof(SECURITY_ATTRIBUTES);
            sa.bInheritHandle = true; //allow inheritable handles

            //create stdout pipe
            if (!CreatePipe(&read_stdout,&newstdout,&sa,0)) {
                    cerr << "ERROR: CreatePipe failed!!" << endl;
                    CloseHandle(newstdout);
                    CloseHandle(read_stdout);
                    return false;
            }
        }

        GetStartupInfo(&si);
        si.dwFlags = STARTF_USESHOWWINDOW;
        si.wShowWindow = SW_HIDE;

        if (buffer != NULL) {
                si.dwFlags = STARTF_USESTDHANDLES|si.dwFlags;
                si.hStdOutput = newstdout;
                si.hStdError = newstdout;   //set the new handles for the child process
                si.hStdInput = NULL;
        }
    
        command += arg_list;

        if (!CreateProcess( NULL, (LPTSTR)command.c_str(), NULL, NULL, TRUE,
                                    CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
                cerr << "ERROR: CreateProcess failed!" << endl;
                if (buffer!=NULL) {
                        CloseHandle(newstdout);
                        CloseHandle(read_stdout);
                }
                return false;
        }
    
        // Wait until process exits.
        WaitForSingleObject(pi.hProcess, INFINITE);
    
        // Close process and thread handles.
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    
        if (buffer!=NULL) {
                memset(buffer, 0, nSize);
                DWORD bread;
                BOOL bSuccess = false;
    
                for (;;)
                {
                    ReadFile(read_stdout, buffer, nSize-1, &bread, NULL);
                    if (!bSuccess || bread == 0) break;
                }
                // buffer[bread]=0;
                CloseHandle(newstdout);
                CloseHandle(read_stdout);
        }
    
        if (exit == 0) 
            return true;
        else
            return false;
// GNU/Linux and Mac OS X code
#else     
        FILE *fp;
        char temp[256];
        string strTemp = "";
        command += arg_list;
        if (buffer == NULL) {
                if(!system(command.c_str()))
                        return true;
                else return false;
        }
    
        fp = popen(command.c_str(), "r");
        if (fp == NULL) {
                cerr << "ERROR: vbm_popen failed" << endl;
                return false;
        }

        memset(buffer, 0, nSize);
        while (fgets(temp,256,fp) != NULL) {
            strTemp += temp;
        }

        pclose(fp);
        strncpy(buffer, strTemp.c_str(), nSize-1);
        return true;
#endif
}

VM::VM() {
        char buffer[256];
    
        virtual_machine_name = "";
        current_period = 0;
        suspended = false;
        last_poll_point = 0;
        poll_err_number = 0;
        poweroff_err_number = 0;
        start_err_number = 0;
        debug_level = 3;
        n_cpus = 1;
        
        boinc_getcwd(buffer);
        disk_name = "cernvm.vmdk";
        disk_path = "cernvm.vmdk";
        disk_path = "/"+disk_path;
        disk_path = buffer+disk_path;
        disk_path = "\""+disk_path+"\"";

        name_path = "";
        name_path += VM_NAME;
}   

void VM::create() 
{
        time_t rawtime;
        string arg_list;

        //createvm
        arg_list = "createvm --name " + virtual_machine_name + " --ostype Linux26 --register";
        if (!vbm_popen(arg_list)) {
                cerr << "ERROR: Create VM method -> createvm failed! Aborting" << endl;
                cerr << "ERROR: " << arg_list << endl;
                if (debug_level >= 3) {
                        cerr << "NOTICE: Removing registered VM because to clean the system" << endl; 
                }
                remove();
                boinc_finish(1);
        }
    
        //modifyvm
        arg_list.clear();
        std::stringstream tmp;
        tmp << n_cpus;
        arg_list = "modifyvm " + virtual_machine_name + \
                " --cpus " + tmp.str() + " --memory 256 --acpi on --ioapic on \
                  --boot1 disk --boot2 none --boot3 none --boot4 none \
                  --nic1 nat \
                  --natdnsproxy1 on";
    
        vbm_popen(arg_list);

        // Enable port-forwarding if compiled with the CernVM-Graphics
        #ifdef APP_GRAPHICS
            if (debug_level >= 4) {
                    cerr << "INFO: Enabling Port Forwarding in the Virtual Machine" << endl;
            }
            arg_list.clear();
            arg_list = " modifyvm " + virtual_machine_name + \
                       " --natpf1  \"graphicsvm,tcp,127.0.0.1,7859,,80\"";
            vbm_popen(arg_list);
        #endif
    
        //storagectl
        arg_list.clear();
        arg_list = "storagectl " + virtual_machine_name + \
                   " --name \"IDE Controller\" --add ide --controller PIIX4";
        vbm_popen(arg_list);
    
    
        //storageattach
        arg_list.clear();
        arg_list = "storageattach " + virtual_machine_name + \
                   " --storagectl \"IDE Controller\" \
                     --port 0 --device 0 --type hdd --medium " \
                   + disk_path;

        if (!vbm_popen(arg_list)) {
                cerr << "ERROR: Create storageattach failed! Aborting" << endl;
                cerr << "ERROR: " << arg_list << endl;
                remove();
                boinc_finish(1);
        }

        // Create VM
        std::ofstream f(name_path.c_str());
        if (f.is_open()) {
                if (f.good()) {
                    f << virtual_machine_name;
                }
                f.close();
        }
        else {
                cerr << "ERROR: Saving VM name failed! Details -> ofstream failed! Aborting" << endl;
                boinc_finish(1);
        }
}

bool VM::exists()
{
        std::ifstream f("VMName");
        if (f.is_open()) {
                f.close();
                return true;
        } 
        else {
                return false;
        }
}

void VM::throttle()
{
        // Check the BOINC CPU preferences for running the VM accordingly
        string arg_list;
        boinc_get_init_data(aid);

        cerr << "INFO: Number of cores: " << n_cpus << endl;

        if (aid.project_preferences) {
                if (!aid.project_preferences) return;
                double max_vm_cpu_pct = 100.0;
                if (parse_double(aid.project_preferences, "<max_vm_cpu_pct>", 
                                                                    max_vm_cpu_pct)) {
                        if (debug_level >= 3) {
                                cerr << "NOTICE: Maximum usage of CPU: " << max_vm_cpu_pct << endl;
                                cerr << "NOTICE: Setting how much CPU time the virtual CPU can use: " << max_vm_cpu_pct << endl;
                        }

                        std::stringstream out;
                        out << int(max_vm_cpu_pct);
    
                        arg_list = " controlvm " + virtual_machine_name + " cpuexecutioncap " + out.str();
                        if (!vbm_popen(arg_list)) {
                                cerr << "ERROR: Impossible to set up CPU percentage usage limit" << endl;
                        }
                        else {
                            if (debug_level >= 3) {
                                    cerr << "NOTICE: Success!" << endl;
                            }
                        }
                }
        }
}

void VM::start(bool vrde=false, bool headless=false) 
{
        // Start the VM in headless mode
        boinc_begin_critical_section();
        string arg_list="";
    
        if (headless) arg_list = " startvm " + virtual_machine_name + " --type headless";
        else arg_list = " startvm " + virtual_machine_name;
        if (!vbm_popen(arg_list)) {
                start_err_number += 1;
                cerr << "ERROR: Impossible to start the VM, seems to be locked " << start_err_number << " time" << endl;

                if (debug_level >= 3 ) {
                        cerr << "NOTICE: Waiting 2 seconds to unlock the VM" << endl;
                }
                boinc_sleep(2);
    
                if (start_err_number > 4) {
                        cerr << "ERROR: Impossible to start the VM after " << start_err_number << " times" << endl;
                        cerr << "ERROR: Removing the VM" << endl;
                        remove();
                        boinc_end_critical_section();
                        boinc_finish(1);
                }
        }
    
        // Resetting the error counter
        start_err_number = 0;
        if (debug_level >=3) cerr << "NOTICE: VM has been started!" << endl;
    
        // Enable or disable VRDP for the VM: (by default is disabled)
        if (vrde) {
                arg_list.clear();
                arg_list = " controlvm " + virtual_machine_name + " vrde on";
        }
        else {
                arg_list.clear(); 
                arg_list = " controlvm " + virtual_machine_name + " vrde off";
        }

        vbm_popen(arg_list);
    
        // If not running in Headless mode, don't allow the user to save, shutdown, power off or restore the VM
        if (!headless) {
                arg_list.clear();
                // Don't allow the user to save, shutdown, power off or restore the VM
                arg_list = " setextradata " + virtual_machine_name + " GUI/RestrictedCloseActions SaveState,Shutdown,PowerOff,Restore";
                vbm_popen(arg_list);
        }
    
        throttle();
        boinc_end_critical_section();
}

void VM::kill() 
{
        boinc_begin_critical_section();
        string arg_list("controlvm " + virtual_machine_name + " poweroff");
        vbm_popen(arg_list);
        boinc_end_critical_section();
}

void VM::pause() 
{
        boinc_begin_critical_section();
        string arg_list("controlvm " + virtual_machine_name + " pause");
        if (vbm_popen(arg_list)) {
                suspended = true;
                time_t current_time = time(NULL);
                current_period += difftime (current_time, last_poll_point);
        }
    
        boinc_end_critical_section();
}

void VM::resume() 
{
        boinc_begin_critical_section();
        string arg_list("controlvm " + virtual_machine_name + " resume");
        if (vbm_popen(arg_list)) {
                suspended = false;
                last_poll_point = time(NULL);
        }

        boinc_end_critical_section();
}

void VM::Check()
{
        boinc_begin_critical_section();
        string arg_list;
        if (suspended) {
                arg_list= "controlvm " + virtual_machine_name + " resume";
                vbm_popen(arg_list);
        }
        arg_list = "controlvm " + virtual_machine_name + " savestate";
        vbm_popen(arg_list);
        boinc_end_critical_section();
}

void VM::savestate()
{
        boinc_begin_critical_section();
        string arg_list("controlvm " + virtual_machine_name + " savestate");
        if (!vbm_popen(arg_list)) {
                cerr << "ERROR: The VM could not be saved" << endl;
        }
        boinc_end_critical_section();
}

void VM::remove() 
{
        boinc_begin_critical_section();
        string arg_list, vminfo, vboxfolder, vboxXML, vboxXMLNew, vmfolder, vmdisk;
        char *env;
        bool vmRegistered = false;
    
        arg_list = " discardstate " + virtual_machine_name;
        if (vbm_popen(arg_list)) {
                if (debug_level >= 3) {
                        cerr << "NOTICE: VM state discarded!" << endl;
                }
        }
        else {
                if (debug_level >= 2) {
                        cerr << "WARNING: it was not possible to discard the state of the VM" << endl;
                }
        }
    
        // Wait to allow to discard the VM state cleanly
        boinc_sleep(2);
    
        // Unregistervm command with --delete option. VBox 4.1 should work well
        arg_list.clear();
        arg_list = " unregistervm " + virtual_machine_name + " --delete";
        if (vbm_popen(arg_list)) {
                if (debug_level >= 3) {
                        cerr << "NOTICE: VM removed via VBoxManage" << endl;
                }
        }
        else {
            if (debug_level >= 2) {
                    cerr << "WARNING: The VM could not be removed via VBoxManage" << endl;
            }
        }
        
        // We test if we can remove the hard disk controller. If the command works, the cernvm.vmdk virtual disk will be also
        // removed automatically
    
        arg_list.clear();
        arg_list = " storagectl  " + virtual_machine_name + " --name \"IDE Controller\" --remove";
        if (vbm_popen(arg_list)) {
            if (debug_level >= 3) {
                    cerr << "NOTICE: Hard disk removed!" << endl;
            }
        }
        else {
                if (debug_level >= 2) {
                        cerr << "WARNING: it was not possible to remove the IDE controller" << endl;
                }
        } 
    
        #ifdef _WIN32
    	env = getenv("HOMEDRIVE");
    	if (debug_level >= 3) {
                cerr << "NOTICE: I'm running in a Windows system..." << endl;
        }
    	vboxXML = string(env);
    	env = getenv("HOMEPATH");
    	vboxXML = vboxXML + string(env);
    	vboxfolder = vboxXML + "\\VirtualBox VMs\\";
    	vboxXML = vboxXML + "\\.VirtualBox\\VirtualBox.xml";
        #else 

        env = getenv("HOME");
        vboxXML = string(env);
    
        if (vboxXML.find("Users") == string::npos) {
            // GNU/Linux
            vboxXML = vboxXML + "/.VirtualBox/VirtualBox.xml";
            vboxfolder = string(env) + "/.VirtualBox/";
            if (debug_level >= 3) {
                    cerr << "NOTICE: I'm running in a GNU/Linux system..." << endl;
            }
        }
        else {
            // Mac OS X
            vboxXML = vboxXML + "/Library/VirtualBox/VirtualBox.xml";
            vboxfolder = string(env) + "/Library/VirtualBox/";
            if (debug_level >= 3) {
                    cerr << "NOTICE: I'm running in a Mac OS X system..." << endl;
            }
        }
        #endif
    
        std::ifstream in(vboxXML.c_str());
    
        if (in.is_open()) {
                vboxXMLNew = vboxfolder + "VirtualBox.xmlNew";
                std::ofstream out(vboxXMLNew.c_str());
    
                int line_n = 0;
                size_t found_init, found_end;
                string line;
                while (std::getline(in,line)) {
                        found_init = line.find("BOINC_VM");
                        if (found_init == string::npos)
                                out << line + "\n";
                        else {
                                vmRegistered = true; 
                                if (debug_level >= 3) {
                                        cerr << "NOTICE: Obtaining the VM folder..." << endl;
                                }
                                found_init = line.find("src=");
                                found_end = line.find(virtual_machine_name + ".vbox");
                                if (found_end != string::npos)
                                    if (debug_level >= 3) {
                                            cerr << "NOTICE: .vbox found at line: " << line_n << " in the VirtualBox.xml file" << endl;
                                    }
                                vmfolder = line.substr(found_init + 5, found_end - (found_init+5));
                                if (debug_level >= 3) {
                                        cerr << "NOTICE: Done!" << endl;
                                }
                        }
                        line_n +=1;
                }
                in.close();
                out.close();
        }
    
        // When the project is reset, we have to first unregister the VM, else we will have an error.
        arg_list = "unregistervm " + virtual_machine_name;
        if (!vbm_popen(arg_list)) {
                if (debug_level >= 3) {
                        cerr << "NOTICE: CernVM does not exist, so it is not necessary to unregister it" << endl;
                }
        }
        else {
            if (debug_level >= 3) {
                    cerr << "NOTICE: Successfully unregistered the CernVM" << endl;
            }
        }

        // Delete old VirtualBox.xml and replace with new one
        std::remove(vboxXML.c_str());
        std::rename(vboxXMLNew.c_str(),vboxXML.c_str());
    
        // Remove remaining BOINC_VM folder
        #ifdef _WIN32
    	if (vmRegistered) {
    	        vmfolder = "RMDIR \"" + vmfolder + "\" /s /q";
    	        if (system(vmfolder.c_str()) == 0) {
    			if (debug_level >= 3) {
                                cerr << "NOTICE: VM folder deleted!" << endl;
                        }
                }
    		else {
                        if (debug_level >= 3) {
                                cerr << "NOTICE: System was clean, nothing to delete" << endl;
                        }
                }
    	}
        else {
                if (debug_level >= 3) {
                        cerr << "NOTICE: VM was not registered, deleting old VM folders..." << endl;
                }
                vmfolder = "RMDIR \"" + vboxfolder + virtual_machine_name + "\" /s /q";
                if (system(vmfolder.c_str()) == 0) {
                        if (debug_level >= 3) {
                                cerr << "NOTICE: VM folder deleted!" << endl;
                        }
                }
                else {
                        if (debug_level >= 3) {
                                cerr << "NOTICE: System was clean, nothing to delete" << endl;
                        }
                }
        }
    
        #else // GNU/Linux and Mac OS X 
        // First delete the VM folder obtained in VirtualBox.xml
        if (vmRegistered) {
                vmfolder = "rm -rf \"" + vmfolder + "\"";
                if (system(vmfolder.c_str()) == 0) {
                    if (debug_level >= 3) {
                            cerr << "NOTICE: VM folder deleted!" << endl;
                    }
                }
                else {
                    if (debug_level >= 3) {
                            cerr << "NOTICE: System was clean, nothing to delete" << endl;
                    }
                }
        }
        else {
                if (debug_level >= 3) {
                        cerr << "NOTICE: VM was not registered, deleting old VM folders..." << endl;
                }
                vmfolder = "rm -rf \"" + string(env) + "/VirtualBox VMs/" + virtual_machine_name + "\" ";
                if ( system(vmfolder.c_str()) == 0 ) {
                        if (debug_level >= 3) {
                                cerr << "NOTICE: VM folder deleted!" << endl; 
                        }
                }
                else {
                        if (debug_level >= 3) {
                                cerr << "NOTICE: System was clean, nothing to delete" << endl;
                        }
                }
        }
        #endif
        boinc_end_critical_section();
}
    
void VM::release()
{
    boinc_begin_critical_section();
    string arg_list("closemedium disk " + disk_path);
    if(!vbm_popen(arg_list)) {
            cerr << "ERROR: It was impossible to release the virtual hard disk" << endl;
    }
    else {
            if (debug_level >= 3) {
                    cerr << "NOTICE: Virtual hard disk unregistered" << endl;
            }
    }
    boinc_end_critical_section();
}

void VM::poll() 
{
    boinc_begin_critical_section();
    string arg_list, status;
    char buffer[1024];
    time_t current_time;
    
    arg_list = "showvminfo " + virtual_machine_name + " --machinereadable";
    if (!vbm_popen(arg_list, buffer, sizeof(buffer))) {
            // Increase the number of errors
            double wait_time = 5.0;
            poll_err_number += 1;
            cerr << "ERROR: Get status from VM failed " << poll_err_number << " times!" << endl;
            if (debug_level >= 3) {
                    cerr << "WARNING: Sleeping poll for " << wait_time << " seconds" << endl;
            }
            boinc_sleep(wait_time);
            if (debug_level >= 3) {
                    cerr << "INFO: Resumming poll" << endl;
            } 
            if (poll_err_number > 4) {
                    cerr << "ERROR: Get status from the VM has failed " << poll_err_number << " times!" << endl;
                    cerr << "ERROR: Aborting the execution" << endl;
                    remove();
                    boinc_end_critical_section();
                    boinc_finish(1);
            }
    }
    else {
            // Each time we read the status we reset the counter of errors
            poll_err_number = 0;

            status = buffer;
            if (status.find("VMState=\"running\"") != string::npos) {
                    if (suspended) {
                            suspended=false;
                            last_poll_point=time(NULL);
                    }
                    else {
                            current_time=time(NULL);
                            current_period += difftime (current_time,last_poll_point);
                            last_poll_point = current_time;
                            if (debug_level >= 4) {
                                    cerr << "INFO: VM poll is running" << endl;
                            }
                    }

                    boinc_end_critical_section();

                    // Reset poweroff error counter, as the VM is running:
                    if ((debug_level >= 3) && (poweroff_err_number > 0)) {
                            cerr << "NOTICE: Resetting poweroff counter!" << endl;
                            cerr << "NOTICE: Virtual Machine up and running again" << endl;
                    }

                    poweroff_err_number = 0;
                    return;
            }

            if (status.find("VMState=\"paused\"") != string::npos) {
                    if (!suspended) {
                            suspended=true;
                            time_t current_time=time(NULL);
                            current_period += difftime (current_time, last_poll_point);
                    }

                    if (debug_level >= 3) {
                            cerr << "NOTICE: VM is paused!" << endl;
                    }
                    boinc_end_critical_section();
                    return;
            }

            if (status.find("VMState=\"poweroff\"") != string::npos) {
                    poweroff_err_number += 1;
                    if (debug_level >= 3) {
                            cerr << "WARNING: VM is powered off and it shouldn't (" << poweroff_err_number << " times!)" << endl;
                            cerr << "WARNING: Retrying in 2 seconds" << endl;
                    }
                    boinc_sleep(2);
                    boinc_end_critical_section();

                    if (poweroff_err_number > 4) {
                            cerr << "ERROR: VM has been powered off for the last " << poweroff_err_number << " poll calls!" << endl;
                            cerr << "ERROR: Cancelling Work Unit!" << endl;
                            boinc_finish(1);
                    }
            }
    }
}

void poll_boinc_messages(VM& vm, BOINC_STATUS &status) 
{
        if (status.reread_init_data_file) {
                if (vm.debug_level >= 3) {
                        cerr << "NOTICE: Project preferences have changed" << endl;
                }
                vm.throttle();
        }

        if (status.no_heartbeat) {
                if (vm.debug_level >= 3) {
                        cerr << "NOTICE: BOICN no_heartbeat" << endl;
                }
                vm.savestate();
                boinc_temporary_exit(0);
        }

        if (status.quit_request) {
                if (vm.debug_level >= 3) {
                        cerr << "NOTICE: Suspending the VM" << endl;
                }
                vm.savestate();
                boinc_temporary_exit(0);
        }

        if (status.abort_request) {
                if (vm.debug_level >= 3) {
                        cerr << "WARNING: User request to abort the WU" << endl;
                }
                vm.savestate();
                vm.remove();
                boinc_finish(EXIT_ABORTED_BY_CLIENT);
        }

        if (status.suspended) {
                if (vm.debug_level >= 4) {
                        cerr << "INFO: Pausing the VM!" << endl;
                }
                if (!vm.suspended) vm.pause();
        } else {
                if (vm.debug_level >= 4) {
                        cerr << "INFO: Resuming the VM!" << endl;
                }
                if (vm.suspended) vm.resume();
        }
}
