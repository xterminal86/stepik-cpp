#include "printer.h"

#include <sstream>
#include <vector>
#include <thread>
#include <cstring>
#include <atomic>

#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <signal.h>
#include <time.h>

#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>

std::vector<std::string> Messages;
std::vector<std::string> OnlineUsers;

struct ServerData
{
  std::string Address;
  uint16_t    Port;
};

std::atomic<bool> MessageReady{false};
std::atomic<bool> IsRunning{true};
std::atomic<bool> ServerOnline{true};

std::string TypedMessage;

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

void CloseConnection(int fd)
{
  int succ = shutdown(fd, SHUT_RDWR);
  CheckError((succ == -1), "shutdown() failed!");
  succ = close(fd);
  CheckError((succ == -1), "close() failed!");
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

void GraphicsThread()
{
  TG::Printer printer;

  initscr();
  nodelay(stdscr, true);
  keypad(stdscr, true);
  noecho();
  curs_set(false);

  start_color();

  printer.Init();

  int tw = printer.TerminalWidth();
  int th = printer.TerminalHeight();

  int twHalf = tw / 2;
  int twHH   = twHalf / 2;
  int twQ    = twHalf / 4;

  int thHalf = th / 2;
  //int thHH   = thHalf / 2;
  int thQ    = thHalf / 4;

  Messages.reserve(th);

  int ch;

  std::string charsPressed;

  while (IsRunning.load())
  {
    ch = getch();

    if (ServerOnline.load())
    {
      switch (ch)
      {
        case '\n':
        {
          if (not charsPressed.empty())
          {
            MessageReady = true;
            TypedMessage = charsPressed;
            charsPressed.clear();
          }
        }
        break;

        case KEY_BACKSPACE:
        case 127:
        case 8:
        {
          if (not charsPressed.empty())
          {
            charsPressed.pop_back();
          }
        }
        break;

        default:
        {
          if (ch >= 32 and ch <= 127)
          {
            charsPressed.push_back(ch);
          }
        }
        break;
      }
    }
    else
    {
      if (ch == 'q')
      {
        break;
      }
    }

    printer.Clear();

    //
    // Chat window
    //
    printer.DrawWindow({ 0,   0 },
                       { twHalf + twHH, th - 1 },
                       " LOBBY ",
                       TG::Colors::White,
                       TG::Colors::Blue,
                       TG::Colors::White,
                       TG::Colors::ShadesOfGrey::Four,
                       TG::Colors::Black);

    //
    // Clients
    //
    printer.DrawWindow({ twHalf + twHH, 0 },
                       { tw - (twHalf + twHH) - 1, th - 1 },
                       " CLIENTS ",
                       TG::Colors::White,
                       TG::Colors::Blue,
                       TG::Colors::White,
                       TG::Colors::ShadesOfGrey::Four,
                       TG::Colors::Black);

    uint8_t lineIndex = 0;
    for (auto& user : OnlineUsers)
    {
      printer.PrintFB(twHalf + twHH + 2,
                      2 + lineIndex,
                      user,
                      TG::Printer::kAlignLeft,
                      TG::Colors::White);
      lineIndex++;
    }

    lineIndex = 0;

    //
    // Message window
    //
    printer.DrawWindow({ 0, th - 5 },
                       { twHalf + twHH, 4 },
                       " YOUR MESSAGE ",
                       TG::Colors::White,
                       TG::Colors::Blue,
                       TG::Colors::White,
                       TG::Colors::ShadesOfGrey::Four,
                       TG::Colors::Black);

    //
    // Dispay chat messages
    //
    for (auto& m : Messages)
    {
      printer.PrintFB(1,
                      1 + lineIndex,
                      m,
                      TG::Printer::kAlignLeft,
                      TG::Colors::White);
      lineIndex++;
    }

    lineIndex = 0;

    //
    // Cursor
    //
    printer.PrintFB(1,
                    th - 3,
                    "$ ",
                    TG::Printer::kAlignLeft,
                    TG::Colors::White);

    printer.PrintFB(3 + charsPressed.length(),
                    th - 3,
                    ' ',
                    TG::Colors::Black,
                    TG::Colors::White);

    //
    // Message display
    //
    if (not charsPressed.empty())
    {
      printer.PrintFB(3,
                      th - 3,
                      charsPressed,
                      TG::Printer::kAlignLeft,
                      TG::Colors::White);
    }

    if (not ServerOnline.load())
    {
      printer.DrawWindow({ twHalf - twQ, thHalf - thQ },
                         { 2 * twQ, 2 * thQ },
                       " SERVER OFFLINE ",
                       TG::Colors::White,
                       TG::Colors::Red,
                       TG::Colors::White,
                       0x440000,
                       TG::Colors::Black);

      printer.PrintFB(twHalf,
                      thHalf,
                      "Press 'q' to exit",
                      TG::Printer::kAlignCenter,
                      TG::Colors::White);
    }

    printer.Render();
  }

  endwin();

  printf("graphics thread returning\n");
}

// =============================================================================

void ParseOnlineUsers(const std::string& msg)
{
  OnlineUsers.clear();

  std::stringstream ss;

  for (auto& c : msg)
  {
    switch (c)
    {
      case ';':
      {
        ss << ")";
        OnlineUsers.push_back(ss.str());
        ss.str(std::string());
      }
      break;

      case '/':
      {
        ss << " (";
      }
      break;

      default:
      {
        if (c != 0x07)
        {
          ss << c;
        }
      }
      break;
    }
  }
}

// =============================================================================

void NetworkThread(ServerData sd)
{
  const uint32_t MessageSize = 1024;

  sockaddr_in sa;
  sa.sin_family = AF_INET;
  sa.sin_port   = htons(sd.Port);

  int succ = inet_pton(AF_INET, sd.Address.data(), &sa.sin_addr);
  CheckError((succ != 1), "inet_pton() failed!");

  int s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  CheckError((s == -1), "socket() failed!");

  succ = connect(s, (sockaddr*)&sa, sizeof(sa));
  CheckError((succ == -1), "connect() failed!");

  SetNonblock(s);

  char buffer[MessageSize];

  while (IsRunning.load())
  {
    int n = recv(s, buffer, MessageSize, MSG_NOSIGNAL);
    if (n == 0 and (errno != EAGAIN))
    {
      ServerOnline = false;
      break;
    }
    else if (n > 0)
    {
      std::string msg(buffer, n);
      if (not msg.empty())
      {
        if (msg[0] == 0x07)
        {
          ParseOnlineUsers(msg);
        }
        else
        {
          Messages.push_back(msg);
        }
      }
    }

    if (MessageReady.load())
    {
      succ = send(s,
                  TypedMessage.data(),
                  TypedMessage.length(),
                  MSG_NOSIGNAL);

      if (succ == -1 && (errno != EAGAIN))
      {
        break;
      }
      else if (succ > 0)
      {
        TypedMessage.clear();
        MessageReady = false;
      }
    }
  }

  CloseConnection(s);

  printf("network thread returning\n");
}

// =============================================================================

void SigHandler(int sigNum)
{
  IsRunning = false;
}

// =============================================================================

int main(int argc, char* argv[])
{
  if (argc < 2)
  {
    printf("Usage: %s <IP> <PORT>\n", argv[0]);
    return 0;
  }

  std::string ipS   = { argv[1] };
  std::string portS = { argv[2] };

  for (auto& c : portS)
  {
    CheckError((not std::isdigit(c)), "Invalid port number!");
  }

  uint16_t port = std::stoul(portS);

  signal(SIGINT, SigHandler);

  ServerData sd = { ipS, port };

  std::thread graphicsThread(GraphicsThread);
  std::thread networkThread(NetworkThread, sd);

  networkThread.join();
  graphicsThread.join();

  return 0;
}
