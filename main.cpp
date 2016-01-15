#include "../network_common/main.h"
#include <vector>
#include <map>

using namespace std;

vector<thread_socket*> vts; //stores all the connections
map<IP_ADDR, string> ip_host; //get a host from an ip
map<string, IP_ADDR> host_ip; //and do it the other way
map<string, thread_socket*> host_socket; //converts a host name to a socket structure

vector<HostNode*> hosts; //contains all the hosts
//map<long, SOCKET> ip_socket;
//vector<IP_PAIR> chat_list;
//vector<long> available_ip;

//this is so that the map can be sorted
bool operator<(const IP_ADDR& lhs, const IP_ADDR& rhs)
{
    return (lhs.source_ip + lhs.source_port) < (rhs.source_ip + rhs.source_port);
}

//this will be a linked list of hosts
class HostNode
{
    private:
        string m_string; //the data
        vector<HostNode*> connected_hosts; //pointers to host nodes
        //HostNode* m_next; //linked list of connected hosts
    public:
        HostNode(string s) : m_string(s) { };
        string GetString() { return m_string; };
        void AddHost(int host_index)
        {
           HostNode* hn = hosts[host_index];
           cout << "Adding Host " << hn->GetString() << " to " << m_string << "'s list of connections." << endl;
           connected_hosts.push_back(hn);
        }
        void RemoveHost(int host_index)
        {
            HostNode* hn = hosts[host_index];
            vector<HostNode*>::iterator it;
            it = find(connected_hosts.begin(), connected_hosts.end(), hn);
            if (it != connected_hosts.end())
            {
                connected_hosts.erase(it);
                cout << "Removed Host " << hn->GetString() << " from " << m_string << "'s list of connections." << endl;
            }
        }
        bool Search(int host_index)
        {
            HostNode* hn = hosts[host_index];
            vector<HostNode*>::iterator it;
            it = find(connected_hosts.begin(), connected_hosts.end(), hn);
            if (it != connected_hosts.end())
                return true;
            return false;
        }
        void SendToAll(int message_type, const char* data)
        {
            //HostNode* temp = this;
            string hostString;

            for (int i = 0; i < connected_hosts.size(); i++)
            {
                hostString = connected_hosts[i]->GetString();
                cout << "Sending data reading:  " << data << ", to " << hostString << endl;
                thread_socket* hostSocket = host_socket[hostString];
                SendMessage(hostSocket, message_type, data);
            }
        }
};

bool serverAlive = true;
string server_password = "password";

void ClientLoop(void* lParam)
{
    //keep running the loop until the user sends a CM_EXIT message
    bool done = false; //a cm_exit message (or a force quit) will set this to true
    char incomingBuf[128]; //try to receive as much data as possible; this is a temporary buffer
    thread_socket* ts = (thread_socket*)lParam;
    char* data = (char*)malloc(MAX_MESSAGE); //this is to hold the total incoming packet buffer
    char* originalData = data;
    char* cmdata = (char*)malloc(MAX_MESSAGE); //this is so that cm.data points to something valid

    while (!done)
    {
        //reset the data position
        data = originalData;
        //get the packet data!
        memset(incomingBuf, 0, sizeof(char)*128);
        int bytesReceived = -1;
        u_long mode = 1;
        ioctlsocket(ts->socket, FIONBIO, &mode); //set socket to non-blocking
        while (bytesReceived < 0)
            bytesReceived = recv(ts->socket, incomingBuf, 128, 0); //keep doing this until actual data is received
        cout << "Received " << bytesReceived << " bytes." << endl;

        if (bytesReceived > 0)
        {
            //got a packet
            //get the packet size
            int totalReceived = 0;
            unsigned long* _pMessageSize = (unsigned long*)incomingBuf;
            unsigned int packetSize = ntohl(*_pMessageSize); //conver to host order
            cout << "Packet Size:  " << packetSize << endl;
            //now put the socket in blocking mode temporarily
            mode = 0;
            ioctlsocket(ts->socket, FIONBIO, &mode); //set socket to blocking
            do
            {
                memcpy(data, incomingBuf, sizeof(char) * bytesReceived);
                data += bytesReceived;
                totalReceived += bytesReceived;
                if (totalReceived < packetSize)
                {
                    bytesReceived = recv(ts->socket, incomingBuf, 128, 0); //recv again
                    cout << "Received " << bytesReceived << " more bytes." << endl;
                }
            } while (totalReceived < packetSize && !bytesReceived != 0);
        }
        if (bytesReceived == 0)
        {
            done = true; //connection wants to close
            free(originalData);
            free(cmdata);
        }

        //now interpret the packet data
        if (!done)
        {
            //derive the chat message
            CHAT_MESSAGE receivedMessage = deserializeMessage(originalData, cmdata);
            unsigned short type = receivedMessage.message_type;
            unsigned short dataSize = receivedMessage.data_size;
            unsigned long ip = receivedMessage.source_ip;
            unsigned short port = receivedMessage.source_port;
            IP_ADDR ia;
            ia.source_ip = ip;
            ia.source_port = port;
            //data = originalData;
            //memcpy(data, receivedMessage.data, dataSize + 1); //we can overwrite the originalData
            //data[dataSize] = '\0'; //make it a zero-terminated string

            cout << "Received Message " << type << " from " << ip << " port " << port << endl;
            cout << "Data reads:  " << receivedMessage.data << endl;
            //now let's interpret the chat message
            switch(type)
            {
                case CM_RETRIEVE:
                {
                    //chat list needed
                    //search through all the hosts, and get their IP_ADDRESSes

                    //but first, get the index of the client host
                    unsigned int index = -1; //client index
                    map<IP_ADDR, string>::iterator it;
                    it = ip_host.find(ia);
                    if (it != ip_host.end())
                    {   //host exists
                        string name = it->second;
                        unsigned int k = 0;
                        for (; k < hosts.size() && hosts[k]->GetString() != name; k++);
                        if (!hosts.empty() && hosts[k]->GetString() == name)
                            index = k;
                    }
                    //however, if this is the person's first time connecting, it will not
                    //be in the hosts

                    //format - name ipaddress:port|1, name ipaddress:port|0, [next record]...
                    //the 1 and 0 signify whether or not the client is connected
                    string total = "";
                    for (unsigned int i = 0; i < hosts.size(); i++)
                    {
                        if (i != index)
                        {
                            string hostname = hosts[i]->GetString();
                            thread_socket* ts_temp = host_socket[hostname];
                            string ipaddress = ts_temp->address_string;
                            total += hostname + " " + ipaddress + "|";
                            //now look up host name in client's list of connected hosts
                            if (index < hosts.size() && index >= 0 && hosts[index]->Search(i))
                                total += "1";
                            else
                                total += "0";
                            total += ", ";
                        }
                    }
                    SendMessage(ts, SM_CLIENTLIST, total.c_str()); //send the client list message
                }
                break;
                case CM_ADD:
                {
                    //new host
                    //first, check if it isn't already taken
                    string hostname;
                    hostname.assign(receivedMessage.data);
                    size_t i = 0;
                    for (; i < hosts.size() && hostname != hosts[i]->GetString(); i++);
                    if (i < hosts.size() && hostname == hosts[i]->GetString())
                        SendMessage(ts, SM_ALREADYUSED);

                    //if not, add a new host node to the hosts vector
                    else
                    {
                        HostNode* hn = new HostNode(hostname);
                        hosts.push_back(hn);
                        //and create some new mappings
                        //IP_ADDR new_ia;
                        //new_ia.source_ip = ip;
                        //new_ia.source_port = port;
                        host_ip[hostname] = ia; //host to ip
                        ip_host[ia] = hostname; //ip to host
                        host_socket[hostname] = ts; //host to socket
                        //send the success message
                        SendMessage(ts, SM_SUCCESS);
                    }
                }
                break;
                case CM_CONNECT:
                {
                    //client wants to connect to other client

                    //check to make sure the host client is both available and not "the client himself"
                    string hostname;
                    hostname.assign(receivedMessage.data);

                    //IP_ADDR ia;
                    //ia.source_ip = ip;
                    //ia.source_port = port;
                    string source_host = ip_host[ia];
                    if (hostname == source_host)
                        SendMessage(ts, SM_FAILURE, "You cannot connect to yourself.");
                    else
                    {
                        size_t k = 0; //this is the index of the host you want to connect to
                        for (; k < hosts.size() && hosts[k]->GetString() != hostname; k++);
                        if (k == hosts.size())
                            SendMessage(ts, SM_FAILURE, "Host is unavailable.");
                        else if (hosts[k]->GetString() == hostname)
                        {
                            //now check to see if the host is already connected to
                            size_t index = 0;
                            for (; index < hosts.size() && hosts[index]->GetString() != source_host; index++);

                            if (hosts[index]->Search(k))
                                SendMessage(ts, SM_FAILURE, "Already connected to host."); //already connected to host
                            //if these checks pass, add a new node to the client's list of connected hosts
                            else
                            {
                                hosts[index]->AddHost(k);
                                SendMessage(ts, SM_SUCCESS, "Connected to host!");
                            }
                        }
                    }
                }
                break;
                case CM_DISCONNECT:
                {
                    //client wants to disconnect from other client

                    //check to make sure the host client isn't "himself" (and is also available and connected to)
                    string hostname;
                    hostname.assign(receivedMessage.data);

                    //IP_ADDR ia;
                    //ia.source_ip = ip;
                    //ia.source_port = port;
                    string source_host = ip_host[ia];
                    if (hostname == source_host) //host is himself
                        SendMessage(ts, SM_FAILURE, "You cannot disconnect from yourself.");
                    else
                    {
                        size_t k = 0; //will be the index of host name
                        for (; k < hosts.size() && hosts[k]->GetString() != hostname; k++);
                        if (k == hosts.size())
                            SendMessage(ts, SM_FAILURE, "The host is unavailable.");
                        else if (hosts[k]->GetString() == hostname)
                        {
                            //since it is available, check to see if it is connected
                            //now check to see if the host is already connected to
                            size_t index = 0; //index is source_host
                            for (; index < hosts.size() && hosts[index]->GetString() != source_host; index++);

                            if (!hosts[index]->Search(k))
                                SendMessage(ts, SM_FAILURE, "Not connected to host."); //not connected to host
                            else
                            {
                                //if these checks pass, remove the node from the client's list of connected hosts
                                hosts[index]->RemoveHost(k);
                                SendMessage(ts, SM_SUCCESS, "Successfully Removed Host");
                                //send a SM_UNAVAILABLE message to host
                                thread_socket* ts = host_socket[hostname];
                                SendMessage(ts, SM_UNAVAILABLE, source_host.c_str());
                            }
                        }
                    }
                }
                break;
                case CM_EXIT:
                {
                    string source_name = ip_host[ia];
                    //send SM_UNAVAILABLE messages
                    size_t k = 0;
                    for (; k < hosts.size() && hosts[k]->GetString() != source_name; k++);
                    if (!hosts.empty() && hosts[k]->GetString() == source_name)
                        hosts[k]->SendToAll(SM_UNAVAILABLE, source_name.c_str());
                    //manually disconnect it from all connected hosts
                    for (int i = 0; i < hosts.size(); i++)
                        hosts[i]->RemoveHost(k);

                    //do cleanup
                    //delete it from the hosts vector
                    if (hosts[k]->GetString() == source_name)
                    {
                        delete hosts[k];
                        hosts.erase(hosts.begin() + k);
                    }

                    //remove it from the maps
                    ip_host.erase(ia);
                    host_ip.erase(source_name);
                    host_socket.erase(source_name);
                    //lastly, close the socket
                    closesocket(ts->socket); //[[]]
                    //remove it from vts
                    size_t index = 0;
                    for (; index < vts.size() && vts[index]->thread_id != ts->thread_id; index++);
                    int thread = vts[index]->thread_id;
                    if (vts[index]->thread_id == ts->thread_id)
                    {
                        delete vts[index]; //de-allocate storage
                        vts.erase(vts.begin() + index);
                    }
                    free(originalData); //clean up data as you exit the process
                    free(cmdata); //clean up data
                    done = true;
                    cout << "Thread #" << thread << " terminated." << endl;
                }
                break;
                case CM_POWEROFF:
                {
                    //check the password
                    string password;
                    password.assign(receivedMessage.data);
                    cout << "Password entered is:  " << receivedMessage.data << endl;
                    if (password == server_password)
                    {
                        free(originalData); //clean up
                        free(cmdata); //clean up
                        done = true;
                        serverAlive = false;
                        //when the server exits its main loop, it will send everybody
                        //a server shutdown message and then deallocate all the memory
                    }
                    else
                        SendMessage(ts, SM_FAILURE, "Wrong Password");
                }
                break;
                case DATA:
                {
                    //send data to all connected clients
                    string dataToSend;
                    dataToSend.assign(receivedMessage.data);
                    //IP_ADDR ia;
                    //ia.source_ip = ip;
                    //ia.source_port = port;
                    string source_host = ip_host[ia];
                    size_t index = 0; //get hosts index of the source host
                    for (; index < hosts.size() && hosts[index]->GetString() != source_host; index++);
                    if (hosts[index]->GetString() == source_host)
                    {
                        source_host += ": " + dataToSend;
                        hosts[index]->SendToAll(DATA, source_host.c_str());
                    }
                }
                break;
            }
        }
    }
    _endthread();
}

int main()
{
    WSADATA wsadata;
    int result = WSAStartup(MAKEWORD(2,2), &wsadata); //start up WSA
    //section #1 - get the address of host machine and print it to the log
    string serverAddress; //print the server's ip address soon
    string serverName; //print the server's name
    DWORD buffer_length = 256;
    char buffer[buffer_length]; //for serverAddress and serverName

    //print the name of server
    gethostname(buffer, sizeof(char)*buffer_length);
    serverName.assign(buffer);
    cout << serverName << endl;

    //get the ip address of server
    addrinfo hints; //input addrinfo
    addrinfo* servInfo; //output addrinfo linked list

    memset(&hints, 0, sizeof(hints));

    hints.ai_family = AF_INET; //internet address
    hints.ai_socktype = SOCK_STREAM; //socket

    int status = getaddrinfo(serverName.c_str(), PORT_NUMBER, &hints, &servInfo); //the name of this computer
    if (status != 0)
        cout << "getaddrinfo error: " << gai_strerror(status) << endl;
    //create a socket with the OS and server's listening port
    else
    {
        int mainSocket;
        bool bound = false; //we want to bind to the first valid address
        for (addrinfo* res = servInfo; res != NULL && !bound; res = res->ai_next)
        {
            //memset(buffer_two, 0, sizeof(char)*64);
            if (WSAAddressToString(res->ai_addr, res->ai_addrlen, NULL, buffer, &buffer_length) == SOCKET_ERROR)
                cout << "WSA Address String Error:  " << WSAGetLastError() << endl;

            serverAddress.assign(buffer);
            cout << "IP Address of " << serverName << ":  " << serverAddress << endl;
            //section #2 - begin
            mainSocket = socket(res->ai_family, res->ai_socktype, res->ai_protocol); //create the socket
            if (mainSocket == -1)
            {
                cout << "Socket Error" << endl;
                continue;
            }
            char yes = '1';
            if (setsockopt(mainSocket, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(char)) == -1)
                cout << "Set Socket Option Error" << endl;
            unsigned long iMode = 1;
            ioctlsocket(mainSocket, FIONBIO, &iMode); //set it to non-blocking
            //bind the socket
            if (bind(mainSocket, res->ai_addr, res->ai_addrlen) == -1)
            {
                cout << "Couldn't Bind" << endl;
                closesocket(mainSocket);
                continue;
            }
            else
                bound = true;
        }
        //now that we've created a socket between server and OS, we can run the main listening loop
        freeaddrinfo(servInfo); //we don't need the address info anymore
        //section #2 - create a server
        //send SM_WELCOME message to whoever tries to establish a connection to it
        cout << "Running server..." << endl;
        //listen() call
        listen(mainSocket, 20);
        sockaddr_storage their_addr; //address of whoever tries to connect
        socklen_t sin_size = sizeof(their_addr);
        do
        {
            int new_fd = accept(mainSocket, (sockaddr*)&their_addr, &sin_size);
            if (new_fd != -1)
            {
                //print "Client [IP Address] connected!" on server log
                WSAAddressToString((sockaddr*)&their_addr, sin_size, NULL, buffer, &buffer_length);
                string client_address;
                client_address.assign(buffer);
                cout << "Client " << client_address << " connected!" << endl;
                //create a new custom socket structure (a "connection")
                unsigned long iMode = 1;
                ioctlsocket(new_fd, FIONBIO, &iMode); //set it to nonblocking
                sockaddr_in* sai = (struct sockaddr_in*)&their_addr;
                thread_socket* ts = new thread_socket();
                ts->socket = new_fd;
                ts->sai = *sai;
                IP_ADDR ia;
                ia.source_port = ntohs(sai->sin_port);
                unsigned long ipa = sai->sin_addr.s_addr;
                ia.source_ip = ntohl(ipa);
                cout << "Source Port:  " << ia.source_port << ", Source IP:  " << ia.source_ip << endl;
                ts->ip_port = ia;
                ts->address_string = client_address;
                ts->thread_id = vts.size();
                vts.push_back(ts);
                //now send the client the acknowledgment message
                SendMessage(ts, SM_WELCOME, "Successfully connected!");

                //start a new connection thread
                _beginthread(ClientLoop, 0, (void*)ts);
            }
        } while (serverAlive);

        //a client typed poweroff
        //send everybody a SM_SERVEROFF message
        //and then get rid of them
        for (size_t i = 0; i < vts.size(); i++)
        {
            SendMessage(vts[i], SM_SERVEROFF);
            closesocket(vts[i]->socket); //[[]]
            delete vts[i];
        }
        vts.clear();

        //remove all the maps
        ip_host.clear();
        host_ip.clear();
        host_socket.clear();

        //remove all the hosts
        for (size_t i = 0; i < hosts.size(); i++)
            delete hosts[i];

        hosts.clear();

        //system("PAUSE");
        closesocket(mainSocket); //[[]] close the main socket
        //end section #2
    }
    WSACleanup();
    return 0;
}
