#include <string>
#include <iostream>

#define PROGRESS_FN "ProgressFile"

using namespace std;

namespace Helper
{
        int unzip(const char *infilename, const char *outfilename)
        {
                gzFile infile = gzopen(infilename, "rb");
                FILE *outfile = fopen(outfilename, "wb");
                if (!infile || !outfile) return -1;
            
                char buffer[128];
                int num_read = 0;
            
                while ((num_read = gzread(infile, buffer, sizeof(buffer))) > 0) {
                        fwrite(buffer, 1, num_read, outfile);
                }
            
                gzclose(infile);
                fclose(outfile);
        }

        #ifdef _WIN32
        bool IsWinNT()
        {
                OSVERSIONINFO osv;
                osv.dwOSVersionInfoSize = sizeof(osv);
                GetVersionEx(&osv);
                return (osv.dwPlatformId == VER_PLATFORM_WIN32_NT);
        }

        bool SettingWindowsPath()
        {
                // DEBUG information:
                fprintf(stderr,"\nNOTICE: Setting VirtualBox PATH in Windows...\n");

                // First, we try to check if the VirtualBox path exists
                string old_path = getenv("path");
                string vbox_path = getenv("ProgramFiles");
                vbox_path += "\\Oracle\\VirtualBox";

                if (GetFileAttributes(vbox_path.c_str()) != INVALID_FILE_ATTRIBUTES) {
                        fprintf(stderr, "NOTICE: Success!!! Installation PATH of VirtualBox is: %s\n", vbox_path.c_str());
                        string new_path = "path=";
                        new_path += vbox_path;
                        new_path += ";";
                        new_path += old_path;
                        putenv(const_cast<char*>(new_path.c_str()));
                        fprintf(stderr, "INFO: New path %s\n", getenv("path"));
                        return (true);
                }
                else {
                        cerr << "ERROR: failing detecting the folder, trying with registry..." << endl;
                        // Second get the HKEY_LOCAL_MACHINE\SOFTWARE\Oracle\VirtualBox
                        fprintf(stderr,"INFO: Trying to grab installation path of VirtualBox from Windows Registry...\n");
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
                                        	fprintf(stderr, "NOTICE: Success!!! Installation PATH of VirtualBox is: %s;\n", szPath);
                                                fprintf(stderr, "NOTICE: Old path %s\n", old_path.c_str());
                                                
                                                string new_path = "path=";
                                                new_path += szPath;
                                                new_path += ";";
                                                new_path += old_path;
                                                putenv(const_cast<char*>(new_path.c_str()));
                                                fprintf(stderr, "INFO: New path %s\n", getenv("path"));
                                                if (szPath) free(szPath);
                                                return(true);
                                        }
    	                			
                                }
                                else {
                                                fprintf(stderr, "ERROR: Retrieving the HKEY_LOCAL_MACHINE\\SOFTWARE\\Oracle\\VirtualBox\\InstallDir value was impossible\n\n");
                                }
                                if (keyHandle) RegCloseKey(keyHandle);	
                                return (false);
                        }
                        else {
                                fprintf(stderr, "ERROR: Opening Windows registry!\n");
                                return (false);
                        }
                }
        }


        #endif

        void write_progress(double secs)
        {
                std::ofstream f(PROGRESS_FN);
                if (f.is_open()) {
                        f <<  secs;
                }
                f.close();
        }   
        
        double read_progress() {
                std::ifstream f(PROGRESS_FN);
                if (f.is_open()) {
                        if (f.good()) {
                                double stored_secs;
                                f >> stored_secs;
                                return stored_secs;
                        }
                        else {
                                return -1;
                        }
                }
                else {
                        return 0;
                }
        }
        
        double update_progress(double secs, int debug_level = 3) 
        {
                double old_secs;
            
                old_secs = read_progress();
                if (old_secs != -1) {
                        write_progress(old_secs +  secs);
                        return(old_secs + secs);
                }
                else {
                        cerr << "ERROR: Reading old_secs from ProgressFile failed" << endl;
                        boinc_finish(1);
                        return(-1);
                }
        }

        #ifdef APP_GRAPHICS
        void update_shmem() 
        {
                if (!Share::data) return;
                // always do this; otherwise a graphics app will immediately
                // assume we're not alive
                Share::data->update_time = dtime();
            
                // Check whether a graphics app is running,
                // and don't bother updating Share::data if so.
                // This doesn't matter here,
                // but may be worth doing if updating Share::data is expensive.
                if (Share::data->countdown > 0) {
                        // the graphics app sets this to 5 every time it renders a frame
                        Share::data->countdown--;
                } else {
                        return;
                }
                Share::data->fraction_done = boinc_get_fraction_done();
                Share::data->cpu_time = boinc_worker_thread_cpu_time();;
                boinc_get_status(&Share::data->status);
                boinc_get_init_data(Share::data->init_data);
        }
        #endif
}

