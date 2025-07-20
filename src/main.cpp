#include <algorithm>
#include <cassert>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>

#include <fcntl.h>
#include <getopt.h>
#include <unistd.h>

#define CSYNC_VER_MAJOR 0
#define CSYNC_VER_MINOR 1
#define CSYNC_VER_PATCH 0

using FILEDeleter = int (*)(FILE *);
using pipe_ptr = std::unique_ptr<FILE, FILEDeleter>;

char *in = nullptr, *out = nullptr;

struct opts
{
  std::string src, dst, bs, oflag, conv, count, status;

  std::string cmd()
  {
    std::string cmd = "sudo dd";
    cmd += " if=" + this->src;
    cmd += " of=" + this->dst;
    cmd += " bs=" + this->bs;
    cmd += " count=" + this->count;
    cmd += " conv=" + this->conv;
    cmd += " oflag=" + this->oflag;
    cmd += " status=" + this->status;
    cmd += " 2>&1";

    return cmd;
  }
};

void version()
{
  std::cout << "csync version " << CSYNC_VER_MAJOR << "." << CSYNC_VER_MINOR << std::endl;
  exit(0);
}

void usage()
{
  std::cout << "Usage: csync [options] -i <input> -o <output>\n\n"
            << "Options:\n"
            << "\t-h, --help:\tshow this message\n"
            << "\t-v, --version:\tshow csync version\n\n"
            << "Arguments:\n"
            << "\tinput:\t\tpath to a input CD-ROM file.\n"
            << "\toutput:\t\tpath to a output filesystem" << std::endl;
  exit(0);
}

/// @brief Parses the supplied arguments to the program.
/// @param argc Number of the arguments.
/// @param argv Supplied arguments in `char*`
/// @return `0` if parsing was successful or `1`.
int get_opt(int argc, char *argv[])
{
  int opt;
  int index = 0;

  static struct option opts[] = {
      {"help", no_argument, 0, 'h'},
      {"version", no_argument, 0, 'v'},
      {"input", required_argument, 0, 'i'},
      {"output", required_argument, 0, 'o'},
      {0, 0, 0, 0},
  };

  while ((opt = getopt_long(argc, argv, "hvi:o:", opts, &index)) != -1)
  {
    switch (opt)
    {
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
pipe_ptr exec_cmd(const std::string &cmd)
{
  pipe_ptr pipe(popen(cmd.c_str(), "r"), pclose);
  return pipe;
}

/// @brief Reads all outputs from the given `pipe`.
/// @param pipe `pipe` connected to the running command.
/// @param flush if `true`, `pipe` will flush its outputs on the terminal.
/// @return whole output of the command.
std::string read_pipe(FILE *pipe, bool flush)
{
  std::string res;

  std::vector<char> buffer(128);

  while (fgets(buffer.data(), 128, pipe) != nullptr)
  {
    if (flush)
    {
      std::cout << buffer.data() << std::endl;
    }
    res += buffer.data();
  }

  return res;
}

/// @brief Checks whether the target device is mounted on system.
/// @param target Absolute path to the device.
/// @return `1` if mounted or `0`
bool is_mounted(const std::string &target)
{
  const std::string cmd = "findmnt -n -o TARGET --source " + target + " 2>/dev/null";

  pipe_ptr pipe = exec_cmd(cmd);

  if (pipe == nullptr)
    return false;

  std::string piped = read_pipe(pipe.get(), false);
  piped.erase(piped.find_last_not_of(" \n\r\t") + 1);
  return !piped.empty();
}

/// @brief Tries to unmount the target device using `umount` command.
/// @param target Absolute path to the mounted device.
/// @return `0` if unmounting was successful or `1`
int unmount_fs(const std::string &target)
{
  std::string umount = "sudo umount " + target;
  std::cout << "info: unmounting device: '\x1b[4m" << target << "\x1b[0m'... ";
  pipe_ptr pipe_umount = exec_cmd(umount);

  if (pipe_umount == nullptr)
  {
    std::cerr << "error: failed to unmount the device: '\x1b[4m" << target << "\x1b[0m." << std::endl;
    std::cerr << "error: command `umount` failed during the execution" << std::endl;
    return 1;
  }

  return 0;
}

/// @brief  Executes `file` unix command with given file.
/// @param target Path to the file.
/// @return Metadata of the target in `std::string`.
std::string get_metadata(const std::string &target)
{
  std::cout << "info: extracting target '\x1b[4m" << target << "\x1b[0m' metadata..." << std::endl;
  const std::string cmd = "file " + target;
  pipe_ptr pipe = exec_cmd(cmd);

  std::string piped = read_pipe(pipe.get(), 0);
  return piped;
}

/// @brief Checks if given target has `ISO 9660 CD-ROM` metadata.
/// @param target Path to the target
/// @return `true` if the target has it or `false`.
bool is_cd_rom(const std::string &target) noexcept
{
  std::cout << "info: checking file metadata..." << std::endl;
  std::string metadata = get_metadata(target);

  const std::string ISO_9660_IDENT = "ISO 9660 CD-ROM";
  const std::string BOOTABLE_IDENT = "(bootable)";
  const std::size_t NPOS = std::string::npos;

  return (metadata.find(ISO_9660_IDENT) != NPOS && metadata.find(BOOTABLE_IDENT) != NPOS);
}

/// @brief Dumps disk using `dd` UNIX command.
/// @param src Path to input `CD-ROM` file.
/// @param dst Path to output filesystem.
/// @return `0` if Dumping was successful or `1`.
int dump_disk(const std::string &src, const std::string &dst)
{
  if (!is_cd_rom(src))
  {
    std::cerr << "error: source filesystem '\x1b[4m" << src << "\x1b[0m' does not have ISO 9660 CD-ROM signature."
              << std::endl;
    return 1;
  }

  if (is_mounted(dst))
  {
    std::cerr << "error: destination '\x1b[4m" << dst << "\x1b[0m' is mounted on the system..." << std::endl;
    std::cerr << "hint: it is required to unmount them first for data integrity." << std::endl;
    return 1;
  }

  std::cout << "info: initiating dumping..." << std::endl;

  // reference: https://wiki.archlinux.org/title/USB_flash_installation_medium
  opts opt = opts();
  opt.src = std::string(src);
  opt.dst = std::string(dst);
  opt.bs = "4M";
  opt.oflag = "direct";
  opt.conv = "fsync";
  opt.status = "progress";

  std::string cmd = opt.cmd();

  pipe_ptr pipe = exec_cmd(cmd.c_str());
  assert(pipe != nullptr);
  std::string cmd_output = read_pipe(pipe.get(), 1);

  return 0;
}

int main(int argc, char *argv[])
{
  get_opt(argc, argv);

  if (in != nullptr && out != nullptr)
  {
    std::cout << "info: source filesystem set to: '\x1b[4m" << in << "\x1b[0m'.\n";
    std::cout << "info: destination filesystem set to: '\x1b[4m" << out << "\x1b[0m. \n";
    std::cout << std::endl;
    dump_disk(in, out);
  }

  std::cerr << "error: source or destination is not set; exiting..." << std::endl;
  return 0;
}
