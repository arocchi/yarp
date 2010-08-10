// -*- mode:C++; tab-width:4; c-basic-offset:4; indent-tabs-mode:nil -*-

/*
 * Copyright (C) 2007-2009 RobotCub Consortium
 * CopyPolicy: Released under the terms of the LGPLv2.1 or later, see LGPL.TXT
 */

#include <stdio.h>
#include <signal.h>
#include <yarp/os/impl/String.h>
#include <yarp/os/Run.h>

#define YARPRUN_ERROR -1

#if defined(WIN32) || defined(WIN64)
typedef DWORD PID;
typedef HANDLE FDESC;
typedef HANDLE STDIO;
#define SIGKILL 0
bool KILL(HANDLE handle,int signum=SIGTERM,bool wait=false) 
{ 
    BOOL bRet=TerminateProcess(handle,0);
    CloseHandle(handle);
    return bRet?true:false;
}
bool TERMINATE(PID pid); 
#define CLOSE(h) CloseHandle(h)
HANDLE  hZombieHunter=NULL;
HANDLE* aHandlesVector=NULL;
#ifndef __GNUC__
DWORD WINAPI ZombieHunter(__in  LPVOID lpParameter);
#else
DWORD WINAPI ZombieHunter(LPVOID lpParameter);
#endif
#else
#include <unistd.h>
#include <fcntl.h>
typedef int PID;
typedef int FDESC;
typedef void* HANDLE;
typedef PID STDIO;
int CLOSE(int h){ return close(h)==0; }
int KILL(int pid,int signum=SIGTERM,bool wait=false)
{ 
    int ret=!kill(pid,signum);
    if (wait) waitpid(pid,0,0);
    return ret; 
}
void sigchild_handler(int sig)
{   
    yarp::os::Run::CleanZombies();
}
#endif

class YarpRunProcInfo
{
public:
	YarpRunProcInfo(yarp::os::ConstString& command,yarp::os::ConstString& alias,yarp::os::ConstString& on,PID pid_cmd,HANDLE handle_cmd,bool hold)
	{ 
		m_command=command;
		m_alias=alias;
		m_on=on;
		m_pid_cmd=pid_cmd;
		m_handle_cmd=handle_cmd;
		m_bHold=hold;
	}

	virtual ~YarpRunProcInfo(){ /*Kill(SIGKILL);*/ }

	virtual bool Match(yarp::os::ConstString& alias){ return m_alias==alias; }

	virtual bool Kill(int signum=SIGTERM)
	{	
        #if defined(WIN32) || defined(WIN64)
        if (m_handle_cmd)
        {
            if (signum==SIGKILL)
            {
                return KILL(m_handle_cmd,signum);
            }
            else
            {
                return TERMINATE(m_pid_cmd);
            }
        }
        #else
        if (m_pid_cmd && !m_bHold)
	    {
		    return KILL(m_pid_cmd,signum);
		}
        #endif
		       
		return true;
	}

    virtual void Clean()
    { 
        m_pid_cmd=0; 
    }

	virtual bool IsActive()
	{
	    if (!m_pid_cmd) return false;
	    
		#if defined(WIN32) || defined(WIN64)
			DWORD status;
			return (::GetExitCodeProcess(m_handle_cmd,&status) && status==STILL_ACTIVE);
		#else
			return !kill(m_pid_cmd,0);
		#endif
	}
	
protected:
	yarp::os::ConstString m_command;
	yarp::os::ConstString m_alias;
	yarp::os::ConstString m_on;
	PID m_pid_cmd;
	HANDLE m_handle_cmd; // only windows
	bool m_bHold; // only linux

	friend class YarpRunInfoVector; 
};

class YarpRunInfoVector
{
public:
	YarpRunInfoVector()
	{
		m_nProcesses=0;
		
		for (int i=0; i<MAX_PROCESSES; ++i)
		{
			m_apList[i]=0;
		}
	}

	~YarpRunInfoVector()
	{
		for (int i=0; i<MAX_PROCESSES; ++i)
		{
			if (m_apList[i])
			{
				delete m_apList[i];
			}
		}
	}

	int Size(){ return m_nProcesses; }

	bool Add(YarpRunProcInfo *process)
	{
		mutex.wait();

		if (m_nProcesses>=MAX_PROCESSES)
		{
			fprintf(stderr,"ERROR: maximum process limit reached\n");
			mutex.post();		
			return false;
		}

		#if defined(WIN32) || defined(WIN64)
        if (hZombieHunter) TerminateThread(hZombieHunter,0);
		#endif

		m_apList[m_nProcesses++]=process;
		//fprintf(stderr,"Processes in list %d\n",m_nProcesses);

		#if defined(WIN32) || defined(WIN64)
        hZombieHunter=CreateThread(0,0,ZombieHunter,0,0,0);
		#endif

		mutex.post();
		
		return true;
	}

	int Kill(yarp::os::ConstString& alias,int signum=SIGTERM)
	{
		mutex.wait();

        YarpRunProcInfo **aKill=new YarpRunProcInfo*[m_nProcesses];
        int nKill=0;	

		for (int i=0; i<m_nProcesses; ++i)
		{
			if (m_apList[i] && m_apList[i]->Match(alias))
			{
				if (m_apList[i]->IsActive())
				{
				    aKill[nKill++]=m_apList[i]; 
				}
			}
		}
	
		mutex.post();

		for (int k=0; k<nKill; ++k)
		{
		    fprintf(stderr,"KILLING  %s (%d)\n",m_apList[k]->m_alias.c_str(),m_apList[k]->m_pid_cmd);
		    aKill[k]->Kill(signum);
		}
        
        delete [] aKill;
        
		return nKill;
	}

	int Killall(int signum=SIGTERM)
	{
	    mutex.wait();
		    
        YarpRunProcInfo **aKill=new YarpRunProcInfo*[m_nProcesses];
        int nKill=0;		
		    
	    for (int i=0; i<m_nProcesses; ++i)
		{    
	        if (m_apList[i])
			{
		        if (m_apList[i]->IsActive())
				{
				    aKill[nKill++]=m_apList[i]; 
				}
			}
		}
		
		mutex.post();
		
		for (int k=0; k<nKill; ++k)
		{
		    fprintf(stderr,"KILLING %s (%d)\n",m_apList[k]->m_alias.c_str(),m_apList[k]->m_pid_cmd);
		    aKill[k]->Kill(signum);
		}
		
		delete [] aKill;
		
		return nKill;
	}
	
	#if defined(WIN32) || defined(WIN64) 
	void GetHandles(HANDLE* &lpHandles,DWORD &nCount)
	{
		mutex.wait();

		for (int i=0; i<m_nProcesses; ++i) if (m_apList[i])
		{
			if (!m_apList[i]->IsActive())
			{
				fprintf(stderr,"CLEAN-UP %s (%d)\n",m_apList[i]->m_alias.c_str(),m_apList[i]->m_pid_cmd);
				m_apList[i]->Clean();
				delete m_apList[i];
				m_apList[i]=0;
			}
		}

		Pack();
		
		for (int i=0; i<m_nProcesses; ++i)
		{
			lpHandles[nCount+i]=m_apList[i]->m_handle_cmd;
		}

		nCount+=m_nProcesses;

		mutex.post();
	}
	#else
	void CleanZombies()
	{	
	    yarp::os::Time::delay(1.0); 
	
		mutex.wait();

        YarpRunProcInfo **apZombie=new YarpRunProcInfo*[m_nProcesses];
        int nZombies=0;

		for (int i=0; i<m_nProcesses; ++i)
		{
            if (m_apList[i] && waitpid(m_apList[i]->m_pid_cmd,0,WNOHANG)==m_apList[i]->m_pid_cmd)
            {	            
	            apZombie[nZombies++]=m_apList[i];
	            m_apList[i]=NULL;
			}
		}

		Pack();
		
		mutex.post();

		for (int z=0; z<nZombies; ++z)
		{
		     fprintf(stderr,"CLEAN-UP %s (%d)\n",apZombie[z]->m_alias.c_str(),apZombie[z]->m_pid_cmd);
	         fflush(stderr);
	         
	         apZombie[z]->Clean();
	         delete apZombie[z];
		}
		
		delete [] apZombie;
	}
	#endif

    yarp::os::Bottle PS()
	{
		mutex.wait();

        yarp::os::Bottle ps,line,grp;

		for (int i=0; i<m_nProcesses; ++i) if (m_apList[i])
		{
			line.clear();

			grp.clear();
			grp.addString("pid");
			grp.addInt(m_apList[i]->m_pid_cmd);
			line.addList()=grp;

			grp.clear();
			grp.addString("tag"); 
			grp.addString(m_apList[i]->m_alias.c_str());
			line.addList()=grp;

			grp.clear();
			grp.addString("cmd");
			grp.addString(m_apList[i]->m_command.c_str());
			line.addList()=grp;

			grp.clear();
			grp.addString("status");
			grp.addString(m_apList[i]->IsActive()?"running":"zombie");
			line.addList()=grp;

			ps.addList()=line;
		}

		mutex.post();

		return ps;
	}

	bool IsRunning(yarp::os::ConstString &alias)
	{
		mutex.wait();

		for (int i=0; i<m_nProcesses; ++i)
		{
			if (m_apList[i] && m_apList[i]->Match(alias))
			{
				if (m_apList[i]->IsActive())
				{
					mutex.post();
					return true;
				}
				else
				{
					mutex.post();
					return false;
				}
			}
		}
		
		mutex.post();

		return false;
	}

	yarp::os::Semaphore mutex;

protected:
	
	void Pack()
	{
		int tot=0;

		for (int i=0; i<m_nProcesses; ++i)
		{
			if (m_apList[i])
			{
				m_apList[tot++]=m_apList[i];
			}
		}

		for (int i=tot; i<m_nProcesses; ++i)
		{
			m_apList[i]=0;
		}

		m_nProcesses=tot;
		
		//fprintf(stderr,"%d processes in list\n",tot);
	}

	static const int MAX_PROCESSES=1024;
	int m_nProcesses;
	YarpRunProcInfo* m_apList[MAX_PROCESSES];
	YarpRunInfoVector *m_pStdioMate;
};

class YarpRunCmdWithStdioInfo : public YarpRunProcInfo
{
public:
	YarpRunCmdWithStdioInfo(yarp::os::ConstString& command,
                            yarp::os::ConstString& alias,
                            yarp::os::ConstString& on,
                            PID pid_cmd,
                            yarp::os::ConstString& stdio_server,
                            YarpRunInfoVector* pStdioVector,
                            STDIO pid_stdin,
                            STDIO pid_stdout,
                            FDESC read_from_pipe_stdin_to_cmd,
                            FDESC write_to_pipe_stdin_to_cmd,
                            FDESC read_from_pipe_cmd_to_stdout,
                            FDESC write_to_pipe_cmd_to_stdout,
                            HANDLE handle_cmd,
                            bool hold)
        : YarpRunProcInfo(command,alias,on,pid_cmd,handle_cmd,hold)
	{
		m_pid_stdin=pid_stdin;
		m_pid_stdout=pid_stdout;
		m_stdio=stdio_server;

		m_pStdioVector=pStdioVector;

		m_read_from_pipe_stdin_to_cmd=read_from_pipe_stdin_to_cmd;
		m_write_to_pipe_stdin_to_cmd=write_to_pipe_stdin_to_cmd;
		m_read_from_pipe_cmd_to_stdout=read_from_pipe_cmd_to_stdout;
		m_write_to_pipe_cmd_to_stdout=write_to_pipe_cmd_to_stdout;
	}

	virtual ~YarpRunCmdWithStdioInfo(){ /*Kill(SIGKILL);*/ }
	
	virtual void Clean()
	{
	    fflush(stdout);
	    fflush(stderr);
	    
		if (m_write_to_pipe_stdin_to_cmd)   CLOSE(m_write_to_pipe_stdin_to_cmd);
		if (m_read_from_pipe_stdin_to_cmd)  CLOSE(m_read_from_pipe_stdin_to_cmd);
		if (m_write_to_pipe_cmd_to_stdout)  CLOSE(m_write_to_pipe_cmd_to_stdout);
		if (m_read_from_pipe_cmd_to_stdout) CLOSE(m_read_from_pipe_cmd_to_stdout);
		m_write_to_pipe_stdin_to_cmd=m_read_from_pipe_stdin_to_cmd=0;
		m_write_to_pipe_cmd_to_stdout=m_read_from_pipe_cmd_to_stdout=0;
	
        yarp::os::NetworkBase::disconnect((yarp::os::ConstString("/")+m_alias+"/stdout").c_str(),(yarp::os::ConstString("/")+m_alias+"/user/stdout").c_str());
        yarp::os::NetworkBase::disconnect((yarp::os::ConstString("/")+m_alias+"/user/stdin").c_str(),(yarp::os::ConstString("/")+m_alias+"/stdin").c_str());
	
	    m_pid_cmd=0;
	
		if (m_pid_stdin)
		{  
		    KILL(m_pid_stdin,SIGTERM,true);
		    fprintf(stderr,"KILLING stdin (%d)\n",m_pid_stdin);
		    m_pid_stdin=0;
		}
		if (m_pid_stdout)
	    {
	        KILL(m_pid_stdout,SIGTERM,true);
	        fprintf(stderr,"KILLING stdout (%d)\n",m_pid_stdout);
	        m_pid_stdout=0;
	    }

		TerminateStdio();
	}

	void TerminateStdio()
	{
		if (m_on==m_stdio)
		{
			m_pStdioVector->Kill(m_alias);
		}
		else
		{
            yarp::os::Bottle msg;
			msg.fromString((yarp::os::ConstString("(killstdio ")+m_alias+")").c_str());

            yarp::os::Port port;
			port.open("...");
			for (int i=0; i<20; ++i)
			{
                if (yarp::os::NetworkBase::connect(port.getName().c_str(),m_stdio.c_str())) break;
			    yarp::os::Time::delay(1.0);
			}
			port.write(msg);
            yarp::os::NetworkBase::disconnect(port.getName().c_str(),m_stdio.c_str());
			port.close();
		}
	}

protected:
	STDIO m_pid_stdin;
    STDIO m_pid_stdout;
	yarp::os::ConstString m_stdio;
	FDESC m_write_to_pipe_stdin_to_cmd;
    FDESC m_read_from_pipe_stdin_to_cmd;
	FDESC m_write_to_pipe_cmd_to_stdout;
    FDESC m_read_from_pipe_cmd_to_stdout;

	YarpRunInfoVector *m_pStdioVector;
};

///////////////////////////
// OS INDEPENDENT FUNCTIONS
///////////////////////////

YarpRunInfoVector yarp::os::Run::m_ProcessVector;
YarpRunInfoVector yarp::os::Run::m_StdioVector;
yarp::os::ConstString yarp::os::Run::m_PortName;
yarp::os::Port* yarp::os::Run::pServerPort=0;

int yarp::os::Run::main(int argc, char *argv[]) 
{
	m_PortName="";

    if (!NetworkBase::checkNetwork())
    {
		fprintf(stderr,"ERROR: no yarp network found.\n");
        return YARPRUN_ERROR;
    }

    Property config;
    config.fromCommand(argc,argv,false);

	if (config.check("echo"))
	{
		char line[1024];
		fprintf(stderr,"Program echo started.\n");
        fflush(stderr);
		while(true)
		{
			scanf("%s",line);
			fprintf(stderr,"%s\n",line);
            fflush(stderr);
		}
		return 0;
	}

	if (config.check("segfault"))
	{
	    fprintf(stderr,"writing to forbidden location\n");
	
	    int *zero=NULL;
	    
	    *zero=0;
	    
	    return 0;
	}

	if (config.check("wait"))
	{
	    yarp::os::Time::delay(config.find("wait").asDouble());
	
	    fprintf(stderr,"Done.\n");
	    
	    return 0;
	}

	// HELP
	if (config.check("help"))
	{
		Help();
		return 0;
	}

	// SERVER
    if (config.check("server")) 
	{
		m_PortName=yarp::os::ConstString(config.find("server").asString());
		return Server();
	} 

	// CLIENT (config is from keyboard)
	if (config.check("stdio")
	 || config.check("cmd") 
	 || config.check("kill") 
	 || config.check("sigterm")
	 || config.check("sigtermall") 
	 || config.check("exit") 
	 || config.check("isrunning")
	 || config.check("ps"))
	{ 
        return SendToServer(config);
    }

	Help();
    return 0;
}

yarp::os::Bottle yarp::os::Run::SendMsg(Bottle& msg,yarp::os::ConstString target)
{
	Port port;
    port.open("...");
    for (int i=0; i<20; ++i)
	{
	    if (NetworkBase::connect(port.getName().c_str(),target.c_str())) break;
	    yarp::os::Time::delay(1.0);
	}
	Bottle response;
    port.write(msg,response);
    NetworkBase::disconnect(port.getName().c_str(),target.c_str());
    port.close();
	
    int size=response.size();
	fprintf(stderr,"RESPONSE:\n=========\n");
    for (int s=0; s<size; ++s)
    {
        //if (response.get(s).isString())
        {
            fprintf(stderr,"%s\n",response.get(s).toString().c_str());
        }
    }
    
	return response;
}

void sigint_handler(int sig)
{
	if (yarp::os::Run::pServerPort)
	{
		yarp::os::Port *pClose=yarp::os::Run::pServerPort;
		yarp::os::Run::pServerPort=0;
		pClose->close();
	}
}

int yarp::os::Run::Server()
{
	Port port;
	port.open(m_PortName.c_str());
	pServerPort=&port;

	signal(SIGINT,sigint_handler);
	signal(SIGTERM,sigint_handler);

	#if !defined(WIN32) && !defined(WIN64)
	signal(SIGCHLD,sigchild_handler); 
	#endif

    while (pServerPort) 
	{
		Bottle msg,output;
        port.read(msg,true);
        
        fprintf(stderr,"%s\n",msg.toString().c_str());
        fflush(stdout);

		if (!pServerPort) break;

		// command with stdio management
		if (msg.check("stdio") && msg.check("cmd"))
		{
			yarp::os::ConstString stdio_port=msg.find("stdio").asString();
			yarp::os::ConstString on_port=msg.find("on").asString();

			// AM I THE CMD OR/AND STDIO SERVER?
			if (m_PortName==yarp::os::ConstString(stdio_port)) // stdio
			{
			    output=UserStdio(msg);
			    
				if (output.get(0).asInt()>0)
                {
                    Bottle res;
				    if (m_PortName==yarp::os::ConstString(on_port))
				    {
                        // execute command here
					    res=ExecuteCmdAndStdio(msg);
				    }
				    else
				    {
					    // execute command on cmd server
                        res=SendMsg(msg,on_port);
				    }
                    
                    res.append(output);
                    output=res;
				}
			}
			else if (m_PortName==yarp::os::ConstString(on_port)) // cmd
			{
				output=ExecuteCmdAndStdio(msg);				
			}
			else // some error
			{
			    output.addInt(-1);
            }
			port.reply(output);
			continue;
		}

		// without stdio
		if (msg.check("cmd"))
		{
			output=ExecuteCmd(msg);
			port.reply(output);
			continue;
		}

		if (msg.check("kill"))
		{
			yarp::os::ConstString alias(msg.findGroup("kill").get(1).asString());
			int sig=msg.findGroup("kill").get(2).asInt();
			output.addString(m_ProcessVector.Kill(alias,sig)?"kill OK":"kill FAILED");
			port.reply(output);
			continue;
		}
		
	    if (msg.check("sigterm"))
		{
		    yarp::os::ConstString alias(msg.find("sigterm").asString());
			output.addString(m_ProcessVector.Kill(alias)?"sigterm OK":"sigterm FAILED");
			port.reply(output);
			continue;
		}

		if (msg.check("sigtermall"))
		{
			m_ProcessVector.Killall();
			output.addString("sigtermall OK");
			port.reply(output);
			continue;
		}

		if (msg.check("ps"))
		{
			output.append(m_ProcessVector.PS());
			port.reply(output);
			continue;
		}

		if (msg.check("isrunning"))
		{
		    yarp::os::ConstString alias(msg.find("isrunning").asString());
			output.addString(m_ProcessVector.IsRunning(alias)?"running":"not running");
			port.reply(output);
			continue;
		}

		if (msg.check("killstdio"))
		{		    
		    fprintf(stderr,"Run::Server() killstdio(%s)\n",msg.find("killstdio").asString().c_str());
		    yarp::os::ConstString alias(msg.find("killstdio").asString());
			m_StdioVector.Kill(alias);
			continue;
		}

		if (msg.check("exit"))
		{
			output.addString("exit OK");

			port.reply(output);
			port.close();
			pServerPort=0;
		}
	}

	Run::m_StdioVector.Killall();
	Run::m_ProcessVector.Killall();
	
	#if defined(WIN32) || defined(WIN64)
	Run::m_ProcessVector.mutex.wait();
	if (hZombieHunter) TerminateThread(hZombieHunter,0);
	
	Run::m_ProcessVector.mutex.post();
	if (aHandlesVector) delete [] aHandlesVector;
	#endif

	return 0;
}

// CLIENT
int yarp::os::Run::SendToServer(yarp::os::Property& config)
{
	yarp::os::Bottle msg;

	// USE A YARP RUN SERVER TO MANAGE STDIO
	//
	// client -> stdio server -> cmd server
	//
	if (config.check("cmd") && config.check("stdio"))
	{
		if (config.find("stdio")=="") { Help("SYNTAX ERROR: missing remote stdio server\n"); return YARPRUN_ERROR; }
		if (config.find("cmd")=="")   { Help("SYNTAX ERROR: missing command\n"); return YARPRUN_ERROR; }
		if (!config.check("as") || config.find("as")=="") { Help("SYNTAX ERROR: missing tag\n"); return YARPRUN_ERROR; }
		if (!config.check("on") || config.find("on")=="") { Help("SYNTAX ERROR: missing remote server\n"); return YARPRUN_ERROR; }

		msg.addList()=config.findGroup("stdio");
		msg.addList()=config.findGroup("cmd");
		msg.addList()=config.findGroup("as");
		msg.addList()=config.findGroup("on");
		
		if (config.check("workdir")) msg.addList()=config.findGroup("workdir");
		if (config.check("geometry")) msg.addList()=config.findGroup("geometry");
		if (config.check("hold")) msg.addList()=config.findGroup("hold");

		Bottle response=SendMsg(msg,config.find("stdio").asString());
		if (!response.size()) return YARPRUN_ERROR;
		return response.get(0).asInt()>0?0:2;
	}
	
	// DON'T USE A RUN SERVER TO MANAGE STDIO
	//
	// client -> cmd server
	//
	if (config.check("cmd"))
	{                
		if (config.find("cmd").asString()=="")   { Help("SYNTAX ERROR: missing command\n"); return YARPRUN_ERROR; }
		if (!config.check("as") || config.find("as").asString()=="") { Help("SYNTAX ERROR: missing tag\n"); return YARPRUN_ERROR; }
		if (!config.check("on") || config.find("on").asString()=="") { Help("SYNTAX ERROR: missing remote server\n"); return YARPRUN_ERROR; }

		msg.addList()=config.findGroup("cmd");
		msg.addList()=config.findGroup("as");

		if (config.check("workdir")) msg.addList()=config.findGroup("workdir");

		Bottle response=SendMsg(msg,config.find("on").asString());
		if (!response.size()) return YARPRUN_ERROR;
		return response.get(0).asInt()>0?0:2;
	}
	
	// client -> cmd server
	if (config.check("kill")) 
	{ 
		if (!config.check("on") || config.find("on").asString()=="") { Help("SYNTAX ERROR: missing remote server\n"); return YARPRUN_ERROR; }
		if (config.findGroup("kill").get(1).asString()=="")  { Help("SYNTAX ERROR: missing tag\n"); return YARPRUN_ERROR; }
		if (config.findGroup("kill").get(2).asInt()==0)	  { Help("SYNTAX ERROR: missing signum\n"); return YARPRUN_ERROR; }

		msg.addList()=config.findGroup("kill");
		
		Bottle response=SendMsg(msg,config.find("on").asString());
		if (!response.size()) return YARPRUN_ERROR;
		return response.get(0).asString()=="kill OK"?0:2;
	}

	// client -> cmd server
	if (config.check("sigterm")) 
	{ 
		if (config.find("sigterm").asString()=="") { Help("SYNTAX ERROR: missing tag"); return YARPRUN_ERROR; }
		if (!config.check("on") || config.find("on").asString()=="") { Help("SYNTAX ERROR: missing remote server\n"); return YARPRUN_ERROR; }
		
		msg.addList()=config.findGroup("sigterm");
		
		Bottle response=SendMsg(msg,config.find("on").asString());
		if (!response.size()) return YARPRUN_ERROR;
		return response.get(0).asString()=="sigterm OK"?0:2;
	}

	// client -> cmd server
	if (config.check("sigtermall")) 
	{ 
		if (!config.check("on") || config.find("on").asString()=="") { Help("SYNTAX ERROR: missing remote server\n"); return YARPRUN_ERROR; }
		
		msg.addList()=config.findGroup("sigtermall");
       
		Bottle response=SendMsg(msg,config.find("on").asString());
		if (!response.size()) return YARPRUN_ERROR;
		return 0;
	}

	if (config.check("ps"))
	{
		if (!config.check("on") || config.find("on").asString()=="") { Help("SYNTAX ERROR: missing remote server\n"); return YARPRUN_ERROR; }
		
		msg.addList()=config.findGroup("ps");
       
		Bottle response=SendMsg(msg,config.find("on").asString());
		if (!response.size()) return YARPRUN_ERROR;
		return 0;
	}

	if (config.check("isrunning"))
	{
		if (!config.check("on") || config.find("on").asString()=="") { Help("SYNTAX ERROR: missing remote server\n"); return YARPRUN_ERROR; }
		if (config.find("isrunning").asString()=="") { Help("SYNTAX ERROR: missing tag\n"); return YARPRUN_ERROR; }

		msg.addList()=config.findGroup("isrunning");
		
		Bottle response=SendMsg(msg,config.find("on").asString());
		if (!response.size()) return YARPRUN_ERROR;
		return response.get(0).asString()=="running"?0:2;
	}

	if (config.check("exit"))
	{
		if (!config.check("on") || config.find("on").asString()=="") { Help("SYNTAX ERROR: missing remote server\n"); return YARPRUN_ERROR; }
		
		msg.addList()=config.findGroup("exit");
        
		Bottle response=SendMsg(msg,config.find("on").asString());
		if (!response.size()) return YARPRUN_ERROR;
		return 0;
	}

	return 0;
}

void yarp::os::Run::Help(const char *msg)
{
	fprintf(stderr,"%s",msg);
    fprintf(stderr,"\nUSAGE:\n\n");
    fprintf(stderr,"yarp run --server SERVERPORT\nrun a server on the local machine\n\n");
    fprintf(stderr,"yarp run --on SERVERPORT --as TAG --cmd COMMAND [ARGLIST] [--workdir WORKDIR]\nrun a command on SERVERPORT server\n\n");
    fprintf(stderr,"yarp run --on SERVERPORT --as TAG --stdio STDIOSERVERPORT [--hold] [--geometry WxH+X+Y] --cmd COMMAND [ARGLIST] [--workdir WORKDIR]\n");
    fprintf(stderr,"run a command on SERVERPORT server sending I/O to STDIOSERVERPORT server\n\n");
    fprintf(stderr,"yarp run --on SERVERPORT --kill TAG SIGNUM\nsend SIGNUM signal to TAG command\n\n");
    fprintf(stderr,"yarp run --on SERVERPORT --sigterm TAG\nterminate TAG command\n\n");
	fprintf(stderr,"yarp run --on SERVERPORT --sigtermall\nterminate all commands\n\n");
    fprintf(stderr,"yarp run --on SERVERPORT --ps\nreport commands running on SERVERPORT\n\n");
	fprintf(stderr,"yarp run --on SERVERPORT --isrunning TAG\nTAG command is running?\n\n");
    fprintf(stderr,"yarp run --on SERVERPORT --exit\nstop SERVERPORT server\n\n");  
}

/////////////////////////
// OS DEPENDENT FUNCTIONS
/////////////////////////

// WINDOWS

#if defined(WIN32) || defined(WIN64)

#ifndef __GNUC__
DWORD WINAPI ZombieHunter(__in  LPVOID lpParameter)
#else
DWORD WINAPI ZombieHunter(LPVOID lpParameter)
#endif
{
	DWORD nCount;

	while (true)
	{
	    if (aHandlesVector)
	    {
		    delete [] aHandlesVector;
		    aHandlesVector=0;
	    }
	    nCount=0;

	    aHandlesVector=new HANDLE[yarp::os::Run::m_ProcessVector.Size()+yarp::os::Run::m_StdioVector.Size()];

	    yarp::os::Run::m_ProcessVector.GetHandles(aHandlesVector,nCount);
	    yarp::os::Run::m_StdioVector.GetHandles(aHandlesVector,nCount);

		if (nCount)
		{
			WaitForMultipleObjects(nCount,aHandlesVector,FALSE,INFINITE);
		}
		else
		{
			hZombieHunter=NULL;
			return 0;
		}
	}

	return 0;
}

// CMD SERVER
yarp::os::Bottle yarp::os::Run::ExecuteCmdAndStdio(Bottle& msg)
{
	yarp::os::ConstString alias=msg.find("as").asString().c_str();

	// PIPES
	SECURITY_ATTRIBUTES pipe_sec_attr; 
    pipe_sec_attr.nLength=sizeof(SECURITY_ATTRIBUTES); 
    pipe_sec_attr.bInheritHandle=TRUE; 
    pipe_sec_attr.lpSecurityDescriptor=NULL;
	HANDLE read_from_pipe_stdin_to_cmd,write_to_pipe_stdin_to_cmd;
    CreatePipe(&read_from_pipe_stdin_to_cmd,&write_to_pipe_stdin_to_cmd,&pipe_sec_attr,0);
    HANDLE read_from_pipe_cmd_to_stdout,write_to_pipe_cmd_to_stdout;
    CreatePipe(&read_from_pipe_cmd_to_stdout,&write_to_pipe_cmd_to_stdout,&pipe_sec_attr,0);

	// RUN STDOUT
	PROCESS_INFORMATION stdout_process_info;
	ZeroMemory(&stdout_process_info,sizeof(PROCESS_INFORMATION));
	STARTUPINFO stdout_startup_info;
	ZeroMemory(&stdout_startup_info,sizeof(STARTUPINFO));

	stdout_startup_info.cb=sizeof(STARTUPINFO); 
	stdout_startup_info.hStdError=stderr;
	stdout_startup_info.hStdOutput=stdout;
	stdout_startup_info.hStdInput=read_from_pipe_cmd_to_stdout;
	stdout_startup_info.dwFlags|=STARTF_USESTDHANDLES;

	BOOL bSuccess=CreateProcess(NULL,	// command name
								(char*)(yarp::os::ConstString("yarp quiet write /")+alias+"/stdout verbatim").c_str(), // command line 
								NULL,          // process security attributes 
								NULL,          // primary thread security attributes 
								TRUE,          // handles are inherited 
								0,             // creation flags 
								NULL,          // use parent's environment 
								NULL,          // use parent's current directory 
								&stdout_startup_info,   // STARTUPINFO pointer 
								&stdout_process_info);  // receives PROCESS_INFORMATION 

    if (!bSuccess)
	{
        DWORD error=GetLastError();
        char errorMsg[1024];
        FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM,NULL,error,0,errorMsg,1024,NULL);
        
	    Bottle result;
        char pidstr[16];
	    sprintf(pidstr,"%d",stdout_process_info.dwProcessId);

        result.addInt(YARPRUN_ERROR);

        yarp::os::ConstString out=yarp::os::ConstString("ABORTED: server=")+m_PortName+" alias="+alias+" cmd=stdout pid="+pidstr+"\n"
                       +yarp::os::ConstString("Can't execute stdout because ")+errorMsg+"\n";

        result.addString(out.c_str());
        fprintf(stderr,out.c_str());
        fflush(stderr);

		CloseHandle(write_to_pipe_stdin_to_cmd);
		CloseHandle(read_from_pipe_stdin_to_cmd);
		CloseHandle(write_to_pipe_cmd_to_stdout);
		CloseHandle(read_from_pipe_cmd_to_stdout);

	    return result;
	}

	// RUN STDIN
	PROCESS_INFORMATION stdin_process_info;
	ZeroMemory(&stdin_process_info,sizeof(PROCESS_INFORMATION));
	STARTUPINFO stdin_startup_info;
	ZeroMemory(&stdin_startup_info,sizeof(STARTUPINFO));

	stdin_startup_info.cb=sizeof(STARTUPINFO); 
	stdin_startup_info.hStdError=write_to_pipe_stdin_to_cmd;
	stdin_startup_info.hStdOutput=write_to_pipe_stdin_to_cmd;
	stdin_startup_info.hStdInput=stdin;
	stdin_startup_info.dwFlags|=STARTF_USESTDHANDLES;

	bSuccess=CreateProcess(NULL,	// command name
								(char*)(yarp::os::ConstString("yarp quiet read /")+alias+"/stdin").c_str(), // command line 
								NULL,          // process security attributes 
								NULL,          // primary thread security attributes 
								TRUE,          // handles are inherited 
								0,             // creation flags 
								NULL,          // use parent's environment 
								NULL,          // use parent's current directory 
								&stdin_startup_info,   // STARTUPINFO pointer 
								&stdin_process_info);  // receives PROCESS_INFORMATION 

	if (!bSuccess)
	{
        DWORD error=GetLastError();
        char errorMsg[1024];
        FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM,NULL,error,0,errorMsg,1024,NULL);
        
	    Bottle result;
        char pidstr[16];
	    sprintf(pidstr,"%d",stdin_process_info.dwProcessId);

        result.addInt(YARPRUN_ERROR);

        yarp::os::ConstString out=yarp::os::ConstString("ABORTED: server=")+m_PortName+" alias="+alias+" cmd=stdin pid="+pidstr+"\n"
                       +yarp::os::ConstString("Can't execute stdin because ")+errorMsg+"\n";

        result.addString(out.c_str());
        fprintf(stderr,out.c_str());
        fflush(stderr);

		TerminateProcess(stdout_process_info.hProcess,YARPRUN_ERROR);
		CloseHandle(stdout_process_info.hProcess);

		CloseHandle(write_to_pipe_stdin_to_cmd);
		CloseHandle(read_from_pipe_stdin_to_cmd);
		CloseHandle(write_to_pipe_cmd_to_stdout);
		CloseHandle(read_from_pipe_cmd_to_stdout);

	    return result;
	}

	// connect yarp read and write
	bool bConnR=false,bConnW=false;
	for (int i=0; i<20 && !(bConnR&&bConnW); ++i)
	{ 	    
	    if (!bConnW && NetworkBase::connect((yarp::os::ConstString("/")+alias+"/stdout").c_str(),(yarp::os::ConstString("/")+alias+"/user/stdout").c_str()))
	        bConnW=true;
			        
	    if (!bConnR && NetworkBase::connect((yarp::os::ConstString("/")+alias+"/user/stdin").c_str(),(yarp::os::ConstString("/")+alias+"/stdin").c_str()))
	        bConnR=true;
	        
	    if (!bConnW || !bConnR) yarp::os::Time::delay(1.0);
	}        
		    
    if (!(bConnR&&bConnW))
    {
	    Bottle result;
        result.addInt(YARPRUN_ERROR);

        yarp::os::ConstString out=yarp::os::ConstString("ABORTED: server=")+m_PortName+" alias="+alias+" cmd=connect\nCan't connect stdio\n";
		
        result.addString(out.c_str());
        fprintf(stderr,out.c_str());
        fflush(stderr);

		if (bConnW) NetworkBase::disconnect((yarp::os::ConstString("/")+alias+"/stdout").c_str(),(yarp::os::ConstString("/")+alias+"/user/stdout").c_str());
		if (bConnR) NetworkBase::disconnect((yarp::os::ConstString("/")+alias+"/user/stdin").c_str(),(yarp::os::ConstString("/")+alias+"/stdin").c_str());

		TerminateProcess(stdout_process_info.hProcess,YARPRUN_ERROR);
		CloseHandle(stdout_process_info.hProcess);
		TerminateProcess(stdin_process_info.hProcess,YARPRUN_ERROR);
		CloseHandle(stdin_process_info.hProcess);

		CloseHandle(write_to_pipe_stdin_to_cmd);
		CloseHandle(read_from_pipe_stdin_to_cmd);
		CloseHandle(write_to_pipe_cmd_to_stdout);
		CloseHandle(read_from_pipe_cmd_to_stdout);

	    return result;
	}

	// RUN COMMAND
	PROCESS_INFORMATION cmd_process_info;
	ZeroMemory(&cmd_process_info,sizeof(PROCESS_INFORMATION));
	STARTUPINFO cmd_startup_info;
	ZeroMemory(&cmd_startup_info,sizeof(STARTUPINFO));

	cmd_startup_info.cb=sizeof(STARTUPINFO); 
	cmd_startup_info.hStdError=write_to_pipe_cmd_to_stdout;
	cmd_startup_info.hStdOutput=write_to_pipe_cmd_to_stdout;
	cmd_startup_info.hStdInput=read_from_pipe_stdin_to_cmd;
	cmd_startup_info.dwFlags|=STARTF_USESTDHANDLES;

	Bottle command_bottle=msg.findGroup("cmd").tail();

    yarp::os::impl::String tmpStr;
	for (int s=0; s<command_bottle.size(); ++s)
    {
        tmpStr+=command_bottle.get(s).toString()+yarp::os::ConstString(" ");
    }

    yarp::os::ConstString commandText(tmpStr.c_str());

	bool bWorkdir=msg.check("workdir");
	yarp::os::ConstString sWorkdir=bWorkdir?msg.find("workdir").asString()+"\\":"";

	bSuccess=CreateProcess(NULL,	// command name
								(char*)(sWorkdir+commandText).c_str(), // command line 
								NULL,          // process security attributes 
								NULL,          // primary thread security attributes 
								TRUE,          // handles are inherited 
								0,             // creation flags 
								NULL, // use parent's environment 
								bWorkdir?sWorkdir.c_str():NULL, // working directory
								&cmd_startup_info,   // STARTUPINFO pointer 
								&cmd_process_info);  // receives PROCESS_INFORMATION 

	if (!bSuccess && bWorkdir)
	{
			bSuccess=CreateProcess(NULL,	// command name
									(char*)(commandText.c_str()), // command line 
									NULL,          // process security attributes 
									NULL,          // primary thread security attributes 
									TRUE,          // handles are inherited 
									0,             // creation flags 
									NULL,          // use parent's environment 
									sWorkdir.c_str(), // working directory 
									&cmd_startup_info,   // STARTUPINFO pointer 
									&cmd_process_info);  // receives PROCESS_INFORMATION 
	}

	if (!bSuccess)
	{
        DWORD error=GetLastError();
        char errorMsg[1024];
        FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM,NULL,error,0,errorMsg,1024,NULL);
        
	    Bottle result;
        char pidstr[16];
	    sprintf(pidstr,"%d",cmd_process_info.dwProcessId);

        result.addInt(YARPRUN_ERROR);

        DWORD nBytes;
        yarp::os::ConstString line1=yarp::os::ConstString("ABORTED: server=")+m_PortName+" alias="+alias+" cmd="+commandText+"pid="+pidstr+"\n";
        WriteFile(write_to_pipe_cmd_to_stdout,line1.c_str(),line1.length(),&nBytes,0);
        yarp::os::ConstString line2=yarp::os::ConstString("Can't execute command because ")+errorMsg+"\n";
        WriteFile(write_to_pipe_cmd_to_stdout,line1.c_str(),line2.length(),&nBytes,0);
        FlushFileBuffers(write_to_pipe_cmd_to_stdout);

        yarp::os::ConstString out=line1+line2;
        result.addString(out.c_str());
        fprintf(stderr,out.c_str());
        fflush(stderr);

		NetworkBase::disconnect((yarp::os::ConstString("/")+alias+"/stdout").c_str(),(yarp::os::ConstString("/")+alias+"/user/stdout").c_str());
		NetworkBase::disconnect((yarp::os::ConstString("/")+alias+"/user/stdin").c_str(),(yarp::os::ConstString("/")+alias+"/stdin").c_str());

        CloseHandle(write_to_pipe_stdin_to_cmd);
		CloseHandle(read_from_pipe_stdin_to_cmd);
		CloseHandle(write_to_pipe_cmd_to_stdout);
		CloseHandle(read_from_pipe_cmd_to_stdout);

        TerminateProcess(stdout_process_info.hProcess,YARPRUN_ERROR);
		CloseHandle(stdout_process_info.hProcess);
		TerminateProcess(stdin_process_info.hProcess,YARPRUN_ERROR);
		CloseHandle(stdin_process_info.hProcess);

	    return result;
	}

    FlushFileBuffers(write_to_pipe_cmd_to_stdout);

	// EVERYTHING IS ALL RIGHT

    yarp::os::ConstString stdio = msg.find("stdio").asString().c_str();
	m_ProcessVector.Add(new YarpRunCmdWithStdioInfo(commandText,
                                                    alias,
                                                    m_PortName,
						                            cmd_process_info.dwProcessId,
                                                    stdio,
                                                    &m_StdioVector,
                                                    stdin_process_info.hProcess,
                                                    stdout_process_info.hProcess,
						                            read_from_pipe_stdin_to_cmd,
                                                    write_to_pipe_stdin_to_cmd,
						                            read_from_pipe_cmd_to_stdout,
                                                    write_to_pipe_cmd_to_stdout,
						                            cmd_process_info.hProcess,
                                                    false));

    char pidstr[16];
	sprintf(pidstr,"%d",cmd_process_info.dwProcessId);
    Bottle result;
    result.addInt(cmd_process_info.dwProcessId);
    yarp::os::ConstString out=yarp::os::ConstString("STARTED: server=")+m_PortName+" alias="+alias+" cmd="+commandText+"pid="+pidstr+"\n";
    result.addString(out.c_str());
    fprintf(stderr,out.c_str());

	return result;
}

yarp::os::Bottle yarp::os::Run::ExecuteCmd(yarp::os::Bottle& msg)
{
	yarp::os::ConstString alias=msg.find("as").asString().c_str();

	// RUN COMMAND
	PROCESS_INFORMATION cmd_process_info;
	ZeroMemory(&cmd_process_info,sizeof(PROCESS_INFORMATION));
	STARTUPINFO cmd_startup_info;
	ZeroMemory(&cmd_startup_info,sizeof(STARTUPINFO));

	cmd_startup_info.cb=sizeof(STARTUPINFO); 

	Bottle command_bottle=msg.findGroup("cmd").tail();
    
    yarp::os::impl::String tmpStr;
	for (int s=0; s<command_bottle.size(); ++s)
    {
        tmpStr+=command_bottle.get(s).toString()+yarp::os::ConstString(" ");
    }

    yarp::os::ConstString commandText(tmpStr.c_str());

	bool bWorkdir=msg.check("workdir");
	yarp::os::ConstString sWorkdir=bWorkdir?msg.find("workdir").asString()+"\\":"";

	BOOL bSuccess=CreateProcess(NULL,	// command name
								(char*)(sWorkdir+commandText).c_str(), // command line 
								NULL,          // process security attributes 
								NULL,          // primary thread security attributes 
								TRUE,          // handles are inherited 
								0,             // creation flags 
								NULL,          // use parent's environment 
								bWorkdir?sWorkdir.c_str():NULL, // working directory 
								&cmd_startup_info,   // STARTUPINFO pointer 
								&cmd_process_info);  // receives PROCESS_INFORMATION 

	if (!bSuccess && bWorkdir)
	{
			bSuccess=CreateProcess(NULL,	// command name
									(char*)(commandText.c_str()), // command line 
									NULL,          // process security attributes 
									NULL,          // primary thread security attributes 
									TRUE,          // handles are inherited 
									0,             // creation flags 
									NULL,          // use parent's environment 
									sWorkdir.c_str(), // working directory 
									&cmd_startup_info,   // STARTUPINFO pointer 
									&cmd_process_info);  // receives PROCESS_INFORMATION 
	}

	if (!bSuccess)
	{
        DWORD error=GetLastError();
        char errorMsg[1024];
        FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM,NULL,error,0,errorMsg,1024,NULL);
        
	    Bottle result;
        char pidstr[16];
	    sprintf(pidstr,"%d",cmd_process_info.dwProcessId);

        result.addInt(YARPRUN_ERROR);

        yarp::os::ConstString out=yarp::os::ConstString("ABORTED: server=")+m_PortName+" alias="+alias+" cmd="+commandText+" pid="+pidstr+"\n"
                       +yarp::os::ConstString("Can't execute command because ")+errorMsg+"\n";

        result.addString(out.c_str());
        fprintf(stderr,out.c_str());
        fflush(stderr);

	    return result;
	}

	// EVERYTHING IS ALL RIGHT

	m_ProcessVector.Add(new YarpRunProcInfo(commandText,
                                            alias,
                                            m_PortName,
                                            cmd_process_info.dwProcessId,
                                            cmd_process_info.hProcess,
                                            false));

    char pidstr[16];
	sprintf(pidstr,"%d",cmd_process_info.dwProcessId);
    Bottle result;
    result.addInt(cmd_process_info.dwProcessId);
    yarp::os::ConstString out=yarp::os::ConstString("STARTED: server=")+m_PortName+" alias="+alias+" cmd="+commandText+"pid="+pidstr+"\n";
    fprintf(stderr,out.c_str());
	return result;
}

// STDIO SERVER
yarp::os::Bottle yarp::os::Run::UserStdio(yarp::os::Bottle& msg)
{
	yarp::os::ConstString alias=msg.find("as").asString().c_str();

	// create yarp read and yarp write client processes
	// RUN STDOUT
	PROCESS_INFORMATION stdio_process_info;
	ZeroMemory(&stdio_process_info,sizeof(PROCESS_INFORMATION));
	STARTUPINFO stdio_startup_info;
	ZeroMemory(&stdio_startup_info,sizeof(STARTUPINFO));

	stdio_startup_info.cb=sizeof(STARTUPINFO); 
	stdio_startup_info.wShowWindow=SW_SHOWNOACTIVATE;
	stdio_startup_info.dwFlags=STARTF_USESHOWWINDOW;

	yarp::os::ConstString command_line=yarp::os::ConstString("yarp quiet readwrite /")+alias+"/user/stdout /"+alias+"/user/stdin";

	BOOL bSuccess=CreateProcess(NULL,	// command name
								(char*)command_line.c_str(), // command line 
								NULL,          // process security attributes 
								NULL,          // primary thread security attributes 
								TRUE,          // handles are inherited 
								CREATE_NEW_CONSOLE, // creation flags 
								NULL,          // use parent's environment 
								NULL,          // use parent's current directory 
								&stdio_startup_info,   // STARTUPINFO pointer 
								&stdio_process_info);  // receives PROCESS_INFORMATION 


	stdio_process_info.hProcess;
	
	Bottle result;
    char pidstr[16];
	sprintf(pidstr,"%d",stdio_process_info.dwProcessId);
	yarp::os::ConstString out;

    if (bSuccess)
    {
        m_StdioVector.Add(new YarpRunProcInfo(command_line,
                                              alias,
                                              m_PortName,
                                              stdio_process_info.dwProcessId,
                                              stdio_process_info.hProcess,
                                              false));

        result.addInt(stdio_process_info.dwProcessId);
        out=yarp::os::ConstString("STARTED: server=")+m_PortName+" alias="+alias+" cmd=CMD pid="+pidstr+"\n";
    }
    else
	{
        DWORD error=GetLastError();
        char errorMsg[1024];
        FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM,NULL,error,0,errorMsg,1024,NULL);
        
        result.addInt(YARPRUN_ERROR);
        out=yarp::os::ConstString("ABORTED: server=")+m_PortName+" alias="+alias+" cmd=stdio pid="+pidstr+"\n"
           +yarp::os::ConstString("Can't open stdio window because ")+errorMsg+"\n";
	}

    result.addString(out.c_str());
    fprintf(stderr,out.c_str());
    fflush(stderr);

	return result;	
}

////////////////
#else // LINUX
////////////////

#define READ_FROM_PIPE 0
#define WRITE_TO_PIPE  1
#define REDIRECT_TO(from,to) dup2(to,from)

int CountArgs(char *str)
{
    int nargs=0;
   
    for (bool bSpace=true; *str; ++str)
    {
        if (bSpace)
        {
            if (*str!=' ')
            {
                ++nargs;
                bSpace=false;
            }
        }
        else
        {
            if (*str==' ')
            {
                bSpace=true;
            }
        }
    }
    
    return nargs;
}

void ParseCmd(char* cmd_str,char** arg_str)
{
    int nargs=0;
    
    for (bool bSpace=true; *cmd_str; ++cmd_str)
    {
        if (*cmd_str!=' ')
        {
            if (bSpace) arg_str[nargs++]=cmd_str;
            bSpace=false;
        }
        else
        {
            *cmd_str=0;
            bSpace=true;
        }
    }
}

void yarp::os::Run::CleanZombies()
{
	m_ProcessVector.CleanZombies();
	m_StdioVector.CleanZombies();
}

//////////////////////////////////////////////////////////////////////////////////////////

yarp::os::Bottle yarp::os::Run::ExecuteCmdAndStdio(yarp::os::Bottle& msg)
{
	yarp::os::ConstString alias(msg.find("as").asString());
	yarp::os::ConstString commandString(msg.find("cmd").toString());
    yarp::os::ConstString stdio_str(msg.find("stdio").asString());

	int  pipe_stdin_to_cmd[2];
	pipe(pipe_stdin_to_cmd);
	int  pipe_cmd_to_stdout[2];
	pipe(pipe_cmd_to_stdout);
	
    int  pipe_child_to_parent[2];
	pipe(pipe_child_to_parent);

	int pid_stdout=fork();

	if (IS_INVALID(pid_stdout))
	{
	    int error=errno;
	    
		yarp::os::ConstString out=yarp::os::ConstString("ABORTED: server=")+m_PortName+" alias="+alias+" cmd=stdout\n"
		               +yarp::os::ConstString("Can't fork stdout process because ")+strerror(error)+"\n";
        
        Bottle result;
        result.addInt(YARPRUN_ERROR);		
        result.addString(out.c_str());
        fprintf(stderr,out.c_str());
        fflush(stderr);

		CLOSE(pipe_stdin_to_cmd[0]);
		CLOSE(pipe_stdin_to_cmd[1]);
		CLOSE(pipe_cmd_to_stdout[0]);
		CLOSE(pipe_cmd_to_stdout[1]);
		
		return result;
	}

	if (IS_NEW_PROCESS(pid_stdout)) // STDOUT IMPLEMENTED HERE
	{
		REDIRECT_TO(STDIN_FILENO,pipe_cmd_to_stdout[READ_FROM_PIPE]);
		int ret=execlp("yarp","yarp","quiet","write",(yarp::os::ConstString("/")+alias+"/"+"stdout").c_str(),"verbatim",NULL);
	    
	    if (ret==YARPRUN_ERROR)
	    {
	        int error=errno;
	        
            yarp::os::ConstString out=yarp::os::ConstString("ABORTED: server=")+m_PortName+" alias="+alias+" cmd=stdout\n"
                           +yarp::os::ConstString("Can't execute stdout because ")+strerror(error)+"\n";
	        
	        FILE* out_to_parent=fdopen(pipe_child_to_parent[WRITE_TO_PIPE],"w");
	        fprintf(out_to_parent,out.c_str());
	        fflush(out_to_parent);
	        fclose(out_to_parent);
	        fprintf(stderr,out.c_str());
            fflush(stderr);
	    }
	    
		exit(ret);
	}

	if (IS_PARENT_OF(pid_stdout))
	{
	    fprintf(stderr,"STARTED: server=%s alias=%s cmd=stdout pid=%d\n",m_PortName.c_str(),alias.c_str(),pid_stdout);
	
		int pid_stdin=fork();

		if (IS_INVALID(pid_stdin))
		{		    
            int error=errno;
                        
            yarp::os::ConstString out=yarp::os::ConstString("ABORTED: server=")+m_PortName+" alias="+alias+" cmd=stdin\n"
                           +yarp::os::ConstString("Can't fork stdin process because ")+strerror(error)+"\n";

		    Bottle result;
            result.addInt(YARPRUN_ERROR);            
            result.addString(out.c_str());
	        fprintf(stderr,out.c_str());
		    fflush(stderr);
		    
		    KILL(pid_stdout);
			CLOSE(pipe_stdin_to_cmd[0]);
			CLOSE(pipe_stdin_to_cmd[1]);
			CLOSE(pipe_cmd_to_stdout[0]);
			CLOSE(pipe_cmd_to_stdout[1]);
		    
		    return result;
		}

		if (IS_NEW_PROCESS(pid_stdin)) // STDIN IMPLEMENTED HERE
		{
			REDIRECT_TO(STDOUT_FILENO,pipe_stdin_to_cmd[WRITE_TO_PIPE]);
			REDIRECT_TO(STDERR_FILENO,pipe_stdin_to_cmd[WRITE_TO_PIPE]);
			int ret=execlp("yarp","yarp","quiet","read",(yarp::os::ConstString("/")+alias+"/"+"stdin").c_str(),NULL);
			
		    if (ret==YARPRUN_ERROR)
	        {
	            int error=errno;
	                
                yarp::os::ConstString out=yarp::os::ConstString("ABORTED: server=")+m_PortName+" alias="+alias+" cmd=stdin\n"
                               +yarp::os::ConstString("Can't execute stdin because ")+strerror(error)+"\n";
                
                FILE* out_to_parent=fdopen(pipe_child_to_parent[WRITE_TO_PIPE],"w");
	            fprintf(out_to_parent,out.c_str());
	            fflush(out_to_parent);
	            fclose(out_to_parent);
	            fprintf(stderr,out.c_str());
                fflush(stderr);
	        }
			
			exit(ret);
		}

		if (IS_PARENT_OF(pid_stdin))
		{
			// connect yarp read and write
			
			fprintf(stderr,"STARTED: server=%s alias=%s cmd=stdin pid=%d\n",m_PortName.c_str(),alias.c_str(),pid_stdin);
			
			bool bConnR=false,bConnW=false;
		    for (int i=0; i<20 && !(bConnR&&bConnW); ++i)
		    { 
			    if (!bConnW && NetworkBase::connect((yarp::os::ConstString("/")+alias+"/stdout").c_str(),(yarp::os::ConstString("/")+alias+"/user/stdout").c_str()))
			    {
			        bConnW=true;
			    }    
		        if (!bConnR && NetworkBase::connect((yarp::os::ConstString("/")+alias+"/user/stdin").c_str(),(yarp::os::ConstString("/")+alias+"/stdin").c_str()))
		        {
		            bConnR=true;
		        }
		        
		        if (!bConnW || !bConnR) yarp::os::Time::delay(1.0);
		    }
		    
		    //bool bConnW=NetworkBase::connect((yarp::os::ConstString("/")+alias+"/stdout").c_str(),(yarp::os::ConstString("/")+alias+"/user/stdout").c_str());       
		    //bool bConnR=NetworkBase::connect((yarp::os::ConstString("/")+alias+"/user/stdin").c_str(),(yarp::os::ConstString("/")+alias+"/stdin").c_str());
		    
		    if (!(bConnR&&bConnW))
		    {				
		        if (bConnW) NetworkBase::disconnect((yarp::os::ConstString("/")+alias+"/stdout").c_str(),(yarp::os::ConstString("/")+alias+"/user/stdout").c_str());
		        if (bConnR) NetworkBase::disconnect((yarp::os::ConstString("/")+alias+"/user/stdin").c_str(),(yarp::os::ConstString("/")+alias+"/stdin").c_str());
		        
	            yarp::os::ConstString out=yarp::os::ConstString("ABORTED: server=")+m_PortName+" alias="+alias+" cmd=connect\nCan't connect stdio\n";
	           
	           	Bottle result;
				result.addInt(YARPRUN_ERROR);
	            result.addString(out.c_str());
	            fprintf(stderr,out.c_str());
	            fflush(stderr); 
	             
	            KILL(pid_stdout);
				KILL(pid_stdin);
				CLOSE(pipe_stdin_to_cmd[0]);
				CLOSE(pipe_stdin_to_cmd[1]);
				CLOSE(pipe_cmd_to_stdout[0]);
				CLOSE(pipe_cmd_to_stdout[1]);
	            
				return result;
		    }

			int pid_cmd=fork();

			if (IS_INVALID(pid_cmd))
			{				
                int error=errno;
		     	
		        yarp::os::ConstString out=yarp::os::ConstString("ABORTED: server=")+m_PortName+" alias="+alias+" cmd="+commandString+"\n"
                               +yarp::os::ConstString("Can't fork command process because ")+strerror(error)+"\n";
	            
			    Bottle result;
                result.addInt(YARPRUN_ERROR);	            
	            result.addString(out.c_str());
	            fprintf(stderr,out.c_str());
	            fflush(stderr);

	            FILE* to_yarp_stdout=fdopen(pipe_cmd_to_stdout[WRITE_TO_PIPE],"w");
	            fprintf(to_yarp_stdout,out.c_str());
	            fflush(to_yarp_stdout);
	            fclose(to_yarp_stdout);
	            
	            NetworkBase::disconnect((yarp::os::ConstString("/")+alias+"/stdout").c_str(),(yarp::os::ConstString("/")+alias+"/user/stdout").c_str());
		        NetworkBase::disconnect((yarp::os::ConstString("/")+alias+"/user/stdin").c_str(),(yarp::os::ConstString("/")+alias+"/stdin").c_str());

				KILL(pid_stdout);
				KILL(pid_stdin);
				CLOSE(pipe_stdin_to_cmd[0]);
				CLOSE(pipe_stdin_to_cmd[1]);
				CLOSE(pipe_cmd_to_stdout[0]);
				CLOSE(pipe_cmd_to_stdout[1]);
		
		        return result;
			}

			if (IS_NEW_PROCESS(pid_cmd)) // RUN COMMAND HERE
			{                        
                char *cmd_str=new char[commandString.length()+1];
                strcpy(cmd_str,commandString.c_str());
                int nargs=CountArgs(cmd_str);
                char **arg_str=new char*[nargs+1];
                ParseCmd(cmd_str,arg_str);
                arg_str[nargs]=0;
                
			    setvbuf(stdout,NULL,_IONBF,0);

				REDIRECT_TO(STDIN_FILENO, pipe_stdin_to_cmd[READ_FROM_PIPE]);
				REDIRECT_TO(STDOUT_FILENO,pipe_cmd_to_stdout[WRITE_TO_PIPE]);
				REDIRECT_TO(STDERR_FILENO,pipe_cmd_to_stdout[WRITE_TO_PIPE]);
	           
				if (msg.check("workdir"))
			    {
			        chdir(msg.find("workdir").asString().c_str());
			    }

				int ret=execvp(arg_str[0],arg_str);   

                fflush(stdout);
                fflush(stderr);

                if (ret==YARPRUN_ERROR)
	            {
	                int error=errno;
	                
                    yarp::os::ConstString out=yarp::os::ConstString("ABORTED: server=")+m_PortName+" alias="+alias+" cmd="+commandString+"\n"
                                   +yarp::os::ConstString("Can't execute command because ")+strerror(error)+"\n";
                    
                	FILE* out_to_parent=fdopen(pipe_child_to_parent[WRITE_TO_PIPE],"w");
	                fprintf(out_to_parent,out.c_str());
	                fflush(out_to_parent);
	                fclose(out_to_parent);
	                fprintf(stderr,out.c_str());
                    fflush(stderr);
                    
                    NetworkBase::disconnect((yarp::os::ConstString("/")+alias+"/stdout").c_str(),(yarp::os::ConstString("/")+alias+"/user/stdout").c_str());
		            NetworkBase::disconnect((yarp::os::ConstString("/")+alias+"/user/stdin").c_str(),(yarp::os::ConstString("/")+alias+"/stdin").c_str());
	            }

        		delete [] cmd_str;
        		delete [] arg_str;

				exit(ret);
			}

			if (IS_PARENT_OF(pid_cmd))
			{
				m_ProcessVector.Add(
				    new YarpRunCmdWithStdioInfo(
				        commandString,
				        alias,
				        m_PortName,
				        pid_cmd,
				        stdio_str,
				        &m_StdioVector,
					    pid_stdin,
					    pid_stdout,
					    pipe_stdin_to_cmd[READ_FROM_PIPE],
					    pipe_stdin_to_cmd[WRITE_TO_PIPE],
					    pipe_cmd_to_stdout[READ_FROM_PIPE],
					    pipe_cmd_to_stdout[WRITE_TO_PIPE],
					    NULL,
					    false
					)
				);
				    
	            char pidstr[16];
	            sprintf(pidstr,"%d",pid_cmd);
	            yarp::os::impl::String out;
	            Bottle result;
	            
	            FILE* in_from_child=fdopen(pipe_child_to_parent[READ_FROM_PIPE],"r");
	            int flags=fcntl(pipe_child_to_parent[READ_FROM_PIPE],F_GETFL,0);
	            fcntl(pipe_child_to_parent[READ_FROM_PIPE],F_SETFL,flags|O_NONBLOCK); 
                for (char buff[1024]; fgets(buff,1024,in_from_child);)
	            {
	                out+=yarp::os::ConstString(buff);
	            }
	            fclose(in_from_child);
	             
	            if (out.length()>0)
	            {
	                result.addInt(YARPRUN_ERROR);
	                NetworkBase::disconnect((yarp::os::ConstString("/")+alias+"/stdout").c_str(),(yarp::os::ConstString("/")+alias+"/user/stdout").c_str());
		            NetworkBase::disconnect((yarp::os::ConstString("/")+alias+"/user/stdin").c_str(),(yarp::os::ConstString("/")+alias+"/stdin").c_str());
	            }
	            else
	            {
	                /*
	                m_ProcessVector.Add(
				        new YarpRunCmdWithStdioInfo(
				            cmd_str,alias,m_PortName,
					        pid_cmd,stdio_str,&m_StdioVector,
					        pid_stdin,pid_stdout,
					        pipe_stdin_to_cmd[READ_FROM_PIPE],pipe_stdin_to_cmd[WRITE_TO_PIPE],
					        pipe_cmd_to_stdout[READ_FROM_PIPE],pipe_cmd_to_stdout[WRITE_TO_PIPE],
					        pid_cmd
					    )
				    );
				    */
	                
	                result.addInt(pid_cmd);
	                out=yarp::os::impl::String("STARTED: server=")+m_PortName.c_str()
	                   +" alias="+alias.c_str()+" cmd="+commandString.c_str()+" pid="
	                   +pidstr+"\n";
	            }
	            
	            result.addString(out.c_str());
	            fprintf(stderr,out.c_str());
                fflush(stderr);
  
                CLOSE(pipe_child_to_parent[READ_FROM_PIPE]);
                CLOSE(pipe_child_to_parent[WRITE_TO_PIPE]);
                
		        return result;
			}
		}
	}

    yarp::os::Bottle result;
    result.addInt(YARPRUN_ERROR);
    result.addString("I should never reach this point!!!\n");
	return result;
}

yarp::os::Bottle yarp::os::Run::UserStdio(yarp::os::Bottle& msg)
{
	yarp::os::ConstString alias(msg.find("as").asString());

	int  pipe_child_to_parent[2];
	pipe(pipe_child_to_parent);

	int pid_cmd=fork();

	if (IS_INVALID(pid_cmd))
	{
		int error=errno;
	    
		yarp::os::ConstString out=yarp::os::ConstString("ABORTED: server=")+m_PortName+" alias="+alias+" cmd=stdio\n"
                       +yarp::os::ConstString("Can't fork stdout process because ")+strerror(error)+"\n";
        
        Bottle result;
        result.addInt(YARPRUN_ERROR);		
        result.addString(out.c_str());
        fprintf(stderr,out.c_str());
        fflush(stderr);
	
		return result;
	}

	if (IS_NEW_PROCESS(pid_cmd)) // RUN COMMAND HERE
	{
		int ret;
        yarp::os::ConstString command=yarp::os::ConstString("/bin/bash -l -c \"yarp quiet readwrite /")+alias+"/user/stdout /"+alias+"/user/stdin\"";
        //yarp::os::ConstString command=yarp::os::ConstString("\"yarp quiet readwrite /")+alias+"/user/stdout /"+alias+"/user/stdin\"";
        const char *hold=msg.check("hold")?"-hold":"+hold";

        setvbuf(stdout,NULL,_IONBF,0);

		if (msg.check("geometry"))
		{
	        yarp::os::ConstString geometry(msg.find("geometry").asString());
			//ret=execlp("xterm","xterm",hold,"-geometry",geometry.c_str(),"-title",alias.c_str(),"-e","yarp","quiet","readwrite",
			//          (yarp::os::ConstString("/")+alias+"/user/stdout").c_str(),(yarp::os::ConstString("/")+alias+"/user/stdin").c_str(),NULL);
			ret=execlp("xterm","xterm",hold,"-geometry",geometry.c_str(),"-title",alias.c_str(),"-e",command.c_str(),NULL);
		}
		else
		{
			//ret=execlp("xterm","xterm",hold,"-title",alias.c_str(),"-e","yarp","quiet","readwrite",
			//          (yarp::os::ConstString("/")+alias+"/user/stdout").c_str(),(yarp::os::ConstString("/")+alias+"/user/stdin").c_str(),NULL);
			ret=execlp("xterm","xterm",hold,"-title",alias.c_str(),"-e",command.c_str(),NULL);
		}
		
		fflush(stdout);
		fflush(stderr);
		
		if (ret==YARPRUN_ERROR)
		{
		    int error=errno;
            yarp::os::ConstString out=yarp::os::ConstString("ABORTED: server=")+m_PortName+" alias="+alias+" cmd=xterm\n"
                           +yarp::os::ConstString("Can't execute command because ")+strerror(error)+"\n";
	     
	        FILE* out_to_parent=fdopen(pipe_child_to_parent[WRITE_TO_PIPE],"w");
	        fprintf(out_to_parent,out.c_str());
	        fflush(out_to_parent);
	        fclose(out_to_parent);
	        fprintf(stderr,out.c_str());
            fflush(stderr);
		}
		
		exit(ret);
	}

	if (IS_PARENT_OF(pid_cmd))
	{
	    yarp::os::ConstString rwCmd=yarp::os::ConstString("xterm -e /bin/bash -l -c yarp readwrite /")+alias+"/user/stdout /"+alias+"/user/stdin";
		m_StdioVector.Add(new YarpRunProcInfo(rwCmd,alias,m_PortName,pid_cmd,NULL,msg.check("hold")));
	    
	    char pidstr[16];
	    sprintf(pidstr,"%d",pid_cmd);
	    
	    Bottle result;
	    yarp::os::impl::String out;
	    
	    FILE* in_from_child=fdopen(pipe_child_to_parent[READ_FROM_PIPE],"r");
	    int flags=fcntl(pipe_child_to_parent[READ_FROM_PIPE],F_GETFL,0);
	    fcntl(pipe_child_to_parent[READ_FROM_PIPE],F_SETFL,flags|O_NONBLOCK); 
        for (char buff[1024]; fgets(buff,1024,in_from_child);)
	    {
	        out+=yarp::os::ConstString(buff);
	    }
	    fclose(in_from_child);
	    
	    if (out.length()>0)
	    {
	        result.addInt(YARPRUN_ERROR);
	    }
	    else
	    {
	        //yarp::os::ConstString rwCmd=yarp::os::ConstString("xterm -e /bin/bash -l -c yarp readwrite /")+alias+"/user/stdout /"+alias+"/user/stdin"; 
	        //m_StdioVector.Add(new YarpRunProcInfo(rwCmd,alias,m_PortName,pid_cmd,msg.check("hold")));
	        result.addInt(pid_cmd);
	        out=yarp::os::impl::String("STARTED: server=")+m_PortName.c_str()+" alias="+alias.c_str()+" cmd=xterm pid="+pidstr+"\n";
	        fprintf(stderr,out.c_str());
            fflush(stderr);
	    }
	    
	    result.addString(out.c_str());
 
        CLOSE(pipe_child_to_parent[READ_FROM_PIPE]);
        CLOSE(pipe_child_to_parent[WRITE_TO_PIPE]);
                
		return result;
	}
	
	Bottle result;
    result.addInt(YARPRUN_ERROR);
	return result;
}

yarp::os::Bottle yarp::os::Run::ExecuteCmd(yarp::os::Bottle& msg)
{
	yarp::os::ConstString alias(msg.find("as").asString());
	yarp::os::ConstString commandString(msg.find("cmd").toString());

	int  pipe_child_to_parent[2];
	pipe(pipe_child_to_parent);

	int pid_cmd=fork();

	if (IS_INVALID(pid_cmd))
	{
	    int error=errno;
	    	     	
	    yarp::os::ConstString out=yarp::os::ConstString("ABORTED: server=")+m_PortName+" alias="+alias+" cmd="+commandString+"\n"
                       +yarp::os::ConstString("Can't fork command process because ")+strerror(error)+"\n";
	            
		Bottle result;
        result.addInt(YARPRUN_ERROR);	            
	    result.addString(out.c_str());
	    fprintf(stderr,out.c_str());
	    fflush(stderr);
		
		return result;
	}

	if (IS_NEW_PROCESS(pid_cmd)) // RUN COMMAND HERE
	{
        int saved_stderr=dup(STDERR_FILENO);
		int null_file=open("/dev/null",O_WRONLY); 
        REDIRECT_TO(STDOUT_FILENO,null_file);
		REDIRECT_TO(STDERR_FILENO,null_file);
		close(null_file);
        
        char *cmd_str=new char[commandString.length()+1];
        strcpy(cmd_str,commandString.c_str());
        int nargs=CountArgs(cmd_str);
        char **arg_str=new char*[nargs+1];
        ParseCmd(cmd_str,arg_str);
        arg_str[nargs]=0;
        
        if (msg.check("workdir"))
		{
		    chdir(msg.find("workdir").asString().c_str());
		}
        
		int ret=execvp(arg_str[0],arg_str);

        if (ret==YARPRUN_ERROR)
	    {
	        int error=errno;
            yarp::os::ConstString out=yarp::os::ConstString("ABORTED: server=")+m_PortName+" alias="+alias+" cmd="+commandString+"\n"
                           +yarp::os::ConstString("Can't execute command because ")+strerror(error)+"\n";
	     
	        FILE* out_to_parent=fdopen(pipe_child_to_parent[WRITE_TO_PIPE],"w");
	        fprintf(out_to_parent,out.c_str());
	        fflush(out_to_parent);
	        fclose(out_to_parent);

		    REDIRECT_TO(STDERR_FILENO,saved_stderr);
	        fprintf(stderr,out.c_str());
            fflush(stderr);
	    }

		delete [] cmd_str;
		delete [] arg_str;

		exit(ret);
	}

	if (IS_PARENT_OF(pid_cmd))
	{
		m_ProcessVector.Add(new YarpRunProcInfo(commandString,alias,m_PortName,pid_cmd,NULL,false));
		
	    char pidstr[16];
	    sprintf(pidstr,"%d",pid_cmd);

        Bottle result;
        yarp::os::impl::String out;
         
        FILE* in_from_child=fdopen(pipe_child_to_parent[READ_FROM_PIPE],"r");
	    int flags=fcntl(pipe_child_to_parent[READ_FROM_PIPE],F_GETFL,0);
	    fcntl(pipe_child_to_parent[READ_FROM_PIPE],F_SETFL,flags|O_NONBLOCK); 
        for (char buff[1024]; fgets(buff,1024,in_from_child);)
	    {
	        out+=yarp::os::impl::String(buff);
	    }
	    fclose(in_from_child);
	    
	    if (out.length()>0)
	    {
	        result.addInt(YARPRUN_ERROR);
	    }
	    else
	    {
	        //m_ProcessVector.Add(new YarpRunProcInfo(commandString,alias,m_PortName,pid_cmd,false));
	        result.addInt(pid_cmd);
	        out=yarp::os::impl::String("STARTED: server=")+m_PortName.c_str()+" alias="+alias.c_str()+" cmd="+commandString.c_str()+" pid="+pidstr+"\n";
	        fprintf(stderr,out.c_str());
            fflush(stderr);
	    }
	    
	    result.addString(out.c_str());
 
        CLOSE(pipe_child_to_parent[READ_FROM_PIPE]);
        CLOSE(pipe_child_to_parent[WRITE_TO_PIPE]);
                
		return result;
	}

	Bottle result;
    result.addInt(YARPRUN_ERROR);
	return result;
}

#endif

/////////////////////////////////////////////////////////////////
// API
/////////////////////////////////////////////////////////////////

int yarp::os::Run::start(const yarp::os::ConstString &node,Property &command,yarp::os::ConstString &keyv)
{
	yarp::os::Bottle msg,grp,response;
	
	grp.clear();
	grp.addString("on");
	grp.addString(node.c_str());
	msg.addList()=grp;

	yarp::os::ConstString dest_srv=node;

	if (command.check("stdio"))
	{
		dest_srv=yarp::os::ConstString(command.find("stdio").asString());

		grp.clear();
		grp.addString("stdio");
		grp.addString(dest_srv.c_str());
		msg.addList()=grp;

		if (command.check("geometry"))
		{
			grp.clear();
			grp.addString("geometry");
			grp.addString(command.find("geometry").asString().c_str());
			msg.addList()=grp;
		}
		
		if (command.check("hold"))
		{
			grp.clear();
			grp.addString("hold");
			msg.addList()=grp;
		}
	}

	grp.clear();
	grp.addString("as");
	grp.addString(keyv.c_str());
	msg.addList()=grp;

	grp.clear();
	grp.addString("cmd");
	grp.addString(command.find("name").asString().c_str());
	grp.addString(command.find("parameters").asString().c_str());
	msg.addList()=grp;

	printf(":: %s\n",msg.toString().c_str());

    response=SendMsg(msg,dest_srv.c_str());

	char buff[16];
	sprintf(buff,"%d",response.get(0).asInt());
    keyv=yarp::os::ConstString(buff);

	return response.get(0).asInt()>0?0:YARPRUN_ERROR;
}

int yarp::os::Run::sigterm(const yarp::os::ConstString &node, const yarp::os::ConstString &keyv)
{
	yarp::os::Bottle msg,grp,response;
	
	grp.clear();
	grp.addString("on");
	grp.addString(node.c_str());
	msg.addList()=grp;

	grp.clear();
	grp.addString("sigterm");
	grp.addString(keyv.c_str());
	msg.addList()=grp;

	printf(":: %s\n",msg.toString().c_str());

    response=SendMsg(msg,node.c_str());

	return response.get(0).asString()=="sigterm OK"?0:YARPRUN_ERROR;
}

int yarp::os::Run::sigterm(const yarp::os::ConstString &node)
{
	yarp::os::Bottle msg,grp,response;
	
	grp.clear();
	grp.addString("on");
	grp.addString(node.c_str());
	msg.addList()=grp;

	grp.clear();
	grp.addString("sigtermall");
	msg.addList()=grp;

	printf(":: %s\n",msg.toString().c_str());

    response=SendMsg(msg,node.c_str());

	return response.get(0).asString()=="sigtermall OK"?0:YARPRUN_ERROR;
}

int yarp::os::Run::kill(const yarp::os::ConstString &node, const yarp::os::ConstString &keyv,int s)
{
	yarp::os::Bottle msg,grp,response;
	
	grp.clear();
	grp.addString("on");
	grp.addString(node.c_str());
	msg.addList()=grp;

	grp.clear();
	grp.addString("kill");
	grp.addString(keyv.c_str());
	grp.addInt(s);
	msg.addList()=grp;

	printf(":: %s\n",msg.toString().c_str());

    response=SendMsg(msg,node.c_str());

	return response.get(0).asString()=="kill OK"?0:YARPRUN_ERROR;
}

int yarp::os::Run::ps(const yarp::os::ConstString &node,yarp::os::ConstString** &processes,int &num_processes)
{
	yarp::os::Bottle msg,grp,response;
	
	grp.clear();
	grp.addString("on");
	grp.addString(node.c_str());
	msg.addList()=grp;

	grp.clear();
	grp.addString("ps");
	msg.addList()=grp;

	printf(":: %s\n",msg.toString().c_str());

	Port port;
    port.open("...");
    for (int i=0; i<20; ++i)
	{
	    if (NetworkBase::connect(port.getName().c_str(),node.c_str())) break;
	    yarp::os::Time::delay(1.0);
	}
    port.write(msg,response);
    NetworkBase::disconnect(port.getName().c_str(),node.c_str());
    port.close();

    num_processes=response.size();

    processes=new yarp::os::ConstString*[num_processes];

	char buff[16];
	for (int i=0; i<num_processes; ++i)
	{
		sprintf(buff,"%d",response.get(i).find("pid").asInt());
		processes[i]=new yarp::os::ConstString(buff);
	}

	return 0;
}

bool yarp::os::Run::isRunning(const yarp::os::ConstString &node, yarp::os::ConstString &keyv)
{
	yarp::os::Bottle msg,grp,response;
	
	grp.clear();
	grp.addString("on");
	grp.addString(node.c_str());
	msg.addList()=grp;

	grp.clear();
	grp.addString("isrunning");
	grp.addString(keyv.c_str());
	msg.addList()=grp;

	printf(":: %s\n",msg.toString().c_str());

	Port port;
    port.open("...");
    for (int i=0; i<20; ++i)
	{
	    if (NetworkBase::connect(port.getName().c_str(),node.c_str())) break;
	    yarp::os::Time::delay(1.0);
	}
    port.write(msg,response);
    NetworkBase::disconnect(port.getName().c_str(),node.c_str());
    port.close();

	if (!response.size()) return false;

	return response.get(0).asString()=="running";
}

// end API


#if defined(WIN32) || defined(WIN64)

#define TA_FAILED        0
#define TA_SUCCESS_CLEAN 1
#define TA_SUCCESS_KILL  2

BOOL CALLBACK TerminateAppEnum( HWND hwnd, LPARAM lParam )
{
	DWORD dwID ;

	GetWindowThreadProcessId(hwnd,&dwID) ;

	if (dwID==(DWORD)lParam)
	{
		PostMessage(hwnd,WM_CLOSE,0,0) ;
	}

	return TRUE ;
}

/*----------------------------------------------------------------
Purpose:
Shut down a 32-Bit Process

Parameters:
dwPID
Process ID of the process to shut down.

dwTimeout
Wait time in milliseconds before shutting down the process.

Return Value:
TA_FAILED - If the shutdown failed.
TA_SUCCESS_CLEAN - If the process was shutdown using WM_CLOSE.
TA_SUCCESS_KILL - if the process was shut down with TerminateProcess().
----------------------------------------------------------------*/ 
bool TERMINATE(PID dwPID) 
{
    static const DWORD dwTimeout=6000;

	HANDLE hProc;
	DWORD dwRet;

	// If we can't open the process with PROCESS_TERMINATE rights,
	// then we give up immediately.
	hProc=OpenProcess(SYNCHRONIZE|PROCESS_TERMINATE,FALSE,dwPID);

	if (hProc==NULL)
	{
		return false;
	}

	// TerminateAppEnum() posts WM_CLOSE to all windows whose PID
	// matches your process's.
	EnumWindows((WNDENUMPROC)TerminateAppEnum,(LPARAM)dwPID);

	// Wait on the handle. If it signals, great. If it times out,
	// then you kill it.
	if (WaitForSingleObject(hProc,dwTimeout)!=WAIT_OBJECT_0)
	{
		dwRet=(TerminateProcess(hProc,0)?TA_SUCCESS_KILL:TA_FAILED);
        fprintf(stdout,"%d brutally terminated\n",dwPID);
        fflush(stdout);
	}
	else
	{
		dwRet=TA_SUCCESS_CLEAN;
        fprintf(stdout,"%d clean terminated\n",dwPID);
        fflush(stdout);
	}

	CloseHandle(hProc);

    return dwRet?true:false;
}

#endif
