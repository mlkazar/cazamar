#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <pthread.h>
#include <string>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>
#include <unistd.h>
#include <stdlib.h>
#include <vector>
#include <map>
#include <string>
#include <string.h>
#include <assert.h>
#include "json.h"

#define QCMD_PORT            7704
#define QCMD_MAX_HOSTS       32

/* The controlling JSON structure is typically read from a file.  It
 * consists of a number of structure elements.
 *
 * One type of structure has a "type" key with value "data", a "name"
 * field with a string name, and a "data" element consisting of an
 * array of strings.
 *
 * Another type of structure has "type" "command", a "name" field, and
 * a required "command" value consisting of a parameterized string.  A
 * command item can be executed, in which case the command's
 * parameters will be interpreted as described below, and then passed
 * to all of the nodes named in the "hosts" data entry.
 *
 * Another type of structure has "type" "sequence", and a "sequence"
 * field giving an array of command names.  When executed, the named
 * commands will be performed in the sequence specified.
 *
 * The string $v:<variable name>$ looks up the variable name in the
 * set of data items in the configuration file, and replaces the
 * string between the '$' characters, inclusive, with a value from the
 * variable's data array.  When executing commands at multiple hosts,
 * each of which is executing a line including a variable, the
 * variable choice is selected in round robin fashion from the set in
 * the variable's array.  If we run out of data in the array before we
 * run out of hosts, the selection operator begins again at the start
 * of the array.
 * 
 * The special data variable 'hosts' gives the addresses of the peers to reach.  It
 * may be specified on the command line as a comma-separated list preceded by -h, or
 * in the qc.json file, as a normal data definition.
 */

/* fwd */
class Data;
class Client;
class Server;
class Listener;

/* one of these for each peer node, instantiated from a variable
 * list.
 */
class Peer {
public:
    std::string _hostName;
    struct sockaddr_in _addr;
    int _controlFd;     /* for STOP and other messages */
    int _commandFd;     /* for command data transfer; should be 'chaosFd' :-) */
    int _aborted;
    int _ix;
    static int _ixGenerator;

    Peer() {
        _controlFd = -1;
        _commandFd = -1;
        _aborted = 0;
        _addr.sin_addr.s_addr = 0;
        _ix = _ixGenerator++;
    }

    void abort() {
        if (_commandFd >= 0) {
            close(_commandFd);
            _commandFd = -1;
        }
        if (_controlFd >= 0) {
            close(_controlFd);
            _controlFd = -1;
        }
        _aborted = 1;
    }

    void reset() {
        _aborted = 0;
    }

    int isAborted() {
        return _aborted;
    }

};

/* static initialization */
int Peer::_ixGenerator = 0;

/* something that points into a variable being used for command lines.
 * We hang one of these off of the cursor object, but might in general
 * want to have these be completely indepedent of the specific Data
 * object and instead be found via an independent search.  But that's
 * probably overkill for now.
 */
class Cursor {
public:
    Data *_datap;
    int32_t _ix;

    Cursor() {
        _datap = NULL;
        _ix = 0;
    }

    void setData(Data *datap) {
        _datap = datap;
        _ix = 0;
    }

    void reset() {
        _ix = 0;
    }

    int32_t getNext(std::string *datap);
};

/* a variable definition, loaded from a file or the command line */
class Data {
public:
    std::string _name;
    std::vector<std::string> _data;
    Cursor _cursor;

    Data() {
        _cursor.setData(this);
    }

    int32_t size() {
        return _data.size();
    }

    std::string getNth(int32_t i) {
        return _data[i];
    }

    void clear() {
        _data.clear();
    }

    void add(std::string data) {
        _data.push_back(data);
    }

    Cursor *getCursor() {
        return &_cursor;
    }
};

class DataSymtab {
public:
    typedef std::pair<std::string, Data *> DataPair;
    std::map<std::string, Data *> _map;
    Data *get(std::string key) {
        std::map<std::string, Data *>::iterator it;
        
        it = _map.find(key);
        if (it == _map.end())
            return NULL;
        else 
            return it->second;
    }

    Data *add(std::string key) {
        Data *existingDatap;
        Data *datap;

        existingDatap = get(key);
        if (existingDatap)
            return existingDatap;
        datap = new Data();
        _map.insert( _map.begin(), DataPair(key, datap));
        return datap;
    }
};

class Listener {
public:
    int16_t _serverPort;
    int _controlFd;
    int _commandFd;
    int _listenFd;
    int _socketsArrived;
    std::vector<Server *> _servers;

    Listener(int16_t port) {
        _controlFd = -1;
        _commandFd = -1;
        _listenFd = -1;
        _socketsArrived = 0;
        _serverPort = port;   /* in host order */
    }

    void start();

    void checkServers();

    std::string processListenerCommand(std::string idata);
    
    void reset();
};

class Server {
public:    
    int16_t _serverPort;
    pthread_cond_t _cv;
    pthread_mutex_t _lock;
    int _controlFd;
    int _commandFd;
    int _runningPid;
    int _pipeFds[2];
    int _finished;
    int _terminatedThreads;
    pthread_t _controlId;
    pthread_t _commandId;
    std::string _inCommand;

    Server(int controlFd, int commandFd) {
        pthread_cond_init(&_cv, NULL);
        pthread_mutex_init(&_lock, NULL);
        _controlFd = controlFd;
        _commandFd = commandFd;
        _runningPid = -1;
        _finished = 0;
        _terminatedThreads = 0;
    }

    static void *controlHelper(void *arg);

    static void *commandHelper(void *arg);

    void commandLoop();

    void controlLoop();

    void start();

    std::string getStatus();

    void shutdown();

    int finished() {
        return _finished;
    }

    std::string localExecute(const char *cmdp);
};

class Client {
public:
    std::string _hostsArg;
    std::string _execArg;
    DataSymtab _symtab;
    std::vector<Peer> _peers;
    std::string _inFile;
    std::string _commandsName;
    int _systemCommand;

    int32_t parseArgs(int argc, char **argv);

    int32_t parseHostsFromArgs();

    int32_t parseHostsFromJson();

    int32_t addDataFromNamedArrays( Json::Node *rootNodep);

    int32_t parseFile(const char *fileNamep);

    int32_t sendCalls();

    void setupConns(int systemCommand);

    int32_t interpretParm( std::string opcode, 
                           std::string parm,
                           Peer *peerp,
                           std::string *resultp);

    int32_t processCommand(std::string inp, Peer *peerp, std::string *outp);

    Client() {
        _inFile = std::string("qcmd.json");
        _commandsName = "commands";
        _systemCommand = 0;
        return;
    }
};

int32_t
Cursor::getNext(std::string *datap) {
    int32_t count = _datap->size();
    if (_ix >= count)
        return -1;
    *datap = _datap->_data[_ix];
    return 0;
}

int32_t
Client::parseArgs(int argc, char **argv)
{
    int32_t i;

    for(i=2; i<argc; i++) {
        if (strcmp(argv[i], "-h") == 0) {
            /* remember the host list */
            if (argc > i+1)
                _hostsArg = std::string(argv[i+1]);
            else
                _hostsArg = std::string("");
            i++;
        }
        else if (strcmp(argv[i], "-f") == 0) {
            _inFile = std::string(argv[i+1]);
            i++;
        }
        else if (strcmp(argv[i], "-c") == 0) {
            _commandsName = std::string(argv[i+1]);
            i++;
        }
        else if (strcmp(argv[i], "-e") == 0) {
            _execArg = std::string(argv[i+1]);
            i++;
        }
        else if (strcmp(argv[i], "-s") == 0) {
            _systemCommand = 1;
            _execArg = std::string(argv[i+1]);
            i++;
        }
    }
    return 0;
}

/* common: read the result string, ending with a null character */
int
readString(int s, std::string *resultp)
{
    int code;
    char tbuffer[128];
    char *tp;
    int tc;
    int i;

    while(1) {
        code = read(s, tbuffer, sizeof(tbuffer));
        if (code < 0) return -1;
        if (code == 0) break;
        for(i=0, tp = tbuffer; i<code; i++, tp++) {
            tc = *tp;
            if (tc == 0) return 0;
            (*resultp).push_back(tc);
        }
    }
    return 0;
}

/* parse a set of comma separated host named into a vector of Peer
 * objects.
 */
int32_t
Client::parseHostsFromArgs()
{
    hostent *hep;
    Peer *peerp;
    const char *tp;
    const char *np;
    int i;
    char tname[1024];
    int16_t localPort;
    int16_t port;

    tp = _hostsArg.c_str();
    localPort = QCMD_PORT;
    while (1) {
        if (strlen(tp) == 0) break;
        np = strchr(tp, ',');
        if (np) {
            /* there's a comma present */
            i = np-tp;
            if (i >= sizeof(tname)-1) {
                printf("qcmd: hostname token too long\n");
                return -1;
            }
            strncpy(tname, tp, i);
            tname[i] = 0;
            tp += i+1;
        }
        else {
            /* all one name */
            i = strlen(tp);
            if (i >= (sizeof(tname)-1)) {
                printf("qcmd: hostname token too long\n");
                return -1;
            }
            strcpy(tname, tp);
            tp += i;
        }

        hep = gethostbyname(tname);
        if (!hep) {
            if (!hep) {
                printf("qcmd: host lookup failed for %s\n", tname);
                exit(1);
            }
        }

        peerp = new Peer();
        peerp->_hostName = tname;
#ifndef __linux__
        peerp->_addr.sin_len = sizeof(struct sockaddr_in);
#endif
        peerp->_addr.sin_family = AF_INET;

        if (strcmp(tname, "localhost") == 0) {
            /* rotate local hosts port so we can run multiple instances on a single machine */
            port = localPort++;
        }
        else {
            port = QCMD_PORT;
        }
        peerp->_addr.sin_port = htons(port);
        memcpy(&peerp->_addr.sin_addr.s_addr, hep->h_addr_list[0], 4);

        _peers.push_back(*peerp);
        delete peerp;
    }

    return 0;
}

int32_t
Client::parseHostsFromJson()
{
    hostent *hep;
    Peer *peerp;
    const char *tp;
    const char *np;
    int32_t i;
    int32_t count;
    Data *datap;
    const char *tnamep;
    int16_t localPort;
    int16_t port;
    std::string hostStr;

    datap = _symtab.get("hosts");
    count = datap->size();

    localPort = QCMD_PORT;
    for(i=0;i<count;i++) {
        hostStr = datap->getNth(i);
        tnamep = hostStr.c_str();

        hep = gethostbyname(tnamep);
        if (!hep) {
            if (!hep) {
                printf("qcmd: host lookup failed for %s\n", tnamep);
                exit(1);
            }
        }
        
        peerp = new Peer();
        peerp->_hostName = std::string(tnamep);
#ifndef __linux__
        peerp->_addr.sin_len = sizeof(struct sockaddr_in);
#endif
        peerp->_addr.sin_family = AF_INET;
        if (strcmp(tnamep, "localhost") == 0) {
            port = localPort++;
        }
        else {
            port = QCMD_PORT;
        }
        peerp->_addr.sin_port = htons(port);
        memcpy(&peerp->_addr.sin_addr.s_addr, hep->h_addr_list[0], 4);

        _peers.push_back(*peerp);
        delete peerp;
    }

    return 0;
}

void sigHandler(int x)
{
    printf("qcmd: received and ignoring signal %d\n", x);
    return;
}

/* search for a struct with the specified name, and whose value is an
 * array of strings to be added to the named data array.
 */
int32_t
Client::addDataFromNamedArrays(Json::Node *rootNodep)
{
    Json::Node *pnodep;
    Json::Node *cnodep;
    Json::Node *arrayNodep;
    Data *commandDatap;
    const char *namep;

    for(pnodep = rootNodep->_children.head(); pnodep; pnodep=pnodep->_dqNextp) {
        namep = pnodep->_name.c_str();
        commandDatap = _symtab.add(namep);
        arrayNodep = pnodep->_children.head();
        if (!arrayNodep->_isArray) {
            printf("qcmd: %s node's value must be an array of strings\n", namep);
            return -1;
        }
        for(cnodep = arrayNodep->_children.head(); cnodep; cnodep = cnodep->_dqNextp) {
            if (!cnodep->_isLeaf) {
                printf("qcmd: %s array elements must be simple strings\n", namep);
                return -1;
            }
            commandDatap->add(cnodep->_name);
        }
    }
    return 0;
}

int32_t
Client::parseFile(const char *fileNamep)
{
    FILE *filep;
    Json json;
    Json::Node *rootNodep;
    Json::Node *pnodep;
    int32_t code;
    Data *commandDatap = NULL;

    filep = fopen(fileNamep, "r");
    if (!filep)
        return -1;
    code = json.parseJsonFile(filep, &rootNodep);
    fclose(filep);
    if (code < 0) {
        return code;
    }
    
    /* iterate over all named arrays in the JSon file and add thenm to our
     * symbol table of data elements.
     */
    code = addDataFromNamedArrays( rootNodep);
    if (code) {
        delete rootNodep;
        return code;
    }

    delete rootNodep;
    return 0;
}

/* main command parser */
int
main (int argc, char **argv)
{
    char *tp;
    int i;
    int isServer = 0;
    int isClient = 0;
    int code;
    std::string cmd;
    Peer *hostsp;
    int16_t serverPort;
    Data *commandDatap;

    if (argc <= 1) {
        printf("usage: qcmd s\n");
        printf("usage: qcmd c \n");
        printf("usage:   -f <filename> to use different input file than qcmd.json\n");
        printf("usage:   -h host1,host2 to specify a host name overrride\n");
        printf("usage:   -e 'command' to perform a different command everywhere\n");
        return -1;
    }

    signal(SIGPIPE, sigHandler);
    signal(SIGHUP, sigHandler);

    hostsp = NULL;
    serverPort = QCMD_PORT;

    if (strcmp(argv[1], "s") == 0) {
        isServer = 1;
        if (argc > 2) {
            serverPort = atoi(argv[2]);
        }
    }
    else if (strcmp(argv[1], "c") == 0)
        isServer = 0;
    else {
        printf("qcmd: bad command: 'qcmd' for help\n");
        return -1;
    }

    if (isServer) {
        Listener listener(serverPort);

        listener.start();
    }
    else {
        Client client;
        std::string resultString;

        /* interpret the command line arguments */
        client.parseArgs(argc, argv);

        /* parse file */
        code = client.parseFile(client._inFile.c_str());
        if (code != 0) {
            printf("qcmd: failed to open file '%s'\n", client._inFile.c_str());
            return 0;
        }
        
        /* add in new data from -h, if present */
        if (client._hostsArg.size() > 0) {
            client.parseHostsFromArgs();
        }
        else {
            client.parseHostsFromJson();
        }

        /* replace commands data if -e provided */
        if (client._execArg.size() > 0) {
            commandDatap = client._symtab.add(client._commandsName);
            commandDatap->clear();
            commandDatap->add(client._execArg);
        }

        /* open connections */
        client.setupConns(client._systemCommand);

        /* send cmd to all hosts */
        client.sendCalls();
    }
}

/* sever: server loop, reading commands and dispatching them to a subprocess */
void
Listener::start()
{
    int ls;
    int s;
    int code;
    std::string inputData;
    std::string outputData;
    char *tp;
    struct sockaddr_in taddr;
    socklen_t taddrLen;
    pthread_t junkId;
    char socketType;
    Server *serverp;
    int flags;

    ls = socket(AF_INET, SOCK_STREAM, 0);

    /* setup reuseaddr */
    flags = 1;
    setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, &flags, sizeof(flags));

#ifndef __linux__
    taddr.sin_len = sizeof(struct sockaddr_in);
#endif
    taddr.sin_family = AF_INET;
    taddr.sin_port = htons(_serverPort);
    taddr.sin_addr.s_addr = htonl(0);
    code = bind(ls, (struct sockaddr *) &taddr, sizeof(taddr));
    if (code < 0) {
        perror("bind");
        exit(1);
    }

    code = listen(ls, 2);
    if (code < 0) {
        perror("listen");
        exit(1);
    }

    while(1) {
        /* wait for a shutdown to turn off socketsArrived */
        taddrLen = sizeof(taddr);
        s = accept(ls, (struct sockaddr *) &taddr, &taddrLen);
        if (s < 0) {
            perror("accept");
            // exit(1);
            continue;
        }

        checkServers();

        /* we should read a UUID here as well, to ensure that two concurrent
         * connection attempts don't confuse their sockets.  But for now, we ignore
         * this possibility.
         */
        code = read(s, &socketType, 1);
        if (code != 1) {
            printf("qcmd: bad incoming socket setup\n");
            reset();
            continue;
        }
        if (socketType == 'N') {
            _controlFd = s;
        }
        else if (socketType == 'M') {
            _commandFd = s;
        }
        else if (socketType == 'L') {
            inputData.erase();
            code = readString(s, &inputData);
            if (code < 0) {
                /* failed to read string */
                reset();
                continue;
            }
            outputData = processListenerCommand(inputData);
            write(s, outputData.c_str(), outputData.size()+1);
            reset();
        }
        else {
            printf("qcmd: bad incoming socket type '%c'\n", socketType);
            reset();
            continue;
        }

        /* see if we have both command and control sockets */
        if (_controlFd >= 0 && _commandFd >= 0) {
            serverp = new Server(_controlFd, _commandFd);
            _servers.push_back(serverp);
            serverp->start();
            
            /* reset the listener state for new calls */
            reset();
        }
    }
}

std::string
Listener::processListenerCommand(std::string idata)
{
    char tbuffer[1024];
    std::string odata;
    uint32_t i;
    Server *serverp;

    if (idata == "systat") {
        sprintf(tbuffer, "%lu active tasks\n", _servers.size());
        odata = std::string(tbuffer);
        for(i=0;i<_servers.size(); i++) {
            serverp = _servers[i];
            odata += serverp->getStatus();
        }
        return odata;
    }
    else if (idata == "help") {
        return std::string("qcmd: '-s systat' get system state\n");
    }
    else {
        return std::string("qcmd: unrecognized command, do '-s help' for full help\n");
    }
}

void
Listener::checkServers()
{
    uint32_t i;
    uint32_t count;
    std::vector<Server *>::iterator it;
    Server *serverp;
    
    for(it = _servers.begin(); it != _servers.end();) {
        serverp = *it;
        if (serverp->finished()) {
            it = _servers.erase(it);
            delete serverp;
        }
        else {
            it++;
        }
    }
}

void
Listener::reset()
{
    /* reset to wait for new connections to arrive */
    _commandFd = -1;
    _controlFd = -1;
    _socketsArrived = 0;
}

std::string 
Server::getStatus() 
{
    char tbuffer[1024];
    std::string odata;
    sprintf(tbuffer, "Pid %d is doing: '", _runningPid);
    odata = std::string(tbuffer);
    odata += _inCommand;
    odata += "'\n";
    return odata;
}

void *
Server::commandHelper(void *argp)
{
    Server *serverp = (Server *) argp;
    serverp->commandLoop();
    return NULL;
}

void *
Server::controlHelper(void *argp)
{
    Server *serverp = (Server *) argp;
    serverp->controlLoop();
    return NULL;
}

void
Server::start()
{
    pthread_create(&_controlId, NULL, controlHelper, this);
    pthread_create(&_commandId, NULL, commandHelper, this);
}

void
Server::shutdown()
{
    pthread_mutex_lock(&_lock);
    if (!_finished) {
        if (_controlFd > 0) {
            close(_controlFd);
            _controlFd = -1;
        }
        if (_commandFd > 0) {
            close(_commandFd);
            _commandFd = -1;
        }

        /* get us out of read from our child */
        if (_pipeFds[0] > 0) {
            close(_pipeFds[0]);
            _pipeFds[0] = -1;
        }

        if (_runningPid > 0) {
            killpg(_runningPid, 9);

            while(1) {
                /* clean up zombie processes */
                int statLoc;
                int code;
                code = wait4(-_runningPid, &statLoc, 0, NULL);
                if (code < 0) 
                    break;
            }
        }
        _finished = 1;
    }
    
    /* do join here for child threads? */

    pthread_mutex_unlock(&_lock);

}

/* loop of pthread that reads commands */
void
Server::commandLoop()
{
    int code;
    std::string data;
    char *tp;

    /* read commands until something bad happens */
    while(1) {
        _inCommand.erase();
        code = readString(_commandFd, &_inCommand);
        /* if we get an EOF or an error, we're done */
        if (code < 0 || _inCommand.length() == 0) {
            shutdown();
            break;
        }
        /* send command output back to the caller */
        data = localExecute(_inCommand.c_str());
        tp = const_cast<char *>(data.c_str());
        write(_commandFd, tp, data.length());
    }
    _terminatedThreads++;
}

void
Server::controlLoop()
{
    std::string inCommand;
    const char *tp;
    int code;

    /* read commands until something bad happens */
    while(1) {
        inCommand.erase();
        code = readString(_controlFd, &inCommand);
        /* if we get an EOF or an error, we're done */
        if (code < 0 || inCommand.length() == 0) {
            shutdown();
            break;
        }
        if (inCommand == "STOP") {
            printf("qcmd: received STOP command\n");
            shutdown();

            /* send command output back to the caller */
            tp = "DONE";
            write(_controlFd, tp, strlen(tp)+1); /* write null, too */
            break;
        }
        else {
            /* send command output back to the caller */
            tp = "DONE";
            write(_controlFd, tp, strlen(tp)+1); /* write null, too */
        }
    }
    _terminatedThreads++;
}

/* server: execute command in a subprocess, and return the 
 * string with the result.  String must be null terminated,
 * but that's our process's responsibility.
 */
std::string
Server::localExecute(const char *cmdp)
{
    int pid;
    char tbuffer[128];
    int code;
    std::string result;
    char tc;
    char *tp;

    /* write to pipeFds[1], read from pipeFds[0] */
    pipe(_pipeFds);

    pid = fork();
    if (pid == 0) {
        /* child */

        /* dup output pipe stdout(1) and stderr(2) */
        close(1);
        close(2);
        close(_pipeFds[0]);      /* we're not reading from this side */
        dup2(_pipeFds[1], 1);
        dup2(_pipeFds[1], 2);

        system(cmdp);

        _exit(0);
    }
    else {
        code = setpgid(pid, pid);
        if (code < 0)
            perror("setpgid");
        pthread_mutex_lock(&_lock);
        _runningPid = pid;
        pthread_mutex_unlock(&_lock);

        /* parent */
        result = "";
        close(_pipeFds[1]);      /* we're not writing from this side */
        _pipeFds[1] = -1;
        while(1) {
            code = read(_pipeFds[0], tbuffer, sizeof(tbuffer));
            if (code <= 0) {
                break;
            }
            result.append(tbuffer, code);
        }
        result.push_back((char) 0);
        
        if (_pipeFds[0] > 0) {
            close(_pipeFds[0]);
            _pipeFds[0] = -1;
        }

        while(1) {
            /* clean up zombie processes */
            int statLoc;
            code = wait4(-_runningPid, &statLoc, 0, NULL);
            if (code < 0) 
                break;
        }

        pthread_mutex_lock(&_lock);
        _runningPid = -1;
        pthread_mutex_unlock(&_lock);
    }

    return result;
}

int32_t
Client::interpretParm(std::string opcode, std::string parm, Peer *peerp, std::string *resultp)
{
    char tbuffer[1024];

    if (opcode == "host") {
        if (parm == "name") {
            resultp->append(peerp->_hostName);
        }
        else if (parm == "ix" || parm == "") {
            sprintf(tbuffer, "%d", peerp->_ix);
            resultp->append(std::string(tbuffer));
        }
    }
    return 0;
}

/* preprocess a command, substituting data for $v:name$ entries in a
 * round-robin fashion.
 */
int32_t
Client::processCommand(std::string inStr, Peer *peerp, std::string *outStrp)
{
    FILE *filep;
    int tc;
    int inDollar;
    std::string opcode;
    std::string parm;
    std::string interpretedParm;
    int32_t code;
    const char *inp;
    int haveOpcode;

    outStrp->erase();

    inDollar = 0;
    inp = inStr.c_str();
    while(1) {
        tc = *inp++;
        if (tc == 0)
            break;
        if (!inDollar) {
            if (tc == '$') {
                inDollar = 1;
                haveOpcode = 0;
                opcode.erase();
                parm.erase();
            }
            else {
                outStrp->append(1, tc);
            }
        }
        else {
            /* in a dollar sign region */
            if (!haveOpcode) {
                /* about to read the opcode */
                if (tc == '$' && opcode.size() == 0) {
                    /* this is a '$$' string, which just turns into a $ character */
                    outStrp->append(1, '$');
                    inDollar = 0;
                    continue;
                }

                /* add this character to the opcode field */
                if (tc == ':') {
                    haveOpcode = 1;
                    continue;
                }
                else if (tc == '$') {
                    /* opcode terminated without a parameter */
                    code = interpretParm(opcode, parm, peerp, outStrp);
                    haveOpcode = 1;
                    inDollar = 0;
                    continue;
                }

                opcode.push_back(tc);
            }
            else {
                /* opcode parsed, get the rest of the string */
                if (tc == '$') {
                    /* we're done parsing the parameter string */
                    code = interpretParm(opcode, parm, peerp, outStrp);
                    inDollar = 0;
                }
                else {
                    /* just another character in the parameter string */
                    parm.append(1, tc);
                }
            } /* saw opcode after '$' */
        } /* if '$' */
    } /* while */

    return 0;
}

/* client: call the client side code with a particular command, and print
 * out the results.
 */
int32_t
Client::sendCalls()
{
    Peer *peerp;
    int32_t cmdIx;
    int32_t i; /* index into peer array */
    int32_t peerCount;
    int32_t commandCount;
    std::string processedCommand;
    Data *commandsDatap;
    std::string baseCommand;
    char tbuffer[1024];
    int32_t code;
    uint8_t tc;
    int32_t failedCalls;
    std::string result;

    commandsDatap = _symtab.get(_commandsName.c_str());
    if (commandsDatap == NULL) {
        printf("qcmd: no such named item in data file (%s)\n", _commandsName.c_str());
        return -1;
    }
    
    failedCalls = 0;
    commandCount = commandsDatap->size();
    for(cmdIx=0;cmdIx<commandCount;cmdIx++) {
        baseCommand = commandsDatap->getNth(cmdIx);

        peerCount  = _peers.size();
        /* write all the commands out */
        result.clear();
        for(i=0; i<peerCount; i++) {
            peerp = &_peers[i];

            processCommand(baseCommand, peerp, &processedCommand);

            result += "qcmd: sending command '";
            result += processedCommand.c_str();
            sprintf(tbuffer, "' to host %d\n", i);
            result += tbuffer;
            
            if (peerp->isAborted()) {
                failedCalls++;
                continue;
            }
            
            if (peerp->_commandFd < 0) {
                continue;
            }

            code = write(peerp->_commandFd, processedCommand.c_str(), processedCommand.length());
            if (code != processedCommand.length()) {
                peerp->abort();
                failedCalls++;
                continue;
            }
            tc = 0;
            code = write(peerp->_commandFd, &tc, 1);
            if (code != 1) {
                peerp->abort();
                failedCalls++;
                continue;
            }
        }

        /* now read the responses */
        for(i=0; i<peerCount; i++) {
            peerp = &_peers[i];
            if (peerp->isAborted() || peerp->_commandFd < 0) {
                sprintf(tbuffer, "qcmd: peer %d failed\n", i);
                result += tbuffer;
                continue;
            }
            sprintf(tbuffer, "qcmd: peer %d results:\n", i);
            result += tbuffer;
            /* read the response, until we get a null termination or an error */
            readString(peerp->_commandFd, &result);
        }
        sprintf(tbuffer, "qcmd: command %d done\n", cmdIx);
        result += tbuffer;

        printf("%s\n", result.c_str());
    }

    printf("qcmd: all done with commands\n");

    return (failedCalls > 0? -1 : 0);
}

/* client: connect to all server machines, with both a command and a
 * control connection.  We can send "STOP" on the control connection
 * (or close it) to kill a running process.
 */
void
Client::setupConns(int systemCommand)
{
    Peer *peerp;
    int s;
    int code;
    struct sockaddr_in taddr;
    struct hostent *hep;
    int32_t count;
    int32_t i;
    char tc;

    count = _peers.size();
    for(i = 0;i<count;i++) {
        peerp = &_peers[i];
        hep = gethostbyname(peerp->_hostName.c_str());
        if (!hep) {
            printf("qcmd: can't find host '%s'\n", peerp->_hostName.c_str());
            exit(1);
        }

        /* setup the command connection */
        s = socket(AF_INET, SOCK_STREAM, 0);
        code = connect(s, (struct sockaddr *) &peerp->_addr, sizeof(struct sockaddr_in));
        if (code<0) {
            perror("connect");
            close(s);
            peerp->_commandFd = -1;
        }
        else {
            tc = (systemCommand? 'L' : 'M');
            write(s, &tc, 1);
            peerp->_commandFd = s;
        }

        if (!systemCommand) {
            /* setup the control connection */
            s = socket(AF_INET, SOCK_STREAM, 0);
            code = connect(s, (struct sockaddr *) &peerp->_addr, sizeof(struct sockaddr_in));
            if (code<0) {
                perror("connect");
                close(s);
                peerp->_controlFd = -1;
            }
            else {
                tc = 'N';
                write(s, &tc, 1);
                peerp->_controlFd = s;
            }
        }
    }
}
