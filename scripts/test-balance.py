#!/usr/bin/env python3
"""Build/run repeatable commodity balance experiments. Python standard library only."""
import argparse
import collections
import csv
import datetime as dt
import hashlib
import json
import math
import os
from pathlib import Path
import platform
import random
import shutil
import statistics
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[1]
VERSION = 1


def percentile(values, fraction):
    ordered = sorted(values)
    if not ordered:
        return 0.0
    position = (len(ordered) - 1) * fraction
    lo = int(position)
    return ordered[lo] + (ordered[min(lo + 1, len(ordered) - 1)] - ordered[lo]) * (position - lo)


def paired_interval(differences, repetitions):
    """Resample whole seeds, not the correlated trades inside a game."""
    if len(differences) < 2:
        return None
    rng = random.Random(814279)
    count = len(differences)
    means = [sum(rng.choices(differences, k=count)) / count for _ in range(repetitions)]
    return [math.exp(percentile(means, .025)), math.exp(percentile(means, .975))]


def write_csv(path, rows):
    if not rows:
        return
    with path.open('w', newline='', encoding='utf-8') as stream:
        writer = csv.DictWriter(stream, fieldnames=list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def run_command(command):
    subprocess.run([str(part) for part in command], check=True)


def build_runner(build_dir):
    cmake = shutil.which('cmake')
    generator_args = []
    if os.name == 'nt':
        # The Visual Studio generator discovers the compiler/SDK without modifying
        # the user's environment or requiring an interactive developer shell.
        vswhere = Path(os.environ.get('ProgramFiles(x86)', 'C:/Program Files (x86)')) / (
            'Microsoft Visual Studio/Installer/vswhere.exe')
        if vswhere.is_file():
            installations = json.loads(subprocess.check_output([
                str(vswhere), '-latest', '-products', '*', '-requires',
                'Microsoft.VisualStudio.Component.VC.Tools.x86.x64', '-format', 'json'
            ], encoding='utf-8-sig'))
            if installations:
                vs = installations[0]
                if not cmake:
                    candidate = Path(vs['installationPath']) / (
                        'Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe')
                    if candidate.is_file():
                        cmake = str(candidate)
                major = int(vs['installationVersion'].split('.')[0])
                year = {16: 2019, 17: 2022, 18: 2026}.get(major)
                if year:
                    generator_args = ['-G', f'Visual Studio {major} {year}', '-A', 'x64']
    if not cmake:
        raise RuntimeError('CMake and a C++17 compiler are required. On Windows, install '
                           'Visual Studio Build Tools with C++ and CMake tools.')
    command = [cmake, '-S', ROOT / 'scripts/balance', '-B', build_dir,
               '-DCMAKE_BUILD_TYPE=Release']
    if not (build_dir / 'CMakeCache.txt').exists():
        command += generator_args
    run_command(command)
    run_command([cmake, '--build', build_dir, '--config', 'Release', '--parallel', '2'])
    name = 'ai-wars-balance.exe' if os.name == 'nt' else 'ai-wars-balance'
    for candidate in (build_dir / 'Release' / name, build_dir / name):
        if candidate.is_file():
            return candidate
    raise RuntimeError('Build completed but the balance runner could not be located.')


def analyze(directory, metadata):
    runs, probes = [], collections.defaultdict(collections.Counter)
    with (directory / 'results.jsonl').open(encoding='utf-8') as stream:
        for line in stream:
            item = json.loads(line)
            if item['type'] == 'run':
                # Also check reconciliation outside the runner: reporting must not
                # double-subtract lost cargo, whose purchase cost is already spent.
                if sum(g['net'] for g in item['goods']) - item['equipment_cost'] != item['net_profit']:
                    raise ValueError(f"Profit accounting mismatch on seed {item['seed']}")
                # Raw JSONL preserves full evidence. Keep only the detail needed
                # for summaries in memory, especially for large exclusion batches.
                item.pop('events', None)
                if item['mode'] == 'without':
                    item['goods'] = []
                elif item['focus']:
                    item['goods'] = [g for g in item['goods'] if g['good'] == item['focus']]
                runs.append(item)
            elif item['type'] == 'probe':
                key = (item['good'], item['phase'])
                probes[key].update({k: v for k, v in item.items()
                                    if k not in ('type', 'seed', 'good', 'phase')})
            else:
                raise ValueError('Unrecognized result record')
    args, economy = metadata['options'], metadata['economy']
    good_ids = [good['id'] for good in economy['goods']]
    expected_games = args['seeds'] * (4 + 4 * len(good_ids))
    if len(runs) != expected_games:
        raise ValueError(f'Incomplete results: expected {expected_games} games, got {len(runs)}')
    focus = {(r['mode'], r['focus'], r['seed']): r for r in runs if r['focus']}
    mixed = [r for r in runs if r['mode'] == 'mixed']
    without = {(r['excluded'], r['policy'], r['seed']): r for r in runs if r['mode'] == 'without'}
    mixed_runs = {(r['policy'], r['seed']): r for r in mixed}
    seeds = range(args['first_seed'], args['first_seed'] + args['seeds'])
    # Use the target good's cash contribution, excluding unrelated end-of-run gifts.
    growth, funded_rows = {}, {}
    for good in good_ids:
        for seed in seeds:
            run = focus['focus-funded', good, seed]
            metric = next(g for g in run['goods'] if g['good'] == good)
            funded_rows[good, seed] = metric
            growth[good, seed] = math.log(max(1, run['starting_cash'] + metric['net']) / run['starting_cash'])
    benchmarks = {seed: statistics.median(growth[g, seed] for g in good_ids) for seed in seeds}
    mixed_lookup = {(r['policy'], r['seed']): {g['good']: g for g in r['goods']} for r in mixed}
    commodities = []
    for good in good_ids:
        start_runs = [focus['focus-start', good, seed] for seed in seeds]
        start_rows = [next(g for g in r['goods'] if g['good'] == good) for r in start_runs]
        funded = [funded_rows[good, seed] for seed in seeds]
        funded_nets = [r['net'] for r in funded]
        differences = [growth[good, seed] - benchmarks[seed] for seed in seeds]
        ratio = math.exp(statistics.mean(differences))
        interval = paired_interval(differences, args['bootstrap'])
        traded = sum(r['buys'] > 0 for r in funded)
        enough = args['seeds'] >= 30 and traded >= max(15, math.ceil(args['seeds'] / 2))
        flag = 'within threshold / uncertain'
        if not enough:
            flag = 'insufficient trading evidence'
        elif interval[0] > args['threshold']:
            flag = 'HIGH funded growth candidate'
        elif interval[1] < 1 / args['threshold']:
            flag = 'LOW funded growth candidate'
        row = {
            'good': good, 'flag': flag,
            'funded_growth_vs_peer': round(ratio, 5),
            'paired_95_low': round(interval[0], 5) if interval else '',
            'paired_95_high': round(interval[1], 5) if interval else '',
            'start_traded_pct': 100 * sum(r['buys'] > 0 for r in start_rows) / len(start_rows),
            'start_median_first_buy_day': statistics.median(
                [r['first_buy_day'] for r in start_rows if r['first_buy_day']])
                if any(r['first_buy_day'] for r in start_rows) else '',
            'start_median_net': statistics.median(r['net'] for r in start_rows),
            'funded_traded_pct': 100 * traded / len(funded),
            'funded_median_net': statistics.median(funded_nets),
            'funded_p10_net': percentile(funded_nets, .1),
            'funded_p90_net': percentile(funded_nets, .9),
            'funded_loss_pct': 100 * sum(n < 0 for n in funded_nets) / len(funded),
            'funded_spent': sum(r['spent'] for r in funded),
            'funded_net_on_spent_pct': 100 * sum(funded_nets) / max(1, sum(r['spent'] for r in funded)),
            'funded_lost_lots': sum(r['lost_lots'] for r in funded),
            'funded_gift_value_at_receipt': sum(r['gift_value_at_receipt'] for r in funded),
        }
        for policy in ('profit', 'roi', 'rotate', 'upgrades'):
            group = [mixed_lookup[policy, seed].get(good, {}) for seed in seeds]
            row[policy + '_picked_pct'] = 100 * sum(g.get('buys', 0) > 0 for g in group) / args['seeds']
            row[policy + '_net_per_game'] = sum(g.get('net', 0) for g in group) / args['seeds']
        for policy in ('profit', 'roi'):
            paired = [math.log(max(1, mixed_runs[policy, seed]['score']) /
                               max(1, without[good, policy, seed]['score'])) for seed in seeds]
            ci = paired_interval(paired, args['bootstrap'])
            row[policy + '_inclusion_growth_ratio'] = math.exp(statistics.mean(paired))
            row[policy + '_inclusion_95_low'] = ci[0] if ci else ''
            row[policy + '_inclusion_95_high'] = ci[1] if ci else ''
            row[policy + '_inclusion_median_profit_change'] = statistics.median(
                mixed_runs[policy, seed]['net_profit'] - without[good, policy, seed]['net_profit']
                for seed in seeds)
        commodities.append(row)
    commodities.sort(key=lambda r: r['funded_growth_vs_peer'], reverse=True)
    opportunities = []
    for (good, phase), total in sorted(probes.items()):
        selected = total['selected']
        opportunities.append({
            'good': good, 'phase': phase, 'hops': total['hops'],
            'departure_available_pct': 100 * total['available'] / max(1, total['hops']),
            'arrival_unavailable_pct': 100 * total['sale_unavailable'] / max(1, total['available']),
            'starting_cash_affordable_pct': 100 * total['starting_affordable'] / max(1, total['available']),
            'probe_cash_affordable_pct': 100 * total['affordable'] / max(1, total['available']),
            'forecast_selected_hops': selected,
            'win_pct': 100 * total['wins'] / selected if selected else '',
            'mean_roi_pct': 100 * total['roi_sum'] / selected if selected else '',
            'mean_gain_per_lot': total['gain_per_lot_sum'] / selected if selected else '',
            'starting_cash_profit_per_hop': total['starting_profit'] / max(1, total['hops']),
            'probe_cash_profit_per_hop': total['budget_profit'] / max(1, total['hops']),
        })
    run_rows = [{k: v for k, v in r.items() if k not in ('type', 'goods', 'events')} for r in runs]
    write_csv(directory / 'runs.csv', run_rows)
    write_csv(directory / 'commodities.csv', commodities)
    write_csv(directory / 'market-opportunities.csv', opportunities)
    policies = []
    for policy in ('profit', 'roi', 'rotate', 'upgrades'):
        group = [r for r in mixed if r['policy'] == policy]
        profits = [r['net_profit'] for r in group]
        policies.append({'policy': policy, 'games': len(group), 'median_profit': statistics.median(profits),
                         'p10_profit': percentile(profits, .1), 'p90_profit': percentile(profits, .9),
                         'loss_pct': 100 * sum(p < 0 for p in profits) / len(profits)})
    summary = {'games': len(runs), 'seeds': args['seeds'], 'policies': policies, 'commodities': commodities}
    (directory / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n', encoding='utf-8')
    report(directory, metadata, summary)
    return summary


def report(directory, metadata, summary):
    args = metadata['options']
    lines = ['# Commodity balance report', '',
        f"{summary['games']:,} complete games across {summary['seeds']:,} seeds. "
        f"Simulation: {metadata['simulation_seconds']:.2f}s. "
        f"Economy SHA-256: `{metadata['economy_sha256']}`.", '',
        '## Unrestricted trading (normal starting funds)', '',
        '| Strategy | Median net profit | 10th–90th percentile | Losing games |',
        '| --- | ---: | ---: | ---: |']
    for row in summary['policies']:
        lines.append(f"| {row['policy']} | ${row['median_profit']:,.0f} | "
                     f"${row['p10_profit']:,.0f} – ${row['p90_profit']:,.0f} | {row['loss_pct']:.1f}% |")
    lines += ['', '## Commodity comparison', '',
        f"Funded focus runs start with ${args['funded_cash']:,} and the normal jacket. "
        'Each row permits buying only that commodity. These expose late-game earning potential; '
        'normal-start runs expose the cost of getting started.', '',
        '| Commodity | Funded growth / peer | Paired 95% interval | Funded median net | Normal-start traded | Finding |',
        '| --- | ---: | --- | ---: | ---: | --- |']
    for row in summary['commodities']:
        ci = (f"{row['paired_95_low']:.2f}–{row['paired_95_high']:.2f}×"
              if row['paired_95_low'] != '' else 'insufficient seeds')
        lines.append(f"| {row['good']} | {row['funded_growth_vs_peer']:.2f}× | {ci} | "
                     f"${row['funded_median_net']:,.0f} | {row['start_traded_pct']:.0f}% | {row['flag']} |")
    lines += ['', '## Does including the commodity help?', '',
        'These paired normal-start runs prohibit purchasing one good at a time. '
        'Ratios compare final wealth with the good available versus excluded; '
        '1.20× means 20% more final wealth when it can be bought. '
        'Free stash gifts remain possible in both runs. '
        'Differences include route changes, subsequent events and replacement trades.', '',
        '| Commodity | Profit strategy ratio (95% interval) | ROI strategy ratio (95% interval) |',
        '| --- | ---: | ---: |']
    for row in summary['commodities']:
        cells = []
        for policy in ('profit', 'roi'):
            ci = (f"{row[policy + '_inclusion_95_low']:.2f}–{row[policy + '_inclusion_95_high']:.2f}"
                  if row[policy + '_inclusion_95_low'] != '' else 'insufficient seeds')
            cells.append(f"{row[policy + '_inclusion_growth_ratio']:.2f}× ({ci})")
        lines.append('| ' + row['good'] + ' | ' + ' | '.join(cells) + ' |')
    lines += ['', '## How to interpret the findings', '',
        '- HIGH/LOW are balance review candidates, not automatic nerf/buff recommendations. '
        'Cheap opening goods and expensive late-game goods can legitimately play different roles.',
        '- Inclusion ratios near 1 mean the tested policy gets little benefit from being able to buy that good. '
        'Ratios above 1 show a contribution to that policy; ratios below 1 expose a policy choosing trades '
        'that a better alternative could replace. These do not prove a commodity is intrinsically harmful.',
        '- Growth is the geometric mean of (starting cash + target commodity cash contribution) / starting cash. '
        'The peer benchmark is the median log growth of all commodities on the same seed. '
        'Unrelated free cargo is excluded from this metric. Zero wealth is floored at $1 for the logarithm.',
        f"- A candidate requires at least 30 seeds, trading on at least half the seeds (minimum 15), "
        f"and its entire paired bootstrap interval beyond {args['threshold']:.2f}× "
        f"or below {1 / args['threshold']:.2f}× peer growth. "
        'Intervals are exploratory 95% intervals, not corrected for testing multiple commodities. '
        'Confirm candidates using a fresh seed range.',
        '- These policies use configured typical city prices and currently visible quotes. '
        'They do not inspect future RNG or preview flights. Their performance is not a prediction of every human player.',
        '- All production price events, day-25 changes, confiscations, muggings, family losses and stashes remain enabled. '
        'Focused runs retain unrelated gifts until final-day sales; those gifts occupy real cargo space. '
        'Same-commodity gifts contribute to its profit; check gift values before attributing growth entirely to trading.',
        '- Only the upgrades policy buys equipment. It buys at most one jacket upgrade per Black Hat visit '
        'and replaces an exhausted exploit if cash is at least four times its cost, with more than five days remaining. '
        'Other policies flee officers unless already equipped.',
        '- Cash contribution = sales + final cargo buyout - purchases. Lost cargo is already paid for; '
        'its cost must not be deducted a second time. Summed contributions minus equipment costs '
        'are checked against each final score minus starting funds.',
        '- Market probes use the same random route and prices for every commodity, fixed cash budgets and no cargo. '
        'They buy only when the next city\'s forecast exceeds the visible buy price. '
        'Profit per hop includes idle and unaffordable hops as zero; ROI is price-only and may include unaffordable quotes. '
        'An unavailable arrival quote is valued at zero. Probes exclude cargo risks and do not compound.',
        '- Phase labels describe the departure day; day 24 → 25 belongs to the middle phase. '
        'Compare early/middle/late ROI and profit per hop in market-opportunities.csv to distinguish '
        'percentage returns, slot value, affordability and availability.',
        '- This is a game-rules balance test. It does not validate screens, artwork or keyboard controls.', '',
        '## Files and reproduction', '',
        '- `runs.csv`: final score and profit for every game; seeds reproduce individual results.',
        '- `commodities.csv`: rankings, uncertainty, coverage, losses, gifts, and selection by strategy.',
        '- `market-opportunities.csv`: common-route price opportunities by commodity and game phase.',
        '- `results.jsonl`: complete per-game commodity accounting and per-seed probe totals.',
        '- `metadata.json` and `economy.json`: settings, timings, source hashes, and exact input economy.',
        '- `trades.jsonl`: action trace when requested with `--trace`.', '',
        'Rerun command (same source/compiler required for an exact replay):', '', '```text',
        'python scripts/test-balance.py --seeds ' + str(args['seeds'])
        + ' --first-seed ' + str(args['first_seed']) + ' --funded-cash ' + str(args['funded_cash'])
        + ' --probe-cash ' + str(args['probe_cash']) + ' --bootstrap ' + str(args['bootstrap'])
        + ' --threshold ' + str(args['threshold']) + ' --economy "' + str(directory / 'economy.json')
        + '"' + (' --trace' if args['trace'] else ''), '```', '']
    (directory / 'report.md').write_text('\n'.join(lines), encoding='utf-8')


def parser():
    cli = argparse.ArgumentParser(description=__doc__)
    cli.add_argument('--seeds', type=int, default=100, help='seeds per strategy and commodity (default: 100)')
    cli.add_argument('--first-seed', type=int, default=1)
    cli.add_argument('--funded-cash', type=int, default=1_000_000, help='equal starting cash in funded focus runs')
    cli.add_argument('--probe-cash', type=int, default=1_000_000, help='fixed common-route probe budget')
    cli.add_argument('--bootstrap', type=int, default=1000, help='paired bootstrap resamples per commodity')
    cli.add_argument('--threshold', type=float, default=1.5, help='growth ratio threshold for review candidates')
    cli.add_argument('--economy', type=Path, default=ROOT / 'config/economy.json')
    cli.add_argument('--output', type=Path, help='new or empty report directory; defaults to build/balance-reports/<time>')
    cli.add_argument('--build-dir', type=Path, default=ROOT / 'build/balance-tester')
    cli.add_argument('--trace', action='store_true', help='record every trade/event (larger output)')
    return cli


def main(argv=None):
    cli = parser()
    args = cli.parse_args(argv)
    if args.seeds < 1 or args.first_seed < 0 or args.first_seed + args.seeds > 2 ** 64:
        cli.error('seeds must be positive and the seed range must fit unsigned 64-bit integers')
    if not 1 <= args.funded_cash <= 1_000_000_000 or not 1 <= args.probe_cash <= 1_000_000_000:
        cli.error('cash budgets must be from 1 to 1,000,000,000')
    if args.bootstrap < 100 or not math.isfinite(args.threshold) or args.threshold <= 1:
        cli.error('bootstrap must be at least 100 and threshold must be finite and greater than 1')
    economy_path = args.economy.resolve()
    economy_bytes = economy_path.read_bytes()
    economy = json.loads(economy_bytes)
    output_dir = (args.output or ROOT / 'build/balance-reports' /
                  dt.datetime.now().strftime('%Y%m%d-%H%M%S-%f')).resolve()
    if output_dir.exists() and any(output_dir.iterdir()):
        cli.error('output directory must be new or empty; previous results will not be overwritten')
    start = time.perf_counter()
    runner = build_runner(args.build_dir.resolve())
    output_dir.mkdir(parents=True, exist_ok=True)
    (output_dir / 'economy.json').write_bytes(economy_bytes)
    hashes = {}
    for relative in ('scripts/test-balance.py', 'scripts/balance/runner.cpp', 'scripts/balance/CMakeLists.txt',
                     'src/model/game.cpp', 'src/model/game.h', 'src/model/economy.cpp',
                     'src/model/economy.h', 'src/model/encounter_text.h', 'src/model/json_input.h',
                     'third_party/nlohmann/json.hpp'):
        hashes[relative] = digest(ROOT / relative)
    metadata = {'format_version': VERSION, 'created_utc': dt.datetime.now(dt.timezone.utc).isoformat(),
                'platform': platform.platform(), 'python': sys.version, 'options': {
                    k: str(v) if isinstance(v, Path) else v for k, v in vars(args).items()},
                'economy': economy, 'economy_sha256': hashlib.sha256(economy_bytes).hexdigest(),
                'source_sha256': hashes, 'runner_sha256': digest(runner), 'status': 'running'}
    meta_path = output_dir / 'metadata.json'
    meta_path.write_text(json.dumps(metadata, indent=2) + '\n', encoding='utf-8')
    games = args.seeds * (4 + 4 * len(economy['goods']))
    print(f'Running {games:,} games over {args.seeds:,} seeds. Reports: {output_dir}', flush=True)
    simulation = time.perf_counter()
    try:
        run_command([runner, output_dir / 'economy.json', output_dir, args.seeds, args.first_seed,
                     args.funded_cash, args.probe_cash, int(args.trace)])
        metadata['simulation_seconds'] = time.perf_counter() - simulation
        summary = analyze(output_dir, metadata)
        metadata['status'] = 'complete'
        metadata['total_seconds'] = time.perf_counter() - start
    except BaseException as error:
        metadata['status'] = 'failed'
        metadata['error'] = str(error)
        raise
    finally:
        meta_path.write_text(json.dumps(metadata, indent=2) + '\n', encoding='utf-8')
    candidates = [r['good'] + ': ' + r['flag'] for r in summary['commodities'] if 'candidate' in r['flag']]
    print(f"Completed {summary['games']:,} games in {metadata['simulation_seconds']:.2f}s simulation, "
          f"{metadata['total_seconds']:.2f}s total (including build/report).")
    print('Review candidates: ' + ('; '.join(candidates) if candidates else 'none at this threshold'))
    print('Report: ' + str(output_dir / 'report.md'))
    return 0


if __name__ == '__main__':
    try:
        sys.exit(main())
    except (OSError, ValueError, RuntimeError, subprocess.CalledProcessError) as error:
        print(f'Balance testing failed: {error}', file=sys.stderr)
        sys.exit(1)
