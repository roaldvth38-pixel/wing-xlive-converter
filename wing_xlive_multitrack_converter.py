#!/usr/bin/env python3
"""Convert Behringer WING X-LIVE recording folders into multitrack WAVs."""

from __future__ import annotations

import argparse
import concurrent.futures
import contextlib
import dataclasses
import logging
import os
import re
import shutil
import subprocess
import sys
import threading
import time
import wave
from collections import defaultdict
from pathlib import Path
from typing import Dict, Iterable, List, Optional, Sequence, Set, Tuple, Union

WAV_EXTENSIONS = {".wav", ".wave"}
STITCH_WAV_BLOCK_FRAMES = 65536
STITCH_ENCODE_BLOCK_FRAMES = 262144
MAX_AUTO_ENCODER_JOBS = 12

CARD_FOLDER_RE = re.compile(r"^(?:sd|card|slot)[ _-]?(?:a|b|1|2)$", re.IGNORECASE)
TRACK_FOLDER_RE = re.compile(r"^(?:ch|trk|track|channel)[ _-]*0*\d{1,2}$", re.IGNORECASE)
COMMON_CONTAINER_RE = re.compile(r"^(?:audio|wav|tracks?)$", re.IGNORECASE)
HEX_CHUNK_NAME_RE = re.compile(r"^[0-9a-fA-F]{8}$")
INVALID_FILENAME_CHARS_RE = re.compile(r"[<>:\"/\\|?*\x00-\x1F]")

KEYWORD_TRACK_RE = re.compile(r"(?:ch|trk|track|channel)[ _-]*0*(\d{1,2})", re.IGNORECASE)
SIMPLE_TRACK_RE = re.compile(r"^0*(\d{1,2})(?:[_.-]+0*(\d{1,6}))?$")
LEADING_TRACK_WITH_TRAILING_INDEX_RE = re.compile(r"^0*(\d{1,2})[_.-]+.*?[_.-]+0*(\d{1,6})$")

WAV_BIT_DEPTH_CHOICES = {8, 16, 24, 32}
FLAC_ENCODING_DEPTH_CHOICES = {
    "24",
    "23/24",
    "22/24",
    "21/24",
    "20/24",
    "19/24",
    "18/24",
    "17/24",
    "16",
}

_OPTIONAL_NUMPY_LOADED = False
_OPTIONAL_NUMPY_MODULE = None
_OPTIONAL_FFMPEG_LOADED = False
_OPTIONAL_FFMPEG_PATH: Optional[Path] = None


def get_optional_numpy_module() -> Optional[object]:
    global _OPTIONAL_NUMPY_LOADED
    global _OPTIONAL_NUMPY_MODULE

    if _OPTIONAL_NUMPY_LOADED:
        return _OPTIONAL_NUMPY_MODULE

    _OPTIONAL_NUMPY_LOADED = True
    try:
        import numpy as np  # type: ignore

        _OPTIONAL_NUMPY_MODULE = np
    except Exception:  # pylint: disable=broad-except
        _OPTIONAL_NUMPY_MODULE = None

    return _OPTIONAL_NUMPY_MODULE


def get_optional_ffmpeg_path() -> Optional[Path]:
    global _OPTIONAL_FFMPEG_LOADED
    global _OPTIONAL_FFMPEG_PATH

    if _OPTIONAL_FFMPEG_LOADED:
        return _OPTIONAL_FFMPEG_PATH

    _OPTIONAL_FFMPEG_LOADED = True

    base_dir = Path(sys.executable).resolve().parent if getattr(sys, "frozen", False) else Path(__file__).resolve().parent
    candidates = [
        base_dir / "ffmpeg.exe",
        base_dir / "ffmpeg",
        base_dir / "dist" / "ffmpeg.exe",
        base_dir / "dist" / "ffmpeg",
        Path.cwd() / "ffmpeg.exe",
        Path.cwd() / "ffmpeg",
    ]

    for candidate in candidates:
        if candidate.exists() and candidate.is_file():
            _OPTIONAL_FFMPEG_PATH = candidate.resolve()
            return _OPTIONAL_FFMPEG_PATH

    from_path = shutil.which("ffmpeg")
    if from_path:
        _OPTIONAL_FFMPEG_PATH = Path(from_path).resolve()
        return _OPTIONAL_FFMPEG_PATH

    _OPTIONAL_FFMPEG_PATH = None
    return None


def ffmpeg_pcm_input_format(sample_width: int) -> str:
    mapping = {
        1: "u8",
        2: "s16le",
        3: "s24le",
        4: "s32le",
    }
    if sample_width not in mapping:
        raise RuntimeError(f"Unsupported PCM sample width for ffmpeg: {sample_width} byte(s).")
    return mapping[sample_width]


@dataclasses.dataclass(frozen=True)
class SourceClip:
    path: Path
    take_root: Path
    track_number: int
    chunk_index: Optional[int]


@dataclasses.dataclass
class ConversionSummary:
    takes_converted: int = 0
    tracks_written: int = 0
    clips_consumed: int = 0
    skipped_files: List[Path] = dataclasses.field(default_factory=list)


@dataclasses.dataclass
class OutputSettings:
    format: str = "wav"
    wav_keep_original_bit_depth: bool = True
    wav_bit_depth: int = 24
    flac_encoding_depth: str = "24"
    flac_compression_level: int = 5
    mp3_mode: str = "cbr"
    mp3_bitrate_kbps: int = 320
    mp3_quality: int = 0
    mp3_write_replaygain: bool = False
    encoder_jobs: int = 0
    track_name_overrides: Dict[int, str] = dataclasses.field(default_factory=dict)


class ConversionCancelled(RuntimeError):
    """Raised when conversion is stopped by a user request."""


@dataclasses.dataclass
class ConversionControl:
    stop_event: Optional[threading.Event] = None
    pause_event: Optional[threading.Event] = None
    pause_poll_seconds: float = 0.2


@dataclasses.dataclass(frozen=True)
class ChunkedTake:
    take_root: Path
    chunk_files: Sequence[Path]
    channel_count: int
    sample_rate: int
    sample_width: int
    comptype: str
    compname: str
    total_frames: int


def honor_conversion_control(control: Optional[ConversionControl]) -> None:
    if control is None:
        return

    stop_event = control.stop_event
    pause_event = control.pause_event
    poll_seconds = max(0.05, float(control.pause_poll_seconds))

    if stop_event is not None and stop_event.is_set():
        raise ConversionCancelled("Conversion stopped by user.")

    while pause_event is not None and pause_event.is_set():
        if stop_event is not None and stop_event.is_set():
            raise ConversionCancelled("Conversion stopped by user.")
        time.sleep(poll_seconds)


def sanitize_track_name(name: str) -> str:
    cleaned = INVALID_FILENAME_CHARS_RE.sub("_", str(name)).strip()
    cleaned = re.sub(r"\s+", " ", cleaned)
    cleaned = cleaned.rstrip(". ")
    if len(cleaned) > 120:
        cleaned = cleaned[:120].rstrip(". ")
    return cleaned


def normalize_track_name_overrides(raw_overrides: object) -> Dict[int, str]:
    if not isinstance(raw_overrides, dict):
        return {}

    normalized: Dict[int, str] = {}
    for raw_key, raw_value in raw_overrides.items():
        try:
            track_number = int(raw_key)
        except (TypeError, ValueError):
            continue

        if not 1 <= track_number <= 64:
            continue

        name = sanitize_track_name(str(raw_value))
        if not name:
            continue

        normalized[track_number] = name

    return normalized


def build_track_output_filename(
    track_number: int,
    extension: str,
    track_name_overrides: Dict[int, str],
    used_stems: Set[str],
) -> str:
    default_stem = f"Track{track_number:02d}"
    custom_stem = sanitize_track_name(track_name_overrides.get(track_number, ""))
    stem = custom_stem or default_stem

    candidate = stem
    if candidate in used_stems:
        candidate = f"{stem}_{track_number:02d}"

    suffix = 2
    while candidate in used_stems:
        candidate = f"{stem}_{track_number:02d}_{suffix}"
        suffix += 1

    used_stems.add(candidate)
    return f"{candidate}.{extension}"


def normalize_output_settings(output_settings: Optional[OutputSettings]) -> OutputSettings:
    settings = output_settings or OutputSettings()

    output_format = (settings.format or "wav").strip().lower()
    if output_format not in {"wav", "flac", "mp3"}:
        raise ValueError("Output format must be wav, flac, or mp3.")

    wav_keep_original = bool(settings.wav_keep_original_bit_depth)
    wav_bit_depth = int(settings.wav_bit_depth)
    if wav_bit_depth not in WAV_BIT_DEPTH_CHOICES:
        raise ValueError("WAV bit depth must be one of: 8, 16, 24, 32.")

    flac_encoding_depth = str(settings.flac_encoding_depth or "24").strip()
    if flac_encoding_depth not in FLAC_ENCODING_DEPTH_CHOICES:
        raise ValueError(
            "FLAC encoding depth must be one of: 24, 23/24, 22/24, 21/24, 20/24, 19/24, 18/24, 17/24, 16."
        )

    flac_compression_level = max(0, min(8, int(settings.flac_compression_level)))

    mp3_mode = (settings.mp3_mode or "cbr").strip().lower()
    if mp3_mode not in {"cbr", "vbr"}:
        raise ValueError("MP3 mode must be cbr or vbr.")

    mp3_bitrate_kbps = max(64, min(320, int(settings.mp3_bitrate_kbps)))
    mp3_quality = max(0, min(9, int(settings.mp3_quality)))
    encoder_jobs = max(0, int(getattr(settings, "encoder_jobs", 0)))
    track_name_overrides = normalize_track_name_overrides(getattr(settings, "track_name_overrides", {}))

    return OutputSettings(
        format=output_format,
        wav_keep_original_bit_depth=wav_keep_original,
        wav_bit_depth=wav_bit_depth,
        flac_encoding_depth=flac_encoding_depth,
        flac_compression_level=flac_compression_level,
        mp3_mode=mp3_mode,
        mp3_bitrate_kbps=mp3_bitrate_kbps,
        mp3_quality=mp3_quality,
        mp3_write_replaygain=bool(settings.mp3_write_replaygain),
        encoder_jobs=encoder_jobs,
        track_name_overrides=track_name_overrides,
    )


def resolve_encoder_jobs(output_settings: OutputSettings, max_parallel_items: int) -> int:
    max_parallel_items = max(1, int(max_parallel_items))

    if output_settings.format == "wav":
        return 1

    if output_settings.encoder_jobs > 0:
        return max(1, min(output_settings.encoder_jobs, max_parallel_items))

    cpu_count = os.cpu_count() or 1
    auto_jobs = max(1, min(cpu_count, MAX_AUTO_ENCODER_JOBS))
    return max(1, min(auto_jobs, max_parallel_items))


def output_extension(output_format: str) -> str:
    return {
        "wav": "wav",
        "flac": "flac",
        "mp3": "mp3",
    }[output_format]


def bit_depth_to_sample_width(bit_depth: int) -> int:
    mapping = {
        8: 1,
        16: 2,
        24: 3,
        32: 4,
    }
    if bit_depth not in mapping:
        raise RuntimeError(f"Unsupported bit depth: {bit_depth}")
    return mapping[bit_depth]


def decode_pcm_samples(raw_data: bytes, sample_width: int) -> List[int]:
    if sample_width == 1:
        return [byte - 128 for byte in raw_data]

    if sample_width == 2:
        samples: List[int] = []
        for index in range(0, len(raw_data), 2):
            value = int.from_bytes(raw_data[index : index + 2], byteorder="little", signed=True)
            samples.append(value)
        return samples

    if sample_width == 3:
        samples = []
        for index in range(0, len(raw_data), 3):
            value = raw_data[index] | (raw_data[index + 1] << 8) | (raw_data[index + 2] << 16)
            if value & 0x800000:
                value -= 0x1000000
            samples.append(value)
        return samples

    if sample_width == 4:
        samples = []
        for index in range(0, len(raw_data), 4):
            value = int.from_bytes(raw_data[index : index + 4], byteorder="little", signed=True)
            samples.append(value)
        return samples

    raise RuntimeError(f"Unsupported PCM sample width: {sample_width} byte(s).")


def numpy_samples_to_pcm_bytes(samples, sample_width: int, np_module: object) -> bytes:
    np = np_module

    if sample_width == 1:
        clipped = np.clip(samples, -128, 127).astype(np.int16, copy=False)
        unsigned = (clipped + 128).astype(np.uint8, copy=False)
        return unsigned.tobytes()

    if sample_width == 2:
        clipped = np.clip(samples, -32768, 32767).astype("<i2", copy=False)
        return clipped.tobytes()

    if sample_width == 3:
        minimum = -(1 << 23)
        maximum = (1 << 23) - 1
        clipped = np.clip(samples, minimum, maximum).astype(np.int32, copy=False)
        unsigned = np.where(clipped < 0, clipped + (1 << 24), clipped).astype(np.uint32, copy=False)
        packed = np.empty((unsigned.size, 3), dtype=np.uint8)
        packed[:, 0] = unsigned & 0xFF
        packed[:, 1] = (unsigned >> 8) & 0xFF
        packed[:, 2] = (unsigned >> 16) & 0xFF
        return packed.reshape(-1).tobytes()

    if sample_width == 4:
        minimum = -(1 << 31)
        maximum = (1 << 31) - 1
        clipped = np.clip(samples, minimum, maximum).astype("<i4", copy=False)
        return clipped.tobytes()

    raise RuntimeError(f"Unsupported PCM sample width: {sample_width} byte(s).")


def convert_pcm_bit_depth_numpy(raw_data: bytes, source_width: int, target_width: int, np_module: object) -> bytes:
    np = np_module
    source_bits = source_width * 8
    target_bits = target_width * 8

    samples = pcm_bytes_to_numpy_samples(raw_data, source_width, np).astype(np.int64, copy=False)

    if target_bits > source_bits:
        shift = target_bits - source_bits
        converted = samples << shift
    else:
        shift = source_bits - target_bits
        divisor = 1 << shift
        converted = np.empty_like(samples, dtype=np.int64)
        non_negative_mask = samples >= 0
        converted[non_negative_mask] = (samples[non_negative_mask] + (divisor // 2)) // divisor
        negative = -samples[~non_negative_mask]
        converted[~non_negative_mask] = -((negative + (divisor // 2)) // divisor)

    return numpy_samples_to_pcm_bytes(converted, target_width, np)


def apply_effective_bit_depth_numpy(raw_data: bytes, sample_width: int, effective_bits: int, np_module: object) -> bytes:
    np = np_module
    source_bits = sample_width * 8
    drop_bits = source_bits - effective_bits

    samples = pcm_bytes_to_numpy_samples(raw_data, sample_width, np).astype(np.int64, copy=False)
    quantized = (samples >> drop_bits) << drop_bits
    return numpy_samples_to_pcm_bytes(quantized, sample_width, np)


def encode_pcm_samples(samples: Sequence[int], sample_width: int) -> bytes:
    output = bytearray()

    if sample_width == 1:
        for sample in samples:
            value = max(-128, min(127, int(sample))) + 128
            output.append(value)
        return bytes(output)

    if sample_width == 2:
        for sample in samples:
            value = max(-32768, min(32767, int(sample)))
            output.extend(int(value).to_bytes(2, byteorder="little", signed=True))
        return bytes(output)

    if sample_width == 3:
        minimum = -(1 << 23)
        maximum = (1 << 23) - 1
        for sample in samples:
            value = max(minimum, min(maximum, int(sample)))
            if value < 0:
                value += 1 << 24
            output.extend((value & 0xFFFFFF).to_bytes(3, byteorder="little", signed=False))
        return bytes(output)

    if sample_width == 4:
        minimum = -(1 << 31)
        maximum = (1 << 31) - 1
        for sample in samples:
            value = max(minimum, min(maximum, int(sample)))
            output.extend(int(value).to_bytes(4, byteorder="little", signed=True))
        return bytes(output)

    raise RuntimeError(f"Unsupported PCM sample width: {sample_width} byte(s).")


def convert_pcm_bit_depth(raw_data: bytes, source_width: int, target_width: int) -> bytes:
    if source_width == target_width or not raw_data:
        return raw_data

    optional_np = get_optional_numpy_module()
    if optional_np is not None:
        return convert_pcm_bit_depth_numpy(raw_data, source_width, target_width, optional_np)

    source_bits = source_width * 8
    target_bits = target_width * 8

    samples = decode_pcm_samples(raw_data, source_width)
    converted: List[int] = []

    if target_bits > source_bits:
        shift = target_bits - source_bits
        converted = [sample << shift for sample in samples]
    else:
        shift = source_bits - target_bits
        divisor = 1 << shift
        for sample in samples:
            if sample >= 0:
                converted.append((sample + (divisor // 2)) // divisor)
            else:
                converted.append(-(((-sample) + (divisor // 2)) // divisor))

    return encode_pcm_samples(converted, target_width)


def apply_effective_bit_depth(raw_data: bytes, sample_width: int, effective_bits: int) -> bytes:
    if not raw_data:
        return raw_data

    source_bits = sample_width * 8
    effective_bits = max(1, min(source_bits, int(effective_bits)))
    if effective_bits == source_bits:
        return raw_data

    optional_np = get_optional_numpy_module()
    if optional_np is not None:
        return apply_effective_bit_depth_numpy(raw_data, sample_width, effective_bits, optional_np)

    drop_bits = source_bits - effective_bits
    samples = decode_pcm_samples(raw_data, sample_width)
    quantized = [(sample >> drop_bits) << drop_bits for sample in samples]
    return encode_pcm_samples(quantized, sample_width)


def split_interleaved_24bit_block_numpy(
    raw_block: bytes,
    channel_count: int,
    output_handles: Sequence[OutputHandle],
    target_sample_width: int,
    np_module: object,
    parallel_executor: Optional[concurrent.futures.ThreadPoolExecutor] = None,
) -> None:
    np = np_module
    frame_size = channel_count * 3
    if frame_size <= 0:
        return

    usable_bytes = len(raw_block) - (len(raw_block) % frame_size)
    if usable_bytes <= 0:
        return

    interleaved = np.frombuffer(memoryview(raw_block)[:usable_bytes], dtype=np.uint8)
    interleaved = interleaved.reshape(-1, channel_count, 3)

    # Fast direct path for 24-bit -> 16-bit conversion (used by MP3 and FLAC 16-bit).
    if target_sample_width == 2:
        samples = (
            interleaved[:, :, 0].astype(np.int32)
            | (interleaved[:, :, 1].astype(np.int32) << 8)
            | (interleaved[:, :, 2].astype(np.int32) << 16)
        )
        sign_mask = (samples & 0x800000) != 0
        samples[sign_mask] -= 0x1000000

        divisor = 1 << 8
        converted = np.empty_like(samples, dtype=np.int32)
        non_negative_mask = samples >= 0
        converted[non_negative_mask] = (samples[non_negative_mask] + (divisor // 2)) // divisor
        negative = -samples[~non_negative_mask]
        converted[~non_negative_mask] = -((negative + (divisor // 2)) // divisor)

        converted = np.clip(converted, -32768, 32767).astype("<i2", copy=False)
        channel_payloads = [
            converted[:, channel_index].reshape(-1).tobytes()
            for channel_index in range(channel_count)
        ]

        if parallel_executor is not None and channel_count > 1:
            futures = [
                parallel_executor.submit(output_handles[channel_index].writeframesraw, channel_payloads[channel_index])
                for channel_index in range(channel_count)
            ]
            for future in futures:
                future.result()
        else:
            for channel_index in range(channel_count):
                output_handles[channel_index].writeframesraw(channel_payloads[channel_index])
        return

    channel_payloads = []
    for channel_index in range(channel_count):
        channel_bytes = interleaved[:, channel_index, :].reshape(-1).tobytes()
        if target_sample_width != 3:
            channel_bytes = convert_pcm_bit_depth(channel_bytes, 3, target_sample_width)
        channel_payloads.append(channel_bytes)

    if parallel_executor is not None and channel_count > 1:
        futures = [
            parallel_executor.submit(output_handles[channel_index].writeframesraw, channel_payloads[channel_index])
            for channel_index in range(channel_count)
        ]
        for future in futures:
            future.result()
    else:
        for channel_index in range(channel_count):
            output_handles[channel_index].writeframesraw(channel_payloads[channel_index])


def flac_effective_bit_depth(depth_label: str) -> int:
    normalized = str(depth_label or "24").strip()
    if "/" in normalized:
        normalized = normalized.split("/", 1)[0]

    value = int(normalized)
    return max(1, min(24, value))


def _import_inprocess_flac_modules() -> Tuple[object, object]:
    try:
        import numpy as np  # type: ignore
        import soundfile as sf  # type: ignore
    except Exception as exc:  # pylint: disable=broad-except
        raise RuntimeError(
            "In-process FLAC encoder is unavailable. Install Python packages: numpy, soundfile."
        ) from exc

    return np, sf


def _import_inprocess_mp3_module() -> object:
    try:
        import lameenc  # type: ignore
    except Exception as exc:  # pylint: disable=broad-except
        raise RuntimeError(
            "In-process MP3 encoder is unavailable. Install Python package: lameenc."
        ) from exc

    return lameenc


def pcm_bytes_to_numpy_samples(raw_data: bytes, sample_width: int, np_module: object):
    np = np_module
    if sample_width == 1:
        samples = np.frombuffer(raw_data, dtype=np.uint8).astype(np.int16)
        samples -= 128
        return samples

    if sample_width == 2:
        return np.frombuffer(raw_data, dtype="<i2")

    if sample_width == 3:
        if not raw_data:
            return np.empty((0,), dtype=np.int32)
        packed = np.frombuffer(raw_data, dtype=np.uint8).reshape(-1, 3)
        samples = (
            packed[:, 0].astype(np.int32)
            | (packed[:, 1].astype(np.int32) << 8)
            | (packed[:, 2].astype(np.int32) << 16)
        )
        sign_mask = (samples & 0x800000) != 0
        samples[sign_mask] -= 0x1000000
        return samples

    if sample_width == 4:
        return np.frombuffer(raw_data, dtype="<i4")

    raise RuntimeError(f"Unsupported PCM sample width for in-process encoder: {sample_width} byte(s).")


class InProcessFlacWriter:
    def __init__(
        self,
        output_file: Path,
        channel_count: int,
        sample_width: int,
        sample_rate: int,
        output_settings: OutputSettings,
    ) -> None:
        np, sf = _import_inprocess_flac_modules()

        output_file.parent.mkdir(parents=True, exist_ok=True)

        self._np = np
        self._channel_count = channel_count
        self._source_width = sample_width
        self._effective_bits = min(sample_width * 8, flac_effective_bit_depth(output_settings.flac_encoding_depth))
        self._target_width = 2 if self._effective_bits <= 16 else 3
        self._closed = False
        self._output_file = output_file

        subtype = "PCM_16" if self._target_width == 2 else "PCM_24"
        self._file = sf.SoundFile(
            str(output_file),
            mode="w",
            samplerate=sample_rate,
            channels=channel_count,
            format="FLAC",
            subtype=subtype,
        )

    def writeframesraw(self, frames: bytes) -> None:
        if not frames or self._closed:
            return

        pcm = frames
        if self._effective_bits < (self._source_width * 8):
            pcm = apply_effective_bit_depth(pcm, self._source_width, self._effective_bits)

        if self._source_width != self._target_width:
            pcm = convert_pcm_bit_depth(pcm, self._source_width, self._target_width)

        sample_array = pcm_bytes_to_numpy_samples(pcm, self._target_width, self._np)
        if self._channel_count > 1:
            sample_array = sample_array.reshape(-1, self._channel_count)

        self._file.write(sample_array)

    def close(self) -> None:
        if self._closed:
            return

        self._closed = True
        self._file.close()


class InProcessMp3Writer:
    def __init__(
        self,
        output_file: Path,
        channel_count: int,
        sample_width: int,
        sample_rate: int,
        output_settings: OutputSettings,
    ) -> None:
        if channel_count not in (1, 2):
            raise RuntimeError("In-process MP3 encoder supports mono or stereo only.")

        lameenc = _import_inprocess_mp3_module()
        output_file.parent.mkdir(parents=True, exist_ok=True)

        self._source_width = sample_width
        self._closed = False
        self._output_file = output_file
        self._handle = output_file.open("wb")

        encoder = lameenc.Encoder()
        encoder.set_in_sample_rate(sample_rate)
        encoder.set_out_sample_rate(sample_rate)
        encoder.set_channels(channel_count)

        if output_settings.mp3_mode == "cbr":
            encoder.set_vbr(lameenc.VBR_OFF)
            encoder.set_bit_rate(output_settings.mp3_bitrate_kbps)
        else:
            encoder.set_vbr(lameenc.VBR_MTRH)
            encoder.set_vbr_quality(float(max(0, min(9, int(output_settings.mp3_quality)))))
            encoder.set_vbr_min_bitrate_kbps(64)
            encoder.set_vbr_max_bitrate_kbps(320)
            encoder.set_vbr_mean_bitrate_kbps(output_settings.mp3_bitrate_kbps)
            encoder.set_vbr_hard_min(False)

        encoder.set_quality(max(0, min(9, int(output_settings.mp3_quality))))
        self._encoder = encoder

    def writeframesraw(self, frames: bytes) -> None:
        if not frames or self._closed:
            return

        pcm = frames
        if self._source_width != 2:
            pcm = convert_pcm_bit_depth(pcm, self._source_width, 2)

        encoded = self._encoder.encode(pcm)
        if encoded:
            self._handle.write(encoded)

    def close(self) -> None:
        if self._closed:
            return

        self._closed = True

        trailing = self._encoder.flush()
        if trailing:
            self._handle.write(trailing)

        self._handle.close()


class FfmpegPipeWriter:
    def __init__(
        self,
        ffmpeg_path: Path,
        output_file: Path,
        channel_count: int,
        sample_width: int,
        sample_rate: int,
        output_settings: OutputSettings,
    ) -> None:
        output_file.parent.mkdir(parents=True, exist_ok=True)

        self._source_width = sample_width
        self._closed = False
        self._output_file = output_file
        self._output_format = output_settings.format
        self._effective_bits = (
            min(sample_width * 8, flac_effective_bit_depth(output_settings.flac_encoding_depth))
            if output_settings.format == "flac"
            else (sample_width * 8)
        )

        command = [
            str(ffmpeg_path),
            "-hide_banner",
            "-loglevel",
            "error",
            "-y",
            "-f",
            ffmpeg_pcm_input_format(sample_width),
            "-ar",
            str(sample_rate),
            "-ac",
            str(channel_count),
            "-i",
            "pipe:0",
        ]

        if output_settings.format == "flac":
            command.extend(
                [
                    "-c:a",
                    "flac",
                    "-compression_level",
                    str(output_settings.flac_compression_level),
                ]
            )
        elif output_settings.format == "mp3":
            command.extend(["-c:a", "libmp3lame"])
            if output_settings.mp3_mode == "cbr":
                bitrate = f"{output_settings.mp3_bitrate_kbps}k"
                command.extend(["-b:a", bitrate])
            else:
                command.extend(
                    [
                        "-q:a",
                        str(max(0, min(9, int(output_settings.mp3_quality)))),
                        "-b:a",
                        f"{output_settings.mp3_bitrate_kbps}k",
                    ]
                )
        else:
            raise RuntimeError(f"Unsupported ffmpeg output format: {output_settings.format}")

        command.append(str(output_file))

        try:
            popen_kwargs = {
                "stdin": subprocess.PIPE,
                "stdout": subprocess.DEVNULL,
                "stderr": subprocess.PIPE,
            }

            if os.name == "nt":
                creationflags = getattr(subprocess, "CREATE_NO_WINDOW", 0)
                if creationflags:
                    popen_kwargs["creationflags"] = creationflags

                startupinfo = subprocess.STARTUPINFO()
                startupinfo.dwFlags |= getattr(subprocess, "STARTF_USESHOWWINDOW", 0)
                startupinfo.wShowWindow = getattr(subprocess, "SW_HIDE", 0)
                popen_kwargs["startupinfo"] = startupinfo

            self._process = subprocess.Popen(command, **popen_kwargs)
        except Exception as exc:  # pylint: disable=broad-except
            raise RuntimeError(f"Failed to start ffmpeg for {output_file.name}: {exc}") from exc

    def writeframesraw(self, frames: bytes) -> None:
        if not frames or self._closed:
            return

        pcm = frames
        if self._output_format == "flac" and self._effective_bits < (self._source_width * 8):
            pcm = apply_effective_bit_depth(pcm, self._source_width, self._effective_bits)

        stdin = self._process.stdin
        if stdin is None:
            raise RuntimeError(f"ffmpeg stdin is unavailable for {self._output_file.name}.")

        try:
            stdin.write(pcm)
        except BrokenPipeError as exc:
            raise RuntimeError(f"ffmpeg terminated early while encoding {self._output_file.name}.") from exc

    def close(self) -> None:
        if self._closed:
            return

        self._closed = True

        stderr_output = ""
        if self._process.stdin is not None:
            self._process.stdin.close()

        if self._process.stderr is not None:
            stderr_bytes = self._process.stderr.read()
            stderr_output = stderr_bytes.decode("utf-8", errors="replace")

        return_code = self._process.wait()
        if return_code != 0:
            tail = stderr_output.strip()
            if tail:
                tail = tail[-1200:]
            raise RuntimeError(
                f"ffmpeg encoding failed for {self._output_file.name} (exit code {return_code})."
                + (f"\n{tail}" if tail else "")
            )


def create_non_wav_writer(
    output_file: Path,
    channel_count: int,
    sample_width: int,
    sample_rate: int,
    output_settings: OutputSettings,
) -> Union["FfmpegPipeWriter", "InProcessFlacWriter", "InProcessMp3Writer"]:
    ffmpeg_path = get_optional_ffmpeg_path()
    if ffmpeg_path is not None:
        return FfmpegPipeWriter(
            ffmpeg_path=ffmpeg_path,
            output_file=output_file,
            channel_count=channel_count,
            sample_width=sample_width,
            sample_rate=sample_rate,
            output_settings=output_settings,
        )

    if output_settings.format == "flac":
        return InProcessFlacWriter(
            output_file=output_file,
            channel_count=channel_count,
            sample_width=sample_width,
            sample_rate=sample_rate,
            output_settings=output_settings,
        )

    if output_settings.format == "mp3":
        return InProcessMp3Writer(
            output_file=output_file,
            channel_count=channel_count,
            sample_width=sample_width,
            sample_rate=sample_rate,
            output_settings=output_settings,
        )

    raise RuntimeError(f"Unsupported non-WAV format: {output_settings.format}")


OutputHandle = Union[wave.Wave_write, FfmpegPipeWriter, InProcessFlacWriter, InProcessMp3Writer]


def natural_key(text: str) -> List[object]:
    tokens = re.split(r"(\d+)", text.lower())
    key: List[object] = []
    for token in tokens:
        if token.isdigit():
            key.append(int(token))
        else:
            key.append(token)
    return key


def format_duration(total_seconds: float) -> str:
    total_seconds_int = max(0, int(round(total_seconds)))
    hours = total_seconds_int // 3600
    minutes = (total_seconds_int % 3600) // 60
    seconds = total_seconds_int % 60
    return f"{hours:02d}:{minutes:02d}:{seconds:02d}"


def is_within_path(path: Path, possible_parent: Path) -> bool:
    try:
        path.relative_to(possible_parent)
        return True
    except ValueError:
        return False


def is_auxiliary_folder(name: str) -> bool:
    return bool(
        CARD_FOLDER_RE.match(name)
        or TRACK_FOLDER_RE.match(name)
        or COMMON_CONTAINER_RE.match(name)
    )


def infer_take_root(wav_file: Path, input_root: Path) -> Path:
    candidate = wav_file.parent
    while candidate != input_root and is_auxiliary_folder(candidate.name):
        candidate = candidate.parent
    return candidate


def parse_track_and_chunk(wav_file: Path) -> Tuple[Optional[int], Optional[int]]:
    stem = wav_file.stem
    normalized = stem.replace(" ", "_")

    keyword_match = KEYWORD_TRACK_RE.search(normalized)
    if keyword_match:
        track_number = int(keyword_match.group(1))
        if 1 <= track_number <= 64:
            trailing = normalized[keyword_match.end() :]
            trailing_numbers = [int(value) for value in re.findall(r"\d+", trailing)]
            chunk_index = trailing_numbers[-1] if trailing_numbers else None
            return track_number, chunk_index

    simple_match = SIMPLE_TRACK_RE.match(normalized)
    if simple_match:
        track_number = int(simple_match.group(1))
        if 1 <= track_number <= 64:
            chunk_group = simple_match.group(2)
            chunk_index = int(chunk_group) if chunk_group is not None else None
            return track_number, chunk_index

    leading_with_trailing = LEADING_TRACK_WITH_TRAILING_INDEX_RE.match(normalized)
    if leading_with_trailing:
        track_number = int(leading_with_trailing.group(1))
        if 1 <= track_number <= 64:
            return track_number, int(leading_with_trailing.group(2))

    return None, None


def parse_hex_chunk_index(wav_file: Path) -> Optional[int]:
    stem = wav_file.stem
    if not HEX_CHUNK_NAME_RE.match(stem):
        return None

    try:
        return int(stem, 16)
    except ValueError:
        return None


def discover_wav_files(input_root: Path, recursive: bool) -> Iterable[Path]:
    if recursive:
        for path in input_root.rglob("*"):
            if path.is_file() and path.suffix.lower() in WAV_EXTENSIONS:
                yield path
    else:
        for path in input_root.iterdir():
            if path.is_file() and path.suffix.lower() in WAV_EXTENSIONS:
                yield path


def collect_clips_from_files(input_root: Path, wav_files: Sequence[Path]) -> Tuple[List[SourceClip], List[Path]]:
    clips: List[SourceClip] = []
    skipped: List[Path] = []

    for wav_file in wav_files:
        track_number, chunk_index = parse_track_and_chunk(wav_file)
        if track_number is None:
            skipped.append(wav_file)
            continue

        take_root = infer_take_root(wav_file, input_root)
        clips.append(
            SourceClip(
                path=wav_file,
                take_root=take_root,
                track_number=track_number,
                chunk_index=chunk_index,
            )
        )

    return clips, skipped


def read_wave_signature(path: Path) -> Tuple[int, int, int, str, str, int]:
    with wave.open(str(path), "rb") as handle:
        return (
            handle.getnchannels(),
            handle.getsampwidth(),
            handle.getframerate(),
            handle.getcomptype(),
            handle.getcompname(),
            handle.getnframes(),
        )


def detect_chunked_take(parent: Path, files: Sequence[Path], mode: str) -> Optional[ChunkedTake]:
    indexed_files: List[Tuple[int, Path]] = []
    for file_path in files:
        chunk_index = parse_hex_chunk_index(file_path)
        if chunk_index is None:
            if mode == "chunked":
                raise RuntimeError(
                    f"Forced chunked mode: {file_path} is not named as an 8-digit hex chunk."
                )
            return None
        indexed_files.append((chunk_index, file_path))

    indexed_files.sort(key=lambda item: item[0])
    chunk_files = [item[1] for item in indexed_files]

    reference_signature: Optional[Tuple[int, int, int, str]] = None
    reference_compname: Optional[str] = None
    total_frames = 0

    for chunk_file in chunk_files:
        channels, sample_width, sample_rate, comptype, compname, frames = read_wave_signature(chunk_file)
        signature = (channels, sample_width, sample_rate, comptype)

        if reference_signature is None:
            reference_signature = signature
            reference_compname = compname
        elif signature != reference_signature:
            if mode == "chunked":
                raise RuntimeError(
                    f"Forced chunked mode: inconsistent WAV format in {chunk_file.name}."
                )
            return None

        total_frames += frames

    if reference_signature is None or reference_compname is None:
        return None

    channel_count = reference_signature[0]
    if channel_count <= 1 and mode == "auto":
        return None

    return ChunkedTake(
        take_root=parent,
        chunk_files=chunk_files,
        channel_count=reference_signature[0],
        sample_rate=reference_signature[2],
        sample_width=reference_signature[1],
        comptype=reference_signature[3],
        compname=reference_compname,
        total_frames=total_frames,
    )


def detect_chunked_takes(wav_files: Sequence[Path], mode: str) -> Tuple[List[ChunkedTake], Set[Path]]:
    grouped_by_parent: Dict[Path, List[Path]] = defaultdict(list)
    for wav_file in wav_files:
        grouped_by_parent[wav_file.parent].append(wav_file)

    takes: List[ChunkedTake] = []
    consumed: Set[Path] = set()

    for parent in sorted(grouped_by_parent.keys(), key=lambda path: natural_key(str(path))):
        files = sorted(grouped_by_parent[parent], key=lambda path: natural_key(path.name))
        try:
            take = detect_chunked_take(parent, files, mode)
        except RuntimeError:
            raise

        if take is not None:
            takes.append(take)
            for file_path in files:
                consumed.add(file_path)

    return takes, consumed


def clip_sort_key(clip: SourceClip, input_root: Path) -> Tuple[int, int, List[object], str]:
    relative_path = clip.path.relative_to(input_root)
    has_known_chunk = 0 if clip.chunk_index is not None else 1
    chunk_value = clip.chunk_index if clip.chunk_index is not None else 0
    return has_known_chunk, chunk_value, natural_key(str(relative_path)), str(relative_path)


def stitch_track(
    clips: Sequence[SourceClip],
    output_file: Path,
    output_settings: OutputSettings,
    conversion_control: Optional[ConversionControl] = None,
) -> int:
    if not clips:
        return 0

    output_file.parent.mkdir(parents=True, exist_ok=True)

    reference_signature: Optional[Tuple[int, int, int, str]] = None
    frames_written = 0

    with contextlib.ExitStack() as stack:
        output_handle: Optional[OutputHandle] = None
        source_sample_width: Optional[int] = None
        source_channel_count: Optional[int] = None
        target_wav_sample_width: Optional[int] = None
        target_non_wav_sample_width: Optional[int] = None

        for clip in clips:
            honor_conversion_control(conversion_control)
            with wave.open(str(clip.path), "rb") as source_handle:
                params = source_handle.getparams()
                signature = (
                    params.nchannels,
                    params.sampwidth,
                    params.framerate,
                    params.comptype,
                )

                if reference_signature is None:
                    reference_signature = signature
                    source_sample_width = params.sampwidth
                    source_channel_count = params.nchannels
                    if output_settings.format == "wav":
                        target_wav_sample_width = (
                            params.sampwidth
                            if output_settings.wav_keep_original_bit_depth
                            else bit_depth_to_sample_width(output_settings.wav_bit_depth)
                        )
                        wav_handle = stack.enter_context(wave.open(str(output_file), "wb"))
                        wav_handle.setnchannels(params.nchannels)
                        wav_handle.setsampwidth(target_wav_sample_width)
                        wav_handle.setframerate(params.framerate)
                        wav_handle.setcomptype(params.comptype, params.compname)
                        output_handle = wav_handle
                    else:
                        target_non_wav_sample_width = params.sampwidth
                        if output_settings.format == "mp3":
                            target_non_wav_sample_width = 2
                        elif (
                            output_settings.format == "flac"
                            and flac_effective_bit_depth(output_settings.flac_encoding_depth) <= 16
                        ):
                            target_non_wav_sample_width = 2

                        encoded_handle = create_non_wav_writer(
                            output_file=output_file,
                            channel_count=params.nchannels,
                            sample_width=target_non_wav_sample_width,
                            sample_rate=params.framerate,
                            output_settings=output_settings,
                        )
                        output_handle = encoded_handle
                        stack.callback(encoded_handle.close)
                else:
                    if signature != reference_signature:
                        raise ValueError(
                            f"WAV format mismatch while stitching track {output_file.name}: "
                            f"{clip.path.name} does not match {clips[0].path.name}"
                        )

                if output_handle is None:
                    raise RuntimeError(f"Could not initialize output writer for {output_file.name}.")

                read_block_frames = (
                    STITCH_ENCODE_BLOCK_FRAMES if output_settings.format != "wav" else STITCH_WAV_BLOCK_FRAMES
                )

                while True:
                    honor_conversion_control(conversion_control)
                    source_chunk = source_handle.readframes(read_block_frames)
                    if not source_chunk:
                        break

                    chunk = source_chunk
                    if (
                        output_settings.format == "wav"
                        and source_sample_width is not None
                        and target_wav_sample_width is not None
                        and target_wav_sample_width != source_sample_width
                    ):
                        chunk = convert_pcm_bit_depth(source_chunk, source_sample_width, target_wav_sample_width)
                    elif (
                        output_settings.format != "wav"
                        and source_sample_width is not None
                        and target_non_wav_sample_width is not None
                        and target_non_wav_sample_width != source_sample_width
                    ):
                        chunk = convert_pcm_bit_depth(source_chunk, source_sample_width, target_non_wav_sample_width)

                    output_handle.writeframesraw(chunk)
                    if source_channel_count is not None and source_sample_width is not None:
                        bytes_per_frame = source_channel_count * source_sample_width
                        frames_written += len(source_chunk) // bytes_per_frame

    return frames_written


def split_interleaved_block(
    raw_block: bytes,
    channel_count: int,
    sample_width: int,
    output_handles: Sequence[OutputHandle],
    target_sample_width: Optional[int] = None,
    parallel_executor: Optional[concurrent.futures.ThreadPoolExecutor] = None,
) -> None:
    target_width = target_sample_width or sample_width

    # Fast path for typical X-LIVE 24-bit chunk data.
    if sample_width == 3:
        optional_np = get_optional_numpy_module()
        if optional_np is not None:
            split_interleaved_24bit_block_numpy(
                raw_block=raw_block,
                channel_count=channel_count,
                output_handles=output_handles,
                target_sample_width=target_width,
                np_module=optional_np,
                parallel_executor=parallel_executor,
            )
            return

    word_format = {1: "B", 2: "H", 4: "I", 8: "Q"}.get(sample_width)
    if word_format is not None:
        words = memoryview(raw_block).cast(word_format)
        channel_payloads = []
        for channel_index in range(channel_count):
            channel_data = words[channel_index::channel_count].tobytes()
            if target_width != sample_width:
                channel_data = convert_pcm_bit_depth(channel_data, sample_width, target_width)
            channel_payloads.append(channel_data)

        if parallel_executor is not None and channel_count > 1:
            futures = [
                parallel_executor.submit(output_handles[channel_index].writeframesraw, channel_payloads[channel_index])
                for channel_index in range(channel_count)
            ]
            for future in futures:
                future.result()
        else:
            for channel_index in range(channel_count):
                output_handles[channel_index].writeframesraw(channel_payloads[channel_index])
        return

    frame_size = channel_count * sample_width
    if frame_size <= 0:
        return

    frames = len(raw_block) // frame_size
    channel_buffers = [bytearray(frames * sample_width) for _ in range(channel_count)]

    for frame_index in range(frames):
        frame_start = frame_index * frame_size
        output_offset = frame_index * sample_width
        for channel_index in range(channel_count):
            sample_start = frame_start + channel_index * sample_width
            sample_end = sample_start + sample_width
            channel_buffers[channel_index][output_offset : output_offset + sample_width] = (
                raw_block[sample_start:sample_end]
            )

    channel_payloads = []
    for channel_data in channel_buffers:
        channel_bytes = bytes(channel_data)
        if target_width != sample_width:
            channel_bytes = convert_pcm_bit_depth(channel_bytes, sample_width, target_width)
        channel_payloads.append(channel_bytes)

    if parallel_executor is not None and channel_count > 1:
        futures = [
            parallel_executor.submit(output_handles[channel_index].writeframesraw, channel_payloads[channel_index])
            for channel_index in range(channel_count)
        ]
        for future in futures:
            future.result()
    else:
        for channel_index in range(channel_count):
            output_handles[channel_index].writeframesraw(channel_payloads[channel_index])


def split_chunked_take(
    take: ChunkedTake,
    output_root: Path,
    dry_run: bool,
    block_frames: int,
    output_settings: OutputSettings,
    logger: logging.Logger,
    conversion_control: Optional[ConversionControl] = None,
) -> int:
    output_root.mkdir(parents=True, exist_ok=True)

    extension = output_extension(output_settings.format)
    used_stems: Set[str] = set()
    output_files = [
        output_root
        / build_track_output_filename(
            track_number=channel_index + 1,
            extension=extension,
            track_name_overrides=output_settings.track_name_overrides,
            used_stems=used_stems,
        )
        for channel_index in range(take.channel_count)
    ]

    if dry_run:
        return take.channel_count

    target_wav_sample_width = (
        take.sample_width
        if output_settings.wav_keep_original_bit_depth
        else bit_depth_to_sample_width(output_settings.wav_bit_depth)
    )
    target_non_wav_sample_width = take.sample_width
    if output_settings.format == "mp3":
        target_non_wav_sample_width = 2
    elif (
        output_settings.format == "flac"
        and flac_effective_bit_depth(output_settings.flac_encoding_depth) <= 16
    ):
        target_non_wav_sample_width = 2

    parallel_jobs = resolve_encoder_jobs(output_settings, take.channel_count)

    with contextlib.ExitStack() as stack:
        parallel_executor: Optional[concurrent.futures.ThreadPoolExecutor] = None
        if output_settings.format != "wav" and parallel_jobs > 1:
            parallel_executor = stack.enter_context(
                concurrent.futures.ThreadPoolExecutor(max_workers=parallel_jobs)
            )

        output_handles: List[OutputHandle] = []
        for output_file in output_files:
            if output_settings.format == "wav":
                output_handle = stack.enter_context(wave.open(str(output_file), "wb"))
                output_handle.setnchannels(1)
                output_handle.setsampwidth(target_wav_sample_width)
                output_handle.setframerate(take.sample_rate)
                output_handle.setcomptype(take.comptype, take.compname)
            else:
                encoded_handle = create_non_wav_writer(
                    output_file=output_file,
                    channel_count=1,
                    sample_width=target_non_wav_sample_width,
                    sample_rate=take.sample_rate,
                    output_settings=output_settings,
                )
                output_handle = encoded_handle
                stack.callback(encoded_handle.close)
            output_handles.append(output_handle)

        for chunk_file in take.chunk_files:
            honor_conversion_control(conversion_control)
            logger.info("    chunk: %s", chunk_file.name)
            with wave.open(str(chunk_file), "rb") as source_handle:
                while True:
                    honor_conversion_control(conversion_control)
                    block = source_handle.readframes(block_frames)
                    if not block:
                        break
                    split_interleaved_block(
                        block,
                        take.channel_count,
                        take.sample_width,
                        output_handles,
                        target_sample_width=(
                            target_wav_sample_width
                            if output_settings.format == "wav"
                            else target_non_wav_sample_width
                        ),
                        parallel_executor=parallel_executor,
                    )

    source_stat = take.chunk_files[0].stat()
    for output_file in output_files:
        os.utime(output_file, (source_stat.st_atime, source_stat.st_mtime))

    return take.channel_count


def convert_xlive_folder(
    input_root: Path,
    output_root: Path,
    recursive: bool,
    dry_run: bool,
    strict: bool,
    mode: str,
    block_frames: int,
    output_settings: Optional[OutputSettings],
    logger: logging.Logger,
    conversion_control: Optional[ConversionControl] = None,
) -> ConversionSummary:
    honor_conversion_control(conversion_control)
    normalized_output = normalize_output_settings(output_settings)

    if normalized_output.format == "mp3" and normalized_output.mp3_write_replaygain:
        logger.warning(
            "ReplayGain tag writing is currently not supported; continuing without ReplayGain tags."
        )

    if normalized_output.format in {"flac", "mp3"}:
        ffmpeg_path = get_optional_ffmpeg_path()
        if ffmpeg_path is not None:
            logger.info("Encoder backend: ffmpeg (%s)", ffmpeg_path)
        else:
            logger.info("Encoder backend: in-process fallback (ffmpeg not found)")
        logger.info("Encoder workers: %d", resolve_encoder_jobs(normalized_output, 64))

    all_wav_files = list(discover_wav_files(input_root, recursive))
    all_wav_files = [
        wav_file
        for wav_file in all_wav_files
        if not is_within_path(wav_file.resolve(), output_root)
    ]
    summary = ConversionSummary(skipped_files=[])

    if not all_wav_files:
        raise RuntimeError("No compatible WAV files found in the input folder.")

    chunked_takes, consumed_files = detect_chunked_takes(all_wav_files, mode)
    remaining_wav_files = [path for path in all_wav_files if path not in consumed_files]
    clips, skipped = collect_clips_from_files(input_root, remaining_wav_files)
    summary.skipped_files.extend(skipped)

    grouped_tracks: Dict[Path, Dict[int, List[SourceClip]]] = defaultdict(lambda: defaultdict(list))
    for clip in clips:
        grouped_tracks[clip.take_root][clip.track_number].append(clip)

    if mode == "chunked" and not chunked_takes:
        raise RuntimeError("Forced chunked mode found no valid hex-named multichannel chunk takes.")

    if mode == "track-files" and chunked_takes:
        raise RuntimeError("Forced track-files mode does not allow chunked multichannel take detection.")

    if not chunked_takes and not clips:
        raise RuntimeError("No compatible WAV files found after mode detection.")

    logger.info("Detected %d chunked take(s).", len(chunked_takes))
    logger.info("Detected %d wav clip(s) mapped to track-file mode.", len(clips))
    logger.info("Output format: %s", normalized_output.format.upper())
    if normalized_output.track_name_overrides:
        logger.info("Track naming overrides: %d", len(normalized_output.track_name_overrides))
    if normalized_output.format == "wav":
        if normalized_output.wav_keep_original_bit_depth:
            logger.info("WAV settings: keep original bit depth")
        else:
            logger.info("WAV settings: forced bit depth=%d-bit PCM", normalized_output.wav_bit_depth)
    if normalized_output.format == "flac":
        logger.info(
            "FLAC settings: encoding-depth=%s, compression-level=%d",
            normalized_output.flac_encoding_depth,
            normalized_output.flac_compression_level,
        )
    if normalized_output.format == "mp3":
        logger.info(
            "MP3 settings: mode=%s, bitrate=%dk",
            normalized_output.mp3_mode.upper(),
            normalized_output.mp3_bitrate_kbps,
        )

    if skipped:
        logger.warning("Skipped %d file(s) because track number could not be detected.", len(skipped))
        for skipped_file in sorted(skipped, key=lambda path: natural_key(str(path))):
            logger.warning("  skipped: %s", skipped_file)

    if strict and skipped:
        raise RuntimeError("Strict mode is enabled and some WAV files could not be mapped.")

    sorted_chunked_takes = sorted(
        chunked_takes,
        key=lambda take: natural_key(str(take.take_root.relative_to(input_root))),
    )

    for take in sorted_chunked_takes:
        honor_conversion_control(conversion_control)
        relative_take = take.take_root.relative_to(input_root)
        if str(relative_take) == ".":
            take_output_root = output_root
            take_label = input_root.name
        else:
            take_output_root = output_root / relative_take
            take_label = str(relative_take)

        duration_seconds = take.total_frames / float(take.sample_rate)
        logger.info("Take (chunked): %s", take_label)
        logger.info(
            "  Chunks: %d | Channels: %d | Format: %d-byte @ %d Hz | Duration: %s",
            len(take.chunk_files),
            take.channel_count,
            take.sample_width,
            take.sample_rate,
            format_duration(duration_seconds),
        )

        written_tracks = split_chunked_take(
            take=take,
            output_root=take_output_root,
            dry_run=dry_run,
            block_frames=block_frames,
            output_settings=normalized_output,
            logger=logger,
            conversion_control=conversion_control,
        )

        if not dry_run:
            logger.info("  Wrote %d output track file(s).", written_tracks)

        summary.takes_converted += 1
        summary.tracks_written += written_tracks
        summary.clips_consumed += len(take.chunk_files)

    take_roots = sorted(grouped_tracks.keys(), key=lambda path: natural_key(str(path.relative_to(input_root))))

    for take_root in take_roots:
        honor_conversion_control(conversion_control)
        track_map = grouped_tracks[take_root]
        relative_take = take_root.relative_to(input_root)
        if str(relative_take) == ".":
            take_output_root = output_root
            take_label = input_root.name
        else:
            take_output_root = output_root / relative_take
            take_label = str(relative_take)

        logger.info("Take (track-files): %s", take_label)
        logger.info("  Tracks: %d", len(track_map))

        if not dry_run:
            take_output_root.mkdir(parents=True, exist_ok=True)

        used_stems: Set[str] = set()

        for track_number in sorted(track_map.keys()):
            honor_conversion_control(conversion_control)
            source_clips = sorted(
                track_map[track_number],
                key=lambda clip: clip_sort_key(clip, input_root),
            )
            extension = output_extension(normalized_output.format)
            output_file = take_output_root / build_track_output_filename(
                track_number=track_number,
                extension=extension,
                track_name_overrides=normalized_output.track_name_overrides,
                used_stems=used_stems,
            )

            logger.info("  Track %02d <- %d clip(s)", track_number, len(source_clips))
            if not dry_run:
                frames = stitch_track(
                    source_clips,
                    output_file,
                    output_settings=normalized_output,
                    conversion_control=conversion_control,
                )
                logger.info("    wrote: %s (%d frames)", output_file, frames)

            summary.tracks_written += 1
            summary.clips_consumed += len(source_clips)

        summary.takes_converted += 1

    return summary


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description=(
            "Convert Behringer WING X-LIVE recording folders into consolidated "
            "multitrack files (WAV, FLAC, or MP3)."
        )
    )
    parser.add_argument(
        "input_folder",
        type=Path,
        help="Path to the X-LIVE recording folder (or parent containing take folders).",
    )
    parser.add_argument(
        "output_folder",
        type=Path,
        help="Destination folder for generated multitrack WAV files.",
    )
    parser.add_argument(
        "--no-recursive",
        action="store_true",
        help="Only scan WAV files in the top input folder (do not recurse).",
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="Show detected takes/tracks and planned outputs without writing files.",
    )
    parser.add_argument(
        "--strict",
        action="store_true",
        help="Fail if any WAV file cannot be mapped to a track number.",
    )
    parser.add_argument(
        "--mode",
        choices=("auto", "chunked", "track-files"),
        default="auto",
        help=(
            "Detection mode: auto selects chunked multichannel and per-track clip modes automatically; "
            "chunked forces native X-LIVE hex chunk mode; track-files forces per-track clip stitching mode."
        ),
    )
    parser.add_argument(
        "--block-frames",
        type=int,
        default=65536,
        help="Frames per read block while splitting chunked multichannel takes.",
    )
    parser.add_argument(
        "--output-format",
        choices=("wav", "flac", "mp3"),
        default="wav",
        help=(
            "Output file format. WAV writes directly; FLAC/MP3 prefer ffmpeg when available "
            "and fall back to in-process encoding."
        ),
    )
    parser.add_argument(
        "--wav-bit-depth",
        type=int,
        default=24,
        choices=(8, 16, 24, 32),
        help="Forced WAV bit depth when --wav-keep-original-bit-depth is not set.",
    )
    parser.add_argument(
        "--wav-keep-original-bit-depth",
        action="store_true",
        default=True,
        help="Keep original source WAV bit depth for WAV output.",
    )
    parser.add_argument(
        "--wav-force-bit-depth",
        dest="wav_keep_original_bit_depth",
        action="store_false",
        help="Force WAV output to use --wav-bit-depth.",
    )
    parser.add_argument(
        "--flac-encoding-depth",
        default="24",
        choices=("24", "23/24", "22/24", "21/24", "20/24", "19/24", "18/24", "17/24", "16"),
        help="FLAC encoding depth profile.",
    )
    parser.add_argument(
        "--flac-compression-level",
        type=int,
        default=5,
        choices=(0, 1, 2, 3, 4, 5, 6, 7, 8),
        help="FLAC data compression level (0 fastest .. 8 slowest).",
    )
    parser.add_argument(
        "--mp3-mode",
        choices=("cbr", "vbr"),
        default="cbr",
        help="MP3 mode: constant bitrate (cbr) or variable bitrate (vbr).",
    )
    parser.add_argument(
        "--mp3-bitrate",
        type=int,
        default=320,
        help="MP3 bitrate in kbps when using CBR mode (range 64..320).",
    )
    parser.add_argument(
        "--mp3-quality",
        type=int,
        default=0,
        help="MP3 quality value (0 best/slowest .. 9 fastest).",
    )
    parser.add_argument(
        "--mp3-write-replaygain",
        action="store_true",
        help="Reserved option for ReplayGain tags (currently not supported).",
    )
    parser.add_argument(
        "--encoder-jobs",
        type=int,
        default=0,
        help=(
            "Parallel encoder workers for FLAC/MP3 (0=auto, 1=single-thread). "
            "Higher values can reduce conversion time on multi-core CPUs."
        ),
    )
    parser.add_argument(
        "--verbose",
        action="store_true",
        help="Enable extra log detail.",
    )
    return parser


def configure_logger(verbose: bool) -> logging.Logger:
    logger = logging.getLogger("wing-xlive-converter")
    logger.handlers.clear()

    handler = logging.StreamHandler(sys.stdout)
    formatter = logging.Formatter("%(levelname)s: %(message)s")
    handler.setFormatter(formatter)

    logger.addHandler(handler)
    logger.setLevel(logging.DEBUG if verbose else logging.INFO)
    logger.propagate = False
    return logger


def main(argv: Optional[Sequence[str]] = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)

    input_root = args.input_folder.resolve()
    output_root = args.output_folder.resolve()

    logger = configure_logger(args.verbose)

    if not input_root.exists() or not input_root.is_dir():
        logger.error("Input folder does not exist or is not a directory: %s", input_root)
        return 2

    try:
        output_settings = OutputSettings(
            format=args.output_format,
            wav_keep_original_bit_depth=args.wav_keep_original_bit_depth,
            wav_bit_depth=args.wav_bit_depth,
            flac_encoding_depth=args.flac_encoding_depth,
            flac_compression_level=args.flac_compression_level,
            mp3_mode=args.mp3_mode,
            mp3_bitrate_kbps=args.mp3_bitrate,
            mp3_quality=args.mp3_quality,
            mp3_write_replaygain=args.mp3_write_replaygain,
            encoder_jobs=args.encoder_jobs,
        )

        summary = convert_xlive_folder(
            input_root=input_root,
            output_root=output_root,
            recursive=not args.no_recursive,
            dry_run=args.dry_run,
            strict=args.strict,
            mode=args.mode,
            block_frames=max(1024, int(args.block_frames)),
            output_settings=output_settings,
            logger=logger,
        )
    except Exception as exc:  # pylint: disable=broad-except
        logger.error("Conversion failed: %s", exc)
        return 1

    logger.info("Done.")
    logger.info(
        "Summary: takes=%d, tracks=%d, clips=%d, skipped=%d",
        summary.takes_converted,
        summary.tracks_written,
        summary.clips_consumed,
        len(summary.skipped_files),
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
