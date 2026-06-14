"""Performance analyzer for protocol comparison."""

from __future__ import annotations

import asyncio
import math
import time
from dataclasses import dataclass
from typing import cast

import psutil
from tabulate import tabulate

from benchmark.models import BenchmarkResult
from benchmark.payloads import request_payload_size, response_payload_size
from shared.base import BaseTranslator
from shared.models import ProtocolType


@dataclass(slots=True)
class _ResourceSnapshot:
    cpu_peak_percent: float | None = None
    rss_peak_mb: float | None = None


class _ResourceSampler:
    """Sample process CPU and RSS while benchmark is running."""

    def __init__(self, pid: int, *, interval_seconds: float = 0.5) -> None:
        self._process = psutil.Process(pid)
        self._interval_seconds = interval_seconds
        self._snapshot = _ResourceSnapshot(cpu_peak_percent=0.0, rss_peak_mb=0.0)

    async def run_until(self, stop_event: asyncio.Event) -> _ResourceSnapshot:
        """Sample until the stop event is set."""
        self._process.cpu_percent(interval=None)
        while not stop_event.is_set():
            cpu = self._process.cpu_percent(interval=None)
            rss_mb = self._process.memory_info().rss / 1024 / 1024
            self._snapshot.cpu_peak_percent = max(
                self._snapshot.cpu_peak_percent or 0.0,
                cpu,
            )
            self._snapshot.rss_peak_mb = max(
                self._snapshot.rss_peak_mb or 0.0,
                rss_mb,
            )
            await asyncio.sleep(self._interval_seconds)
        return self._snapshot


class PerformanceAnalyzer:
    """Run benchmarks and aggregate latency/resource metrics."""

    async def run_case(
        self,
        translator: BaseTranslator,
        *,
        protocol: ProtocolType,
        queries: list[str],
        concurrency: int,
        provider_name: str,
        server_pid: int | None = None,
    ) -> BenchmarkResult:
        """Run one benchmark case against a translator client."""
        latencies_ms: list[float] = []
        semaphore = asyncio.Semaphore(concurrency)
        stop_event = asyncio.Event()
        sampler_task: asyncio.Task[_ResourceSnapshot] | None = None

        if server_pid is not None:
            sampler = _ResourceSampler(server_pid)
            sampler_task = asyncio.create_task(sampler.run_until(stop_event))

        async def run_query(word: str) -> None:
            async with semaphore:
                started = time.perf_counter_ns()
                await translator.translate(word)
                elapsed_ms = (time.perf_counter_ns() - started) / 1_000_000
                latencies_ms.append(elapsed_ms)

        started_at = time.perf_counter()
        try:
            await asyncio.gather(*(run_query(word) for word in queries))
        finally:
            stop_event.set()

        resource_snapshot = (
            await sampler_task
            if sampler_task is not None
            else _ResourceSnapshot(cpu_peak_percent=None, rss_peak_mb=None)
        )
        elapsed_seconds = time.perf_counter() - started_at

        sample_word = queries[0] if queries else "hello"
        sample_translation = await translator.translate(sample_word)

        return BenchmarkResult(
            protocol=protocol,
            total_requests=len(queries),
            concurrency=concurrency,
            qps=len(queries) / elapsed_seconds if elapsed_seconds > 0 else 0.0,
            avg_latency_ms=(
                sum(latencies_ms) / len(latencies_ms) if latencies_ms else 0.0
            ),
            p95_latency_ms=_percentile(latencies_ms, 95),
            p99_latency_ms=_percentile(latencies_ms, 99),
            cpu_peak_percent=resource_snapshot.cpu_peak_percent,
            rss_peak_mb=resource_snapshot.rss_peak_mb,
            request_payload_bytes=request_payload_size(protocol, sample_word),
            response_payload_bytes=response_payload_size(
                protocol,
                sample_word,
                sample_translation,
                provider=provider_name,
            ),
        )

    @staticmethod
    def render_table(results: list[BenchmarkResult]) -> str:
        """Render a comparison table."""
        rows = [
            [
                result.protocol.value,
                result.total_requests,
                result.concurrency,
                f"{result.qps:.2f}",
                f"{result.avg_latency_ms:.2f}",
                f"{result.p95_latency_ms:.2f}",
                f"{result.p99_latency_ms:.2f}",
                _format_optional(result.cpu_peak_percent),
                _format_optional(result.rss_peak_mb),
                result.request_payload_bytes,
                result.response_payload_bytes,
            ]
            for result in results
        ]
        headers = [
            "Protocol",
            "Requests",
            "Concurrency",
            "QPS",
            "Avg(ms)",
            "P95(ms)",
            "P99(ms)",
            "CPU Peak(%)",
            "RSS Peak(MB)",
            "Req Payload(B)",
            "Resp Payload(B)",
        ]
        return cast(str, tabulate(rows, headers=headers, tablefmt="github"))


def _percentile(values: list[float], percentile: int) -> float:
    """Compute a percentile from latency samples."""
    if not values:
        return 0.0
    ordered = sorted(values)
    rank = max(0, math.ceil(percentile / 100 * len(ordered)) - 1)
    return ordered[rank]


def _format_optional(value: float | None) -> str:
    if value is None:
        return "N/A"
    return f"{value:.2f}"
