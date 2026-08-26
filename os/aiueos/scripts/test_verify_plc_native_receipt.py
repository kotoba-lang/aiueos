import importlib.util
import pathlib
import unittest


PATH = pathlib.Path(__file__).with_name("verify-plc-native-receipt.py")
SPEC = importlib.util.spec_from_file_location("verify_plc_native_receipt", PATH)
verifier = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(verifier)


DIGEST = "a" * 64


def qualified():
    return {
        "format": "aiueos-plc-native-receipt/v1",
        "profile": "aiueos-plc-v1",
        "target": "x86_64-aiueos-user-v1",
        "source_language": "iec-61131-3-structured-text-subset-v1",
        "runtime_linux": False,
        "runtime_jvm": False,
        "runtime_gc": False,
        "dynamic_allocation": False,
        "capabilities": [16, 17, 18, 19],
        "scan": {"input_snapshot": True,
                 "shadow_outputs": True,
                 "atomic_commit": True,
                 "safe_state_on_failure": True},
        "st_source_sha256": DIGEST,
        "generated_kotoba_sha256": DIGEST,
        "native_elf_sha256": DIGEST,
        "compiler_commit": "b" * 40,
        "rt_kernel_receipt_sha256": DIGEST,
        "rt_kernel_artifact_sha256": DIGEST,
        "io_map_sha256": DIGEST,
        "admission_analysis_sha256": DIGEST,
        "timing": {"priority": 5, "cycle_us": 10000,
                   "deadline_us": 8000, "budget_us": 1000, "wcet_us": 700},
        "deployment_ready": True,
    }


class PlcReceiptTest(unittest.TestCase):
    def test_fully_bound_deployment_passes(self):
        self.assertEqual([], verifier.violations(qualified()))

    def test_unbound_build_receipt_is_not_deployable(self):
        receipt = qualified()
        receipt["deployment_ready"] = False
        receipt["rt_kernel_receipt_sha256"] = None
        receipt["rt_kernel_artifact_sha256"] = None
        failures = verifier.violations(receipt)
        self.assertIn("deployment_ready", failures)
        self.assertIn("rt_kernel_receipt_sha256", failures)
        self.assertIn("rt_kernel_artifact_sha256", failures)

    def test_linux_jvm_gc_or_dynamic_allocation_is_refused(self):
        for key in ("runtime_linux", "runtime_jvm", "runtime_gc", "dynamic_allocation"):
            receipt = qualified()
            receipt[key] = True
            self.assertIn(key, verifier.violations(receipt))

    def test_nontransactional_output_is_refused(self):
        for key in ("input_snapshot", "shadow_outputs", "atomic_commit",
                    "safe_state_on_failure"):
            receipt = qualified()
            receipt["scan"][key] = False
            self.assertIn("scan." + key, verifier.violations(receipt))

    def test_capability_surface_is_exact(self):
        receipt = qualified()
        receipt["capabilities"].append(20)
        self.assertIn("capabilities", verifier.violations(receipt))

    def test_invalid_timing_envelope_is_refused(self):
        receipt = qualified()
        receipt["timing"]["wcet_us"] = 1001
        self.assertIn("timing.envelope", verifier.violations(receipt))

        receipt = qualified()
        receipt["timing"]["deadline_us"] = 10001
        self.assertIn("timing.envelope", verifier.violations(receipt))


if __name__ == "__main__":
    unittest.main()
