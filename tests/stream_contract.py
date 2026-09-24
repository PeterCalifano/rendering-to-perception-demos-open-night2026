"""Exercise file-stream ordering, tracking, retry, and error behavior.

Run with ``python3 tests/stream_contract.py build/demo/camera_stream_demo``.
"""

from __future__ import annotations

import json
import subprocess
import sys
import tempfile
from pathlib import Path

import cv2
import numpy as np


def make_frame(path: Path, index: int, *, dark: bool = False, width_px: int = 640) -> None:
    """Write one camera-like frame with visible point texture and an order marker."""
    image = np.zeros((480, width_px, 3), dtype=np.uint8)
    if not dark:
        center = (width_px // 2 + 2 * index, 240)
        cv2.circle(image, center, 175, (110, 110, 110), -1)
        for row in range(-5, 6):
            for column in range(-5, 6):
                position = (center[0] + 24 * column, center[1] + 24 * row)
                if (position[0] - center[0]) ** 2 + (position[1] - center[1]) ** 2 < 150**2:
                    intensity = 230 if (row + column) % 2 else 30
                    cv2.circle(image, position, 4, (intensity,) * 3, -1)
    if not dark:
        image[450:470, 10:30] = (40 + 30 * index,) * 3
    if not cv2.imwrite(str(path), image):
        raise RuntimeError(f"Could not write fixture: {path}")


def run_stream(executable: Path, input_dir: Path, output_dir: Path, count: int) -> subprocess.CompletedProcess[str]:
    """Run a finite folder stream through the public CLI."""
    return subprocess.run(
        [str(executable), "--frames-dir", str(input_dir), "--fps", "4", "--max-frames",
         str(count), "--headless", "--output-dir", str(output_dir)],
        capture_output=True, text=True, timeout=30, check=False,
    )


def read_records(output_dir: Path) -> list[dict[str, object]]:
    """Read the serialized public summary records."""
    return [json.loads(line) for line in (output_dir / "frames.jsonl").read_text().splitlines()]


def main(executable: Path) -> None:
    """Check natural order, persistent IDs, dark-frame retry, and dimension rejection."""
    with tempfile.TemporaryDirectory(prefix="camera-stream-contract-") as temporary:
        root = Path(temporary)
        input_dir = root / "ordered"
        input_dir.mkdir()
        for index, name in enumerate(("frame1.png", "frame2.png", "frame10.png",
                                      "frame11.png", "frame12.png")):
            make_frame(input_dir / name, index)
        output_dir = root / "tracked"
        result = run_stream(executable, input_dir, output_dir, 5)
        assert result.returncode == 0, result.stdout + result.stderr
        records = read_records(output_dir)
        assert len(records) == 5, records
        assert [item["source_index"] for item in records] == list(range(5))
        assert [item["processed_index"] for item in records] == list(range(5))
        assert any(item["active_features"] > 0 for item in records)
        assert any(set(current["active_track_ids"]) & set(previous["active_track_ids"])
                   for previous, current in zip(records, records[1:]))
        for index in range(5):
            image = cv2.imread(str(output_dir / "frames" / f"frame_{index:06d}.png"))
            assert image is not None
            assert tuple(image[460, 20]) == (40 + 30 * index,) * 3

        retry_dir = root / "retry"
        retry_dir.mkdir()
        for index in range(5):
            make_frame(retry_dir / f"frame{index}.png", index, dark=index < 2)
        retry_output = root / "retry_output"
        result = run_stream(executable, retry_dir, retry_output, 5)
        assert result.returncode == 0, result.stdout + result.stderr
        retry = read_records(retry_output)
        assert retry[0]["mask_status"] == "EMPTY" and retry[0]["extraction_retry"]
        assert retry[1]["mask_status"] == "EMPTY" and retry[1]["extraction_retry"]
        assert any(item["mask_status"] == "READY" and item["active_features"] > 0
                   for item in retry[2:])

        mismatch_dir = root / "mismatch"
        mismatch_dir.mkdir()
        make_frame(mismatch_dir / "frame1.png", 0)
        make_frame(mismatch_dir / "frame2.png", 1, width_px=600)
        result = run_stream(executable, mismatch_dir, root / "mismatch_output", 2)
        assert result.returncode != 0
        assert "dimensions changed" in result.stderr


if __name__ == "__main__":
    main(Path(sys.argv[1]).resolve())
