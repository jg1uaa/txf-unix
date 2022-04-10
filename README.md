# Tiny Xfer File (TXF)

---
## Description

A simple file transfer program for IPv4-based private LAN.

**Not intended to be used with public or IPv6-based network.**


## Usage

```
$ txf
txf [ipv4-addr] [port] [(filename to send)]
$
```

### Example: Server (sender)

`[ipv4-addr]` is IPv4 address of server.

`[(filename to send)]` is required.

Run server first, waiting for connection from client.

```
$ txf 192.168.0.1 9999 output.img
* server
output.img, 268435456 byte
connected from 192.168.0.2
$
```

After file transfer is completed, server will stop.

### Example: Client (receiver)

`[ipv4-addr]` is IPv4 address of server.

`[(filename to send)]` is *not* required.


```
$ txf 192.168.0.1 9999
* client
connected to 192.168.0.10
output.img, 268435456 byte
$ ls -l output.img
-rw-r--r--  1 uaa  users  268435456 Apr 10 09:58 output.img
$
```

## Limitation

- no support IPv6
- no support name resolution
- no support large file transfer, up to 0x7fffffff bytes
- no support long file name, up to 20 ASCII characters
- no support timestamp

No plan to fix them.

## License

WTFPL (http://www.wtfpl.net/)

