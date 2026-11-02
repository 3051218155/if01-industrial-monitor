"""舵机控制接口，默认关闭。"""
from __future__ import annotations


class ServoController:
    def __init__(self, enabled: bool = False, open_angle: int = 90, close_angle: int = 0):
        self.enabled = enabled
        self.open_angle = open_angle
        self.close_angle = close_angle

    def eyes_open(self) -> None:
        if not self.enabled:
            print("[SERVO] eyes_open (disabled stub)")
            return
        print(f"[SERVO] eyes_open -> {self.open_angle}")

    def eyes_close(self) -> None:
        if not self.enabled:
            print("[SERVO] eyes_close (disabled stub)")
            return
        print(f"[SERVO] eyes_close -> {self.close_angle}")
