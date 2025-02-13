/*
    Shion Ito
    Department of Computer Science & Engineering
    Texas A&M University
    Date  : 10/04/2020
 */
#include "common.h"
#include <sys/wait.h>
#include "FIFOreqchannel.h"
#include "MQreqchannel.h"
#include "SHMreqchannel.h"
using namespace std;


int main(int argc, char *argv[]){
    struct timeval start, end; // used for reading time
    double time_taken;
    int c;
    int buffercap = MAX_MESSAGE;
    int p = 0, ecg = 1;
    double t = -1.0;
    bool isnewchan = false;
    bool isfiletransfer = false;
    string filename;
    string ipcmethod = "f";
    int nchannels = 1;


    while ((c = getopt (argc, argv, "p:t:e:m:f:c:i:")) != -1){
        switch (c){
            case 'p':
                p = atoi (optarg);
                break;
            case 't':
                t = atof (optarg);
                break;
            case 'e':
                ecg = atoi (optarg);
                break;
            case 'm':
                buffercap = atoi (optarg);
                break;
            case 'c':
                isnewchan = true;
                nchannels = atoi(optarg);
                break;
            case 'f':
                isfiletransfer = true;
                filename = optarg;
                break;
            case 'i':
                ipcmethod = optarg;
                break;
        }
    }
    
    // fork part
    if (fork()==0){ // child 
	    char* args [] = {"./server", "-m", (char *) to_string(buffercap).c_str(),"-i",(char*) ipcmethod.c_str(), NULL};
        if (execvp (args [0], args) < 0){
            perror ("exec filed");
            exit (0);
        }

    }

    RequestChannel* control_chan = NULL;
    if(ipcmethod == "f"){

       control_chan = new FIFORequestChannel ("control", FIFORequestChannel::CLIENT_SIDE);
    }
    else if (ipcmethod == "q"){
        control_chan = new MQRequestChannel ("control", FIFORequestChannel::CLIENT_SIDE);
    }
    
    else if (ipcmethod == "m"){
     control_chan = new SHMRequestChannel ("control", FIFORequestChannel::CLIENT_SIDE,buffercap);;
    }

    RequestChannel* chan = control_chan;
    RequestChannel* savedChannels[nchannels];
    int ClientLoop = 0;
    for(int i = 0; i<nchannels ; i++){

        ClientLoop= i + 1;
        if (isnewchan){
            cout << "Using the new channel everything following" << endl;
            MESSAGE_TYPE m = NEWCHANNEL_MSG;
            control_chan->cwrite (&m, sizeof (m));
            char newchanname [100];
            control_chan->cread (newchanname, sizeof (newchanname));
            if(ipcmethod == "f")
                chan = new FIFORequestChannel (newchanname, RequestChannel::CLIENT_SIDE);
            else if(ipcmethod == "q"){
                chan = new MQRequestChannel (newchanname, RequestChannel::CLIENT_SIDE);
            }
            else if (ipcmethod == "m"){
                chan = new SHMRequestChannel (newchanname, RequestChannel::CLIENT_SIDE,buffercap);
            }
            savedChannels[i] = chan;

            cout << "New channel by the name " << newchanname << " is created" << endl;
            cout << "All further communication will happen through it instead of the main channel" << endl << endl;
        }
        
        if (!isfiletransfer){   // requesting data msgs
            if (t >= 0){    // 1 data point
                datamsg d (p, t, ecg);
                chan->cwrite (&d, sizeof (d));
                gettimeofday(&start,NULL);
                double ecgvalue;
                chan->cread (&ecgvalue, sizeof (double));
                gettimeofday(&end,NULL);
                cout << "Ecg " << ecg << " value for patient "<< p << " at time " << t << " is: " << ecgvalue << endl;
                time_taken = (end.tv_sec - start.tv_sec) * 1e6; 
                time_taken = (time_taken + (end.tv_usec -  start.tv_usec)) * 1e-6;
                cout << "The time taken is -> " << time_taken <<endl;
            }else{          // bulk (i.e., 1K) data requests 
                double ts = 0;  
                datamsg d (p, ts, ecg);
                double ecgvalue;
                gettimeofday(&start,NULL);
                for (int i=0; i<1000; i++){
                    chan->cwrite (&d, sizeof (d));
                    chan->cread (&ecgvalue, sizeof (double));
                    d.seconds += 0.004; //increment the timestamp by 4ms
                    cout << ecgvalue << endl;
                }
                gettimeofday(&end,NULL);
                time_taken = (end.tv_sec - start.tv_sec) * 1e6; 
                time_taken = (time_taken + (end.tv_usec -  start.tv_usec)) * 1e-6;
                cout << "The time taken is -> " << time_taken <<endl;
            }
        }
        else if (isfiletransfer){
        // part 2 requesting a file
        filemsg f (0,0);  // special first message to get file size
        int to_alloc = sizeof (filemsg) + filename.size() + 1; // extra byte for NULL
        char* buf = new char [to_alloc];
        memcpy (buf, &f, sizeof(filemsg));
        strcpy (buf + sizeof (filemsg), filename.c_str());
        chan->cwrite (buf, to_alloc);
        __int64_t filesize;
        chan->cread (&filesize, sizeof (__int64_t));
        if(i == nchannels -1 ){
            cout << "File size: " << filesize/nchannels  + filesize%nchannels<< endl;
        }
        else cout << "File size: " << filesize/nchannels << endl;

        filemsg* fm = (filemsg*) buf;
        __int64_t rem = filesize / nchannels;
        string outfilepath = string("received/") + filename;
        FILE* outfile;

        
        if(i == 0){
            outfile = fopen (outfilepath.c_str(), "wb");  //wb means to write binaray from beginning "ab" append binary
        }
        else{
             outfile = fopen (outfilepath.c_str(), ("wb","a+"));
        }
        gettimeofday(&start,NULL);
        fm->offset = rem*i; // Change the offset depending on which child
        
        if(i == nchannels -1 ){
            rem += filesize %nchannels;
        }

        char* recv_buffer = new char [MAX_MESSAGE];
        while (rem>0){
            fm->length = (int) min (rem, (__int64_t) MAX_MESSAGE);
            chan->cwrite (buf, to_alloc);
            chan->cread (recv_buffer, MAX_MESSAGE);
            fwrite (recv_buffer, 1, fm->length, outfile);
            rem -= fm->length;
            fm->offset += fm->length;
            //cout << fm->offset << endl;
        }
        gettimeofday(&end,NULL);
        fclose (outfile);
        time_taken = (end.tv_sec - start.tv_sec) * 1e6; 
        time_taken = (time_taken + (end.tv_usec -  start.tv_usec)) * 1e-6;

        cout << "The time taken is -> " << time_taken <<endl;
        delete recv_buffer;
        delete buf;
        cout << "File transfer completed" << endl;
    }
    
    }
    MESSAGE_TYPE q = QUIT_MSG;
    chan->cwrite (&q, sizeof (MESSAGE_TYPE));

    if (chan != control_chan ){ // this means that the user requested a new channel, so the control_channel must be destroyed as well 
        control_chan->cwrite (&q, sizeof (MESSAGE_TYPE));
    }
    if (isnewchan){
        for(int i = 0; i< nchannels; i++){
            delete savedChannels[i];
        }
    }
    // wait for the child process running server
    // this will allow the server to properly do clean up
    // if wait is not used, the server may sometimes crash
	wait (0);
    
}