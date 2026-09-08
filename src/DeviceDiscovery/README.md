# device-discovery

`device-discovery` is a C++17 Linux command-line utility that finds a **directly
connected** Linux device, verifies that it really runs an SSH server,
authenticates to it and reports the IPv4 addresses of its two network
interfaces.

The user never supplies a subnet, an interface name or a target IP:

```
DEVICE_SSH_PASSWORD='root' ./device-discovery
```

---

## 1. Physical setup

```
PC Ethernet interface
        |
        | direct Ethernet connection (no other devices on the link)
        |
Target Linux device (static IP, sshd on TCP/22, two relevant interfaces)
```

Assumptions baked into the tool:

* the PC interface is connected **only** to the target device;
* the PC interface uses DHCP, the target uses a **static** address that may be
  outside the PC's subnet;
* we are authorised to discover and administer the target.

## 2. Build

### Dependencies

| Requirement | Debian/Ubuntu | Fedora/RHEL | Arch |
|---|---|---|---|
| C++17 compiler | `build-essential` | `gcc-c++` | `base-devel` |
| CMake ≥ 3.16 | `cmake` | `cmake` | `cmake` |
| libssh2 | `libssh2-1-dev` | `libssh2-devel` | `libssh2` |
| pthreads | provided by glibc | provided by glibc | provided by glibc |

Everything else (`getifaddrs()`, netlink, `AF_PACKET`, POSIX sockets) comes from
the C library and the kernel.

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

The binary is `build/device-discovery`.

### Privileges

Stage A (subnet sweep) works as an unprivileged user. The Layer-2 fallback needs
raw packet access:

```bash
sudo setcap cap_net_raw,cap_net_admin+eip build/device-discovery
# or simply run it with sudo -E (keeping DEVICE_SSH_PASSWORD in the environment)
```

## 3. How discovery works

### 3.1 Interface selection (`src/network/interface_discovery.cpp`)

Interfaces are enumerated with `getifaddrs(3)` and filtered, in order:

1. loopback interfaces (`IFF_LOOPBACK`) are dropped;
2. interfaces that are administratively down (`!IFF_UP`) or without carrier
   (`!IFF_RUNNING`) are dropped;
3. interfaces without an IPv4 address are dropped;
4. obviously virtual interfaces are dropped by name prefix (`docker`, `br-`,
   `virbr`, `veth`, `vmnet`, `vboxnet`, `tun`, `tap`, `wg`, `ppp`, `zt`,
   `tailscale`, `bond`, …).

If **exactly one** interface survives, it is used. If **zero** survive the tool
exits with `No suitable network interface found`. If **more than one** survives
the tool prints all candidates and exits — it never picks an arbitrary
interface. `--interface NAME` exists purely for troubleshooting and is never
required.

### 3.2 Stage A — directly connected IPv4 subnet

The PC address and netmask define the directly connected network. Every usable
host address (network and broadcast addresses and the PC itself excluded) is
probed for TCP/22 through the thread pool. Networks larger than 4096 hosts are
refused rather than swept, so a misconfigured mask can never turn the tool into
an Internet scanner.

A candidate is only accepted when the peer **also** sends a valid SSH
identification string starting with `SSH-`; an open port alone is never enough.

### 3.3 Stage B — static-IP / Layer-2 fallback (`src/network/arp_discovery.cpp`)

If Stage A finds nothing, the target most likely has a static address outside the
PC's DHCP subnet. Discovery then drops to Layer 2 and is strictly limited to the
selected interface:

1. **Neighbour table** — the kernel ARP cache for this interface is read with
   netlink (`RTM_GETNEIGH`).
2. **Passive observation** — an `AF_PACKET` socket bound to the interface listens
   for `--l2-timeout` milliseconds and records the sender protocol address of
   every ARP frame and the source address of every IPv4 frame seen on the link.
   A statically addressed Linux device reveals itself through gratuitous ARP,
   ARP requests for its gateway, mDNS/LLDP/broadcast traffic, etc.
3. **Active confirmation** — ARP requests are sent on the link for the addresses
   already known, to refresh stale entries.

Only addresses actually observed on the link are then handed to the TCP/SSH
verification step; no arbitrary address space is ever scanned. Layer-2 discovery
and TCP/SSH verification are deliberately separate components.

If the device never emits a packet, the tool reports that the link is up but the
device stayed silent instead of claiming that no device exists.

If the discovered address is outside the PC's subnet the PC has no route to it;
the tool says so explicitly and suggests adding a temporary address in that
subnet, for example:

```bash
sudo ip addr add 10.20.30.2/24 dev enp1s0
```

### 3.4 SSH verification and authentication

Every candidate is connected to on port 22, its identification string is read and
checked against `SSH-`, and the banner is recorded. Only then — never before — is
the password sent. Authentication uses **libssh2** (`libssh2_userauth_password`);
the SSH protocol is not reimplemented.

The tool continues automatically only when **exactly one** candidate was
confirmed. When several are found it prints them all with their banners and exits
with `WARNING: multiple SSH devices discovered`; `--select IP` allows an explicit
choice.

### 3.5 Remote interface enumeration

After authentication `ifconfig -a` is executed and its stdout captured, because
`ifconfig` is part of the device contract. If it is missing or produces nothing
usable, the tool falls back to `ip -4 addr show` and says so in a warning. The
parser understands both the modern net-tools layout (`inet 192.168.10.50`), the
legacy one (`inet addr:192.168.10.50`) and `ip` output, ignores `lo`, IPv6 and
entries without an IPv4 address, and makes no assumption about interface names
(`eth0`, `ens33`, `enp1s0`, … all work).

If the number of non-loopback IPv4 interfaces is not two, everything found is
still reported together with a warning and exit code 10.

## 4. SSH credential configuration

* Username: `root` by default (`--ssh-user`).
* Password: read from the environment variable `DEVICE_SSH_PASSWORD`
  (`--ssh-password-env VAR` to use another one). It is never a command-line
  option, because arguments are visible to other users through `/proc`.
* The password is never printed, logged or included in errors or JSON output.
* The variable is removed from the environment right after it is read.

Using a file instead of the shell history:

```bash
install -m 600 /dev/null ~/.device-discovery.env
echo "DEVICE_SSH_PASSWORD='...'" > ~/.device-discovery.env
set -a; . ~/.device-discovery.env; set +a
./device-discovery
```

### Host-key verification

**Default: host-key verification is disabled** (`--host-key-policy off`). The
server key is not checked; its SHA256 fingerprint is reported in the output and
with `--verbose`. This is acceptable only because the device is reached over a
physically isolated point-to-point link — it is *not* secure against an attacker
with access to that link, and this is stated explicitly rather than implied to be
safe.

Use `--host-key-policy known-hosts [--known-hosts PATH]` for strict verification
against an OpenSSH `known_hosts` file (default `~/.ssh/known_hosts`); the
connection is refused when the key is unknown or does not match.

## 5. Command-line usage

```
device-discovery [options]

  --threads N              worker threads used for probing (default 32)
  --ssh-user USER          SSH user name (default root)
  --ssh-password-env VAR   environment variable holding the password
                           (default DEVICE_SSH_PASSWORD)
  --timeout MS             TCP connect timeout (default 750)
  --banner-timeout MS      SSH banner read timeout (default 1000)
  --auth-timeout MS        SSH authentication timeout (default 5000)
  --exec-timeout MS        SSH command timeout (default 5000)
  --l2-timeout MS          layer-2 listening window (default 8000)
  --host-key-policy P      off | known-hosts (default off)
  --known-hosts PATH       known_hosts file for the known-hosts policy
  --select IP              continue with this candidate when several are found
  --interface NAME         override interface auto-detection (troubleshooting)
  --json                   machine readable output
  --verbose                progress diagnostics on stderr
  -h, --help               help
```

There are deliberately **no** `--subnet` and `--target-ip` options.

### Example: successful run

```
$ DEVICE_SSH_PASSWORD='root' ./device-discovery
Directly connected device discovered

SSH endpoint:
  IP:       192.168.10.50
  Banner:   SSH-2.0-OpenSSH_9.6
  Host key: SHA256:0lS2c7Cw1H9Nl1eB1a8i2v0F8oGf9M7yB8h4hqZzZ0c=

Network interfaces:
  eth0      192.168.10.50
  eth1      10.20.30.1
```

```
$ DEVICE_SSH_PASSWORD='root' ./device-discovery --json
{
  "ssh_ip": "192.168.10.50",
  "ssh_banner": "SSH-2.0-OpenSSH_9.6",
  "host_key_fingerprint": "SHA256:0lS2c7Cw1H9Nl1eB1a8i2v0F8oGf9M7yB8h4hqZzZ0c=",
  "discovery_stage": "subnet-scan",
  "interfaces": [
    {
      "name": "eth0",
      "ipv4": "192.168.10.50"
    },
    {
      "name": "eth1",
      "ipv4": "10.20.30.1"
    }
  ],
  "warnings": []
}
```

### Example: failure output

```
$ DEVICE_SSH_PASSWORD='root' ./device-discovery
error: no device discovered: the subnet sweep on enp1s0 found no SSH server and
layer-2 discovery observed no IPv4 traffic on the link (no IPv4 address was
observed on the link: the device is either silent or does not use IPv4 on this
interface). The device may be silent; check cabling/link state or power-cycle the
device so that it emits ARP traffic.
$ echo $?
4
```

```
$ DEVICE_SSH_PASSWORD='root' ./device-discovery
WARNING: multiple SSH devices discovered
  192.168.10.50  SSH-2.0-OpenSSH_9.6
  192.168.10.51  SSH-2.0-dropbear_2022.83
error: refusing to choose automatically; rerun with --select IP
$ echo $?
5
```

## 6. Exit codes

| Code | Meaning |
|---|---|
| 0 | success |
| 2 | no suitable network interface found |
| 3 | multiple candidate interfaces found |
| 4 | no device discovered / discovered at L2 but unreachable / port 22 closed |
| 5 | multiple SSH devices discovered (or `--select` did not match) |
| 6 | SSH connection, handshake or host-key verification failed |
| 7 | SSH authentication failed |
| 8 | `ifconfig` (and the `ip` fallback) could not be executed |
| 9 | interface output could not be parsed |
| 10 | fewer or more than two non-loopback IPv4 interfaces were found |
| 64 | usage error |
| 78 | configuration error (e.g. password variable not set) |

## 7. Concurrency and timeouts

* `src/threading/thread_pool.cpp` implements a fixed-size pool (default 32
  workers, `--threads N`) with a mutex-protected task queue, a condition
  variable, idempotent shutdown and `std::future`-based error propagation. No
  thread is created per IP address and the queue is bounded by the number of
  addresses of a single small subnet.
* All network operations are bounded. Connects use non-blocking sockets with
  `poll()`; banner reads have their own deadline; libssh2 sessions use
  `libssh2_session_set_timeout()`. Defaults live in `src/core/config.hpp`
  (`750 / 1000 / 5000 / 5000` ms) instead of being scattered as magic numbers.

## 8. Testing

`ctest --test-dir build` runs five suites built on a tiny dependency-free
assertion header:

| Suite | Covers |
|---|---|
| `test_interface_discovery` | one valid Ethernet interface, loopback only, multiple candidates, interface without IPv4/carrier, virtual-name detection, IPv4 helpers |
| `test_ssh_detector` | valid banner, non-SSH service on the port, malformed banner, connection refused, banner timeout, invalid address (uses a loopback mock server, no device needed) |
| `test_ifconfig_parser` | modern and legacy `ifconfig`, `ip -4 addr`, IPv6-only interfaces, `lo` filtering, non-`eth*` naming, format auto-detection |
| `test_thread_pool` | many tasks, empty queue, shutdown/idempotency, task exceptions, concurrent execution |
| `test_port_scanner` | subnet host enumeration, /31 and /32 links, oversized-network refusal, human and JSON formatting |

The mock SSH server in `test_ssh_detector` provides the "test mode" that lets
detection and parsing be exercised without a physical device.

## 9. Troubleshooting

| Symptom | Action |
|---|---|
| `Multiple candidate interfaces found` | disconnect the unrelated links or use `--interface NAME` |
| `No suitable network interface found` | check the cable/link state; the PC port must have an IPv4 address (DHCP or a manual one) |
| `layer-2 discovery needs raw packet access` | run with `sudo -E` or `setcap cap_net_raw,cap_net_admin+eip` |
| L2 finds an address but SSH is unreachable | the address is outside the PC subnet: `sudo ip addr add <free-ip-in-that-subnet>/<prefix> dev <iface>` |
| Device stays silent | power-cycle it or wait longer with `--l2-timeout 20000` |
| Scan feels slow | raise `--threads` or lower `--timeout` |
| `port 22 is open but the peer is not an SSH server` | something else listens on 22; check the device |
| `SSH authentication failed` | check `DEVICE_SSH_PASSWORD` and that root login with a password is allowed on the device |
| `expected two non-loopback IPv4 interfaces but found N` | all interfaces found are still listed; verify the device configuration |
