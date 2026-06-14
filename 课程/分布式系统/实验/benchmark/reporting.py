"""Benchmark aggregation, export, and chart rendering helpers."""

from __future__ import annotations

import csv
import json
from collections import defaultdict
from dataclasses import asdict
from pathlib import Path
from typing import Any, cast

import matplotlib
from tabulate import tabulate

from benchmark.models import BenchmarkAggregate, BenchmarkRunRecord
from shared.models import ProtocolType

matplotlib.use("Agg")
import matplotlib.pyplot as plt

_PROTOCOL_COLORS: dict[ProtocolType, str] = {
    ProtocolType.SOCKET_JSON: "#c65d3a",
    ProtocolType.GRPC: "#1d5c8f",
}

_PROTOCOL_LABELS: dict[ProtocolType, str] = {
    ProtocolType.SOCKET_JSON: "Socket + JSON",
    ProtocolType.GRPC: "gRPC + Protobuf",
}


def aggregate_run_records(
    records: list[BenchmarkRunRecord],
) -> list[BenchmarkAggregate]:
    """Aggregate raw benchmark runs by protocol and concurrency."""
    grouped: dict[
        tuple[ProtocolType, int], list[BenchmarkRunRecord]
    ] = defaultdict(list)
    for record in records:
        grouped[(record.protocol, record.concurrency)].append(record)

    aggregates: list[BenchmarkAggregate] = []
    for (protocol, concurrency), items in sorted(
        grouped.items(),
        key=lambda entry: (entry[0][0].value, entry[0][1]),
    ):
        aggregates.append(
            BenchmarkAggregate(
                protocol=protocol,
                concurrency=concurrency,
                runs=len(items),
                total_requests=items[0].total_requests,
                avg_qps=_avg([item.qps for item in items]),
                avg_latency_ms=_avg([item.avg_latency_ms for item in items]),
                avg_p95_latency_ms=_avg([item.p95_latency_ms for item in items]),
                avg_p99_latency_ms=_avg([item.p99_latency_ms for item in items]),
                avg_cpu_peak_percent=_avg_optional(
                    [item.cpu_peak_percent for item in items]
                ),
                avg_rss_peak_mb=_avg_optional([item.rss_peak_mb for item in items]),
                request_payload_bytes=items[0].request_payload_bytes,
                response_payload_bytes=items[0].response_payload_bytes,
            )
        )

    return aggregates


def render_run_table(records: list[BenchmarkRunRecord]) -> str:
    """Render a markdown table for raw benchmark runs."""
    rows = [
        [
            record.protocol.value,
            record.run_number,
            record.total_requests,
            record.concurrency,
            f"{record.qps:.2f}",
            f"{record.avg_latency_ms:.2f}",
            f"{record.p95_latency_ms:.2f}",
            f"{record.p99_latency_ms:.2f}",
            _format_optional(record.cpu_peak_percent),
            _format_optional(record.rss_peak_mb),
            record.request_payload_bytes,
            record.response_payload_bytes,
        ]
        for record in records
    ]
    headers = [
        "Protocol",
        "Run",
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


def render_aggregate_table(aggregates: list[BenchmarkAggregate]) -> str:
    """Render a markdown table for aggregated benchmark results."""
    rows = [
        [
            aggregate.protocol.value,
            aggregate.concurrency,
            aggregate.runs,
            aggregate.total_requests,
            f"{aggregate.avg_qps:.2f}",
            f"{aggregate.avg_latency_ms:.2f}",
            f"{aggregate.avg_p95_latency_ms:.2f}",
            f"{aggregate.avg_p99_latency_ms:.2f}",
            _format_optional(aggregate.avg_cpu_peak_percent),
            _format_optional(aggregate.avg_rss_peak_mb),
            aggregate.request_payload_bytes,
            aggregate.response_payload_bytes,
        ]
        for aggregate in aggregates
    ]
    headers = [
        "Protocol",
        "Concurrency",
        "Runs",
        "Requests",
        "Avg QPS",
        "Avg(ms)",
        "Avg P95(ms)",
        "Avg P99(ms)",
        "Avg CPU Peak(%)",
        "Avg RSS Peak(MB)",
        "Req Payload(B)",
        "Resp Payload(B)",
    ]
    return cast(str, tabulate(rows, headers=headers, tablefmt="github"))


def export_benchmark_bundle(
    output_dir: Path,
    *,
    raw_records: list[BenchmarkRunRecord],
    aggregates: list[BenchmarkAggregate],
) -> None:
    """Write benchmark outputs to JSON, CSV, Markdown, and SVG charts."""
    output_dir.mkdir(parents=True, exist_ok=True)

    raw_rows = [_normalize_dict(asdict(record)) for record in raw_records]
    aggregate_rows = [_normalize_dict(asdict(item)) for item in aggregates]

    (output_dir / "raw_results.json").write_text(
        json.dumps(raw_rows, indent=2),
        encoding="utf-8",
    )
    _write_csv(output_dir / "raw_results.csv", raw_rows)
    (output_dir / "raw_results.md").write_text(
        render_run_table(raw_records),
        encoding="utf-8",
    )

    (output_dir / "aggregate_results.json").write_text(
        json.dumps(aggregate_rows, indent=2),
        encoding="utf-8",
    )
    _write_csv(output_dir / "aggregate_results.csv", aggregate_rows)
    (output_dir / "aggregate_results.md").write_text(
        render_aggregate_table(aggregates),
        encoding="utf-8",
    )

    render_metric_chart(
        aggregates,
        metric="avg_qps",
        output_path=output_dir / "qps.svg",
        title="QPS vs Concurrency",
        y_label="QPS",
    )
    render_metric_chart(
        aggregates,
        metric="avg_p95_latency_ms",
        output_path=output_dir / "p95_latency.svg",
        title="P95 Latency vs Concurrency",
        y_label="Latency (ms)",
    )
    render_metric_chart(
        aggregates,
        metric="avg_p99_latency_ms",
        output_path=output_dir / "p99_latency.svg",
        title="P99 Latency vs Concurrency",
        y_label="Latency (ms)",
    )
    render_metric_chart(
        aggregates,
        metric="avg_cpu_peak_percent",
        output_path=output_dir / "cpu_peak.svg",
        title="CPU Peak vs Concurrency",
        y_label="CPU Peak (%)",
    )
    render_metric_chart(
        aggregates,
        metric="avg_rss_peak_mb",
        output_path=output_dir / "rss_peak.svg",
        title="RSS Peak vs Concurrency",
        y_label="RSS Peak (MB)",
    )
    render_payload_chart(aggregates, output_dir / "payload_size.svg")


def render_metric_chart(
    aggregates: list[BenchmarkAggregate],
    *,
    metric: str,
    output_path: Path,
    title: str,
    y_label: str,
) -> None:
    """Render a line chart comparing protocols under a metric."""
    figure, axis = plt.subplots(figsize=(8, 5))
    for protocol in ProtocolType:
        items = [item for item in aggregates if item.protocol == protocol]
        if not items:
            continue
        xs = [item.concurrency for item in items]
        ys = [float(getattr(item, metric) or 0.0) for item in items]
        axis.plot(
            xs,
            ys,
            marker="o",
            linewidth=2,
            color=_PROTOCOL_COLORS[protocol],
            label=_PROTOCOL_LABELS[protocol],
        )

    axis.set_title(title)
    axis.set_xlabel("Concurrency")
    axis.set_ylabel(y_label)
    axis.grid(True, linestyle="--", alpha=0.35)
    axis.legend()
    figure.tight_layout()
    figure.savefig(output_path, format="svg")
    plt.close(figure)


def render_payload_chart(
    aggregates: list[BenchmarkAggregate],
    output_path: Path,
) -> None:
    """Render a payload size comparison bar chart."""
    ordered = sorted(
        {item.protocol: item for item in aggregates}.values(),
        key=lambda item: item.protocol.value,
    )
    protocols = [_PROTOCOL_LABELS[item.protocol] for item in ordered]
    request_payloads = [item.request_payload_bytes for item in ordered]
    response_payloads = [item.response_payload_bytes for item in ordered]

    figure, axis = plt.subplots(figsize=(8, 5))
    positions = list(range(len(protocols)))
    width = 0.35
    axis.bar(
        [position - width / 2 for position in positions],
        request_payloads,
        width=width,
        label="Request Payload",
        color="#e9b44c",
    )
    axis.bar(
        [position + width / 2 for position in positions],
        response_payloads,
        width=width,
        label="Response Payload",
        color="#386641",
    )
    axis.set_xticks(positions)
    axis.set_xticklabels(protocols)
    axis.set_ylabel("Bytes")
    axis.set_title("Payload Size Comparison")
    axis.legend()
    axis.grid(True, axis="y", linestyle="--", alpha=0.35)
    figure.tight_layout()
    figure.savefig(output_path, format="svg")
    plt.close(figure)


def _write_csv(path: Path, rows: list[dict[str, Any]]) -> None:
    if not rows:
        path.write_text("", encoding="utf-8")
        return

    with path.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=list(rows[0].keys()))
        writer.writeheader()
        writer.writerows(rows)


def _normalize_dict(payload: dict[str, Any]) -> dict[str, Any]:
    normalized: dict[str, Any] = {}
    for key, value in payload.items():
        if isinstance(value, ProtocolType):
            normalized[key] = value.value
        else:
            normalized[key] = value
    return normalized


def _avg(values: list[float]) -> float:
    return sum(values) / len(values) if values else 0.0


def _avg_optional(values: list[float | None]) -> float | None:
    concrete = [value for value in values if value is not None]
    if not concrete:
        return None
    return _avg(concrete)


def _format_optional(value: float | None) -> str:
    if value is None:
        return "N/A"
    return f"{value:.2f}"
