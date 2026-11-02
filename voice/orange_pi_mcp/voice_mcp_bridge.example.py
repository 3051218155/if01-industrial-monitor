#!/usr/bin/env python3
"""兼容旧文件名：转发到 voice_mcp_bridge。"""
from voice_mcp_bridge import handle_utterance, handle_voice_text, main

__all__ = ["handle_utterance", "handle_voice_text", "main"]

if __name__ == "__main__":
    main()
