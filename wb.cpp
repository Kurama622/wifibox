#include <functional>
#include <mutex>
#include <ncurses.h>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

typedef std::vector<std::string> shell_r;

constexpr const char *CLEAR = "\tclear...";
constexpr const char *PROGRESS = "\t...";
constexpr const char *DEFAULT = "";
constexpr const char *CONNECTED = "\t(*)";
constexpr const char *FAILED = "\t(failed)";

shell_r COMMAND(const std::vector<std::string> &args,
                const std::function<bool(const std::string &, const shell_r &)>
                    f = nullptr) {
  std::stringstream cmd;
  for (auto &arg : args) {
    cmd << arg << " ";
  }

  cmd << "2> /dev/null";
  shell_r result = {};
  FILE *fp = popen(cmd.str().c_str(), "r");
  if (!fp) {
    return result;
  }

  char buf[256];
  try {
    while (fgets(buf, sizeof(buf), fp)) {
      std::string line(buf);

      if (!line.empty()) {
        line.erase(line.find_last_not_of("\n") + 1);
        if (!line.empty()) {
          if ((f && f(line, result)) || !f) {
            result.push_back(line);
          }
        }
      }
    }
  } catch (...) {
    pclose(fp);
    return result;
  }
  pclose(fp);
  return result;
}

void get_password(WINDOW *win, int starty, int startx, char *password,
                  int max_len) {
  int pos = 0;
  int ch;
  wmove(win, starty, startx);
  wrefresh(win);

  while ((ch = wgetch(win)) != '\n' && ch != '\r') {
    if (ch == KEY_BACKSPACE || ch == 127 || ch == '\b') {
      if (pos > 0) {
        pos--;
        password[pos] = '\0';
        // backspace (delete)
        int cur_y, cur_x;
        getyx(win, cur_y, cur_x);
        mvwaddch(win, cur_y, cur_x - 1, ' ');
        wmove(win, cur_y, cur_x - 1);
        wrefresh(win);
      }
    } else if (pos < max_len - 1 && ch >= 32 && ch <= 126) {
      password[pos++] = ch;

      // show *
      waddch(win, '*');
      wrefresh(win);
    }
  }
  password[pos] = '\0';
}

int main() {
  initscr();
  int rows, cols;
  getmaxyx(stdscr, rows, cols);
  noecho();
  cbreak();
  keypad(stdscr, TRUE);
  nodelay(stdscr, TRUE);
  curs_set(0);

  if (has_colors()) {
    start_color();
    init_pair(1, COLOR_YELLOW, COLOR_BLACK); // Title
    init_pair(2, COLOR_CYAN, COLOR_BLACK);   // Notify
    init_pair(3, COLOR_GREEN, COLOR_BLACK);  // Selected
    init_pair(4, COLOR_WHITE, COLOR_BLACK);  // Text
  }

  int height = 20, width = 50;
  int starty = (rows - height) / 2, startx = (cols - width) / 2;

  WINDOW *win = newwin(height, width, starty, startx);
  const std::string title_str = " WiFi Manager ";

  std::vector<std::string> wifi = {"Scanning..."};
  std::vector<std::string> memorized = {};
  std::vector<std::string> wifi_status;
  std::mutex mtx;

  std::thread scan_thread = std::thread([&wifi, &mtx]() {
    std::lock_guard<std::mutex> lock(mtx);
    wifi = COMMAND({"nmcli", "-t", "-f", "active,SSID", "dev", "wifi", "list"},
                   [](const std::string &line, const shell_r &result) {
                     for (auto &item : result) {
                       if (item == line) {
                         return false;
                       }
                     }
                     if (line == "yes:" || line == "no:")
                       return false;
                     return true;
                   });
  });

  std::thread status_thread = std::thread([&memorized, &mtx]() {
    std::lock_guard<std::mutex> lock(mtx);
    memorized = COMMAND({"nmcli", "-t", "-f", "NAME", "connection", "show"});
  });
  std::thread render_thread = std::thread([&]() {
    int highlight = 0;
    bool initialize = true;
    while (true) {
      werase(win);
      box(win, 0, 0);
      // set title
      wattron(win, COLOR_PAIR(1));
      mvwprintw(win, 0, (width - title_str.size()) / 2, "%s",
                title_str.c_str());
      wattroff(win, COLOR_PAIR(1));

      if (wifi.size() == 1 && wifi[0] == "Scanning...") {
        wattron(win, COLOR_PAIR(2));
        mvwprintw(win, 2, 2, "%s", wifi[0].c_str());
        wattroff(win, COLOR_PAIR(2));
        wrefresh(win);
      } else {
        wifi_status.resize(wifi.size());
        for (int i = 0; i < wifi.size(); ++i) {
          if (initialize) {
            if (wifi[i].find("yes:") != std::string::npos) {
              wifi[i] = wifi[i].replace(0, 4, "");
              wifi_status[i] = CONNECTED;
              highlight = i;
            } else {
              wifi[i] = wifi[i].replace(0, 3, "");
              wifi_status[i] = DEFAULT;
            }
          }

          if (i == highlight) {
            wattron(win, COLOR_PAIR(3));
            mvwprintw(win, i + 2, 2, "%s", (wifi[i] + wifi_status[i]).c_str());
            wattroff(win, COLOR_PAIR(3));
          } else {
            wattron(win, COLOR_PAIR(4));
            mvwprintw(win, i + 2, 2, "%s", (wifi[i] + wifi_status[i]).c_str());
            wattroff(win, COLOR_PAIR(4));
          }
        }
        wrefresh(win);

        initialize = false;
        int ch = getch();
        if (ch == 'q')
          break;
        else if (ch == 'j')
          highlight = (highlight + 1) % wifi.size();
        else if (ch == 'k')
          highlight = (highlight - 1 + wifi.size()) % wifi.size();
        else if (ch == KEY_BACKSPACE) {
          wifi_status[highlight] = CLEAR;
          std::thread clear_thread = std::thread([&]() {
            std::lock_guard<std::mutex> lock(mtx);
            shell_r res = COMMAND(
                {"nmcli", "connection", "delete", "'" + wifi[highlight] + "'"});

            if (res.size() > 0 &&
                res[0].find("successfully") != std::string::npos) {
              wifi_status[highlight] = DEFAULT;
              for (auto i = memorized.begin(); i != memorized.end();) {
                if (*i == wifi[highlight]) {
                  i = memorized.erase(i);
                } else {
                  ++i;
                }
              }
            } else {
              wifi_status[highlight] = FAILED;
            }
          });
          clear_thread.join();
        } else if (ch == '\n') {
          bool connected = false;

          for (auto &ssid : memorized) {
            if (wifi[highlight] == ssid) {
              for (int i = 0; i < wifi.size(); i++) {
                wifi_status[i] = DEFAULT;
              }

              wifi_status[highlight] = PROGRESS;
              std::thread connect_thread = std::thread([&]() {
                std::lock_guard<std::mutex> lock(mtx);
                shell_r res = COMMAND(
                    {"nmcli", "device", "wifi", "connect", "'" + ssid + "'"});

                if (res.size() > 0 &&
                    res[0].find("successfully") != std::string::npos) {
                  wifi_status[highlight] = CONNECTED;
                  connected = true;
                } else {
                  wifi_status[highlight] = FAILED;
                }
              });
              connect_thread.join();
              break;
            }
          }
          if (!connected) {
            char password[128];
            std::string ssid = wifi[highlight];
            wattron(win, COLOR_PAIR(2));
            mvwprintw(win, wifi.size() + 3, 2, "Password: ");
            wattroff(win, COLOR_PAIR(2));
            wrefresh(win);
            get_password(win, wifi.size() + 3, 12, password, sizeof(password));
            for (int i = 0; i < wifi.size(); i++) {
              wifi_status[i] = DEFAULT;
            }
            wifi_status[highlight] = PROGRESS;
            std::thread connect_thread = std::thread([&]() {
              shell_r res =
                  COMMAND({"nmcli", "dev", "wifi", "connect", "'" + ssid + "'",
                           "password", "'" + std::string(password) + "'"});

              if (res.size() > 0 &&
                  res[0].find("successfully") != std::string::npos) {
                wifi_status[highlight] = CONNECTED;
                memorized.push_back(ssid);
                connected = true;
              } else {
                wifi_status[highlight] = FAILED;
              }
            });
            connect_thread.join();
          }
        }
      }
    }
  });

  std::vector<std::thread> jobs;
  jobs.emplace_back(std::move(scan_thread));
  jobs.emplace_back(std::move(status_thread));
  jobs.emplace_back(std::move(render_thread));

  for (auto &job : jobs) {
    job.join();
  }

  delwin(win);
  endwin();
  return 0;
}
