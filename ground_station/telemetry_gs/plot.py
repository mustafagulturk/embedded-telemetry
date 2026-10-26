"""Dashboard figure (altitude, accel, gyro, status panel) for live view and GIF export.

:class:`Dashboard` only depends on a ``matplotlib.figure.Figure`` so it can be
rendered headless (Agg) by ``make_gif``. :func:`run_live_plot` wraps it in a
pyplot window driven by ``FuncAnimation``.
"""

from __future__ import annotations

from collections.abc import Sequence
from dataclasses import dataclass

import matplotlib
from matplotlib.animation import FuncAnimation
from matplotlib.artist import Artist
from matplotlib.axes import Axes
from matplotlib.figure import Figure
from matplotlib.lines import Line2D
from matplotlib.patches import FancyBboxPatch
from matplotlib.text import Text

from .protocol import LinkCounters, Mode, Stats, Telemetry
from .station import GroundStation

# Palette: reference categorical slots 1-3 for x/y/z, muted ink for raw data,
# fixed status colors for the flight mode badge.
SURFACE = "#fcfcfb"
TEXT_PRIMARY = "#0b0b0b"
TEXT_SECONDARY = "#52514e"
GRID = "#e4e3df"
RAW = "#a3a29c"
SERIES = ("#2a78d6", "#eb6834", "#1baf7a")
MODE_COLORS: dict[int, str] = {
    Mode.NORMAL: "#0ca30c",
    Mode.DEGRADED: "#fab219",
    Mode.FAILSAFE: "#d03b3b",
}
# Badge text ink: white on green/red, dark on the light warning yellow.
MODE_INK: dict[int, str] = {
    Mode.NORMAL: "#ffffff",
    Mode.DEGRADED: TEXT_PRIMARY,
    Mode.FAILSAFE: "#ffffff",
}
MODE_LABELS: dict[int, str] = {
    Mode.NORMAL: "NORMAL",
    Mode.DEGRADED: "DEGRADED (kısıtlı)",
    Mode.FAILSAFE: "FAILSAFE (güvenli mod)",
}
# Built-in matplotlib backends that cannot open a window.
NON_INTERACTIVE_BACKENDS = frozenset({"agg", "cairo", "pdf", "pgf", "ps", "svg", "template"})
FAULT_LABELS: tuple[str, ...] = ("IMU", "BARO", "Kuyruk taşması", "Watchdog reset")
RESET_LABELS: tuple[str, ...] = ("güç açılışı", "watchdog", "yazılım", "diğer")


@dataclass(frozen=True)
class DashboardView:
    """Everything the dashboard needs for one redraw."""

    samples: Sequence[Telemetry]
    stats: Stats | None
    link: LinkCounters
    window_s: float = 10.0
    source_label: str = ""
    note: str = ""


def _style_axes(ax: Axes, ylabel: str) -> None:
    ax.set_facecolor(SURFACE)
    ax.grid(True, color=GRID, linewidth=0.8)
    ax.set_ylabel(ylabel, color=TEXT_SECONDARY)
    ax.tick_params(colors=TEXT_SECONDARY, labelsize=8)
    for spine in ("top", "right"):
        ax.spines[spine].set_visible(False)
    for spine in ("left", "bottom"):
        ax.spines[spine].set_color(GRID)


def _fault_text(faults: int) -> str:
    names = [label for bit, label in enumerate(FAULT_LABELS) if faults & (1 << bit)]
    unknown = faults & ~((1 << len(FAULT_LABELS)) - 1)
    if unknown:
        names.append(f"0x{unknown:02X}")
    return ", ".join(names) if names else "yok"


def status_text(view: DashboardView) -> str:
    """Multi-line status panel text (Turkish)."""
    tel = view.samples[-1] if view.samples else None
    st = view.stats
    link = view.link
    lines: list[str] = []
    if tel is not None:
        lines += [
            f"Zaman     : {tel.t_s:8.2f} s",
            f"İrtifa    : {tel.alt_filt_m:8.2f} m",
            f"Basınç    : {tel.pressure:8d} Pa",
            f"Sıcaklık  : {tel.temp_c:8.2f} °C",
            f"Hatalar   : {_fault_text(tel.faults)}",
        ]
    else:
        lines.append("Veri bekleniyor...")
    lines += [
        "",
        "Bağlantı",
        f"  alınan  : {link.rx_total}",
        f"  kayıp   : {link.lost_total} ({link.loss_pct:.2f} %)",
        f"  CRC hata: {link.crc_errors_total}",
        "",
        "İstatistik (1 Hz)",
    ]
    if st is not None:
        reset = RESET_LABELS[st.reset_cause] if st.reset_cause < len(RESET_LABELS) else "?"
        lines += [
            f"  CPU yükü: {st.cpu_load_pct:5.1f} %",
            f"  sensör  : {st.sensor_max_us:5d} µs max",
            f"  paketle : {st.packer_max_us:5d} µs max",
            f"  gönder  : {st.tx_max_us:5d} µs max",
            f"  kuyruk  : {st.queue_drops} atılan",
            f"  I2C hata: {st.i2c_errors}",
            f"  reset   : {reset}",
        ]
    else:
        lines.append("  (henüz yok)")
    if view.source_label:
        label = view.source_label
        if len(label) > 30:
            label = "…" + label[-29:]
        lines += ["", f"Kaynak: {label}"]
    if view.note:
        lines.append(view.note)
    return "\n".join(lines)


class Dashboard:
    def __init__(self, fig: Figure) -> None:
        self.fig = fig
        fig.set_facecolor(SURFACE)
        gs = fig.add_gridspec(3, 3, width_ratios=(1.0, 1.0, 0.85), hspace=0.35, wspace=0.3,
                              left=0.07, right=0.98, top=0.90, bottom=0.09)
        self.ax_alt: Axes = fig.add_subplot(gs[0, 0:2])
        self.ax_acc: Axes = fig.add_subplot(gs[1, 0:2], sharex=self.ax_alt)
        self.ax_gyr: Axes = fig.add_subplot(gs[2, 0:2], sharex=self.ax_alt)
        self.ax_txt: Axes = fig.add_subplot(gs[:, 2])

        _style_axes(self.ax_alt, "İrtifa [m]")
        _style_axes(self.ax_acc, "İvme [g]")
        _style_axes(self.ax_gyr, "Açısal hız [°/s]")
        self.ax_gyr.set_xlabel("Zaman [s]", color=TEXT_SECONDARY)
        fig.suptitle("Yer istasyonu telemetrisi", x=0.01, ha="left", color=TEXT_PRIMARY,
                     fontsize=11)

        (self.l_alt_raw,) = self.ax_alt.plot([], [], color=RAW, linewidth=1.0, label="ham")
        (self.l_alt_filt,) = self.ax_alt.plot([], [], color=SERIES[0], linewidth=2.0,
                                              label="filtreli")
        self.l_acc: list[Line2D] = [
            self.ax_acc.plot([], [], color=c, linewidth=1.2, label=n)[0]
            for c, n in zip(SERIES, ("x", "y", "z"))
        ]
        self.l_gyr: list[Line2D] = [
            self.ax_gyr.plot([], [], color=c, linewidth=1.2, label=n)[0]
            for c, n in zip(SERIES, ("x", "y", "z"))
        ]
        for ax in (self.ax_alt, self.ax_acc, self.ax_gyr):
            legend = ax.legend(loc="lower right", bbox_to_anchor=(1.0, 1.0), fontsize=8,
                               frameon=False, ncol=3, borderaxespad=0.0, handlelength=1.5)
            for t in legend.get_texts():
                t.set_color(TEXT_SECONDARY)

        self.ax_txt.set_axis_off()
        self.mode_badge: Text = self.ax_txt.text(
            0.0, 1.0, "", transform=self.ax_txt.transAxes, va="top", ha="left",
            fontsize=11, fontweight="bold", color=TEXT_PRIMARY,
            bbox={"boxstyle": "round,pad=0.4", "facecolor": GRID, "edgecolor": "none"},
        )
        self.status: Text = self.ax_txt.text(
            0.0, 0.90, "", transform=self.ax_txt.transAxes, va="top", ha="left",
            fontsize=8.5, family="monospace", color=TEXT_PRIMARY,
        )

    def artists(self) -> list[Artist]:
        return [self.l_alt_raw, self.l_alt_filt, *self.l_acc, *self.l_gyr,
                self.mode_badge, self.status]

    def _set_badge_color(self, color: str) -> None:
        patch: FancyBboxPatch | None = self.mode_badge.get_bbox_patch()
        if patch is not None:
            patch.set_facecolor(color)

    def render(self, view: DashboardView) -> None:
        samples = view.samples
        t = [s.t_ms / 1000.0 for s in samples]
        self.l_alt_raw.set_data(t, [s.alt_raw / 100.0 for s in samples])
        self.l_alt_filt.set_data(t, [s.alt_filt / 100.0 for s in samples])
        for line, values in zip(self.l_acc, (
            [s.ax / 1000.0 for s in samples],
            [s.ay / 1000.0 for s in samples],
            [s.az / 1000.0 for s in samples],
        )):
            line.set_data(t, values)
        for line, values in zip(self.l_gyr, (
            [s.gx / 10.0 for s in samples],
            [s.gy / 10.0 for s in samples],
            [s.gz / 10.0 for s in samples],
        )):
            line.set_data(t, values)

        if t:
            t_end = max(t[-1], view.window_s)
            self.ax_alt.set_xlim(t_end - view.window_s, t_end)
            for ax in (self.ax_alt, self.ax_acc, self.ax_gyr):
                ax.relim()
                ax.autoscale_view(scalex=False, scaley=True)

        tel = samples[-1] if samples else None
        if tel is not None:
            self.mode_badge.set_text(f"Mod: {MODE_LABELS.get(tel.mode, str(tel.mode))}")
            self.mode_badge.set_color(MODE_INK.get(tel.mode, TEXT_PRIMARY))
            self._set_badge_color(MODE_COLORS.get(tel.mode, GRID))
        else:
            self.mode_badge.set_text("Mod: -")
            self.mode_badge.set_color(TEXT_PRIMARY)
            self._set_badge_color(GRID)
        self.status.set_text(status_text(view))


class LivePlot:
    """Couples a :class:`GroundStation` to a :class:`Dashboard`; one ``update`` per tick."""

    def __init__(self, station: GroundStation, fig: Figure, window_s: float = 10.0,
                 poll_budget_s: float = 0.05) -> None:
        self.station = station
        self.dashboard = Dashboard(fig)
        self.window_s = window_s
        self.poll_budget_s = poll_budget_s
        self._window_samples = int(window_s * 100) + 50

    def update(self, _frame: int = 0) -> list[Artist]:
        st = self.station
        st.poll(budget_s=self.poll_budget_s)
        hist = list(st.history)[-self._window_samples:]
        note = "Kaynak bitti." if st.source.eof else ""
        self.dashboard.render(DashboardView(hist, st.latest_stats, st.link, self.window_s,
                                            st.source.name, note))
        return self.dashboard.artists()


def is_interactive_backend() -> bool:
    return matplotlib.get_backend().lower() not in NON_INTERACTIVE_BACKENDS


def run_live_plot(
    station: GroundStation,
    window_s: float = 10.0,
    duration_s: float | None = None,
    interval_ms: int = 100,
) -> None:
    """Open a live window; returns when it is closed or ``duration_s`` elapsed.

    Raises RuntimeError when no interactive matplotlib backend is available.
    """
    import matplotlib.pyplot as plt  # pyplot only for the interactive window

    if not is_interactive_backend():
        raise RuntimeError(
            f"etkileşimli matplotlib arka ucu yok (şu an: {matplotlib.get_backend()})"
        )
    fig = plt.figure(figsize=(11, 6.5))
    live = LivePlot(station, fig, window_s, poll_budget_s=interval_ms / 2000.0)

    def tick(frame: int) -> list[Artist]:
        artists = live.update(frame)
        if duration_s is not None and station.elapsed() >= duration_s:
            plt.close(fig)
        return artists

    anim = FuncAnimation(fig, tick, interval=interval_ms, cache_frame_data=False)
    plt.show()
    del anim
