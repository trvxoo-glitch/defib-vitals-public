import json
import os
import re
import struct
import subprocess
import sys
import tempfile
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import defib_logs  # noqa: E402
import defib_natives  # noqa: E402,F401

F4SE_LOG = """F4SE runtime: initialize (version = 0.7.9 010B0F00 01DB0E1D2A54C6E4, os = 6.2 (9200))
checking plugin Vitals.dll
plugin Vitals.dll (00000001 Vitals 00000100) loaded correctly
checking plugin OldMod.dll
plugin OldMod.dll (00000001 OldMod 00000001) disabled, incompatible with current runtime
"""


def make_session(tmp, vitals_in_process=("Vitals.dll",)):
    f4 = os.path.join(tmp, "F4SE")
    vit = os.path.join(f4, "Vitals")
    os.makedirs(vit)
    open(os.path.join(f4, "f4se.log"), "w").write(F4SE_LOG)
    recs = [
        {"kind": "environment", "game_version": "1.11.240.0", "runtime": "AE", "f4se_version": "0.7.9.0",
         "vitals_version": "0.1.0.0", "address_library_file": "version-1-11-240-0.bin", "address_library_present": True},
        {"kind": "plugin", "file": "Vitals.dll", "in_process": "Vitals.dll" in vitals_in_process, "f4se_registered": True},
        {"kind": "plugin", "file": "OldMod.dll", "in_process": "OldMod.dll" in vitals_in_process},
        {"kind": "fn", "name": "Old::Format", "id": 3, "result": "resolved_no_baseline", "source": "commonlibf4.txt"},
        {"kind": "address", "name": "BSTimer::Singleton", "id": 2703179, "result": "resolved_no_baseline", "source": "commonlibf4.txt"},
        {"kind": "address", "name": "Bad::Fn", "id": 1, "result": "MISMATCH", "source": "port.txt"},
        {"kind": "layout_probe", "probe": "BSTimer.lastTime", "runtime": "AE", "ms_counter_slots": ["0x20"],
         "library_offset": "0x20", "agrees_with_library": True},
    ]
    with open(os.path.join(vit, "Vitals.jsonl"), "w") as f:
        for r in recs:
            f.write(json.dumps(r) + "\n")
        f.write('{"kind":"addr')  # cut-off line, as after a crash
    # breadcrumbs: header + 2 entries
    cap, es = 4, 128
    buf = bytearray(64 + cap * es)
    buf[0:4] = b"VTLB"
    struct.pack_into("<IIIQ", buf, 4, 1, cap, es, 3)
    for seq, port, where in ((1, b"Vitals", b"load"), (2, b"MyPort", b"hook:Update")):
        off = 64 + (seq % cap) * es
        struct.pack_into("<QqII", buf, off, seq, 0, 42, 0)
        buf[off + 24: off + 24 + len(port)] = port
        buf[off + 64: off + 64 + len(where)] = where
    open(os.path.join(vit, "breadcrumbs.bin"), "wb").write(bytes(buf))
    open(os.path.join(tmp, "Fallout4Custom.ini"), "w").write("[Papyrus]\nbEnableLogging=1\nbEnableTrace=1\nbLoadDebugInformation=1\n")
    os.makedirs(os.path.join(tmp, "Logs", "Script"))
    open(os.path.join(tmp, "Logs", "Script", "Papyrus.0.log"), "w").write("[11/11/2026 - 01:00:00AM] error: Cannot call X() on a None object\n")


class BreadcrumbReaderTests(unittest.TestCase):
    """defib_logs.parse_breadcrumbs rejects the files Share::BreadcrumbsText (C++) rejects: a header it cannot trust is
    "no breadcrumbs", never a loop over a made-up capacity (entry size 0 used to run ~4 billion times)."""

    def _file(self, cap, esize, size=None, magic=b"VTLB"):
        n = 64 + cap * esize if size is None else size
        buf = bytearray(max(n, 64))
        buf[0:4] = magic
        struct.pack_into("<IIIQ", buf, 4, 1, cap, esize, 3)
        if n >= 64 + 128:
            struct.pack_into("<QqII", buf, 64, 7, 0, 42, 0)
            buf[88:94] = b"MyPort"
            buf[128:132] = b"hook"
        fd, path = tempfile.mkstemp(suffix=".bin")
        with os.fdopen(fd, "wb") as f:
            f.write(bytes(buf[:n]))
        self.addCleanup(os.remove, path)
        return path

    def test_valid_file_still_reads(self):
        bc = defib_logs.parse_breadcrumbs(self._file(4, 128))
        self.assertEqual([(e[2], e[3]) for e in bc["entries"]], [("MyPort", "hook")])

    def test_unexpected_layouts_are_not_breadcrumbs(self):
        self.assertIsNone(defib_logs.parse_breadcrumbs(self._file(200_000_000, 0, size=192)))  # entry size 0: the old hang
        self.assertIsNone(defib_logs.parse_breadcrumbs(self._file(4, 64)))                    # another entry size
        self.assertIsNone(defib_logs.parse_breadcrumbs(self._file(65537, 128, size=64 + 4 * 128)))  # capacity over the limit
        self.assertIsNone(defib_logs.parse_breadcrumbs(self._file(0, 128, size=192)))         # no entries
        self.assertIsNone(defib_logs.parse_breadcrumbs(self._file(8, 128, size=64 + 4 * 128)))  # truncated: 8 entries claimed, 4 present
        self.assertIsNone(defib_logs.parse_breadcrumbs(self._file(4, 128, magic=b"NOPE")))


class AnalyzerTests(unittest.TestCase):
    def test_agree(self):
        with tempfile.TemporaryDirectory() as tmp:
            make_session(tmp)
            rep = defib_logs.build_report(tmp)
            self.assertIn("AGREE", rep)
            self.assertIn("FAILED `oldmod.dll`", rep)
            self.assertIn("MyPort / hook:Update", rep)
            self.assertIn("MISMATCH", rep)
            self.assertIn("resolved_no_baseline: 2", rep)
            self.assertIn("runtime 1.11.240, F4SE 0.7.9", rep)
            self.assertNotIn("Papyrus logging not fully on", rep)

    def test_disagree(self):
        with tempfile.TemporaryDirectory() as tmp:
            make_session(tmp, vitals_in_process=("Vitals.dll", "OldMod.dll"))
            rep = defib_logs.build_report(tmp)
            self.assertIn("disagree", rep)

    def test_redact(self):
        self.assertEqual(defib_logs.redact(r"C:\Users\Alex\Documents"), r"C:\Users\<user>\Documents")


class GeneratorTests(unittest.TestCase):
    def test_ids(self):
        src = """namespace RE::ID
{
	namespace BSTimer
	{
		inline constexpr REL::VariantID Singleton{ 1256126, 2703179 };
	}
	namespace Inl
	{
		inline constexpr REL::VariantID Gone{ 484974, 0 };  // Inlined
	}
	namespace A { namespace B
	{
		inline constexpr REL::ID Fn{ 1, 2, 3 };
	} }
}
"""
        with tempfile.NamedTemporaryFile("w", suffix=".h", delete=False) as f:
            f.write(src)
        out = subprocess.run([sys.executable, os.path.join(ROOT, "tools", "gen_addresses_from_commonlib.py"), f.name],
                             capture_output=True, text=True).stdout
        os.unlink(f.name)
        self.assertIn("BSTimer::Singleton|ptr|1256126|2703179|2703179|-|-|-", out)
        self.assertIn("A::B::Fn|fn|1|2|3", out)
        self.assertNotIn("Gone", out)


if __name__ == "__main__":
    unittest.main()


class NativesTests(unittest.TestCase):
    def test_stubs_and_diff(self):
        import defib_natives
        recs = [
            {"kind": "papyrus_native", "module": "Mod.dll", "script": "Foo", "function": "Bar", "static": True,
             "params": ["a", "b"], "param_types": ["Int", "String"], "returns": "Bool"},
            {"kind": "papyrus_native", "module": "Mod.dll", "script": "Foo", "function": "Gone", "static": False,
             "params": [], "param_types": [], "returns": "None"},
        ]
        new = [dict(recs[0], returns="Int")]
        with tempfile.TemporaryDirectory() as t:
            a, b = os.path.join(t, "a.jsonl"), os.path.join(t, "b.jsonl")
            open(a, "w").write("\n".join(json.dumps(r) for r in recs))
            open(b, "w").write("\n".join(json.dumps(r) for r in new))
            self.assertEqual(defib_natives.sig(recs[0]), "Bool Function Bar(Int a, String b) native global")
            self.assertEqual(defib_natives.sig(recs[1]), "Function Gone() native")
            import io
            from contextlib import redirect_stdout
            out = io.StringIO()
            with redirect_stdout(out):
                defib_natives.main(["diff", a, b])
            self.assertIn("MISSING  Foo.Gone", out.getvalue())
            self.assertIn("CHANGED  Foo.Bar", out.getvalue())


class CompareAndGateTests(unittest.TestCase):
    def _write(self, path, recs):
        with open(path, "w") as f:
            for r in recs:
                f.write(json.dumps(r) + "\n")

    def test_compare_finds_new_hook_and_plugin(self):
        env = {"kind": "environment", "game_version": "1.11.240.0", "vitals_version": "0.2.3.0"}
        before = [env, {"kind": "plugin", "file": "Vitals.dll", "in_process": True},
                  {"kind": "address", "name": "A::f", "entry_kind": "fn", "checked_as": "fn", "result": "match"},
                  {"kind": "address", "name": "A::data", "entry_kind": "ptr", "checked_as": "ptr", "result": "checked_data", "bytes": "00"}]
        after = [env, {"kind": "plugin", "file": "Vitals.dll", "in_process": True},
                 {"kind": "plugin", "file": "NewMod.dll", "in_process": True},
                 {"kind": "address", "name": "A::f", "entry_kind": "fn", "checked_as": "fn", "result": "hooked",
                  "jump_target": "0x1", "hooked_by": "NewMod.dll"},
                 {"kind": "address", "name": "A::data", "entry_kind": "ptr", "checked_as": "ptr", "result": "checked_data", "bytes": "11"}]
        with tempfile.TemporaryDirectory() as tmp:
            p1, p2 = os.path.join(tmp, "a.jsonl"), os.path.join(tmp, "b.jsonl")
            self._write(p1, before)
            self._write(p2, after)
            rep = defib_logs.compare_sessions(p2, p1)
        self.assertIn("added `NewMod.dll` (loaded)", rep)
        self.assertIn("`NewMod.dll` x1", rep)
        self.assertNotIn("A::data", rep)  # data changes are not reported

    def test_gate_reports_resolution(self):
        v = {"environment": [{"vitals_version": "0.2.3.0"}], "plugin": [],
             "address": [{"result": "match"}, {"result": "unresolved"}]}
        from collections import defaultdict
        dv = defaultdict(list, v)
        gate = dict((g[0], g[1]) for g in defib_logs.stage0_gate(None, dv))
        self.assertFalse(gate["known IDs resolve on this build"])
        self.assertTrue(gate["known IDs match their expected bytes"])


class DoctorReportTests(unittest.TestCase):
    def test_doctor_section_and_history(self):
        with tempfile.TemporaryDirectory() as tmp:
            make_session(tmp)
            vit = os.path.join(tmp, "F4SE", "Vitals")
            with open(os.path.join(vit, "Vitals.jsonl"), "a") as f:
                f.write("\n" + json.dumps({"kind": "doctor_issue", "severity": "problem", "category": "load_order",
                                           "subject": "Patch.esp", "message": "Patch.esp needs the master Base.esm, which is installed but NOT active.",
                                           "advice": "Activate Base.esm."}) + "\n")
                f.write(json.dumps({"kind": "doctor", "problems": 1, "warnings": 0, "issues": 1, "rules": 6}) + "\n")
            rep = defib_logs.build_report(tmp)
            self.assertIn("## Plugin doctor", rep)
            self.assertIn("Doctor: Patch.esp needs the master Base.esm", rep)
            os.makedirs(os.path.join(vit, "history"))
            for name in ("Vitals-20261112-010000.jsonl", "Vitals-20261113-010000.jsonl"):
                with open(os.path.join(vit, "history", name), "w") as f:
                    f.write(json.dumps({"kind": "plugin", "file": "Vitals.dll", "in_process": True}) + "\n")
            out = subprocess.run([sys.executable, os.path.join(ROOT, "tools", "defib_logs.py"), tmp, "--history"],
                                 capture_output=True, text=True, check=True).stdout
            self.assertTrue(out.startswith("1: Vitals-20261113-010000.jsonl"))
            out = subprocess.run([sys.executable, os.path.join(ROOT, "tools", "defib_logs.py"), tmp, "--compare", "2"],
                                 capture_output=True, text=True, check=True).stdout
            self.assertIn("## Plugins", out)


class PreflightTests(unittest.TestCase):
    def _plugin(self, path, masters=(), flags=0, encoding="utf-8"):
        sub = b"".join(b"MAST" + struct.pack("<H", len(m.encode(encoding)) + 1) + m.encode(encoding) + b"\0" + b"DATA" + struct.pack("<H", 8) + b"\0" * 8
                       for m in masters)
        with open(path, "wb") as f:
            f.write(b"TES4" + struct.pack("<II", len(sub), flags) + b"\0" * 12 + sub)

    def test_load_order_and_rules(self):
        import defib_preflight
        with tempfile.TemporaryDirectory() as game:
            data = os.path.join(game, "Data")
            os.makedirs(os.path.join(data, "Interface"))
            os.makedirs(os.path.join(data, "F4SE", "Plugins"))
            self._plugin(os.path.join(data, "Fallout4.esm"), flags=1)
            self._plugin(os.path.join(data, "Base.esm"), ["Fallout4.esm"], flags=1)
            self._plugin(os.path.join(data, "Patch.esp"), ["Fallout4.esm", "Base.esm"])
            self._plugin(os.path.join(data, "Late.esp"), ["Fallout4.esm", "Later.esp"])
            self._plugin(os.path.join(data, "Later.esp"), ["Fallout4.esm"])
            pt = os.path.join(game, "plugins.txt")
            open(pt, "w").write("Base.esm\n*Patch.esp\n*Late.esp\n*Later.esp\n*Gone.esp\n")
            swf = os.path.join(data, "Interface", "MainMenu.swf")
            open(swf, "wb").write(b"FWS")
            os.utime(swf, (1500000000, 1500000000))
            rules = os.path.join(game, "rules.txt")
            open(rules, "w").write("oldfile | Data\\Interface\\MainMenu.swf | 2024-04-25 | problem | old menu\n")
            res = defib_preflight.run(game, pt, "1.11.240", [rules])
            msgs = "\n".join(i["severity"] + ": " + i["message"] for i in res["issues"])
            self.assertIn("problem: Patch.esp needs the master Base.esm, which is installed but NOT active", msgs)
            self.assertIn("problem: Late.esp loads before its master Later.esp", msgs)  # same crash as an inactive master
            self.assertIn("warning: Gone.esp is active", msgs)
            self.assertIn("problem: Data\\Interface\\MainMenu.swf is dated 2017", msgs)
            self.assertIn("# Defib preflight", defib_preflight.to_markdown(res))

    def test_real_load_order_and_master_names(self):
        """Master-flagged plugins load first whatever plugins.txt says; MAST names are UTF-8 when valid (as in game)."""
        import defib_preflight
        with tempfile.TemporaryDirectory() as game:
            data = os.path.join(game, "Data")
            os.makedirs(data)
            self._plugin(os.path.join(data, "Fallout4.esm"), flags=1)
            self._plugin(os.path.join(data, "Normal.esp"), ["Fallout4.esm"])
            self._plugin(os.path.join(data, "Flagged.esp"), ["Fallout4.esm", "Normal.esp"], flags=1)  # ESM flag, needs an .esp
            self._plugin(os.path.join(data, "Caf\u00e9.esm"), ["Fallout4.esm"], flags=1)
            self._plugin(os.path.join(data, "Uses.esp"), ["Caf\u00e9.esm"])                         # UTF-8 MAST
            self._plugin(os.path.join(data, "Old.esp"), ["Caf\u00e9.esm"], encoding="cp1252")         # Windows-1252 MAST
            pt = os.path.join(game, "plugins.txt")
            with open(pt, "w", encoding="utf-8") as f:
                f.write("*Normal.esp\n*Flagged.esp\n*Caf\u00e9.esm\n*Uses.esp\n*Old.esp\n")
            issues, _info = defib_preflight.check_load_order(game, pt)
            msgs = "\n".join(i["severity"] + ": " + i["message"] for i in issues)
            self.assertIn("problem: Flagged.esp loads before its master Normal.esp: Flagged.esp has the master (ESM) flag", msgs)
            self.assertNotIn("Caf", msgs)  # both spellings of the master name find the active plugin
            self.assertEqual(defib_preflight.actual_load_order(1, [True, False, True, True, False]), [0, 2, 3, 1, 4])


class PackageTests(unittest.TestCase):
    def test_fomod_package_builds_and_checks(self):
        import make_package
        with tempfile.TemporaryDirectory() as tmp:
            dll = os.path.join(tmp, "Vitals.dll")
            open(dll, "wb").write(b"MZ")
            out = os.path.join(tmp, "package")
            self.assertEqual(make_package.build(dll, out), 0)
            dev = open(os.path.join(out, "11 Settings - Developer", "F4SE", "Plugins", "Vitals.ini")).read()
            self.assertIn("bPapyrusNatives=1", dev)
            self.assertIn("bLayoutProbes=1", dev)
            player = open(os.path.join(out, "10 Settings - Player", "F4SE", "Plugins", "Vitals.ini")).read()
            self.assertIn("bLayoutProbes=0", player)
            self.assertTrue(os.path.isdir(os.path.join(out, "00 Core", "F4SE", "Plugins", "Vitals", "knowledge")))
            # the small main set stays in the main package (GPL needs LICENSE with the DLL); the rest is the optional file
            for kept in ("README.md", "LICENSE", "EXCEPTIONS", "AI_DISCLOSURE.md", os.path.join("docs", "USER_GUIDE.md"),
                         os.path.join("docs", "PRIVACY_AND_SAFETY.md")):
                self.assertTrue(os.path.isfile(os.path.join(out, "Docs", kept)), kept)
            for moved in ("CHANGELOG.md", os.path.join("docs", "FAQ.md"), os.path.join("docs", "ARCHITECTURE.md")):
                self.assertFalse(os.path.exists(os.path.join(out, "Docs", moved)), moved)
                self.assertTrue(os.path.isfile(os.path.join(out + "-extra", moved)), moved)
            os.remove(os.path.join(out, "00 Core", "F4SE", "Plugins", "Vitals.dll"))
            self.assertEqual(make_package.check(out), 1)

    def test_share_marker_only_in_opt_in_folder(self):
        import make_package
        with tempfile.TemporaryDirectory() as tmp:
            dll = os.path.join(tmp, "Vitals.dll")
            open(dll, "wb").write(b"MZ")
            out = os.path.join(tmp, "package")
            self.assertEqual(make_package.build(dll, out), 0)
            marker = os.path.join(out, "20 Share - Yes", "F4SE", "Plugins", "Vitals", "share-optin.txt")
            self.assertIn("bSharePack=0", open(marker, encoding="utf-8").read())
            self.assertTrue(make_package.share_on(marker))
            no = os.path.join(out, "21 Share - No", "F4SE", "Plugins", "Vitals", "share-optin.txt")
            self.assertFalse(make_package.share_on(no))  # picking No replaces an earlier Yes on reinstall
            # the opt-in is never pre-selected
            cfg = open(os.path.join(out, "fomod", "ModuleConfig.xml"), encoding="utf-8").read()
            self.assertIn("Yes, help improve Vitals", cfg)
            # a marker anywhere else would switch sharing on for everyone
            stray = os.path.join(out, "00 Core", "F4SE", "Plugins", "Vitals", "share-optin.txt")
            open(stray, "w").write("x")
            self.assertEqual(make_package.check(out), 1)
            os.remove(stray)
            self.assertEqual(make_package.check(out), 0)
            cfgp = os.path.join(out, "fomod", "ModuleConfig.xml")
            bad = cfg.replace('<folder source="20 Share - Yes" destination="" />\n\t\t\t\t\t\t\t</files>\n\t\t\t\t\t\t\t<typeDescriptor><type name="Optional" />',
                              '<folder source="20 Share - Yes" destination="" />\n\t\t\t\t\t\t\t</files>\n\t\t\t\t\t\t\t<typeDescriptor><type name="Recommended" />')
            self.assertNotEqual(bad, cfg)
            open(cfgp, "w", encoding="utf-8").write(bad)
            self.assertEqual(make_package.check(out), 1)
            # No must stay the pre-selected default
            bad_no = cfg.replace('<folder source="21 Share - No" destination="" />\n\t\t\t\t\t\t\t</files>\n\t\t\t\t\t\t\t<typeDescriptor><type name="Recommended" />',
                                 '<folder source="21 Share - No" destination="" />\n\t\t\t\t\t\t\t</files>\n\t\t\t\t\t\t\t<typeDescriptor><type name="Optional" />')
            self.assertNotEqual(bad_no, cfg)
            open(cfgp, "w", encoding="utf-8").write(bad_no)
            self.assertEqual(make_package.check(out), 1)
            open(cfgp, "w", encoding="utf-8").write(cfg)
            self.assertEqual(make_package.check(out), 0)


class AgeNoteTests(unittest.TestCase):
    def _dll(self, tmp, ts):
        p = os.path.join(tmp, "X.dll")
        d = bytearray(256)
        d[0:2] = b"MZ"
        struct.pack_into("<I", d, 0x3C, 0x80)
        d[0x80:0x84] = b"PE\0\0"
        struct.pack_into("<I", d, 0x88, ts)
        open(p, "wb").write(bytes(d))
        return p

    def test_before_and_after_next_gen(self):
        import calendar, datetime, defib_preflight
        with tempfile.TemporaryDirectory() as tmp:
            old = calendar.timegm(datetime.datetime(2021, 8, 7).timetuple())
            new = calendar.timegm(datetime.datetime(2025, 3, 1).timetuple())
            self.assertIn("Built (date inside the DLL) 2021-08-07, before the April 2024 Next-Gen update", defib_preflight.age_note(self._dll(tmp, old)))
            self.assertIn("Built (date inside the DLL) 2025-03-01, after the Next-Gen update", defib_preflight.age_note(self._dll(tmp, new)))
            # a reproducible build has a meaningless timestamp: fall back to the file date and say it is weaker
            self.assertIn("Its file is dated", defib_preflight.age_note(self._dll(tmp, 0xDEADBEEF)))
            # the wording does not call a file outdated: the newest file on a mod page may simply lack Next-Gen support
            self.assertNotIn("outdated", defib_preflight.age_note(self._dll(tmp, old)))
            self.assertIn("most likely made for Old-Gen", defib_preflight.age_note(self._dll(tmp, old)))
            # a /Brepro build (debug entry type 16) has a hash for a timestamp: do not trust it
            repro = self._dll(tmp, new)
            d = bytearray(open(repro, "rb").read())
            d = bytearray(d + bytes(0x2000))
            struct.pack_into("<H", d, 0x86, 1)
            struct.pack_into("<H", d, 0x94, 0xF0)
            struct.pack_into("<H", d, 0x98, 0x20B)
            struct.pack_into("<II", d, 0x98 + 160, 0x1000, 28)
            sec = 0x98 + 0xF0
            struct.pack_into("<IIII", d, sec + 8, 0x1000, 0x1000, 0x1000, 0x400)
            struct.pack_into("<I", d, 0x400 + 12, 16)
            open(repro, "wb").write(bytes(d))
            self.assertIn("Its file is dated", defib_preflight.age_note(repro))

    def test_linker_floor_beats_a_fake_old_date(self):
        import calendar, datetime, defib_preflight
        with tempfile.TemporaryDirectory() as tmp:
            old = calendar.timegm(datetime.datetime(2020, 1, 1).timetuple())
            p = self._dll(tmp, old)
            d = bytearray(open(p, "rb").read())
            d[0x98 + 2], d[0x98 + 3] = 14, 44  # linker 14.44: Visual Studio 2022 17.14, May 2025
            open(p, "wb").write(bytes(d))
            os.utime(p, (old, old))  # the file date is old too (an archive can do that)
            note = defib_preflight.age_note(p)
            self.assertIn("linker first released 2025-05-13", note)
            self.assertIn("after the Next-Gen update", note)
            self.assertNotIn("Old-Gen", note)
            # an old linker with an old date: the date stands
            d[0x98 + 3] = 20
            open(p, "wb").write(bytes(d))
            self.assertIn("Built (date inside the DLL) 2020-01-01", defib_preflight.age_note(p))


class LooseSetupTests(unittest.TestCase):
    def test_loose_files_need_ini_settings(self):
        import defib_preflight
        with tempfile.TemporaryDirectory() as tmp:
            game, mg = os.path.join(tmp, "game"), os.path.join(tmp, "mg")
            os.makedirs(os.path.join(game, "Data", "Meshes", "empty"))
            os.makedirs(mg)
            self.assertEqual(defib_preflight.check_loose_setup(game, mg), [])  # empty folders a manager left: nothing loose
            with open(os.path.join(game, "Data", "Meshes", "empty", "x.nif"), "wb") as f:
                f.write(b"nif")
            self.assertEqual(len(defib_preflight.check_loose_setup(game, mg)), 1)  # no INI at all
            open(os.path.join(mg, "Fallout4Custom.ini"), "w").write("[Archive]\nbInvalidateOlderFiles=1\nsResourceDataDirsFinal=\n")
            self.assertEqual(defib_preflight.check_loose_setup(game, mg), [])
            open(os.path.join(mg, "Fallout4Custom.ini"), "w").write("[Archive]\nbInvalidateOlderFiles=1\n")
            self.assertIn("sResourceDataDirsFinal", defib_preflight.check_loose_setup(game, mg)[0]["message"])
            # the game's INI files are UTF-16 with a BOM, as the game and BethINI write them
            with open(os.path.join(mg, "Fallout4Custom.ini"), "wb") as f:
                f.write("[Archive]\r\nbInvalidateOlderFiles=1\r\nsResourceDataDirsFinal=\r\n".encode("utf-16"))
            self.assertEqual(defib_preflight.check_loose_setup(game, mg), [])
            # Fallout4Prefs.ini counts too (Custom wins over it)
            os.remove(os.path.join(mg, "Fallout4Custom.ini"))
            with open(os.path.join(mg, "Fallout4Prefs.ini"), "w") as f:
                f.write("[Archive]\nbInvalidateOlderFiles=1\nsResourceDataDirsFinal=\n")
            self.assertEqual(defib_preflight.check_loose_setup(game, mg), [])
            os.remove(os.path.join(game, "Data", "Meshes", "empty", "x.nif"))
            os.remove(os.path.join(mg, "Fallout4Prefs.ini"))
            self.assertEqual(defib_preflight.check_loose_setup(game, mg), [])  # nothing loose, nothing to warn about


def _ver(text):
    a, b, c = (int(x) for x in text.split("."))
    return (a, b, c)


class LoaderRulesTests(unittest.TestCase):
    """tests/loader_cases.txt: the scanner (and through it the preflight) must agree with Vitals' C++ rules."""

    CASES = os.path.join(ROOT, "tests", "loader_cases.txt")

    def test_scanner_matches_shared_cases(self):
        import f4se_dll_scan as s
        n = 0
        for ln in open(self.CASES, encoding="utf-8"):
            ln = ln.strip()
            if not ln or ln.startswith("#"):
                continue
            f = [x.strip() for x in ln.split("|")]
            era, game, f4se, exports = f[0], _ver(f[1]), None if f[2] == "-" else _ver(f[2]), f[3].split(",")
            vd = None
            if "version" in exports:
                vd = {"dataVersion": int(f[4]), "name": "x" if f[5] == "y" else "", "addressIndependence": int(f[6]),
                      "structureIndependence": int(f[7]),
                      "compatibleVersions": [] if f[8] == "-" else [s.make_version(*_ver(v)) for v in f[8].split(",")],
                      "seVersionRequired": 0 if f[9] == "-" else s.make_version(*_ver(f[9])), "reservedBreaking": int(f[10])}
            status, _, why = s.verdict(game, era, "query" in exports, vd, f4se=f4se, al_present=f[11] == "y", has_any=f[3] != "none")
            self.assertEqual((status, why), (f[12], f[13]), ln)
            n += 1
        self.assertGreater(n, 20)

    def test_cpp_rules(self):
        cxx = next((c for c in ("g++", "clang++") if subprocess.run(["which", c], capture_output=True).returncode == 0), None)
        if not cxx:
            self.skipTest("no C++ compiler")
        with tempfile.TemporaryDirectory() as tmp:
            exe = os.path.join(tmp, "rules_test")
            b = subprocess.run([cxx, "-std=c++20", "-I", os.path.join(ROOT, "include"), os.path.join(ROOT, "tests", "cpp", "rules_test.cpp"), "-o", exe],
                               capture_output=True, text=True)
            if b.returncode != 0 and "c++20" in b.stderr:
                self.skipTest("compiler without C++20")
            self.assertEqual(b.returncode, 0, b.stderr)
            r = subprocess.run([exe, self.CASES], capture_output=True, text=True)
            self.assertEqual(r.returncode, 0, r.stdout + r.stderr)


class ShareInboxTests(unittest.TestCase):
    def _pack(self, folder, n, install, dlls, crash=None):
        import zipfile
        path = os.path.join(folder, f"p{n}.zip")
        with zipfile.ZipFile(path, "w", zipfile.ZIP_STORED) as z:
            z.writestr("manifest.json", json.dumps({"format": "vitals-share 1", "install_id": install, "runtime": "AE", "game_version": "1.11.240"}))
            lines = [json.dumps({"kind": "doctor_issue", "severity": "warning", "category": "plugin_dll", "subject": d, "reason": "no_version_data"}) for d in dlls]
            z.writestr("Vitals.jsonl", "\n".join(lines) + "\n{\"kind\": cut off")
            z.writestr("dll-sources.txt", "header\ndll | mod folder\n" + "".join(f"{d} | Mod-{d}-1\n" for d in dlls))
            z.writestr("papyrus-summary.txt", '  3x  Unbound native function "GetVersion" in object "SUP_F4SE" -- x\n')
            if crash:
                z.writestr("crash/crash-1.log", crash)
        return path

    def test_summary_counts_installs_and_finds_candidates(self):
        import share_inbox
        with tempfile.TemporaryDirectory() as tmp:
            crash = 'Unhandled exception "EXCEPTION_ACCESS_VIOLATION" at 0x7FF6 Fallout4.exe+2a3b4c5\n\nPROBABLE CALL STACK:\n\t[0] 0x1 X.dll+0001234\n'
            for i in range(6):
                self._pack(tmp, i, f"{i:032x}", ["NewThing.dll", "XDI.dll"], crash if i < 2 else None)
            self._pack(tmp, 9, f"{0:032x}", ["NewThing.dll"])  # same install again: counted once
            open(os.path.join(tmp, "junk.zip"), "wb").write(b"not a zip")
            text, info = share_inbox.summarize(tmp, min_installs=5)
            self.assertEqual(info["installs"], 6)
            self.assertEqual(info["packs"], 7)
            self.assertIn("NewThing.dll", info["candidates"])
            self.assertNotIn("XDI.dll", info["candidates"])  # defib-census.txt has a rule for it
            self.assertIn("| Fallout4.exe+2A3B4C5 | 2 |", text)
            self.assertIn("| X.dll | 2 |", text)
            self.assertIn("| SUP_F4SE.GetVersion | 6 |", text)
            self.assertIn("Mod-NewThing.dll-1", text)


class DiscordPullTests(unittest.TestCase):
    def test_pull_discord_downloads_zips_once(self):
        import share_inbox
        calls = []

        def fake(path, token, binary=False):
            calls.append(path)
            self.assertEqual(token, "tok")
            if path == "/guilds/G/channels":
                return [{"id": "1", "type": 0, "parent_id": "CAT"}, {"id": "2", "type": 0, "parent_id": "OTHER"}, {"id": "3", "type": 4}]
            if path.startswith("/channels/1/messages"):
                return [{"id": "10", "attachments": [{"id": "5", "filename": "Vitals-Share.zip", "size": 100, "url": "http://x/a.zip"},
                                                     {"id": "6", "filename": "notes.txt", "size": 5, "url": "http://x/n.txt"},
                                                     {"id": "7", "filename": "../../evil.zip", "size": 5, "url": "http://x/e.zip"}]}]
            return b"PK"
        orig = share_inbox._discord
        share_inbox._discord = fake
        try:
            with tempfile.TemporaryDirectory() as tmp:
                self.assertEqual(share_inbox.pull_discord(tmp, "tok", "G", "CAT"), 2)
                self.assertEqual(sorted(os.listdir(tmp)), ["10_5-Vitals-Share.zip", "10_7-.._.._evil.zip"])
                self.assertEqual(share_inbox.pull_discord(tmp, "tok", "G", "CAT"), 0)  # nothing new the second time
            self.assertFalse(any("/channels/2/" in c for c in calls))  # only the chosen category is read
        finally:
            share_inbox._discord = orig

    def test_category_is_required(self):
        import share_inbox
        env = {k: os.environ.pop(k) for k in ("VITALS_DISCORD_CATEGORY",) if k in os.environ}
        try:
            with tempfile.TemporaryDirectory() as tmp, self.assertRaises(SystemExit):
                share_inbox.pull_discord(tmp, "tok", "G", "")
        finally:
            os.environ.update(env)

    def test_token_only_goes_to_the_api(self):
        import share_inbox
        import urllib.request
        seen = []

        class R:
            def __init__(self, req):
                seen.append((req.full_url, req.get_header("Authorization")))

            def __enter__(self):
                return self

            def __exit__(self, *a):
                return False

            def read(self):
                return b"[]"
        orig = urllib.request.urlopen
        urllib.request.urlopen = lambda req, timeout=0: R(req)
        try:
            share_inbox._discord("/guilds/G/channels", "tok")
            share_inbox._discord("https://cdn.discordapp.com/attachments/1/2/Vitals-Share.zip", "tok", binary=True)
        finally:
            urllib.request.urlopen = orig
        self.assertEqual(seen[0][1], "Bot tok")
        self.assertIsNone(seen[1][1])
        # a link that is not Discord's file server is never fetched, with or without the token
        for bad in ("https://evil.example/x.zip", "http://cdn.discordapp.com/x.zip", "https://cdn.discordapp.com.evil.example/x.zip"):
            with self.assertRaises(SystemExit):
                share_inbox._discord(bad, "tok", binary=True)

    def test_zip_bomb_is_skipped(self):
        import share_inbox
        import zipfile
        with tempfile.TemporaryDirectory() as tmp:
            p = os.path.join(tmp, "bomb.zip")
            with zipfile.ZipFile(p, "w", zipfile.ZIP_DEFLATED) as z:
                z.writestr("manifest.json", '{"format":"vitals-share 1"}')
                z.writestr("Vitals.jsonl", b"\0" * (20 << 20))
            self.assertIsNone(share_inbox.read_pack(p))


class PreflightEdgeTests(unittest.TestCase):
    def test_rule_runtime_tags(self):
        import defib_preflight
        with tempfile.TemporaryDirectory() as tmp:
            game = os.path.join(tmp, "game")
            os.makedirs(os.path.join(game, "Data", "Interface"))
            swf = os.path.join(game, "Data", "Interface", "MainMenu.swf")
            open(swf, "w").write("x")
            os.utime(swf, (1577836800, 1577836800))  # 2020
            rules = os.path.join(tmp, "r.txt")
            open(rules, "w").write("oldfile@NG,AE | Data\\Interface\\MainMenu.swf | 2024-04-25 | problem | old menu\n")
            r = defib_preflight.load_rules([rules])
            self.assertEqual(r[0]["kind"], "oldfile")
            self.assertEqual(len(defib_preflight.check_rules(game, r, "1.11.240", [])), 1)
            self.assertEqual(len(defib_preflight.check_rules(game, r, "1.10.984", [])), 1)
            self.assertEqual(len(defib_preflight.check_rules(game, r, "1.10.163", [])), 0)
            os.utime(swf, (315532800, 315532800))  # 1980-01-01, an archive with no real date
            self.assertEqual(len(defib_preflight.check_rules(game, r, "1.11.240", [])), 0)

    def test_bad_rule_date_is_skipped(self):
        import defib_preflight
        with tempfile.TemporaryDirectory() as game:
            os.makedirs(os.path.join(game, "Data", "Interface"))
            open(os.path.join(game, "Data", "Interface", "X.swf"), "wb").write(b"FWS")
            rules = [{"kind": "oldfile", "match": "Data\\Interface\\X.swf", "extra": "2024/04/25", "severity": "problem", "message": "m"}]
            self.assertEqual(defib_preflight.check_rules(game, rules, "1.11.240", []), [])

    def test_windows_1252_plugin_names(self):
        import defib_preflight
        with tempfile.TemporaryDirectory() as game:
            data = os.path.join(game, "Data")
            os.makedirs(data)
            name = "Café.esp"
            PreflightTests._plugin(None, os.path.join(data, "Fallout4.esm"), flags=1)
            PreflightTests._plugin(None, os.path.join(data, name), ["Fallout4.esm"])
            pt = os.path.join(game, "plugins.txt")
            open(pt, "wb").write(b"*Caf\xe9.esp\n")  # written in Windows-1252, as the game does
            issues, info = defib_preflight.check_load_order(game, pt)
            self.assertEqual([i for i in issues if i["severity"] != "info"], [], issues)
            self.assertIn("2 active plugins", info[0])

    def test_ini_values_with_comments(self):
        import defib_preflight
        with tempfile.TemporaryDirectory() as tmp:
            game, mg = os.path.join(tmp, "game"), os.path.join(tmp, "mg")
            os.makedirs(os.path.join(game, "Data", "Textures"))
            os.makedirs(mg)
            open(os.path.join(mg, "Fallout4Custom.ini"), "w").write("[Archive]\nbInvalidateOlderFiles=1 ; BethINI\nsResourceDataDirsFinal= ; empty\n")
            self.assertEqual(defib_preflight.check_loose_setup(game, mg), [])

    def test_unloadable_dll_is_a_warning_not_a_crash(self):
        import defib_preflight
        with tempfile.TemporaryDirectory() as game:
            plugins = os.path.join(game, "Data", "F4SE", "Plugins")
            os.makedirs(plugins)
            open(os.path.join(plugins, "version-1-11-240-0.bin"), "wb").write(b"")
            open(os.path.join(plugins, "Old.dll"), "wb").write(_pe_with_exports(["F4SEPlugin_Query", "F4SEPlugin_Load"]))
            issues, _ = defib_preflight.check_dlls(game, "1.11.240")
            self.assertEqual(len(issues), 1)
            self.assertEqual(issues[0]["severity"], "warning")
            self.assertIn("Query only", issues[0]["message"])


def _pe_with_exports(names):
    """A minimal 64-bit PE with one section holding an export table (enough for f4se_dll_scan)."""
    d = bytearray(0x400)
    d[0:2] = b"MZ"
    struct.pack_into("<I", d, 0x3C, 0x80)
    d[0x80:0x84] = b"PE\0\0"
    struct.pack_into("<HH", d, 0x84, 0x8664, 1)
    struct.pack_into("<I", d, 0x88, 1500000000)
    struct.pack_into("<H", d, 0x94, 0xF0)
    opt = 0x98
    struct.pack_into("<H", d, opt, 0x20B)
    sec_rva, sec_raw = 0x1000, 0x200
    struct.pack_into("<II", d, opt + 112, sec_rva, 0x100)
    sh = opt + 0xF0
    d[sh:sh + 8] = b".edata\0\0"
    struct.pack_into("<IIII", d, sh + 8, 0x200, sec_rva, 0x200, sec_raw)
    n = len(names)
    funcs, nameptrs, ords, strs = 0x40, 0x60, 0x80, 0xA0
    struct.pack_into("<IIIII", d, sec_raw + 20, n, n, sec_rva + funcs, sec_rva + nameptrs, sec_rva + ords)
    o = strs
    for i, nm in enumerate(sorted(names)):
        struct.pack_into("<I", d, sec_raw + funcs + 4 * i, sec_rva + 0x10)
        struct.pack_into("<I", d, sec_raw + nameptrs + 4 * i, sec_rva + o)
        struct.pack_into("<H", d, sec_raw + ords + 2 * i, i)
        d[sec_raw + o:sec_raw + o + len(nm)] = nm.encode()
        o += len(nm) + 1
    return bytes(d)


class ThirdReviewTests(unittest.TestCase):
    def test_package_refuses_dangerous_output(self):
        import make_package
        for bad in (ROOT, os.path.dirname(ROOT), os.path.expanduser("~")):
            with self.assertRaises(SystemExit):
                make_package.safe_output_dir(bad)
        with tempfile.TemporaryDirectory() as tmp:
            open(os.path.join(tmp, "notes.txt"), "w").close()
            with self.assertRaises(SystemExit):  # a non-empty folder that is not an earlier package
                make_package.safe_output_dir(tmp)
            self.assertTrue(make_package.safe_output_dir(os.path.join(tmp, "package")))

    def test_scanner_reports_damaged_dll(self):
        import f4se_dll_scan
        with tempfile.TemporaryDirectory() as tmp:
            p = os.path.join(tmp, "Broken.dll")
            good = _pe_with_exports(["F4SEPlugin_Load"])
            open(p, "wb").write(good[:0x120])  # cut inside the section table
            r = f4se_dll_scan.scan_file(p)
            self.assertIn("error", r)

    def test_scanner_survives_pe_offset_near_4gb(self):
        import f4se_dll_scan
        with tempfile.TemporaryDirectory() as tmp:
            for off in (0xFFFFFFF0, 0xFFFFFFFF, 0x7FFFFFFF):
                p = os.path.join(tmp, "Crafted.dll")
                d = bytearray(_pe_with_exports(["F4SEPlugin_Load"]))
                struct.pack_into("<I", d, 0x3C, off)
                with open(p, "wb") as f:
                    f.write(bytes(d))
                self.assertIn("error", f4se_dll_scan.scan_file(p))

    def test_shipped_address_lists_have_hex_expected_values(self):
        """A non-hex expected value would be reported as bad_expected_value in game: catch it here instead."""
        adir = os.path.join(ROOT, "dist", "Data", "F4SE", "Plugins", "Vitals", "addresses")
        bad = []
        for name in os.listdir(adir):
            if not name.endswith(".txt"):
                continue
            for n, ln in enumerate(open(os.path.join(adir, name), encoding="utf-8"), 1):
                f = [x.strip() for x in ln.split("|")]
                if ln.startswith("#") or len(f) < 8:
                    continue
                for v in f[5:8]:
                    v = "".join(v.split())
                    if v and v != "-" and (len(v) % 2 or not re.fullmatch(r"(?:[0-9A-Fa-f]{2}|\?\?)+", v)):
                        bad.append(f"{name}:{n}: {v}")
        self.assertEqual(bad, [])

    def test_report_says_when_address_library_is_unusable(self):
        with tempfile.TemporaryDirectory() as tmp:
            make_session(tmp)
            with open(os.path.join(tmp, "F4SE", "Vitals", "Vitals.jsonl"), "a") as f:
                f.write("\n" + json.dumps({"kind": "address_check_unavailable", "address_library_file": "version-1-10-163-0.bin",
                                           "state": "truncated", "problem": "the file is cut short"}) + "\n")
            rep = defib_logs.build_report(tmp)
            self.assertIn("SKIPPED: Address Library `version-1-10-163-0.bin` is not usable (the file is cut short)", rep)

    def test_generator_keeps_bytes_and_kinds(self):
        src = "namespace RE::ID\n{\n\tnamespace A\n\t{\n\t\tinline constexpr REL::ID Fn{ 1, 2, 3 };\n\t\tinline constexpr REL::ID Glob{ 4, 5, 6 };\n\t\tinline constexpr REL::ID Moved{ 7, 8, 10 };\n\t}\n}\n"
        old = "# header\nA::Fn|fn|1|2|3|-|-|4889\nA::Glob|data|4|5|6|-|-|-\nA::Moved|fn|7|8|9|-|-|C3\n"
        with tempfile.TemporaryDirectory() as tmp:
            h, k = os.path.join(tmp, "IDs.h"), os.path.join(tmp, "old.txt")
            open(h, "w").write(src)
            open(k, "w").write(old)
            out = subprocess.run([sys.executable, os.path.join(ROOT, "tools", "gen_addresses_from_commonlib.py"), h, "--keep", k],
                                 capture_output=True, text=True).stdout
        self.assertIn("A::Fn|fn|1|2|3|-|-|4889", out)
        self.assertIn("A::Glob|data|4|5|6|-|-|-", out)
        self.assertIn("A::Moved|fn|7|8|10|-|-|-", out)  # the ID changed: old bytes dropped


class KnowledgeRulesTests(unittest.TestCase):
    KNOW = os.path.join(ROOT, "dist", "Data", "F4SE", "Plugins", "Vitals", "knowledge")

    def test_shipped_rules_parse_and_are_unique(self):
        import defib_preflight
        files = [os.path.join(self.KNOW, f) for f in os.listdir(self.KNOW) if f.endswith(".txt")]
        self.assertIn("defib-census.txt", [os.path.basename(f) for f in files])
        rules = defib_preflight.load_rules(files)
        self.assertGreaterEqual(len(rules), 16)
        kinds = {"dll", "needs", "conflict", "oldfile", "file", "dllbuild", "patchedby"}
        for r in rules:
            self.assertIn(r["kind"], kinds, r)
            self.assertIn(r["severity"], ("problem", "warning", "info"), r)
            if r["kind"] == "oldfile":
                defib_preflight.dt.date.fromisoformat(r["extra"])
            if r["kind"] == "dll":
                self.assertIn(r["extra"], ("refused", "-"), r)

    def test_newest_generated_file_wins(self):
        import defib_preflight
        with tempfile.TemporaryDirectory() as tmp:
            def write(name, header, msg):
                p = os.path.join(tmp, name)
                open(p, "w").write(header + f"dll | X.dll | refused | warning | {msg}\n")
                return p
            old = write("defib-census.txt", "# Generated: 2026-11-17\n", "census")
            new = write("nexus-rules.txt", "# Generated on 2026-12-01\n", "nexus")
            same = write("a-same-day.txt", "# generated: 2026-11-17\n", "sameday")
            mine = write("zz-mine.txt", "# my own rules\n", "mine")
            self.assertEqual(defib_preflight.generated_date(["# Generated by defib-census", "# Generated: 2026-13-40"]), None)
            self.assertEqual(defib_preflight.generated_date(["dll | Generated: 2026-11-17 | - | info | m"]), None)
            # no date first (name order), then newest first, equal dates in name order: the same order as Rules::RuleFileOrder
            self.assertEqual([os.path.basename(p) for p in defib_preflight.rule_file_order([old, new, same, mine])],
                             ["zz-mine.txt", "nexus-rules.txt", "a-same-day.txt", "defib-census.txt"])
            self.assertEqual(defib_preflight.load_rules([old, new])[0]["message"], "nexus")
            self.assertEqual(defib_preflight.load_rules([old, new, mine])[0]["message"], "mine")
            self.assertEqual(defib_preflight.load_rules([old, same])[0]["message"], "sameday")

    def test_dllbuild_rule_matches_one_plugin_version(self):
        import defib_preflight
        from unittest import mock
        self.assertEqual(defib_preflight.packed_version_text((4 << 4) | 13), "0.0.4.13")
        self.assertTrue(defib_preflight.dll_build_matches("0.0.4.13", "0.0.4.13"))
        self.assertFalse(defib_preflight.dll_build_matches("0.0.4.13", "0.0.4.12"))
        self.assertTrue(defib_preflight.dll_build_matches("0.0.4", "0.0.4.13"))
        self.assertFalse(defib_preflight.dll_build_matches("", "1.0.0.0") or defib_preflight.dll_build_matches("1..", "1.0.0.0"))
        with tempfile.TemporaryDirectory() as game:
            plugins = os.path.join(game, "Data", "F4SE", "Plugins")
            os.makedirs(plugins)
            open(os.path.join(plugins, "Sup.dll"), "wb").write(b"MZ")
            rules = [{"kind": "dllbuild", "eras": None, "match": "Sup.dll", "extra": "0.0.4.13", "severity": "warning", "message": "m"}]
            with mock.patch.object(defib_preflight.f4se_dll_scan, "scan_file", return_value={"f4seExports": [], "versionData": {"pluginVersion": (4 << 4) | 13}}):
                self.assertEqual(len(defib_preflight.check_rules(game, rules, "1.11.240", [])), 1)
                self.assertEqual(defib_preflight.check_rules(game, rules, "1.11.240", [{"category": "plugin_dll", "subject": "Sup.dll"}]), [])  # not running
            with mock.patch.object(defib_preflight.f4se_dll_scan, "scan_file", return_value={"f4seExports": [], "versionData": {"pluginVersion": (4 << 4) | 12}}):
                self.assertEqual(defib_preflight.check_rules(game, rules, "1.11.240", []), [])

    def test_same_rule_in_two_files_is_used_once(self):
        import defib_preflight
        with tempfile.TemporaryDirectory() as tmp:
            line = "dll | X.dll | refused | warning | msg\n"
            a, b = os.path.join(tmp, "a.txt"), os.path.join(tmp, "b.txt")
            open(a, "w").write(line)
            open(b, "w").write(line.replace("X.dll", "x.DLL"))
            self.assertEqual(len(defib_preflight.load_rules([a, b])), 1)
