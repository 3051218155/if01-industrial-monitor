# -*- coding: utf-8 -*-
"""Install and verify the board-side Modbus slave CSV service."""
from __future__ import annotations

import argparse
import getpass
import os
import time
from pathlib import Path

import paramiko

LOCAL_UNIT = Path(__file__).with_name("fpga-modbus-slave.service")
REMOTE_UNIT = "/etc/systemd/system/fpga-modbus-slave.service"
REMOTE_CSV = "/root/serial_hmi_log.csv"


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default=os.getenv("FPGA_BOARD_HOST", "192.168.137.100"))
    parser.add_argument("--user", default=os.getenv("FPGA_BOARD_USER", "root"))
    parser.add_argument("--password", default=os.getenv("FPGA_BOARD_PASSWORD"))
    args = parser.parse_args()
    if not LOCAL_UNIT.is_file():
        print(f"missing service unit: {LOCAL_UNIT}")
        return 1

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

    def run(command: str, timeout: int = 30) -> str:
        _, stdout, stderr = client.exec_command(command, timeout=timeout)
        return (stdout.read() + stderr.read()).decode("utf-8", "replace").strip()

    try:
        sftp = client.open_sftp()
        sftp.put(str(LOCAL_UNIT), REMOTE_UNIT)
        sftp.close()

        print("=== install service ===")
        print(
            run(
                "systemctl stop fpga-modbus-slave.service 2>/dev/null || true; "
                "pkill -x modbus_slave 2>/dev/null || true; sleep 1; "
                "systemctl daemon-reload; "
                "systemctl enable fpga-modbus-slave.service; "
                "systemctl restart fpga-modbus-slave.service; sleep 3"
            )
        )

        active = run("systemctl is-active fpga-modbus-slave.service")
        process = run("pgrep -a -x modbus_slave || true")
        print("=== state ===")
        print(f"service: {active}")
        print(process)
        if active != "active":
            print(run("journalctl -u fpga-modbus-slave.service --no-pager -n 30"))
            return 1

        time.sleep(3)
        count_before = run(f"wc -l < {REMOTE_CSV} 2>/dev/null || echo 0")
        time.sleep(3)
        count_after = run(f"wc -l < {REMOTE_CSV} 2>/dev/null || echo 0")

        print("=== local storage ===")
        print(
            run(
                f"stat -c 'file=%n size=%s modified=%y' {REMOTE_CSV} 2>&1; "
                f"tail -n 3 {REMOTE_CSV} 2>&1; "
                "df -h /; "
                "findmnt -no SOURCE,FSTYPE,OPTIONS /"
            )
        )
        print(f"rows: {count_before} -> {count_after}")

        try:
            grew = int(count_after) > int(count_before)
        except ValueError:
            grew = False
        if not grew:
            print("FAIL: CSV row count did not increase")
            print(run("journalctl -u fpga-modbus-slave.service --no-pager -n 30"))
            return 1

        enabled = run("systemctl is-enabled fpga-modbus-slave.service")
        print(f"enabled: {enabled}")
        if enabled != "enabled":
            print("FAIL: service is not enabled after reboot")
            return 1

        print("=== restart persistence ===")
        restart_rows = run(f"wc -l < {REMOTE_CSV}")
        print(run("systemctl restart fpga-modbus-slave.service; sleep 3"))
        time.sleep(2)
        after_restart = run(f"wc -l < {REMOTE_CSV}")
        active = run("systemctl is-active fpga-modbus-slave.service")
        print(f"service: {active}, rows: {restart_rows} -> {after_restart}")
        try:
            preserved = (
                active == "active"
                and int(after_restart) >= int(restart_rows)
                and int(after_restart) > 1
            )
        except ValueError:
            preserved = False
        if not preserved:
            print("FAIL: restart lost service state or CSV history")
            print(run("journalctl -u fpga-modbus-slave.service --no-pager -n 30"))
            return 1

        print("PASS: service enabled, CSV growing, restart preserved history")
        return 0
    finally:
        client.close()


if __name__ == "__main__":
    raise SystemExit(main())
