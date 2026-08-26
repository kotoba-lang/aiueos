import importlib.util
import pathlib
import unittest


path = pathlib.Path(__file__).with_name("verify-rt-kernel-receipt.py")
spec = importlib.util.spec_from_file_location("verify_rt_kernel_receipt", path)
verifier = importlib.util.module_from_spec(spec)
spec.loader.exec_module(verifier)


DIGEST = "a" * 64


def qualified():
    return {
        "format": "aiueos-kotoba-native-rt-receipt/v1",
        "target": "x86_64-aiueos-rt-kernel-v1",
        "timing_profile": "hard-real-time",
        "rtos_qualified": True,
        "runtime_linux": False,
        "runtime_jvm": False,
        "runtime_gc": False,
        "hosted_adapters": False,
        "c_sources": [],
        "foreign_objects": [],
        "imports": [],
        "dynamic_dependencies": [],
        "scheduler": {"policy": "fixed-priority-preemptive", "cores": 1,
                      "priority_inversion": "priority-ceiling",
                      "admission": "response-time-analysis"},
        "memory": {"dynamic_allocation_after_start": False,
                   "page_faults_after_start": False},
        "amu_rt_subset_version": 1,
        "artifact_sha256": DIGEST,
        "measured_artifact_sha256": DIGEST,
        "admission_analysis_sha256": DIGEST,
        "measurement": {"physical_hardware": True,
                        "max_interrupt_latency_us": 40,
                        "max_dispatch_latency_us": 60,
                        "max_timer_jitter_us": 8,
                        "wcet_max_us": 400},
        "requirements": {"max_interrupt_latency_us": 50,
                         "max_dispatch_latency_us": 75,
                         "max_timer_jitter_us": 10,
                         "task_budget_us": 500},
    }


class ReceiptTest(unittest.TestCase):
    def test_qualified_native_receipt_passes(self):
        self.assertEqual([], verifier.violations(qualified()))

    def test_existing_best_effort_receipt_cannot_be_relabeled(self):
        receipt = qualified()
        receipt.update({"target": "x86_64-aiueos-kernel-v1",
                        "timing_profile": "best-effort",
                        "rtos_qualified": False})
        failures = verifier.violations(receipt)
        self.assertIn("target", failures)
        self.assertIn("timing_profile", failures)
        self.assertIn("rtos_qualified", failures)

    def test_linux_or_jvm_is_refused(self):
        for key in ("runtime_linux", "runtime_jvm", "runtime_gc",
                    "hosted_adapters"):
            receipt = qualified()
            receipt[key] = True
            self.assertIn(key, verifier.violations(receipt))

    def test_measurement_must_bind_the_exact_kernel(self):
        receipt = qualified()
        receipt["measured_artifact_sha256"] = "b" * 64
        self.assertIn("measurement_artifact_binding", verifier.violations(receipt))


if __name__ == "__main__":
    unittest.main()
