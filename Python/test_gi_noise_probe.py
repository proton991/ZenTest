"""Regression checks for the motion and preset contracts used by quality gates."""
import sys
import tempfile
import unittest
from pathlib import Path

import numpy as np
from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools'))
from gi_noise_probe import motion_path, preset, WARMUP, denoiser_error_blocks, load_trace_breakdown
from gi_noise_gates import apply_frame_budgets, bounds, compare, frame_budget
from gi_removal_gate import compare_frame
from gi_stability import digest, write_json
from profile_hybrid_gi import actual_state


class NoiseProbeTests(unittest.TestCase):
    def test_trace_dump_checks_extent_and_preserves_cache_counters(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / 'trace.bin'
            header = np.array([0x52544947, 1, 2, 1, 8, 0], dtype='<u4')
            pixels = np.array([[4, 2, 1, 1, 3, 2, 0, 0], [8, 4, 2, 1, 5, 3, 0, 0]], dtype='<u4')
            valid = header.tobytes() + pixels.tobytes()
            path.write_bytes(valid)
            np.testing.assert_array_equal(load_trace_breakdown(path), pixels.reshape(1, 2, 8))
            for malformed in (valid[:-4], valid + b'\0', valid[:8] + np.array([3], dtype='<u4').tobytes() + valid[12:]):
                path.write_bytes(malformed)
                with self.assertRaisesRegex(ValueError, 'size'):
                    load_trace_breakdown(path)

    def test_error_blocks_decode_validation_and_ignore_background(self):
        data = np.zeros((3, 5, 18, 4), np.float32)
        data[1, 4, 8, 3] = 1
        data[1, 4, 13] = [2, 2, 8, 4]
        data[1, 4, 14] = [2, 2, 4, 4]
        data[1, 4, 15] = [16, 16, 32, 4]
        data[1, 4, 16] = [0, 3, 1, 0]
        data[1, 4, 17] = [3, 3, 0, 2]
        actual = np.full((3, 5, 3), 10.0)
        expected = np.zeros_like(actual)
        report = denoiser_error_blocks(data, actual, expected, block_size=2)
        self.assertEqual(len(report['blocks']), 1)
        block = report['blocks'][0]
        self.assertEqual((block['x'], block['y'], block['error_pixels']), (4, 0, 1))
        self.assertEqual(block['signals']['bounce']['clamp_sigmas_max'], 3)
        self.assertEqual(block['signals']['reflection']['footprint']['bicubic'], 1)
        self.assertEqual(block['signals']['visibility']['rejection']['plane'], 1)
        self.assertEqual(block['signals']['sky']['repair']['pyramid_3'], 1)
        with self.assertRaises(ValueError):
            denoiser_error_blocks(data[:, :, :13], actual, expected)

    def test_profile_uses_active_gi_extent_after_initial_pbr_frame(self):
        log = ('ZenApp render: 3840x2160, async-compute=available, RHI=threaded, '
               'ray-provider=compute, ray-query=unavailable, diagnostics=false\n'
               'ZenApp presentation: fullscreen=true, framebuffer=3840x2160\n'
               'ZenApp culling: requested=true, path=gpu_indirect, reason=\n'
               'ZenApp GI extent: 0x0, output=3840x2160\n'
               'ZenApp GI extent: 1920x1080, output=3840x2160\n')
        state = actual_state(log)
        self.assertEqual((state['gi_width'], state['gi_height']), (1920, 1080))
        self.assertEqual((state['render_width'], state['render_height']), (3840, 2160))

    def setUp(self):
        self.setup = dict(camera=dict(eye=[0., 0., 0.], target=[1., .2, 0.], sceneScale=2.),
                          rendering=dict(gi=dict(cone={})), run={})

    def test_pan_keeps_eye_and_pitch_and_has_constant_angular_speed(self):
        for kind, speed in (('pan-slow', .25), ('pan-fast', 1.)):
            keys = motion_path(self.setup, kind)['keys']
            for step, key in enumerate(keys[2:], 1):
                self.assertEqual(key['eye'], self.setup['camera']['eye'])
                self.assertEqual(key['frame'], WARMUP + step)
                direction = np.asarray(key['target']) - key['eye']
                self.assertAlmostEqual(direction[1], .2)
                self.assertAlmostEqual(np.linalg.norm(direction), np.sqrt(1.04))
                self.assertAlmostEqual(np.degrees(np.arctan2(-direction[2], direction[0])), speed * step)

    def test_endpoint_control_preserves_every_measured_pose(self):
        short = motion_path(self.setup, 'pan-slow', 64)['keys']
        extended = motion_path(self.setup, 'pan-slow', 65)['keys']
        self.assertEqual(short, extended[:-1])
        self.assertEqual(short[-1]['frame'], 224)
        self.assertNotEqual(short[-1]['target'], extended[-1]['target'])

    def test_dolly_preserves_orientation_and_has_bounded_translation(self):
        keys = motion_path(self.setup, 'dolly')['keys']
        for key in keys:
            np.testing.assert_allclose(np.asarray(key['target']) - key['eye'], [1., .2, 0.])
        self.assertAlmostEqual(np.linalg.norm(keys[65]['eye']), .005 * 2.)

    def test_presets_freeze_tiers_without_mutating_source(self):
        self.setup['rendering']['gi']['voxelizer'] = 1
        self.setup['rendering']['gi']['cone'].update(gather=0, probeSpacing=8)
        hardware = preset(self.setup, 'hardware')['rendering']['gi']['cone']
        compute = preset(self.setup, 'compute')['rendering']['gi']['cone']
        editor = preset(self.setup, 'editor')['rendering']['gi']['cone']
        self.assertEqual(self.setup['rendering']['gi']['cone'], dict(gather=0, probeSpacing=8))
        self.assertEqual(self.setup['rendering']['gi']['voxelizer'], 1)
        self.assertNotIn('voxelizer', preset(self.setup, 'hardware')['rendering']['gi'])
        self.assertNotIn('gather', hardware)
        self.assertNotIn('probeSpacing', hardware)
        self.assertNotIn('featureProviders', hardware)
        self.assertNotIn('featureProviders', compute)
        self.assertEqual(hardware['pixelResolutionDivisor'], 2)
        self.assertTrue(hardware['restirGI'])
        self.assertEqual(compute['pixelResolutionDivisor'], 2)
        self.assertNotIn('diffuseResolution', compute)
        self.assertFalse(compute['restirGI'])
        self.assertEqual(editor['pixelResolutionDivisor'], 1)
        self.assertFalse(editor['restirGI'])
        reduced = preset(self.setup, 'compute', 4)['rendering']['gi']['cone']
        expected = dict(compute, pixelResolutionDivisor=4)
        self.assertEqual(reduced, expected)

    def test_quality_gates_apply_aa_only_to_rms_and_require_both_repeats(self):
        limits = bounds({'hardware/static_rms': [2., 3.], 'hardware/static_flicker': [.1, .2],
                         'hardware/pan-fast/64': [8., 10.]}, denoiser=True)
        self.assertEqual(limits['hardware/static_rms']['maximum'], 4.)
        self.assertEqual(limits['hardware/static_flicker']['maximum'], .2)
        self.assertEqual(limits['hardware/pan-fast/64']['maximum'], 4.)
        candidate = {'hardware/static_rms': [4., 4.], 'hardware/static_flicker': [.1, .2],
                     'hardware/pan-fast/64': [3., 4.]}
        self.assertTrue(compare(limits, candidate)['passed'])
        candidate['hardware/pan-fast/64'] = [3., 4.01]
        self.assertFalse(compare(limits, candidate)['passed'])
        candidate['hardware/pan-fast/64'] = [3.]
        with self.assertRaises(ValueError):
            compare(limits, candidate)
        with self.assertRaises(ValueError):
            compare(limits, {})

    def test_phase_frame_budget_replaces_only_its_preset_and_keeps_the_frozen_limit(self):
        limits = bounds({'hardware/frame_median_ms': [31.7, 31.72], 'compute/frame_median_ms': [83.3, 85.2]},
                        denoiser=False)
        with tempfile.TemporaryDirectory() as temporary:
            summaries = [Path(temporary) / f'summary-{i}.json' for i in range(2)]
            for path, median in zip(summaries, (33.0, 33.1)):
                write_json(path, {'median_ms': median})
            budget = frame_budget('hardware', summaries)
            invalid = Path(temporary) / 'invalid.json'
            write_json(invalid, {'median_ms': 0.0})
            with self.assertRaises(ValueError):
                frame_budget('hardware', [invalid])
            hooked = Path(temporary) / 'hooked.json'
            write_json(hooked, {'median_ms': 33.0, 'rtss_running': True})
            with self.assertRaises(ValueError):
                frame_budget('hardware', [summaries[0], hooked])
        self.assertEqual(budget['maximum'], 33.1)
        limits = apply_frame_budgets(limits, {'hardware': budget})
        self.assertEqual(limits['hardware/frame_median_ms']['maximum'], 33.1)
        self.assertEqual(limits['hardware/frame_median_ms']['frozen_maximum'], 31.72)
        self.assertEqual(limits['compute/frame_median_ms']['maximum'], 85.2)
        self.assertTrue(compare(limits, {'hardware/frame_median_ms': [33.0, 33.1],
                                         'compute/frame_median_ms': [84., 85.]})['passed'])
        self.assertFalse(compare(limits, {'hardware/frame_median_ms': [33.0, 33.2],
                                          'compute/frame_median_ms': [84., 85.]})['passed'])
        with self.assertRaises(ValueError):
            apply_frame_budgets(limits, {'editor': budget})


class RemovalGateTests(unittest.TestCase):
    def test_single_channel_change_fails_pixel_identity(self):
        with tempfile.TemporaryDirectory() as temporary:
            baseline, candidate = Path(temporary) / 'baseline', Path(temporary) / 'candidate'
            baseline.mkdir()
            candidate.mkdir()
            pixels = np.full((3, 4, 3), 20, dtype=np.uint8)
            Image.fromarray(pixels).save(baseline / 'frame-320.png')
            write_json(baseline / 'capture.json', dict(files={'frame-320.png': digest(baseline / 'frame-320.png')}))
            Image.fromarray(pixels).save(candidate / 'frame-320.png')
            self.assertTrue(compare_frame(candidate, baseline, 320)['byte_identical'])
            Image.fromarray(pixels).save(candidate / 'frame-320.png', compress_level=0)
            self.assertNotEqual(digest(candidate / 'frame-320.png'), digest(baseline / 'frame-320.png'))
            self.assertTrue(compare_frame(candidate, baseline, 320)['byte_identical'])
            pixels[1, 2, 0] += 1
            Image.fromarray(pixels).save(candidate / 'frame-320.png')
            result = compare_frame(candidate, baseline, 320)
            self.assertFalse(result['byte_identical'])
            self.assertEqual(result['changed_pixels'], 1)
            self.assertEqual(result['max_channel_8bit'], 1)
            self.assertAlmostEqual(result['rms_8bit'], 1 / 6)

    def test_changed_baseline_is_rejected_before_comparison(self):
        with tempfile.TemporaryDirectory() as temporary:
            baseline = Path(temporary)
            Image.new('RGB', (4, 3), (20, 20, 20)).save(baseline / 'frame-320.png')
            write_json(baseline / 'capture.json', dict(files={'frame-320.png': digest(baseline / 'frame-320.png')}))
            Image.new('RGB', (4, 3), (21, 20, 20)).save(baseline / 'frame-320.png')
            with self.assertRaisesRegex(ValueError, 'Baseline capture changed'):
                compare_frame(baseline, baseline, 320)


if __name__ == '__main__':
    unittest.main()
