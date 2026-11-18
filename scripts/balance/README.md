# Repeatable commodity balance testing

From the project directory:

```powershell
python scripts/test-balance.py
```

This builds a small C++17 runner against the **current production economy and
game rules**, runs the experiments, and prints the location of `report.md`.
No Codex, API calls, paid services, pip packages, downloads, game installation,
artwork, or saved profiles are used by the script. Run it directly from a terminal
as often as needed. The first build requires CMake and a C++17 compiler. On Windows,
the script discovers Visual Studio Build Tools and its bundled CMake automatically.
On Linux/macOS, put CMake and a compiler on PATH. Builds are incremental.

## Typical commands

```powershell
# 100 shared seeds across all strategies, focused runs and exclusion comparisons.
python scripts/test-balance.py --seeds 100

# More evidence, using a fresh seed range to confirm the first report.
python scripts/test-balance.py --seeds 1000 --first-seed 10001

# Test an edited economy without changing the default game configuration.
python scripts/test-balance.py --economy config/economy.json --seeds 100

# Replay one seed, recording trades, arrivals, gifts and cargo losses.
python scripts/test-balance.py --first-seed 42 --seeds 1 --trace

# Explicit output directory must be new or empty.
python scripts/test-balance.py --seeds 100 --output build/balance-reports/my-experiment
```

The default 19-commodity economy produces **8,000 complete games for 100 seeds**.
`--seeds` means seeds per experiment, not the total number of games. Use the same
seed range when comparing economy revisions, and a fresh range to confirm a result.
Each output saves the input economy, settings, source hashes, raw results and
timings. Reports are stored under the ignored `build/` directory by default.
Nonempty directories are rejected to preserve previous evidence. A failed batch
has `status: failed` in `metadata.json` and must not be treated as a complete report.

## What the experiments measure

| Experiment | Purpose |
| --- | --- |
| Mixed `profit` | Choose a good and route by expected dollar gain with current cash and jacket space. |
| Mixed `roi` | Choose by expected percentage return, highlighting capital efficiency. |
| Mixed `rotate` | Use seed-dependent fixed city strides and pick the best forecast trade on that route. Some strides visit only a subset of cities; common-route probes provide broader coverage. |
| Mixed `upgrades` | Dollar-gain strategy with conservative jacket and exploit purchases. |
| `focus-start` | Buy only the target good using normal starting funds: can it get going at all? |
| `focus-funded` | Buy only the target good with equal funds (default $1,000,000): how does it earn once accessible? |
| `without` (profit and ROI) | Compare each mixed strategy with the same strategy prohibited from buying one commodity: does access to it help? |
| Common-route probes | Compare every good on the exact same market arrivals, using normal starting funds and a fixed larger budget. No compounding or cargo-loss risk. |

All actual games use real encounter handling, the day-25 change, real cargo
limits, integer purchases/sales, and final buyout rules. Officers are fled unless
an exploit is available. Only the mixed `upgrades` policy buys equipment. Every
arrival is acknowledged as in the UI, but no screens are rendered. Policies sell
their target holdings on arrival when a quote is available and buy at most one
commodity for the next hop when its forecast gain is positive. Forecasts use
configured typical city prices and known active effects, **never future quotes or
future random draws**. These are defined benchmarks, not optimal play.

Focused games retain unrelated free stash goods until final-day sales, so those
gifts cannot finance entry into the target commodity. They still occupy cargo
slots and can affect encounters. Same-commodity gifts do benefit its results;
gift values and losses are included in the output to make that visible.

Market probes gate purchases using the same forecast rule on random routes. They
measure one-hop opportunities separately for departures on days 1–10, 11–24 and
25 onward. An unavailable arrival quote is valued at zero; unsellable cargo in a
real game can instead be carried onward. Their ROI is price-only, whereas profit
per hop includes zero contributions from idle/unaffordable hops and respects the
normal jacket capacity. Probes are a diagnostic, not a substitute for the games.

## Reading the report

Open **`report.md`** first. It contains strategy profit distributions, commodity
rankings and the interpretation rules. Then inspect:

- **`commodities.csv`**: normal-start access, funded profit and downside, net return
  on purchase spend, gifts/losses, selection rates and contributions for each policy.
- **`market-opportunities.csv`**: phase-specific availability, affordability, ROI,
  win rate, profit per lot and profit per hop under both budgets.
- **`runs.csv`**: score, net profit and seed for every actual game.
- **`results.jsonl`**: per-game commodity accounting and per-seed probe totals.
- **`metadata.json` / `economy.json`**: source identity, exact input and settings.
- **`trades.jsonl`**: optional detailed action trace (`--trace`; can be large).

Flags are **review candidates**, not declarations that a commodity is broken.
The funded growth metric compares each commodity with median peer growth on the
same seed. It uses only that target commodity's cash contribution, excluding
unrelated gifts sold at the end. It takes a geometric mean to reduce domination
by a single spectacular run. The 95% bootstrap interval resamples **whole paired
seeds**, rather than treating trades from one run as independent observations.
At least 30 seeds and trades on at least half of the seeds (minimum 15) are needed.
The entire interval must exceed **1.5×** peer growth or fall below **1/1.5×**
to flag a candidate. `--threshold` and `--bootstrap` adjust these settings.
Intervals are exploratory and are not adjusted for multiple comparisons.

Use the mixed strategy selection rates, early-game access and market ROI to judge
whether a candidate is fulfilling an intended role. A cheap commodity can be vital
at the start and understandably weak once all cargo slots are occupied. A costly
commodity can generate large dollar gains while offering ordinary percentage returns.
Confirm suspected imbalances on fresh seeds and more than one policy before tuning.

The paired exclusion comparisons provide a second view of practical importance.
An inclusion growth ratio of 1.20 means 20% more final wealth with that commodity
available than with its purchases prohibited. A ratio near 1 means that policy
gets little benefit from access to it. A ratio below 1 points to a strategy that
could improve its choices, rather than proving the commodity is intrinsically
harmful. Free stash gifts are still possible, and route and cargo changes can
change later events. The report includes paired confidence intervals for both
strategies; these comparisons use normal starting funds.

## Checking the tester

After a first build:

```powershell
python -m unittest discover -s scripts/balance -p test_balance.py -v
```

Checks cover paired statistics, exact seed/trace replay, a known-price toy economy,
cargo-loss accounting, excluded-good purchase prevention, missing market availability,
identification of a known slot-value imbalance, report generation and prevention
of a false flag when there is only one identical peer. The runner also checks cash,
cargo, quotes and profit reconciliation throughout every batch. This tool tests
balance and game rules; screen, artwork and keyboard testing remain separate.
Test fixtures are retained under `build/balance-test-tmp/` for diagnosis.
