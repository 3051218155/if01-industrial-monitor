# -*- coding: utf-8 -*-
"""Install boot-time loading for cma_mem and hard_npu."""

from __future__ import annotations

import argparse
import getpass
import os
from pathlib import Path

import paramiko


ROOT = Path(__file__).resolve().parent
UNIT = ROOT / "fpga-npu-modules.service"
DROPIN = ROOT / "fpga-npu-service.conf"


def run(
    client: paramiko.SSHClient, command: str, timeout: int = 30
) -> str:
    _, stdout, stderr = client.exec_command(command, timeout=timeout)
    output = (stdout.read() + stderr.read()).decode("utf-8", "replace").strip()
    status = stdout.channel.recv_exit_status()
    if status:
        raise RuntimeError(f"command failed ({status}): {command}\n{output}")
    return output


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default=os.getenv("FPGA_BOARD_HOST", "192.168.137.100"))
    parser.add_argument("--user", default=os.getenv("FPGA_BOARD_USER", "root"))
    parser.add_argument("--password", default=os.getenv("FPGA_BOARD_PASSWORD"))
    args = parser.parse_args()
    password = args.password or getpass.getpass("Board password: ")
    client = paramiko.SSHClient()
    client.set_missing_host_key_policy(paramiko.AutoAddPolicy())
    client.connect(
        args.host,
        username=args.user,
        password=password,
        timeout=10,
        banner_timeout=10,
    )
    try:
        run(
            client,
            "mkdir -p /etc/systemd/system/fpga-modbus-slave.service.d "
            "/etc/systemd/system/if01-gateway.service.d",
        )
        sftp = client.open_sftp()
        try:
            with sftp.open("/etc/systemd/system/fpga-npu-modules.service", "w") as stream:
                stream.write(UNIT.read_text(encoding="utf-8"))
            for service in ("fpga-modbus-slave", "if01-gateway"):
                path = (
                    f"/etc/systemd/system/{service}.service.d/"
                    "10-npu-modules.conf"
                )
                with sftp.open(path, "w") as stream:
                    stream.write(DROPIN.read_text(encoding="utf-8"))
        finally:
            sftp.close()
        print(run(
            client,
            "systemctl daemon-reload; "
            "systemctl enable --now fpga-npu-modules.service; "
            "systemctl status fpga-npu-modules.service --no-pager -n 10",
        ))
        print(run(
            client,
            "grep -E '^(cma_mem|hard_npu|soft_npu) ' /proc/modules; "
            "ls -l /dev/cma_mem /dev/hard_npu /dev/soft_npu; "
            "systemctl is-enabled fpga-npu-modules.service "
            "fpga-modbus-slave.service if01-gateway.service",
        ))
        return 0
    finally:
        client.close()


if __name__ == "__main__":
    raise SystemExit(main())
