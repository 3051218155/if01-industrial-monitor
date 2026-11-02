# 同步机器人核心文件到香橙派
# 用法：.\tools\sync_to_pi.ps1 [-HostIp 10.197.143.207] [-User orangepi]
param(
    [string]$HostIp = "10.197.143.207",
    [string]$User = "orangepi"
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)

$RobotFiles = @(
    "main.py",
    "llm_intent.py",
    "intent_exec.py",
    "pc_bridge.py",
    "config.yaml",
    "baidu_speech.py"
)

Write-Host "同步 robot -> ${User}@${HostIp}:~/robot"
foreach ($f in $RobotFiles) {
    $src = Join-Path $Root $f
    if (-not (Test-Path $src)) { Write-Warning "跳过缺失: $f"; continue }
    scp $src "${User}@${HostIp}:~/robot/$f"
}

$McpSrc = Join-Path (Split-Path -Parent $Root) "tools\orange_pi_mcp\voice_mcp_bridge.py"
if (Test-Path $McpSrc) {
    Write-Host "同步 MCP voice_mcp_bridge.py"
    scp $McpSrc "${User}@${HostIp}:~/orange_pi_mcp/voice_mcp_bridge.py"
}

Write-Host "完成。板上执行: cd ~/robot && source .venv/bin/activate && python main.py"
