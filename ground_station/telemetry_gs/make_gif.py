"""Render a demo GIF of the dashboard from a telemetry CSV (headless, Agg backend).

Usage::

    python -m telemetry_gs.make_gif sim.csv --stats-csv sim_stats.csv -o docs/demo_sim.gif
"""

from __future__ import annotations

import argparse
import bisect
import sys
from io import BytesIO
from pathlib import Path

from matplotlib.animation import FuncAnimation, PillowWriter
from matplotlib.artist import Artist
from matplotlib.backends.backend_agg import FigureCanvasAgg
from matplotlib.figure import Figure
from PIL import Image

from .csv_logger import StatsRow, TelemetryRow, read_stats_csv, read_telemetry_csv
from .plot import Dashboard, DashboardView


class CompactGifWriter(PillowWriter):
    """PillowWriter that quantizes each frame to a small adaptive palette without
    dithering, which keeps line-plot GIFs roughly 30 % smaller."""

    def __init__(self, fps: int = 10, colors: int = 48) -> None:
        super().__init__(fps=fps)
        self.colors = colors
        self.images: list[Image.Image] = []

    def setup(self, fig: Figure, outfile: str | Path, dpi: float | None = None) -> None:
        super().setup(fig, outfile, dpi=dpi)
        self.images = []

    def grab_frame(self, **savefig_kwargs: object) -> None:
        buf = BytesIO()
        self.fig.savefig(buf, format="rgba", dpi=self.dpi)
        rgb = Image.frombuffer("RGBA", self.frame_size, buf.getvalue(), "raw", "RGBA", 0, 1)
        self.images.append(
            rgb.convert("RGB").quantize(
                colors=self.colors, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE
            )
        )

    def finish(self) -> None:
        self.images[0].save(
            self.outfile, save_all=True, append_images=self.images[1:],
            duration=int(1000 / self.fps), loop=0,
        )


def render_gif(
    rows: list[TelemetryRow],
    stats_rows: list[StatsRow],
    out: Path,
    seconds: float = 10.0,
    fps: int = 10,
    dpi: int = 72,
    window_s: float = 10.0,
    width_in: float = 10.0,
    height_in: float = 6.0,
    label: str = "",
) -> int:
    """Render the whole flight compressed into ``seconds`` of GIF. Returns frame count."""
    if not rows:
        raise ValueError("CSV dosyasında telemetri satırı yok")
    times = [r.telemetry.t_ms / 1000.0 for r in rows]
    stat_times = [s.stats.uptime_ms / 1000.0 for s in stats_rows]
    t0, t1 = times[0], times[-1]
    n_frames = max(1, int(round(seconds * fps)))
    samples = [r.telemetry for r in rows]

    fig = Figure(figsize=(width_in, height_in), dpi=dpi)
    FigureCanvasAgg(fig)
    dash = Dashboard(fig)

    def update(k: int) -> list[Artist]:
        t_now = t0 + (t1 - t0) * (k + 1) / n_frames
        hi = bisect.bisect_right(times, t_now)
        lo = bisect.bisect_left(times, t_now - window_s)
        si = bisect.bisect_right(stat_times, t_now)
        stats = stats_rows[si - 1].stats if si > 0 else None
        link = rows[hi - 1].link if hi > 0 else rows[0].link
        dash.render(DashboardView(samples[lo:hi], stats, link, window_s, label))
        return dash.artists()

    anim = FuncAnimation(fig, update, frames=n_frames, blit=False, cache_frame_data=False)
    anim.save(str(out), writer=CompactGifWriter(fps=fps), dpi=dpi)
    return n_frames


def build_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(
        add_help=False,
        prog="python -m telemetry_gs.make_gif",
        description="Telemetri CSV dosyasından gösterim GIF'i üretir (ekran gerekmez).",
    )
    p.add_argument("csv", type=Path, help="telemetri CSV dosyası (--csv çıktısı)")
    p.add_argument("--stats-csv", type=Path, default=None, help="istatistik CSV (--stats-csv çıktısı)")
    p.add_argument("-o", "--out", type=Path, default=Path("demo.gif"), help="çıktı GIF dosyası")
    p.add_argument("--seconds", type=float, default=10.0, help="GIF süresi, s (varsayılan: 10)")
    p.add_argument("--fps", type=int, default=10, help="kare hızı (varsayılan: 10)")
    p.add_argument("--dpi", type=int, default=72, help="çözünürlük (varsayılan: 72)")
    p.add_argument("--window", type=float, default=10.0, help="grafik zaman penceresi, s")
    p.add_argument("--label", default="", help="durum panelinde gösterilecek kaynak etiketi")
    p.add_argument("-h", "--help", action="help", help="bu yardım mesajını göster ve çık")
    return p


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    rows = read_telemetry_csv(args.csv)
    stats_rows = read_stats_csv(args.stats_csv) if args.stats_csv else []
    args.out.parent.mkdir(parents=True, exist_ok=True)
    n = render_gif(rows, stats_rows, args.out, args.seconds, args.fps, args.dpi, args.window,
                   label=args.label)
    size = args.out.stat().st_size
    print(f"{args.out}: {n} kare, {size / 1024:.0f} KiB", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
