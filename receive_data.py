#!/usr/bin/env python3
import argparse
import socket
import sys
from tqdm import tqdm

def recv_exactly(conn, n):
    """Read exactly n bytes from conn, or raise if the connection closes early."""
    buf = bytearray()
    while len(buf) < n:
        chunk = conn.recv(min(n - len(buf), 1 << 20))
        if not chunk:
            raise ConnectionError(
                f"Connection closed after {len(buf)} of {n} bytes")
        buf.extend(chunk)
    return bytes(buf)

def main():
    parser = argparse.ArgumentParser(
        description="Receive one capture over TCP and save to file.")
    parser.add_argument("filename", help="output file path")
    parser.add_argument("--host", default="127.0.0.1",
                        help="bind address (default: 127.0.0.1, loopback for SSH tunnel)")
    parser.add_argument("--port", type=int, default=5001,
                        help="streaming port (default: 5001, check free ports via `ss -tnlp | grep :PORT`)")
    args = parser.parse_args()

    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as srv:
        srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        srv.bind((args.host, args.port))
        srv.listen(1)
        print(f"Listening on {args.host}:{args.port} ...", flush=True)

        conn, addr = srv.accept()
        with conn:
            print(f"Connection from {addr}", flush=True)

            # 1. Read the 8-byte header = number of payload bytes to expect
            header = recv_exactly(conn, 8)
            nbytes = int.from_bytes(header, "little")
            print(f"Header says {nbytes} bytes incoming", flush=True)

            # 2. Read exactly that many bytes, streaming to disk
            received = 0
            with open(args.filename, "wb") as f, \
                 tqdm(total=nbytes, unit="B", unit_scale=True,
                      unit_divisor=1024, desc="Receiving") as bar:
                while received < nbytes:
                    chunk = conn.recv(min(nbytes - received, 1 << 20))
                    if not chunk:
                        bar.close()
                        print(f"ERROR: connection closed early "
                              f"({received}/{nbytes} bytes)", file=sys.stderr)
                        sys.exit(1)
                    f.write(chunk)
                    received += len(chunk)
                    bar.update(len(chunk))

            print(f"Saved {received} bytes to {args.filename}", flush=True)

    print("Done.", flush=True)

if __name__ == "__main__":
    main()
