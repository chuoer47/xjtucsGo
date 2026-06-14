"""Benchmark result models."""

from __future__ import annotations

from dataclasses import dataclass

from shared.models import ProtocolType


@dataclass(slots=True)
class BenchmarkResult:
    """Aggregated benchmark statistics."""

    protocol: ProtocolType
    total_requests: int
    concurrency: int
    qps: float
    avg_latency_ms: float
    p95_latency_ms: float
    p99_latency_ms: float
    cpu_peak_percent: float | None
    rss_peak_mb: float | None
    request_payload_bytes: int
    response_payload_bytes: int


@dataclass(slots=True)
class BenchmarkRunRecord:
    """A single benchmark run with explicit run index."""

    protocol: ProtocolType
    run_number: int
    total_requests: int
    concurrency: int
    qps: float
    avg_latency_ms: float
    p95_latency_ms: float
    p99_latency_ms: float
    cpu_peak_percent: float | None
    rss_peak_mb: float | None
    request_payload_bytes: int
    response_payload_bytes: int

    @classmethod
    def from_result(
        cls,
        result: BenchmarkResult,
        *,
        run_number: int,
    ) -> BenchmarkRunRecord:
        """Create a run record from a benchmark result."""
        return cls(
            protocol=result.protocol,
            run_number=run_number,
            total_requests=result.total_requests,
            concurrency=result.concurrency,
            qps=result.qps,
            avg_latency_ms=result.avg_latency_ms,
            p95_latency_ms=result.p95_latency_ms,
            p99_latency_ms=result.p99_latency_ms,
            cpu_peak_percent=result.cpu_peak_percent,
            rss_peak_mb=result.rss_peak_mb,
            request_payload_bytes=result.request_payload_bytes,
            response_payload_bytes=result.response_payload_bytes,
        )


@dataclass(slots=True)
class BenchmarkAggregate:
    """Average statistics grouped by protocol and concurrency."""

    protocol: ProtocolType
    concurrency: int
    runs: int
    total_requests: int
    avg_qps: float
    avg_latency_ms: float
    avg_p95_latency_ms: float
    avg_p99_latency_ms: float
    avg_cpu_peak_percent: float | None
    avg_rss_peak_mb: float | None
    request_payload_bytes: int
    response_payload_bytes: int
