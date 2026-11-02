"""拓展屏显示 —— 对应文档 §4.4.5（原全息风扇）。"""
from __future__ import annotations

from pathlib import Path


class ExtDisplay:
    def __init__(self, assets_dir: str, width: int = 1280, height: int = 800, fullscreen: bool = False):
        self.assets_dir = Path(assets_dir)
        self.width = width
        self.height = height
        self.fullscreen = fullscreen
        self._pygame = None
        self._screen = None

    def _ensure_pygame(self) -> None:
        if self._pygame is not None:
            return
        import pygame

        pygame.init()
        flags = pygame.FULLSCREEN if self.fullscreen else 0
        self._screen = pygame.display.set_mode((self.width, self.height), flags)
        pygame.display.set_caption("工业运维机器人 · 拓展屏")
        self._pygame = pygame

    def show_status(self, device_id: int, status: str, fault_location: str = "") -> None:
        """优先显示图片；没有素材则用色块+文字占位。"""
        self._ensure_pygame()
        assert self._pygame and self._screen

        key = "normal" if status == "normal" else "fault"
        candidates = [
            self.assets_dir / f"{device_id}_{key}.png",
            self.assets_dir / f"{device_id}_{key}.jpg",
            self.assets_dir / f"default_{key}.png",
        ]
        img_path = next((p for p in candidates if p.exists()), None)

        if img_path:
            image = self._pygame.image.load(str(img_path))
            image = self._pygame.transform.smoothscale(image, (self.width, self.height))
            self._screen.blit(image, (0, 0))
        else:
            color = (30, 120, 60) if status == "normal" else (160, 40, 40)
            self._screen.fill(color)
            font = self._pygame.font.SysFont("DejaVu Sans", 48)
            lines = [
                f"Device #{device_id}",
                f"Status: {status}",
                f"Fault: {fault_location or '-'}",
            ]
            y = self.height // 2 - 60
            for line in lines:
                surf = font.render(line, True, (255, 255, 255))
                rect = surf.get_rect(center=(self.width // 2, y))
                self._screen.blit(surf, rect)
                y += 60

        self._pygame.display.flip()
        # 处理一次事件，避免窗口无响应
        for event in self._pygame.event.get():
            if event.type == self._pygame.QUIT:
                self.close()

    def close(self) -> None:
        if self._pygame:
            self._pygame.quit()
            self._pygame = None
            self._screen = None
