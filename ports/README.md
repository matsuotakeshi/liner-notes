# FreeBSD ports

`comms/liner-notes` is the Liner notes port. See [PORTS.md](../PORTS.md).
The ZMODEM option depends on the official FreeBSD `comms/lrzsz` port.
No private lrzsz port, lrzsz source archive or lrzsz executable is distributed.
`test-resume.py` is Liner notes' interoperability test, not the transfer engine.
The optional developer tool `scripts/build-lrzsz.py` downloads upstream source
into a separate directory for Linux/isolated tests; it is not the FreeBSD
installation procedure and is not run by the Liner notes port.
