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

