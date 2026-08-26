import importlib.util
import json
import pathlib
import unittest


PATH = pathlib.Path(__file__).with_name("make-plc-native-receipt.py")
SPEC = importlib.util.spec_from_file_location("make_plc_native_receipt", PATH)
maker = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(maker)


class PlcDeploymentInputTest(unittest.TestCase):
    def test_shipped_motor_map_and_admission_are_valid(self):
        examples = PATH.parents[1] / "plc" / "examples"
        io_map = json.loads((examples / "motor-io-map.json").read_text())
        admission = json.loads((examples / "motor-admission.json").read_text())
        frontend_path = PATH.with_name("compile-plc-st.py")
        frontend = maker.load_frontend(frontend_path)
        source = (examples / "motor.st").read_text()
        _, inputs, outputs, _ = frontend.Parser(source).parse()
        maker.validate_io_map(io_map, inputs, outputs)
        duplicate_sink = {**io_map, "outputs": [dict(item) for item in io_map["outputs"]]}
        duplicate_sink["outputs"][1]["sink"] = duplicate_sink["outputs"][0]["sink"]
        with self.assertRaisesRegex(ValueError, "sinks"):
            maker.validate_io_map(duplicate_sink, inputs, outputs)
        self.assertEqual(5, maker.validate_admission(admission)["priority"])

    def test_io_map_requires_exact_indices_and_safe_outputs(self):
        valid = {"format": "aiueos-plc-io-map/v1",
                 "inputs": [{"index": 0}, {"index": 1}],
                 "outputs": [{"index": 0, "safe_value": 0},
                             {"index": 1, "safe_value": -1}]}
        maker.validate_io_map(valid, 2, 2)
        invalid = {**valid, "outputs": [{"index": 0, "safe_value": 0}]}
        with self.assertRaisesRegex(ValueError, "output map"):
            maker.validate_io_map(invalid, 2, 2)
        invalid = {**valid, "outputs": [{"index": 0},
                                         {"index": 1, "safe_value": 0}]}
        with self.assertRaisesRegex(ValueError, "safe value"):
            maker.validate_io_map(invalid, 2, 2)

    def test_admission_requires_nested_timing_envelope(self):
        valid = {"format": "aiueos-plc-admission/v1", "priority": 5,
                 "cycle_us": 10000, "deadline_us": 8000,
                 "budget_us": 1000, "wcet_us": 700}
        self.assertEqual(700, maker.validate_admission(valid)["wcet_us"])
        for key, value in (("wcet_us", 1001), ("budget_us", 8001),
                           ("deadline_us", 10001)):
            invalid = {**valid, key: value}
            with self.assertRaisesRegex(ValueError, "WCET"):
                maker.validate_admission(invalid)


if __name__ == "__main__":
    unittest.main()
