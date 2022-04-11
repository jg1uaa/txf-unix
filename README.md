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

### Example: Get file from server

#### Server (sender)

`[ipv4-addr]` is IPv4 address of server.

`[(filename to send)]` is required.

Run server first, waiting for connection from client.

```
$ txf 192.168.0.1 9999 sendfile
```

After file transfer is completed, server will stop.

#### Client (receiver)

`[ipv4-addr]` is IPv4 address of server.

`[(filename to send)]` is *not* required.


```
$ txf 192.168.0.1 9999
```

### Example: Put file to server

#### Server (receiver)

`[ipv4-addr]` is IPv4 address of server.

`[port]` is negative port value.

`[(filename to send)]` is *not* required.


Run server first, waiting for connection from client.

```
$ txf 192.168.0.1 -9999
```

After file transfer is completed, server will stop.

#### Client (sender)

`[ipv4-addr]` is IPv4 address of server.

`[port]` is negative port value.

`[(filename to send)]` is required.

```
$ txf 192.168.0.1 -9999 sendfile
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
