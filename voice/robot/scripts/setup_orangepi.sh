#!/usr/bin/env bash
# 在香橙派上执行：bash scripts/setup_orangepi.sh
set -euo pipefail
cd "$(dirname "$0")/.."

echo "[1/4] apt packages..."
sudo apt update
sudo apt install -y python3-pip python3-venv git sox alsa-utils

echo "[2/4] python venv..."
python3 -m venv .venv
# shellcheck disable=SC1091
source .venv/bin/activate
pip install -U pip
pip install -r requirements.txt

echo "[3/4] dirs..."
mkdir -p assets/devices

echo "[4/4] done."
echo "激活环境:  source ~/robot/.venv/bin/activate"
echo "假网关:    python tools/fake_gateway.py"
echo "主程序:    python main.py"
df -h / | tail -1
free -h | head -2
