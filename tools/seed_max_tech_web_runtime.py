#!/usr/bin/env python3
"""Persistent runtime state for seed_max_tech_web.

This module is imported by the Streamlit app. Streamlit reruns the app script
on every interaction, but imported modules stay loaded, so this registry keeps
run state alive across reruns.
"""

from __future__ import annotations

import threading
from dataclasses import dataclass, field
from typing import Any, Optional


@dataclass
class RuntimeRegistry:
    lock: threading.Lock = field(default_factory=threading.Lock)
    runs: dict[str, Any] = field(default_factory=dict)
    latest_run_id: Optional[str] = None


REGISTRY = RuntimeRegistry()

