"""Database integrity and statistical regression checks; GPU fixtures run separately."""
import json
from pathlib import Path
import sys
import tempfile
import unittest

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[2]/'tools'))
from ground_truth_workflow import digest, metrics, safe_child, verify, write_json


class GroundTruthWorkflowTests(unittest.TestCase):
    def test_noise_correction_preserves_block_covariance(self):
        truth = np.ones((16, 16, 3))
        noise = np.ones_like(truth)*.2
        noise[8:] *= -1
        result = metrics(truth+noise, truth, noise, np.ones((16, 16), bool))
        self.assertAlmostEqual(result['noise_corrected_rms'], 0)
        self.assertAlmostEqual(result['block_rms'], 0)
        self.assertAlmostEqual(result['relative_bias'], 0)
        self.assertAlmostEqual(result['reference_noise_rms'], .2)

    def test_bias_and_partial_blocks(self):
        truth = np.ones((9, 11, 3))
        result = metrics(truth*1.1, truth, np.zeros_like(truth), np.ones((9, 11), bool))
        self.assertAlmostEqual(result['relative_bias'], .1)
        self.assertAlmostEqual(result['noise_corrected_rms'], .1)
        self.assertAlmostEqual(result['block_rms'], .1)

    def test_no_receivers_is_an_error(self):
        with self.assertRaises(ValueError):
            metrics(np.zeros((2, 2, 3)), np.zeros((2, 2, 3)), np.zeros((2, 2, 3)), np.zeros((2, 2), bool))

    def test_paths_cannot_escape_dataset(self):
        with tempfile.TemporaryDirectory() as directory:
            with self.assertRaises(ValueError):
                safe_child(Path(directory), '../truth.npz')

    def test_modified_or_unhashed_truth_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            frame = root/'frames'/'C1_L1'
            frame.mkdir(parents=True)
            write_json(frame/'scene.zenrender', {})
            write_json(frame/'frame.json', dict(setup_sha256=digest(frame/'scene.zenrender'), validation=dict(passed=True)))
            (frame/'truth.npz').write_bytes(b'fixture')
            files = {path.relative_to(root).as_posix(): digest(path) for path in frame.iterdir()}
            manifest = dict(version=1, frames=[dict(id='C1_L1')], files=files, inputs={}, tools={})
            write_json(root/'dataset.json', manifest)
            verify(root)
            (frame/'truth.npz').write_bytes(b'changed')
            with self.assertRaisesRegex(ValueError, 'hash mismatch'):
                verify(root)
            del manifest['files']['frames/C1_L1/truth.npz']
            write_json(root/'dataset.json', manifest)
            with self.assertRaisesRegex(ValueError, 'Unhashed'):
                verify(root)


if __name__ == '__main__':
    unittest.main()
