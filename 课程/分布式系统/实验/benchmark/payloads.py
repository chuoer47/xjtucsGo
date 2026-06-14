"""Payload sizing helpers."""

from __future__ import annotations

from shared import translator_pb2
from shared.models import ProtocolType, TranslationRequest, TranslationResponse
from shared.socket_protocol import json_payload_size


def request_payload_size(protocol: ProtocolType, word: str) -> int:
    """Return the serialized request payload size."""
    request = TranslationRequest(request_id="benchmark", word=word)
    if protocol == ProtocolType.SOCKET_JSON:
        return json_payload_size(request)
    if protocol == ProtocolType.GRPC:
        message = translator_pb2.TranslateRequest(  # type: ignore[attr-defined]
            request_id=request.request_id,
            word=request.word,
        )
        return int(message.ByteSize())
    raise ValueError(f"unsupported protocol: {protocol}")


def response_payload_size(
    protocol: ProtocolType,
    word: str,
    translation: str,
    *,
    provider: str,
    found: bool = True,
) -> int:
    """Return the serialized response payload size."""
    response = TranslationResponse(
        request_id="benchmark",
        word=word,
        translation=translation,
        protocol=protocol,
        provider=provider,
        found=found,
    )
    if protocol == ProtocolType.SOCKET_JSON:
        return json_payload_size(response)
    if protocol == ProtocolType.GRPC:
        message = translator_pb2.TranslateResponse(  # type: ignore[attr-defined]
            request_id=response.request_id,
            word=response.word,
            translation=response.translation,
            protocol=response.protocol.value,
            provider=response.provider,
            found=response.found,
        )
        return int(message.ByteSize())
    raise ValueError(f"unsupported protocol: {protocol}")

