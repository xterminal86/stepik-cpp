#include <cstdio>
#include <string>
#include <sstream>
#include <vector>
#include <map>

#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/ip.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>

const uint32_t MaxEvents = SOMAXCONN;
const uint64_t MessageBufferSize = 1024;

const uint32_t Mask1 = 0x000000FF;
const uint32_t Mask2 = 0x0000FF00;
const uint32_t Mask3 = 0x00FF0000;
const uint32_t Mask4 = 0xFF000000;

struct ClientInfo
{
  uint32_t Ip;
  int Fd;
};

std::map<int, ClientInfo> ClientInfoByFd;

// =============================================================================

template<typename ... Args>
std::string StringFormat(const std::string& format, Args ... args)
{
  size_t size = snprintf(nullptr, 0, format.c_str(), args ...);
  std::string s;

  if (!size)
  {
    return s;
  }

  s.resize(size);
  char *buf = (char *)s.data();
  snprintf(buf, size + 1, format.c_str(), args ...);
  return s;
}

// =============================================================================

void CheckError(bool isError, const std::string& errorMessage)
{
  if (isError)
  {
    printf("%s\n", errorMessage.data());
    perror(strerror(errno));
    exit(EXIT_FAILURE);
  }
}

// =============================================================================

void SetNonblock(int fd)
{
  int flags = fcntl(fd, F_GETFL);
  CheckError((flags == -1), "Couldn't GET flags!");

  flags |= O_NONBLOCK;

  int succ = fcntl(fd, F_SETFL, flags);
  CheckError((succ == -1), "Couldn't SET flags!");
}

// =============================================================================

void CloseConnection(int fd, bool ignoreErrors = true)
{
  int succ = shutdown(fd, SHUT_RDWR);
  if (not ignoreErrors)
  {
    CheckError((succ == -1), StringFormat("shutdown() fd = %i failed!", fd));
  }

  succ = close(fd);
  if (not ignoreErrors)
  {
    CheckError((succ == -1), StringFormat("close() fd = %i failed!", fd));
  }
}

// =============================================================================

std::string GetTime()
{
  std::string res;

  timeval tv;
  int succ = gettimeofday(&tv, nullptr);
  if (succ == 0)
  {
    time_t tt;
    tm* nowtm;

    tt    = tv.tv_sec;
    nowtm = localtime(&tt);

    char tmBuf[16];
    strftime(tmBuf, sizeof(tmBuf), "%H:%M:%S", nowtm);
    res = { tmBuf };
  }

  return res;
}

// =============================================================================

std::string IpToString(const uint32_t& addr)
{
  std::stringstream ss;

  uint32_t value1 = addr & Mask1;
  uint32_t value2 = (addr & Mask2) >> 8;
  uint32_t value3 = (addr & Mask3) >> 16;
  uint32_t value4 = (addr & Mask4) >> 24;

  ss << std::to_string(value1) << "."
     << std::to_string(value2) << "."
     << std::to_string(value3) << "."
     << std::to_string(value4);

  return ss.str();
}

// =============================================================================

std::string CreateMessage(int whoFd, const std::string& msg)
{
  if (ClientInfoByFd.count(whoFd) == 0)
  {
    return std::string();
  }

  ClientInfo ci = ClientInfoByFd[whoFd];

  std::stringstream ss;

  std::string timestamp = StringFormat("[%s]  ", GetTime().data());
  std::string sender    = StringFormat("%s (%i)", IpToString(ci.Ip).data(), whoFd);

  sender.insert(sender.end(), 22 - sender.length(), ' ');

  ss << timestamp << sender << " | " << msg;

  return ss.str();
}

// =============================================================================

std::string GetOnlineUsers()
{
  std::stringstream ss;

  //
  // Need to distinguish this service message from chat message.
  //
  // Best way would probably be to use some text-based protocol with JSON,
  // but for testing purposes this will do.
  //
  ss << (uint8_t)0x07;

  for (auto& kvp : ClientInfoByFd)
  {
    ss << IpToString(kvp.second.Ip) << "/" << std::to_string(kvp.second.Fd) << ";";
  }

  return ss.str();
}

// =============================================================================

void SendOnlineUsers(int toWho)
{
  std::string onlineUsers = GetOnlineUsers();
  send(toWho, onlineUsers.data(), onlineUsers.length(), MSG_NOSIGNAL);
}

// =============================================================================

const std::vector<std::string> Greeting =
{
  R"(/=====================\)",
  R"(|                     |)",
  R"(|       WELCOME       |)",
  R"(|                     |)",
  R"(\=====================/)"
};

void SendGreeting(int toWho)
{
  std::string timestamp = StringFormat("[%s]  ", GetTime().data());
  std::string server    = "SERVER";
  server.insert(server.end(), 22 - server.length(), ' ');

  for (size_t i = 0; i < Greeting.size(); i++)
  {
    std::stringstream ss;

    ss << timestamp << server << " | " << Greeting[i];

    std::string str = ss.str();

    int succ = send(toWho,
                    str.data(),
                    str.length(),
                    MSG_NOSIGNAL);

    //
    // If for some reason we can't send, beat it.
    //
    if (succ == -1)
    {
      break;
    }
  }
}

// =============================================================================

void SendMulticast(const std::string& msg, int fdToExclude = -1)
{
  for (auto& kvp : ClientInfoByFd)
  {
    if (fdToExclude != -1 and (kvp.first == fdToExclude))
    {
      continue;
    }

    send(kvp.first, msg.data(), msg.length(), MSG_NOSIGNAL);
  }
}

// =============================================================================

bool IsRunning = true;

void SignalHandler(int sigNum)
{
  IsRunning = false;
  printf("Caught SIGINT!\n");
}

// =============================================================================

int main(int argc, char* argv[])
{
  if (argc < 2)
  {
    printf("Usage: %s <PORT>\n", argv[0]);
    return 0;
  }

  std::string portS = { argv[1] };

  for (auto& c : portS)
  {
    CheckError((not std::isdigit(c)), "Invalid port number!");
  }

  uint16_t port = std::stoul(portS);

  int masterSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  CheckError((masterSocket == -1), "Couldn't create socket!");

  //
  // Protects against "address already in use" error
  // after trying to restart the server on the same port
  // in case of premature termination.
  //
  int optval = 1;
  int succ = setsockopt(masterSocket,
                        SOL_SOCKET,
                        SO_REUSEADDR,
                        &optval,
                        sizeof(optval));
  CheckError((succ == -1), "setsockopt() failed!");

  sockaddr_in sa;
  sa.sin_family      = AF_INET;
  sa.sin_port        = htons(port);
  sa.sin_addr.s_addr = htonl(INADDR_ANY);

  succ = bind(masterSocket, (sockaddr*)(&sa), sizeof(sa));
  CheckError((succ == -1), "Couldn't bind!");

  //
  // By default all sockets are created as "blocking",
  // which means program will block on send / recv calls
  // until all data has been send / recv'd.
  //
  SetNonblock(masterSocket);

  succ = listen(masterSocket, SOMAXCONN);
  CheckError((succ == -1), "listen() failed!");

  //
  // Create handler to OS kernel for epoll().
  // Basically this allows us to query kernel
  // for various information as well as set different attributes.
  //
  int epollFd = epoll_create1(0);
  CheckError((epollFd == -1), "Couldn't create epoll fd!\n");

  //
  // Create special structure instance
  // and fill it with data we want to monitor.
  //
  // In this case we want to monitor
  // server's connections listener socket
  // on events of "input" data available in it
  // (meaning there is an incoming connection).
  //
  // Then register this instance in OS kernel using epoll_ctl().
  //
  epoll_event masterSocketEvent;

  masterSocketEvent.data.fd = masterSocket;
  masterSocketEvent.events  = EPOLLIN;

  succ = epoll_ctl(epollFd,
                   EPOLL_CTL_ADD,
                   masterSocket,
                   &masterSocketEvent);
  CheckError((succ == -1), "Failed to register epoll event!");

  signal(SIGINT, SignalHandler);

  //
  // Declare maximum number of possible clients basically.
  // Right now it's empty, but will be filled
  // every time epoll_wait() returns.
  //
  epoll_event events[MaxEvents];

  while (IsRunning)
  {
    //
    // Check if there are any events we subscribed on at epollFd.
    //
    // We pass events array which will be filled with events data
    // up to MaxEvents, but may be less, exact value of which
    // is returned from epoll_wait().
    //
    // Last parameter is timeout in ms,
    // which we may specify in order to allow CPU some rest.
    //
    // -1 means block indefinitely, specifying some other value
    // will make epoll_wait() return immediately if timeout expires
    // (I guess with n = 0).
    //
    int n = epoll_wait(epollFd, events, MaxEvents, -1);

    for (int i = 0; i < n; i++)
    {
      //
      // If something happened, we must check all received
      // file descriptors, and if it's a masterSocket,
      // this means we have a new client.
      //
      // New client gets his own socket,
      // which is set to non-blocking,
      // and registered with epollFd for subsequent monitoring,
      // just like we did with masterSocket above.
      //
      if (events[i].data.fd == masterSocket)
      {
        sockaddr_in clientInfo;
        socklen_t clientInfoSize = sizeof(clientInfo);

        int client = accept(masterSocket,
                            (sockaddr*)&clientInfo,
                            &clientInfoSize);
        CheckError((client == -1), "accept() failed!");

        SetNonblock(client);

        SendGreeting(client);

        std::string clientIp   = IpToString(clientInfo.sin_addr.s_addr);
        ClientInfoByFd[client] = { clientInfo.sin_addr.s_addr, client };

        SendMulticast(GetOnlineUsers());

        std::string userConnectedMsg = StringFormat("[%s]  SERVER | %s (%i) connected",
                                                    GetTime().data(),
                                                    clientIp.data(),
                                                    client);

        printf("%s\n", userConnectedMsg.data());

        SendMulticast(userConnectedMsg, client);

        epoll_event evt;
        evt.data.fd = client;
        evt.events = EPOLLIN;

        succ = epoll_ctl(epollFd, EPOLL_CTL_ADD, client, &evt);
        CheckError((succ == -1), "Failed to register epoll event!");
      }
      else
      {
        //
        // If this is not a masterSocket,
        // this means there is data to be read.
        //
        // Which is exactly what we'll do.
        //
        static char buffer[MessageBufferSize];
        int res = recv(events[i].data.fd,
                       buffer,
                       MessageBufferSize,
                       MSG_NOSIGNAL);

        //
        // This condition pair means
        // that connection doesn't exist anymore.
        //
        if (res == 0 && (errno != EAGAIN))
        {
          std::string userDisconnectedMsg = StringFormat("[%s]  SERVER | %s (%i) disconnected",
                                                         GetTime().data(),
                                                         IpToString(ClientInfoByFd[events[i].data.fd].Ip).data(),
                                                         events[i].data.fd);

          printf("%s\n", userDisconnectedMsg.data());

          ClientInfoByFd.erase(events[i].data.fd);

          SendMulticast(userDisconnectedMsg);
          SendMulticast(GetOnlineUsers());

          succ = epoll_ctl(epollFd,
                           EPOLL_CTL_DEL,
                           events[i].data.fd,
                           nullptr);
          CheckError((succ == -1), "Failed to delete fd from epoll!");

          CloseConnection(events[i].data.fd);
        }
        else if (res > 0)
        {
          std::string msg(buffer, res);

          std::string chatMessage = CreateMessage(events[i].data.fd, msg);

          SendMulticast(chatMessage);
        }
      }
    }
  }

  CloseConnection(masterSocket, false);

  return 0;
}
