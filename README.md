# Tiny Xfer File (TXF)

---
## Description

A simple file transfer program for private LAN.

**Not intended to be used with public network.**


## Usage

```
$ txf
usage:  ./txf -p [client port] -l [IP address]
        ./txf -P [server port] -l [IP address]  -f [filename]
$
```

### Example: Get file from server

#### Server (sender)

`[IP address]` is IP address of server.

`[filename]` is required.

Run server first, waiting for connection from client.

```
$ txf -P 9999 -l 192.168.0.1 -f sendfile
```

After file transfer is completed, server will stop.

#### Client (receiver)

`[IP address]` is IP address of server.

`[filename]` is *not* required.


```
$ txf -p 9999 -l 192.168.0.1
```

### Example: Put file to server

#### Server (receiver)

`[IP address]` is IP address of server.

`[filename]` is *not* required.


Run server first, waiting for connection from client.

```
$ txf -P 9999 -l 192.168.0.1
```

After file transfer is completed, server will stop.

#### Client (sender)

`[IP address]` is IP address of server.

`[filename]` is required.

```
$ txf -p 9999 -l 192.168.0.1 -f sendfile
```

## Limitation

- no support large file transfer, up to 0x7fffffff bytes
- no support long file name, up to 20 ASCII characters
- no support timestamp

No plan to fix them.

## License

WTFPL (http://www.wtfpl.net/)
