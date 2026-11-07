import subprocess
import sys

import pytest


def _server(tmp_path, body):
    script = tmp_path / "account_server.py"
    script.write_text(body, encoding="utf-8")
    return [sys.executable, str(script)]


def test_reads_both_account_endpoints_after_handshake_and_handles_out_of_order_replies(tmp_path):
    from usage_agent import codex_rpc

    command = _server(tmp_path, '''
import json, sys
initialized = False
requests = []
for line in sys.stdin:
    row = json.loads(line)
    method = row.get("method")
    if method == "initialize":
        print(json.dumps({"id": row["id"], "result": {"userAgent": "test"}}), flush=True)
    elif method == "initialized":
        initialized = True
    else:
        assert initialized
        assert method in ("account/rateLimits/read", "account/usage/read")
        requests.append(row)
        if len(requests) == 2:
            print(json.dumps({"method": "account/rateLimits/updated", "params": {}}), flush=True)
            for request in reversed(requests):
                value = ({"summary": {"lifetimeTokens": 5150490649}}
                         if request["method"] == "account/usage/read" else {"rateLimits": {}})
                print(json.dumps({"id": request["id"], "result": value}), flush=True)
''')
    result = codex_rpc.read_account(command, timeout_s=3)
    assert result["usage"]["summary"]["lifetimeTokens"] == 5_150_490_649
    assert result["limits"] == {"rateLimits": {}}


def test_rpc_error_in_one_metric_preserves_the_other_metric(tmp_path):
    from usage_agent import codex_rpc

    command = _server(tmp_path, '''
import json, sys
for line in sys.stdin:
    row = json.loads(line)
    if "id" not in row: continue
    if row["method"] == "initialize": result = {"result": {}}
    elif row["method"] == "account/usage/read": result = {"result": {"summary": {"lifetimeTokens": 123}}}
    else: result = {"error": {"code": -1, "message": "private diagnostic data"}}
    print(json.dumps({"id": row["id"], **result}), flush=True)
''')
    result = codex_rpc.read_account(command, timeout_s=3)
    assert result == {"limits": None, "usage": {"summary": {"lifetimeTokens": 123}}}


def test_timeout_stops_the_owned_app_server(tmp_path, monkeypatch):
    from usage_agent import codex_rpc

    command = _server(tmp_path, "import time; time.sleep(10)\n")
    processes = []
    real_popen = subprocess.Popen

    def start(*args, **kwargs):
        process = real_popen(*args, **kwargs)
        processes.append(process)
        return process

    monkeypatch.setattr(codex_rpc.subprocess, "Popen", start)
    with pytest.raises(codex_rpc.CodexRPCError):
        codex_rpc.read_account(command, timeout_s=0.3)
    assert len(processes) == 1
    assert processes[0].poll() is not None
