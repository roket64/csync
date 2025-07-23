# csync

`csync` is a command-line utility written in C++, designed to safely and efficiently write a bootable CD-ROM image (`.iso`) to a block device.

## Prerequisites

### 1. coreutils

`csync` uses `dd`, `file`, `findmnt` commands internally, which is included in [GNU core utilities](https://www.gnu.org/software/coreutils/). Make sure they are being installed in your system.

## Usage

To use `csync`, provide a path to the source `.iso` file and the destination device.

```bash
# Basic usage
sudo csync -i <path-to-your-iso-file> -o <path-to-your-device>

# Example
sudo csync --input /home/user/Downloads/ubuntu.iso --output /dev/sdX
```

### Options

- `-i`, `--input`: Specifies the path to the source CD-ROM image file.
- `-o`, `--output`: Specifies the path to the destination block device (e.g. `/dev/sda`).
- `-h`, `--help`: Displays the help message.
- `-v`, `--version`: Shows the current version of the utility.