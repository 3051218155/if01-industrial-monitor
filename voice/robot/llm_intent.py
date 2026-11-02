"""用 OpenAI 兼容 API（DeepSeek 等）把 ASR 文本解析成结构化指令；失败则模糊规则兜底。"""
from __future__ import annotations

import json
import re
from typing import Any

import requests

SYSTEM_PROMPT = """你是工业运维机器人的语音意图解析器。
用户来自麦克风 ASR，文字常有错别字、同音字、口语、语序乱，请按「意思」理解，不要抠字眼。
只输出一个 JSON 对象，不要 markdown，不要解释。

字段：
- action: query | show | ping | ignore | wake_only
- device_id: 1~6 或 null（用户没说几号机就填 null，程序会自动取电脑大屏当前选中的设备）
- fault: bearing | imbalance | looseness | coupling | none | null
- reply: 简短中文应答（ignore 为空；query/show 可先写「好的」）

核心原则（极重要）：
A) 只要语义上像在查设备、看大屏、切故障、检查连通 → 必须给出 query/show/ping，宁肯猜对也不要 ignore
B) 仅当明显闲聊、完全无关、或纯噪声时才 ignore
C) 只叫名字/「在吗」→ wake_only

action 判定（意思相近即可）：
- query：查状态、怎么样、如何、好不好、有没有问题、运行情况、温度振动、帮我看看几号机…
- show：显示/打开/看下/切到/大屏/画面 + 某故障或恢复正常；或直接说「三号轴承故障」「三号不平衡」这类要展示的语义
- ping：在线吗、通不通、测网、检查系统
- 同时像查询又像展示故障部位 → 优先 show；纯问状态且没有部位 → query

device_id：
- 一/1/壹→1，二/2/两→2，三/3/仨→3，四→4，五→5，六→6
- 用户只说「当前设备 / 这台电机 / 电机状态」，没有明确编号 → null，不要自己猜编号
- ASR 错写：三好/三浩/山号≈三号；轴成/轴程/轴城≈轴承

fault：
- 轴承/轴成/磨损/轴瓦 → bearing
- 不平衡/转子/震动大/晃 → imbalance
- 地脚/松动/螺丝松 → looseness
- 联轴/连轴/对中不好 → coupling
- 正常/恢复/复位/好了 → none

示例（皆应执行，勿 ignore）：
「三号怎么样」→ query,3
「帮我看看三号机」→ query,3
「显示三号轴承」→ show,3,bearing
「三号轴承故障」→ show,3,bearing
「把三号不平衡打到大屏」→ show,3,imbalance
「三号恢复正常」→ show,3,none
「轴成坏了显示一下」→ show,3,bearing
「连通了吗」→ ping
"""

NARRATE_PROMPT = """你是工业运维机器人的语音播报员。
根据用户问题和真实查询结果，用一句自然口语回答，只回答用户问到的内容。

必须遵守：
1) 只输出纯中文，不要 JSON、markdown、标题、引号
2) 用户只问某一项（振动、温度、电流、峰值、健康度、告警…）就只报这一项，绝不补充其他指标
3) 只有用户问「状态 / 怎么样 / 有什么问题 / 正不正常」时，才说运行状态加一到两个关键数值（状态、振动、温度里选）
4) 不要提数据来源，不要念字段名，不要说「根据数据」「查询结果」这类元话术
5) 不编造数据里没有的内容；查不到就简短说查不到
6) 全文不超过 50 个字，适合直接朗读
"""

# ASR 常见错字 → 规范词
_ASR_FIXES: list[tuple[str, str]] = [
    (r"小[陈成程尘撑乘城承呈]", "小橙"),
    (r"轴[成承程城乘]", "轴承"),
    (r"不[平评]衡", "不平衡"),
    (r"[联连]轴[器器]?", "联轴"),
    (r"地[脚角]", "地脚"),
    (r"三[好浩豪号]", "三号"),
    (r"二[好浩号]", "二号"),
    (r"两号", "二号"),
    (r"一[好浩号]|壹号", "一号"),
    (r"显[示试事]", "显示"),
    (r"[查察]询?", "查询"),
    (r"状[态太]", "状态"),
    (r"故[障璋章]", "故障"),
]

_QUERY_KEYS = (
    "查", "状态", "怎么样", "怎样", "如何", "好不好", "情况", "运行",
    "有没有问题", "正不正常", "温", "振动", "看看", "帮我看", "问一下",
)
_SHOW_KEYS = (
    "显示", "大屏", "画面", "切换", "展示", "切到", "打开", "放到", "打到",
    "看看屏", "投屏", "演示",
)
_FAULT_MAP = (
    (("轴承", "轴瓦", "磨损"), "bearing"),
    (("不平衡", "转子", "晃"), "imbalance"),
    (("地脚", "松动"), "looseness"),
    (("联轴", "连轴", "不对中"), "coupling"),
    (("正常", "恢复", "复位", "好了"), "none"),
)
_PING_KEYS = ("连通", "在线", "测网", "ping", "检查系统", "通不通")


def normalize_asr(text: str) -> str:
    t = (text or "").strip()
    for ch in "。.!！？?，,、":
        t = t.replace(ch, "")
    for pat, rep in _ASR_FIXES:
        t = re.sub(pat, rep, t)
    return t.strip()


def _parse_device_id(text: str) -> int | None:
    m = re.search(r"([一二两三四五六123456])\s*号", text)
    if not m:
        m = re.search(r"(?<!\d)([1-6])(?!\d)", text)
    if not m:
        return None
    token = m.group(1)
    mapping = {"一": 1, "二": 2, "两": 2, "三": 3, "四": 4, "五": 5, "六": 6}
    return mapping.get(token) or int(token)


def _parse_fault(text: str) -> str | None:
    for words, site in _FAULT_MAP:
        if any(w in text for w in words):
            return site
    if "故障" in text:
        return "bearing"  # 只说故障未指部位时，默认定位到轴承
    return None


def fuzzy_parse(text: str) -> dict[str, Any]:
    """规则模糊解析：意思沾边就执行，供 LLM ignore / 失败时兜底。"""
    raw = (text or "").strip()
    t = normalize_asr(raw)
    if not t:
        return {"action": "ignore", "device_id": None, "fault": None, "reply": "", "source": "fuzzy"}

    if any(k in t for k in _PING_KEYS):
        return {
            "action": "ping",
            "device_id": None,
            "fault": None,
            "reply": "正在检查连通",
            "source": "fuzzy",
        }

    device_id = _parse_device_id(t)
    fault = _parse_fault(t)
    want_query = any(k in t for k in _QUERY_KEYS)
    want_show = any(k in t for k in _SHOW_KEYS) or (fault is not None and not want_query)

    # 「三号轴承故障」类：有部位/故障词 → show
    if fault is not None and not want_query:
        want_show = True

    # 画面切换（show）需要设备号；纯查询没听到编号时留 None，
    # 由执行层取电脑大屏当前选中的设备。
    if device_id is None and (want_show or fault):
        device_id = 3

    if want_show:
        return {
            "action": "show",
            "device_id": device_id or 3,
            "fault": fault or "bearing",
            "reply": "好的",
            "source": "fuzzy",
        }
    if want_query:
        return {
            "action": "query",
            "device_id": device_id,
            "fault": None,
            "reply": "好的",
            "source": "fuzzy",
        }

    # 仅有设备号：「三号呢」→ query
    if device_id is not None and len(t) <= 8:
        return {
            "action": "query",
            "device_id": device_id,
            "fault": None,
            "reply": "好的",
            "source": "fuzzy",
        }

    return {"action": "ignore", "device_id": None, "fault": None, "reply": "", "source": "fuzzy"}


class LlmIntent:
    def __init__(self, cfg: dict[str, Any] | None):
        cfg = cfg or {}
        self.enabled = bool(cfg.get("enabled", True) and cfg.get("api_key"))
        self.api_key = str(cfg.get("api_key") or "")
        base = str(cfg.get("base_url") or "").rstrip("/")
        if base.endswith("/v1"):
            self.base_url = base
        else:
            self.base_url = base + "/v1" if base else ""
        self.model = str(cfg.get("model") or "deepseek-chat")
        self.timeout = float(cfg.get("timeout_sec", 40))
        self.retries = int(cfg.get("retries", 2))

    def _chat(self, system: str, user: str, *, temperature: float = 0) -> str:
        if not self.enabled:
            raise RuntimeError("llm disabled")
        url = f"{self.base_url}/chat/completions"
        headers = {
            "Authorization": f"Bearer {self.api_key}",
            "Content-Type": "application/json",
        }
        payload = {
            "model": self.model,
            "temperature": temperature,
            "messages": [
                {"role": "system", "content": system},
                {"role": "user", "content": user},
            ],
        }
        last_err = ""
        for attempt in range(1, max(self.retries, 1) + 1):
            try:
                resp = requests.post(url, headers=headers, json=payload, timeout=self.timeout)
                resp.raise_for_status()
                return str(resp.json()["choices"][0]["message"]["content"] or "").strip()
            except Exception as exc:
                last_err = str(exc)
                print(f"[LLM] 第{attempt}次失败：{last_err}", flush=True)
        raise RuntimeError(last_err or "llm request failed")

    def parse(self, text: str) -> dict[str, Any]:
        text_n = normalize_asr(text)
        if not self.enabled:
            return fuzzy_parse(text_n)

        try:
            content = self._chat(
                SYSTEM_PROMPT,
                f"ASR原文：{text}\n规范化：{text_n}\n请按意思输出 JSON。",
                temperature=0,
            )
        except Exception as exc:
            fb = fuzzy_parse(text_n)
            if fb.get("action") != "ignore":
                fb["error"] = str(exc)
                return fb
            return {
                "action": "ignore",
                "device_id": None,
                "fault": None,
                "reply": "",
                "source": "llm_error",
                "error": str(exc),
            }

        data = self._extract_json(content)
        data["source"] = "llm"
        data["raw"] = content
        # LLM 过严判 ignore 时，用模糊规则再救一次
        if data.get("action") == "ignore":
            fb = fuzzy_parse(text_n)
            if fb.get("action") != "ignore":
                print(f"[LLM] ignore→模糊兜底：{fb}", flush=True)
                return fb
        # 用户原话里没有编号时，不让模型自己猜设备号：一律按大屏当前选中设备查
        if data.get("action") in {"query", "ping"} and _parse_device_id(text_n) is None:
            data["device_id"] = None
        # 补全缺省设备号：画面切换需要指定，查询交给程序取当前选中设备
        if data.get("action") == "show" and not data.get("device_id"):
            data["device_id"] = 3
        if data.get("action") == "show" and not data.get("fault"):
            data["fault"] = "bearing"
        return data

    def narrate(self, user_text: str, exec_out: dict[str, Any]) -> str:
        """把查询/执行结果交给模型，生成可 TTS 的人类语言讲解。失败返回空串。"""
        if not self.enabled:
            return ""
        facts = {
            "user_question": user_text,
            "ok": exec_out.get("ok"),
            "data": exec_out.get("data"),
            "steps": exec_out.get("steps"),
            "fallback_reply": exec_out.get("reply"),
        }
        user = (
            f"用户原话：{user_text}\n"
            f"设备数据 JSON：\n{json.dumps(facts, ensure_ascii=False)}"
        )
        try:
            content = self._chat(NARRATE_PROMPT, user, temperature=0.3)
        except Exception as exc:
            print(f"[LLM] 讲解生成失败：{exc}", flush=True)
            return ""
        text = content.strip()
        if text.startswith("```"):
            text = re.sub(r"^```(?:\w+)?\s*", "", text)
            text = re.sub(r"\s*```$", "", text)
        text = text.strip().strip("\"'“”")
        if len(text) > 80:
            text = text[:80].rstrip("，,。.!！？?") + "。"
        return text

    @staticmethod
    def _extract_json(content: str) -> dict[str, Any]:
        text = (content or "").strip()
        if text.startswith("```"):
            text = re.sub(r"^```(?:json)?\s*", "", text)
            text = re.sub(r"\s*```$", "", text)
        try:
            obj = json.loads(text)
        except json.JSONDecodeError:
            m = re.search(r"\{.*\}", text, re.S)
            obj = json.loads(m.group(0)) if m else {}
        action = str(obj.get("action") or "ignore")
        if action not in {"query", "show", "ping", "ignore", "wake_only"}:
            action = "ignore"
        did = obj.get("device_id")
        try:
            did = int(did) if did is not None and str(did).strip() != "" else None
        except (TypeError, ValueError):
            did = None
        fault = obj.get("fault")
        if fault is not None:
            fault = str(fault)
            if fault not in {"bearing", "imbalance", "looseness", "coupling", "none"}:
                fault = None
        return {
            "action": action,
            "device_id": did,
            "fault": fault,
            "reply": str(obj.get("reply") or ""),
        }
