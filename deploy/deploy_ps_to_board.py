#!/usr/bin/env python3
"""Build firmware/linux locally and deploy it to an IF-01 board over SSH."""

from __future__ import annotations

import argparse
import getpass
import os
from pathlib import Path
import shlex
import sys

import paramiko


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "firmware" / "linux"
BINARIES = (
    "pl_regs",
    "motor_ctrl",
    "modbus_master",
    "modbus_slave",
    "sensor_monitor",
    "fault_ai",
    "gateway",
    "perf_bench",
)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default=os.getenv("FPGA_BOARD_HOST", "192.168.137.100"))
    parser.add_argument("--user", default=os.getenv("FPGA_BOARD_USER", "root"))
    parser.add_argument("--password", default=os.getenv("FPGA_BOARD_PASSWORD"))
    parser.add_argument("--remote-dir", default="/root/fpga_ps")
    parser.add_argument("--skip-build", action="store_true")
    return parser.parse_args()


def run_checked(
    client: paramiko.SSHClient, command: str, timeout: int = 180
) -> str:
    _, stdout, stderr = client.exec_command(command, timeout=timeout)
    output = (stdout.read() + stderr.read()).decode("utf-8", "replace").strip()
    status = stdout.channel.recv_exit_status()
    if output:
        print(output)
    if status:
        raise RuntimeError(f"remote command failed ({status}): {command}")
    return output


def main() -> int:
    args = parse_args()
    password = args.password or getpass.getpass("Board password: ")
    if not args.skip_build:
        command = (
            f'cd "{SOURCE}" && make clean && make all'
            if os.name != "nt"
            else None
        )
        if command:
            print("Run make on a Linux host before deployment on Windows:")
            print(command)
        else:
            print("Windows host detected; using existing firmware/linux binaries.")

    client = paramiko.SSHClient()
    client.set_missing_host_key_policy(paramiko.AutoAddPolicy())
    client.connect(
        args.host,
        username=args.user,
        password=password,
        timeout=15,
        banner_timeout=15,
    )
    remote = shlex.quote(args.remote_dir)
    try:
        run_checked(client, f"mkdir -p {remote}")
        sftp = client.open_sftp()
        try:
            for path in sorted(SOURCE.iterdir()):
                if path.suffix in {".cpp", ".h"} or path.name == "Makefile":
                    sftp.put(str(path), f"{args.remote_dir}/{path.name}")
            for name in BINARIES:
                local = SOURCE / name
                if local.is_file():
                    sftp.put(str(local), f"{args.remote_dir}/{name}")
        finally:
            sftp.close()

        if not args.skip_build:
            run_checked(client, f"cd {remote} && make all")

        binaries = " ".join(BINARIES)
        run_checked(
            client,
            "systemctl stop fpga-modbus-slave.service if01-gateway.service "
            "2>/dev/null || true; "
            f"cd {remote} && install -m 0755 {binaries} /root/",
        )
        run_checked(
            client,
            "systemctl daemon-reload; "
            "systemctl restart fpga-modbus-slave.service if01-gateway.service",
        )
        print(run_checked(client, "systemctl is-active fpga-modbus-slave.service if01-gateway.service"))
        return 0
    finally:
        client.close()


if __name__ == "__main__":
    sys.exit(main())
