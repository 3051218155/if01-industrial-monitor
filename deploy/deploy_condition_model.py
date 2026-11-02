# -*- coding: utf-8 -*-
"""Stage condition-model artifacts on the board and report NPU readiness."""
from __future__ import annotations

import argparse
import getpass
import hashlib
import os
from pathlib import Path

import paramiko


BASE_ARTIFACTS = (
    "condition_mlp_float.onnx",
    "condition_mlp_qdq_uint8.onnx",
    "preprocess.json",
    "metrics.json",
)
OPTIONAL_ARTIFACTS = ("condition_mlp_npu.onnx",)


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--source",
        type=Path,
        default=Path(__file__).resolve().parents[1] / "ai" / "model",
    )
    parser.add_argument("--host", default=os.getenv("FPGA_BOARD_HOST", "192.168.137.100"))
    parser.add_argument("--user", default=os.getenv("FPGA_BOARD_USER", "root"))
    parser.add_argument("--password", default=os.getenv("FPGA_BOARD_PASSWORD"))
    parser.add_argument("--remote-dir", default="/root/ai/condition")
    parser.add_argument("--require-npu", action="store_true")
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
        client.exec_command(f"mkdir -p {args.remote_dir}")[1].channel.recv_exit_status()
        sftp = client.open_sftp()
        artifacts = BASE_ARTIFACTS + tuple(
            name for name in OPTIONAL_ARTIFACTS
            if (args.source / name).is_file()
        )
        for name in artifacts:
            local = args.source / name
            if not local.is_file():
                print(f"missing artifact: {local}")
                return 1
            remote = f"{args.remote_dir}/{name}"
            sftp.put(str(local), remote)
            print(f"uploaded {name}: {local.stat().st_size} bytes")
        sftp.close()

        def run(command: str) -> str:
            _, stdout, stderr = client.exec_command(command, timeout=20)
            return (
                stdout.read() + stderr.read()
            ).decode("utf-8", "replace").strip()

        remote_hash = run(f"cd {args.remote_dir} && sha256sum {' '.join(artifacts)}")
        print("=== board sha256 ===")
        print(remote_hash)
        remote_lookup = {
            line.split()[1]: line.split()[0]
            for line in remote_hash.splitlines()
            if len(line.split()) == 2
        }
        for name in artifacts:
            local_hash = sha256(args.source / name)
            if remote_lookup.get(name) != local_hash:
                print(f"FAIL hash mismatch: {name}")
                return 1

        devices = run(
            "for p in /dev/soft_npu /dev/hard_npu; "
            "do [ -e $p ] && echo present:$p || echo absent:$p; done"
        )
        print("=== NPU state ===")
        print(devices)
        npu_ready = "present:" in devices
        print(
            "PASS model staged on board"
            if npu_ready
            else "PASS model staged; NPU inference blocked by missing PL IP/driver"
        )
        if args.require_npu and not npu_ready:
            return 1
        return 0
    finally:
        client.close()


if __name__ == "__main__":
    raise SystemExit(main())
