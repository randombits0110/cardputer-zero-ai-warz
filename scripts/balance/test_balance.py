"""Checks for the experiment/reporting tool; no third-party Python dependencies."""
import copy
import importlib.util
import json
import math
from pathlib import Path
import subprocess
import unittest
import uuid

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location('balance_tool', ROOT / 'scripts/test-balance.py')
balance = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(balance)


class StatisticsTests(unittest.TestCase):
    def test_constant_paired_effect_has_exact_interval(self):
        low, high = balance.paired_interval([math.log(2)] * 40, 100)
        self.assertAlmostEqual(low, 2)
        self.assertAlmostEqual(high, 2)

    def test_resampling_is_reproducible_and_preserves_pair_differences(self):
        sample = [-.5, -.2, 0, .1, .8] * 10
        first = balance.paired_interval(sample, 500)
        self.assertEqual(first, balance.paired_interval(sample, 500))
        self.assertLess(first[0], math.exp(sum(sample) / len(sample)))
        self.assertGreater(first[1], math.exp(sum(sample) / len(sample)))

    def test_single_seed_has_no_confidence_interval(self):
        self.assertIsNone(balance.paired_interval([0], 100))


class RunnerTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        choices = [ROOT / 'build/balance-tester/Release/ai-wars-balance.exe',
                   ROOT / 'build/balance-tester/ai-wars-balance']
        cls.runner = next((path for path in choices if path.is_file()), None)
        if cls.runner is None:
            raise unittest.SkipTest('Run scripts/test-balance.py first to build the runner')

    def setUp(self):
        temporary_root = ROOT / 'build/balance-test-tmp'
        temporary_root.mkdir(parents=True, exist_ok=True)
        # Preserve fixtures in the ignored build directory for failed-test diagnosis.
        # Ordinary mkdir also avoids Python 3.14's owner-only Windows temp ACL.
        self.directory = temporary_root / uuid.uuid4().hex
        self.directory.mkdir()

    def run_experiment(self, economy, name, seeds=40):
        directory = self.directory / name
        directory.mkdir()
        config = directory / 'economy.json'
        config.write_text(json.dumps(economy), encoding='utf-8')
        result = subprocess.run([str(self.runner), str(config), str(directory), str(seeds), '1', '1000',
                                 '1000', '1'], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        rows = [json.loads(line) for line in (directory / 'results.jsonl').read_text().splitlines()]
        return directory, rows

    @staticmethod
    def economy():
        economy = json.loads((ROOT / 'config/economy.json').read_text())
        good = copy.deepcopy(economy['goods'][0])
        good['price'] = {'min': 10, 'max': 10}
        economy['goods'] = [good]
        economy['cities'] = copy.deepcopy(economy['cities'][:2])
        for city, modifier in zip(economy['cities'], (100, 200)):
            city['price_percent'] = {good['id']: modifier}
        economy['news'] = [copy.deepcopy(event) for event in economy['news'] if event['good'] == good['id']]
        for event in economy['news']:
            event['weight'] = 0
        economy['jackets'] = []
        economy['market_wide_news']['chance_basis_points'] = 0
        economy['rules'].update({'days': 2, 'starting_cash': 100, 'starting_capacity': 10,
                                  'guaranteed_opening_good': good['id']})
        return economy

    def test_exact_replay_and_cash_accounting_with_losses(self):
        economy = self.economy()
        first_dir, first = self.run_experiment(economy, 'first')
        second_dir, second = self.run_experiment(economy, 'second')
        self.assertEqual(first, second)
        self.assertEqual((first_dir / 'trades.jsonl').read_bytes(),
                         (second_dir / 'trades.jsonl').read_bytes())
        runs = [row for row in first if row['type'] == 'run']
        self.assertEqual(len(runs), 40 * 8)
        self.assertTrue(any(g['lost_lots'] > 0 for r in runs for g in r['goods']))
        for row in runs:
            self.assertEqual(sum(g['sales'] + g['buyout'] - g['spent'] for g in row['goods']),
                             row['score'] - row['starting_cash'] + row['equipment_cost'])
        clean = [r for r in runs if r['mode'] == 'focus-start'
                 and not any(g['lost_lots'] or g['gift_lots'] for g in r['goods'])]
        self.assertTrue(clean)
        for row in clean:
            self.assertEqual(row['net_profit'], 100)  # ten $10 lots sold for $20
        for row in runs:
            if row['mode'] == 'without':
                self.assertTrue(all(g['buys'] == 0 for g in row['goods']))

    def test_equal_peer_is_not_flagged_and_zero_trade_focus_is_retained(self):
        economy = self.economy()
        directory, _ = self.run_experiment(economy, 'one-peer')
        metadata = {'economy': economy, 'economy_sha256': 'fixture', 'simulation_seconds': 0,
                    'options': {'seeds': 40, 'first_seed': 1, 'bootstrap': 100,
                                'threshold': 1.5, 'funded_cash': 1000, 'probe_cash': 1000,
                                'trace': True}}
        summary = balance.analyze(directory, metadata)
        row = summary['commodities'][0]
        self.assertEqual(row['funded_growth_vs_peer'], 1)
        self.assertNotIn('candidate', row['flag'])
        self.assertEqual(row['paired_95_low'], 1)
        self.assertTrue((directory / 'report.md').is_file())
        # An unavailable commodity must still get focus rows, not disappear from
        # the denominator or be mislabeled weak based on nonexistent trades.
        for city in economy['cities']:
            city['price_percent'] = {economy['goods'][0]['id']: 0}
        # Opening-good affordability is guaranteed by the engine on day 1, so
        # check that no later purchases occur and the zero-opportunity probe is safe.
        _, rows = self.run_experiment(economy, 'unavailable', seeds=1)
        probes = [r for r in rows if r['type'] == 'probe']
        self.assertTrue(all(p['selected'] == 0 for p in probes))
        self.assertTrue(all(len(r['goods']) >= 1 for r in rows if r['type'] == 'run' and r['focus']))

    def test_known_slot_value_imbalance_is_flagged(self):
        economy = self.economy()
        expensive = copy.deepcopy(economy['goods'][0])
        expensive.update({'id': 'expensive', 'name': 'Expensive test good', 'short_name': 'Expensive',
                          'price': {'min': 100, 'max': 100}})
        economy['goods'].append(expensive)
        original_news = copy.deepcopy(economy['news'])
        for event in original_news:
            event.update({'id': 'test-' + event['id'], 'good': 'expensive'})
        economy['news'].extend(original_news)
        for city in economy['cities']:
            city['price_percent']['expensive'] = next(iter(city['price_percent'].values()))
        directory, _ = self.run_experiment(economy, 'known-imbalance')
        summary = balance.analyze(directory, {'economy': economy, 'economy_sha256': 'fixture',
            'simulation_seconds': 0, 'options': {'seeds': 40, 'first_seed': 1, 'bootstrap': 100,
                'threshold': 1.1, 'funded_cash': 1000, 'probe_cash': 1000, 'trace': True}})
        rows = {r['good']: r for r in summary['commodities']}
        self.assertEqual(rows['expensive']['flag'], 'HIGH funded growth candidate')
        self.assertEqual(rows[economy['goods'][0]['id']]['flag'], 'LOW funded growth candidate')


if __name__ == '__main__':
    unittest.main()
