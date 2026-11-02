#!/usr/bin/env python3
"""Install the condition model and strict HARDNPU runtime on IF-01."""

from __future__ import annotations

import argparse
import getpass
import hashlib
import os
from datetime import datetime
from pathlib import Path
import sys

import paramiko


ROOT = Path(__file__).resolve().parents[1]
FILES = (
    ROOT / "ai" / "runtime_condition_infer.py",
    ROOT / "ai" / "npu_condition_infer.py",
    ROOT / "ai" / "generic_npu_run_strict.cpp",
    ROOT / "ai" / "model" / "condition_runtime_npu16_9x9_qdq_inferred.onnx",
    ROOT / "ai" / "model" / "runtime_preprocess.json",
    ROOT / "ai" / "model" / "preprocess.json",
)


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default=os.getenv("FPGA_BOARD_HOST", "192.168.137.100"))
    parser.add_argument("--user", default=os.getenv("FPGA_BOARD_USER", "root"))
    parser.add_argument("--password", default=os.getenv("FPGA_BOARD_PASSWORD"))
    parser.add_argument("--remote-dir", default="/root/ai/condition")
    return parser.parse_args()


def run(client: paramiko.SSHClient, command: str, timeout: int = 30) -> str:
    _, stdout, stderr = client.exec_command(command, timeout=timeout)
    output = (stdout.read() + stderr.read()).decode("utf-8", "replace").strip()
    status = stdout.channel.recv_exit_status()
    if status:
        raise RuntimeError(f"command failed ({status}): {command}\n{output}")
    return output


def main() -> int:
    args = parse_args()
    missing = [str(path) for path in FILES if not path.is_file()]
    if missing:
        print("Missing files:")
        print("\n".join(missing))
        return 1

    password = args.password or getpass.getpass("Board password: ")
    client = paramiko.SSHClient()
    client.set_missing_host_key_policy(paramiko.AutoAddPolicy())
    client.connect(
        args.host,
        username=args.user,
        password=password,
        timeout=15,
        banner_timeout=15,
    )
    try:
        stamp = datetime.now().strftime("%Y%m%d_%H%M%S")
        backup = f"{args.remote_dir}/rollback_{stamp}"
        run(client, f"mkdir -p {args.remote_dir} {backup}")
        sftp = client.open_sftp()
        try:
            for local in FILES:
                remote = f"{args.remote_dir}/{local.name}"
                temporary = f"{remote}.new"
                try:
                    sftp.stat(remote)
                except FileNotFoundError:
                    pass
                else:
                    run(client, f"cp -p {remote} {backup}/{local.name}")
                sftp.put(str(local), temporary)
                remote_hash = run(client, f"sha256sum {temporary}").split()[0]
                if remote_hash != sha256(local):
                    raise RuntimeError(f"upload hash mismatch: {local}")
                run(client, f"mv -f {temporary} {remote}")
                print(f"installed {remote}")
        finally:
            sftp.close()

        build = (
            f"cd {args.remote_dir} && "
            "g++ -std=c++17 -O2 generic_npu_run_strict.cpp "
            "-o generic_npu_run_strict -larmnn -larmnnOnnxParser"
        )
        run(client, build, timeout=180)
        print(f"strict runtime built; rollback={backup}")
        return 0
    finally:
        client.close()


if __name__ == "__main__":
    sys.exit(main())
