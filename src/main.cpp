#include <getopt.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

#include <cassert>
#include <iostream>
#include <memory>
#include <vector>

#define CSYNC_VER_MAJOR 0
#define CSYNC_VER_MINOR 1
#define CSYNC_VER_PATCH 0

using FILEDeleter = int (*)(FILE *);
using fileptr = std::unique_ptr<FILE, FILEDeleter>;

char *in = nullptr, *out = nullptr;

void version() {
  std::cout << "csync version " << CSYNC_VER_MAJOR << "." << CSYNC_VER_MINOR
            << std::endl;
  exit(0);
}

void usage() {
  std::cout << "Usage: csync [options] -i <input> -o <output>\n\n"
            << "Options:\n"
            << "\t-h, --help:\tshow this message\n"
            << "\t-v, --version:\tshow csync version\n\n"
            << "Arguments:\n"
            << "\tinput:\t\tpath to a input CD-ROM file\n"
            << "\toutput:\t\tpath to a output filesystem" << std::endl;
  exit(0);
}

/// @brief Parses the supplied arguments to the program.
/// @param argc Number of the arguments.
/// @param argv Supplied arguments in `char*`
/// @return `0` if parsing was successful or `1`.
int get_opt(int argc, char *argv[]) {
  int opt;
  int index = 0;

  static struct option opts[] = {
      {"help", no_argument, 0, 'h'},
      {"version", no_argument, 0, 'v'},
      {"input", required_argument, 0, 'i'},
      {"output", required_argument, 0, 'o'},
      {0, 0, 0, 0},
  };

  while ((opt = getopt_long(argc, argv, "hvi:o:", opts, &index)) != -1) {
    switch (opt) {
      case 'h':
        usage();
        break;
      case 'v':
        version();
        break;
      case 'i':
        in = optarg;
        break;
      case 'o':
        out = optarg;
        break;
    }
  }

  return 0;
}

/// @brief Executes the given command
/// @param cmd Command to execute
/// @return `pipe` connected to the running command.
[[deprecated("replaced by `exec_cmd_secure()`")]]
fileptr exec_cmd(const std::string &cmd) {
  fileptr pipe(popen(cmd.c_str(), "r"), pclose);
  return pipe;
}

/// @brief Reads all outputs from the given `pipe`.
/// @param pipe `pipe` connected to the running command.
/// @param flush if `true`, `pipe` will flush its outputs on the terminal.
/// @return whole output of the command.
[[deprecated("replaced by `exec_cmd_secure()`")]]
std::string read_pipe(FILE *pipe, bool flush) {
  std::string res;

  std::vector<char> buffer(128);

  while (fgets(buffer.data(), 128, pipe) != nullptr) {
    if (flush) {
      std::cout << buffer.data() << std::endl;
    }
    res += buffer.data();
  }

  return res;
}

/// @brief Securely executes the given command.
/// @param args Arguments that will be passed to the process.
/// @param buffer Buffer for contain outputs of the process.
/// @param flush if `true`, buffer will flush its outputs on the terminal.
/// @return `0` if execution was successful or `1`.
int exec_cmd_secure(const std::vector<std::string> &args, std::string &buffer,
                    bool flush) {
  if (args.empty()) {
    return -1;
  }

  std::vector<char *> argv;
  for (const auto &arg : args) {
    argv.push_back(const_cast<char *>(arg.c_str()));
  }
  argv.push_back(nullptr);

  int pipefd[2];
  if (pipe(pipefd) == -1) {
    perror("error: failed to create pipeline: ");
    return -1;
  }

  pid_t pid = fork();
  if (pid == -1) {
    perror("error: failed to create new process: ");
    return -1;
  }

  if (pid == 0) {
    close(pipefd[0]);

    dup2(pipefd[1], STDOUT_FILENO);
    dup2(pipefd[1], STDERR_FILENO);

    close(pipefd[1]);
    execvp(argv[0], argv.data());

    _exit(1);
  }

  close(pipefd[1]);

  ssize_t bytes;

  std::vector<char> pipebuf(128);
  while ((bytes = read(pipefd[0], pipebuf.data(), pipebuf.size())) > 0) {
    if (flush) {
      std::cout.write(pipebuf.data(), bytes);
      std::cout.flush();
    }
    buffer.append(pipebuf.data(), bytes);
  }

  close(pipefd[0]);
  int status;
  waitpid(pid, &status, 0);

  int exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
  return exit_code;
}

/// @brief Checks whether the target device is mounted on system.
/// @param target Absolute path to the device.
/// @return `1` if mounted or `0`
bool is_mounted(const std::string &target) {
  std::vector<std::string> findmnt_args = {
      "findmnt", "-n", "-o", "TARGET", "--source", target,
  };

  std::string piped;
  // ignoreing exit_status
  exec_cmd_secure(findmnt_args, piped, 0);

  piped.erase(piped.find_last_not_of(" \n\r\t") + 1);

  return !piped.empty();
}

/// @brief  Executes `file` command with given file.
/// @param target Path to the file.
/// @return Metadata of the target in `std::string`.
std::string get_metadata(const std::string &target) {
  std::cout << "info: extracting target '\x1b[4m" << target
            << "\x1b[0m' metadata..." << std::endl;

  std::vector<std::string> file_args = {
      "file",
      target,
  };

  std::string piped;
  // ignoring exit_status
  exec_cmd_secure(file_args, piped, 0);

  return piped;
}

/// @brief Checks if given target has `ISO 9660 CD-ROM` metadata.
/// @param target Path to the target
/// @return `true` if the target has it or `false`.
bool is_cd_rom(const std::string &target) noexcept {
  std::cout << "info: checking file metadata..." << std::endl;
  std::string metadata = get_metadata(target);

  const std::string ISO_9660_IDENT = "ISO 9660 CD-ROM";
  const std::string BOOTABLE_IDENT = "(bootable)";
  const std::size_t NPOS = std::string::npos;

  return (metadata.find(ISO_9660_IDENT) != NPOS &&
          metadata.find(BOOTABLE_IDENT) != NPOS);
}

/// @brief Asks user for confirmation.
/// @param target Path to the target filesystem.
/// @return `true` if user confirms or `false`.
bool confirm_dump(const std::string &target) {
  std::cout << "\x1b[31mWARNING\x1b[0m: "
            << "destination disk '\x1b[4m" << target
            << "\x1b[0m' will be wiped. "
            << "proceed? [y/N] " << std::flush;

  termios old_term, new_term;
  tcgetattr(STDIN_FILENO, &old_term);

  new_term = old_term;
  new_term.c_lflag &= ~ECHO;
  tcsetattr(STDIN_FILENO, TCSANOW, &new_term);

  char c = getchar();

  tcsetattr(STDIN_FILENO, TCSANOW, &old_term);
  std::cout << std::endl;

  return (c == 'y' || c == 'Y');
}

/// @brief Dumps disk using `dd` command.
/// @param src Path to input `CD-ROM` file.
/// @param dst Path to output filesystem.
/// @return `0` if Dumping was successful or `1`.
int dump_disk(const std::string &src, const std::string &dst) {
  if (!is_cd_rom(src)) {
    std::cerr << "error: source filesystem '\x1b[4m" << src
              << "\x1b[0m' does not have ISO 9660 CD-ROM signature."
              << std::endl;
    return 1;
  }

  if (is_mounted(dst)) {
    std::cerr << "error: destination '\x1b[4m" << dst
              << "\x1b[0m' is mounted on the system..." << std::endl;
    std::cerr
        << "hint: it is required to unmount them first for data integrity."
        << std::endl;
    return 1;
  }

  if (!confirm_dump(dst)) {
    std::cout << "info: canceled by user." << std::endl;
    exit(0);
  }

  std::cout << "info: initiating dumping..." << std::endl;

  // reference: https://wiki.archlinux.org/title/USB_flash_installation_medium
  std::vector<std::string> dd_args = {
      "sudo",  "dd",           "if=" + src,  "of=" + dst,
      "bs=4M", "oflag=direct", "conv=fsync", "status=progress",
  };

  std::string piped;
  int exit_code = exec_cmd_secure(dd_args, piped, 1);

  return exit_code;
}

int main(int argc, char *argv[]) {
  get_opt(argc, argv);

  if (in != nullptr && out != nullptr) {
    std::cout << "info: source filesystem set: '\x1b[4m" << in << "\x1b[0m'.\n";
    std::cout << "info: destination filesystem set: '\x1b[4m" << out
              << "\x1b[0m'.";
    std::cout << std::endl;

    int exit_status = dump_disk(in, out);

    if (exit_status == 0) {
      std::cout << "info: Done." << std::endl;
    } else {
      std::cout << "error: failed to dump disk to destination." << std::endl;
    }
    exit(exit_status);
  }

  std::cerr << "error: source or destination is not set; exiting..."
            << std::endl;
  return 1;
}
