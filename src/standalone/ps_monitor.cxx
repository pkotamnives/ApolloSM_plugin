#include <utmpx.h>
#include <stdio.h>
#include <string>
#include <vector>
#include <ApolloSM/ApolloSM.hh>
#include <standalone/userCount.hh>
#include <standalone/lnxSysMon.hh>

#include <errno.h>
#include <string.h>

//pselect stuff
#include <sys/select.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

//signals
#include <signal.h>

//umask
#include <sys/types.h>
#include <sys/stat.h>

#include <BUException/ExceptionBase.hh>

#include <syslog.h>  ///for syslog

#define SEC_IN_US 1000000
#define NS_IN_US 1000

// ================================================================================
// Setup for boost program_options
#include <boost/program_options.hpp>
#include <standalone/progOpt.hh>
#include <fstream>
#include <iostream>
#define DEFAULT_CONFIG_FILE "/etc/ps_monitor"
#define DEFAULT_POLLTIME_IN_SECONDS 10
#define DEFAULT_RUN_DIR "/opt/address_table"
#define DEFAULT_PID_FILE "/var/run/ps_monitor.pid"
namespace po = boost::program_options;

// ====================================================================================================
// Kill program if it is in background
bool static volatile loop;

void static signal_handler(int const signum) {
  if(SIGINT == signum || SIGTERM == signum) {
    loop = false;
  }
}

// ====================================================================================================
long us_difftime(struct timespec cur, struct timespec end){ 
  return ( (end.tv_sec  - cur.tv_sec )*SEC_IN_US + 
	   (end.tv_nsec - cur.tv_nsec)/NS_IN_US);
}

// ==================================================

int main(int argc, char ** argv) {

  //=======================================================================
  // Set up program options
  //=======================================================================
  //Command Line options
  po::options_description cli_options("cmpwrdown options");
  cli_options.add_options()
    ("help,h",    "Help screen")
    ("POLLTIME_IN_SECONDS,s", po::value<int>()->implicit_value(-1),          "polltime in seconds")
    ("RUN_DIR,r",             po::value<std::string>()->implicit_value(""), "run path")
    ("PID_FILE,d",            po::value<std::string>()->implicit_value(""), "pid file");

  //Config File options
  po::options_description cfg_options("cmpwrdown options");
  cfg_options.add_options()
    ("POLLTIME_IN_SECONDS", po::value<int>(),         "polltime in seconds")
    ("RUN_DIR",             po::value<std::string>(), "run path")
    ("PID_FILE",            po::value<std::string>(), "pid file");

  //variable_maps for holding program options
  po::variables_map cli_map;
  po::variables_map cfg_map;

  //Store command line and config file arguments into cli_map and cfg_map
  try {
    cli_map = storeCliArguments(cli_options, argc, argv);
  } catch (std::exception &e) {
    std::cout << cli_options << std::endl;
    return 0;
  }

  try {
    cfg_map = storeCfgArguments(cfg_options, DEFAULT_CONFIG_FILE);  
  } catch (std::exception &e) {}
  
  //Help option - ends program
  if(cli_map.count("help")){
    std::cout << cli_options << '\n';
    return 0;
  }

  //Set polltime_in_seconds
  int polltime_in_seconds = DEFAULT_POLLTIME_IN_SECONDS;
  setOptionValue(polltime_in_seconds, "POLLTIME_IN_SECONDS", cli_map, cfg_map);
  //Set runPath
  std::string runPath = DEFAULT_RUN_DIR;
  setOptionValue(runPath, "RUN_DIR", cli_map, cfg_map);
  //set pidFileName
  std::string pidFileName = DEFAULT_PID_FILE;
  setOptionValue(pidFileName, "PID_FILE", cli_map, cfg_map);

  // ============================================================================
  // Deamon book-keeping
  pid_t pid, sid;
  pid = fork();
  if(pid < 0){
    //Something went wrong.
    //log something
    exit(EXIT_FAILURE);
  }else if(pid > 0){
    //We are the parent and created a child with pid pid
    FILE * pidFile = fopen(pidFileName.c_str(),"w");
    fprintf(pidFile,"%d\n",pid);
    fclose(pidFile);
    exit(EXIT_SUCCESS);
  }else{
    // I'm the child!
    //open syslog
    openlog(NULL,LOG_CONS|LOG_PID,LOG_DAEMON);
  }

  
  //Change the file mode mask to allow read/write
  umask(0);

  //Start logging
  syslog(LOG_INFO,"Opened log file\n");

  // create new SID for the daemon.
  sid = setsid();
  if (sid < 0) {
    syslog(LOG_ERR,"Failed to change SID\n");
    exit(EXIT_FAILURE);
  }
  syslog(LOG_INFO,"Set SID to %d\n",sid);

  //Move to RUN_DIR
  if ((chdir(runPath.c_str())) < 0) {
    syslog(LOG_ERR,"Failed to change path to \"%s\"\n",runPath.c_str());    
    exit(EXIT_FAILURE);
  }
  syslog(LOG_INFO,"Changed path to \"%s\"\n", runPath.c_str());    

  //Everything looks good, close the standard file fds.
  close(STDIN_FILENO);
  close(STDOUT_FILENO);
  close(STDERR_FILENO);

  // ============================================================================
  // Daemon code setup

  // ====================================
  // Signal handling
  struct sigaction sa_INT,sa_TERM,old_sa;
  memset(&sa_INT ,0,sizeof(sa_INT)); //Clear struct
  memset(&sa_TERM,0,sizeof(sa_TERM)); //Clear struct
  //setup SA
  sa_INT.sa_handler  = signal_handler;
  sa_TERM.sa_handler = signal_handler;
  sigemptyset(&sa_INT.sa_mask);
  sigemptyset(&sa_TERM.sa_mask);
  sigaction(SIGINT,  &sa_INT , &old_sa);
  sigaction(SIGTERM, &sa_TERM, NULL);
  loop = true;


  // ==================================================
  // If /var/run/utmp does not exist we are done
  {
    FILE * file = fopen("/var/run/utmp","r");
    if(NULL == file) {
      syslog(LOG_INFO,"Utmp file does not exist. Terminating countUsers\n");
      return -1;
    }
  }

  //=======================================================================
  // Start ps monitor
  //=======================================================================
  ApolloSM * SM = NULL;
  try{
    // ==================================
    // Initialize ApolloSM
    SM = new ApolloSM();
    if(NULL == SM){
      syslog(LOG_ERR,"Failed to create new ApolloSM\n");
      exit(EXIT_FAILURE);
    }else{
      syslog(LOG_INFO,"Created new ApolloSM\n");      
    }
    std::vector<std::string> arg;
    arg.push_back("connections.xml");
    SM->Connect(arg);

    //vars for pselect
    fd_set readSet,readSet_ret;
    FD_ZERO(&readSet);
    struct timespec timeout = {polltime_in_seconds,0};
    int maxFDp1 = 0;
    
    //Create a usercount process
    userCount uCnt;
    int fdUserCount = uCnt.initNotify();
    syslog(LOG_INFO,"iNotify setup on FD %d\n",fdUserCount);
    FD_SET(fdUserCount,&readSet);
    if(fdUserCount >= maxFDp1){
      maxFDp1 = fdUserCount+1;
    }


    // ==================================
    // Main DAEMON loop
    syslog(LOG_INFO,"Starting PS monitor\n");

    // ==================================================
    // All the work

    //Do one read of users file before we start our loop
    uint32_t superUsers,normalUsers;
    int inRate, outRate;
    int networkMon_return = networkMonitor(inRate, outRate); //run once to burn invalid first values
    uCnt.GetUserCounts(superUsers,normalUsers);
    try {
      SM->RegWriteRegister("PL_MEM.USERS_INFO.SUPER_USERS.COUNT",superUsers);
      SM->RegWriteRegister("PL_MEM.USERS_INFO.USERS.COUNT",normalUsers);	  
    }catch(std::exception const & e){
      syslog(LOG_ERR,"Caught std::exception: %s\n",e.what());          
    }
    while(loop){
      readSet_ret = readSet;
      int pselRet = pselect(maxFDp1,&readSet_ret,NULL,NULL,&timeout,NULL);
      if(0 == pselRet){
	//timeout, do CPU/mem monitoring
	uint32_t mon;
	mon = MemUsage()*100; //Scale the value by 100 to get two decimal places for reg   
	try {
	  SM->RegWriteRegister("PL_MEM.ARM.MEM_USAGE",mon);
	}catch(std::exception const & e){
	  syslog(LOG_ERR,"Caught std::exception: %s\n",e.what());          
	}
	mon = CPUUsage()*100; //Scale the value by 100 to get two decimal places for reg   
	try {
	  SM->RegWriteRegister("PL_MEM.ARM.CPU_LOAD",mon);
	}catch(std::exception const & e){
	  syslog(LOG_ERR,"Caught std::exception: %s\n",e.what());          
	}
	networkMon_return = networkMonitor(inRate, outRate);
	if(!networkMon_return){ //networkMonitor was successful
	  try {
	    SM->RegWriteRegister("PL_MEM.NETWORK.ETH0.RX",uint32_t(inRate));
	    SM->RegWriteRegister("PL_MEM.NETWORK.ETH0.TX",uint32_t(outRate));
	  }catch(std::exception const & e){
	    syslog(LOG_ERR,"Caught std::exception: %s\n",e.what());          
	  }
	} else { //networkMonitor failed
	  syslog(LOG_ERR, "Error in networkMonitor, return %d\n", networkMon_return);
	}
	float days,hours,minutes;
	Uptime(days,hours,minutes);
	try {
	  SM->RegWriteRegister("PL_MEM.ARM.SYSTEM_UPTIME.DAYS",uint32_t(100.0*days));
	  SM->RegWriteRegister("PL_MEM.ARM.SYSTEM_UPTIME.HOURS",uint32_t(100.0*hours));
	  SM->RegWriteRegister("PL_MEM.ARM.SYSTEM_UPTIME.MINS",uint32_t(100.0*minutes));
	} catch(std::exception const & e){
	  syslog(LOG_ERR,"Caught std::exception: %s\n",e.what());          
	}
      }else if(pselRet > 0){
	//a FD is readable. 
	if(FD_ISSET(fdUserCount,&readSet_ret)){
	  if(uCnt.ProcessWatchEvent()){
	    uCnt.GetUserCounts(superUsers,normalUsers);
	    try {
	      SM->RegWriteRegister("PL_MEM.USERS_INFO.SUPER_USERS.COUNT",superUsers);
	      SM->RegWriteRegister("PL_MEM.USERS_INFO.USERS.COUNT",normalUsers);
	    }catch(std::exception const & e){
	      syslog(LOG_ERR,"Caught std::exception: %s\n",e.what());          
	    }
	  }
	}
      }else{
	syslog(LOG_ERR,"Error in pselect %d(%s)",errno,strerror(errno));
      }
    }
  }catch(BUException::exBase const & e){
    syslog(LOG_ERR,"Caught BUException: %s\n   Info: %s\n",e.what(),e.Description());          
  }catch(std::exception const & e){
    syslog(LOG_ERR,"Caught std::exception: %s\n",e.what());          
  }

  // ==================================================
  // Clean up. Close and delete everything.
    
  // Delete SM
  if(NULL != SM) {
    delete SM;
  }

  // Restore old action of receiving SIGINT (which is to kill program) before returning 
  sigaction(SIGINT, &old_sa, NULL);
  syslog(LOG_INFO,"PS Monitor Daemon ended\n");
  return 0;

}
